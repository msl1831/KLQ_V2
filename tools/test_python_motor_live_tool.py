"""Host-only motor live-tool fault tests. Never creates a Serial or real HostLink."""
import ast,json,struct,sys,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import test_python_motor_live as live

UID=struct.pack('<III',0x4150354c,0x38323609,0x00d86c62)

class Clock:
    def __init__(self):self.now=10.
    def monotonic(self):return self.now
    def sleep(self,n):self.now+=n

class Device:
    def __init__(self,clock,scenario):
        self.clock=clock;self.scenario=scenario;self.calls=[];self.write_timeout=1
        self.state=0;self.valid=0;self.error=0;self.run_count=0;self.test_entered=False
        self.run_at=0.;self.raw=b'';self.remote_count=0;self.restored=False;self.fired=set();self.short_count=0;self.ready_info_count=0
    @property
    def write_timeout(self):return self._write_timeout
    @write_timeout.setter
    def write_timeout(self,value):
        if value==1 and getattr(self,'test_entered',False) and self.scenario=='timeout_restore_fail':raise OSError('timeout reconfigure fault')
        self._write_timeout=value
    def add(self,kind,cmd,payload=b'',**kw):
        self.calls.append(dict(kind=kind,cmd=cmd,payload=payload,pc=self.clock.now,**kw))
    def info(self):
        return (f'KLQ ROBOT FW 0.13; APP=0x08008000; MAX=489472; PAGE=2048; VALID=1; '
                f'USER_STATE={self.state}; USER_VALID={self.valid}; UI=1; PY_ERROR={self.error}; PY_LINE=0; '
                f'PK_SHORT={self.short_count}; EXT_MOTOR=1; EXT_MOTOR_STATUS=1; EXT_VOICE=1').encode()
    def sample(self):
        if not self.test_entered or self.run_count>1 or self.state!=3:
            return dict(state=live.IDLE,online=True,goal=0,ack_pwm=0)
        elapsed=self.clock.now-self.run_at
        if self.scenario=='missed' and elapsed>.10:
            return dict(state=live.IDLE,online=True,goal=0,ack_pwm=0)
        if self.scenario=='pending' and .25<=elapsed<.31:
            return dict(state=live.PENDING,online=True,goal=1000,ack_pwm=1000)
        return dict(state=live.ACTIVE,online=True,goal=1000,ack_pwm=1000)

class FakeUSB:
    def __init__(self,device,timeout=.5):self.device=device;self.timeout=timeout
    def request(self,cmd,payload=b'',retries=3):
        d=self.device;d.clock.now+=.005
        d.add('usb',cmd,payload,retries=retries,timeout=self.timeout,write_timeout=d.write_timeout)
        if cmd==1:
            if d.scenario in ('pc4','pc4_two') and d.state==2 and d.valid and not d.test_entered and b'motor_power(3)' in d.raw:
                d.ready_info_count+=1
                if d.ready_info_count==3:
                    d.short_count=2 if d.scenario=='pc4_two' else 1;d.state=3;d.test_entered=True;d.run_count+=1;d.run_at=d.clock.now
                    d.add('pc4','SHORT_PRESS')
            return d.info()
        if cmd==0x50:return b'voice'
        if cmd==0x55:return d.sample()
        if cmd==0x53:
            if d.test_entered and d.scenario=='disarm_fail' and 'arm' not in d.fired:
                d.fired.add('arm');raise OSError('cleanup disarm fault')
            return b''
        if cmd==2:
            d.state=1;d.valid=0;d.raw=b'';return b''
        if cmd==3:
            offset=struct.unpack_from('<I',payload)[0];d.raw+=payload[4:]
            return struct.pack('<I',offset+len(payload)-4)
        if cmd==4:d.state=2;d.valid=1;d.error=0;return b''
        if cmd==5:
            d.calls[-1]['is_power_restore']=b'motor_power(2)' in d.raw
            d.run_count+=1
            if b'motor_power(2)' in d.raw:
                d.restored=True;d.state=2;return b''
            d.state=3;d.test_entered=True;d.run_at=d.clock.now
            if d.scenario in ('run_fail','stop_fail','disarm_fail','remote_stop_fail','interrupt','timeout_restore_fail'):
                if d.scenario=='interrupt':raise KeyboardInterrupt()
                raise OSError('RUN transport fault')
            return b''
        if cmd==9:
            if d.scenario=='stop_fail' and d.test_entered and 'stop' not in d.fired:
                d.fired.add('stop');raise OSError('cleanup STOP fault')
            d.state=0 if d.state==1 else 2;return b''
        raise AssertionError('unexpected USB command '+str(cmd))

class FakeRemote:
    def __init__(self,device,port,kind):self.device=device;self.port=port;assert kind=='motor'
    def __enter__(self):
        self.device.remote_count+=1;self.number=self.device.remote_count;self.device.add('remote','OPEN');return self
    def __exit__(self,*args):self.device.add('remote','CLOSE')
    def request(self,cmd,payload=b''):
        d=self.device;d.add('remote',cmd,payload)
        if cmd==1:
            b=bytearray(36);struct.pack_into('<H',b,0,1);b[2:4]=bytes((1,2));struct.pack_into('<HHH',b,4,2,2,2)
            struct.pack_into('<I',b,12,0x00020000);b[24:]=UID if d.scenario!='wrong_uid' else bytes(12)
            return bytes(b)
        if cmd==0x302:
            if d.scenario=='remote_stop_fail' and self.number==2:raise OSError('diagnostic STOP fault')
            return b''
        if cmd==0x301:return struct.pack('<IhBB',100,0,0,0)
        raise AssertionError('unexpected remote command '+str(cmd))

class LiveToolTests(unittest.TestCase):
    def run_tool(self,scenario,case='program_stop',expected=Exception,start_key=False):
        clock=Clock();device=Device(clock,scenario);directory=ROOT/'build/python_motor_tool_tests'/self._testMethodName
        report=directory/'report.json';directory.mkdir(parents=True,exist_ok=True)
        def decoded(m):return [dict(index=i+1,**m) for i in range(3)]
        with patch.object(live,'USBLink',FakeUSB),patch.object(live,'HostLink',FakeRemote),\
             patch.object(live,'decode_motors',decoded),patch.object(live,'decode_voice',lambda _:dict(enabled=0,active=0)),\
             patch.object(live,'time',clock),patch.object(live.serial,'Serial',side_effect=AssertionError('No hardware allowed')):
            if expected is None:result=live.exercise(device,2,UID,case,report,start_key=start_key)
            else:
                with self.assertRaises(expected):live.exercise(device,2,UID,case,report,start_key=start_key)
                result=json.loads(report.read_text(encoding='utf-8'))
        return device,result
    def test_wrong_identity_never_runs_downloads_or_sends_set(self):
        d,r=self.run_tool('wrong_uid',expected=ValueError)
        self.assertFalse(any(c['kind']=='usb' and c['cmd'] in (2,3,4,5) for c in d.calls))
        self.assertFalse(any(c['cmd']==0x300 for c in d.calls));self.assertEqual(r['result'],'FAIL')
    def test_run_transport_failure_still_stops_restores_power_and_clears_ram(self):
        d,r=self.run_tool('run_fail',expected=OSError)
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertTrue(r['default_power_restored']);self.assertTrue(r['cleanup_pass'])
        self.assertTrue(d.restored);self.assertEqual((d.state,d.valid),(0,0));self.assertEqual(d.write_timeout,1)
    def test_disarm_failure_does_not_skip_stop_or_ram_cleanup(self):
        d,r=self.run_tool('disarm_fail',expected=OSError)
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertTrue(d.restored);self.assertEqual((d.state,d.valid),(0,0))
        self.assertTrue(any(x.startswith('disarm:') for x in r['cleanup_errors']));self.assertFalse(r['cleanup_pass'])
    def test_usb_stop_failure_does_not_skip_diagnostic_stop(self):
        d,r=self.run_tool('stop_fail',expected=OSError)
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertTrue(d.restored);self.assertEqual((d.state,d.valid),(0,0))
        self.assertTrue(any(x.startswith('stop:') for x in r['cleanup_errors']))
    def test_timeout_reconfiguration_failure_does_not_abort_cleanup(self):
        d,r=self.run_tool('timeout_restore_fail',expected=OSError)
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertTrue(d.restored);self.assertEqual((d.state,d.valid),(0,0))
        self.assertTrue(any(x.startswith('write_timeout_restore:') for x in r['cleanup_errors']))
    def test_unconfirmed_diagnostic_stop_skips_power_program_but_clears_ram(self):
        d,r=self.run_tool('remote_stop_fail',expected=OSError)
        self.assertFalse(d.restored);self.assertFalse(r.get('default_power_restored',False));self.assertEqual((d.state,d.valid),(0,0))
        self.assertTrue(any(x.startswith('motorSTOP:') for x in r['cleanup_errors']))
    def test_keyboard_interrupt_still_attempts_layered_cleanup(self):
        d,r=self.run_tool('interrupt',expected=KeyboardInterrupt)
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertTrue(d.restored);self.assertEqual((d.state,d.valid),(0,0))
    def test_stuck_output_hits_independent_guard_before_overall_timeout(self):
        d,r=self.run_tool('stuck',expected=TimeoutError)
        self.assertTrue(r['active_guard_stop_ack']);self.assertLess(r['active_guard_fired']-r['run_sent'],1.35)
        fast=[c for c in d.calls if c['kind']=='usb' and c['cmd'] in (1,0x55,9) and c['timeout']==.12]
        self.assertTrue(fast);self.assertTrue(all(c['retries']==1 and c['write_timeout']==.12 for c in fast))
        self.assertTrue(d.restored)
    def test_late_usb_stop_is_not_counted_as_active_interruption(self):
        _,r=self.run_tool('missed',case='usb_stop',expected=AssertionError)
        self.assertIn('usb_stop_missed_active',r);self.assertNotIn('usb_stop_sent',r);self.assertEqual(r['result'],'FAIL')
    def test_active_usb_stop_requires_current_active_snapshot_and_waits_pending(self):
        d,r=self.run_tool('pending',case='usb_stop',expected=None)
        self.assertEqual(r['motor_before_usb_stop']['state'],live.ACTIVE)
        self.assertEqual((r['motor_before_usb_stop']['goal'],r['motor_before_usb_stop']['ack_pwm']),(1000,1000))
        self.assertTrue(r['normal_stop_confirmed_before_diagnostic']);self.assertTrue(d.restored)
        opens=[c for c in d.calls if c['cmd']=='OPEN'];self.assertEqual(len(opens),2)
        self.assertGreater(opens[-1]['pc'],r['finished_at']+.25)
    def test_pc4_start_requires_one_shortpress_without_usb_run_of_test(self):
        d,r=self.run_tool('pc4',case='usb_stop',expected=None,start_key=True)
        self.assertEqual(r['start_mode'],'physicalPC4');self.assertEqual(r['pc4_short_delta'],1)
        self.assertTrue(r['normal_stop_confirmed_before_diagnostic']);self.assertTrue(r['cleanup_pass'])
        run_commands=[c for c in d.calls if c['kind']=='usb' and c['cmd']==5]
        self.assertEqual(len(run_commands),1);self.assertTrue(run_commands[0]['is_power_restore'])
        self.assertEqual(sum(c['kind']=='pc4' for c in d.calls),1)
    def test_pc4_wait_timeout_never_usb_runs_test_and_still_cleans_up(self):
        d,r=self.run_tool('pc4_timeout',expected=TimeoutError,start_key=True)
        self.assertIn('PC4 start not detected',r['error']);self.assertEqual(r['start_mode'],'physicalPC4')
        self.assertFalse(d.test_entered);self.assertTrue(d.restored);self.assertTrue(r['cleanup_pass'])
        run_commands=[c for c in d.calls if c['kind']=='usb' and c['cmd']==5]
        self.assertEqual(len(run_commands),1);self.assertTrue(run_commands[0]['is_power_restore'])
        self.assertEqual((d.state,d.valid),(0,0))
    def test_pc4_multiple_press_delta_is_rejected_and_stopped(self):
        d,r=self.run_tool('pc4_two',expected=AssertionError,start_key=True)
        self.assertEqual(r['pc4_short_delta'],2);self.assertEqual(r['result'],'FAIL');self.assertTrue(r['cleanup_pass'])
        self.assertTrue(r['cleanup_motor']['stop_ack']);self.assertEqual((d.state,d.valid),(0,0))
    def test_generated_sources_have_no_loops_and_finite_waits(self):
        for case in live.CASES:
            with self.subTest(case=case):
                tree=ast.parse(live.source_for(case,2));self.assertFalse(any(isinstance(n,(ast.While,ast.For)) for n in ast.walk(tree)))
                for n in ast.walk(tree):
                    if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr in ('wait','motor_for'):
                        value=ast.literal_eval(n.args[-1]);self.assertGreaterEqual(value,0);self.assertLessEqual(value,2)
if __name__=='__main__':unittest.main()
