"""ENTER-only UART-error recovery must revalidate boot identity before any write."""
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import host_link
import pack
import update
from test_host_tool import Controller, package

ENTER, LAYOUT, BEGIN, DATA, END, RUN = 0x01F0, 0x0204, 0x0200, 0x0201, 0x0202, 0x0203


class ResetEdgeController(Controller):
    def __init__(self, *, recovery_failure=None, enter_reason=2,
                 enter_exception=None, data_reason=None):
        super().__init__('ir', mode=1)
        self.recovery_failure = recovery_failure
        self.enter_reason, self.enter_exception = enter_reason, enter_exception
        self.data_reason = data_reason
        self.entered, self.recovery_seen = False, False
        self.commands = []

    @staticmethod
    def response(request, payload):
        seq, cmd = struct.unpack_from('<IH', request, 4)
        return b'\1' + host_link.frame(seq, cmd | 0x8000, b'\0' * 4 + payload)

    def device(self, request):
        cmd = struct.unpack_from('<H', request, 8)[0]
        self.commands.append(cmd)
        if cmd == ENTER:
            self.entered, self.mode = True, 0
            if self.enter_exception is not None:
                raise self.enter_exception
            return bytes((2, self.enter_reason))
        if cmd == 1 and self.entered and not self.recovery_seen:
            self.recovery_seen = True
            if self.recovery_failure == 'uart':
                return b'\2\2'
            reply = super().device(request)
            info = bytearray(reply[21:])
            if self.recovery_failure == 'application':
                info[2] = 1
            elif self.recovery_failure == 'uid':
                info[24] ^= 1
            elif self.recovery_failure == 'board':
                struct.pack_into('<H', info, 4, 1)
            return self.response(request, info)
        if cmd == LAYOUT and self.recovery_failure == 'layout':
            return self.response(request, struct.pack('<IHHII', pack.APP + 128,
                                  pack.CAPACITY, pack.PAGE, pack.META, 0x1800))
        if cmd == DATA and self.data_reason is not None:
            return bytes((2, self.data_reason))
        return super().device(request)


class TransitionRecoveryTests(unittest.TestCase):
    def link(self, controller):
        return host_link.HostLink(controller, 2, 'ir', usb_timeout=0.01, poll_interval=0)

    def test_enter_uart_error_revalidates_and_commits_whole_image(self):
        controller = ResetEdgeController()
        image = package('ir')
        with self.link(controller) as link:
            self.assertEqual(update.upgrade(link, image), ('ir', 272))
        self.assertEqual(controller.commands[:6], [1, ENTER, 1, 9, 1, LAYOUT])
        self.assertEqual(controller.commands.count(BEGIN), 1)
        self.assertEqual(controller.commands.count(END), 1)
        self.assertEqual(controller.data_offsets, [0, 128, 256])
        self.assertEqual(controller.app, pack.read_package(image)[1])
        self.assertEqual(controller.commands[-2:], [RUN, 1])
        self.assertEqual(controller.mode, 1)
        self.assertIsNone(controller.token)

    def test_recovered_old_or_unknown_boot_never_reaches_begin(self):
        for version in (0, 0x10000, 0x10001):
            with self.subTest(version=version):
                controller = ResetEdgeController()
                controller.boot_version = version
                with self.assertRaisesRegex(ValueError, r'boot 1\.0\.2 required.*nothing erased'):
                    with self.link(controller) as link:
                        update.upgrade(link, package('ir'))
                self.assertEqual(controller.commands, [1, ENTER, 1, 9, 1])
                self.assertNotIn(BEGIN, controller.commands)
                self.assertNotIn(DATA, controller.commands)
                self.assertNotIn(RUN, controller.commands)
                self.assertFalse(controller.app)
                self.assertIsNone(controller.token)

    def test_failed_recovery_identity_or_layout_never_reaches_begin(self):
        for failure in ('application', 'uid', 'board', 'layout', 'uart'):
            with self.subTest(failure=failure):
                controller = ResetEdgeController(recovery_failure=failure)
                with self.assertRaises(ValueError):
                    with self.link(controller) as link:
                        update.upgrade(link, package('ir'))
                self.assertEqual(controller.commands[:3], [1, ENTER, 1])
                self.assertEqual(LAYOUT in controller.commands, failure == 'layout')
                self.assertNotIn(BEGIN, controller.commands)
                self.assertNotIn(DATA, controller.commands)
                self.assertFalse(controller.app)
                self.assertIsNone(controller.token)

    def test_other_enter_bridge_errors_are_not_recovered(self):
        for reason in (3, 4, 5, 99):
            with self.subTest(reason=reason):
                controller = ResetEdgeController(enter_reason=reason)
                with self.assertRaises(host_link.BridgeError) as caught:
                    with self.link(controller) as link:
                        update.upgrade(link, package('ir'))
                self.assertEqual(caught.exception.reason, reason)
                self.assertEqual(controller.commands, [1, ENTER])
                self.assertNotIn(BEGIN, controller.commands)
                self.assertIsNone(controller.token)

    def test_data_uart_error_is_not_swallowed_or_restarted(self):
        controller = ResetEdgeController(data_reason=2)
        with self.assertRaises(host_link.BridgeError) as caught:
            with self.link(controller) as link:
                update.upgrade(link, package('ir'))
        self.assertEqual(caught.exception.reason, 2)
        self.assertEqual(controller.commands.count(BEGIN), 1)
        self.assertEqual(controller.commands.count(DATA), 1)
        self.assertEqual(controller.commands[-1], DATA)
        self.assertNotIn(END, controller.commands)
        self.assertNotIn(RUN, controller.commands)
        self.assertIsNone(controller.token)

    def test_plain_value_error_on_enter_is_not_swallowed(self):
        original = ValueError('invalid UART response')
        controller = ResetEdgeController(enter_exception=original)
        with self.assertRaises(ValueError) as caught:
            with self.link(controller) as link:
                update.upgrade(link, package('ir'))
        self.assertIs(caught.exception, original)
        self.assertEqual(controller.commands, [1, ENTER])
        self.assertFalse(controller.app)
        self.assertIsNone(controller.token)

    def test_host_link_preserves_typed_bridge_uart_reason(self):
        controller = Controller('ir')
        with self.link(controller) as link:
            controller.fail_reason = 2
            with self.assertRaises(host_link.BridgeError) as caught:
                link.request(1)
        self.assertIsInstance(caught.exception, ValueError)
        self.assertEqual(caught.exception.reason, 2)
        self.assertIn('UART error', str(caught.exception))
        self.assertIsNone(controller.token)


if __name__ == '__main__':
    unittest.main()
