"""Live tests for the RAM-only KLQ Python subset. Never writes internal Flash."""
import struct, time, zlib
from klq_usb import BEGIN, DATA, END, RUN, STOP, Client, find_port
from test_user_program import reconnect, values, wait_ui

def load(client, source):
    data=source.encode('ascii')
    client.command(STOP)
    client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
    client.command(DATA,struct.pack('<I',0)+data)
    client.command(END)
    wait_ui(client,1)

def wait_state(client, state, timeout=3):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        info=values(client.info())
        if int(info['USER_STATE'])==state: return info
        time.sleep(.02)
    raise AssertionError('state timeout: '+str(info))

def main():
    client=reconnect('KLQ ROBOT FW ')
    try:
        load(client,'''import klq
klq.display_number(7)
klq.wait(0.05)
for _ in range(3):
    klq.display_face(1)
    klq.wait(0.05)
klq.display_pattern("00081c3e7f3e1c080000000000")
klq.wait(0.05)
''')
        t=time.monotonic(); client.command(RUN)
        time.sleep(.08)
        assert int(values(client.info())['USER_STATE'])==3
        done=wait_state(client,2)
        assert int(done['PY_ERROR'])==0 and time.monotonic()-t>=.20
        print('calls, decimal wait, finite loop and natural completion PASSED')

        load(client,'import klq\nklq.missing()\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==2 and int(failed['PY_LINE'])==2
        print('unknown API error and line PASSED')

        load(client,'import klq\nklq.wait_ir(1, 1, 50)\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==6 and int(failed['PY_LINE'])==2
        print('unsupported hardware error PASSED')

        load(client,'import klq\nwhile True:\n    klq.wait(0.05)\n')
        client.command(RUN); time.sleep(.12)
        assert int(values(client.info())['USER_STATE'])==3
        t=time.monotonic(); client.command(STOP)
        stopped=wait_state(client,2)
        assert time.monotonic()-t<.5 and int(stopped['PY_ERROR'])==0
        print('cooperative forever loop and USB stop PASSED')
    finally:
        client.close()

if __name__=='__main__': main()
