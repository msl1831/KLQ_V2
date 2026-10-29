import binascii
import contextlib
import io
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import host_link
import pack
import update


def package(kind='ul', size=272):
    app = bytearray(b'\x5a' * size)
    struct.pack_into('<II', app, 0, 0x20000C00, pack.APP + 9)
    struct.pack_into('<4sHHII', app, pack.ID_OFFSET, b'KID1', *pack.PROFILES[kind][:2], pack.APP, 1)
    return pack.update_package(kind, app)


class Controller:
    def __init__(self, kind='ul', mode=0):
        self.kind, self.mode = kind, mode
        self.queue = bytearray()
        self.frames, self.inner, self.data_offsets = [], [], []
        self.capable, self.wrong_token = True, False
        self.token = self.closed_token = None
        self.last_inner, self.result = None, b'\0'
        self.pending = False
        self.drop = set()
        self.dropped = set()
        self.fail_reason = None
        self.bad_inner = None
        self.prefix_noise = False
        self.app, self.offset = bytearray(), 0
        self.serial_closed = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.serial_closed = True

    def flush(self):
        pass

    def read(self, size):
        data = bytes(self.queue[:min(size, 7)])
        del self.queue[:len(data)]
        return data

    def write(self, data):
        self.frames.append(bytes(data))
        seq, cmd, size, crc = struct.unpack_from('<IHHI', data, 4)
        assert data[:4] == b'KLQ1' and len(data) == size + 16
        assert binascii.crc32(data[:12] + data[16:]) == crc
        status, body = self.dispatch(cmd, data[16:])
        if cmd in self.drop and cmd not in self.dropped:
            self.dropped.add(cmd)
            return len(data)
        reply = host_link.frame(seq, cmd | 0x8000, struct.pack('<I', status) + body)
        if self.prefix_noise:
            stale = host_link.frame(seq + 1, cmd | 0x8000, b'\0' * 4)
            bad = bytearray(reply)
            bad[12] ^= 1
            self.queue.extend(b'SC7A20 READY\r\n' + stale + bad)
            self.prefix_noise = False
        self.queue.extend(reply)
        return len(data)

    def dispatch(self, cmd, body):
        if cmd == 1:
            return 0, (b'KLQ ROBOT FW 0.2; EXT_UPGRADE=1; EXT_PORTS=3' if self.capable else b'KLQ ROBOT FW 0.2')
        if cmd == host_link.OPEN:
            port, token, board, hw = struct.unpack('<BIHH', body)
            assert port < 3 and token and (board, hw) in [v[:2] for v in pack.PROFILES.values()]
            self.token = token
            return 0, struct.pack('<I', token ^ int(self.wrong_token))
        token = struct.unpack_from('<I', body)[0]
        if cmd == host_link.CLOSE:
            if token not in (self.token, self.closed_token):
                return 4, b''
            self.closed_token, self.token = token, None
            return 0, b''
        if token != self.token:
            return 4, b''
        if cmd == host_link.XFER:
            inner = body[4:]
            if inner != self.last_inner:
                self.last_inner = inner
                self.inner.append(inner)
                self.result = self.device(inner)
                self.pending = True
            return 0, b''
        if cmd == host_link.POLL:
            if self.pending:
                self.pending = False
                return 0, b'\0'
            return 0, self.result
        return 1, b''

    def device(self, data):
        seq, cmd, size, crc = struct.unpack_from('<IHHI', data, 4)
        assert len(data) == size + 16 and binascii.crc32(data[:12] + data[16:]) == crc
        body, reply, status = data[16:], b'', 0
        if self.fail_reason is not None:
            reason, self.fail_reason = self.fail_reason, None
            return bytes((2, reason))
        if cmd == 1:
            result = bytearray(36)
            board, hw, cls = pack.PROFILES[self.kind]
            typ = {'ul': 1, 'ir': 2, 'motor': 2}[self.kind]
            struct.pack_into('<HBBHHHHIIHH', result, 0, 1, self.mode, cls, board, hw, typ, 0, 0x10000, 8, 132, pack.CAPACITY)
            result[24:36] = b'123456789ABC'
            reply = bytes(result)
        elif cmd == 0x01F0:
            self.mode = 0
        elif cmd in (9, 0x0302):
            pass
        elif cmd == 0x0204:
            reply = struct.pack('<IHHII', pack.APP, pack.CAPACITY, 128, pack.META, 0x1800)
        elif cmd == 0x0200:
            board, hw, size, self.crc = struct.unpack('<HHHI', body)
            if (board, hw) != pack.PROFILES[self.kind][:2]:
                status = 2
            else:
                self.app, self.offset = bytearray(size), 0
                reply = b'\0\0'
        elif cmd == 0x0201:
            offset = struct.unpack_from('<H', body)[0]
            assert offset == self.offset
            self.data_offsets.append(offset)
            self.app[offset:offset + size - 2] = body[2:]
            self.offset += size - 2
            reply = struct.pack('<H', self.offset)
        elif cmd == 0x0202:
            assert self.offset == len(self.app) and binascii.crc32(self.app) == self.crc
            pack.update_package(self.kind, self.app)
        elif cmd == 0x0203:
            self.mode = 1
        else:
            status = 1
        response = bytearray(host_link.frame(seq, cmd | 0x8000, struct.pack('<I', status) + reply))
        if self.bad_inner == 'crc':
            response[12] ^= 1
        elif self.bad_inner == 'sequence':
            response[4] ^= 1
        elif self.bad_inner == 'command':
            response[8] ^= 1
        elif self.bad_inner == 'length':
            response[10] ^= 1
        elif self.bad_inner == 'status':
            response = bytearray(host_link.frame(seq, cmd | 0x8000, struct.pack('<I', 6)))
        return b'\1' + response


class HostTests(unittest.TestCase):
    def link(self, controller, kind='ul', port=1):
        return host_link.HostLink(controller, port, kind, usb_timeout=0.01, poll_interval=0)

    def test_full_upgrade_all_boards_and_ports(self):
        for kind, port in zip(pack.PROFILES, (1, 2, 3)):
            controller = Controller(kind, mode=1)
            with self.link(controller, kind, port) as link:
                self.assertEqual(update.upgrade(link, package(kind)), (kind, 272))
            self.assertIsNone(controller.token)
            self.assertEqual(controller.mode, 1)
            self.assertEqual(controller.data_offsets, [0, 128, 256])
            open_body = next(f[16:] for f in controller.frames if struct.unpack_from('<H', f, 8)[0] == host_link.OPEN)
            self.assertEqual(open_body[0], port - 1)

    def test_controller_capability_checked_before_open(self):
        controller = Controller()
        controller.capable = False
        with self.assertRaisesRegex(ValueError, 'lacks'):
            self.link(controller).open()
        self.assertEqual([struct.unpack_from('<H', f, 8)[0] for f in controller.frames], [1])

    def test_invalid_port_and_kind_before_io(self):
        controller = Controller()
        for port in (0, 4, -1):
            with self.assertRaises(ValueError):
                self.link(controller, port=port)
        with self.assertRaises(ValueError):
            self.link(controller, kind='wrong')
        self.assertEqual(controller.frames, [])

    def test_open_token_mismatch_still_closes(self):
        controller = Controller()
        controller.wrong_token = True
        with self.assertRaisesRegex(ValueError, 'token'):
            self.link(controller).open()
        self.assertIsNone(controller.token)

    def test_identity_rejected_and_session_released(self):
        controller = Controller('ir')
        with self.assertRaisesRegex(ValueError, 'identity'):
            with self.link(controller) as link:
                update.upgrade(link, package())
        self.assertIsNone(controller.token)
        self.assertFalse(controller.app)

    def test_outer_ack_loss_keeps_request_sequences_and_writes_once(self):
        controller = Controller()
        controller.drop = {host_link.OPEN, host_link.XFER, host_link.POLL, host_link.CLOSE}
        with self.link(controller) as link:
            update.upgrade(link, package(), False)
        self.assertEqual(controller.data_offsets, [0, 128, 256])
        for cmd in controller.drop:
            packets = [f for f in controller.frames if struct.unpack_from('<H', f, 8)[0] == cmd]
            self.assertEqual(packets[0], packets[1])
        self.assertIsNone(controller.token)

    def test_terminal_uart_timeout_retries_with_new_inner_sequence(self):
        controller = Controller()
        with self.link(controller) as link:
            controller.fail_reason = 1
            self.assertEqual(len(link.request(1)), 36)
        self.assertEqual(len(controller.inner), 2)
        self.assertNotEqual(controller.inner[0][4:8], controller.inner[1][4:8])

    def test_transport_and_inner_errors_close_session(self):
        for reason in (2, 3, 4, 5, 99):
            controller = Controller()
            with self.assertRaisesRegex(ValueError, 'controller bridge'):
                with self.link(controller) as link:
                    controller.fail_reason = reason
                    link.request(1)
            self.assertIsNone(controller.token)
        for corrupt in ('crc', 'sequence', 'command', 'length', 'status'):
            controller = Controller()
            with self.assertRaises(ValueError):
                with self.link(controller) as link:
                    controller.bad_inner = corrupt
                    link.request(1)
            self.assertIsNone(controller.token)

    def test_unsolicited_text_corruption_and_stale_outer_reply_recovered(self):
        controller = Controller()
        controller.prefix_noise = True
        with self.link(controller) as link:
            self.assertEqual(len(link.request(1)), 36)

    def test_keyboard_interrupt_releases_session(self):
        controller = Controller()
        with self.assertRaises(KeyboardInterrupt):
            with self.link(controller):
                raise KeyboardInterrupt()
        self.assertIsNone(controller.token)

    def test_usb_reset_error_is_distinct_from_remote_status(self):
        controller = Controller()
        with self.link(controller) as link:
            controller.token = None
            with self.assertRaises(host_link.USBError) as error:
                link.request(1)
            self.assertEqual(error.exception.status, 4)
            controller.token = link.token

    def test_cli_checks_package_and_host_options_before_serial(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'image.klq'
            path.write_bytes(b'bad')
            serial = types.SimpleNamespace(Serial=Mock())
            args = ['update.py', '--port', 'COM_TEST', '--image', str(path), '--via-host', '--sensor-port', '1']
            with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args):
                with self.assertRaises(ValueError):
                    update.main()
            serial.Serial.assert_not_called()
            path.write_bytes(package())
            for extras in (['--via-host'], ['--sensor-port', '1']):
                args = ['update.py', '--port', 'COM_TEST', '--image', str(path)] + extras
                with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit):
                        update.main()
            serial.Serial.assert_not_called()

    def test_cli_host_and_direct_routing(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'image.klq'
            path.write_bytes(package())
            controller = Controller()
            serial = types.SimpleNamespace(Serial=Mock(return_value=controller))
            args = ['update.py', '--port', 'COM_TEST', '--image', str(path), '--via-host', '--sensor-port', '2', '--stay-in-boot']
            with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), contextlib.redirect_stdout(io.StringIO()):
                update.main()
            self.assertTrue(controller.serial_closed)
            self.assertIsNone(controller.token)
            self.assertEqual(controller.mode, 0)
            args = ['update.py', '--port', 'COM_TEST', '--image', str(path)]
            direct = Controller()
            serial.Serial.return_value = direct
            with patch.dict(sys.modules, {'serial': serial}), patch.object(sys, 'argv', args), patch.object(update, 'upgrade', return_value=('ul', 272)) as fn, contextlib.redirect_stdout(io.StringIO()):
                update.main()
            self.assertIsInstance(fn.call_args.args[0], update.Link)
            self.assertEqual(direct.frames, [])


if __name__ == '__main__':
    unittest.main()
