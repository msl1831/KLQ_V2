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
from pathlib import Path
from urllib.parse import urlsplit

import serial
from serial.tools import list_ports
from voice_usb import STATUS as VOICE_STATUS, decode as decode_voice

MAGIC = b"KLQ1"
INFO = 1
VID, PIDS = 0x314B, {0x0108, 0x0109}
PORT_ON = re.compile(
    r"P([1-3]) ON C=(\d+) T=(\d+) UID=([0-9A-F/]+) D=(\d+)mm "
    r"TICKS=(\d+) ST=(\d+) SEQ=(\d+) TO=(\d+) FE=(\d+) UE=(\d+)"
)
PORT_IR = re.compile(
    r"P([1-3]) ON C=(\d+) T=(\d+) UID=([0-9A-F/]+) L=(\d+) R=(\d+) "
    r"ST=(\d+) Q=(\d+) SEQ=(\d+) TO=(\d+) FE=(\d+) UE=(\d+)"
)
PORT_OFF = re.compile(r"P([1-3]) OFF TO=(\d+) FE=(\d+) UE=(\d+) REQ=(\d+) RESP=(\d+)")
SC7 = re.compile(r"X=(-?\d+)mg Y=(-?\d+)mg Z=(-?\d+)mg ROLL=(-?[\d.]+)deg PITCH=(-?[\d.]+)deg")
SC7_READY = re.compile(r"SC7A20 READY ADDR=(\S+) ID=(\S+) VER=(\S+)")


def blank_voice() -> dict:
    return {"supported": False, "updated": 0.0, "error": ""}


def blank_state(port: str | None = None) -> dict:
    return {
        "connected": False, "port": port or "自动识别", "since": 0.0,
        "last_update": 0.0, "error": "等待 KLQ USB CDC", "firmware": {}, "voice": blank_voice(),
        "accelerometer": {"online": False, "x": 0, "y": 0, "z": 0,
                          "roll": 0.0, "pitch": 0.0, "address": "--",
                          "identity": "--", "version": "--", "updated": 0.0},
        "ports": [
            {"index": i, "online": False, "class": 0, "type": 0, "uid": "--",
             "distance": 0, "ticks": 0, "sample_status": 0, "sequence": 0,
             "left": 0, "right": 0, "quality_flags": 0,
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
        self.paused = threading.Event()
        self.released = threading.Event(); self.released.set()
        self.owner_lock = threading.Lock()
        self.control_lock = threading.Lock()
        self.voice_seq: int | None = None
        self.info_seq: int | None = None

    def pause_reader(self, timeout: float = 3.0) -> dict:
        with self.control_lock:
            with self.owner_lock:
                self.paused.set()
            released = self.released.wait(timeout)
            return {"paused": True, "released": released}

    def resume_reader(self) -> dict:
        with self.control_lock:
            with self.owner_lock:
                if self.stop.is_set():
                    return {"paused": self.paused.is_set(), "released": self.released.is_set(), "error": "reader is stopped"}
                self.paused.clear()
            return {"paused": False, "released": self.released.is_set()}

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
            if not connected or changed:
                self.voice_seq = self.info_seq = None
            if not connected:
                self.state = blank_state(port)
                self.state["events"] = list(self.events)
            self.state["connected"], self.state["port"], self.state["error"] = connected, port, error
            if connected and changed:
                self.state["since"] = time.time()
                self.event(f"已连接 {port}")
            elif not connected and changed:
                self.event(f"连接断开：{error}")

    @staticmethod
    def packet(seq: int, command: int = INFO) -> bytes:
        head = struct.pack("<4sIHH", MAGIC, seq, command, 0)
        return head + struct.pack("<I", zlib.crc32(b"", zlib.crc32(head)))

    def parse_info(self, payload: bytes) -> bool:
        if len(payload) < 4 or struct.unpack_from("<I", payload)[0]:
            return False
        try:
            text = payload[4:].decode("ascii").strip()
            if any(ord(c) < 32 or ord(c) > 126 for c in text):
                return False
            parts = [v.strip() for v in text.split(";")]
            if not re.fullmatch(r"KLQ (?:ROBOT FW|USB BOOT) \d+(?:\.\d+){1,2}", parts[0]):
                return False
            info = {"name": parts[0]}
            for part in parts[1:]:
                if not part:
                    continue
                key, value = part.split("=", 1)
                if not re.fullmatch(r"[A-Z][A-Z0-9_]*", key) or not value or key in info:
                    return False
                info[key] = value
            if (not 0 < int(info["APP"], 0) <= 0xFFFFFFFF or int(info["MAX"], 0) <= 0
                    or int(info["PAGE"], 0) <= 0 or info["VALID"] not in ("0", "1")):
                return False
        except (UnicodeError, ValueError, KeyError):
            return False
        with self.lock:
            changed = self.state["firmware"] != info
            self.state["firmware"] = info
            if info.get("EXT_VOICE") == "1":
                self.state["voice"]["supported"] = True
            else:
                self.state["voice"] = blank_voice()
                self.voice_seq = None
            if changed:
                self.event("主机状态已更新")
            port = self.state["port"]
        self.set_connection(True, port)
        return True

    def parse_voice(self, payload: bytes) -> None:
        try:
            if len(payload) < 4:
                raise ValueError("语音状态响应过短")
            status = struct.unpack_from("<I", payload)[0]
            if status:
                raise ValueError(f"主机语音状态返回错误 {status}")
            voice = decode_voice(payload[4:])
            voice.update(supported=True, updated=time.time(), error="")
        except ValueError as error:
            with self.lock:
                self.state["voice"]["error"] = str(error)
            return
        with self.lock:
            self.state["voice"] = voice
            self.state["last_update"] = voice["updated"]

    def parse_line(self, raw: bytes) -> None:
        line = raw.decode("ascii", "ignore").strip()
        if not line:
            return
        now = time.time()
        match = PORT_IR.search(line)
        if match:
            values = match.groups()
            with self.lock:
                p = self.state["ports"][int(values[0]) - 1]
                was_online = p["online"]
                p.update({"online": True, "class": int(values[1]), "type": int(values[2]),
                          "uid": values[3], "left": int(values[4]), "right": int(values[5]),
                          "distance": 0, "ticks": 0, "sample_status": int(values[6]),
                          "quality_flags": int(values[7]), "sequence": int(values[8]),
                          "timeout": int(values[9]), "frame_error": int(values[10]),
                          "uart_error": int(values[11]), "updated": now})
                if not was_online:
                    self.event(f"端口 {p['index']} 红外传感器上线：{p['uid']}")
                self.state["last_update"] = now
            return
        match = PORT_ON.search(line)
        with self.lock:
            if match:
                values = match.groups(); p = self.state["ports"][int(values[0]) - 1]
                was_online = p["online"]
                p.update({"online": True, "class": int(values[1]), "type": int(values[2]),
                          "uid": values[3], "distance": int(values[4]), "ticks": int(values[5]),
                          "left": 0, "right": 0, "quality_flags": 0,
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
                    p.update({"online": False, "class": 0, "type": 0, "uid": "--",
                              "distance": 0, "ticks": 0, "sample_status": 0, "sequence": 0,
                              "left": 0, "right": 0, "quality_flags": 0,
                              "timeout": int(values[1]),
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
                if zlib.crc32(frame[16:], zlib.crc32(frame[:12])) == crc:
                    if cmd == INFO | 0x8000 and seq == self.info_seq:
                        if self.parse_info(frame[16:]):
                            self.info_seq = None
                    elif cmd == (VOICE_STATUS | 0x8000) and seq == self.voice_seq:
                        self.voice_seq = None
                        self.parse_voice(frame[16:])
                continue
            if newline < 0:
                if len(data) > 2048:
                    del data[:-256]
                return

    def run(self) -> None:
        while not self.stop.is_set():
            with self.owner_lock:
                claimed = not self.paused.is_set()
                if claimed:
                    self.released.clear()
            if not claimed:
                self.set_connection(False, self.explicit_port or "自动识别", "监测已暂停，串口已释放")
                self.stop.wait(0.05); continue
            port = self.explicit_port or "自动识别"
            device = None
            try:
                found = self.find_port()
                if self.paused.is_set() or self.stop.is_set():
                    continue
                if not found:
                    self.set_connection(False, port, "未找到 KLQ USB CDC")
                    self.stop.wait(0.5); continue
                port = found
                with serial.Serial(port, 115200, timeout=0.1, write_timeout=1) as device:
                    if self.paused.is_set() or self.stop.is_set():
                        continue
                    device.reset_input_buffer(); self.set_connection(False, port, "等待主机响应")
                    data, info_at, voice_at = bytearray(), 0.0, 0.0
                    while not self.stop.is_set() and not self.paused.is_set():
                        now = time.monotonic()
                        if now >= info_at:
                            self.seq = (self.seq + 1) & 0xFFFFFFFF
                            self.info_seq = self.seq
                            device.write(self.packet(self.seq)); info_at = now + 2.0
                        elif now >= voice_at:
                            with self.lock:
                                supported = self.state["voice"]["supported"]
                            if supported:
                                self.seq = (self.seq + 1) & 0xFFFFFFFF
                                self.voice_seq = self.seq
                                device.write(self.packet(self.seq, VOICE_STATUS))
                            voice_at = now + 0.25
                        chunk = device.read(max(1, device.in_waiting))
                        if chunk:
                            data.extend(chunk); self.consume(data)
            except (serial.SerialException, OSError) as error:
                self.set_connection(False, port, str(error)); self.stop.wait(0.5)
            finally:
                if device is not None and device.is_open:
                    self.stop.set()
                    self.set_connection(False, port, "串口关闭未确认，监测读取已停止")
                else:
                    if self.paused.is_set():
                        self.set_connection(False, port, "监测已暂停，串口已释放")
                    self.released.set()

    def snapshot(self) -> bytes:
        with self.lock:
            data = copy.deepcopy(self.state)
        data["server_time"] = time.time()
        data["paused"] = self.paused.is_set()
        data["serial_released"] = self.released.is_set()
        return json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


HTML = r'''<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>KLQ 实时观测</title>
<style>
:root{color-scheme:dark;--bg:#080b12;--card:#111722;--line:#253044;--muted:#8b9bb2;--text:#eef5ff;--cyan:#38d9ff;--green:#4ee7a8;--red:#ff687d;--amber:#ffc857}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 80% -20%,#16354d 0,transparent 38%),var(--bg);color:var(--text);font:14px/1.45 system-ui,"Microsoft YaHei",sans-serif}main{max-width:1200px;margin:auto;padding:24px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}h1{font-size:23px;margin:0;letter-spacing:.04em}.subtitle{color:var(--muted);font-size:12px;margin-top:3px}.connection{display:flex;align-items:center;gap:9px;padding:8px 12px;border:1px solid var(--line);border-radius:20px;background:#0d131e}.dot{width:9px;height:9px;border-radius:50%;background:var(--red);box-shadow:0 0 14px var(--red)}.connected .dot{background:var(--green);box-shadow:0 0 14px var(--green)}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{grid-column:span 4;background:linear-gradient(145deg,#141c29,#0e141e);border:1px solid var(--line);border-radius:15px;padding:16px;box-shadow:0 10px 28px #0005}.card.wide{grid-column:span 8}.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.14em;color:var(--muted);margin:0 0 13px}.port-title{display:flex;justify-content:space-between;align-items:center}.badge{font-size:11px;font-weight:700;padding:4px 8px;border-radius:12px;background:#2a1820;color:var(--red)}.badge.on{background:#123128;color:var(--green)}.distance{font-size:35px;font-weight:750;letter-spacing:-.04em;margin:8px 0;color:var(--cyan)}.distance small{font-size:13px;font-weight:500;color:var(--muted);margin-left:5px}.kv{display:grid;grid-template-columns:1fr auto;gap:7px 12px}.kv span:nth-child(odd){color:var(--muted)}.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace}.errors{display:flex;gap:8px;margin-top:13px}.err{flex:1;text-align:center;padding:7px 3px;border:1px solid var(--line);border-radius:8px;color:var(--muted);font-size:11px}.err b{display:block;color:var(--text);font-size:15px}.axes{display:grid;grid-template-columns:repeat(3,1fr);gap:9px}.axis{background:#0b111a;border-radius:10px;padding:12px;text-align:center}.axis b{display:block;font-size:22px;margin-top:3px}.axis span{color:var(--muted);font-size:11px}.tilt{display:grid;grid-template-columns:150px 1fr;gap:18px;align-items:center;margin-top:15px}.horizon{width:142px;height:142px;border:1px solid var(--line);border-radius:50%;overflow:hidden;position:relative;background:#101b27}.plane{position:absolute;width:230px;height:230px;left:-44px;top:-44px;transition:transform .2s}.sky,.ground{height:50%}.sky{background:#184a6b}.ground{background:#59442e}.hline{position:absolute;left:0;right:0;top:50%;height:2px;background:#fff}.cross{position:absolute;inset:50% auto auto 50%;width:54px;height:18px;border:2px solid var(--amber);border-top:0;transform:translate(-50%,-50%)}.angle{font-size:24px;font-weight:700}.angle small{font-size:12px;color:var(--muted)}.sysname{font-size:18px;font-weight:700;color:var(--cyan);margin-bottom:12px}.chips{display:flex;flex-wrap:wrap;gap:7px}.chip{border:1px solid var(--line);border-radius:9px;padding:7px 9px;background:#0a1018}.chip span{color:var(--muted);margin-right:5px}.events{max-height:190px;overflow:auto}.event{display:grid;grid-template-columns:65px 1fr;gap:8px;padding:7px 0;border-bottom:1px solid #1c2635}.event time{color:var(--muted);font-family:monospace}.empty{color:var(--muted)}@media(max-width:850px){.card,.card.wide{grid-column:span 12}.tilt{grid-template-columns:130px 1fr}.horizon{width:125px;height:125px}}
</style></head><body><main><div class="top"><div><h1>KLQ 实时观测</h1><div class="subtitle">外接端口 · SC7A20HTR · 主机状态 · <a href="/faces" style="color:var(--cyan)">待机表情候选</a></div></div><div id="conn" class="connection"><i class="dot"></i><span>正在连接</span></div></div>
<div class="grid"><div id="ports" style="display:contents"></div>
<section class="card wide"><h2>内置加速度传感器</h2><div class="axes"><div class="axis"><span>X 轴</span><b id="ax">--</b><span>mg</span></div><div class="axis"><span>Y 轴</span><b id="ay">--</b><span>mg</span></div><div class="axis"><span>Z 轴</span><b id="az">--</b><span>mg</span></div></div><div class="tilt"><div class="horizon"><div id="plane" class="plane"><div class="sky"></div><div class="ground"></div><div class="hline"></div></div><div class="cross"></div></div><div><div class="angle"><span id="roll">--</span>° <small>ROLL</small></div><div class="angle"><span id="pitch">--</span>° <small>PITCH</small></div><div class="subtitle" id="scmeta">等待 SC7A20HTR</div></div></div></section>
<section class="card"><h2>机器人主机</h2><div id="sysname" class="sysname">等待状态</div><div id="chips" class="chips"></div></section>
<section class="card wide"><h2>内置语音 · CI1303 · 只读观测</h2><div id="vversion" class="sysname">等待主机语音状态</div><div class="kv"><span>语音运动</span><b id="venabled">--</b><span>唤醒记录</span><b id="vawake">--</b><span>最近接收语义</span><b id="vsemantic" class="mono">--</b><span>最近处理 score / seq</span><b id="vscore">--</b><span>策略阶段</span><b id="vphase">--</b><span>策略结果记录</span><b id="vreason">--</b><span>动作记录</span><b id="vactive">--</b></div><div id="vmeta" class="subtitle" style="margin:10px 0">版本为最近查询记录，不作为CI在线心跳</div><div id="vcounts" class="chips"></div></section>
<section class="card wide"><h2>状态事件</h2><div id="events" class="events"><div class="empty">等待数据…</div></div></section></div></main>
<script>
const $=id=>document.getElementById(id), esc=v=>String(v??'--').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const ports=$('ports'); ports.innerHTML=[1,2,3].map(i=>`<section class="card"><div class="port-title"><h2>外接端口 ${i}</h2><span id="pb${i}" class="badge">离线</span></div><div id="pd${i}" class="distance">--<small>mm</small></div><div class="kv"><span>设备</span><b id="pt${i}">等待发现</b><span>UID</span><b id="pu${i}" class="mono">--</b><span>回波</span><b id="pk${i}">-- ticks</b><span>样本</span><b id="ps${i}">--</b></div><div class="errors"><div class="err"><b id="pto${i}">0</b>超时</div><div class="err"><b id="pfe${i}">0</b>帧错误</div><div class="err"><b id="pue${i}">0</b>UART</div></div></section>`).join('');
function device(p){return !p.online?'等待发现':p.class===2&&p.type===2?'直流电机':p.class===1&&p.type===1?'超声波':p.class===1&&p.type===2?'双路红外':p.class||p.type?`类别 ${p.class} / 类型 ${p.type}`:'未知设备'}
function age(d,t){return t?Math.max(0,(d.server_time-t)).toFixed(1)+'s 前':'--'}
function renderVoice(d){const v=d.voice||{},available=v.supported&&v.updated,phases=['空闲','等待启动确认','有限时运行','等待停止确认'],reasons=['无拒绝记录','运动未启用','主机忙碌','未唤醒','版本未确认','没有可用电机','存在多个电机','驱动异常','设备身份变化','请求超时','意图过期','重复意图','不支持语义','已取消'];
$('vversion').textContent=available?(v.version_valid?`已读版本 ${v.version}`:'尚无有效版本记录'):d.connected?(v.supported?'等待语音状态快照':'当前主机尚未提供语音观测'):'等待主机连接';
$('venabled').textContent=available?(v.enabled?'语音运动已启用':'运动禁用'):'--';$('vawake').textContent=available?(v.awake?'主机记录：已唤醒':'主机记录：休眠'):'--';const semantic=available?Number(v.semantic).toString(16).toUpperCase().padStart(8,'0'):null,words={'4B510001':'小奇小奇','4B510101':'电机正转','4B510102':'电机反转','4B510103':'停止电机'};$('vsemantic').textContent=available&&v.semantic?`0x${semantic}${words[semantic]?' · '+words[semantic]:''}`:'--';$('vscore').textContent=available&&v.recognitions?`${v.score} / ${v.intent_seq}`:'--';$('vphase').textContent=available?(phases[v.phase]??`阶段 ${v.phase}`):'--';$('vreason').textContent=available?(reasons[v.reason]??`原因 ${v.reason}`):'--';$('vactive').textContent=available?(v.active?`控制阶段中 · 端口 ${v.port}`:'无活动动作'):'--';$('vmeta').textContent=v.error?v.error:available?`主机快照 ${age(d,v.updated)} · 版本为最近已读记录，无CI在线心跳 · 接收语义与最近处理score可能分属不同帧`:'只查询状态，不发送唤醒、音效或运动指令';const e=v.events||[],counts=[['识别接收',v.recognitions],['重复',v.duplicates],['过期/丢弃',v.dropped],['接受动作',v.accepted],['拒绝动作',v.rejected],['动作完成',v.completed],['动作错误',v.errors],['动作取消',v.cancelled],['B0上电',e[0]],['B1唤醒',e[1]],['B2退出',e[2]],['B3播放接受',e[3]],['B4完成回调',e[4]],['播放ACK',v.play_acks],['播放回调',v.play_done],['UART错误',v.uart_errors],['RX溢出',v.rx_overflows],['TX错误',v.tx_errors],['请求超时',v.timeouts]];$('vcounts').innerHTML=available?counts.map(([k,n])=>`<div class="chip"><span>${k}</span>${esc(n)}</div>`).join(''):''}
function render(d){const c=$('conn');c.classList.toggle('connected',d.connected);c.querySelector('span').textContent=d.connected?`${d.port} 已连接`:`${d.port} 未连接${d.error?' · '+d.error:''}`;
d.ports.forEach(p=>{const i=p.index,b=$(`pb${i}`),ir=p.class===1&&p.type===2,motor=p.class===2&&p.type===2;b.classList.toggle('on',p.online);b.textContent=p.online?'在线':'离线';$(`pd${i}`).innerHTML=!p.online?'--':motor?'通信在线<small>PWM未采集</small>':ir?`左 ${esc(p.left)} / 右 ${esc(p.right)}<small>0～1000</small>`:esc(p.distance)+'<small>mm</small>';$(`pt${i}`).textContent=device(p);$(`pu${i}`).textContent=p.online?p.uid:'--';$(`pk${i}`).previousElementSibling.textContent=motor?'PWM反馈':ir?'质量标志':'回波';$(`pk${i}`).textContent=p.online?(motor?'未采集':ir?`${p.quality_flags}`:`${p.ticks} ticks`):'--';$(`ps${i}`).previousElementSibling.textContent=motor?'运行反馈':'样本';$(`ps${i}`).textContent=p.online?(motor?'未采集':`#${p.sequence} · 状态 ${p.sample_status}`):motor?'--':`REQ ${p.requests} / RESP ${p.responses}`;$(`pto${i}`).textContent=p.timeout;$(`pfe${i}`).textContent=p.frame_error;$(`pue${i}`).textContent=p.uart_error;});
const a=d.accelerometer;$('ax').textContent=a.online?a.x:'--';$('ay').textContent=a.online?a.y:'--';$('az').textContent=a.online?a.z:'--';$('roll').textContent=a.online?a.roll.toFixed(2):'--';$('pitch').textContent=a.online?a.pitch.toFixed(2):'--';$('plane').style.transform=`rotate(${a.roll||0}deg) translateY(${Math.max(-35,Math.min(35,a.pitch||0))}px)`;$('scmeta').textContent=a.online?`地址 ${a.address} · ID ${a.identity} · VER ${a.version} · ${age(d,a.updated)}`:'传感器离线';
renderVoice(d);const f=d.firmware||{};$('sysname').textContent=f.name||'等待主机 INFO';const keys=['APP','VALID','UI','USER_STATE','USER_VALID','USER_LENGTH','USER_RECEIVED','USER_MAX','PY_ERROR','PY_LINE','PK_SHORT','USB_RESETS'];$('chips').innerHTML=keys.filter(k=>f[k]!==undefined).map(k=>`<div class="chip"><span>${k}</span>${esc(f[k])}</div>`).join('');$('events').innerHTML=d.events.length?d.events.map(e=>`<div class="event"><time>${esc(e.time)}</time><div>${esc(e.message)}</div></div>`).join(''):'<div class="empty">等待数据…</div>'}
async function update(){try{const r=await fetch('/api/state',{cache:'no-store'});render(await r.json())}catch(e){$('conn').classList.remove('connected');$('conn').querySelector('span').textContent='监控服务断开'}}setInterval(update,250);update();
</script></body></html>'''


class Handler(BaseHTTPRequestHandler):
    monitor: Monitor
    faces = Path(__file__).with_name("idle_faces.html").read_bytes()

    def json_reply(self, code: int, value: dict) -> None:
        body = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(code); self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body))); self.send_header("Cache-Control", "no-store")
        self.end_headers(); self.wfile.write(body)

    def local_origin(self) -> bool:
        origin = self.headers.get("Origin")
        if origin is None:
            return True
        try:
            actual = urlsplit(origin)
            target = urlsplit("http://" + self.headers.get("Host", ""))
            return (actual.scheme == "http" and target.hostname in {"127.0.0.1", "localhost", "::1"}
                    and actual.hostname == target.hostname
                    and (actual.port or 80) == (target.port or 80) == self.server.server_port
                    and not actual.username and not actual.password and not target.username
                    and not actual.path and not actual.query and not actual.fragment)
        except ValueError:
            return False

    def do_POST(self) -> None:
        if self.path not in ("/api/pause", "/api/resume"):
            self.send_error(404); return
        if not self.local_origin():
            self.json_reply(403, {"error": "unexpected HTTP Origin"}); return
        try:
            if int(self.headers.get("Content-Length", "0")) != 0:
                raise ValueError()
        except ValueError:
            self.json_reply(400, {"error": "empty POST body required"}); return
        if self.path == "/api/pause":
            result = self.monitor.pause_reader()
            self.json_reply(200 if result["released"] else 503, result)
        else:
            result = self.monitor.resume_reader()
            self.json_reply(503 if result.get("error") else 200, result)

    def do_GET(self) -> None:
        if self.path == "/api/state":
            body, kind = self.monitor.snapshot(), "application/json; charset=utf-8"
        elif self.path in ("/", "/index.html"):
            body, kind = HTML.encode("utf-8"), "text/html; charset=utf-8"
        elif self.path == "/faces":
            body, kind = self.faces, "text/html; charset=utf-8"
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
