"""
candump sources for the frame-level view. Each one pushes into a queue:
    ("can", engine.Frame)      one CAN frame
    ("can_status", text)       connection / replay messages
    ("can_eof", description)   end of a replay
    ("can_connected", text)    new TCP connection (frames already seen will come again)

  CandumpTcpReader  live: on the ZCU104  candump -L can0 > /tmp/raw.log
                                         tail -n +1 -F /tmp/raw.log | nc <mac_ip> 5001
  CandumpReplay     replays a candump -L log (or generated lines) at the speed of its timestamps
  listener_log_to_candump()
                    builds candump -L lines from a LISTENER log, with the same timing,
                    to test the view without the board (see the function for what it adds)

Only the candump -L format is read:  (1731964800.123456) can0 00081003#0011223344556677
Run  python can_sources.py output.txt > raw_sim.log  to save the generated frames.
"""

from __future__ import annotations

import queue
import re
import sys
import threading
import time
from typing import Iterable, List, Optional

import can_id as cid
import halo_config as cfg
from engine import Frame
from halo_log import HaloEvent, parse_line
from sources import HaloTcpReader
from halo_serial import HaloSerialReader

CAN_ERR_FLAG = 0x20000000
CAN_EFF_MASK = 0x1FFFFFFF
_RE_CANDUMP = re.compile(r"^\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]{1,8})(##?)(\S*)")


def parse_candump_line(line: str) -> Optional[Frame]:
    """One line of candump -L -> Frame; None for anything else."""
    m = _RE_CANDUMP.match(line.strip())
    if not m:
        return None
    ts = float(m.group(1))
    id_txt, sep, data_txt = m.group(3), m.group(4), m.group(5)
    raw_id = int(id_txt, 16)
    extended = len(id_txt) == 8
    is_error = extended and bool(raw_id & CAN_ERR_FLAG)
    is_remote = data_txt.upper().startswith("R")
    if sep == "##" and data_txt:
        data_txt = data_txt[1:]               # CAN FD: first char is the flags nibble
    data = b"" if is_remote else bytes.fromhex(data_txt) if data_txt else b""
    return Frame(ts=ts, rx_mono=time.monotonic(), can_id=raw_id & CAN_EFF_MASK, extended=extended,
                 dlc=len(data), data=data, is_error=is_error, is_remote=is_remote)


# --------------------------------------------------------------------------
# Live over TCP (reuses HaloTcpReader: only the line handling changes)
# --------------------------------------------------------------------------
class _FrameQueue:
    """Queue adapter: text lines from HaloTcpReader become ("can", Frame) items."""

    def __init__(self, out: "queue.Queue"):
        self.out = out

    def put(self, item):
        kind, value = item
        if kind == "halo":
            fr = parse_candump_line(value)
            if fr is not None:
                self.out.put(("can", fr))
        elif kind == "halo_status":
            text = (value.replace("in attesa della ZCU", "in attesa di candump (ZCU)")
                         .replace("ZCU collegata", "candump collegato"))
            self.out.put(("can_status", text))
            if "collegata da" in value:
                # new connection: tail -n +1 sends the file again from the start
                self.out.put(("can_connected", text))


class CandumpTcpReader(HaloTcpReader):
    def __init__(self, out: "queue.Queue", port: int, host: str = "0.0.0.0"):
        super().__init__(_FrameQueue(out), port, host)
        self.name = "can-tcp"
        self.description = f"candump ZCU104 via TCP :{port}"


# --------------------------------------------------------------------------
# Replay
# --------------------------------------------------------------------------
class CandumpReplay(threading.Thread):
    """Replays candump -L lines at the speed of their timestamps.

    start_ts: timestamp that corresponds to the start of the replay (default: the first
    frame). The simulator passes the time origin of the listener log, so the two replays
    stay aligned when they run together.
    """

    def __init__(self, out: "queue.Queue", path: Optional[str] = None,
                 lines: Optional[List[str]] = None, speed: float = 1.0,
                 start_ts: Optional[float] = None, description: Optional[str] = None):
        super().__init__(daemon=True, name="can-replay")
        self.out = out
        self.path = path
        self.lines = lines
        self.speed = max(speed, 1e-3)
        self.start_ts = start_ts
        self.stop_evt = threading.Event()
        self.paused = threading.Event()
        self.description = description or (f"replay {path}" + (f" ×{speed:g}" if speed != 1.0 else ""))

    def _source(self) -> Iterable[str]:
        if self.lines is not None:
            return self.lines
        return open(self.path, encoding="utf-8", errors="replace")

    def run(self):
        try:
            src = self._source()
        except OSError as e:
            self.out.put(("can_status", f"impossibile aprire {self.path}: {e}"))
            return
        start = time.monotonic()
        paused_total = 0.0
        t0 = self.start_ts
        for line in src:
            if self.stop_evt.is_set():
                return
            fr = parse_candump_line(line)
            if fr is None:
                continue
            if t0 is None:
                t0 = fr.ts
            while not self.stop_evt.is_set():
                if self.paused.is_set():
                    p0 = time.monotonic()
                    while self.paused.is_set() and not self.stop_evt.is_set():
                        time.sleep(0.05)
                    paused_total += time.monotonic() - p0
                    continue
                wait = (fr.ts - t0) / self.speed - (time.monotonic() - start - paused_total)
                if wait <= 0:
                    break
                time.sleep(min(wait, 0.05))
            fr.rx_mono = time.monotonic()
            self.out.put(("can", fr))
        self.out.put(("can_eof", self.description))

    def stop(self):
        self.stop_evt.set()


# --------------------------------------------------------------------------
# Simulator: candump lines from a listener log
# --------------------------------------------------------------------------
SIM_EPOCH = 1_763_400_000.0      # fixed REALTIME origin, so the output is reproducible
SIM_CLOCK_PPM = -17.0            # controller clock vs ZCU, as measured on the bench
SIM_FRAME_SPACING_S = 150e-6     # distance between the segments of one message
SIM_ALIEN = (0x1ABCDEF0, 0.033, 10.0, 20.0)   # (ID, period, from, to): test external node


def listener_log_to_candump(path: str, alien: bool = True) -> List[str]:
    """candump -L lines with the timing of a listener log (current or old format).

    For every event of the log it generates the frames HALO puts on the bus:
      REQ  1 frame  (DLC 8)        RSP / MSG  3 frames (8 + 8 + 5) ending at the event time
    plus what the listener does not show:
      SYNC + FOLLOW_UP every second from the controller, with its clock at SIM_CLOCK_PPM
      and a restart (sequence and clock from 0) after every silence longer than 0.5 s,
      as the controller restart at ~24 s of output.txt;
      if alien is True, an external node (SIM_ALIEN) between 10 and 20 s.
    Payloads: REQ = 8 bytes of controller time, RSP/MSG = the payload text of the log.
    """
    events = [r for r in (parse_line(l) for l in open(path, encoding="utf-8", errors="replace"))
              if isinstance(r, HaloEvent)]
    events.sort(key=lambda e: e.t)
    if not events:
        return []
    frames = []                                        # (t, id, data)

    def ident(src, dst, ch, lane, sync, pkt, retr, tag):
        return (src << cid.SRC_SHIFT | dst << cid.DST_SHIFT | ch << cid.CH_SHIFT |
                lane << cid.LANE_SHIFT | sync << cid.SYNC_SHIFT | pkt << cid.PKT_SHIFT |
                retr << cid.RETRANS_SHIFT | tag)

    # controller lifetimes: a silence > 0.5 s on the whole bus is taken as a restart
    lives = [[events[0].t - 0.3, None]]
    for a, b in zip(events, events[1:]):
        if b.t - a.t > 0.5:
            lives[-1][1] = a.t + 0.05
            lives.append([b.t - 0.05, None])
    lives[-1][1] = events[-1].t + 0.05
    scale = 1.0 + SIM_CLOCK_PPM * 1e-6

    def ctrl_us(t):                                    # controller clock at ZCU time t
        boot = lives[0][0]
        for start, _end in lives:                      # latest (re)start before t
            if start <= t:
                boot = start
        return int((t - boot) * 1e6 * scale) & 0xFFFFFFFF

    for c_name in {ch.controller for ch in cfg.CHANNELS.values()}:
        c = cid.NODE_BY_NAME[c_name]
        for boot, end in lives:
            seq, t = 0, boot + 0.02
            while t < end:
                frames.append((t, ident(c, 0, 0, 0, 0, seq, cid.SYNC_TYPE_SYNC, 0), b"\x00"))
                frames.append((t + 250e-6, ident(c, 0, 0, 0, 0, seq, cid.SYNC_TYPE_FOLLOW_UP, 0),
                               ctrl_us(t).to_bytes(4, "little")))
                seq = (seq + 1) % 128
                t += 1.0 / scale                       # 1 s of controller clock

    for ev in events:
        ch = cfg.CHANNELS.get(ev.channel)
        if ch is None:
            continue
        c, r = cid.NODE_BY_NAME[ch.controller], cid.NODE_BY_NAME[ch.responder]
        local = ch.global_id & cid.CH_MASK
        tag = ev.tag or 0
        if ev.type == "REQ":
            frames.append((ev.t, ident(c, r, local, 0, 1, 0, 0, tag), ctrl_us(ev.t).to_bytes(8, "little")))
            continue
        text = (ev.payload or "").encode("ascii", "replace")[:21].ljust(21, b"\x00")
        segs = [text[0:8], text[8:16], text[16:21]]
        if ev.type == "RSP":
            src, dst, lane = r, c, 0
        else:
            src, dst, lane, tag = c, r, 1, 0
        for k, seg in enumerate(segs):
            t = ev.t - (len(segs) - 1 - k) * SIM_FRAME_SPACING_S
            frames.append((t, ident(src, dst, local, lane, 1, k, 0, tag), seg))

    if alien:
        aid, period, t_from, t_to = SIM_ALIEN
        t = t_from
        while t < min(t_to, events[-1].t):
            frames.append((t, aid, bytes(range(8))))
            t += period

    frames.sort(key=lambda x: x[0])
    return [f"({SIM_EPOCH + t:.6f}) can0 {i:08X}#{d.hex().upper()}" for t, i, d in frames]


class CandumpSimulator(CandumpReplay):
    """Replay of listener_log_to_candump(path), aligned with the listener log replay."""

    def __init__(self, out: "queue.Queue", listener_log: str, speed: float = 1.0):
        super().__init__(out, lines=listener_log_to_candump(listener_log), speed=speed,
                         start_ts=SIM_EPOCH,
                         description=f"simulazione candump da {listener_log}"
                                     + (f" ×{speed:g}" if speed != 1.0 else ""))

class CandumpSerialReader(HaloSerialReader):
    """candump -L printed on the ZCU104 serial console (no network).
    Same serial handling as HaloSerialReader; the lines become ("can", Frame) items."""

    def __init__(self, out: "queue.Queue", port: str, baud: int = 115200,
                 save_path: Optional[str] = None):
        super().__init__(_FrameQueue(out), port, baud, save_path)
        self.name = "can-serial"
        self.description = f"candump ZCU104 via seriale {port} @ {baud}"

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("uso: python can_sources.py <log del listener>  > raw_sim.log")
    print("\n".join(listener_log_to_candump(sys.argv[1])))
