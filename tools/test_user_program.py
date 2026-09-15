"""Live RAM-only user-program and display-state test; never writes Flash."""
import struct
import time
import zlib

from klq_usb import (BEGIN, DATA, DISPLAY, END, FINISH, RESET, RUN, STOP,
                     Client, USER_PROGRAM_MAX, find_port)


def reconnect(prefix):
    deadline=time.monotonic()+25
    last=None
    while time.monotonic()<deadline:
        client=None
        try:
            client=Client(find_port(),timeout=2)
            info=client.info()
            if not info.startswith(prefix): raise RuntimeError(info)
            return client
        except Exception as error:
            last=error
            if client: client.close()
            time.sleep(.3)
    raise RuntimeError('USB did not reconnect: '+str(last))


def values(info):
    result={}
    for item in info.split(';'):
        if '=' in item:
            key,value=item.strip().split('=',1)
            result[key]=value
    return result


def wait_ui(client, expected, timeout=2):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        info=client.info()
        if int(values(info)['UI'])==expected: return info
        time.sleep(.05)
    raise AssertionError(f'UI did not reach {expected}: {info}')


def main():
    client=reconnect('KLQ ROBOT FW ')
    program=b'from klq import button, port\nwhile button.user_pressed():\n    port.motor_stop(1)\n'
    try:
        client.command(STOP)
        try: client.command(BEGIN,struct.pack('<II',USER_PROGRAM_MAX+1,0),retries=0)
        except RuntimeError as error: assert 'range/offset' in str(error)
        else: raise AssertionError('Oversized RAM program accepted')

        client.command(BEGIN,struct.pack('<II',len(program),zlib.crc32(program)^1))
        assert int(values(client.info())['UI'])==2
        assert client.command(DATA,struct.pack('<I',0)+program)==struct.pack('<I',len(program))
        try: client.command(END,retries=0)
        except RuntimeError as error: assert 'invalid/incomplete image' in str(error)
        else: raise AssertionError('Incorrect user-program CRC accepted')
        assert int(values(client.info())['UI'])==6
        wait_ui(client,1)
        assert values(client.info())['USER_VALID']=='0'

        client.command(BEGIN,struct.pack('<II',len(program),zlib.crc32(program)))
        client.command(DATA,struct.pack('<I',0)+program)
        client.command(END)
        complete=values(client.info())
        assert complete['USER_VALID']=='1' and int(complete['UI'])==3
        wait_ui(client,1)

        client.command(RUN)
        running=values(client.info())
        assert int(running['USER_STATE'])==3 and int(running['UI'])==4
        client.command(DISPLAY,bytes.fromhex('00081c3e7f3e1c080000000000'))
        assert int(values(client.info())['UI'])==5
        client.command(FINISH)
        finished=values(client.info())
        assert int(finished['USER_STATE'])==2 and int(finished['UI'])==1

        client.command(RUN)
        client.command(STOP)
        assert int(values(client.info())['UI'])==1
        print('RAM download, CRC, download/complete/standby/run/user/finish states PASSED',flush=True)

        client.command(RESET,retries=0); client.close()
        client=reconnect('KLQ USB BOOT ')
        client.command(RUN,retries=0); client.close()
        client=reconnect('KLQ ROBOT FW ')
        after_reset=values(client.info())
        assert after_reset['USER_VALID']=='0' and int(after_reset['USER_STATE'])==0
        print('Reset clears RAM user program state PASSED',flush=True)
    finally:
        client.close()


if __name__=='__main__': main()
