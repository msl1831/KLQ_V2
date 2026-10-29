"""Update PY32 peripherals by direct UART or through the KLQ controller USB port."""
import argparse
import binascii
import struct
import time
from pathlib import Path
from pack import APP, META, PAGE, CAPACITY, PROFILES, read_package


def encode(seq, cmd, payload=b''):
    prefix = b'KLQ1' + struct.pack('<IHH', seq, cmd, len(payload))
    return prefix + struct.pack('<I', binascii.crc32(prefix + payload)) + payload


class Link:
    def __init__(self, port):
        self.port, self.sequence, self.buffer = port, 0, bytearray()

    def request(self, cmd, payload=b'', timeout=0.5, retries=3):
        self.sequence = (self.sequence + 1) & 0xffffffff
        seq = self.sequence
        frame = encode(seq, cmd, payload)
        for _ in range(retries):
            self.buffer.clear()  # Discard an incomplete frame left by a previous timeout.
            self.port.write(frame)
            self.port.flush()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                self.buffer.extend(self.port.read(148))
                while len(self.buffer) >= 4:
                    pos = self.buffer.find(b'KLQ1')
                    if pos < 0:
                        del self.buffer[:-3]
                        break
                    del self.buffer[:pos]
                    if len(self.buffer) < 16:
                        break
                    rseq, rcmd, size, crc = struct.unpack_from('<IHHI', self.buffer, 4)
                    if size > 132 or size < 4:
                        del self.buffer[0]
                        continue
                    if len(self.buffer) < size + 16:
                        break
                    data = bytes(self.buffer[:size + 16])
                    if binascii.crc32(data[:12] + data[16:]) != crc:
                        del self.buffer[0]
                        continue
                    del self.buffer[:size + 16]
                    if rseq != seq or rcmd != cmd | 0x8000:
                        continue
                    status = struct.unpack_from('<I', data, 16)[0]
                    if status:
                        raise ValueError(f'command 0x{cmd:04X}: device status {status}')
                    return data[20:]
        raise TimeoutError(f'command 0x{cmd:04X}: no matching response')


def check_info(data, kind, mode=None, uid=None):
    board, hw, cls = PROFILES[kind]
    if len(data) != 36 or struct.unpack_from('<H', data)[0] != 1 or data[3] != cls or struct.unpack_from('<HH', data, 4) != (board, hw):
        raise ValueError('device identity differs from package; nothing erased')
    if mode is not None and data[2] != mode:
        raise ValueError('unexpected device boot/application mode')
    if uid is not None and data[24:36] != uid:
        raise ValueError('device UID changed during upgrade')
    return data[24:36]


def upgrade(link, package, run=True):
    kind, app, crc = read_package(package)
    info = link.request(1)
    uid = check_info(info, kind)
    if info[2] == 1:
        try:
            link.request(0x01F0, timeout=0.15, retries=1)
        except TimeoutError:
            pass  # Application may reset after its ACK was lost.
    elif info[2] != 0:
        raise ValueError('unknown device mode')
    link.request(9, timeout=0.15, retries=3)
    check_info(link.request(1), kind, 0, uid)
    layout = link.request(0x0204)
    if layout != struct.pack('<IHHII', APP, CAPACITY, PAGE, META, APP - 0x08000000):
        raise ValueError('boot layout differs; install the common factory image first')
    board, hw, _ = PROFILES[kind]
    result = link.request(0x0200, struct.pack('<HHHI', board, hw, len(app), crc), timeout=10.0)
    if len(result) != 2:
        raise ValueError('invalid BEGIN acknowledgement')
    offset = struct.unpack('<H', result)[0]
    if offset > len(app) or (offset != len(app) and offset % PAGE):
        raise ValueError('invalid BEGIN resume offset')
    for pos in range(offset, len(app), PAGE):
        chunk = app[pos:pos + PAGE]
        result = link.request(0x0201, struct.pack('<H', pos) + chunk)
        if result != struct.pack('<H', pos + len(chunk)):
            raise ValueError('unexpected DATA acknowledgement offset')
    link.request(0x0202)
    if run:
        try:
            link.request(0x0203, timeout=0.15, retries=1)
        except (TimeoutError, ValueError):
            pass  # A lost RUN ACK can already have started the application.
        deadline = time.monotonic() + 2.0
        while True:
            try:
                check_info(link.request(1, timeout=0.15, retries=1), kind, 1, uid)
                break
            except (TimeoutError, ValueError):
                if time.monotonic() >= deadline:
                    raise RuntimeError('image committed; application INFO not confirmed')
    return kind, len(app)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='UART adapter COM port, or KLQ USB COM port with --via-host')
    parser.add_argument('--image', type=Path, required=True, help='board-tagged *_update.klq')
    parser.add_argument('--via-host', action='store_true', help='forward through the KLQ controller USB CDC bridge')
    parser.add_argument('--sensor-port', type=int, choices=(1, 2, 3), help='external controller port; required with --via-host')
    parser.add_argument('--stay-in-boot', action='store_true')
    args = parser.parse_args()
    package = args.image.read_bytes()
    kind, _, _ = read_package(package)
    if args.via_host and args.sensor_port is None:
        parser.error('--via-host requires --sensor-port 1, 2 or 3')
    if not args.via_host and args.sensor_port is not None:
        parser.error('--sensor-port requires --via-host')
    import serial
    with serial.Serial(args.port, 115200, timeout=0.01, write_timeout=1) as port:
        if args.via_host:
            from host_link import HostLink
            with HostLink(port, args.sensor_port, kind) as link:
                kind, size = upgrade(link, package, not args.stay_in_boot)
        else:
            kind, size = upgrade(Link(port), package, not args.stay_in_boot)
    print(f'{kind}: {size} bytes committed and CRC verified' + ('; staying in boot' if args.stay_in_boot else '; application INFO confirmed'))


if __name__ == '__main__':
    main()
