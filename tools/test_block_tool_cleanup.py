"""Offline fault injection; never constructs a real serial client."""
import copy
import subprocess
import sys
from unittest.mock import patch

import test_blocks_live as live


class Injected(Exception):
    pass


def main_path(stage, stop_bad=False, close_bad=False, save_bad=False, cleanup_bad=False):
    events, reports = [], []
    original = Injected('original ' + stage)

    class FakeClient:
        def __init__(self, port, timeout):
            assert port == 'COM_FAKE' and timeout == 2
            self.pressed = 0
        def info(self):
            if stage == 'info':
                raise original
            return 'WRONG' if stage == 'assert' else 'KLQ ROBOT FW 0.8;USER_STATE=2;PY_ERROR=0'
        def command(self, cmd, **kw):
            events.append('stop')
            if stop_bad:
                raise Injected('stop failure')
        def close(self):
            events.append('close')
            if close_bad:
                raise Injected('close failure')

    def snap(client):
        if stage == 'snapshot':
            raise original
        raise AssertionError('unexpected stage')

    def save(report, path):
        events.append('save')
        reports.append(copy.deepcopy(report))
        if save_bad:
            raise OSError('save failure')

    def clean(port):
        events.append('clear')
        assert port == 'COM_FAKE'
        if cleanup_bad:
            raise Injected('cleanup failure')
        return {'final_port': 'COM_NEW'}

    with patch.object(live, 'Client', FakeClient), patch.object(live, 'snapshot', snap), \
            patch.object(live, 'save_report', save), patch.object(live, 'clear_program', clean), \
            patch.object(sys, 'argv', ['offline', '--port', 'COM_FAKE', '--report', 'unused.json']):
        try:
            live.main()
        except Exception as exc:
            if stage == 'assert':
                assert type(exc) is AssertionError and str(exc) == 'WRONG', exc
            else:
                assert exc is original, (stage, exc)
        else:
            raise AssertionError('original exception swallowed')
    assert events == ['stop', 'close', 'save', 'clear', 'save'], events
    assert reports[0]['result'] == 'FAIL'
    if stop_bad:
        assert 'stop_error' in reports[-1]
    if close_bad:
        assert 'close_error' in reports[-1]
    if save_bad:
        assert 'initial_report_error' in reports[-1]
    if cleanup_bad:
        assert 'cleanup_error' in reports[-1]
    print('PASS main fault cleanup:', stage, stop_bad, close_bad, save_bad, cleanup_bad)


def clear_path():
    commands, events = [], []
    class FakeClient:
        def __init__(self, port):
            self.serial = type('Serial', (), {'port': port})()
        def info(self):
            return 'KLQ ROBOT FW 0.8;USER_VALID=0;USER_STATE=0;UI=1'
        def close(self):
            events.append('close ' + self.serial.port)
    clients = [FakeClient('COM_BOOT_NEW'), FakeClient('COM_FW_NEW')]
    def run(command, check, timeout):
        assert check and timeout == 8
        commands.append(command)
        if command[-1] == 'reset':
            raise subprocess.TimeoutExpired(command, timeout)
        raise subprocess.CalledProcessError(1, command)
    def reconnect(prefix):
        events.append('reconnect ' + prefix)
        return clients.pop(0)
    with patch.object(live.subprocess, 'run', run), patch.object(live, 'reconnect', reconnect), \
            patch.object(live.time, 'sleep', lambda s: None):
        report = live.clear_program('COM_ORIGINAL')
    assert commands[0][-2:] == ['COM_ORIGINAL', 'reset']
    assert commands[1][-2:] == ['COM_BOOT_NEW', 'run']
    assert report['final_port'] == 'COM_FW_NEW'
    assert len(report['usb_transition_warnings']) == 2
    assert events == ['reconnect KLQ USB BOOT ', 'close COM_BOOT_NEW',
                      'reconnect KLQ ROBOT FW ', 'close COM_FW_NEW'], events
    print('PASS transitions: timeout/nonzero recover, expected prefixes, COM changes, clients closed')


def main():
    for stage in ('info', 'assert', 'snapshot'):
        main_path(stage)
        main_path(stage, stop_bad=True, close_bad=True, save_bad=True, cleanup_bad=True)
    clear_path()
    print('RESULT PASS 7 offline fault-injection cases; no hardware access')


if __name__ == '__main__':
    main()
