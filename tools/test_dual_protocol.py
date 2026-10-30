"""Compile the actual dual-link protocol with finite fixtures and missing-guard controls. No device IO."""
from pathlib import Path
import subprocess,os,json,hashlib,argparse
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/dual_protocol_20261030';OUT.mkdir(parents=True,exist_ok=True);FIXTURE=ROOT/'sensor_boot/tests'
ap=argparse.ArgumentParser();ap.add_argument('--compiler',type=Path,default=Path('C:/Program Files (x86)/Microsoft Visual Studio/VC98/Bin/CL.EXE'));args=ap.parse_args();VC=args.compiler.parent.parent;env=os.environ.copy();env['INCLUDE']=str(VC/'Include');env['LIB']=str(VC/'Lib')
source=(ROOT/'bootloader/protocol.c').read_text();report={'scope':'actual production protocol.c with native USB/BLE/MMIO fixtures, no hardware access','sources':{'protocol':hashlib.sha256((ROOT/'bootloader/protocol.c').read_bytes()).hexdigest()},'variants':{}}
changes={
 'current':None,
 'without_owner_guard':('else if(download_owner&&download_owner!=id+1u&&!readonly(cmd)&&cmd!=CMD_STOP)status=ERR_STATE;','else if(0)status=ERR_STATE;'),
 'shared_receive_context':('Protocol *ctx=links+id;uint16_t budget=256u;','Protocol *ctx=links;uint16_t budget=256u;'),
 'cancel_any_download_on_disconnect':('if(download_owner==id+1u)','if(download_owner)'),
 'without_pending_control_barrier':('if(other->reply_pending||other->tx_owner)return 0u;','if(0)return 0u;'),
 'keep_abandoned_control_cache':('other->action_pending=other->cached=0u;','return 1u;'),
 'allow_ble_engineering':('else if(id&&!readonly(cmd)&&cmd>=CMD_EXT_OPEN)status=ERR_COMMAND;','else if(0)status=ERR_COMMAND;'),
 'without_crc_guard':('if (wire_crc != calc) status=ERR_CRC;','if (0) status=ERR_CRC;'),
 'invalidate_app_before_metadata':('if (!erase_page(META_BASE))','if (!erase_page(APP_BASE))'),
 'skip_reset_before_tx':('if(link_reset(id)){reset_received(ctx,id);return;}','if(0){reset_received(ctx,id);return;}'),
}
for name,patch in changes.items():
 d=OUT/name;d.mkdir(exist_ok=True);candidate=source
 if patch:
  a,b=patch;assert a in candidate,name;candidate=candidate.replace(a,b,1 if name!='cancel_any_download_on_disconnect' else -1)
 (d/'protocol_source.c').write_text(candidate);(d/'protocol.h').write_text((ROOT/'bootloader/protocol.h').read_text());(d/'test_usb_bridge.c').write_text((FIXTURE/'test_usb_bridge.c').read_text())
 kinds=['app','boot'] if name in ('current','without_owner_guard','shared_receive_context','cancel_any_download_on_disconnect','without_pending_control_barrier','keep_abandoned_control_cache') else (['boot'] if name=='invalidate_app_before_metadata' else ['app'])
 report['variants'][name]={}
 for kind in kinds:
  test='test_dual_protocol.c' if kind=='app' else 'test_dual_boot.c';(d/test).write_text((FIXTURE/test).read_text());exe=d/(kind+'.exe')
  includes=([FIXTURE/'dual_host'] if kind=='boot' else [])+[ROOT/'sensor_boot/tests/usb_host',d,ROOT/'bootloader']
  cmd=[str(args.compiler),'/nologo','/TC','/W3','/O2','/DKLQ_BLE']+(['/DKLQ_DEMO_APP'] if kind=='app' else [])+['/I'+str(p) for p in includes]+[str(d/test),'/Fe'+str(exe)]
  b=subprocess.run(cmd,cwd=d,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30);(d/(kind+'.compile.log')).write_bytes(b.stdout);assert b.returncode==0,(name,kind,b.stdout.decode(errors='replace'))
  r=subprocess.run([str(exe)],cwd=d,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20);text=r.stdout.decode('ascii');(d/(kind+'.log')).write_text(text)
  assert r.returncode==(0 if name=='current' else 1),(name,kind,r.returncode,text)
  report['variants'][name][kind]={'returncode':r.returncode,'stdout':text,'command':cmd,'protocol_sha256':hashlib.sha256(candidate.encode()).hexdigest()};print(name,kind,text.splitlines()[-1],flush=True)
report['result']='PASS';(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS actual dual-link protocol and all missing-protection negative controls')
