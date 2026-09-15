"""Halt the known PY32 sensor briefly and verify host unbind/re-discovery."""
import re,serial,subprocess,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
UID='07000001002700594d00000c4e54514aa5a5a5a597969908'
PACK=ROOT/'UL/firmware/vendor/PY32F002B_Firmware/Packs/MDK/Puya.PY32F0xx_DFP.1.2.6.pack'
PY=ROOT/'UL/.venv/Scripts/python.exe'
RX=re.compile(r'P1 (ON .*UID=([0-9A-F/]+)|OFF )')

def main():
    code=("import time;from pyocd.core.helpers import ConnectHelper;"
          f"s=ConnectHelper.session_with_chosen_probe(unique_id='{UID}',target_override='py32f002bx5',pack=r'{PACK}',frequency=1000000,connect_mode='attach');"
          "s.open();s.target.halt();time.sleep(1);s.target.reset();s.close()")
    with serial.Serial('COM123',115200,timeout=.1) as port:
        port.reset_input_buffer(); proc=subprocess.Popen([str(PY),'-c',code])
        end=time.monotonic()+8; buf=b''; states=[]
        while time.monotonic()<end:
            buf+=port.read(512)
            while b'\n' in buf:
                line,buf=buf.split(b'\n',1); m=RX.search(line.decode('ascii','ignore'))
                if m:
                    state='ON' if m.group(1).startswith('ON') else 'OFF'
                    uid=m.group(2)
                    if not states or states[-1][0]!=state: states.append((state,uid))
            if proc.poll() is not None and len(states)>=3 and states[-1][0]=='ON': break
        assert proc.wait(timeout=5)==0
    names=[v[0] for v in states]
    assert 'OFF' in names and names[-1]=='ON',states
    online=[v[1] for v in states if v[0]=='ON']
    assert online and len(set(online))==1,states
    print(' -> '.join(names),f'UID={online[-1]} PASSED')

if __name__=='__main__': main()
