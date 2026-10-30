"""KLQ2 ECB02C BLE client. Install requirements_ble.txt; does not use a COM port."""
import argparse
import asyncio
import json
import secrets
import struct
import time
import zlib
from pathlib import Path
from klq_usb import (MAGIC, INFO, BEGIN, DATA, END, RUN, ECHO, RESET, STOP,
                     USER_PROGRAM_MAX, ERRORS, encode, validate_image)

SERVICE='0000fff0-0000-1000-8000-00805f9b34fb'
NOTIFY='0000fff1-0000-1000-8000-00805f9b34fb'
WRITE='0000fff2-0000-1000-8000-00805f9b34fb'

class ProtocolError(RuntimeError): pass
class CommandError(ProtocolError):
    def __init__(self,status):
        self.status=status
        super().__init__(f'KLQ error {status}: '+(ERRORS[status] if status<len(ERRORS) else 'unknown'))
class AmbiguousActionError(TimeoutError): pass

class FrameParser:
    def __init__(self): self.buffer=bytearray()
    def clear(self): self.buffer.clear()
    def feed(self,data):
        self.buffer.extend(data)
        frames=[]
        while len(self.buffer)>=4:
            at=self.buffer.find(MAGIC)
            if at<0:
                del self.buffer[:-3]; break
            if at: del self.buffer[:at]
            if len(self.buffer)<16: break
            _,seq,cmd,n,crc=struct.unpack_from('<4sIHHI',self.buffer)
            if n>1032:
                del self.buffer[0]; continue
            if len(self.buffer)<16+n: break
            frame=bytes(self.buffer[:16+n]);del self.buffer[:16+n]
            if zlib.crc32(frame[16:],zlib.crc32(frame[:12]))==crc:frames.append((seq,cmd,frame[16:]))
        return frames

def boot_mode(info):
    return info.startswith(('KLQ USB BOOT ','KLQ BLE BOOT ','KLQ BOOT '))

def ble_status(data):
    if len(data)!=88 or data[0]!=1:raise ProtocolError('Unsupported BLE status ABI/length')
    result=dict(zip(('abi','ready','connected','configured','init_stage','error','step','attempts'),data[:8]))
    result.update(zip(('rx_bytes','tx_bytes','uart_errors','rx_overflows','tx_timeouts','init_timeouts','config_writes'),struct.unpack_from('<7I',data,8)))
    for name,lo,hi in [('name',36,57),('version',57,73),('mac',73,86)]:result[name]=data[lo:hi].split(b'\0',1)[0].decode('ascii',errors='replace')
    return result

class Client:
    def __init__(self,device,timeout=10,fragment=20,gap=.02,client_factory=None):
        if not 1<=fragment<=20 or gap<0 or timeout<=0:raise ValueError('Require fragment1..20, gap>=0, timeout>0')
        self.device=device;self.timeout=timeout;self.fragment=fragment;self.gap=gap;self.factory=client_factory
        self.client=None;self.seq=secrets.randbits(32);self.parser=FrameParser();self.lock=asyncio.Lock()
        self.pending=None;self.response=None;self.failure=None;self.event=asyncio.Event();self.loop=None
        self.write_char=None;self.notify_char=None;self.write_response=None
    def _dispatch(self,fn,*args):
        if self.loop is not None and not self.loop.is_closed():self.loop.call_soon_threadsafe(fn,*args)
    def _notified(self,source,_characteristic,data):self._dispatch(self._consume,source,bytes(data))
    def _consume(self,source,data):
        if source is not self.client:return
        for seq,cmd,payload in self.parser.feed(data):
            if self.pending==(seq,cmd) and self.response is None:
                self.response=payload;self.event.set()
    def _disconnected(self,client):self._dispatch(self._lost,client)
    def _lost(self,source):
        if source is not self.client:return
        self.parser.clear();self.response=None;self.failure=ConnectionError('KLQ BLE disconnected');self.event.set()
    async def connect(self):
        if self.client is not None:raise RuntimeError('Client already connected; disconnect before reconnect')
        if self.lock.locked():raise RuntimeError('Outstanding RPC must finish before reconnect')
        self.loop=asyncio.get_running_loop();self.parser.clear();self.pending=self.response=self.failure=None;self.event.clear();self.seq=secrets.randbits(32)
        factory=self.factory
        if factory is None:
            from bleak import BleakClient
            factory=BleakClient
        self.client=factory(self.device,disconnected_callback=self._disconnected,timeout=20,winrt={'use_cached_services':False})
        try:
            await self.client.connect()
            if not self.client.is_connected:raise ConnectionError('BLE connect did not establish a session')
            service=self.client.services.get_service(SERVICE)
            if service is None:raise ProtocolError('ECB02C FFF0 service missing')
            by_uuid={c.uuid.lower():c for c in service.characteristics}
            self.write_char=by_uuid.get(WRITE);self.notify_char=by_uuid.get(NOTIFY)
            if self.write_char is None or self.notify_char is None:raise ProtocolError('ECB02C FFF1/FFF2 characteristic missing')
            props=set(self.write_char.properties)
            if 'write' in props:self.write_response=True
            elif 'write-without-response' in props:self.write_response=False
            else:raise ProtocolError('FFF2 is not writable')
            if not {'notify','indicate'}&set(self.notify_char.properties):raise ProtocolError('FFF1 does not notify/indicate')
            source=self.client
            await self.client.start_notify(self.notify_char,lambda char,data:self._notified(source,char,data))
            return self
        except BaseException:
            await self.disconnect();raise
    async def disconnect(self):
        client,self.client=self.client,None
        self.failure=ConnectionError('KLQ BLE disconnected');self.event.set()
        try:
            if client is not None:
                try:
                    if self.notify_char is not None and client.is_connected:await client.stop_notify(self.notify_char)
                except Exception:pass
                finally:await client.disconnect()
        finally:
            self.parser.clear();self.pending=self.response=None
            self.write_char=self.notify_char=None
    async def __aenter__(self):return await self.connect()
    async def __aexit__(self,*_):await self.disconnect()
    def describe(self):
        services=[]
        for service in self.client.services:
            services.append({'uuid':service.uuid,'characteristics':[{'uuid':c.uuid,'properties':list(c.properties)} for c in service.characteristics]})
        return {'address':getattr(self.device,'address',str(self.device)),'mtu':self.client.mtu_size,
                'att_fragment':self.fragment,'gap_ms':self.gap*1000,'write_with_response':self.write_response,'services':services}
    async def command(self,cmd,payload=b'',retries=None):
        if len(payload)>1028:raise ValueError('KLQ payload exceeds1028 bytes')
        if retries is None:retries=0 if cmd in (RUN,RESET) else 2
        if retries<0 or (cmd in (RUN,RESET) and retries):raise ValueError('RUN/RESET cannot be blindly retried across BOOT/APP modes')
        async with self.lock:
            if self.client is None or not self.client.is_connected:raise ConnectionError('KLQ BLE not connected')
            source=self.client
            self.seq=(self.seq+1)&0xffffffff;packet=encode(self.seq,cmd,payload)
            self.parser.clear();self.pending=(self.seq,cmd|0x8000);self.response=self.failure=None;self.event.clear()
            try:
                for attempt in range(retries+1):
                    sent_complete=False
                    try:
                        async with asyncio.timeout(self.timeout):
                            for at in range(0,len(packet),self.fragment):
                                if source is not self.client:raise ConnectionError('BLE session changed')
                                if self.failure:raise self.failure
                                await source.write_gatt_char(self.write_char,packet[at:at+self.fragment],response=self.write_response)
                                if at+self.fragment<len(packet):await asyncio.sleep(self.gap)
                            sent_complete=True
                            while self.response is None:
                                if source is not self.client:raise ConnectionError('BLE session changed')
                                if self.failure:raise self.failure
                                await self.event.wait();self.event.clear()
                            if source is not self.client:raise ConnectionError('BLE session changed')
                            reply=self.response
                            if len(reply)<4:raise ProtocolError('Malformed status response')
                            status=struct.unpack_from('<I',reply)[0]
                            if status:raise CommandError(status)
                            return reply[4:]
                    except TimeoutError as error:
                        if not sent_complete:
                            cls=AmbiguousActionError if cmd in (RUN,RESET) else TimeoutError
                            raise cls('BLE frame write timed out; reconnect and confirm state before retry') from error
                        if attempt==retries:
                            if cmd in (RUN,RESET):raise AmbiguousActionError('Action ACK unconfirmed; reconnect and INFO before any new RUN/RESET') from error
                            raise TimeoutError(f'No valid BLE response to command{cmd} after{attempt+1} attempts') from error
                        self.parser.clear();self.response=None;self.event.clear()
            finally:self.pending=None
    async def info(self):return (await self.command(INFO)).decode('ascii')
    async def _download(self,data,block):
        if not 1<=block<=1024:raise ValueError('Download block must be1..1024 bytes')
        await self.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data)))
        for at in range(0,len(data),block):
            part=data[at:at+block];reply=await self.command(DATA,struct.pack('<I',at)+part)
            if reply!=struct.pack('<I',at+len(part)):raise ProtocolError('Unexpected committed offset')
        await self.command(END)
    async def load_program(self,data,run=False,block=256):
        if not 1<=len(data)<=USER_PROGRAM_MAX:raise ValueError(f'User program must be1..{USER_PROGRAM_MAX} bytes')
        if not (await self.info()).startswith('KLQ ROBOT FW '):raise ProtocolError('Python text requires APP mode; BEGIN in BOOT updates firmware')
        await self._download(data,block)
        if run:
            deadline=time.monotonic()+3
            while True:
                info=await self.info();fields=dict(p.strip().split('=',1) for p in info.split(';') if '=' in p)
                if fields.get('UI')=='1' and fields.get('USER_VALID')=='1':break
                if time.monotonic()>=deadline:raise TimeoutError('Download complete but APP not READY')
                await asyncio.sleep(.05)
            await self.command(RUN)
    async def flash(self,data,run=False,block=256):
        validate_image(data)
        if not 2<=block<=1024 or block&1:raise ValueError('Firmware block must be even,2..1024 bytes')
        if not boot_mode(await self.info()):raise ProtocolError('Firmware write requires BOOT mode; use RESET and confirm INFO separately')
        await self._download(data,block)
        if run:await self.command(RUN)

async def discover(seconds=10,address=None,name='KLQ2'):
    from bleak import BleakScanner
    found=await BleakScanner.discover(timeout=seconds,return_adv=True)
    rows=[];matches=[]
    for device,adv in found.values():
        label=adv.local_name or device.name
        rows.append({'address':device.address,'name':label,'rssi':adv.rssi,'service_uuids':list(adv.service_uuids)})
        if (address and device.address.lower()==address.lower()) or (not address and label==name):matches.append(device)
    return rows,matches

async def cli(args):
    rows,matches=await discover(args.scan_seconds,args.address,args.name)
    if args.action=='scan':print(json.dumps(rows,ensure_ascii=False,indent=2));return
    if len(matches)!=1:raise RuntimeError(f'Expected one matching peripheral; found{len(matches)}. Use scan/address; will not connect to an unknown device.')
    async with Client(matches[0],args.timeout,gap=args.gap_ms/1000) as client:
        print(json.dumps(client.describe(),ensure_ascii=False,indent=2))
        if args.action=='services':return
        if args.action=='info':print(await client.info())
        elif args.action=='status':print(json.dumps(ble_status(await client.command(0x56)),ensure_ascii=False,indent=2))
        elif args.action=='echo':
            data=args.text.encode('utf-8');reply=await client.command(ECHO,data)
            if reply!=data:raise ProtocolError('Echo mismatch')
            print(reply.decode('utf-8'))
        elif args.action=='program':
            await client.load_program(args.python.read_bytes(),args.run,args.block_size);print('RAM program CRC accepted'+('; RUN sent' if args.run else '; READY after completion icon'))
        elif args.action=='flash':
            await client.flash(args.bin.read_bytes(),args.run,args.block_size);print('System APP image CRC accepted'+('; RUN sent' if args.run else ''))
        elif args.action=='run':print(await client.info());await client.command(RUN);print('RUN sent; reconnect/INFO to confirm mode/state')
        elif args.action=='reset':await client.command(RESET);print('RESET sent; reconnect/INFO to confirm BOOT')
        elif args.action=='stop':await client.command(STOP);print('STOP acknowledged')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--address',help='Only connect to this advertised BLE address')
    p.add_argument('--name',default='KLQ2');p.add_argument('--scan-seconds',type=float,default=10)
    p.add_argument('--timeout',type=float,default=10);p.add_argument('--gap-ms',type=float,default=20)
    p.add_argument('--block-size',type=int,default=256);p.add_argument('--deps',type=Path,help='Optional isolated Bleak package directory')
    sub=p.add_subparsers(dest='action',required=True)
    for action in ('scan','services','info','status','run','stop','reset'):sub.add_parser(action)
    e=sub.add_parser('echo');e.add_argument('text',nargs='?',default='KLQ2 BLE test')
    a=sub.add_parser('program');a.add_argument('python',type=Path);a.add_argument('--run',action='store_true')
    f=sub.add_parser('flash');f.add_argument('bin',type=Path);f.add_argument('--run',action='store_true')
    args=p.parse_args()
    if args.deps:
        import sys
        sys.path.insert(0,str(args.deps.resolve()))
    asyncio.run(cli(args))

if __name__=='__main__':main()
