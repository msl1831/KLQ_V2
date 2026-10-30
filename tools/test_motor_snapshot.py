"""Pure host motor-cache decoding, compatibility, single-reader and UI tests."""
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import status_dashboard as dashboard
from test_status_dashboard_connection import FakeDevice,FakeFactory,worker,until,reply

HOST=b"KLQ ROBOT FW 0.13; APP=0x08008000; MAX=489472; PAGE=2048; VALID=1; EXT_MOTOR=1; EXT_MOTOR_STATUS=1"
OLD=HOST.replace(b"0.13",b"0.12").replace(b"; EXT_MOTOR_STATUS=1",b"")
ROWS=((0,1,0,0),(2,1,-1000,-1000),(3,0,0,666))
def body(rows=ROWS):return bytes([1])+b"".join(struct.pack("<BBhh",*r) for r in rows)
def snapshot(m):return json.loads(m.snapshot())

class AutoDevice(FakeDevice):
    def write(self,data):
        count=super().write(data);seq,cmd=struct.unpack_from('<IH',data,4)
        self.factory.commands.append(cmd)
        if cmd==1:self.inject(reply(self.factory.host,seq=seq))
        elif cmd==0x55:self.inject(reply(body(),seq=seq,command=0x8055,status=self.factory.motor_status))
        else:raise AssertionError('mock saw unexpected control command '+hex(cmd))
        return count
class AutoFactory(FakeFactory):
    def __init__(self,host=HOST,motor_status=0):
        super().__init__();self.host=host;self.motor_status=motor_status;self.commands=[]
    def __call__(self,port,*args,**kwargs):
        if port!='FAKE':raise AssertionError('real device forbidden')
        with self.lock:
            self.active+=1;self.maximum=max(self.maximum,self.active);self.created+=1
            self.current=AutoDevice(self);return self.current

class MotorSnapshotTests(unittest.TestCase):
    def test_decode_signed_fields_and_port_order(self):
        p=dashboard.decode_motors(body())
        self.assertEqual(p[1],dict(index=2,state=2,online=True,goal=-1000,ack_pwm=-1000))
        self.assertEqual(p[2],dict(index=3,state=3,online=False,goal=0,ack_pwm=666))
        self.assertEqual([v['index'] for v in p],[1,2,3])

    def test_invalid_abi_length_and_field_ranges(self):
        for data in (b'',body()[:-1],body()+b'0',bytes([2])+body()[1:]):
            with self.assertRaises(ValueError):dashboard.decode_motors(data)
        for row in ((5,1,0,0),(2,2,0,0),(2,1,1001,0),(2,1,0,-1001)):
            for index in range(3):
                rows=list(ROWS);rows[index]=row
                with self.assertRaises(ValueError):dashboard.decode_motors(body(rows))

    def test_matching_crc_response_only_and_invalid_payload_clears_old_data(self):
        m=dashboard.Monitor('FAKE');m.parse_info(struct.pack('<I',0)+HOST);m.motor_seq=22
        frame=reply(body(),seq=22,command=0x8055);bad=bytearray(frame);bad[-1]^=1
        for data in (bytes(bad),reply(body(),seq=23,command=0x8055),frame[:-1]):
            m.consume(bytearray(data));self.assertFalse(m.state['motors']['ports'])
        m.consume(bytearray(frame));s=snapshot(m);self.assertEqual(s['motors']['ports'][1]['goal'],-1000)
        self.assertGreater(s['motors']['updated'],0);self.assertIsNone(m.motor_seq)
        m.parse_motors(struct.pack('<I',0)+body()[:-1]);s=snapshot(m)
        self.assertEqual(s['motors']['ports'],[]);self.assertEqual(s['motors']['updated'],0);self.assertTrue(s['motors']['error'])

    def test_old_firmware_and_disconnect_clear_snapshot(self):
        m=dashboard.Monitor('FAKE');m.parse_info(struct.pack('<I',0)+HOST);m.parse_motors(struct.pack('<I',0)+body())
        m.parse_info(struct.pack('<I',0)+OLD);self.assertEqual(m.state['motors'],dashboard.blank_motors())
        m.parse_info(struct.pack('<I',0)+HOST);m.parse_motors(struct.pack('<I',0)+body());m.motor_seq=9
        m.set_connection(False,'FAKE','lost');self.assertEqual(m.state['motors'],dashboard.blank_motors());self.assertIsNone(m.motor_seq)

    def test_unknown_command_disables_until_reconnect(self):
        m=dashboard.Monitor('FAKE');m.parse_info(struct.pack('<I',0)+HOST)
        m.parse_motors(struct.pack('<I',1));self.assertTrue(m.motor_rejected);self.assertFalse(m.state['motors']['supported'])
        m.parse_info(struct.pack('<I',0)+HOST);self.assertFalse(m.state['motors']['supported'])
        m.set_connection(False,'FAKE','lost');m.parse_info(struct.pack('<I',0)+HOST)
        self.assertTrue(m.state['motors']['supported']);self.assertFalse(m.motor_rejected)

    def test_one_reader_polls_new_firmware_and_never_sends_controls(self):
        factory=AutoFactory()
        with worker(factory) as m:
            until(lambda:snapshot(m)['motors']['updated']>0)
            first=snapshot(m)['motors']['updated'];until(lambda:snapshot(m)['motors']['updated']>first)
            self.assertEqual(snapshot(m)['motors']['ports'][1]['ack_pwm'],-1000)
            self.assertTrue(set(factory.commands)<={1,0x55});self.assertGreaterEqual(factory.commands.count(0x55),2)
            self.assertEqual(m.pause_reader(timeout=1),{'paused':True,'released':True});self.assertEqual(factory.active,0)
        self.assertEqual(factory.maximum,1)

    def test_legacy_never_queried_and_unknown_queried_once(self):
        for host,code,want in ((OLD,0,0),(HOST,1,1)):
            factory=AutoFactory(host,code)
            with worker(factory) as m:
                until(lambda:snapshot(m)['connected'])
                if want:until(lambda:m.motor_rejected)
                time.sleep(.55)
                self.assertEqual(factory.commands.count(0x55),want);self.assertEqual(snapshot(m)['motors']['ports'],[])
            self.assertEqual(factory.maximum,1)

    def test_render_goal_last_ack_stage_and_stale_unknown(self):
        node=shutil.which('node')
        self.assertIsNotNone(node,'Node required for actual dashboardJS render test')
        js=dashboard.HTML.split('<script>',1)[1].split('</script>',1)[0]
        js=js.replace('setInterval(update,250);update();','')
        s=dashboard.blank_state('FAKE');s.update(server_time=100,connected=True)
        s['ports'][0].update(online=True,**{'class':1},type=2,left=11,right=22)
        s['ports'][1].update(online=True,**{'class':2},type=2)
        s['motors']=dict(supported=True,updated=100,error='',ports=dashboard.decode_motors(body()))
        pre="const nodes={};global.document={getElementById(id){return nodes[id]||(nodes[id]={style:{},classList:{toggle(){}},previousElementSibling:{},querySelector(){return this}})}};"
        tail="const sample="+json.dumps(s)+";render(sample);console.log(JSON.stringify([nodes.pd2.innerHTML,nodes.pk2.textContent,nodes.ps2.textContent,nodes.pd1.innerHTML]));sample.server_time=103;render(sample);console.log(JSON.stringify([nodes.pd2.innerHTML,nodes.pk2.textContent,nodes.ps2.textContent]));"
        result=subprocess.run([node,'-e',pre+js+tail],capture_output=True,encoding='utf-8',timeout=10)
        self.assertEqual(result.returncode,0,result.stderr);fresh,stale=map(json.loads,result.stdout.splitlines())
        self.assertIn('目标 -1000',fresh[0]);self.assertIn('最后ACK -1000',fresh[0]);self.assertEqual(fresh[1],'-1000');self.assertEqual(fresh[2],'运行已ACK')
        self.assertIn('左 11 / 右 22',fresh[3]);self.assertIn('无有效电机快照',stale[0]);self.assertEqual(stale[1:],['--','--'])

if __name__=='__main__':unittest.main(verbosity=2)
