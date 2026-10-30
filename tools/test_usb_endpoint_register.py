"""Exercise real Geehy EP functions against RC_W0/toggle semantics, without hardware.

python tools/test_usb_endpoint_register.py [--compiler path/to/CL.EXE]
Uses the repository's VC98 native compiler convention. Creates only build outputs.
Current source must pass; restoring the old writes and OR-ing both flags into every
write must each fail, proving detection of lost asynchronous events and broken ACKs.
This is not a USB bus/Windows timing model and cannot establish a timeout's cause.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
DRIVER=ROOT/'vendor/APM32E10x_EVAL_SDK-main/Libraries/USB_Device_Lib/Driver/src/drv_usb_device.c'
HEADER=DRIVER.parents[1]/'inc/drv_usb_device.h'
NAMES=('USBD_SetEPType','USBD_SetEPKind','USBD_ResetEPKind','USBD_ResetEPRxFlag',
       'USBD_ResetEPTxFlag','USBD_ToggleTx','USBD_ToggleRx','USBD_SetEpAddr',
       'USBD_SetEPTxStatus','USBD_SetEPRxStatus','USBD_SetEPTxRxStatus')
WRITE=re.compile(r'USBD->EP\[ep\]\.EP\s*=\s*([^;]+);')
MANUAL='https://global.geehy.com/uploads/tool/APM32E103xCxE%C2%A0User%C2%A0Manual%C2%A0V1.6.pdf'


def extract(source: str) -> str:
    result=[]
    for name in NAMES:
        match=re.search(r'void '+name+r'\([^)]*\)\s*\{',source)
        if not match:raise ValueError('missing real function '+name)
        end=match.end();depth=1
        while depth:
            if end>=len(source):raise ValueError('unbalanced function '+name)
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        function=source[match.start():end]
        if len(WRITE.findall(function))!=1:raise ValueError('expected one real EP write in '+name)
        result.append(function)
    if len(WRITE.findall(source))!=len(NAMES):
        raise ValueError('new/uncovered EP write: review extraction coverage')
    return '\n\n'.join(result)


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default=shutil.which('cl') or r'C:\Program Files (x86)\Microsoft Visual Studio\VC98\bin\CL.EXE')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'build/usb_endpoint_register_20261030')
    args=parser.parse_args();compiler=Path(args.compiler).resolve()
    if not compiler.is_file():raise FileNotFoundError(compiler)
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    source=DRIVER.read_text(encoding='utf-8');header=HEADER.read_text(encoding='utf-8')
    functions=extract(source)
    license_header=source[:source.index('#include')]
    enum=re.search(r'typedef enum\s*\{\s*USBD_EP_BIT_ADDR\b.*?\}USBD_EP_BIT_T;',header,re.S)
    mask=re.search(r'^#define USBD_EP_MASK_DEFAULT[^\n]+',header,re.M)
    if not enum or not mask:raise ValueError('real bit definitions changed')
    bits='\n'.join(f'#define BIT{i} (1u<<{i})' for i in range(16))+'\n'+enum.group()+'\n'+mask.group()
    template=(ROOT/'tools/usb_endpoint_model.cpp').read_text(encoding='utf-8')
    if template.count('/* DRIVER_BITS */')!=1 or template.count('/* DRIVER_FUNCTIONS */')!=1:
        raise ValueError('invalid semantic model template')
    variants={'current':functions,'original':WRITE.sub('USBD->EP[ep].EP = reg;',functions),
              'wrong_all_keep':WRITE.sub('USBD->EP[ep].EP = reg | USBD_EP_BIT_CTFR | USBD_EP_BIT_CTFT;',functions)}
    if len(set(variants.values()))!=3:raise ValueError('source is not distinct from the required negative controls')
    env=os.environ.copy();vc=compiler.parent.parent
    env['INCLUDE']=str(vc/'Include');env['LIB']=str(vc/'Lib')
    report={'manual':MANUAL,'section':'26.4.2.1 pp400-401','source':str(DRIVER),
            'sha256':hashlib.sha256(DRIVER.read_bytes()).hexdigest(),'functions':list(NAMES),
            'header_sha256':hashlib.sha256(HEADER.read_bytes()).hexdigest(),
            'model_sha256':hashlib.sha256((ROOT/'tools/usb_endpoint_model.cpp').read_bytes()).hexdigest(),
            'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'compiler':str(compiler),
            'model':'RC_W0/T/RW/RO; hardware completion flags injected between real read and write',
            'limitations':'Not bus/Windows timing, physical acceptance, or timeout-cause proof','variants':{}}
    for name,body in variants.items():
        cpp=output/(name+'.cpp');exe=output/(name+'.exe')
        cpp.write_text(template.replace('/* DRIVER_BITS */',bits).replace('/* DRIVER_FUNCTIONS */',license_header+'\n'+body),encoding='utf-8')
        build=subprocess.run([str(compiler),'/nologo','/TP','/W3','/O2',str(cpp),'/Fe'+str(exe),'/Fo'+str(output/(name+'.obj'))],
                             cwd=output,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (output/(name+'.compile.log')).write_bytes(build.stdout)
        if build.returncode:raise RuntimeError(name+' compile failed: '+build.stdout.decode(errors='replace'))
        run=subprocess.run([str(exe)],cwd=output,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        (output/(name+'.log')).write_bytes(run.stdout)
        text=run.stdout.decode('ascii');summary=re.search(r'RESULT cases=(\d+) failures=(\d+)',text)
        ops={m[0]:int(m[1]) for m in re.findall(r'OP (\S+) failures=(\d+)',text)}
        if not summary or len(ops)!=len(NAMES):raise AssertionError(name+' did not finish all real functions')
        cases,failures=map(int,summary.groups())
        if name=='current':assert run.returncode==0 and failures==0
        elif name=='original':assert run.returncode==1 and failures>0 and all(ops.values())
        else:
            assert run.returncode==1 and failures>0 and ops['ResetEPRxFlag']>0 and ops['ResetEPTxFlag']>0
            assert all(v==0 for k,v in ops.items() if k not in ('ResetEPRxFlag','ResetEPTxFlag'))
        report['variants'][name]={'cases':cases,'failures':failures,'exit_code':run.returncode,'operation_failures':ops,
                                  'expected':'PASS' if name=='current' else 'FAIL (negative control)','verified':True}
        print(name+': '+summary.group()+(' PASS' if name=='current' else ' EXPECTED_FAILURE'),flush=True)
    report['result']='PASS';(output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('PASS real EP driver semantics and both negative controls; no hardware accessed.',flush=True)


if __name__=='__main__':main()
