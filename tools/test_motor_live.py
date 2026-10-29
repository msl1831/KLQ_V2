"""Bounded, low-power motor test through KLQ USB; no Flash operations."""
import argparse
import datetime
import json
import re
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'sensor_boot/tools'))
from host_link import HostLink

SET_PWM, STATUS, STOP = 0x0300, 0x0301, 0x0302


def uid_bytes(text):
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{24}|[0-9A-Fa-f]{8}[-/][0-9A-Fa-f]{8}[-/][0-9A-Fa-f]{8})', text):
        raise ValueError('UID must contain exactly three 8-digit hexadecimal words')
    raw = text.replace('-', '').replace('/', '')
    return struct.pack('<III', *(int(raw[i:i+8], 16) for i in (0, 8, 16)))


def validate(pwm, seconds, uid, allow_high_pwm=False):
    limit = 800 if allow_high_pwm else 300
    if type(pwm) is not int or pwm == 0 or not -limit <= pwm <= limit:
        raise ValueError(f'test PWM must be nonzero and within +/-{limit}; higher duty requires --allow-high-pwm')
    if not 0 < seconds <= 2:
        raise ValueError('test duration must be greater than zero and at most 2 seconds')
    if not isinstance(uid, bytes) or len(uid) != 12:
        raise ValueError('expected UID must be exactly 96 bits')


def status(data):
    if len(data) != 8:
        raise ValueError('invalid motor STATUS length')
    uptime, pwm, reserved, active = struct.unpack('<IhBB', data)
    if not -1000 <= pwm <= 1000 or reserved or active > 1:
        raise ValueError('invalid motor STATUS fields')
    return dict(uptime_ms=uptime, pwm=pwm, active=active)


def exercise(link, uid, pwm=200, seconds=1.0, *, report=None, now=time.monotonic, sleep=time.sleep, allow_high_pwm=False):
    record = {'started_at': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
              'method': 'KLQ native USB -> selected UART -> motor APP; no Flash commands',
              'sensor_port': link.sensor_port, 'uid': '/'.join(f'{x:08X}' for x in struct.unpack('<III', uid)) if len(uid) == 12 else uid.hex(),
              'pwm': pwm, 'allow_high_pwm': allow_high_pwm, 'seconds_requested': seconds, 'events': [], 'result': 'NOT_STARTED',
              'identity_verified': False, 'stop_confirmed': False, 'close_confirmed': False}
    def save():
        if report is not None:
            path = Path(report)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    def request(cmd, payload=b''):
        event = dict(cmd=f'0x{cmd:04X}', request_hex=payload.hex())
        begin = now()
        try:
            data = link.request(cmd, payload, timeout=.1, retries=1)
            event.update(ok=True, response_hex=data.hex())
            return data
        except BaseException as error:
            event.update(ok=False, error=repr(error))
            raise
        finally:
            event['seconds'] = round(now()-begin, 6)
            record['events'].append(event)
    verified = False
    error = None
    cleanup = []
    save()
    try:
        validate(pwm, seconds, uid, allow_high_pwm)
        host = link.usb.request(1).decode('ascii')
        record['host_before'] = host
        if not host.startswith('KLQ ROBOT FW ') or 'EXT_MOTOR=1' not in {f.strip() for f in host.split(';')}:
            raise ValueError('controller firmware must support EXT_MOTOR=1')
        link.open()
        info = request(1)
        if (len(info) != 36 or struct.unpack_from('<H', info)[0] != 1 or info[2:4] != b'\x01\x02'
                or struct.unpack_from('<HHH', info, 4) != (2, 2, 2) or info[24:36] != uid
                or struct.unpack_from('<I', info, 12)[0] >> 16 != 2):
            raise ValueError('expected motor UID/board/hardware/application identity differs')
        record['motor_info_hex'] = info.hex()
        verified = record['identity_verified'] = True
        if request(STOP):
            raise ValueError('unexpected STOP acknowledgement')
        initial = status(request(STATUS))
        if initial['pwm'] or initial['active']:
            raise ValueError('motor did not confirm initial zero output')
        record['status_before'] = initial
        save()
        begin = now()
        deadline = begin + seconds
        while now() < deadline:
            sent = now()
            ack = request(SET_PWM, struct.pack('<h', pwm))
            if ack != struct.pack('<h', pwm):
                raise ValueError('SET_PWM acknowledgement differs')
            current = status(request(STATUS))
            if current['pwm'] != pwm or current['active'] != 1:
                raise ValueError('motor did not confirm requested output')
            record['status_running'] = current
            remaining = deadline - now()
            if remaining > 0:
                sleep(min(remaining, max(0, .05-(now()-sent))))
        record['run_seconds'] = round(now()-begin, 6)
        record['result'] = 'RUN_COMPLETED'
    except BaseException as caught:
        error = caught
        record.update(result='FAIL', error=repr(caught), error_type=type(caught).__name__)
    finally:
        if verified:
            try:
                if request(STOP):
                    raise ValueError('unexpected final STOP acknowledgement')
                record['stop_acknowledged'] = True
            except BaseException as caught:
                cleanup.append('STOP: '+repr(caught))
            try:
                final = status(request(STATUS))
                record['status_after'] = final
                if final['pwm'] or final['active']:
                    raise ValueError('motor did not confirm final zero output')
                record['stop_confirmed'] = True
            except BaseException as caught:
                cleanup.append('STATUS: '+repr(caught))
        try:
            link.close()
            record['close_confirmed'] = True
        except BaseException as caught:
            cleanup.append('CLOSE: '+repr(caught))
        record['cleanup_errors'] = cleanup
        if error is None and cleanup:
            error = RuntimeError('motor test cleanup failed: '+'; '.join(cleanup))
            record.update(result='FAIL', error=repr(error), error_type=type(error).__name__)
        if error is None:
            record['result'] = 'PASS'
        save()
    if error is not None:
        raise error
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', help='main controller USB COM port; auto-detect when omitted')
    parser.add_argument('--sensor-port', type=int, choices=(1, 2, 3), required=True)
    parser.add_argument('--uid', required=True, help='expected motor UID as three 8-digit words')
    parser.add_argument('--pwm', type=int, default=200, help='signed duty, default limit +/-300; --allow-high-pwm permits +/-800; default 200')
    parser.add_argument('--allow-high-pwm', action='store_true', help='explicitly permit PWM up to +/-800 for a prepared test')
    parser.add_argument('--seconds', type=float, default=1.0, help='duration >0..2 seconds; default 1')
    parser.add_argument('--report', type=Path, default=Path('build/motor_live.json'))
    args = parser.parse_args(argv)
    exercise_started = False
    try:
        uid = uid_bytes(args.uid)
        validate(args.pwm, args.seconds, uid, args.allow_high_pwm)
        import serial
        from serial.tools import list_ports
        if args.port:
            port_name = args.port
        else:
            ports = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (0x314B, 0x0108)]
            if len(ports) != 1:
                raise ValueError('specify --port; native KLQ USB matches: '+str(ports))
            port_name = ports[0]
        with serial.Serial(port_name, 115200, timeout=.01, write_timeout=1) as port:
            port.reset_input_buffer()
            link = HostLink(port, args.sensor_port, 'motor')
            exercise_started = True
            result = exercise(link, uid, args.pwm, args.seconds, report=args.report, allow_high_pwm=args.allow_high_pwm)
        print(f"PASS: requested PWM {args.pwm} for {result['run_seconds']:.3f}s; zero-output STATUS and CLOSE confirmed. Physical rotation requires observation.")
        return 0
    except BaseException as error:
        if not exercise_started:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps({'result': 'FAIL', 'error': repr(error), 'motion_requested': False}, indent=2)+'\n', encoding='utf-8')
        elif args.report.exists():
            saved = json.loads(args.report.read_text(encoding='utf-8'))
            if saved.get('result') != 'FAIL':
                saved.update(result='FAIL', error=repr(error))
                args.report.write_text(json.dumps(saved, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        print(f'{type(error).__name__}: {error}', file=sys.stderr)
        return 130 if isinstance(error, KeyboardInterrupt) else 1


if __name__ == '__main__':
    raise SystemExit(main())
