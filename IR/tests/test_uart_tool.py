import binascii
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import types
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("ir_uart_check", Path(__file__).parents[1] / "tools/test_sensor_protocol.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


class FakePort:
    def __init__(self, fault=False, wrong_type=False, progressing=False):
        self.commands = []
        self.rx = b""
        self.fault = fault
        self.wrong_type = wrong_type
        self.progressing = progressing
        self.sample_count = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        pass

    def reset_input_buffer(self):
        self.rx = b""

    def flush(self):
        pass

    def write(self, data):
        seq, cmd, size, crc = struct.unpack_from("<IHHI", data, 4)
        assert len(data) == 16 + size
        assert binascii.crc32(data[:12] + data[16:]) == crc
        self.commands.append(cmd)
        if cmd == tool.CMD_INFO:
            body = struct.pack("<HBBHHHHIIHHIII", 1, 1, 1, 1 if self.wrong_type else 3, 1, 1 if self.wrong_type else 2, 0, 0x10000, 7, 132, 0, 1, 2, 3)
        elif cmd == tool.CMD_ECHO:
            body = data[16:]
        elif cmd == tool.CMD_SELF_TEST:
            body = bytes((1, 0 if self.fault else 1, 1, 0))
        elif cmd == tool.CMD_GET_STATUS:
            body = struct.pack("<IHHIHBB", 100, 0, 0, 10, 0, 0, 8)
        elif cmd == tool.CMD_GET_SAMPLE:
            sequence = 10 + self.sample_count if self.progressing else 10
            self.sample_count += 1
            body = struct.pack("<IIhhHHHHHHBBBB", sequence, 95, 2000, -500, 488, 0, 3000, 1000, 1000, 1500, 2, 32, 0, 0)
        else:
            assert cmd == tool.CMD_RESET
            body = b""
        payload = struct.pack("<I", 0) + body
        prefix = b"KLQ1" + struct.pack("<IHH", seq, cmd | 0x8000, len(payload))
        self.rx = prefix + struct.pack("<I", binascii.crc32(prefix + payload)) + payload

    def read(self, count):
        result, self.rx = self.rx[:count], self.rx[count:]
        return result


class ToolTests(unittest.TestCase):
    def run_cli(self, port, *extra):
        serial = types.SimpleNamespace(Serial=lambda *args, **kwargs: port)
        with patch.dict(sys.modules, {"serial": serial}), patch.object(sys, "argv", ["ir_check", "--port", "FAKE", "--samples", "2", "--interval", "0", *extra]), patch.object(tool.time, "sleep"), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return tool.main()

    def test_read_only_by_default(self):
        port = FakePort()
        self.assertEqual(self.run_cli(port), 0)
        self.assertEqual(port.commands, [1, 6, 0x104, 0x103, 0x101, 0x101])

    def test_reset_is_explicit(self):
        port = FakePort()
        self.assertEqual(self.run_cli(port, "--reset"), 0)
        self.assertEqual(port.commands[-1], 7)

    def test_other_sensor_rejected_before_further_commands(self):
        port = FakePort(wrong_type=True)
        self.assertEqual(self.run_cli(port), 1)
        self.assertEqual(port.commands, [1])

    def test_failed_adc_is_failure(self):
        port = FakePort(fault=True)
        self.assertEqual(self.run_cli(port), 1)
        self.assertNotIn(7, port.commands)

    def test_crc_is_checked(self):
        port = FakePort()
        port.write(tool.encode_frame(1, tool.CMD_INFO))
        frame = bytearray(port.rx)
        frame[-1] ^= 1
        with self.assertRaises(ValueError):
            tool.decode_frame(frame)

    def test_signed_sample_and_bad_lengths(self):
        port = FakePort()
        port.write(tool.encode_frame(1, tool.CMD_GET_SAMPLE))
        sample = tool.decode_sample(tool.decode_frame(port.rx)[3])
        self.assertEqual(sample["right_delta"], -500)
        self.assertEqual(sample["right_value"], 0)
        with self.assertRaises(ValueError):
            tool.decode_sample(b"\0" * 16)

    def test_stuck_sequence_fails_with_spaced_samples(self):
        self.assertEqual(self.run_cli(FakePort(), "--interval", "0.02"), 1)

    def test_progressing_sequence_passes_with_spaced_samples(self):
        self.assertEqual(self.run_cli(FakePort(progressing=True), "--interval", "0.02"), 0)

    def test_single_sample_remains_diagnostic(self):
        self.assertEqual(self.run_cli(FakePort(), "--samples", "1", "--interval", "0.02"), 0)

    def test_reserved_quality_and_invalid_driver_state_rejected(self):
        port = FakePort()
        port.write(tool.encode_frame(1, tool.CMD_GET_SAMPLE))
        body = tool.decode_frame(port.rx)[3]
        for offset, value in ((25, 0x40), (25, 0x80), (26, 4)):
            with self.subTest(offset=offset, value=value):
                malformed = bytearray(body)
                malformed[offset] = value
                with self.assertRaises(ValueError):
                    tool.decode_sample(malformed)


if __name__ == "__main__":
    unittest.main()
