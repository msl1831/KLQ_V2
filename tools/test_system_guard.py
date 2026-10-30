"""Compile literal board.c function extracts with VC98 register/OS mocks.
No board source edits, device/USB/serial/probe access, ARM build or fault injection.
Native x86 maps the MCU RAM address via VirtualAlloc; this verifies C decisions,
not real Cortex exception entry, peripheral clocks, reset or power behavior.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FUNCTIONS=('key_down','usb_disconnect','board_request_bootloader','board_consume_bootloader_request','fault_valid','board_fault_code','board_fault_pc','board_watchdog_enabled','board_watchdog_init','board_watchdog_feed','board_fault_capture','board_power_off')
MACROS=('BOOT_REQUEST_ADDRESS','BOOT_REQUEST_MAGIC','FAULT_ADDRESS','FAULT_MAGIC')

def extract(source,name):
    pattern=r'(?m)^(?:static\s+)?(?:void|bool|uint8_t|uint32_t)\s+'+re.escape(name)+r'\([^;{}]*\)\s*\{'
    match=re.search(pattern,source)
    if not match:raise ValueError('Production C function absent: '+name)
    i=source.index('{',match.start());depth=0;state='code'
    while i<len(source):
        c=source[i];n=source[i:i+2]
        if state=='line':
            if c=='\n':state='code'
        elif state=='comment':
            if n=='*/':state='code';i+=1
        elif state in ('string','char'):
            if c=='\\':i+=1
            elif (state=='string' and c=='"') or (state=='char' and c=="'"):state='code'
        elif n=='//':state='line';i+=1
        elif n=='/*':state='comment';i+=1
        elif c=='"':state='string'
        elif c=="'":state='char'
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if not depth:return source[match.start():i+1]
        i+=1
    raise ValueError('Unbalanced function: '+name)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',default=r'C:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CL.EXE')
    p.add_argument('--output-dir',type=Path,default=ROOT/'build/optimization_system_guard_20261030')
    args=p.parse_args();cc=Path(args.compiler);out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    source_path=ROOT/'bootloader/board.c';raw=source_path.read_bytes();source=raw.decode('utf-8').replace('\r\n','\n');functions={n:extract(source,n) for n in FUNCTIONS}
    macros=[]
    for n in MACROS:
        m=re.search(r'(?m)^#define '+n+r'\s+[^\r\n]+',source)
        if not m:raise ValueError('Production macro absent: '+n)
        macros.append(m.group(0))
    globals_=[]
    for n in ('static uint8_t watchdog_enabled;','static uint32_t watchdog_at;'):
        if source.count(n)!=1:raise ValueError('State declaration changed: '+n)
        globals_.append(n)
    test=ROOT/'tools/system_guard/test_system_guard.c';mock=test.with_name('guard_mock.h');vc=cc.parent.parent
    env=os.environ.copy();env['INCLUDE']=str(vc/'Include');env['LIB']=str(vc/'Lib')
    mutations={
        'without_pointer_bounds':('board_fault_capture',r'if \(\(uint32_t\)frame>=0x20000000u[^\n]+\{','if (1) {',True),
        'without_alignment_guard':('board_fault_capture',' && !((uint32_t)frame&3u)','',False),
        'without_checksum_validation':('fault_valid',' && f[5]==(FAULT_MAGIC^f[1]^f[2]^f[3]^f[4])','',False),
        'without_boot_recovery_request':('board_fault_capture','board_request_bootloader(); NVIC_SystemReset();','NVIC_SystemReset();',False),
        'without_watchdog_enable_gate':('board_watchdog_feed','watchdog_enabled && ','',False),
        'wrong_watchdog_feed_interval':('board_watchdog_feed','board_ms+250u','board_ms+1u',False),
        'ignore_lsi_timeout':('board_watchdog_init','    if (!RCM->CSTS_B.LSIRDYFLG) return;','',False),
        'ignore_watchdog_register_timeout':('board_watchdog_init','    if (IWDT->STS) { board_request_bootloader(); NVIC_SystemReset(); }','',False),
        'return_after_armed_timeout':('board_watchdog_init','    if (IWDT->STS) { board_request_bootloader(); NVIC_SystemReset(); }','    if (IWDT->STS) return;',False),
        'without_watchdog_early_enable':('board_watchdog_init','    IWDT->KEY=0xCCCCu;','',False),
        'configure_before_enable':('board_watchdog_init',r'    IWDT->KEY=0xCCCCu;([^\n]*)\n    IWDT->KEY=0x5555u;\n    IWDT->PSC=6u; IWDT->CNTRLD=312u;',r'    IWDT->KEY=0x5555u;\n    IWDT->PSC=6u; IWDT->CNTRLD=312u;\n    IWDT->KEY=0xCCCCu;\1',True),
        'power_off_waits_for_usb':('board_power_off','if (usb_active) usb_detach();','if (usb_active) usb_disconnect();',False),
        'power_off_keeps_pc5_high':('board_power_off','    GPIOC->BC = GPIO_PIN_5;','',False),
    }
    report={'source_sha256':{'bootloader/board.c':hashlib.sha256(raw).hexdigest(),str(test.relative_to(ROOT)):hashlib.sha256(test.read_bytes()).hexdigest(),str(mock.relative_to(ROOT)):hashlib.sha256(mock.read_bytes()).hexdigest()},'compiler':str(cc),'extraction':'Function bodies unchanged; line endings normalized before native source generation.','extracted_functions':{n:hashlib.sha256(t.encode()).hexdigest() for n,t in functions.items()},'variants':{},'boundaries':['Literal function bodies/macros/state declarations extracted from real board.c; surrounding MCU initialization, USB detach implementation and ARM assembly are excluded.','Native VC98 x86, VirtualAlloc(0x20000000,128KiB); peripheral registers and barriers mocked, reset replaced by longjmp. KEY uses a mock lvalue accessor to record real-C write order and pre-config register state; native cannot establish silicon synchronization behavior.','A native thread advances volatile board_ms in simulated 10ms steps for the real LSI and IWDT register timeout loops; host wall time is not MCU timing.','Power-off is observed at its first watchdog-feed call after PC5 BC write; does not simulate electrical rails or later button debounce/wake.','No devices, serial, probe, hardware fault injection or ARM target build.','Resident boot handoff, real watchdog timing/reset domain, fault-stack assembly/vector linkage and power-off hardware still require target integration/physical validation.']}
    variants={'current_app':None,'current_boot_extract':None,**mutations}
    for name,mutation in variants.items():
        variant=functions.copy()
        if mutation:
            function,old,new,regex=mutation;text=variant[function]
            if regex:
                text,count=re.subn(old,new,text)
                if count!=1:raise ValueError('Mutation hook changed: '+name)
            else:
                if text.count(old)!=1:raise ValueError('Mutation hook changed: '+name)
                text=text.replace(old,new)
            variant[function]=text
        parts=['#include "guard_mock.h"',*macros,*globals_,'#define board_watchdog_feed production_watchdog_feed','#define KEY key_slot()[0]']
        parts += [variant[n] for n in FUNCTIONS if n!='board_power_off']
        parts += ['#undef board_watchdog_feed','#undef KEY',variant['board_power_off']]
        src=out/'guard_source.c';src.write_text('\n\n'.join(parts)+'\n');(out/(name+'.source.c')).write_bytes(src.read_bytes())
        exe=out/(name+'.exe');cmd=[str(cc),'/nologo','/TC','/W3','/O2','/I'+str(ROOT/'sensor_boot/tests/host'),'/I'+str(test.parent),'/I'+str(out),str(test),'/Fe'+str(exe),'/Fo'+str(out)+os.sep,'/link','kernel32.lib']
        if name!='current_boot_extract':cmd.insert(5,'/DKLQ_DEMO_APP')
        built=subprocess.run(cmd,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        (out/(name+'.compile.log')).write_bytes(built.stdout)
        if built.returncode:raise RuntimeError(built.stdout.decode(errors='replace'))
        try:
            result=subprocess.run([str(exe)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=10)
            stdout=result.stdout.decode('ascii',errors='replace');code=result.returncode
        except subprocess.TimeoutExpired as e:
            stdout=(e.stdout or b'').decode('ascii',errors='replace');code='timeout'
        (out/(name+'.log')).write_text(stdout)
        good=(code==0 and 'RESULT PASS' in stdout) if not mutation else (code==1 and 'FAIL line=' in stdout)
        report['variants'][name]={'exit_code':code,'stdout':stdout,'verified':good,'command':cmd,'extracted_sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
        print(name+': '+stdout.strip(),flush=True)
        if not good:
            report['result']='FAIL';(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(1)
    report['result']='PASS';(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS real board C extracts and '+str(len(mutations))+' broken-guard negative controls.')
if __name__=='__main__':main()
