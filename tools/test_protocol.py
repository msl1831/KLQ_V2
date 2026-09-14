"""Host format checks and optional live USB checks; no application erase."""
import struct, unittest, zlib, sys
from klq_usb import encode, validate_image, APP_BASE, APP_MAX, Client, ECHO, BEGIN, DATA

class FormatTests(unittest.TestCase):
    def test_crc_known_vector(self):
        self.assertEqual(zlib.crc32(b'123456789'),0xcbf43926)
        packet=encode(0x1234,ECHO,b'123456789')
        self.assertEqual(packet[:4],b'KLQ1')
        self.assertEqual(struct.unpack_from('<I',packet,12)[0],zlib.crc32(packet[16:],zlib.crc32(packet[:12])))
    def test_image_validation(self):
        valid=struct.pack('<II',0x20020000,APP_BASE+9)+b'\x00\xbf'*4
        validate_image(valid)
        for data in [b'',valid[:7],b'\x00'*(APP_MAX+1),
                     struct.pack('<II',0x20000000,APP_BASE+9)+valid[8:],
                     struct.pack('<II',0x20020000,0x08000101)+valid[8:],
                     struct.pack('<II',0x20020000,APP_BASE+8)+valid[8:]]:
            with self.assertRaises(ValueError): validate_image(data)

def live(port):
    import random,time
    c=Client(port,timeout=3)
    try:
        print(c.info())
        start=time.monotonic()
        for n in [0,1,2,43,44,63,64,65,127,128,511,1024,1028]*2:
            data=random.Random(n).randbytes(n)
            assert c.command(ECHO,data,retries=0)==data
        print('26 echo transfers passed; seconds:',round(time.monotonic()-start,3))
        # Replaying an identical sequence must return the cached response.
        seq=0xabcdef01; data=b'duplicate transaction'
        packet=encode(seq,ECHO,data)
        for _ in range(2):
            c.serial.write(packet); assert c.receive(seq,ECHO)==data
        print('Duplicate request replay passed')
        seq+=1; packet=bytearray(encode(seq,ECHO,b'bad crc')); packet[-1]^=1
        c.serial.write(packet)
        try: c.receive(seq,ECHO)
        except RuntimeError as e: assert 'CRC mismatch' in str(e)
        else: raise AssertionError('Corrupt frame accepted')
        for payload in [struct.pack('<II',0,0),struct.pack('<II',APP_MAX+1,0)]:
            try: c.command(BEGIN,payload,retries=0)
            except RuntimeError as e: assert 'range/offset' in str(e)
            else: raise AssertionError('Out-of-range BEGIN accepted')
        assert c.command(ECHO,b'after errors',retries=0)==b'after errors'
        print('CRC rejection, size bounds, and recovery passed')
    finally: c.close()

if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--port': live(sys.argv[2])
    else: unittest.main()
