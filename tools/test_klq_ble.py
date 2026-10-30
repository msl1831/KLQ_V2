"""Pure-Python fake-GATT tests. Never scans, connects or opens a serial port."""
import asyncio
import json
import struct
import sys
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parent))
import klq_ble as ble

def wire_response(seq,cmd,body=b'',status=0):
    payload=struct.pack('<I',status)+body
    head=struct.pack('<4sIHH',b'KLQ1',seq,cmd|0x8000,len(payload))
    return head+struct.pack('<I',zlib.crc32(payload,zlib.crc32(head)))+payload

def image(n=700):return struct.pack('<II',0x20001000,0x08008009)+bytes((i&255 for i in range(n-8)))

class Services:
    def __init__(self,chars):self.service=SimpleNamespace(uuid=ble.SERVICE,characteristics=chars)
    def get_service(self,uuid):return self.service if uuid==ble.SERVICE else None
    def __iter__(self):return iter([self.service])

class Server:
    def __init__(self,boot=False):
        self.boot=boot;self.source=bytearray();self.length=0;self.crc=0;self.state=0;self.valid=False
        self.records=[];self.effects={};self.cache={};self.drop_first=set();self.drop_always=set();self.noisy=False;self.status=0
        self.disconnect_on=None;self.write_props=['write','write-without-response'];self.notify_props=['read','notify']
    def info(self):
        if self.boot:return b'KLQ USB BOOT 0.5; VALID=1; BLE=1'
        return f'KLQ ROBOT FW 0.15; UI=1; USER_STATE={self.state}; USER_VALID={int(self.valid)}; USER_LENGTH={self.length}'.encode()
    def process(self,frame):
        magic,seq,cmd,n,crc=struct.unpack_from('<4sIHHI',frame)
        assert magic==b'KLQ1' and len(frame)==16+n and zlib.crc32(frame[16:],zlib.crc32(frame[:12]))==crc
        p=frame[16:];self.records.append((seq,cmd,p,frame))
        key=seq,cmd,p
        if key in self.cache:return self.cache[key]
        self.effects[cmd]=self.effects.get(cmd,0)+1
        status=self.status;body=b''
        if not status:
            if cmd==ble.INFO:body=self.info()
            elif cmd==ble.ECHO:body=p
            elif cmd==ble.BEGIN:self.length,self.crc=struct.unpack('<II',p);self.source.clear();self.valid=False;self.state=1
            elif cmd==ble.DATA:
                at=struct.unpack_from('<I',p)[0];assert at==len(self.source);self.source.extend(p[4:]);body=struct.pack('<I',len(self.source))
            elif cmd==ble.END:
                assert len(self.source)==self.length and zlib.crc32(self.source)==self.crc;self.valid=True;self.state=2
            elif cmd==ble.RUN:self.state=3
            elif cmd==ble.STOP:self.state=2 if self.valid else 0
            elif cmd==ble.RESET:self.boot=True
            else:status=1
        reply=wire_response(seq,cmd,body,status);self.cache[key]=reply
        return reply

class Gatt:
    def __init__(self,server,on_disconnect):
        self.server=server;self.on_disconnect=on_disconnect;self.is_connected=False;self.mtu_size=23
        self.rx=bytearray();self.writes=[];self.callback=None;self.disconnected=False
        self.services=Services([SimpleNamespace(uuid=ble.WRITE,properties=server.write_props,max_write_without_response_size=20),SimpleNamespace(uuid=ble.NOTIFY,properties=server.notify_props)])
    async def connect(self):self.is_connected=True
    async def disconnect(self):self.is_connected=False;self.disconnected=True;self.on_disconnect(self)
    async def start_notify(self,char,callback):assert char.uuid==ble.NOTIFY;self.callback=callback
    async def stop_notify(self,char):pass
    async def write_gatt_char(self,char,data,response):
        assert char.uuid==ble.WRITE and 0<len(data)<=20
        self.writes.append((bytes(data),response));self.rx.extend(data)
        if len(self.rx)<16:return
        n=struct.unpack_from('<H',self.rx,10)[0]
        if len(self.rx)<16+n:return
        frame=bytes(self.rx[:16+n]);del self.rx[:16+n]
        assert not self.rx
        reply=self.server.process(frame);seq,cmd,p,_=self.server.records[-1]
        if self.server.disconnect_on==cmd:
            self.is_connected=False;self.on_disconnect(self);return
        if cmd in self.server.drop_always:return
        if cmd in self.server.drop_first:self.server.drop_first.remove(cmd);return
        if self.server.noisy:
            bad=bytearray(reply);bad[-1]^=1
            reply=b'AT+CONNECTED\r\n'+wire_response((seq+1)&0xffffffff,cmd,b'old')+bad+reply
        for at in range(0,len(reply),20):self.callback(None,bytearray(reply[at:at+20]))

class Factory:
    def __init__(self,server=None):self.server=server or Server();self.instances=[];self.kwargs=[]
    def __call__(self,_device,**kw):
        self.kwargs.append(kw);g=Gatt(self.server,kw['disconnected_callback']);self.instances.append(g);return g

class CodecTests(unittest.TestCase):
    def test_fragmented_noise_crc_and_coalesced_frames(self):
        p=ble.FrameParser();raw=b'noise'+wire_response(7,ble.ECHO,b'a')+wire_response(8,ble.ECHO,b'b')
        got=[]
        for b in raw:got+=p.feed(bytes([b]))
        self.assertEqual([(7,0x8006,struct.pack('<I',0)+b'a'),(8,0x8006,struct.pack('<I',0)+b'b')],got)
        self.assertLessEqual(len(p.buffer),3)
        bad=bytearray(wire_response(9,ble.ECHO,b'bad'));bad[-1]^=1
        self.assertEqual([],p.feed(bad))
        oversized=struct.pack('<4sIHHI',b'KLQ1',1,0x8006,65535,0)
        self.assertEqual([(10,0x8006,b'\0\0\0\0ok')],p.feed(oversized+wire_response(10,ble.ECHO,b'ok')))
    def test_status_and_mode_validation(self):
        raw=bytearray(88);raw[0:4]=bytes([1,1,1,1]);struct.pack_into('<7I',raw,8,*range(7));raw[36:40]=b'KLQ2'
        value=ble.ble_status(raw);self.assertEqual(value['name'],'KLQ2');self.assertEqual(value['config_writes'],6)
        for bad in (b'',bytes(87),bytes(88)):
            with self.assertRaises(ble.ProtocolError):ble.ble_status(bad)
        self.assertTrue(ble.boot_mode('KLQ USB BOOT 0.5;'));self.assertFalse(ble.boot_mode('KLQ ROBOT FW 0.15; BOOT=1'))
    def test_invalid_transport_options(self):
        for kw in ({'fragment':0},{'fragment':21},{'gap':-1},{'timeout':0}):
            with self.assertRaises(ValueError):ble.Client('FAKE',**kw)

class ClientTests(unittest.IsolatedAsyncioTestCase):
    def client(self,server=None,**kw):
        factory=Factory(server);return ble.Client('FAKE',timeout=2,gap=0,client_factory=factory,**kw),factory
    async def test_echo_fragmentation_noise_crc_seq_filters(self):
        server=Server();server.noisy=True;c,f=self.client(server)
        async with c:
            self.assertEqual(await c.command(ble.ECHO,bytes(range(256))*4),bytes(range(256))*4)
            self.assertTrue(all(len(data)<=20 and response for data,response in c.client.writes))
            self.assertEqual(c.describe()['mtu'],23)
            self.assertEqual(f.kwargs[0]['winrt'],{'use_cached_services':False})
        self.assertTrue(f.instances[0].disconnected)
    async def test_retry_uses_same_seq_and_request_once(self):
        server=Server();server.drop_first={ble.ECHO};c,f=self.client(server);c.timeout=.15
        async with c:
            self.assertEqual(await c.command(ble.ECHO,b'retry'),b'retry')
            records=server.records
            self.assertEqual(len(records),2);self.assertEqual(records[0][3],records[1][3]);self.assertEqual(server.effects[ble.ECHO],1)
    async def test_payload_limit_and_error_not_retried(self):
        server=Server();server.status=4;c,f=self.client(server)
        async with c:
            with self.assertRaises(ble.CommandError) as caught:await c.command(ble.ECHO,b'error')
            self.assertEqual(caught.exception.status,4);self.assertEqual(len(server.records),1)
            with self.assertRaises(ValueError):await c.command(ble.ECHO,bytes(1029))
            self.assertEqual(len(server.records),1)
    async def test_run_reset_timeout_is_ambiguous_not_retried(self):
        for command in (ble.RUN,ble.RESET):
            server=Server();server.drop_always={command};c,f=self.client(server);c.timeout=.15
            async with c:
                with self.assertRaises(ble.AmbiguousActionError):await c.command(command)
                self.assertEqual(len(server.records),1)
                with self.assertRaises(ValueError):await c.command(command,retries=2)
                self.assertEqual(len(server.records),1)
    async def test_write_without_response_only(self):
        server=Server();server.write_props=['write-without-response'];c,f=self.client(server)
        async with c:
            self.assertEqual(await c.command(ble.ECHO,b'no-response'),b'no-response')
            self.assertTrue(all(not response for _,response in c.client.writes))
    async def test_missing_notification_rejects_cleanly(self):
        server=Server();server.notify_props=['read'];c,f=self.client(server)
        with self.assertRaises(ble.ProtocolError):await c.connect()
        self.assertIsNone(c.client);self.assertTrue(f.instances[0].disconnected)
    async def test_python_download_retries_commit_once_and_runs(self):
        server=Server();server.drop_first={ble.DATA};c,f=self.client(server)
        data=b'#'+b'x'*1300+b'\n'
        async with c:
            await c.load_program(data,run=True)
            self.assertEqual(server.source,data);self.assertTrue(server.valid);self.assertEqual(server.state,3)
            self.assertEqual(server.effects[ble.DATA],6);self.assertEqual(server.effects[ble.RUN],1)
            self.assertTrue(all(len(d)<=20 for d,_ in c.client.writes))
    async def test_python_boot_guard_and_local_length(self):
        server=Server(boot=True);c,f=self.client(server)
        async with c:
            with self.assertRaises(ble.ProtocolError):await c.load_program(b'import klq\n')
            self.assertEqual([x[1] for x in server.records],[ble.INFO]);server.records.clear()
            for bad in (b'',bytes(16385)):
                with self.assertRaises(ValueError):await c.load_program(bad)
            self.assertEqual(server.records,[])
    async def test_firmware_app_guard_local_vectors_and_alignment(self):
        server=Server();c,f=self.client(server)
        async with c:
            with self.assertRaises(ble.ProtocolError):await c.flash(image())
            self.assertEqual([x[1] for x in server.records],[ble.INFO]);server.records.clear()
            for data,block in [(b'bad',256),(bytes(32),256),(image(),255),(image(),0)]:
                with self.assertRaises(ValueError):await c.flash(data,block=block)
            self.assertEqual(server.records,[])
    async def test_firmware_boot_download_only_system_app(self):
        server=Server(boot=True);c,f=self.client(server)
        async with c:
            data=image(701);await c.flash(data,run=True)
            self.assertEqual(server.source,data);self.assertTrue(server.valid)
            self.assertEqual(server.effects[ble.BEGIN],1);self.assertEqual(server.effects[ble.END],1);self.assertEqual(server.effects[ble.RUN],1)
    async def test_disconnect_and_old_callbacks_do_not_poison_new_session(self):
        server=Server();c,f=self.client(server)
        await c.connect();old=c.client;server.disconnect_on=ble.ECHO
        with self.assertRaises(ConnectionError):await c.command(ble.ECHO,b'disconnect')
        await c.disconnect();server.disconnect_on=None;await c.connect()
        c._lost(old);c._consume(old,wire_response(c.seq+1,ble.ECHO,b'old'))
        self.assertIsNone(c.failure);self.assertEqual(c.parser.buffer,b'')
        self.assertEqual(await c.command(ble.ECHO,b'new'),'new'.encode())
        await c.disconnect()
    async def test_stale_notify_cannot_satisfy_current_request(self):
        server=Server();c,f=self.client(server)
        await c.connect();old=c.client;await c.disconnect();await c.connect();server.drop_always={ble.ECHO}
        task=asyncio.create_task(c.command(ble.ECHO,b'new',retries=0))
        while not server.records:await asyncio.sleep(0)
        seq=server.records[-1][0];old.callback(None,wire_response(seq,ble.ECHO,b'poison'))
        await asyncio.sleep(.001);self.assertFalse(task.done())
        c.client.callback(None,wire_response(seq,ble.ECHO,b'correct'));self.assertEqual(await task,b'correct')
        await c.disconnect()
    async def test_timeout_after_partial_write_does_not_blindly_resend(self):
        c,f=self.client();c.timeout=.15
        async with c:
            original=c.client.write_gatt_char
            async def slow_write(*args,**kw):
                await original(*args,**kw)
                await asyncio.sleep(.2)
            c.client.write_gatt_char=slow_write
            with self.assertRaisesRegex(TimeoutError,'frame write timed out'):await c.command(ble.ECHO,bytes(100))
            self.assertEqual(len(c.client.writes),1);self.assertEqual(f.server.records,[])
    async def test_malformed_status_is_rejected_without_retry(self):
        server=Server();server.drop_always={ble.ECHO};c,f=self.client(server)
        async with c:
            task=asyncio.create_task(c.command(ble.ECHO,b'x'))
            while not server.records:await asyncio.sleep(0)
            seq=server.records[-1][0];c.client.callback(None,ble.encode(seq,ble.ECHO|0x8000,b'x'))
            with self.assertRaisesRegex(ble.ProtocolError,'Malformed status'):await task
            self.assertEqual(len(server.records),1)
    async def test_all_complete_request_timeouts_use_same_seq(self):
        server=Server();server.drop_always={ble.ECHO};c,f=self.client(server);c.timeout=.15
        async with c:
            with self.assertRaises(TimeoutError):await c.command(ble.ECHO,b'x')
            self.assertEqual(len(server.records),3);self.assertEqual(len({x[3] for x in server.records}),1)
    async def test_reconnect_waits_for_outstanding_rpc_to_fail(self):
        server=Server();server.drop_always={ble.ECHO};c,f=self.client(server)
        await c.connect();task=asyncio.create_task(c.command(ble.ECHO,b'pending',retries=0))
        while not server.records:await asyncio.sleep(0)
        await c.disconnect()
        with self.assertRaises(RuntimeError):await c.connect()
        with self.assertRaises(ConnectionError):await task
        await c.connect();server.drop_always.clear();self.assertEqual(await c.command(ble.ECHO,b'new'),b'new');await c.disconnect()
    async def test_single_outstanding_rpc(self):
        c,f=self.client()
        async with c:
            a,b=await asyncio.gather(c.command(ble.ECHO,b'first'*20),c.command(ble.ECHO,b'second'*20))
            self.assertEqual((a,b),(b'first'*20,b'second'*20))
            self.assertEqual(len(f.server.records),2);self.assertNotEqual(f.server.records[0][0],f.server.records[1][0])
    async def test_scan_selection_does_not_connect_unknown_name(self):
        devices={'1':(SimpleNamespace(address='AA',name='OTHER'),SimpleNamespace(local_name='OTHER',rssi=-10,service_uuids=[])),
                 '2':(SimpleNamespace(address='BB',name='KLQ2'),SimpleNamespace(local_name='KLQ2',rssi=-20,service_uuids=[ble.SERVICE]))}
        async def discover(**_):return devices
        with patch.dict(sys.modules,{'bleak':SimpleNamespace(BleakScanner=SimpleNamespace(discover=discover))}):
            rows,matches=await ble.discover();self.assertEqual(len(rows),2);self.assertEqual([d.address for d in matches],['BB'])
            _,matches=await ble.discover(address='aa');self.assertEqual([d.address for d in matches],['AA'])
            _,matches=await ble.discover(name='MISSING');self.assertEqual(matches,[])

if __name__=='__main__':unittest.main(verbosity=2)
