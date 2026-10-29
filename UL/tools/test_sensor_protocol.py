import argparse
import binascii
import struct
import sys
import time

MAGIC = b"KLQ1"
CMD_INFO = 0x0001
CMD_ECHO = 0x0006
CMD_GET_SAMPLE = 0x0101
CMD_GET_STATUS = 0x0103


def encode_frame(sequence: int, command: int, payload: bytes = b"") -> bytes:
    prefix = MAGIC + struct.pack("<IHH", sequence, command, len(payload))
    crc = binascii.crc32(prefix + payload) & 0xFFFFFFFF
    return prefix + struct.pack("<I", crc) + payload


def decode_frame(data: bytes) -> tuple[int, int, int, bytes]:
    if len(data) < 16 or data[:4] != MAGIC:
        raise ValueError("invalid KLQ1 header")
    sequence, command, length, crc = struct.unpack_from("<IHHI", data, 4)
    if len(data) != 16 + length:
        raise ValueError("invalid KLQ1 length")
    payload = data[16:]
    expected = binascii.crc32(data[:12] + payload) & 0xFFFFFFFF
    if crc != expected:
        raise ValueError(f"CRC mismatch: got {crc:08x}, expected {expected:08x}")
    if length < 4:
        raise ValueError("response has no status")
    status = struct.unpack_from("<I", payload, 0)[0]
    return sequence, command, status, payload[4:]


def read_response(port, timeout: float = 0.5) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    expected = None
    while time.monotonic() < deadline:
        chunk = port.read(64)
        if chunk:
            data.extend(chunk)
            start = data.find(MAGIC)
            if start > 0:
                del data[:start]
            if len(data) >= 16 and expected is None:
                expected = 16 + struct.unpack_from("<H", data, 10)[0]
            if expected is not None and len(data) >= expected:
                return bytes(data[:expected])
    raise TimeoutError("sensor response timeout")


def transact(port, sequence: int, command: int, payload: bytes = b""):
    port.reset_input_buffer()
    port.write(encode_frame(sequence, command, payload))
    port.flush()
    return decode_frame(read_response(port))


def main() -> int:
    parser = argparse.ArgumentParser(description="KLQ UL sensor protocol smoke test")
    parser.add_argument("--port", required=True, help="UART adapter connected to sensor PA3/PA4")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        print("pyserial is required", file=sys.stderr)
        return 2

    with serial.Serial(args.port, args.baud, timeout=0.02) as port:
        for sequence, command, payload in (
            (1, CMD_ECHO, b"UL"),
            (2, CMD_INFO, b""),
            (3, CMD_GET_SAMPLE, b""),
            (4, CMD_GET_STATUS, b""),
        ):
            seq, response_command, status, body = transact(port, sequence, command, payload)
            print(f"seq={seq} cmd=0x{response_command:04X} status={status} data={body.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
