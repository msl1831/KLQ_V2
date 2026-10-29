import binascii
import json
import struct
import subprocess
import sys
import unittest
import zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import pack
import update

def image(size=240,kind="ul"):
    data=bytearray(b'\xa5'*size)
    struct.pack_into('<II',data,0,0x20000C00,pack.APP+9)
    struct.pack_into("<4sHHII",data,pack.ID_OFFSET,b"KID1",*pack.PROFILES[kind][:2],pack.APP,1)
    return bytes(data)

class CDevice:
    def __init__(self,kind,drop_data=False):
        self.process=subprocess.Popen([str(ROOT/'tests/build'/f'sim_{kind}.exe')],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        self.queue=bytearray()
        self.drop_data,self.dropped,self.data_commands=drop_data,False,0
    def write(self,data):
        cmd=struct.unpack_from('<H',data,8)[0]
        self.process.stdin.write(struct.pack('<H',len(data))+data)
        self.process.stdin.flush()
        size=struct.unpack('<H',self.process.stdout.read(2))[0]
        reply=self.process.stdout.read(size)
        if cmd==0x0201:
            self.data_commands+=1
            if self.drop_data and not self.dropped:
                self.dropped=True
                return len(data)
        self.queue.extend(reply)
        return len(data)
    def read(self,n):
        chunk=bytes(self.queue[:min(n,7)])
        del self.queue[:len(chunk)]
        return chunk
    def flush(self): pass
    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=3)
        self.process.stdout.close()
        if self.process.returncode: raise RuntimeError(f'C simulator exited {self.process.returncode}')

class PackageTests(unittest.TestCase):
    def test_roundtrip_all_boards(self):
        for kind in pack.PROFILES:
            data=pack.update_package(kind,image(kind=kind))
            self.assertEqual(pack.read_package(data),(kind,image(kind=kind),binascii.crc32(image(kind=kind))))
    def test_corruption_and_truncation(self):
        data=pack.update_package('ul',image())
        for pos in (0,4,6,8,10,12,16,20,len(data)-1):
            corrupt=bytearray(data);corrupt[pos]^=0x40
            with self.assertRaises(ValueError,msg=f'offset {pos}'): pack.read_package(corrupt)
        for size in (0,19,len(data)-1,len(data)+1):
            with self.assertRaises(ValueError): pack.read_package((data+b'\0')[:size])
    def test_reject_wrong_link_address_stack_and_length(self):
        for size in (0,7,143,pack.CAPACITY+2):
            with self.assertRaises(ValueError): pack.update_package('ul',b'\0'*size)
        for sp,pc in ((0x20000C08,pack.APP+9),(0x20000C00,0x08000009),(0x20000BFF,pack.APP+9),(0x20000C00,pack.APP+8)):
            data=bytearray(image());struct.pack_into('<II',data,0,sp,pc)
            with self.assertRaises(ValueError): pack.update_package('ul',data)
    def test_factory_hex_manifest_and_zip(self):
        for kind in pack.PROFILES:
            dest=ROOT/'build'/kind/'release'
            if not dest.exists(): self.skipTest('build all three targets first')
            manifest=json.loads((dest/f'{kind}_manifest.json').read_text())
            binary=(dest/f'{kind}_factory.bin').read_bytes()
            self.assertEqual(len(binary),pack.SIZE)
            seen,high={},0
            for line in (dest/f'{kind}_factory.hex').read_text().splitlines():
                rec=bytes.fromhex(line[1:]);self.assertEqual(sum(rec)&255,0)
                size,offset,typ=rec[0],int.from_bytes(rec[1:3],'big'),rec[3]
                self.assertEqual(len(rec),size+5)
                if typ==4:high=int.from_bytes(rec[4:-1],'big')<<16
                elif typ==0:
                    for i,b in enumerate(rec[4:-1]):
                        address=high+offset+i
                        self.assertNotIn(address,seen)
                        self.assertTrue(pack.BASE<=address<pack.BASE+pack.SIZE)
                        seen[address]=b;self.assertEqual(binary[address-pack.BASE],b)
            self.assertEqual(len(seen),manifest['programmed_bytes'])
            app_size=manifest['app_bytes'];app=binary[pack.APP-pack.BASE:pack.APP-pack.BASE+app_size]
            self.assertTrue(pack.vector_ok(binary[:manifest['boot_bytes']],pack.BASE))
            self.assertEqual(pack.read_package((dest/f'{kind}_update.klq').read_bytes())[1],app)
            meta=struct.unpack_from('<HHHHII',binary,pack.META-pack.BASE)
            self.assertEqual(meta,(*pack.PROFILES[kind][:2],app_size,0,binascii.crc32(app),0x314D4C4B))
            with zipfile.ZipFile(dest/f'{kind}_release.zip') as z:
                for name in z.namelist(): self.assertEqual(z.read(name),(dest/name).read_bytes())
            for name,digest in manifest['sha256'].items():
                self.assertEqual(pack.hashlib.sha256((dest/name).read_bytes()).hexdigest(),digest)

class UpdateTests(unittest.TestCase):
    def test_embedded_board_cannot_be_retagged(self):
        with self.assertRaisesRegex(ValueError,'embedded identity'):
            pack.update_package('ir',image())
    def test_application_reset_and_lost_run_ack(self):
        class Device:
            def __init__(self):
                self.mode,self.commands=1,[]
            def request(self,cmd,payload=b'',**kwargs):
                self.commands.append(cmd)
                if cmd==1:
                    data=bytearray(36)
                    struct.pack_into('<HBBHH',data,0,1,self.mode,1,1,1)
                    data[24:36]=b'123456789abc'
                    return bytes(data)
                if cmd==0x01F0:
                    self.mode=0
                    raise TimeoutError('lost ENTER ack')
                if cmd==9:return b''
                if cmd==0x0204:return struct.pack('<IHHII',pack.APP,pack.CAPACITY,128,pack.META,0x1800)
                if cmd==0x0200:return b'\0\0'
                if cmd==0x0201:return struct.pack('<H',struct.unpack_from('<H',payload)[0]+len(payload)-2)
                if cmd==0x0202:return b''
                if cmd==0x0203:
                    self.mode=1
                    raise TimeoutError('lost RUN ack')
                raise AssertionError(cmd)
        link=Device()
        self.assertEqual(update.upgrade(link,pack.update_package('ul',image())),('ul',240))
        self.assertEqual(link.commands[:5],[1,0x01F0,9,1,0x0204])
        self.assertEqual(link.commands[-2:],[0x0203,1])
    def test_bad_layout_uid_and_resume_offset_stop_before_data(self):
        for failure in ('uid','layout','offset'):
            class Device:
                def __init__(self):self.infos,self.data=0,0
                def request(self,cmd,payload=b'',**kwargs):
                    if cmd==1:
                        self.infos+=1
                        data=bytearray(36)
                        struct.pack_into('<HBBHH',data,0,1,0,1,1,1)
                        data[24:36]=b'123456789abc'
                        if failure=='uid' and self.infos==2:data[24]^=1
                        return bytes(data)
                    if cmd==9:return b''
                    if cmd==0x0204:
                        return b'bad' if failure=='layout' else struct.pack('<IHHII',pack.APP,pack.CAPACITY,128,pack.META,0x1800)
                    if cmd==0x0200:return b'\1\0'
                    if cmd==0x0201:self.data+=1
                    raise AssertionError(cmd)
            link=Device()
            with self.assertRaises(ValueError):update.upgrade(link,pack.update_package('ul',image()))
            self.assertEqual(link.data,0)
    def device(self,kind='ul',**kwargs):
        if not (ROOT/'tests/build'/f'sim_{kind}.exe').exists(): self.skipTest('run run_tests.ps1 first')
        port=CDevice(kind,**kwargs);self.addCleanup(port.close);return port
    def test_real_c_firmware_full_image_each_board(self):
        for kind in pack.PROFILES:
            package=(ROOT/'build'/kind/'release'/f'{kind}_update.klq').read_bytes()
            port=self.device(kind)
            self.assertEqual(update.upgrade(update.Link(port),package,False),(kind,len(package)-pack.HEADER.size))
    def test_lost_data_ack_retransmits(self):
        port=self.device(drop_data=True)
        update.upgrade(update.Link(port),pack.update_package('ul',image()),False)
        self.assertTrue(port.dropped);self.assertEqual(port.data_commands,3)
    def test_wrong_board_rejected_before_erase(self):
        port=self.device('ir')
        with self.assertRaisesRegex(ValueError,'identity'): update.upgrade(update.Link(port),pack.update_package('ul',image()),False)
        self.assertEqual(port.data_commands,0)
    def test_corrupt_package_rejected_before_io(self):
        class NoIO:
            def request(self,*args,**kwargs): raise AssertionError('must not touch device')
        with self.assertRaises(ValueError): update.upgrade(NoIO(),b'bad',False)
    def test_resume_same_powered_session(self):
        port=self.device();link=update.Link(port);app=image(272)
        link.request(9)
        link.request(0x0200,struct.pack('<HHHI',1,1,len(app),binascii.crc32(app)))
        link.request(0x0201,b'\0\0'+app[:128])
        update.upgrade(link,pack.update_package('ul',app),False)
        self.assertEqual(port.data_commands,3)
    def test_frame_recovers_noise_bad_crc_and_stale_sequence(self):
        class Replies:
            def write(self,f):
                seq,cmd=struct.unpack_from('<IH',f,4)
                good=update.encode(seq,cmd|0x8000,b'\0\0\0\0ok')
                bad=bytearray(good);bad[12]^=1
                self.queue=bytearray(b'noise'+update.encode(seq+1,cmd|0x8000,b'\0'*4)+bad+good)
            def flush(self): pass
            def read(self,n):
                data=bytes(self.queue[:n]);del self.queue[:n];return data
        self.assertEqual(update.Link(Replies()).request(1),b'ok')
if __name__=='__main__':unittest.main()
