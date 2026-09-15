"""Live destructive test of the APP partition only; leaves demo installed, loader running.
Pass --allow-app-erase explicitly. The bootloader partition is never a USB write target.
"""
import argparse,struct,time,zlib
from pathlib import Path
from klq_usb import Client,find_port,BEGIN,DATA,END,RUN,ECHO,RESET,validate_image

def reconnect(prefix):
    deadline=time.monotonic()+25
    time.sleep(1)
    last=None
    while time.monotonic()<deadline:
        c=None
        try:
            c=Client(find_port(),timeout=2)
            info=c.info()
            if not info.startswith(prefix): raise RuntimeError(info)
            print(info,flush=True)
            return c
        except Exception as e:
            last=e
            if c: c.close()
            time.sleep(.3)
    raise RuntimeError('USB did not reconnect: '+str(last))

def expect_image_error(c):
    try: c.command(RUN,retries=0)
    except RuntimeError as e: assert 'invalid/incomplete image' in str(e)
    else: raise AssertionError('Invalid application started')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('image'); p.add_argument('--allow-app-erase',action='store_true',required=True)
    args=p.parse_args(); data=Path(args.image).read_bytes();validate_image(data)
    if len(data)<1024: raise ValueError('Use the supplied demo app for this test')
    c=reconnect('KLQ USB BOOT ')
    try:
        c.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
        c.command(DATA,struct.pack('<I',0)+data[:1024])
        c.command(RESET,retries=0); c.close()
        c=reconnect('KLQ USB BOOT ')
        assert 'VALID=0;' in c.info(); expect_image_error(c)
        print('Interrupted download + reset rejected incomplete app',flush=True)
        c.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)^1))
        for offset in range(0,len(data),1024):
            c.command(DATA,struct.pack('<I',offset)+data[offset:offset+1024])
        try: c.command(END,retries=0)
        except RuntimeError as e: assert 'invalid/incomplete image' in str(e)
        else: raise AssertionError('Incorrect image CRC accepted')
        expect_image_error(c)
        print('Whole-image CRC mismatch rejected',flush=True)
        c.flash(args.image,run=True); c.close()
        c=reconnect('KLQ ROBOT FW ')
        assert c.command(ECHO,b'Application USB OK',retries=0)==b'Application USB OK'
        c.command(RESET,retries=0); c.close()
        c=reconnect('KLQ USB BOOT ')
        assert 'VALID=1;' in c.info()
        print('USB flash -> verify -> app -> USB echo -> reset -> loader PASSED',flush=True)
    finally: c.close()

if __name__=='__main__': main()
