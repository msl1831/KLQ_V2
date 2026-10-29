"""Validate local images and package the Python-enabled host plus peripheral updates."""
import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'sensor_boot/tools'))
from pack import PROFILES,APP,META,read_package,vector_ok

def digest(data): return hashlib.sha256(data).hexdigest()

def hex_check(data,binary,base,limit,complete=False):
    high=0; seen={}; eof=False
    for text in data.decode('ascii').splitlines():
        if eof or not text.startswith(':'): raise ValueError('HEX record after EOF or missing colon')
        record=bytes.fromhex(text[1:]); n=record[0]
        if len(record)!=n+5 or sum(record)&255: raise ValueError('HEX size/checksum')
        offset=int.from_bytes(record[1:3],'big'); kind=record[3]
        if kind==4:
            if n!=2: raise ValueError('HEX address record')
            high=int.from_bytes(record[4:6],'big')<<16
        elif kind==0:
            for i,value in enumerate(record[4:-1]):
                address=high+offset+i
                if not base<=address<limit or address in seen: raise ValueError('HEX range/overlap')
                if binary[address-base]!=value: raise ValueError('HEX/BIN mismatch')
                seen[address]=value
        elif kind==1:
            if n: raise ValueError('HEX EOF')
            eof=True
        elif kind!=5: raise ValueError('HEX record type')
    if not eof or not seen: raise ValueError('HEX incomplete')
    if complete and len(seen)!=len(binary): raise ValueError('HEX does not cover complete binary')
    return {'bytes':len(seen),'first':f'0x{min(seen):08X}','last':f'0x{max(seen):08X}'}

def host_image(folder,name,base,limit,required=()):
    image=(folder/(name+'.bin')).read_bytes(); ihex=(folder/(name+'.hex')).read_bytes()
    if not 8<=len(image)<=limit-base: raise ValueError('host size')
    sp,pc=struct.unpack_from('<II',image)
    if sp%8 or not 0x20000000<sp<=0x2001FFF8 or not pc&1 or not base+8<=pc&~1<base+len(image):
        raise ValueError('host vectors')
    mapping=(folder/(name+'.map')).read_text(errors='replace')
    sizes=[int(re.search(r'Total '+kind+r'  Size[^\r\n]*?\s(\d+)\s*\(',mapping)[1]) for kind in ('RO','RW')]
    for symbol in required:
        if not re.search(r'\b'+symbol+r'\b',mapping): raise ValueError('missing linked function '+symbol)
    result=dict(base=f'0x{base:08X}',length=len(image),sha256=digest(image),sp=f'0x{sp:08X}',
                reset=f'0x{pc:08X}',ro=sizes[0],ram=sizes[1],hex=hex_check(ihex,image,base,base+len(image),True))
    return image,ihex,result

def package(app_dir,boot_dir,destination):
    payload={}; profiles={}
    image,ihex,host=host_image(app_dir,'klq_demo_app',0x08008000,0x0807F800,
                             ('mini_python_poll','power_key_poll','user_program_start','external_upgrade_open'))
    if b'KLQ ROBOT FW 0.9;' not in image or b'EXT_UPGRADE=1; EXT_PORTS=3' not in image:
        raise ValueError('expected integrated host 0.9 with bridge capability')
    _,_,boot=host_image(boot_dir,'klq_bootloader',0x08000000,0x08008000)
    boot_map=(boot_dir/'klq_bootloader.map').read_text(errors='replace')
    if any(re.search(r'\b'+s+r'\b',boot_map) for s in ('mini_python_poll','external_upgrade_open')):
        raise ValueError('application code leaked into host bootloader')
    payload['firmware/klq_robot_0.9.bin']=image; payload['firmware/klq_robot_0.9.hex']=ihex
    for device,(board,hw,cls) in PROFILES.items():
        folder=ROOT/'sensor_boot/build'/device/'release'
        manifest=json.loads((folder/f'{device}_manifest.json').read_text())
        package_data=(folder/f'{device}_update.klq').read_bytes(); kind,app,crc=read_package(package_data)
        if (kind,manifest['board_id'],manifest['hw_compat'],manifest['device_class'])!=(device,board,hw,cls):
            raise ValueError('manifest board mismatch')
        if digest(app)!=manifest['app_sha256'] or manifest['app_crc32']!=f'0x{crc:08X}':
            raise ValueError('manifest application hash/CRC')
        binary=(folder/f'{device}_factory.bin').read_bytes()
        if len(binary)!=24576 or binary[APP-0x08000000:APP-0x08000000+len(app)]!=app:
            raise ValueError('factory application differs from update')
        if (manifest['app_bytes']!=len(app) or manifest['app_capacity']!=18304 or manifest['page_bytes']!=128 or
            manifest['app_base']!='0x08001800' or manifest['metadata_base']!='0x08005F80'):
            raise ValueError('manifest layout mismatch')
        boot_size=manifest['boot_bytes']
        if not 8<=boot_size<=6144 or not vector_ok(binary[:boot_size],0x08000000) or digest(binary[:boot_size])!=manifest['boot_sha256']:
            raise ValueError('factory boot vector/hash')
        expected=(board,hw,len(app),0,crc,0x314D4C4B)
        if struct.unpack_from('<HHHHII',binary,META-0x08000000)!=expected: raise ValueError('factory metadata')
        factory=hex_check((folder/f'{device}_factory.hex').read_bytes(),binary,0x08000000,0x08006000)
        if factory['bytes']!=manifest['programmed_bytes']: raise ValueError('factory HEX size')
        for name,sha in manifest['sha256'].items():
            if digest((folder/name).read_bytes())!=sha: raise ValueError('peripheral artifact hash '+name)
        for suffix in ('factory.bin','factory.hex','update.klq','manifest.json'):
            name=f'{device}_{suffix}'; payload[f'peripherals/{device}/{name}']=(folder/name).read_bytes()
        profiles[device]=manifest
    for rel in ('tools/update.py','tools/host_link.py','tools/pack.py'):
        payload[rel]=(ROOT/'sensor_boot'/rel).read_bytes()
    payload['tools/klq_usb.py']=(ROOT/'tools/klq_usb.py').read_bytes()
    for name in ('protocol_v1.md','usb_bridge_v1.md'):
        doc=(ROOT/'sensor_boot/docs'/name).read_text(encoding='utf-8')
        doc=doc.replace('../../docs/verification_20261029_host_upgrade.md','https://github.com/msl1831/KLQ_V2/blob/codex/python-runtime/docs/verification_20261029_host_upgrade.md')
        doc=doc.replace('(verification_20261029_usb_bridge.md)','(https://github.com/msl1831/KLQ_V2/blob/codex/python-runtime/sensor_boot/docs/verification_20261029_usb_bridge.md)')
        payload['docs/'+name]=doc.encode('utf-8')
    payload['requirements.txt']=b'pyserial==3.5\n'
    payload['README.txt']=('''KLQ 主控0.9与UL/IR/MOTOR升级包（2026-10-29）
本包已通过构建、镜像检查和软件模拟，本轮未烧录或实板验收。
主控保留RAM Python、26项积木接口、电源键、显示与传感器调度。

安装Python 3.12，再执行：python -m pip install -r requirements.txt
关闭其它串口监视器。COM10和外接端口号须按实际连接修改。

已有KLQ USB引导0.3可更新本包主控应用，无需为此功能重刷主控引导：
python tools/klq_usb.py --port COM10 reset
等待USB重新连接，先用info确认返回KLQ USB BOOT，然后执行：
python tools/klq_usb.py --port COM10 flash firmware/klq_robot_0.9.bin --run

每块外接板首次需经SWD安装对应peripherals/<板型>/*_factory.hex。
以后可通过主控USB更新外接应用。例如：
python tools/update.py --via-host --port COM10 --sensor-port 1 --image peripherals/ul/ul_update.klq
python tools/update.py --via-host --port COM10 --sensor-port 2 --image peripherals/ir/ir_update.klq
python tools/update.py --via-host --port COM10 --sensor-port 3 --image peripherals/motor/motor_update.klq
三类设备均可接任意1～3端口，工具按包与设备身份核对。

升级会停止当前Python程序；完成或取消后须手动重新运行，不恢复旧动作。
串口升级只选*_update.klq，不能选工厂BIN/HEX或主控BIN。
真实USB/UART、断电恢复、电机停车及板端RUN/TC发送超时边界仍待验收。
电机/红外Python积木的实物驱动是另一个接入任务，本包不将模拟动作视作实物通过。
''').encode('utf-8')
    sources={}
    for path in sorted((ROOT/'bootloader').glob('*')):
        if path.suffix in ('.c','.h','.s'):sources[str(path.relative_to(ROOT)).replace('\\','/')]=digest(path.read_bytes())
    result={'date':'2026-10-29','host_version':'0.9','baseline':'ff33f97438f782a5844bf3ce6e2cd9ed120a402c',
            'validation':'build and native simulations; no physical flashing',
            'host':host,'boot_regression':boot,'peripherals':profiles,'source_sha256':sources,
            'files':{name:digest(data) for name,data in sorted(payload.items())}}
    payload['manifest.json']=(json.dumps(result,ensure_ascii=False,indent=2,sort_keys=True)+'\n').encode('utf-8')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(payload.items()):
            item=zipfile.ZipInfo(name,(2026,10,29,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED;item.external_attr=0o644<<16
            z.writestr(item,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
    with zipfile.ZipFile(destination) as z:
        if z.testzip() is not None:raise ValueError('ZIP CRC')
        for name,data in payload.items():
            if z.read(name)!=data:raise ValueError('ZIP differs from validated input '+name)
    result['bundle']={'path':str(destination.relative_to(ROOT)).replace('\\','/'),'bytes':destination.stat().st_size,
                      'sha256':digest(destination.read_bytes()),'entries':len(payload)}
    evidence=destination.with_suffix('.json');evidence.write_text(json.dumps(result,ensure_ascii=False,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app-dir',type=Path,default=ROOT/'build/host_bridge_0_9/app')
    p.add_argument('--boot-dir',type=Path,default=ROOT/'build/host_bridge_0_9/boot')
    p.add_argument('--output',type=Path,default=ROOT/'build/host_bridge_0_9/release/klq_host_0.9_peripheral_updates.zip')
    a=p.parse_args();r=package(a.app_dir,a.boot_dir,a.output);print(json.dumps(r['bundle'],indent=2))
