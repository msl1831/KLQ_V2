"""KLQ USB CDC upgrade session; one selected external UART is held by the MCU."""
import binascii
import secrets
import struct
import time

from pack import PROFILES

OPEN, XFER, POLL, CLOSE = range(0x40, 0x44)
COMMANDS = {1, 9, 0x01F0, 0x0302, 0x0200, 0x0201, 0x0202, 0x0203, 0x0204}
REASONS = {1: 'UART response timeout', 2: 'UART error', 3: 'device identity changed or wrong board',
           4: 'invalid device response or layout', 5: 'session cancelled'}


def frame(seq, cmd, payload=b''):
    prefix = b'KLQ1' + struct.pack('<IHH', seq, cmd, len(payload))
    return prefix + struct.pack('<I', binascii.crc32(prefix + payload)) + payload


class USBError(RuntimeError):
    def __init__(self, command, status):
        self.command, self.status = command, status
        super().__init__(f'controller USB command 0x{command:04X}: status {status}')


class USBLink:
    def __init__(self, port, timeout=0.5):
        self.port, self.timeout = port, timeout
        self.sequence = secrets.randbits(32)
        self.buffer = bytearray()

    def request(self, cmd, payload=b'', retries=3):
        if len(payload) > 1028:
            raise ValueError('controller USB payload exceeds 1028 bytes')
        self.sequence = (self.sequence + 1) & 0xffffffff
        seq = self.sequence
        packet = frame(seq, cmd, payload)
        for _ in range(retries):
            self.buffer.clear()
            count = self.port.write(packet)
            if count is not None and count != len(packet):
                continue
            self.port.flush()
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                self.buffer.extend(self.port.read(256))
                while len(self.buffer) >= 4:
                    pos = self.buffer.find(b'KLQ1')
                    if pos < 0:
                        del self.buffer[:-3]
                        break
                    del self.buffer[:pos]
                    if len(self.buffer) < 16:
                        break
                    reply_seq, reply_cmd, size, crc = struct.unpack_from('<IHHI', self.buffer, 4)
                    if size < 4 or size > 1032:
                        del self.buffer[0]
                        continue
                    if len(self.buffer) < size + 16:
                        break
                    data = bytes(self.buffer[:size + 16])
                    if binascii.crc32(data[:12] + data[16:]) != crc:
                        del self.buffer[0]
                        continue
                    del self.buffer[:size + 16]
                    if reply_seq != seq or reply_cmd != cmd | 0x8000:
                        continue
                    status = struct.unpack_from('<I', data, 16)[0]
                    if status:
                        raise USBError(cmd, status)
                    return data[20:]
        raise TimeoutError(f'controller USB command 0x{cmd:04X}: no matching response')


class HostLink:
    def __init__(self, port, sensor_port, kind, *, usb_timeout=0.5, poll_interval=0.005):
        if sensor_port not in (1, 2, 3):
            raise ValueError('sensor port must be 1, 2 or 3')
        if kind not in PROFILES:
            raise ValueError('unknown package board')
        self.usb = USBLink(port, usb_timeout)
        self.sensor_port, self.kind = sensor_port, kind
        self.token = secrets.randbits(32) or 1
        self.sequence = secrets.randbits(32)
        self.poll_interval = poll_interval
        self.attempted = self.opened = False

    def open(self):
        if self.opened:
            return self
        info = self.usb.request(1).decode('ascii', errors='replace')
        fields = {field.strip() for field in info.split(';')}
        if not info.startswith('KLQ ROBOT FW ') or 'EXT_UPGRADE=1' not in fields or 'EXT_PORTS=3' not in fields:
            raise ValueError('controller system firmware lacks the USB external-upgrade bridge; install its new system firmware first')
        board, hw, _ = PROFILES[self.kind]
        self.attempted = True
        try:
            result = self.usb.request(OPEN, struct.pack('<BIHH', self.sensor_port - 1, self.token, board, hw))
            if result != struct.pack('<I', self.token):
                raise ValueError('controller OPEN returned a different session token')
            self.opened = True
        except BaseException as error:
            try:
                self.close()
            except BaseException as cleanup:
                error.add_note(f'Controller CLOSE also failed: {cleanup}')
            raise
        return self

    def close(self):
        if not self.attempted:
            return
        try:
            if self.usb.request(CLOSE, struct.pack('<I', self.token)):
                raise ValueError('unexpected controller CLOSE payload')
        finally:
            self.opened = self.attempted = False

    def __enter__(self):
        return self.open()

    def __exit__(self, kind, error, traceback):
        try:
            self.close()
        except BaseException as cleanup:
            if error is None:
                raise
            error.add_note(f'Controller CLOSE also failed: {cleanup}')

    def request(self, cmd, payload=b'', timeout=0.5, retries=3):
        if not self.opened:
            raise ValueError('controller upgrade session is not open')
        if cmd not in COMMANDS or len(payload) > 132 or timeout <= 0 or retries < 1:
            raise ValueError('invalid bridge command, payload or retry budget')
        floor = 1.5 if cmd == 0x01F0 else (1.0 if cmd == 1 else 0.5)
        for attempt in range(retries):
            self.sequence = (self.sequence + 1) & 0xffffffff
            seq = self.sequence
            inner = frame(seq, cmd, payload)
            if self.usb.request(XFER, struct.pack('<I', self.token) + inner):
                raise ValueError('unexpected controller XFER acknowledgement')
            # The MCU retries its UART request up to three times before reporting failure.
            deadline = time.monotonic() + 3.0 * max(timeout, floor) + 2.0
            while time.monotonic() < deadline:
                result = self.usb.request(POLL, struct.pack('<I', self.token))
                if result == b'\0':
                    time.sleep(self.poll_interval)
                    continue
                if len(result) == 2 and result[0] == 2:
                    reason = result[1]
                    if reason == 1:
                        if attempt + 1 < retries:
                            break
                        raise TimeoutError('controller bridge: ' + REASONS[1])
                    raise ValueError('controller bridge: ' + REASONS.get(reason, f'unknown failure {reason}'))
                if not result or result[0] != 1:
                    raise ValueError('malformed controller bridge result state')
                data = result[1:]
                if len(data) < 20 or data[:4] != b'KLQ1':
                    raise ValueError('malformed bridged UART response')
                reply_seq, reply_cmd, size, crc = struct.unpack_from('<IHHI', data, 4)
                if size < 4 or size > 132 or len(data) != 16 + size or reply_seq != seq or reply_cmd != cmd | 0x8000:
                    raise ValueError('bridged UART response sequence, command or length mismatch')
                if binascii.crc32(data[:12] + data[16:]) != crc:
                    raise ValueError('bridged UART response CRC mismatch')
                status = struct.unpack_from('<I', data, 16)[0]
                if status:
                    raise ValueError(f'command 0x{cmd:04X}: device status {status}')
                return data[20:]
            else:
                # The MCU may still own an in-flight request. Do not replace it blindly.
                raise TimeoutError('controller bridge: result deadline expired; close and reopen the session')
        raise AssertionError('unreachable')
