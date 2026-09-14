"""Build with the installed Keil ARM Compiler 5; no IDE project required."""
from pathlib import Path
import argparse, subprocess, os

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'vendor/APM32E10x_EVAL_SDK-main'
COMPILER = Path(os.environ.get('KLQ_ARMCC', 'C:/Keil_v5/ARM/ARMCC/bin'))

def build(demo=False):
    name = 'klq_demo_app' if demo else 'klq_bootloader'
    out = ROOT/'build'/name
    out.mkdir(parents=True, exist_ok=True)
    lib = SDK/'Libraries'
    include = [ROOT/'bootloader', lib/'CMSIS/Include',
               lib/'Device/Geehy/APM32E10x/Include', lib/'APM32E10x_StdPeriphDriver/inc',
               lib/'USB_Device_Lib/Core_Device/Standard/inc', lib/'USB_Device_Lib/Driver/inc']
    files = list((ROOT/'bootloader').glob('*.c'))
    files += [lib/f'APM32E10x_StdPeriphDriver/src/apm32e10x_{n}.c' for n in ['rcm','gpio','fmc']]
    files += list((lib/'USB_Device_Lib/Core_Device/Standard/src').glob('*.c'))
    files += [lib/'USB_Device_Lib/Driver/src/drv_usb_device.c']
    objects = []
    common = ['--cpu=Cortex-M3', '--c99', '-O2', '-g', '--split_sections', '-D__MICROLIB', '-DAPM32E10X_HD']
    if demo: common += ['-DKLQ_DEMO_APP']
    for inc in include: common += ['-I', str(inc)]
    for src in files:
        obj = out/(src.stem+'.o')
        subprocess.run([str(COMPILER/'armcc.exe'), *common, '-c', str(src), '-o', str(obj)], check=True)
        objects.append(obj)
    startup = out/'startup.o'
    subprocess.run([str(COMPILER/'armasm.exe'), '--cpu=Cortex-M3', '--pd', '__MICROLIB SETA 1',
                    str(ROOT/'bootloader/startup.s'), '-o', str(startup)], check=True)
    objects.append(startup)
    base, size = (0x08008000, 0x77800) if demo else (0x08000000, 0x8000)
    scatter = out/(name+'.sct')
    scatter.write_text(f'''LR_CODE 0x{base:08X} 0x{size:X} {{
  ER_CODE 0x{base:08X} 0x{size:X} {{ startup.o (RESET, +First) *(InRoot$$Sections) .ANY (+RO) }}
  RW_RAM 0x20000000 0x20000 {{ .ANY (+RW +ZI) }}
}}
''')
    axf = out/(name+'.axf')
    subprocess.run([str(COMPILER/'armlink.exe'), '--scatter', str(scatter), '--library_type=microlib',
                    '--map', '--symbols', '--info=sizes', '--list',str(out/(name+'.map')),
                    '--entry=Reset_Handler', '-o', str(axf), *map(str,objects)], check=True)
    for fmt, suffix in [('--bin','.bin'),('--i32combined','.hex')]:
        subprocess.run([str(COMPILER/'fromelf.exe'),fmt,'--output',str(out/(name+suffix)),str(axf)],check=True)
    print(name, 'binary bytes:', (out/(name+'.bin')).stat().st_size)

if __name__ == '__main__':
    p=argparse.ArgumentParser(); p.add_argument('--demo', action='store_true'); args=p.parse_args()
    build(args.demo)
