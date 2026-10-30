"""Run bounded Python motor blocks through main USB; no Flash writes.

Power3 is100%; sources are finite, with an additional1.2s PC nonzero-segment guard.
Requires EXT_MOTOR_STATUS=1 and an exact expected motor UID. Overwrites test RAM.
Normal motor evidence uses read-only0x55; diagnostic OPEN is used only before/after.
The PC guard is not an electrical watchdog; blocked OS/USB calls can delay it.
"""
import argparse,binascii,datetime,json,struct,sys,time
from pathlib import Path
import serial
from serial.tools import list_ports
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'sensor_boot/tools'))
from host_link import USBLink,HostLink
from status_dashboard import decode_motors
from test_motor_live import uid_bytes,status as decode_device_status
from voice_usb import decode as decode_voice
IDLE,PENDING,ACTIVE,STOPPING,ERROR=range(5)
CASES=('timed','continuous','program_stop','natural_end','runtime_error','usb_stop')

def source_for(case,port):
    lines=['import klq','klq.motor_power(3)','klq.wait(2)']
    if case=='timed':
        lines += ['klq.display_number(1)',f'klq.motor_for({port}, 1, 0.8)','klq.display_number(0)','klq.wait(0.6)',
                  'klq.display_number(2)',f'klq.motor_for({port}, -1, 0.8)','klq.display_number(0)','klq.wait(0.6)']
    elif case=='continuous':
        for direction in (1,-1):
            lines += [f'klq.display_number({1 if direction==1 else 2})',f'klq.motor_run({port}, {direction})','klq.wait(0.8)',
                      f'klq.motor_stop({port})','klq.display_number(0)','klq.wait(0.6)']
    elif case=='program_stop':
        lines += [f'klq.motor_run({port}, 1)','klq.wait(0.4)','klq.stop()','klq.missing()']
    elif case=='natural_end':
        lines += [f'klq.motor_run({port}, 1)','klq.wait(0.4)']
    elif case=='runtime_error':
        lines += [f'klq.motor_run({port}, 1)','klq.wait(0.4)',f'klq.motor_run({port}, 0)','klq.wait(0.2)']
    elif case=='usb_stop':
        lines += [f'klq.motor_run({port}, 1)','klq.wait(1)',f'klq.motor_stop({port})','klq.wait(0.4)']
    else:raise ValueError('unknown case')
    return '\n'.join(lines)+'\n'


def fields(text):
    return {x.strip().split('=',1)[0]:x.strip().split('=',1)[1] for x in text.split(';') if '=' in x}


def exercise(device,port,uid,case,report,start_key=False):
    rec={'started_at':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
         'case':case,'sensor_port':port,'uid':uid.hex(),'power_level':3,'pwm':1000,'source':source_for(case,port),
         'result':'STARTED','samples':[],'events':[],
         'evidence_scope':'normal cached UART ACK/goal and PC timings; no encoder or electrical stop-latency measurement'}
    def save():report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(rec,indent=2)+'\n',encoding='utf-8')
    running=False
    def request(command,payload=b''):
        return link.request(command,payload,retries=1 if running else 3)
    def info(label):
        text=request(1).decode('ascii');rec['events'].append({'label':label,'pc':time.monotonic(),'info':text});return fields(text)
    def motor():
        m=decode_motors(request(0x55))[port-1];m['pc']=time.monotonic();rec['samples'].append(m);return m
    def download(raw,wait=True):
        request(2,struct.pack('<II',len(raw),binascii.crc32(raw)))
        for at in range(0,len(raw),1024):
            chunk=raw[at:at+1024];assert request(3,struct.pack('<I',at)+chunk)==struct.pack('<I',at+len(chunk))
        request(4)
        if wait:
            deadline=time.monotonic()+3
            while True:
                f=info('download_ready')
                if f['USER_STATE']=='2' and f['UI']=='1':break
                if time.monotonic()>deadline:raise TimeoutError('READY not reached')
                time.sleep(.02)
    def direct_stop(label):
        with HostLink(device,port,'motor') as remote:
            identity=remote.request(1)
            if (len(identity)!=36 or identity[2:4]!=bytes((1,2)) or struct.unpack_from('<HHH',identity,4)!=(2,2,2)
                    or struct.unpack_from('<I',identity,12)[0]>>16!=2 or identity[24:]!=uid):raise ValueError('motor identity mismatch')
            rec[label]={'identity_hex':identity.hex(),'stop_ack':False}
            assert remote.request(0x302)==b'';rec[label]['stop_ack']=True
            s=decode_device_status(remote.request(0x301));assert s['pwm']==0 and s['active']==0
            rec[label]['status']=s
    failure=None;verified=False;link=USBLink(device);old_write_timeout=getattr(device,'write_timeout',1)
    try:
        f=info('before');assert 'EXT_MOTOR_STATUS=1' in rec['events'][-1]['info'] and f['USER_STATE']=='0' and f['USER_VALID']=='0'
        link.request(0x53,bytes((0,)));v=decode_voice(link.request(0x50));assert not v['enabled'] and not v['active']
        direct_stop('preflight');verified=True;time.sleep(.7)
        download(rec['source'].encode('ascii'));deadline=time.monotonic()+3
        while True:
            m=motor()
            if m['online'] and m['state']==IDLE and m['ack_pwm']==0 and m['goal']==0:break
            if time.monotonic()>deadline:raise TimeoutError('Motor not idle after discovery')
            time.sleep(.02)
        running=True;link.timeout=.12;device.write_timeout=.12
        rec.update(rpc_timeout_seconds=.12,active_guard_seconds=1.2)
        if start_key:
            rec['start_mode']='physicalPC4';before_short=int(f.get('PK_SHORT','0'));deadline=time.monotonic()+60
            print('READY_PC4: downloaded, shortpressPC4 once to start timedsequence.',flush=True)
            while True:
                f=info('waiting_for_PC4')
                if f['USER_STATE']=='3':break
                if f['USER_STATE']!='2' or time.monotonic()>deadline:raise TimeoutError('PC4 start not detected')
                time.sleep(.05)
            rec['pc4_short_delta']=int(f['PK_SHORT'])-before_short;assert rec['pc4_short_delta']==1
        else:
            rec['start_mode']='USB_RUN';request(5)
        rec['run_sent']=time.monotonic();end=time.monotonic()+8;seen_running=start_key;active_started=None;nonzero_started=None;last_info=0
        while True:
            m=motor();now=time.monotonic()
            if m['state']==ERROR or not m['online']:raise RuntimeError('Motor driver fault/offline')
            if m['goal'] or m['ack_pwm']:
                if nonzero_started is None:nonzero_started=now
                elif now-nonzero_started>=1.2:
                    rec['active_guard_snapshot']=dict(m);rec['active_guard_fired']=now
                    try:request(9);rec['active_guard_stop_ack']=time.monotonic()
                    except Exception as e:rec['active_guard_stop_error']=repr(e)
                    raise TimeoutError('Continuous nonzero motor segment exceeded1.2s PC guard')
            else:nonzero_started=None
            if m['state']==ACTIVE and m['ack_pwm']==1000 and active_started is None:active_started=now
            if case=='usb_stop' and active_started is not None and now-active_started>=.25 and 'usb_stop_sent' not in rec:
                if m['state']==ACTIVE and m['goal']==m['ack_pwm']==1000:
                    rec['motor_before_usb_stop']=dict(m);rec['usb_stop_sent']=now
                    request(9);rec['usb_stop_ack']=time.monotonic()
                elif not m['goal'] or not m['ack_pwm'] or m['state']==STOPPING:
                    rec['usb_stop_missed_active']=dict(m)
                    raise AssertionError('USB STOP did not catch an active motor interval')
            if now-last_info>=.08:
                f=info('running');last_info=now;seen_running|=f['USER_STATE']=='3'
                expected=4 if case=='runtime_error' else 0
                if int(f['PY_ERROR']) not in (0,expected):raise AssertionError('Unexpected Python error '+str(f))
            m['user_state']=f['USER_STATE']
            if seen_running and f['USER_STATE']=='2' and m['state']==IDLE and m['goal']==0 and m['ack_pwm']==0:break
            if now>end:raise TimeoutError('Program or motor stop not completed')
            time.sleep(.01)
        rec['finished_at']=time.monotonic();rec['host_after']=f
        assert f['USER_VALID']=='1' and int(f['PY_ERROR'])==(4 if case=='runtime_error' else 0)
        if case=='runtime_error':assert int(f['PY_LINE'])==rec['source'].splitlines().index(f'klq.motor_run({port}, 0)')+1
        observed={m['ack_pwm'] for m in rec['samples'] if m['state']==ACTIVE}
        assert {1000,-1000}<=observed if case in ('timed','continuous') else 1000 in observed
        if case in ('timed','continuous'):
            pos=max(i for i,m in enumerate(rec['samples']) if m['state']==ACTIVE and m['ack_pwm']==1000)
            neg=min(i for i,m in enumerate(rec['samples']) if m['state']==ACTIVE and m['ack_pwm']==-1000)
            assert any(m['state']==IDLE and m['goal']==m['ack_pwm']==0 and m.get('user_state')=='3' for m in rec['samples'][pos+1:neg])
            rec['intermediate_normal_stop_while_running']=True
        for pwm in (1000,-1000) if case in ('timed','continuous') else (1000,):
            samples=[m for m in rec['samples'] if m['state']==ACTIVE and m['ack_pwm']==pwm]
            span=samples[-1]['pc']-samples[0]['pc'];rec.setdefault('active_ack_spans',{})[str(pwm)]=span
            if case in ('timed','continuous'):assert span>.35
        if case=='usb_stop':assert rec['usb_stop_ack']<rec['finished_at'] and rec['finished_at']-active_started<.9
        # Observe stopped state before any diagnostic OPEN; it must remain stopped.
        stopped_until=time.monotonic()+.3
        while time.monotonic()<stopped_until:
            m=motor();assert m['online'] and m['state']==IDLE and m['goal']==m['ack_pwm']==0;time.sleep(.02)
        rec['normal_stop_confirmed_before_diagnostic']=True
        rec['result']='PASS'
    except BaseException as e:
        failure=e;rec.update(result='FAIL',error=repr(e))
    finally:
        running=False;link.timeout=.5;errors=[]
        try:device.write_timeout=old_write_timeout
        except Exception as e:errors.append('write_timeout_restore: '+repr(e))
        for label,action in [('disarm',lambda:link.request(0x53,bytes((0,)))),('stop',lambda:link.request(9))]:
            try:action()
            except Exception as e:errors.append(label+': '+repr(e))
        if verified:
            try:direct_stop('cleanup_motor')
            except Exception as e:errors.append('motorSTOP: '+repr(e))
            if rec.get('cleanup_motor',{}).get('stop_ack'):
                try:
                    download(b'import klq\nklq.motor_power(2)\npass\n');request(5)
                    deadline=time.monotonic()+3
                    while True:
                        f=info('restore_default_power')
                        if f['USER_STATE']=='2' and f['PY_ERROR']=='0':break
                        if time.monotonic()>deadline:raise TimeoutError('Default power restore program not completed')
                        time.sleep(.02)
                    rec['default_power_restored']=True
                except Exception as e:errors.append('powerrestore: '+repr(e))
            else:errors.append('powerrestore: skipped because motorSTOP ACK unconfirmed')
            try:
                request(2,struct.pack('<II',1,0));request(9)
                f=info('cleanup');assert f['USER_STATE']=='0' and f['USER_VALID']=='0' and f['PY_ERROR']=='0'
                v=decode_voice(request(0x50));assert not v['enabled'] and not v['active'];rec['cleanup_pass']=not errors
            except Exception as e:errors.append('RAMcleanup: '+repr(e))
        if errors:
            rec['cleanup_errors']=errors;rec['result']='FAIL';failure=failure or RuntimeError('; '.join(errors))
        save()
    if failure:raise failure
    return rec


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--sensor-port',type=int,choices=(1,2,3),required=True)
    p.add_argument('--start-key',action='store_true',help='wait <=60s forPC4; automatic cleanup clears testRAM onexit; use retainedexample forhuman replay');p.add_argument('--uid',required=True);p.add_argument('--case',choices=CASES,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    uid=uid_bytes(a.uid);ports=[a.port] if a.port else [x.device for x in list_ports.comports() if (x.vid,x.pid)==(0x314b,0x0108)]
    if len(ports)!=1:raise ValueError('Specify unique main USB --port')
    with serial.Serial(ports[0],115200,timeout=.01,write_timeout=1) as device:
        device.reset_input_buffer();r=exercise(device,a.sensor_port,uid,a.case,a.report,a.start_key)
    print('PASS '+a.case+'; normalACK stop confirmed beforediagnostic, ARM0/emptyRAM.',flush=True)
if __name__=='__main__':main()
