"""Real Python USB tool -> real C USB/port bridge -> real C PY32 boot core, with mocked hardware."""
import os
import struct
import subprocess
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import host_link
import pack
import update


class Pipeline:
    def __init__(self,kind,port):
        self.host=subprocess.Popen([os.environ.get('KLQ_PIPELINE_EXE',str(ROOT/'tests/build/pipeline/bridge_pipeline.exe')),str(port-1)],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        self.device=subprocess.Popen([str(ROOT/'tests/build'/f'sim_{kind}.exe')],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        self.queue=bytearray()
        self.uart_commands=[]
        self.device_commands=[]
        self.drop_uart_data=False

    def exchange(self,typ,data=b'',ticks=0):
        n=ticks if typ==b'T' else len(data)
        self.host.stdin.write(typ+struct.pack('<H',n)+data)
        self.host.stdin.flush()
        while True:
            header=self.host.stdout.read(3)
            if len(header)!=3:
                raise RuntimeError(f'host simulator stopped: {self.host.poll()}')
            typ=header[:1]
            size=struct.unpack_from('<H',header,1)[0]
            payload=self.host.stdout.read(size)
            if typ==b'D':
                return
            if typ==b'B':
                self.queue.extend(payload)
            elif typ==b'U':
                self.uart_commands.append(struct.unpack_from('<H',payload,8)[0])
                self.device.stdin.write(struct.pack('<H',len(payload))+payload)
                self.device.stdin.flush()
                reply_size=self.device.stdout.read(2)
                if len(reply_size)!=2:
                    raise RuntimeError(f'device simulator stopped: {self.device.poll()}')
                reply=self.device.stdout.read(struct.unpack('<H',reply_size)[0])
                self.device_commands.append((payload,reply))
                if self.drop_uart_data and struct.unpack_from('<H',payload,8)[0]==0x0201:
                    reply=b''
                self.host.stdin.write(b'R'+struct.pack('<H',len(reply))+reply)
                self.host.stdin.flush()
            else:
                raise RuntimeError(f'unknown simulator output {typ!r}')

    def write(self,data):
        self.exchange(b'W',data)
        return len(data)

    def read(self,n):
        if not self.queue:
            self.exchange(b'T',ticks=8)
        data=bytes(self.queue[:min(n,13)])
        del self.queue[:len(data)]
        return data

    def flush(self):
        pass

    def reset_usb(self):
        self.exchange(b'Z')

    def close(self):
        errors=[]
        for process in (self.host,self.device):
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
            try:
                process.wait(timeout=3)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
                process.stdout.close()
            if process.returncode:
                errors.append(process.returncode)
        if errors:
            raise RuntimeError(f'pipeline simulators exited {errors}')


class PipelineTests(unittest.TestCase):
    def test_all_board_images_through_actual_c_usb_and_uart_bridge(self):
        for kind,port in ((kind,port) for kind in pack.PROFILES for port in (1,2,3)):
            with self.subTest(kind=kind,port=port):
                if not (ROOT/'tests/build/pipeline/bridge_pipeline.exe').exists():
                    self.skipTest('run run_pipeline_tests.ps1 first')
                pipe=Pipeline(kind,port)
                try:
                    image=(ROOT/'build'/kind/'release'/f'{kind}_update.klq').read_bytes()
                    with host_link.HostLink(pipe,port,kind,poll_interval=0) as link:
                        self.assertEqual(update.upgrade(link,image,False),(kind,len(image)-pack.HEADER.size))
                    self.assertIn(9,pipe.uart_commands)
                    self.assertIn(0x0204,pipe.uart_commands)
                    self.assertEqual(pipe.uart_commands.count(0x0200),1)
                    self.assertEqual(pipe.uart_commands.count(0x0202),1)
                    self.assertEqual(pipe.uart_commands.count(0x0201),(len(image)-pack.HEADER.size+127)//128)
                finally:
                    pipe.close()

    def test_wrong_actual_c_board_does_not_receive_begin(self):
        if not (ROOT/'tests/build/pipeline/bridge_pipeline.exe').exists():
            self.skipTest('run run_pipeline_tests.ps1 first')
        pipe=Pipeline('ir',1)
        try:
            image=(ROOT/'build/ul/release/ul_update.klq').read_bytes()
            with self.assertRaises(ValueError):
                with host_link.HostLink(pipe,1,'ul',poll_interval=0) as link:
                    update.upgrade(link,image,False)
            self.assertNotIn(0x0200,pipe.uart_commands)
            self.assertNotIn(0x0201,pipe.uart_commands)
        finally:
            pipe.close()

    def test_usb_reset_mid_image_closes_session_then_reopens_and_resumes(self):
        if not (ROOT/'tests/build/pipeline/bridge_pipeline.exe').exists():
            self.skipTest('run run_pipeline_tests.ps1 first')
        pipe=Pipeline('ul',2)
        try:
            image=(ROOT/'build/ul/release/ul_update.klq').read_bytes()
            _,app,crc=pack.read_package(image)
            with host_link.HostLink(pipe,2,'ul',poll_interval=0) as link:
                update.check_info(link.request(1),'ul',0)
                link.request(0x0204)
                link.request(0x0200,struct.pack('<HHHI',1,1,len(app),crc),timeout=10)
                link.request(0x0201,b'\0\0'+app[:128])
                pipe.reset_usb()
                with self.assertRaises(host_link.USBError):
                    link.request(1)
            with host_link.HostLink(pipe,2,'ul',poll_interval=0) as link:
                self.assertEqual(update.upgrade(link,image,False),('ul',len(app)))
            data=[request for request,_ in pipe.device_commands if struct.unpack_from('<H',request,8)[0]==0x0201]
            self.assertEqual(len(data),(len(app)+127)//128)
            self.assertEqual(sum(struct.unpack_from('<H',f,16)[0]==0 for f in data),1)
        finally:
            pipe.close()

    def test_uart_ack_loss_exhaustion_cleans_up_then_same_image_resumes(self):
        if not (ROOT/'tests/build/pipeline/bridge_pipeline.exe').exists():
            self.skipTest('run run_pipeline_tests.ps1 first')
        pipe=Pipeline('ir',3)
        try:
            image=(ROOT/'build/ir/release/ir_update.klq').read_bytes()
            pipe.drop_uart_data=True
            with self.assertRaises(TimeoutError):
                with host_link.HostLink(pipe,3,'ir',poll_interval=0) as link:
                    update.upgrade(link,image,False)
            pipe.drop_uart_data=False
            with host_link.HostLink(pipe,3,'ir',poll_interval=0) as link:
                self.assertEqual(update.upgrade(link,image,False),('ir',len(image)-pack.HEADER.size))
            self.assertEqual(pipe.uart_commands.count(0x0200),2)
            self.assertEqual(pipe.uart_commands.count(0x0202),1)
        finally:
            pipe.close()


if __name__=='__main__':
    unittest.main()
