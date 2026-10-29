"""Run the vendor RDP Level 1 to 0 sequence from HK32 SRAM.

The transition automatically erases all main Flash. The companion binary is
linked at 0x20000000 from ram_unprotect.c using ARMCC5.
"""

import argparse
import hashlib
import time
from pathlib import Path

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper


RAM_BASE = 0x20000000
STATUS = 0x20000700
STACK = 0x20000800
BIN = Path(__file__).resolve().parents[1] / "firmware/boot/build/ram_unprotect.bin"
AXF = BIN.with_suffix(".axf")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe-id", required=True)
    parser.add_argument("--uid", required=True)
    parser.add_argument("--pack", required=True)
    args = parser.parse_args()
    image = BIN.read_bytes()
    with AXF.open("rb") as stream:
        entry = ELFFile(stream).header.e_entry
    if not 8 <= len(image) <= STATUS - RAM_BASE:
        raise RuntimeError("unexpected SRAM routine size")
    if not (entry & 1 and RAM_BASE <= entry & ~1 < RAM_BASE + len(image)):
        raise RuntimeError("unexpected SRAM routine entry")
    probes = ConnectHelper.get_all_connected_probes(blocking=False, unique_id=args.probe_id)
    if len(probes) != 1 or probes[0].unique_id != args.probe_id:
        raise RuntimeError("exact probe ID is not unique")
    session = ConnectHelper.session_with_chosen_probe(
        blocking=False, unique_id=args.probe_id,
        target_override="hk32f030mf4u6", pack=args.pack,
        frequency=100000, connect_mode="halt", no_config=True,
        auto_unlock=False, resume_on_disconnect=False,
    )
    with session:
        t = session.target
        t.reset_and_halt()
        cpuid = t.read_memory(0xE000ED00)
        uid = f"{t.read_memory(0x1FFFF838):08X}{t.read_memory(0x1FFFF83C):08X}"
        obr = t.read_memory(0x4002201C)
        print(f"cpuid=0x{cpuid:08X} uid={uid} OBR=0x{obr:08X}")
        if cpuid != 0x410CC200 or uid != args.uid.upper() or obr & 6 != 2:
            raise RuntimeError("target or RDP Level 1 preflight failed")
        print(f"RAM routine bytes={len(image)} sha256={hashlib.sha256(image).hexdigest()}")
        t.write_memory_block8(RAM_BASE, image)
        if bytes(t.read_memory_block8(RAM_BASE, len(image))) != image:
            raise RuntimeError("SRAM routine readback mismatch")
        for i in range(4):
            t.write_memory(STATUS + 4 * i, 0)
        t.write_core_register("sp", STACK)
        t.write_core_register("xpsr", 0x01000000)
        t.write_core_register("pc", entry)
        print(f"entry=0x{t.read_core_register('pc'):08X} sp=0x{t.read_core_register('sp'):08X}")
        t.resume()
        deadline = time.monotonic() + 15.0
        last = 0
        while time.monotonic() < deadline:
            try:
                status = t.read_memory(STATUS)
                if status != last:
                    print(f"status=0x{status:04X}")
                    last = status
                if status == 0xA006 or status & 0xF000 == 0xE000:
                    break
            except Exception as exc:
                print(f"transient SWD read: {exc}")
            time.sleep(0.05)
        t.halt()
        values = [t.read_memory(STATUS + 4 * i) for i in range(4)]
        print("final status/SR/RDP/CR:", " ".join(f"0x{x:08X}" for x in values))
        if values[0] != 0xA006:
            raise RuntimeError("SRAM RDP sequence did not complete")


if __name__ == "__main__":
    main()
