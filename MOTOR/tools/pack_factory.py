"""Pack the SWD first-install image from the two linked binaries."""

import binascii
import hashlib
import json
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "firmware" / "factory" / "build"
BOOT = ROOT / "firmware" / "boot" / "build" / "motor_boot.bin"
APP = ROOT / "firmware" / "app" / "build" / "motor_app.bin"
FLASH_BASE = 0x08000000
FLASH_SIZE = 0x6000
APP_OFFSET = 0x1800
META_OFFSET = 0x5F80
BOARD_ID = 2
HW_COMPAT = 2
META_MAGIC = 0x314D4C4B


def vector_ok(image: bytes, base: int) -> bool:
    if len(image) < 8:
        return False
    sp, pc = struct.unpack_from("<II", image)
    return (
        sp % 8 == 0
        and 0x20000000 < sp <= 0x20000C00
        and pc & 1 == 1
        and base <= pc & ~1 < base + len(image)
    )


def hex_record(address: int, kind: int, data: bytes) -> str:
    body = bytes((len(data), address >> 8, address & 255, kind)) + data
    return ":" + body.hex().upper() + f"{(-sum(body)) & 255:02X}"


def main() -> None:
    boot, app = BOOT.read_bytes(), APP.read_bytes()
    if not 8 <= len(boot) <= APP_OFFSET or not vector_ok(boot, FLASH_BASE):
        raise ValueError("boot size/vector invalid")
    if not 8 <= len(app) <= META_OFFSET - APP_OFFSET or len(app) & 1:
        raise ValueError("app size invalid")
    if not vector_ok(app, FLASH_BASE + APP_OFFSET):
        raise ValueError("app vector invalid")
    crc = binascii.crc32(app) & 0xFFFFFFFF
    record = struct.pack("<HHHHII", BOARD_ID, HW_COMPAT, len(app), 0, crc, META_MAGIC)
    image = bytearray(b"\xff" * FLASH_SIZE)
    image[: len(boot)] = boot
    image[APP_OFFSET : APP_OFFSET + len(app)] = app
    image[META_OFFSET : META_OFFSET + len(record)] = record
    OUT.mkdir(parents=True, exist_ok=True)
    dest = OUT / "motor_factory.bin"
    dest.write_bytes(image)
    lines = [hex_record(0, 4, bytes((8, 0)))]
    for address, data in ((0, boot), (APP_OFFSET, app), (META_OFFSET, record)):
        for offset in range(0, len(data), 16):
            lines.append(hex_record(address + offset, 0, data[offset : offset + 16]))
    lines.append(hex_record(0, 1, b""))
    hex_data = ("\n".join(lines) + "\n").encode("ascii")
    (OUT / "motor_factory.hex").write_bytes(hex_data)
    manifest = {
        "target": "PY32F002BF15U6TR",
        "flash_base": f"0x{FLASH_BASE:08X}",
        "image_bytes": FLASH_SIZE,
        "board_id": BOARD_ID,
        "hw_compat": HW_COMPAT,
        "boot_bytes": len(boot),
        "app_address": f"0x{FLASH_BASE + APP_OFFSET:08X}",
        "app_bytes": len(app),
        "app_crc32": f"0x{crc:08X}",
        "metadata_address": f"0x{FLASH_BASE + META_OFFSET:08X}",
        "factory_sha256": hashlib.sha256(image).hexdigest(),
        "factory_hex_sha256": hashlib.sha256(hex_data).hexdigest(),
        "programmed_bytes": len(boot) + len(app) + len(record),
        "boot_sha256": hashlib.sha256(boot).hexdigest(),
        "app_sha256": hashlib.sha256(app).hexdigest(),
    }
    (OUT / "motor_factory_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
