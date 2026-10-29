"""Read-only IR UART checks; RESET is sent only with --reset."""
import argparse
import binascii
import json
import struct
import sys
import time

MAGIC = b"KLQ1"
MAX_PAYLOAD = 132
CMD_INFO = 0x0001
CMD_ECHO = 0x0006
CMD_RESET = 0x0007
CMD_GET_SAMPLE = 0x0101
CMD_GET_STATUS = 0x0103
CMD_SELF_TEST = 0x0104


def encode_frame(sequence, command, payload=b""):
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("request payload exceeds 132 bytes")
    prefix = MAGIC + struct.pack("<IHH", sequence, command, len(payload))
    return prefix + struct.pack("<I", binascii.crc32(prefix + payload)) + payload


def decode_frame(data):
    if len(data) < 20 or data[:4] != MAGIC:
        raise ValueError("invalid KLQ1 response header")
    sequence, command, length, crc = struct.unpack_from("<IHHI", data, 4)
    if not 4 <= length <= MAX_PAYLOAD or len(data) != 16 + length:
        raise ValueError("invalid KLQ1 response length")
    if binascii.crc32(data[:12] + data[16:]) != crc:
        raise ValueError("KLQ1 response CRC mismatch")
    return sequence, command, struct.unpack_from("<I", data, 16)[0], data[20:]


def read_response(port, deadline):
    data = bytearray()
    while time.monotonic() < deadline:
        data.extend(port.read(64))
        while data:
            start = data.find(MAGIC)
            if start < 0:
                del data[:-3]
                break
            if start:
                del data[:start]
            if len(data) < 16:
                break
            length = struct.unpack_from("<H", data, 10)[0]
            if not 4 <= length <= MAX_PAYLOAD:
                del data[0]
                continue
            total = 16 + length
            if len(data) < total:
                break
            frame = bytes(data[:total])
            try:
                result = decode_frame(frame)
            except ValueError:
                del data[0]
                continue
            del data[:total]
            yield result
    raise TimeoutError("IR response timeout (no matching valid frame)")


def transact(port, sequence, command, payload=b"", timeout=0.5):
    port.reset_input_buffer()
    port.write(encode_frame(sequence, command, payload))
    port.flush()
    for seq, response_command, status, body in read_response(port, time.monotonic() + timeout):
        if seq != sequence or response_command != command | 0x8000:
            continue
        if status:
            raise ValueError(f"command 0x{command:04x} returned status {status}")
        return body
    raise TimeoutError("IR response timeout")


def decode_info(body):
    if len(body) != 36:
        raise ValueError("INFO data must contain 36 bytes")
    fields = struct.unpack("<HBBHHHHIIHHIII", body)
    keys = ("protocol", "mode", "device_class", "board_id", "hw_compat", "sensor_type",
            "reserved", "firmware", "capabilities", "max_payload", "max_image_size",
            "uid0", "uid1", "uid2")
    info = dict(zip(keys, fields))
    if tuple(info[k] for k in ("protocol", "mode", "device_class", "board_id", "hw_compat", "sensor_type")) != (1, 1, 1, 3, 1, 2):
        raise ValueError(f"connected device is not IR_V1: {info}")
    if info["max_payload"] != MAX_PAYLOAD or info["capabilities"] & 7 != 7:
        raise ValueError("IR does not advertise the required protocol capabilities")
    return info


def decode_sample(body):
    if len(body) != 28:
        raise ValueError("IR GET_SAMPLE data must contain 28 bytes")
    values = struct.unpack("<IIhhHHHHHHBBBB", body)
    keys = ("sequence", "capture_time_ms", "left_delta", "right_delta", "left_value",
            "right_value", "left_ambient", "left_active", "right_ambient", "right_active",
            "status", "quality_flags", "driver_state", "reserved")
    sample = dict(zip(keys, values))
    if sample["status"] > 3 or sample["quality_flags"] & 0xC0 or sample["driver_state"] > 3 or sample["reserved"]:
        raise ValueError("invalid IR sample status/reserved field")
    for side in ("left", "right"):
        if sample[side + "_value"] > 1000:
            raise ValueError("normalized IR value exceeds 1000")
        if sample[side + "_ambient"] > 4095 or sample[side + "_active"] > 4095:
            raise ValueError("raw IR ADC value exceeds 12-bit range")
        if sample["status"] in (1, 2) and sample[side + "_delta"] != sample[side + "_ambient"] - sample[side + "_active"]:
            raise ValueError("IR signed difference disagrees with raw ADC readings")
    return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="UART adapter wired to IR PA3/PA4, common GND")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--interval", type=float, default=0.05)
    parser.add_argument("--reset", action="store_true", help="explicitly send RESET after checks")
    args = parser.parse_args()
    if not 1 <= args.samples <= 10000 or not 0 <= args.interval <= 60:
        parser.error("samples must be 1..10000 and interval 0..60 seconds")
    try:
        import serial
        with serial.Serial(args.port, args.baud, timeout=0.02) as port:
            info = decode_info(transact(port, 1, CMD_INFO))
            print("INFO " + json.dumps(info))
            if transact(port, 2, CMD_ECHO, b"IR") != b"IR":
                raise ValueError("ECHO payload mismatch")
            body = transact(port, 3, CMD_SELF_TEST)
            if len(body) != 4:
                raise ValueError("SELF_TEST data must contain four bytes")
            print("SELF_TEST " + json.dumps(dict(zip(("uart_init_ok", "adc_init_ok", "measurement_seen", "reserved"), body))))
            if body[0] != 1 or body[1] != 1 or body[3]:
                raise ValueError("IR UART/ADC self-test failed")
            body = transact(port, 4, CMD_GET_STATUS)
            if len(body) != 16:
                raise ValueError("GET_STATUS data must contain 16 bytes")
            print("STATUS " + json.dumps(dict(zip(("uptime_ms", "uart_error_count", "frame_error_count", "measurement_count", "adc_timeout_count", "driver_state", "reset_flags"), struct.unpack("<IHHIHBB", body)))))
            measurement_seen = False
            capture_sequences = set()
            for i in range(args.samples):
                sample = decode_sample(transact(port, 5 + i, CMD_GET_SAMPLE))
                print("SAMPLE " + json.dumps(sample))
                if sample["status"] == 3:
                    raise ValueError("IR sample reports an ADC hardware fault")
                measurement_seen |= sample["status"] in (1, 2)
                if sample["status"] in (1, 2):
                    capture_sequences.add(sample["sequence"])
                if i + 1 < args.samples:
                    time.sleep(args.interval)
            if not measurement_seen:
                raise ValueError("IR returned no complete sample during this check")
            if args.samples > 1 and args.interval >= 0.02 and len(capture_sequences) < 2:
                raise ValueError("IR capture sequence did not advance during this check")
            if args.reset:
                if transact(port, 5 + args.samples, CMD_RESET):
                    raise ValueError("RESET response contains unexpected data")
                print("RESET acknowledged")
    except (ImportError, OSError, ValueError, TimeoutError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
