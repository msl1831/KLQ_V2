"""Host-only connection confirmation and ownership tests; never open a device."""
from contextlib import contextmanager
from pathlib import Path
import json
import struct
import sys
import threading
import time
import unittest
from unittest.mock import patch
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parent))
import status_dashboard as dashboard

HOST=b"KLQ ROBOT FW 0.12; APP=0x08008000; MAX=489472; PAGE=2048; VALID=1; EXT_VOICE=1"

def reply(body=HOST,seq=73,command=0x8001,status=0):
    payload=struct.pack('<I',status)+body
    head=struct.pack('<4sIHH',b'KLQ1',seq,command,len(payload))
    return head+struct.pack('<I',zlib.crc32(head+payload))+payload

def waiting():
    monitor=dashboard.Monitor('FAKE')
    monitor.set_connection(False,'FAKE','等待主机响应');monitor.info_seq=73
    return monitor

def snapshot(monitor):return json.loads(monitor.snapshot())

def until(predicate,seconds=2):
    deadline=time.monotonic()+seconds
    while not predicate():
        if time.monotonic()>deadline:raise AssertionError('mock worker deadline')
        time.sleep(.005)

class FakeDevice:
    def __init__(self,factory):
        self.factory=factory;self.is_open=True;self.incoming=bytearray()
        self.packet=None;self.written=threading.Event()
    def __enter__(self):return self
    def __exit__(self,*args):
        self.factory.close_started.set()
        if not self.factory.close_gate.wait(3):raise AssertionError('mock close gate')
        with self.factory.lock:
            self.is_open=False;self.factory.active-=1
    def reset_input_buffer(self):
        with self.factory.lock:self.incoming.clear()
    @property
    def in_waiting(self):
        with self.factory.lock:return len(self.incoming)
    def inject(self,data):
        with self.factory.lock:self.incoming.extend(data)
    def write(self,data):
        self.packet=bytes(data);self.written.set()
        if self.factory.fail_write:raise dashboard.serial.SerialTimeoutException('mock Write timeout')
        return len(data)
    def read(self,count):
        with self.factory.lock:
            if self.incoming:
                data=bytes(self.incoming[:count]);del self.incoming[:count];return data
        time.sleep(.005);return b''

class FakeFactory:
    def __init__(self,fail_write=False):
        self.fail_write=fail_write;self.lock=threading.Lock();self.active=0;self.maximum=0
        self.created=0;self.current=None;self.close_gate=threading.Event();self.close_gate.set()
        self.close_started=threading.Event()
    def __call__(self,port,*args,**kwargs):
        if port!='FAKE':raise AssertionError('no real port allowed')
        with self.lock:
            self.active+=1;self.maximum=max(self.maximum,self.active);self.created+=1
            self.current=FakeDevice(self);return self.current

@contextmanager
def worker(factory):
    monitor=dashboard.Monitor('FAKE')
    with patch.object(dashboard.serial,'Serial',factory):
        thread=threading.Thread(target=monitor.run);thread.start()
        try:yield monitor
        finally:
            factory.close_gate.set();monitor.stop.set();thread.join(2)
            if thread.is_alive():raise AssertionError('mock worker did not exit')

class ConnectionTests(unittest.TestCase):
    def test_fragmented_info_only_connects_after_complete_valid_frame(self):
        m=waiting();data=reply()
        buffer=bytearray(b'noise\n')
        for byte in data[:-1]:
            buffer.append(byte);m.consume(buffer);self.assertFalse(snapshot(m)['connected'])
        buffer.append(data[-1]);m.consume(buffer)
        state=snapshot(m);self.assertTrue(state['connected']);self.assertEqual(state['firmware']['name'],'KLQ ROBOT FW 0.12')
        self.assertGreater(state['since'],0);self.assertEqual(state['error'],'');self.assertIsNone(m.info_seq)

    def test_bad_crc_truncated_and_unmatched_responses_stay_offline(self):
        frame=reply();bad=bytearray(frame);bad[-1]^=1
        for data in (bytes(bad),reply(seq=74),reply(command=0x800B),reply(command=0x8050)):
            with self.subTest(data=data[:16]):
                m=waiting();m.consume(bytearray(data));self.assertFalse(snapshot(m)['connected']);self.assertEqual(m.state['firmware'],{})
        for length in range(len(frame)):
            m=waiting();m.consume(bytearray(frame[:length]));self.assertFalse(snapshot(m)['connected'])

    def test_status_and_info_shape_must_be_valid(self):
        bad=(b'',b'KLQ ROBOT FW 0.12',HOST.replace(b'KLQ',b'OTHER'),HOST+b'; APP=4',
             HOST.replace(b'APP=0x08008000',b'APP=bad'),HOST.replace(b'PAGE=2048',b'PAGE=0'),
             HOST.replace(b'MAX=489472',b'MAX=-1'),HOST.replace(b'VALID=1',b'VALID=2'),
             HOST.replace(b'PAGE=2048',b'PAGE'),HOST+b'\xff',HOST+b'\x00',HOST+b'; name=forged')
        for body in bad:
            with self.subTest(body=body):
                m=waiting();m.consume(bytearray(reply(body)));self.assertFalse(snapshot(m)['connected'])
        m=waiting();m.consume(bytearray(reply(status=4)));self.assertFalse(snapshot(m)['connected'])

    def test_boot_info_is_accepted_without_voice(self):
        m=waiting();body=HOST.replace(b'ROBOT FW 0.12',b'USB BOOT 0.3').replace(b'; EXT_VOICE=1',b'')
        m.consume(bytearray(reply(body)));self.assertTrue(m.state['connected']);self.assertFalse(m.state['voice']['supported'])

    def test_disconnect_clears_all_cached_data_and_pending_requests(self):
        m=waiting();m.consume(bytearray(reply()))
        m.parse_line(b'P1 ON C=1 T=1 UID=AAAA/BBBB/CCCC D=30mm TICKS=20 ST=0 SEQ=8 TO=0 FE=0 UE=0\n')
        m.parse_line(b'X=1mg Y=2mg Z=1000mg ROLL=0.0deg PITCH=1.0deg\n')
        m.state['voice'].update(updated=1,enabled=1);m.voice_seq=10;m.info_seq=11
        self.assertTrue(m.state['ports'][0]['online']);self.assertTrue(m.state['accelerometer']['online'])
        m.set_connection(False,'FAKE','lost');s=snapshot(m)
        self.assertFalse(s['connected']);self.assertEqual(s['firmware'],{});self.assertFalse(s['voice']['supported'])
        self.assertFalse(s['accelerometer']['online']);self.assertTrue(all(not p['online'] and not p['updated'] for p in s['ports']))
        self.assertIsNone(m.info_seq);self.assertIsNone(m.voice_seq)

    def test_open_worker_waits_for_matching_legal_info(self):
        factory=FakeFactory()
        with worker(factory) as m:
            until(lambda:factory.current is not None and factory.current.written.is_set());device=factory.current
            s=snapshot(m);self.assertFalse(s['connected']);self.assertEqual(s['error'],'等待主机响应');self.assertFalse(s['serial_released'])
            seq=struct.unpack_from('<I',device.packet,4)[0]
            bad=bytearray(reply(seq=seq));bad[-1]^=1
            device.inject(bytes(bad)+reply(seq=seq,status=4)+reply(seq=seq+1));time.sleep(.05)
            self.assertFalse(snapshot(m)['connected'])
            valid=reply(seq=seq);device.inject(valid[:-1]);time.sleep(.03);self.assertFalse(snapshot(m)['connected'])
            device.inject(valid[-1:]);until(lambda:snapshot(m)['connected']);self.assertEqual(factory.maximum,1)

    def test_first_write_timeout_never_publishes_connected(self):
        factory=FakeFactory(fail_write=True)
        with worker(factory) as m:
            until(lambda:factory.current is not None and factory.current.written.is_set())
            until(lambda:'Write timeout' in snapshot(m)['error'])
            s=snapshot(m);self.assertFalse(s['connected']);self.assertEqual(s['firmware'],{})
            self.assertFalse(any(e['message'].startswith('已连接') for e in s['events']))
        self.assertEqual(factory.active,0);self.assertEqual(factory.maximum,1)

    def test_pause_release_waits_for_close_and_resume_has_one_owner(self):
        factory=FakeFactory();factory.close_gate.clear()
        with worker(factory) as m:
            until(lambda:factory.current is not None and factory.current.written.is_set())
            first=factory.current;result=m.pause_reader(timeout=.02)
            self.assertEqual(result,{'paused':True,'released':False});self.assertTrue(factory.close_started.wait(1))
            self.assertFalse(m.released.is_set());self.assertEqual(factory.active,1)
            factory.close_gate.set();until(m.released.is_set);self.assertEqual(factory.active,0)
            m.resume_reader();until(lambda:factory.created==2 and factory.current.written.is_set())
            self.assertIsNot(first,factory.current);self.assertFalse(snapshot(m)['connected'])
            seq=struct.unpack_from('<I',factory.current.packet,4)[0];factory.current.inject(reply(seq=seq))
            until(lambda:snapshot(m)['connected'])
            self.assertEqual(m.pause_reader(timeout=1),{'paused':True,'released':True})
            self.assertEqual(factory.active,0);self.assertEqual(factory.maximum,1);self.assertEqual(factory.created,2)

if __name__=='__main__':unittest.main(verbosity=2)
