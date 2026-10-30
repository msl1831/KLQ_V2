"""Run real mini_python/runtime with deterministic motor-state faults; no hardware.
The negative control removes only the asynchronous poll check and must fail.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CHECK='    if (!klq_runtime_motors_ok()) { fail(MINI_ERR_RUNTIME,error_pc); return status; }'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default=r'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE');p.add_argument('--output-dir',type=Path,default=ROOT/'build/python_motor_fault_20261030');a=p.parse_args()
 cc=Path(a.compiler);out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);vc=cc.parent.parent;env=os.environ.copy();env['INCLUDE']=str(vc/'Include');env['LIB']=str(vc/'Lib');fixture=ROOT/'tools/block_sim'
 mini=ROOT/'bootloader/mini_python.c';runtime=ROOT/'bootloader/klq_runtime.c';test=fixture/'test_motor_fault.c';source=mini.read_text();assert source.count(CHECK)==1
 report={'sources':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in (mini,runtime,ROOT/'bootloader/klq_runtime.h',test)},'boundaries':'actual interpreter/runtime, mocked GPIO/UI/normal motor state; no serial/USB/device access','variants':{}}
 for name in ('current','without_async_check'):
  src=mini
  if name!='current':src=out/'mini_without_check.c';src.write_text(source.replace(CHECK,''))
  exe=out/(name+'.exe');cmd=[str(cc),'/nologo','/TP','/W3','/WX','/O2','/FI'+str(fixture/'klq_mock.h'),'/I'+str(fixture/'vc98'),'/I'+str(fixture),'/I'+str(ROOT/'bootloader'),str(src),str(runtime),str(test),'/Fe'+str(exe),'/Fo'+str(out)+os.sep]
  build=subprocess.run(cmd,env=env,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(out/(name+'.compile.log')).write_bytes(build.stdout)
  if build.returncode:raise RuntimeError(build.stdout.decode(errors='replace'))
  run=subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15);text=run.stdout.decode('ascii');(out/(name+'.log')).write_text(text)
  if name=='current':assert run.returncode==0 and 'RESULT PASS assertions=' in text
  else:assert run.returncode==1 and 'MINI_ERROR' in text
  report['variants'][name]={'exit_code':run.returncode,'stdout':text,'verified':True};print(name+':\n'+text,flush=True)
 report['result']='PASS';(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS actual runtime fault propagation and missing-check negative control.')
if __name__=='__main__':main()
