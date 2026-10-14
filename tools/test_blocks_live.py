"""Exercise generated non-voice blocks on a KLQ robot; downloads RAM programs only."""
import argparse
import json
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

from klq_usb import BEGIN, DATA, END, RUN, STOP, Client, find_port
from status_dashboard import PORT_ON, SC7
from test_user_program import reconnect, values, wait_ui


def download(client, source):
    data = source.encode('ascii')
    client.command(STOP)
    client.command(BEGIN, struct.pack('<II', len(data), zlib.crc32(data)))
    for at in range(0, len(data), 1024):
        reply = client.command(DATA, struct.pack('<I', at) + data[at:at + 1024])
        assert struct.unpack('<I', reply)[0] == min(at + 1024, len(data))
    client.command(END)
    wait_ui(client, 1)


def snapshot(client):
    raw = bytearray()
    end = time.monotonic() + .7
    while time.monotonic() < end:
        raw.extend(client.serial.read(max(1, client.serial.in_waiting)))
    sensors, tilt = {}, None
    for line in raw.decode('ascii', 'ignore').splitlines():
        m = PORT_ON.search(line)
        if m:
            v = m.groups()
            if v[1:3] == ('1', '1') and v[6] == '1':
                sensors[int(v[0])] = {'uid': v[3], 'mm': int(v[4]), 'status': 1}
        m = SC7.search(line)
        if m:
            tilt = {'roll': float(m[4]), 'pitch': float(m[5])}
    return sensors, tilt


def clear_program(serial_port):
    result = {'usb_transition_warnings': []}
    # USB reset may block Windows CDC close; isolate each transition.
    for action in ('reset', 'run'):
        print(f'Cleanup USB {action}', flush=True)
        try:
            subprocess.run([sys.executable, str(Path(__file__).with_name('klq_usb.py')),
                            '--port', serial_port, action], check=True, timeout=8)
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError) as exc:
            result['usb_transition_warnings'].append(action + ': ' + str(exc))
        time.sleep(.5)
        client = reconnect('KLQ USB BOOT ' if action == 'reset' else 'KLQ ROBOT FW ')
        try:
            serial_port = client.serial.port
            if action == 'run':
                result['final_info'] = client.info()
                result['final_port'] = serial_port
                info = values(result['final_info'])
                assert info['USER_VALID'] == '0' and info['USER_STATE'] == '0' and info['UI'] == '1', info
        finally:
            client.close()
    return result


def save_report(report, path):
    if path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


def case(client, results, name, blocks, body, mode='done', error=0, line=0, minimum=0):
    download(client, 'import klq\n' + body)
    started = time.monotonic()
    client.command(RUN)
    stop_seconds = None
    if mode in ('pending', 'display'):
        time.sleep(.04 if mode == 'display' else .15)
        info = values(client.info())
        assert int(info['USER_STATE']) == 3, (name, info)
        if mode == 'display':
            assert int(info['UI']) == 5, (name, info)
        else:
            stop_at = time.monotonic()
            client.command(STOP)
            stop_seconds = time.monotonic() - stop_at
            assert stop_seconds < .5, (name, stop_seconds)
    deadline = started + max(3, minimum + 1)
    while True:
        info = values(client.info())
        if int(info['USER_STATE']) == 2:
            break
        assert time.monotonic() < deadline, (name, info)
        time.sleep(.01)
    elapsed = time.monotonic() - started
    assert int(info['PY_ERROR']) == error, (name, info)
    if line:
        assert int(info['PY_LINE']) == line, (name, info)
    assert elapsed >= minimum, (name, elapsed, minimum)
    assert info['USER_VALID'] == '1', (name, info)
    results.append({'name': name, 'blocks': blocks, 'result': 'PASS',
                    'seconds': round(elapsed, 4), 'error': error,
                    'error_line': int(info['PY_LINE']), 'stop_seconds': stop_seconds})
    print(f'PASS {name}: {elapsed:.3f}s, PY_ERROR={error}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    serial_port = args.port or find_port()
    client = Client(serial_port, timeout=2)
    report = {'cases': [], 'limits': []}
    results = report['cases']
    try:
        report['firmware'] = client.info()
        assert report['firmware'].startswith('KLQ ROBOT FW '), report['firmware']
        sensors, tilt = snapshot(client)
        report.update(sensors=sensors, tilt=tilt)
        case(client, results, 'entry/import/pass', [1], 'pass\n')
        case(client, results, 'decimal delay', [2], 'klq.wait(0.2)\n', minimum=.19)
        case(client, results, 'zero delay', [2], 'klq.wait(0)\n')
        case(client, results, 'finite and nested loops', [3],
             'for _ in range(0):\n    klq.missing()\nfor _ in range(2):\n    for _ in range(3):\n        klq.wait(0.02)\n', minimum=.11)
        case(client, results, 'forever with wait', [4], 'while True:\n    klq.wait(0.02)\n', mode='pending')
        case(client, results, 'forever without wait', [4], 'while True:\n    klq.display_face(1)\n', mode='pending')
        case(client, results, 'stop skips following action', [5], 'klq.stop()\nklq.missing()\n')
        case(client, results, 'single and movement power levels', [11, 18],
             ''.join(f'klq.{name}({level})\n' for name in ('motor_power', 'move_power') for level in (1, 2, 3)))
        case(client, results, 'all port motor stop and movement stop', [10, 17],
             ''.join(f'klq.motor_stop({p})\n' for p in (1, 2, 3)) + 'klq.move_stop()\n')
        for direction in (1, -1):
            case(client, results, f'motor_for direction {direction} unsupported', [6 if direction == 1 else 7],
                 f'klq.motor_for(1, {direction}, 0.2)\n', error=6, line=2)
            case(client, results, f'motor_run direction {direction} unsupported', [8 if direction == 1 else 9],
                 f'klq.motor_run(1, {direction})\n', error=6, line=2)
        for direction in range(4):
            case(client, results, f'move_for direction {direction} unsupported', [12 + direction],
                 f'klq.move_for({direction}, 0.2)\n', error=6, line=2)
            case(client, results, f'move direction {direction} unsupported', [16],
                 f'klq.move({direction})\n', error=6, line=2)
        for port in sensors:
            case(client, results, f'P{port} distance greater than zero', [19], f'klq.wait_distance({port}, 1, 0)\n')
            case(client, results, f'P{port} distance below upper limit', [19], f'klq.wait_distance({port}, -1, 6553)\n')
            case(client, results, f'P{port} false distance condition', [19], f'klq.wait_distance({port}, -1, 0)\n', mode='pending')
        if not sensors:
            report['limits'].append('No valid live CS100A sample; distance hardware cases skipped.')
        for compare in (-1, 1):
            case(client, results, f'IR compare {compare} unsupported', [20], f'klq.wait_ir(1, {compare}, 50)\n', error=6, line=2)
        if tilt:
            directions = (tilt['pitch'] < -25, tilt['pitch'] > 25,
                          tilt['roll'] < -25, tilt['roll'] > 25)
            for direction, active in enumerate(directions):
                if active:
                    case(client, results, f'current tilt direction {direction}', [21], f'klq.wait_tilt({direction})\n')
                    case(client, results, f'opposite tilt direction {direction ^ 1}', [21],
                         f'klq.wait_tilt({direction ^ 1})\n', mode='pending')
        report['limits'].append('Live tilt tests use current sensor orientation; physical front/back/left/right labels require board positioning.')
        button_results = []
        for pressed in (0, 1):
            download(client, f'import klq\nklq.wait_button({pressed})\n')
            client.command(RUN); time.sleep(.1)
            info = values(client.info())
            button_results.append(int(info['USER_STATE']) == 2)
            assert int(info['PY_ERROR']) == 0, info
            client.command(STOP)
        assert sum(button_results) == 1, button_results
        results.append({'name': 'button press/release are complementary', 'blocks': [22],
                        'result': 'PASS', 'completed': button_results})
        report['limits'].append('Button logic sampled current input; actual PC3 press/release and polarity not physically accepted here.')
        case(client, results, 'ten display faces', [23],
             ''.join(f'klq.display_face({i})\nklq.wait(0.05)\n' for i in range(10)), mode='display')
        case(client, results, 'numeric glyph boundaries', [25],
             ''.join(f'klq.display_number({i})\nklq.wait(0.05)\n' for i in (0, 9, 10, 99, 100)), mode='display')
        case(client, results, 'custom 13-column pattern', [24],
             'klq.display_pattern("00081c3e7f3e1c080000000000")\nklq.wait(0.1)\n', mode='display')
        case(client, results, 'user display off', [26], 'klq.display_off()\nklq.wait(0.1)\n', mode='display')
        for name, body, error in (
            ('invalid face', 'klq.display_face(10)\n', 4),
            ('invalid number', 'klq.display_number(101)\n', 4),
            ('invalid pattern bit 7', 'klq.display_pattern("80000000000000000000000000")\n', 3),
            ('invalid distance comparison', 'klq.wait_distance(2, 0, 10)\n', 4),
            ('unknown function', 'klq.missing()\n', 2),
            ('bad indentation', '    klq.wait(0)\n', 1)):
            case(client, results, name, [], body, error=error, line=2)
        report['covered_blocks'] = sorted({b for row in results for b in row['blocks']})
        assert report['covered_blocks'] == list(range(1, 27)), report['covered_blocks']
        report['result'] = 'PASS'
    except Exception as exc:
        report.update(result='FAIL', error=repr(exc))
        raise
    finally:
        try:
            client.command(STOP, retries=0)
        except Exception as exc:
            report['stop_error'] = repr(exc)
        finally:
            try:
                client.close()
            except Exception as exc:
                report['close_error'] = repr(exc)
        try:
            save_report(report, args.report)
        except OSError as exc:
            report['initial_report_error'] = repr(exc)
        try:
            report.update(clear_program(serial_port))
        except Exception as exc:
            report['cleanup_error'] = repr(exc)
            if report.get('result') != 'FAIL':
                raise
        finally:
            try:
                save_report(report, args.report)
            except OSError:
                print(json.dumps(report, ensure_ascii=False), flush=True)
                if report.get('result') != 'FAIL':
                    raise
    print(f"PASS {len(results)} cases; all 26 block mappings covered; RAM cleared.", flush=True)


if __name__ == '__main__':
    main()
