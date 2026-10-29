"""No-hardware cleanup and stale-command tests for bounded motor diagnostics."""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock
import test_motor_live as live
from host_link import HostLink, frame

UID = struct.pack('<III', 0x4150354C, 0x38323609, 0x00D86C62)


class Clock:
    value = 0
    def now(self): return self.value
    def sleep(self, seconds): self.value += seconds


class Link:
    sensor_port = 3
    def __init__(self):
        self.usb = Mock()
        self.usb.request.return_value = b'KLQ ROBOT FW 0.10; EXT_UPGRADE=1; EXT_PORTS=3; EXT_MOTOR=1'
        self.commands = []
        self.opened = False
        self.closed = False
        self.pwm = 0
        self.fail = None
        self.wrong_uid = False
        self.status_bad = False
        self.stop_fail = False
    def open(self): self.opened = True; return self
    def close(self): self.closed = True
    def request(self, cmd, payload=b'', **kw):
        assert kw['retries'] == 1
        self.commands.append(cmd)
        if cmd == 1:
            info = bytearray(36)
            struct.pack_into('<HBBHHHHI', info, 0, 1, 1, 2, 2, 2, 2, 0, 0x20000)
            info[24:36] = b'x'*12 if self.wrong_uid else UID
            return bytes(info)
        if cmd == live.SET_PWM:
            self.pwm = struct.unpack('<h', payload)[0]
            if self.fail: raise self.fail
            return payload
        if cmd == live.STOP:
            if self.stop_fail and self.pwm: raise TimeoutError('stop lost')
            self.pwm = 0
            return b''
        if cmd == live.STATUS:
            return struct.pack('<IhBB', 100, self.pwm, int(self.status_bad), int(self.pwm != 0))
        raise AssertionError(cmd)


class CleanupTests(unittest.TestCase):
    def run_tool(self, link, pwm=200, seconds=.11):
        clock = Clock()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'result.json'
            try:
                result = live.exercise(link, UID, pwm, seconds, report=path, now=clock.now, sleep=clock.sleep)
            finally:
                self.saved = json.loads(path.read_text())
            return result
    def test_explicit_high_pwm_guard_and_cleanup(self):
        for pwm,allow,success in ((800,False,False),(800,True,True),(801,True,False)):
            link=Link();clock=Clock()
            if success:
                result=live.exercise(link,UID,pwm,.11,now=clock.now,sleep=clock.sleep,allow_high_pwm=allow)
                self.assertTrue(result['stop_confirmed'] and result['close_confirmed'])
                self.assertIn(live.SET_PWM,link.commands)
                self.assertEqual(link.pwm,0)
            else:
                with self.assertRaises(ValueError):live.exercise(link,UID,pwm,.11,now=clock.now,sleep=clock.sleep,allow_high_pwm=allow)
                self.assertNotIn(live.SET_PWM,link.commands)
    def test_normal_stops_and_closes(self):
        link = Link(); result = self.run_tool(link)
        self.assertEqual(result['result'], 'PASS')
        self.assertEqual(link.commands.count(live.SET_PWM), 3)
        self.assertEqual(link.commands[-2:], [live.STOP, live.STATUS])
        self.assertTrue(link.closed and result['stop_confirmed'])
    def test_bad_identity_has_no_motion(self):
        link = Link(); link.wrong_uid = True
        with self.assertRaises(ValueError): self.run_tool(link)
        self.assertNotIn(live.SET_PWM, link.commands)
        self.assertTrue(link.closed)
    def test_bad_capability_has_no_motion(self):
        link = Link(); link.usb.request.return_value = b'KLQ ROBOT FW 0.9; EXT_UPGRADE=1'
        with self.assertRaises(ValueError): self.run_tool(link)
        self.assertFalse(link.opened)
        self.assertNotIn(live.SET_PWM, link.commands)
    def test_bounds_have_no_motion(self):
        for pwm, seconds in ((0,1), (301,1), (-301,1), (200,0), (200,2.001), (200,float('nan'))):
            link = Link()
            with self.subTest(pwm=pwm, seconds=seconds), self.assertRaises(ValueError): self.run_tool(link,pwm,seconds)
            self.assertFalse(link.opened)
            self.assertNotIn(live.SET_PWM, link.commands)
    def test_timeout_never_reapplies_motion(self):
        link = Link(); link.fail = TimeoutError('lost SET acknowledgement')
        with self.assertRaises(TimeoutError): self.run_tool(link)
        self.assertEqual(link.commands.count(live.SET_PWM), 1)
        self.assertEqual(link.commands[-2:], [live.STOP, live.STATUS])
        self.assertTrue(link.closed and self.saved['stop_confirmed'])
        self.assertEqual(link.pwm, 0)
    def test_keyboardinterrupt_stops_closes_and_saves(self):
        link = Link(); link.fail = KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt): self.run_tool(link)
        self.assertEqual(link.commands[-2:], [live.STOP, live.STATUS])
        self.assertTrue(link.closed and self.saved['stop_confirmed'])
        self.assertEqual(self.saved['error_type'], 'KeyboardInterrupt')
    def test_bad_status_aborts_before_set(self):
        link = Link(); link.status_bad = True
        with self.assertRaises(ValueError): self.run_tool(link)
        self.assertNotIn(live.SET_PWM, link.commands)
        self.assertTrue(link.closed)
    def test_stop_failure_still_checks_status_and_closes(self):
        link = Link(); link.stop_fail = True
        with self.assertRaises(RuntimeError): self.run_tool(link)
        self.assertEqual(link.commands[-2:], [live.STOP, live.STATUS])
        self.assertTrue(link.closed)
        self.assertFalse(self.saved['stop_confirmed'])
    def test_cli_invalid_input_replaces_old_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'result.json'
            path.write_text('{"result": "PASS"}')
            result = live.main(['--sensor-port','3','--uid','4150354C/38323609/00D86C62','--pwm','301','--report',str(path)])
            self.assertEqual(result, 1)
            saved = json.loads(path.read_text())
            self.assertEqual(saved['result'], 'FAIL')
            self.assertFalse(saved['motion_requested'])
    def test_uid_parser(self):
        self.assertEqual(live.uid_bytes('4150354C/38323609/00D86C62'), UID)
        self.assertEqual(live.uid_bytes('4150354C3832360900D86C62'), UID)
        for text in ('4150354C/38323609/00D86C6', 'x'*24, '/4150354C/38323609/00D86C62'):
            with self.assertRaises(ValueError): live.uid_bytes(text)
    def test_host_link_forces_single_set_attempt(self):
        link = HostLink(None, 3, 'motor', poll_interval=0)
        link.opened = True
        link.usb = Mock()
        link.usb.request.side_effect = [b'', b'\x02\x01']
        with self.assertRaises(TimeoutError): link.request(live.SET_PWM, struct.pack('<h',200), retries=8)
        self.assertEqual(link.usb.request.call_count, 2)
        self.assertEqual(link.usb.request.call_args_list[0].args[0], 0x41)


if __name__ == '__main__': unittest.main(verbosity=2)
