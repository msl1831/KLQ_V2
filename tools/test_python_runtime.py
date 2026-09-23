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
klq.motor_power(2)
klq.move_power(2)
klq.display_number(7)
klq.wait(0.05)
for _ in range(3):
    klq.display_face(1)
    klq.wait(0.05)
klq.display_pattern("00081c3e7f3e1c080000000000")
klq.wait(0.05)
klq.display_off()
''')
        t=time.monotonic(); client.command(RUN)
        time.sleep(.08)
        assert int(values(client.info())['USER_STATE'])==3
        done=wait_state(client,2)
        assert int(done['PY_ERROR'])==0 and time.monotonic()-t>=.20
        print('calls, decimal wait, finite loop and natural completion PASSED')

        load(client,'import klq\nklq.wait(0.5)\n')
        client.command(RUN)
        done=wait_state(client,2,timeout=2)
        assert int(done['PY_ERROR'])==0
        print('half-second wait completes PASSED')

        load(client,'import klq\nklq.missing()\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==2 and int(failed['PY_LINE'])==2
        print('unknown API error and line PASSED')

        assert int(failed['USER_VALID'])==1 and int(failed['USER_LENGTH'])==len('import klq\nklq.missing()\n')
        wait_ui(client,1)
        client.command(RUN)
        assert int(wait_state(client,2)['PY_ERROR'])==2
        print('failed program exits and remains in RAM for retry PASSED')

        load(client,'import klq\nfor _ in range(2):\nklq.wait(0.1)\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==1 and int(failed['PY_LINE'])==2
        print('syntax error exits automatically PASSED')

        load(client,'import klq\nklq.wait_ir(1, 1, 50)\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==6 and int(failed['PY_LINE'])==2
        print('unsupported hardware error PASSED')

        load(client,'import klq\nklq.motor_run(1, 1)\n')
        client.command(RUN)
        failed=wait_state(client,2)
        assert int(failed['PY_ERROR'])==6 and int(failed['PY_LINE'])==2
        print('motor protocol missing reports unsupported PASSED')

        load(client,'import klq\nfor _ in range(0):\n    klq.missing()\nfor _ in range(2):\n    for _ in range(2):\n        klq.wait(0.02)\nklq.stop()\n')
        client.command(RUN)
        done=wait_state(client,2)
        assert int(done['PY_ERROR'])==0
        print('zero-count skip, nested loops and stop block PASSED')

        load(client,'import klq\nklq.wait_button(0)\n')
        client.command(RUN)
        button=wait_state(client,2)
        assert int(button['PY_ERROR'])==0
        print('PC3 released state PASSED')

        load(client,'import klq\nklq.wait_distance(1, -1, 50)\n')
        client.command(RUN)
        time.sleep(.2)
        assert int(values(client.info())['USER_STATE'])==3
        client.command(STOP)
        assert int(wait_state(client,2)['PY_ERROR'])==0
        print('offline distance wait remains stoppable PASSED')

        load(client,'import klq\nklq.wait_tilt(0)\n')
        client.command(RUN)
        time.sleep(.15)
        assert int(values(client.info())['USER_STATE'])==3
        client.command(STOP)
        assert int(wait_state(client,2)['PY_ERROR'])==0
        print('stationary tilt wait remains stoppable PASSED')

        load(client,'import klq\nwhile True:\n    klq.wait(0.05)\n')
        client.command(RUN); time.sleep(.12)
        assert int(values(client.info())['USER_STATE'])==3
        t=time.monotonic(); client.command(STOP)
        stopped=wait_state(client,2)
        assert time.monotonic()-t<.5 and int(stopped['PY_ERROR'])==0
        print('cooperative forever loop and USB stop PASSED')

        load(client,'import klq\nwhile True:\n    pass\n')
        client.command(RUN); time.sleep(.12)
        assert int(values(client.info())['USER_STATE'])==3
        t=time.monotonic(); client.command(STOP)
        stopped=wait_state(client,2)
        assert time.monotonic()-t<.5 and int(stopped['USER_VALID'])==1
        print('no-wait infinite loop remains stoppable and retained PASSED')

        client.command(RUN)
        replacement=b'import klq\nklq.display_number(8)\n'
        client.command(BEGIN,struct.pack('<II',len(replacement),zlib.crc32(replacement)))
        client.command(DATA,struct.pack('<I',0)+replacement)
        client.command(END)
        ready=wait_state(client,2)
        assert int(ready['USER_LENGTH'])==len(replacement) and int(ready['USER_VALID'])==1
        print('new download replaces running loop without reboot PASSED')

        load(client,'import klq\nfor _ in range(2):\n    klq.display_face(1)\n    klq.wait(0.5)\n    klq.display_off()\n    klq.wait(0.5)\n')
        client.command(RUN)
        client.close()
        time.sleep(2.8)
        client=reconnect('KLQ ROBOT FW ')
        completed=values(client.info())
        assert int(completed['USER_STATE'])==2 and int(completed['PY_ERROR'])==0, completed
        print('program completes with USB port closed PASSED')
    finally:
        try: client.command(STOP)
        except Exception: pass
        client.close()

if __name__=='__main__': main()
