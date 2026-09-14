"""Program only the KLQ loader using the connected CMSIS-DAP and Geehy FLM."""
from pathlib import Path
from datetime import datetime
import argparse, hashlib, json, os
from pyocd.core.helpers import ConnectHelper
from pyocd.flash.file_programmer import FileProgrammer

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_PACK=Path(os.environ.get('LOCALAPPDATA',''))/'Arm/Packs/Geehy/APM32E1xx_DFP/1.0.3'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pack',default=str(DEFAULT_PACK))
    p.add_argument('--probe',default='LU_2022_8888')
    p.add_argument('--verify-only',action='store_true')
    args=p.parse_args()
    binary=ROOT/'build/klq_bootloader/klq_bootloader.bin'
    image=binary.read_bytes()
    if not 8<len(image)<=0x8000: raise ValueError('Bootloader exceeds reserved region')
    session=ConnectHelper.session_with_chosen_probe(unique_id=args.probe,target_override='apm32e103re',
        pack=args.pack,frequency=1000000,connect_mode='attach',auto_unlock=False)
    if session is None: raise RuntimeError('No selected DAP probe')
    with session:
        target=session.target
        target.halt()
        try:
            if target.read16(0x1FFFF7E0)!=512: raise RuntimeError('Unexpected target flash capacity')
            if not args.verify_only:
                folder=ROOT/'backups'; folder.mkdir(exist_ok=True)
                backup=folder/f'before_boot_flash_{datetime.now():%Y%m%d_%H%M%S}.bin'
                print('Backing up all internal Flash:',backup,flush=True)
                data=bytes(target.read_memory_block8(0x08000000,0x80000))
                with backup.open('xb') as f: f.write(data)
                backup.with_suffix('.sha256').write_text(hashlib.sha256(data).hexdigest()+'\n')
                # Never use mass erase, unlock read protection, or modify option bytes.
                # Match pyocd load: remove the running firmware's SysTick/IRQ state
                # before executing the RAM flash algorithm.
                target.reset_and_halt()
                FileProgrammer(session,chip_erase='sector',trust_crc=False).program(str(binary),base_address=0x08000000)
            actual=bytes(target.read_memory_block8(0x08000000,len(image)))
            if actual!=image: raise RuntimeError('Flash readback differs from build output')
            print('Verified loader bytes:',len(image), 'SHA256:',hashlib.sha256(actual).hexdigest())
        finally:
            target.reset()

if __name__=='__main__': main()
