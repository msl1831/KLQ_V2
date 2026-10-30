"""Actual ble_serial.c with UART/STA model; no device/serial/adapter access."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--compiler',default=r'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE');p.add_argument('--output-dir',type=Path,default=ROOT/'build/bluetooth_20261030/driver_tests');a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
 cc=Path(a.compiler);vc=cc.parents[1];env=os.environ.copy();env['INCLUDE']=str(vc/'Include');env['LIB']=str(vc/'Lib');source=ROOT/'bootloader/ble_serial.c';raw=source.read_text();variants={'current':raw,'no_pacing':raw.replace('#define BATCH_MS 20u','#define BATCH_MS 0u'),'done_before_tc':raw.replace('if(TX_COMPLETE())tx_state=BLE_TX_DONE;','tx_state=BLE_TX_DONE;'),'no_epoch_abort':raw.replace('if(tx_epoch!=epoch||board_ms-tx_started>=TX_MS)','if(board_ms-tx_started>=TX_MS)').replace('if(tx_state==BLE_TX_BUSY)tx_state=BLE_TX_FAILED;',''),'blind_no_final_reset':raw.replace('if(changed&&resets<2u)','if(0)'),'no_name_difference':raw.replace('"+NAME:","KLQ2"','"+NAME:",0')}
 variants['no_reset_release']=raw.replace('if(v&&tx_state==BLE_TX_BUSY){tx_state=BLE_TX_FAILED;tx_left=0u;tx_data=0;batch=0u;}','')
 variants['allow_pending_borrow']=raw.replace('||reset_seen||tx_state==BLE_TX_BUSY','||tx_state==BLE_TX_BUSY')
 variants['validate_before_lock']=raw.replace('uint32_t k=LOCK();\n    if(!connected||!ready||reset_seen||tx_state==BLE_TX_BUSY||!p||!n||n>1048u){UNLOCK(k);return false;}','uint32_t k;\n    if(!connected||!ready||reset_seen||tx_state==BLE_TX_BUSY||!p||!n||n>1048u)return false;\n    k=LOCK();')
 report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'variants':{},'scope':'actual C, UART/STA emulator; not real module AT timing, GPIO/IRQ, RF, buffers, or TC-to-GATT delivery','result':'STARTED'}
 for n,s in variants.items():
  src=out/(n+'.c');src.write_text(s);exe=out/(n+'.exe');cmd=[str(cc),'/nologo','/TC','/W3','/O2','/DKLQ_BLE_TEST','/I'+str(ROOT/'sensor_boot/tests/host'),'/I'+str(ROOT/'bootloader'),str(src),str(ROOT/'tools/ble_serial_tests/test_ble_serial.c'),'/Fe'+str(exe),'/Fo'+str(out)+os.sep]
  cp=subprocess.run(cmd,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(out/(n+'.compile.log')).write_bytes(cp.stdout)
  if cp.returncode:raise RuntimeError(cp.stdout.decode(errors='replace'))
  cp=subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20);text=cp.stdout.decode('ascii');(out/(n+'.log')).write_text(text)
  report['variants'][n]={'returncode':cp.returncode,'output':text};print(n,text,flush=True)
  assert (cp.returncode==0 and 'RESULT PASS' in text) if n=='current' else (cp.returncode==1 and 'FAIL line=' in text),n
 report['result']='PASS';(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
