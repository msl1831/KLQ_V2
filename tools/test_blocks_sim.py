"""Compile unchanged production mini_python/runtime C and execute deterministic mocks.

No USB, probe, board Flash or serial access. GCC/Clang or VC98 can compile the test.
Set KLQ_SIM_CC or --cc to select a compiler. VC98 uses C++ mode and width-only shims.
Board.h is suppressed by a forced mock header; GPIO/RCM, external ports and UI are mocked.
The resulting report records compiler/source hashes so host evidence remains traceable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('KLQ_SIM_CC'))
    parser.add_argument('--report', type=Path, help='Optional JSON evidence destination')
    args = parser.parse_args()
    candidates = [args.cc, shutil.which('gcc'), shutil.which('clang'), shutil.which('cl'),
                  r'C:\Program Files (x86)\STMicroelectronics\st_toolset\stvd\MinGW\bin\gcc.exe']
    cc = next((p for p in candidates if p and Path(p).is_file()), None)
    if not cc:
        parser.error('GCC/Clang/VC not found; supply --cc or KLQ_SIM_CC')
    fixtures = ROOT / 'tools/block_sim'
    sources = [ROOT / 'bootloader/mini_python.c', ROOT / 'bootloader/klq_runtime.c', fixtures / 'test_blocks.c']
    msvc = Path(cc).name.lower() in ('cl', 'cl.exe')
    version_run = subprocess.run([cc] if msvc else [cc, '--version'], text=True,
                                 capture_output=True, timeout=10)
    version = (version_run.stderr if msvc else version_run.stdout).splitlines()[0]
    with tempfile.TemporaryDirectory(prefix='klq-block-sim-') as temporary:
        executable = Path(temporary) / ('blocks.exe' if os.name == 'nt' else 'blocks')
        if msvc:
            command = [cc, '/nologo', '/TP', '/W3', '/WX', '/O2',
                       '/FI' + str(fixtures / 'klq_mock.h'), '/I' + str(fixtures / 'vc98'),
                       '/I' + str(fixtures), '/I' + str(ROOT / 'bootloader'),
                       *(str(p) for p in sources), '/Fe' + str(executable),
                       '/Fo' + str(Path(temporary)) + os.sep]
        else:
            command = [cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2']
            if 'GCC) 4.' not in version:
                command += ['-Wno-misleading-indentation']
            command += ['-include', str(fixtures / 'klq_mock.h'), '-I', str(fixtures),
                        '-I', str(ROOT / 'bootloader'), *(str(p) for p in sources), '-o', str(executable)]
        subprocess.run(command, check=True, timeout=30, cwd=temporary)
        result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=15)
        print(result.stdout, end='')
        if result.stderr:
            print(result.stderr, end='')
        report = dict(compiler=version, command=command, exit_code=result.returncode,
                      stdout=result.stdout, stderr=result.stderr,
                      sha256={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sources + sorted(fixtures.rglob('*.h'))},
                      boundaries=['actual mini_python.c and klq_runtime.c unchanged',
                                  'GPIO clock/config/input, external ports and robot UI mocked',
                                  'motor support success is simulated; current production motor protocol unsupported',
                                  'no USB, sensor hardware, physical motors or actual button tested',
                                  'VC mode uses C++ parsing; production ARMCC build remains required']
                     if msvc else ['actual mini_python.c and klq_runtime.c unchanged',
                                   'GPIO clock/config/input, external ports and robot UI mocked',
                                   'no USB, sensor hardware, physical motors or actual button tested'])
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        if result.returncode:
            raise SystemExit(result.returncode)

if __name__ == '__main__':
    main()
