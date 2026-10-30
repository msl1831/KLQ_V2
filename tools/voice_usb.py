"""KLQ UART4 voice diagnostics through native main USB; motion remains off unless explicitly armed."""
import argparse,json,struct,sys,time,datetime
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'sensor_boot/tools'))
from host_link import USBLink,USBError
STATUS,QUERY,PLAY,ARM,CANCEL=range(0x50,0x55)
def decode(b):
 if len(b)!=108 or b[0]!=1:raise ValueError('voice status ABI mismatch')
 s=dict(abi=b[0],enabled=b[1],version_valid=b[2],awake=b[3],version='.'.join(str(n) for n in b[4:7]),play_state=b[7],request=b[8],last_ack=b[9],last_error=b[10],last_cmd=b[11],last_seq=b[12],active=b[13],port=b[14],reason=b[15])
 names=['semantic','rx_frames','bad_frames','uart_errors','rx_overflows','tx_errors','timeouts','accepted','rejected','completed','errors']
 for name,off in zip(names,range(16,60,4)):s[name]=struct.unpack_from('<I',b,off)[0]
 s['events']=list(struct.unpack_from('<5H',b,60))
 for name,off in [('recognitions',70),('duplicates',72),('dropped',74),('play_acks',80),('play_done',82)]:s[name]=struct.unpack_from('<H',b,off)[0]
 for name,off in [('play_id',76),('rx_bytes',84),('tx_frames',88),('last_rx_ms',92),('cancelled',100)]:s[name]=struct.unpack_from('<I',b,off)[0]
 s.update(score=b[96],intent_seq=b[97],phase=b[98],pending_sound=struct.unpack_from('<H',b,104)[0],pwm=struct.unpack_from('<h',b,106)[0]);return s
class Probe:
 def __init__(self,link,record,report):self.link,self.record,self.report=link,record,report
 def save(self):self.report.parent.mkdir(parents=True,exist_ok=True);self.report.write_text(json.dumps(self.record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 def status(self):
  s=decode(self.link.request(STATUS));s['pc_monotonic']=round(time.monotonic(),6);self.record['samples'].append(s);self.save();return s
 def wait(self,predicate,seconds=5):
  end=time.monotonic()+seconds
  while True:
   s=self.status()
   if predicate(s):return s
   if time.monotonic()>=end:raise TimeoutError('voice status condition not confirmed')
   time.sleep(.05)
 def idle(self):return self.wait(lambda s:not s['request'] and s['play_state']==0)
 def version(self):
  before=self.idle();self.link.request(QUERY)
  after=self.wait(lambda s:not s['request'] and s['version_valid'] and s['rx_frames']>before['rx_frames'])
  if after['version']!='0.2.0' or after['timeouts']!=before['timeouts'] or after['last_error']:raise ValueError('CI version exchange failed')
  self.record['version_exchange']=dict(before=before,after=after);self.save();return after
 def play(self,n):
  before=self.idle();self.link.request(PLAY,struct.pack('<I',n))
  after=self.wait(lambda s:((s['play_acks']-before['play_acks'])&65535)>0 and ((s['play_done']-before['play_done'])&65535)>0 and ((s['events'][3]-before['events'][3])&65535)>0 and ((s['events'][4]-before['events'][4])&65535)>0 and s['last_ack']==128 and s['play_id']==n and s['request']==0 and s['play_state']==0 and not s['pending_sound'] and s['recognitions']==before['recognitions'])
  if after['timeouts']!=before['timeouts'] or after['last_error']:raise ValueError('sound request error')
  self.record.setdefault('sounds',[]).append(dict(id=n,before=before,after=after));self.save();return after

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--report',type=Path,default=Path('build/voice_usb.json'))
 sub=p.add_subparsers(dest='action',required=True);sub.add_parser('status');sub.add_parser('version');sub.add_parser('sounds')
 q=sub.add_parser('play');q.add_argument('id',type=int,choices=range(5000,5008));q=sub.add_parser('arm');q.add_argument('enable',type=int,choices=(0,1));sub.add_parser('cancel');q=sub.add_parser('watch');q.add_argument('--seconds',type=float,default=30)
 a=p.parse_args();rec=dict(started_at=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),action=a.action,samples=[],result='STARTED')
 try:
  import serial
  from serial.tools import list_ports
  ports=[x.device for x in list_ports.comports() if (x.vid,x.pid)==(0x314b,0x0108)] if not a.port else [a.port]
  if len(ports)!=1:raise ValueError('specify main USB --port; matches '+str(ports))
  if a.action=='watch' and not 0<a.seconds<=120:raise ValueError('watch seconds must be >0 and <=120')
  with serial.Serial(ports[0],115200,timeout=.01,write_timeout=1) as port:
   port.reset_input_buffer();link=USBLink(port);rec['host']=link.request(1).decode('ascii');rec['port']=ports[0]
   if not rec['host'].startswith('KLQ ROBOT FW ') or 'EXT_VOICE=1' not in rec['host']:raise ValueError('host needs EXT_VOICE=1')
   probe=Probe(link,rec,a.report)
   if a.action=='version':probe.version()
   elif a.action=='sounds':
    link.request(ARM,b'\x00');safe=probe.status()
    if safe['enabled'] or safe['active']:raise ValueError('no-motion sound batch not disarmed')
    probe.version()
    for n in range(5000,5008):probe.play(n);print('sound',n,'ACK/B3/B4',flush=True);time.sleep(.4)
   elif a.action=='play':probe.play(a.id)
   elif a.action=='arm':link.request(ARM,bytes([a.enable]));rec['after']=probe.status()
   elif a.action=='cancel':link.request(CANCEL);rec['after']=probe.status()
   elif a.action=='watch':
    end=time.monotonic()+a.seconds;last=None
    while time.monotonic()<end:
     s=probe.status();key=(s['semantic'],s['recognitions'],tuple(s['events']),s['accepted'],s['rejected'],s['active'],s['reason'])
     if key!=last:print(json.dumps(s),flush=True);last=key
     time.sleep(.1)
   else:rec['status']=probe.status();print(json.dumps(rec['status']),flush=True)
  rec['result']='PASS';print('PASS',a.action,flush=True)
 except BaseException as e:rec.update(result='FAIL',error=repr(e));raise
 finally:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(rec,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
