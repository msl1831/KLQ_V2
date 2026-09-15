"""KLQ USB loader client. Install pyserial; run --help for commands."""
import argparse
import secrets
import struct
import time
import zlib
from pathlib import Path
import serial
from serial.tools import list_ports

MAGIC = b'KLQ1'
APP_BASE, META_BASE = 0x08008000, 0x0807F800
APP_MAX = META_BASE - APP_BASE
INFO, BEGIN, DATA, END, RUN, ECHO, RESET, DISPLAY, STOP, FINISH = range(1,11)
USER_PROGRAM_MAX = 16384
ERRORS = ['OK','unknown command','invalid length','CRC mismatch','invalid state',
          'range/offset error','flash write/erase failed','invalid/incomplete image']

def encode(seq, cmd, payload=b''):
    prefix = struct.pack('<4sIHH', MAGIC, seq, cmd, len(payload))
    return prefix + struct.pack('<I', zlib.crc32(payload,zlib.crc32(prefix))) + payload

def validate_image(data):
    if not 8 <= len(data) <= APP_MAX:
        raise ValueError(f'Image size must be 8..{APP_MAX} bytes')
    sp, pc = struct.unpack_from('<II',data)
    if not (0x20000000 < sp <= 0x20020000 and sp % 8 == 0):
        raise ValueError(f'Invalid initial stack pointer 0x{sp:08X}')
    if not (pc & 1 and APP_BASE+8 <= (pc & ~1) < APP_BASE+len(data)):
        raise ValueError(f'Image must be linked at 0x{APP_BASE:08X}; reset vector is 0x{pc:08X}')

class Client:
    def __init__(self, port, timeout=15):
        self.serial = serial.Serial(port,115200,timeout=0.1,write_timeout=5)
        self.timeout = timeout
        self.seq = secrets.randbits(32)
        self.buffer = bytearray()
        time.sleep(0.15)
        self.serial.reset_input_buffer()

    def close(self): self.serial.close()

    def receive(self, seq, cmd):
        deadline = time.monotonic()+self.timeout
        while time.monotonic() < deadline:
            self.buffer += self.serial.read(max(1,self.serial.in_waiting))
            while len(self.buffer) >= 16:
                start = self.buffer.find(MAGIC)
                if start < 0:
                    del self.buffer[:-3]; break
                if start:
                    del self.buffer[:start]
                    if len(self.buffer)<16: break
                _, reply_seq, reply_cmd, n, crc = struct.unpack_from('<4sIHHI',self.buffer)
                if n>1032:
                    del self.buffer[0]; continue
                if len(self.buffer)<16+n: break
                packet=bytes(self.buffer[:16+n]); del self.buffer[:16+n]
                if zlib.crc32(packet[16:],zlib.crc32(packet[:12])) != crc: continue
                if reply_seq != seq or reply_cmd != cmd|0x8000: continue
                if n<4: raise RuntimeError('Malformed status response')
                status=struct.unpack_from('<I',packet,16)[0]
                if status:
                    raise RuntimeError(f'KLQ error {status}: '+(ERRORS[status] if status<len(ERRORS) else 'unknown'))
                return packet[20:]
        raise TimeoutError(f'No response to command {cmd} on {self.serial.port}')

    def command(self, cmd, payload=b'', retries=2):
        self.seq=(self.seq+1)&0xffffffff
        packet=encode(self.seq,cmd,payload)
        for attempt in range(retries+1):
            self.serial.write(packet)
            try: return self.receive(self.seq,cmd)
            except TimeoutError:
                if attempt==retries: raise
        raise AssertionError('unreachable')

    def info(self): return self.command(INFO).decode('ascii')

    def flash(self, path, run=False):
        data=Path(path).read_bytes()
        validate_image(data) # Do not erase anything until local checks pass.
        info=self.info()
        if not info.startswith('KLQ USB BOOT '):
            raise RuntimeError('Device is not in bootloader; use reset first')
        print(info)
        self.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
        for offset in range(0,len(data),1024):
            chunk=data[offset:offset+1024]
            received=self.command(DATA,struct.pack('<I',offset)+chunk)
            if struct.unpack('<I',received)[0] != offset+len(chunk):
                raise RuntimeError('Unexpected committed offset')
            print(f'\rDownloaded {offset+len(chunk)}/{len(data)} bytes',end='',flush=True)
        self.command(END)
        print('\nFlash and image CRC verified.')
        if run: self.command(RUN,retries=0)

    def load_program(self, path, run=False):
        data=Path(path).read_bytes()
        if not 1 <= len(data) <= USER_PROGRAM_MAX:
            raise ValueError(f'User program size must be 1..{USER_PROGRAM_MAX} bytes')
        info=self.info()
        if not info.startswith('KLQ ROBOT FW '):
            raise RuntimeError('Robot system firmware is not running; use run first')
        print(info)
        self.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
        for offset in range(0,len(data),1024):
            chunk=data[offset:offset+1024]
            received=self.command(DATA,struct.pack('<I',offset)+chunk)
            if struct.unpack('<I',received)[0] != offset+len(chunk):
                raise RuntimeError('Unexpected RAM program offset')
            print(f'\rDownloaded {offset+len(chunk)}/{len(data)} bytes to RAM',end='',flush=True)
        self.command(END)
        print('\nUser program CRC verified; completion icon is being shown.')
        if run:
            time.sleep(0.9)
            self.command(RUN)
            print('User program run state started (Python VM is not integrated yet).')

def find_port():
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x314B,0x0108)]
    if len(ports)!=1: raise RuntimeError('Specify --port; matching USB ports: '+str(ports))
    return ports[0]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',help='COM number; auto-detects the Geehy example VID/PID')
    sub=p.add_subparsers(dest='action',required=True)
    for name in ['ports','info','run','stop','finish','reset']: sub.add_parser(name)
    f=sub.add_parser('flash'); f.add_argument('bin'); f.add_argument('--run',action='store_true')
    u=sub.add_parser('program'); u.add_argument('python'); u.add_argument('--run',action='store_true')
    e=sub.add_parser('echo'); e.add_argument('text',nargs='?',default='KLQ USB test')
    d=sub.add_parser('display'); d.add_argument('columns',help='13 hex bytes, left to right, bit0=top')
    args=p.parse_args()
    if args.action=='ports':
        for port in list_ports.comports(): print(port.device,port.description,port.hwid)
        return
    client=Client(args.port or find_port())
    try:
        if args.action=='info': print(client.info())
        elif args.action=='flash': client.flash(args.bin,args.run)
        elif args.action=='program': client.load_program(args.python,args.run)
        elif args.action=='run':
            info=client.info()
            client.command(RUN,retries=0)
            print('System firmware started' if info.startswith('KLQ USB BOOT ') else
                  'User program run state started (Python VM is not integrated yet)')
        elif args.action=='stop': client.command(STOP); print('User program stopped; standby restored')
        elif args.action=='finish': client.command(FINISH); print('User program finished; standby restored')
        elif args.action=='reset': client.command(RESET,retries=0); print('Reset to bootloader')
        elif args.action=='echo':
            value=args.text.encode()
            if len(value)>1028: raise ValueError('Echo payload too long')
            reply=client.command(ECHO,value)
            if reply!=value: raise RuntimeError('Echo mismatch')
            print(reply.decode())
        elif args.action=='display':
            columns=bytes.fromhex(args.columns)
            if len(columns)!=13: raise ValueError('Exactly 13 bytes required')
            client.command(DISPLAY,columns)
    finally: client.close()

if __name__=='__main__': main()
