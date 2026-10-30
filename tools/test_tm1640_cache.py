"""Execute real TM1640 driver with GPIO/delay mocks; no hardware access."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default=r'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE');p.add_argument('--output-dir',type=Path,default=ROOT/'build/tm1640_cache_20261030');a=p.parse_args()
 cc=Path(a.compiler);out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);vc=cc.parent.parent;env=os.environ.copy();env['INCLUDE']=str(vc/'Include');env['LIB']=str(vc/'Lib');test=ROOT/'tools/block_sim/test_tm1640.c';driver=ROOT/'bootloader/tm1640.c';report={'sources':{str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in (test,driver)},'boundaries':'actual driver, GPIO and delays mocked; no bus/hardware access','variants':{}}
 for name in ('boot','app'):
  exe=out/(name+'.exe');cmd=[str(cc),'/nologo','/TP','/W3','/WX','/O2','/I'+str(ROOT/'tools/block_sim/vc98'),str(test),'/Fe'+str(exe),'/Fo'+str(out)+os.sep]
  if name=='app':cmd.insert(6,'/DKLQ_DEMO_APP')
  build=subprocess.run(cmd,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(out/(name+'.compile.log')).write_bytes(build.stdout)
  if build.returncode:raise RuntimeError(build.stdout.decode(errors='replace'))
  run=subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15);text=run.stdout.decode('ascii');(out/(name+'.log')).write_text(text);assert run.returncode==0;report['variants'][name]={'exit_code':run.returncode,'stdout':text};print(name+': '+text,flush=True)
 report['result']='PASS';(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
