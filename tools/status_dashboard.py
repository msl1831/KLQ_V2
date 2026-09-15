"""Serve a live KLQ host dashboard from the robot USB CDC stream."""
from __future__ import annotations

import argparse
import copy
import json
import re
import struct
import threading
import time
import zlib
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial
from serial.tools import list_ports

MAGIC = b"KLQ1"
INFO = 1
VID, PIDS = 0x314B, {0x0108, 0x0109}
PORT_ON = re.compile(
    r"P([1-3]) ON C=(\d+) T=(\d+) UID=([0-9A-F/]+) D=(\d+)mm "
    r"TICKS=(\d+) ST=(\d+) SEQ=(\d+) TO=(\d+) FE=(\d+) UE=(\d+)"
)
PORT_OFF = re.compile(r"P([1-3]) OFF TO=(\d+) FE=(\d+) UE=(\d+) REQ=(\d+) RESP=(\d+)")
SC7 = re.compile(r"X=(-?\d+)mg Y=(-?\d+)mg Z=(-?\d+)mg ROLL=(-?[\d.]+)deg PITCH=(-?[\d.]+)deg")
SC7_READY = re.compile(r"SC7A20 READY ADDR=(\S+) ID=(\S+) VER=(\S+)")


def blank_state(port: str | None = None) -> dict:
    return {
        "connected": False, "port": port or "自动识别", "since": 0.0,
        "last_update": 0.0, "error": "等待 KLQ USB CDC", "firmware": {},
        "accelerometer": {"online": False, "x": 0, "y": 0, "z": 0,
                          "roll": 0.0, "pitch": 0.0, "address": "--",
                          "identity": "--", "version": "--", "updated": 0.0},
        "ports": [
            {"index": i, "online": False, "class": 0, "type": 0, "uid": "--",
             "distance": 0, "ticks": 0, "sample_status": 0, "sequence": 0,
             "timeout": 0, "frame_error": 0, "uart_error": 0,
             "requests": 0, "responses": 0, "updated": 0.0}
            for i in range(1, 4)
        ],
        "events": []
    }


class Monitor:
    def __init__(self, port: str | None):
        self.explicit_port = port
        self.state = blank_state(port)
        self.lock = threading.Lock()
        self.events = deque(maxlen=30)
        self.seq = int(time.time()) & 0xFFFFFFFF
        self.stop = threading.Event()

    def find_port(self) -> str | None:
        if self.explicit_port:
            return self.explicit_port
        matches = [p.device for p in list_ports.comports()
                   if p.vid == VID and p.pid in PIDS]
        return matches[0] if len(matches) == 1 else None

    def event(self, message: str) -> None:
        now = time.time()
        self.events.appendleft({"time": time.strftime("%H:%M:%S"), "message": message})
        self.state["events"] = list(self.events)
        self.state["last_update"] = now

    def set_connection(self, connected: bool, port: str, error: str = "") -> None:
        with self.lock:
            changed = self.state["connected"] != connected or self.state["port"] != port
            self.state["connected"], self.state["port"], self.state["error"] = connected, port, error
            if connected and changed:
                self.state["since"] = time.time()
                self.event(f"已连接 {port}")
            elif not connected and changed:
                self.event(f"连接断开：{error}")

    @staticmethod
    def packet(seq: int) -> bytes:
        head = struct.pack("<4sIHH", MAGIC, seq, INFO, 0)
        return head + struct.pack("<I", zlib.crc32(b"", zlib.crc32(head)))

    def parse_info(self, payload: bytes) -> None:
        if len(payload) < 4 or struct.unpack_from("<I", payload)[0]:
            return
        text = payload[4:].decode("ascii", "replace")
        parts = [v.strip() for v in text.split(";")]
        info = {"name": parts[0]} if parts else {}
        for part in parts[1:]:
            if "=" in part:
                key, value = part.split("=", 1)
                info[key] = value
        with self.lock:
            changed = self.state["firmware"] != info
            self.state["firmware"] = info
            if changed:
                self.event("主机状态已更新")

    def parse_line(self, raw: bytes) -> None:
        line = raw.decode("ascii", "ignore").strip()
        if not line:
            return
        now = time.time()
        match = PORT_ON.search(line)
        with self.lock:
            if match:
                values = match.groups(); p = self.state["ports"][int(values[0]) - 1]
                was_online = p["online"]
                p.update({"online": True, "class": int(values[1]), "type": int(values[2]),
                          "uid": values[3], "distance": int(values[4]), "ticks": int(values[5]),
                          "sample_status": int(values[6]), "sequence": int(values[7]),
                          "timeout": int(values[8]), "frame_error": int(values[9]),
                          "uart_error": int(values[10]), "updated": now})
                if not was_online:
                    self.event(f"端口 {p['index']} 设备上线：{p['uid']}")
            else:
                match = PORT_OFF.search(line)
                if match:
                    values = match.groups(); p = self.state["ports"][int(values[0]) - 1]
                    was_online = p["online"]
                    p.update({"online": False, "timeout": int(values[1]),
                              "frame_error": int(values[2]), "uart_error": int(values[3]),
                              "requests": int(values[4]), "responses": int(values[5]), "updated": now})
                    if was_online:
                        self.event(f"端口 {p['index']} 设备离线")
                else:
                    match = SC7.search(line)
                    if match:
                        values = match.groups(); a = self.state["accelerometer"]
                        a.update({"online": True, "x": int(values[0]), "y": int(values[1]),
                                  "z": int(values[2]), "roll": float(values[3]),
                                  "pitch": float(values[4]), "updated": now})
                    else:
                        match = SC7_READY.search(line)
                        if match:
                            a = self.state["accelerometer"]
                            a.update({"online": True, "address": match.group(1),
                                      "identity": match.group(2), "version": match.group(3),
                                      "updated": now})
                            self.event("SC7A20HTR 已就绪")
                        elif line.startswith("SC7A20 ERROR"):
                            self.state["accelerometer"]["online"] = False
                            self.event(line)
            self.state["last_update"] = now

    def consume(self, data: bytearray) -> None:
        while data:
            magic = data.find(MAGIC)
            newline = data.find(b"\n")
            if newline >= 0 and (magic < 0 or newline < magic):
                self.parse_line(bytes(data[:newline + 1])); del data[:newline + 1]
                continue
            if magic > 0:
                self.parse_line(bytes(data[:magic])); del data[:magic]
                continue
            if magic == 0:
                if len(data) < 16:
                    return
                _, seq, cmd, length, crc = struct.unpack_from("<4sIHHI", data)
                if length > 1032:
                    del data[0]
                    continue
                total = 16 + length
                if len(data) < total:
                    return
                frame = bytes(data[:total]); del data[:total]
                if zlib.crc32(frame[16:], zlib.crc32(frame[:12])) == crc and cmd == INFO | 0x8000:
                    self.parse_info(frame[16:])
                continue
            if newline < 0:
                if len(data) > 2048:
                    del data[:-256]
                return

    def run(self) -> None:
        while not self.stop.is_set():
            port = self.find_port()
            if not port:
                self.set_connection(False, self.explicit_port or "自动识别", "未找到 KLQ USB CDC")
                self.stop.wait(0.5); continue
            try:
                with serial.Serial(port, 115200, timeout=0.1, write_timeout=1) as device:
                    device.reset_input_buffer(); self.set_connection(True, port)
                    data, info_at = bytearray(), 0.0
                    while not self.stop.is_set():
                        now = time.monotonic()
                        if now >= info_at:
                            self.seq = (self.seq + 1) & 0xFFFFFFFF
                            device.write(self.packet(self.seq)); info_at = now + 2.0
                        chunk = device.read(max(1, device.in_waiting))
                        if chunk:
                            data.extend(chunk); self.consume(data)
            except (serial.SerialException, OSError) as error:
                self.set_connection(False, port, str(error)); self.stop.wait(0.5)

    def snapshot(self) -> bytes:
        with self.lock:
            data = copy.deepcopy(self.state)
        data["server_time"] = time.time()
        return json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


HTML = r'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>KLQ 实时观测</title>
<style>
:root{color-scheme:dark;--bg:#080b12;--card:#111722;--line:#253044;--muted:#8b9bb2;--text:#eef5ff;--cyan:#38d9ff;--green:#4ee7a8;--red:#ff687d;--amber:#ffc857}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 80% -20%,#16354d 0,transparent 38%),var(--bg);color:var(--text);font:14px/1.45 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1200px;margin:auto;padding:24px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}h1{font-size:23px;margin:0;letter-spacing:.04em}.subtitle{color:var(--muted);font-size:12px;margin-top:3px}.connection{display:flex;align-items:center;gap:9px;padding:8px 12px;border:1px solid var(--line);border-radius:20px;background:#0d131e}.dot{width:9px;height:9px;border-radius:50%;background:var(--red);box-shadow:0 0 14px var(--red)}.connected .dot{background:var(--green);box-shadow:0 0 14px var(--green)}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{grid-column:span 4;background:linear-gradient(145deg,#141c29,#0e141e);border:1px solid var(--line);border-radius:15px;padding:16px;box-shadow:0 10px 28px #0005}.card.wide{grid-column:span 8}.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.14em;color:var(--muted);margin:0 0 13px}.port-title{display:flex;justify-content:space-between;align-items:center}.badge{font-size:11px;font-weight:700;padding:4px 8px;border-radius:12px;background:#2a1820;color:var(--red)}.badge.on{background:#123128;color:var(--green)}.distance{font-size:35px;font-weight:750;letter-spacing:-.04em;margin:8px 0;color:var(--cyan)}.distance small{font-size:13px;font-weight:500;color:var(--muted);margin-left:5px}.kv{display:grid;grid-template-columns:1fr auto;gap:7px 12px}.kv span:nth-child(odd){color:var(--muted)}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace}.errors{display:flex;gap:8px;margin-top:13px}.err{flex:1;text-align:center;padding:7px 3px;border:1px solid var(--line);border-radius:8px;color:var(--muted);font-size:11px}.err b{display:block;color:var(--text);font-size:15px}.axes{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}.axis{background:#0b111a;border-radius:10px;padding:12px;text-align:center}.axis b{display:block;font-size:22px;margin-top:3px}.axis span{color:var(--muted);font-size:11px}.tilt{display:grid;grid-template-columns:150px 1fr;gap:18px;align-items:center;margin-top:15px}.horizon{width:142px;height:142px;border:1px solid var(--line);border-radius:50%;overflow:hidden;position:relative;background:#101b27}.plane{position:absolute;width:230px;height:230px;left:-44px;top:-44px;transition:transform .2s}.sky,.ground{height:50%}.sky{background:#184a6b}.ground{background:#59442e}.hline{position:absolute;left:0;right:0;top:50%;height:2px;background:#fff}.cross{position:absolute;inset:50% auto auto 50%;width:54px;height:18px;border:2px solid var(--amber);border-top:0;transform:translate(-50%,-50%)}.angle{font-size:24px;font-weight:700}.angle small{font-size:12px;color:var(--muted)}.sysname{font-size:18px;font-weight:700;color:var(--cyan);margin-bottom:12px}.chips{display:flex;flex-wrap:wrap;gap:7px}.chip{border:1px solid var(--line);border-radius:9px;padding:7px 9px;background:#0a1018}.chip span{color:var(--muted);margin-right:5px}.events{max-height:190px;overflow:auto}.event{display:grid;grid-template-columns:65px 1fr;gap:8px;padding:7px 0;border-bottom:1px solid #1c2635}.event time{color:var(--muted);font-family:monospace}.empty{color:var(--muted)}@media(max-width:850px){.card,.card.wide{grid-column:span 12}.tilt{grid-template-columns:130px 1fr}.horizon{width:125px;height:125px}}
</style></head><body><main><div class="top"><div><h1>KLQ 实时观测</h1><div class="subtitle">外接端口 · SC7A20HTR · 主机状态</div></div><div id="conn" class="connection"><i class="dot"></i><span>正在连接</span></div></div>
<div class="grid"><div id="ports" style="display:contents"></div>
<section class="card wide"><h2>内置加速度传感器</h2><div class="axes"><div class="axis"><span>X 轴</span><b id="ax">--</b><span>mg</span></div><div class="axis"><span>Y 轴</span><b id="ay">--</b><span>mg</span></div><div class="axis"><span>Z 轴</span><b id="az">--</b><span>mg</span></div></div><div class="tilt"><div class="horizon"><div id="plane" class="plane"><div class="sky"></div><div class="ground"></div><div class="hline"></div></div><div class="cross"></div></div><div><div class="angle"><span id="roll">--</span>° <small>ROLL</small></div><div class="angle"><span id="pitch">--</span>° <small>PITCH</small></div><div class="subtitle" id="scmeta">等待 SC7A20HTR</div></div></div></section>
<section class="card"><h2>机器人主机</h2><div id="sysname" class="sysname">等待状态</div><div id="chips" class="chips"></div></section>
<section class="card wide"><h2>状态事件</h2><div id="events" class="events"><div class="empty">等待数据…</div></div></section></div></main>
<script>
const $=id=>document.getElementById(id), esc=v=>String(v??'--').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const ports=$('ports'); ports.innerHTML=[1,2,3].map(i=>`<section class="card"><div class="port-title"><h2>外接端口 ${i}</h2><span id="pb${i}" class="badge">离线</span></div><div id="pd${i}" class="distance">--<small>mm</small></div><div class="kv"><span>设备</span><b id="pt${i}">等待发现</b><span>UID</span><b id="pu${i}" class="mono">--</b><span>回波</span><b id="pk${i}">-- ticks</b><span>样本</span><b id="ps${i}">--</b></div><div class="errors"><div class="err"><b id="pto${i}">0</b>超时</div><div class="err"><b id="pfe${i}">0</b>帧错误</div><div class="err"><b id="pue${i}">0</b>UART</div></div></section>`).join('');
function device(p){return p.class===1&&p.type===1?'CS100A 超声':p.class||p.type?`类别 ${p.class} / 类型 ${p.type}`:'等待发现'}
function age(d,t){return t?Math.max(0,(d.server_time-t)).toFixed(1)+'s 前':'--'}
function render(d){const c=$('conn');c.classList.toggle('connected',d.connected);c.querySelector('span').textContent=d.connected?`${d.port} 已连接`:`${d.port} 未连接`;
d.ports.forEach(p=>{const i=p.index,b=$(`pb${i}`);b.classList.toggle('on',p.online);b.textContent=p.online?'在线':'离线';$(`pd${i}`).innerHTML=(p.online?esc(p.distance):'--')+'<small>mm</small>';$(`pt${i}`).textContent=device(p);$(`pu${i}`).textContent=p.uid;$(`pk${i}`).textContent=p.online?`${p.ticks} ticks`:'--';$(`ps${i}`).textContent=p.online?`#${p.sequence} · 状态 ${p.sample_status}`:`REQ ${p.requests} / RESP ${p.responses}`;$(`pto${i}`).textContent=p.timeout;$(`pfe${i}`).textContent=p.frame_error;$(`pue${i}`).textContent=p.uart_error;});
const a=d.accelerometer;$('ax').textContent=a.online?a.x:'--';$('ay').textContent=a.online?a.y:'--';$('az').textContent=a.online?a.z:'--';$('roll').textContent=a.online?a.roll.toFixed(2):'--';$('pitch').textContent=a.online?a.pitch.toFixed(2):'--';$('plane').style.transform=`rotate(${a.roll||0}deg) translateY(${Math.max(-35,Math.min(35,a.pitch||0))}px)`;$('scmeta').textContent=a.online?`地址 ${a.address} · ID ${a.identity} · VER ${a.version} · ${age(d,a.updated)}`:'传感器离线';
const f=d.firmware||{};$('sysname').textContent=f.name||'等待主机 INFO';const keys=['APP','VALID','UI','USER_STATE','USER_VALID','USER_LENGTH','USER_MAX','USB_RESETS'];$('chips').innerHTML=keys.filter(k=>f[k]!==undefined).map(k=>`<div class="chip"><span>${k}</span>${esc(f[k])}</div>`).join('');$('events').innerHTML=d.events.length?d.events.map(e=>`<div class="event"><time>${esc(e.time)}</time><div>${esc(e.message)}</div></div>`).join(''):'<div class="empty">等待数据…</div>'}
async function update(){try{const r=await fetch('/api/state',{cache:'no-store'});render(await r.json())}catch(e){$('conn').classList.remove('connected');$('conn').querySelector('span').textContent='监控服务断开'}}setInterval(update,250);update();
</script></body></html>'''


class Handler(BaseHTTPRequestHandler):
    monitor: Monitor

    def do_GET(self) -> None:
        if self.path == "/api/state":
            body, kind = self.monitor.snapshot(), "application/json; charset=utf-8"
        elif self.path in ("/", "/index.html"):
            body, kind = HTML.encode("utf-8"), "text/html; charset=utf-8"
        else:
            self.send_error(404); return
        self.send_response(200); self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body))); self.send_header("Cache-Control", "no-store")
        self.end_headers(); self.wfile.write(body)

    def log_message(self, *_args) -> None:
        pass


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="COM port; omit to auto-detect KLQ USB")
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--http-port", type=int, default=8765)
    args = parser.parse_args()
    monitor = Monitor(args.port); Handler.monitor = monitor
    worker = threading.Thread(target=monitor.run, daemon=True); worker.start()
    server = ThreadingHTTPServer((args.listen, args.http_port), Handler)
    print(f"KLQ dashboard: http://{args.listen}:{args.http_port} ({args.port or 'auto USB'})", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        monitor.stop.set(); server.server_close(); worker.join(timeout=2)


if __name__ == "__main__":
    main()
