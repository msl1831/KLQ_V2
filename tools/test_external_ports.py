"""Observe host USB logs and verify one discovered CS100A is sampled repeatedly."""
import argparse,re,time
import serial

RX=re.compile(r"P(\d) ON C=(\d+) T=(\d+) UID=([0-9A-F]{8}/[0-9A-F]{8}/[0-9A-F]{8}) D=(\d+)mm TICKS=(\d+) ST=(\d+) SEQ=(\d+) TO=(\d+) FE=(\d+) UE=(\d+)")

def main():
    a=argparse.ArgumentParser()
    a.add_argument('--port',default='COM123')
    a.add_argument('--seconds',type=float,default=8)
    x=a.parse_args(); found=[]; buf=b''
    with serial.Serial(x.port,115200,timeout=.2) as s:
        s.reset_input_buffer()
        end=time.monotonic()+x.seconds
        while time.monotonic()<end:
            buf+=s.read(512)
            while b'\n' in buf:
                line,buf=buf.split(b'\n',1)
                m=RX.search(line.decode('ascii','ignore'))
                if m: found.append(m.groups())
    assert len(found)>=2,'CS100A not discovered through host USB log'
    port,cls,typ,uid=found[0][:4]
    assert cls=='1' and typ=='1'
    assert all(v[0]==port and v[3]==uid for v in found)
    assert len({v[7] for v in found})>1,'GET_SAMPLE sequence did not advance'
    assert found[-1][8]==found[0][8],'new timeout during stable sample polling'
    assert found[-1][9]==found[0][9] and found[-1][10]==found[0][10], 'new frame/UART error'
    a,b=found[0],found[-1]
    print(f'P{port} CS100A UID={uid}')
    print(f'first: distance={a[4]}mm ticks={a[5]} status={a[6]} sample={a[7]}')
    print(f'last:  distance={b[4]}mm ticks={b[5]} status={b[6]} sample={b[7]}')
    print(f'frames={len(found)} timeout={b[8]} frame_error={b[9]} uart_error={b[10]} PASSED')

if __name__=='__main__': main()
