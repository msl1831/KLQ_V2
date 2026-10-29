"""Install a Motor factory image on an explicitly identified PY32 board."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import zlib

from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target
from pyocd.flash.file_programmer import FileProgrammer

ROOT = Path(__file__).resolve().parents[2]
RELEASE = ROOT / 'sensor_boot/build/motor/release'
PACK = ROOT / 'UL/firmware/vendor/PY32F002B_Firmware/Packs/MDK/Puya.PY32F0xx_DFP.1.2.6.pack'


def main():
    if not __debug__:
        raise RuntimeError('Run without -O so target and image guards remain enabled')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--uid', required=True, help='Expected three-word MCU UID, hexadecimal')
    parser.add_argument('--sha256', required=True, help='Reviewed factory HEX SHA-256')
    args = parser.parse_args()
    expected_uid = re.sub(r'[^0-9A-Fa-f]', '', args.uid).upper()
    if len(expected_uid) != 24:
        raise ValueError('A full 96-bit MCU UID is required')
    manifest = json.loads((RELEASE / 'motor_manifest.json').read_text())
    image = RELEASE / 'motor_factory.hex'
    binary = (RELEASE / 'motor_factory.bin').read_bytes()
    assert manifest['target'] == 'PY32F002BF15U6TR'
    assert (manifest['board_id'], manifest['hw_compat'], manifest['device_class']) == (2, 2, 2)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    assert digest == args.sha256.lower() == manifest['sha256'][image.name]
    assert len(binary) == 0x6000
    assert hashlib.sha256(binary).hexdigest() == manifest['sha256']['motor_factory.bin']
    app = binary[0x1800:0x1800 + manifest['app_bytes']]
    assert app[0xC0:0xD0] == struct.pack('<4sHHII', b'KID1', 2, 2, 0x08001800, 1)
    assert struct.unpack_from('<HHHHII', binary, 0x5F80) == (
        2, 2, len(app), 0, zlib.crc32(app), 0x314D4C4B)
    stamp = datetime.now(timezone(timedelta(hours=8))).strftime('%Y%m%d_%H%M%S')
    out = ROOT / 'MOTOR/flash_evidence' / ('flash_' + stamp)
    out.mkdir(parents=True, exist_ok=False)
    report = dict(probe=args.probe, expected_uid=expected_uid, target='py32f002bx5',
                  image=str(image), image_sha256=digest, manifest=manifest, status='started',
                  original_flash_backed_up=False, option_bytes_changed=False)

    def save():
        (out / 'flash_result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def session():
        s = ConnectHelper.session_with_chosen_probe(unique_id=args.probe,
            target_override='py32f002bx5', pack=str(PACK), frequency=1000000,
            connect_mode='halt', auto_unlock=False, resume_on_disconnect=False)
        if s is None:
            raise RuntimeError('Requested probe unavailable')
        return s

    def identity(s):
        t = s.target
        uid = ''.join(f'{t.read32(0x1FFF0000 + 4*i):08X}' for i in range(3))
        assert s.probe.unique_id == args.probe and uid == expected_uid
        assert t.read32(0xE000ED00) == 0x410CC601
        assert t.read32(0x40015800) == 0x20220064
        assert t.memory_map.get_region_for_address(0x08000000).length == 0x6000
        assert t.memory_map.get_region_for_address(0x20000000).length == 0xC00
        return uid

    save()
    print('Evidence:', out, flush=True)
    try:
        with session() as s:
            report['uid'] = identity(s)
            t = s.target
            options = bytes(t.read_memory_block8(0x1FFF0080, 16))
            report['options_before'] = options.hex()
            # Keep both DRV8837 inputs low while the blank target is programmed.
            t.write32(0x40021034, t.read32(0x40021034) | 1)
            t.write32(0x50000018, 3 << 16)
            t.write32(0x50000004, t.read32(0x50000004) & ~3)
            t.write32(0x5000000C, (t.read32(0x5000000C) & ~15) | 10)
            t.write32(0x50000000, (t.read32(0x50000000) & ~15) | 5)
            save()
            FileProgrammer(s, chip_erase='sector', smart_flash=False,
                           trust_crc=False).program(str(image))
            t.halt()
            report['status'] = 'programmed'
            save()
        with session() as s:
            identity(s)
            t = s.target
            actual = bytes(t.read_memory_block8(0x08000000, 0x6000))
            report['readback'] = {}
            for name, offset, size in (('boot', 0, manifest['boot_bytes']),
                    ('app', 0x1800, len(app)), ('metadata_page', 0x5F80, 128)):
                data = actual[offset:offset + size]
                assert data == binary[offset:offset + size], name + ' readback differs'
                report['readback'][name] = dict(address=f'0x{0x08000000+offset:08X}',
                    bytes=size, sha256=hashlib.sha256(data).hexdigest(), matches=True)
            report['full_flash_sha256'] = hashlib.sha256(actual).hexdigest()
            report['full_flash_matches_factory'] = actual == binary
            after = bytes(t.read_memory_block8(0x1FFF0080, 16))
            report['options_after'] = after.hex()
            report['option_bytes_changed'] = after != options
            assert after == options, 'Option bytes changed'
            report['status'] = 'readback_verified'
            save()
            t.reset_and_halt(reset_type=Target.ResetType.SYSRESETREQ)
            t.resume()
            time.sleep(1.2)
            t.halt()
            runtime = dict(pc=f'0x{t.read_core_register("pc"):08X}',
                vtor=f'0x{t.read32(0xE000ED08):08X}',
                gpioa_moder=f'0x{t.read32(0x50000000):08X}',
                gpioa_odr=t.read32(0x50000014), gpioa_idr=t.read32(0x50000010),
                tim1_arr=t.read32(0x40012C2C), tim1_ccr1=t.read32(0x40012C34),
                tim1_ccr2=t.read32(0x40012C38))
            report['runtime'] = runtime
            save()
            assert 0x080018D0 <= int(runtime['pc'], 16) < 0x08001800 + len(app)
            assert runtime['vtor'] == '0x08001800'
            assert runtime['tim1_ccr1'] == runtime['tim1_ccr2'] == 0
            assert runtime['gpioa_idr'] & 3 == 0
            t.resume()
            report['status'] = 'passed_running'
            save()
        print(json.dumps(report, indent=2), flush=True)
    except Exception as error:
        report['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    main()
