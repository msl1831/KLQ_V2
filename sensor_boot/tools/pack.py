"""Build first-install images and board-tagged UART update packages."""
import argparse
import binascii
import hashlib
import json
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {'ul': (1, 1, 1), 'ir': (3, 1, 1), 'motor': (2, 2, 2)}
BASE, SIZE, APP, META, PAGE = 0x08000000, 0x6000, 0x08001800, 0x08005F80, 128
CAPACITY = META - APP
ID_OFFSET = 0xC0
HEADER = struct.Struct('<4sHHHHII')


def vector_ok(image, base):
    if len(image) < 8:
        return False
    sp, pc = struct.unpack_from('<II', image)
    return sp % 8 == 0 and 0x20000000 < sp <= 0x20000C00 and pc & 1 and base <= pc & ~1 < base + len(image)


def update_package(device, app):
    board, hw, _ = PROFILES[device]
    if len(app) & 1 or not ID_OFFSET + 16 <= len(app) <= CAPACITY or not vector_ok(app, APP):
        raise ValueError('application size/vector invalid; build at 0x08001800')
    if app[ID_OFFSET:ID_OFFSET + 16] != struct.pack('<4sHHII', b'KID1', board, hw, APP, 1):
        raise ValueError('application embedded identity differs from selected board/ABI')
    return HEADER.pack(b'KSU1', 1, board, hw, len(app), binascii.crc32(app), APP) + app


def read_package(data):
    if len(data) < HEADER.size:
        raise ValueError('truncated update header')
    magic, version, board, hw, size, crc, base = HEADER.unpack_from(data)
    app = data[HEADER.size:]
    if magic != b'KSU1' or version != 1 or base != APP or size != len(app):
        raise ValueError('invalid update header/length')
    kinds = [k for k, v in PROFILES.items() if v[:2] == (board, hw)]
    if len(kinds) != 1 or update_package(kinds[0], app) != data:
        raise ValueError('invalid board, CRC or application vector')
    return kinds[0], app, crc


def hex_record(address, kind, data):
    body = bytes((len(data), address >> 8, address & 255, kind)) + data
    return ':' + body.hex().upper() + f'{(-sum(body)) & 255:02X}'


def pack(device):
    out = ROOT / 'build' / device
    app_name = 'motor_app' if device == 'motor' else f'{device}_sensor_app'
    boot = (out / 'boot/sensor_boot.bin').read_bytes()
    app = (out / 'app' / (app_name + '.bin')).read_bytes()
    if not 8 <= len(boot) <= APP - BASE or not vector_ok(boot, BASE):
        raise ValueError('boot size/vector invalid')
    package = update_package(device, app)
    board, hw, cls = PROFILES[device]
    crc = binascii.crc32(app)
    record = struct.pack('<HHHHII', board, hw, len(app), 0, crc, 0x314D4C4B)
    image = bytearray(b'\xff' * SIZE)
    regions = ((0, boot), (APP - BASE, app), (META - BASE, record))
    lines = [hex_record(0, 4, b'\x08\x00')]
    for offset, data in regions:
        image[offset:offset + len(data)] = data
        lines += [hex_record(offset + i, 0, data[i:i + 16]) for i in range(0, len(data), 16)]
    lines.append(hex_record(0, 1, b''))
    artifacts = {f'{device}_factory.bin': bytes(image), f'{device}_factory.hex': ('\n'.join(lines) + '\n').encode(),
                 f'{device}_update.klq': package}
    manifest = dict(target='PY32F002BF15U6TR', device=device, board_id=board, hw_compat=hw, device_class=cls,
                    flash_base=f'0x{BASE:08X}', flash_bytes=SIZE, boot_bytes=len(boot),
                    app_base=f'0x{APP:08X}', app_capacity=CAPACITY, app_bytes=len(app), app_crc32=f'0x{crc:08X}',
                    metadata_base=f'0x{META:08X}', page_bytes=PAGE, programmed_bytes=sum(len(d) for _, d in regions),
                    boot_sha256=hashlib.sha256(boot).hexdigest(), app_sha256=hashlib.sha256(app).hexdigest(),
                    sha256={n: hashlib.sha256(d).hexdigest() for n, d in artifacts.items()})
    artifacts[f'{device}_manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    dest = out / 'release'
    dest.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (dest / name).write_bytes(data)
    with zipfile.ZipFile(dest / f'{device}_release.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in artifacts.items():
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(f'{device}: boot={len(boot)} app={len(app)} CRC={crc:08X}; {dest}')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', choices=PROFILES, required=True)
    pack(parser.parse_args().device)
