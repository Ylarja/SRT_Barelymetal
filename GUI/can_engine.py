"""
Frame-level engine for the HALO bus, fed by candump (can_sources.py). Pure Python, no Qt.

From every frame (engine.Frame) it computes:
  - per flow (channel x REQ/RSP/MSG, SYNC, FOLLOW_UP, external sources):
      frames, frames/s, messages, segments per message, DLCs, time a message takes on the
      bus (first to last segment), message period (first segment to first segment, gaps
      longer than GAP_FACTOR x nominal excluded), frames with RETRANS field != 0, status;
  - bus load and frames/s over the last second;
  - per controller: SYNC sequence, sequence jumps, SYNC -> FOLLOW_UP delay, drift of the
    controller clock against the ZCU clock (from the timestamps carried by FOLLOW_UP),
    controller restarts;
  - nodes and external sources for the bus graphic of ui.py (TopologyWidget).

Timestamps are the candump ones (CLOCK_REALTIME of the ZCU): an interval that goes
backwards is skipped and reported. The payload is shown as it is; the only field read
from a payload is the controller timestamp of FOLLOW_UP, needed for the drift.
"""

from __future__ import annotations

import math
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional, Set, Tuple

import can_id as cid
import halo_config as cfg
from engine import Frame, LogLine, Node, frame_bits
from halo_engine import RunningStats

NOMINAL_US: Dict[str, Optional[float]] = {
    "REQ": cfg.NOMINAL_PERIOD_US["REQ"],
    "RSP": cfg.NOMINAL_PERIOD_US["RSP"],
    "MSG": cfg.NOMINAL_PERIOD_US["MSG"],
    "SYNC": cid.SYNC_PERIOD_US,
    "FOLLOW_UP": cid.SYNC_PERIOD_US,
    "ALIEN": None,              # unknown: no nominal, no gap exclusion
}
KIND_ORDER = {"REQ": 0, "RSP": 1, "MSG": 2, "SYNC": 3, "FOLLOW_UP": 4, "ALIEN": 5}


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------
@dataclass
class CanFlow:
    name: str
    kind: str
    channel: Optional[str]
    base_id: int
    extended: bool
    node_key: str
    nominal_us: Optional[float]
    frames: int = 0
    messages: int = 0
    dlcs: Set[int] = field(default_factory=set)
    retrans_nonzero: int = 0
    period: RunningStats = field(default_factory=RunningStats)      # [us], gaps excluded
    period_min_us: Optional[float] = None
    period_max_us: Optional[float] = None
    gaps: int = 0
    on_bus: RunningStats = field(default_factory=RunningStats)      # first->last segment [us]
    on_bus_max_us: Optional[float] = None
    seg_last: int = 0               # segments of the last complete message
    seg_max: int = 0
    last_start_ts: Optional[float] = None
    cur_first_ts: Optional[float] = None
    cur_last_ts: Optional[float] = None
    cur_segments: int = 0
    last_seq: Optional[int] = None  # SYNC / FOLLOW_UP sequence of the last frame
    last_ts: Optional[float] = None
    last_data: bytes = b""
    last_id: int = 0
    window: Deque[float] = field(default_factory=deque)             # frame ts, last second
    status: str = "LEARN"           # LEARN | OK | DRIFT | SILENT
    reason: str = ""                # ALIEN only

    def fps(self, now_ts: float) -> float:
        while self.window and now_ts - self.window[0] > 1.0:
            self.window.popleft()
        return float(len(self.window))


@dataclass
class IdEntry:
    """One row of the TopologyWidget tooltip (same fields it reads from engine.IdStats)."""
    can_id: int
    extended: bool
    name: str
    node: str
    count: int = 0


@dataclass
class SyncState:
    """Time-sync bursts of one controller."""
    controller: str
    syncs: int = 0
    follow_ups: int = 0
    last_seq: Optional[int] = None
    seq_jumps: int = 0
    restarts: int = 0
    sync_ts: Dict[int, float] = field(default_factory=dict)        # seq -> SYNC arrival ts
    delay: RunningStats = field(default_factory=RunningStats)       # SYNC -> FOLLOW_UP [us]
    delay_max_us: Optional[float] = None
    # drift: baseline pair (ZCU time of SYNC, controller TX time) and unwrapped controller time
    base_zcu: Optional[float] = None
    base_ctrl: float = 0.0
    prev_zcu: Optional[float] = None
    prev_ctrl_raw: int = 0
    ctrl_unwrapped: float = 0.0
    drift_ppm: Optional[float] = None
    last_ctrl_raw: Optional[int] = None


# --------------------------------------------------------------------------
# Engine
# --------------------------------------------------------------------------
class CanEngine:
    DRIFT_MIN_SPAN_S = 5.0      # drift shown after this much baseline
    RESTART_TOL = 0.01          # controller vs ZCU step mismatch that means a restart / jump

    def __init__(self, bitrate: int = 1_000_000,
                 on_log: Optional[Callable[[LogLine], None]] = None):
        self.bitrate = bitrate
        self.on_log = on_log
        self.on_frame: Optional[Callable[[Frame, cid.CanFrameInfo, float], None]] = None

        self.flows: Dict[str, CanFlow] = {}
        self.ids: Dict[Tuple[int, bool], IdEntry] = {}   # read by TopologyWidget (tooltips)
        self.nodes: Dict[str, Node] = {}                  # read by TopologyWidget
        self.sync: Dict[str, SyncState] = {}
        self.log: Deque[LogLine] = deque(maxlen=5000)

        self.first_ts: Optional[float] = None
        self.last_ts: Optional[float] = None
        self.total_frames = 0
        self.clock_jumps = 0
        self.skip_seen = False          # after a TCP reconnection: drop frames not newer than last_ts
        self.duplicates = 0
        self.bits_window: Deque[Tuple[float, float]] = deque()   # (ts, bits), last second
        self.load_history: Deque[float] = deque(maxlen=120)
        self.alien_frames = 0

        for nid, n in sorted(cfg.NODES.items()):
            role = "listener · in ascolto" if n.name == "listener" else n.name.rstrip("0123456789")
            self.nodes[n.name] = Node(key=n.name, title=f"0x{nid:02X} · {role}", known=True,
                                      first_seen=0.0, in_dbc=True)

    # ---------------------------------------------------------------- logging
    def now(self) -> float:
        """Seconds since the first frame (candump clock)."""
        if self.first_ts is None or self.last_ts is None:
            return 0.0
        return self.last_ts - self.first_ts

    def _log(self, level: str, text: str):
        line = LogLine(self.now(), level, text)
        self.log.append(line)
        if self.on_log:
            self.on_log(line)

    def info(self, text: str):
        self._log("INFO", text)

    # ---------------------------------------------------------------- input
    def ingest(self, f: Frame):
        if f.is_error or f.is_remote:
            return                      # out of scope: error frames, remote frames
        if self.skip_seen and self.last_ts is not None:
            if f.ts <= self.last_ts:
                self.duplicates += 1        # sent again by tail -n +1 after a reconnection
                return
            self.skip_seen = False
        if self.first_ts is None:
            self.first_ts = f.ts
        if self.last_ts is not None and f.ts < self.last_ts - 0.001:
            self.clock_jumps += 1
            self._log("WARN", f"orologio di candump tornato indietro di {self.last_ts - f.ts:.3f} s "
                              f"(CLOCK_REALTIME della ZCU): intervalli scartati")
            for fl in self.flows.values():
                fl.last_ts = fl.last_start_ts = fl.cur_first_ts = None
            for s in self.sync.values():
                s.base_zcu = s.prev_zcu = None
        self.last_ts = f.ts if self.last_ts is None else max(self.last_ts, f.ts)
        self.total_frames += 1
        self.bits_window.append((f.ts, frame_bits(f.dlc, f.extended)))

        info = cid.classify(f.can_id, f.extended)
        flow = self._flow(f, info)
        self._update_flow(flow, f, info)
        self._update_node(flow, f, info)
        if info.kind in ("SYNC", "FOLLOW_UP"):
            self._update_sync(f, info)
        elif info.kind == "ALIEN":
            self.alien_frames += 1
        if self.on_frame:
            self.on_frame(f, info, f.ts - self.first_ts)

    # ---------------------------------------------------------------- flows
    def _flow(self, f: Frame, info: cid.CanFrameInfo) -> CanFlow:
        name = info.flow
        fl = self.flows.get(name)
        if fl is None:
            if info.kind == "ALIEN":
                # short key for the bus graphic card ("SRC 0x1A", "11 bit")
                node_key = name.replace("ESTRANEO ", "")
                base = f.can_id                 # foreign ID: shown as it is, not masked
            else:
                node_key = info.src_name
                base = cid.base_id(f.can_id)
            fl = CanFlow(name=name, kind=info.kind, channel=info.channel, base_id=base,
                         extended=f.extended, node_key=node_key, nominal_us=NOMINAL_US[info.kind],
                         reason=info.reason)
            self.flows[name] = fl
        return fl

    def _starts_message(self, info: cid.CanFrameInfo) -> bool:
        """First frame of a message: segment 0 for data frames, every frame otherwise."""
        if info.kind in ("REQ", "RSP", "MSG"):
            return info.fields.pkt == 0
        return True

    def _update_flow(self, fl: CanFlow, f: Frame, info: cid.CanFrameInfo):
        fl.frames += 1
        fl.dlcs.add(f.dlc)
        fl.last_ts = f.ts
        fl.last_data = f.data
        fl.last_id = f.can_id
        fl.window.append(f.ts)
        if info.kind in ("REQ", "RSP", "MSG") and info.fields.retrans:
            fl.retrans_nonzero += 1         # HALO data frames only: on foreign IDs the field means nothing

        if self._starts_message(info):
            self._close_message(fl)
            fl.messages += 1
            # sync bursts: a sequence that does not continue (controller restart) breaks the period
            seq_break = False
            if info.kind in ("SYNC", "FOLLOW_UP"):
                seq = info.fields.pkt
                seq_break = fl.last_seq is not None and seq != (fl.last_seq + 1) % 128
                fl.last_seq = seq
            if fl.last_start_ts is not None:
                interval_us = (f.ts - fl.last_start_ts) * 1e6
                if seq_break or (fl.nominal_us is not None and interval_us > cfg.GAP_FACTOR * fl.nominal_us):
                    fl.gaps += 1        # interruption: counted, kept out of the statistics
                elif interval_us > 0:
                    fl.period.add(interval_us)
                    fl.period_min_us = interval_us if fl.period_min_us is None else min(fl.period_min_us, interval_us)
                    fl.period_max_us = interval_us if fl.period_max_us is None else max(fl.period_max_us, interval_us)
            fl.last_start_ts = f.ts
            fl.cur_first_ts = f.ts
            fl.cur_segments = 0
        fl.cur_segments += 1
        fl.cur_last_ts = f.ts

    def _close_message(self, fl: CanFlow):
        """Closes the message in progress when the next one starts."""
        if fl.cur_first_ts is None or fl.cur_segments == 0:
            return
        fl.seg_last = fl.cur_segments
        fl.seg_max = max(fl.seg_max, fl.cur_segments)
        if fl.cur_segments > 1:
            d = (fl.cur_last_ts - fl.cur_first_ts) * 1e6
            fl.on_bus.add(d)
            fl.on_bus_max_us = d if fl.on_bus_max_us is None else max(fl.on_bus_max_us, d)

    # ---------------------------------------------------------------- nodes
    def _update_node(self, fl: CanFlow, f: Frame, info: cid.CanFrameInfo):
        key = fl.node_key
        node = self.nodes.get(key)
        if node is None:
            node = Node(key=key, title=key, known=False, first_seen=self.now(), in_dbc=False)
            self.nodes[key] = node
            self._log("UNK", f"nodo estraneo: {key}, primo ID 0x{f.can_id:08X} ({info.reason})")
        node.frames_window.append(f.rx_mono)
        node.last_mono = f.rx_mono
        # tooltip entries: one per flow for known nodes, one per raw ID for external ones
        tip_id = f.can_id if info.kind == "ALIEN" else fl.base_id
        ent = self.ids.get((tip_id, f.extended))
        if ent is None:
            ent = self.ids[(tip_id, f.extended)] = IdEntry(tip_id, f.extended, fl.name, key)
            if tip_id not in node.ids:
                node.ids.append(tip_id)
                node.ids.sort()
        ent.count += 1

    def active_nodes(self) -> List[Node]:
        return [n for n in self.nodes.values() if n.in_dbc or n.ids]

    def unknown_nodes(self) -> List[Node]:
        return sorted((n for n in self.nodes.values() if not n.known), key=lambda n: n.first_seen)

    def acknowledge(self, key: str, value: bool = True):
        n = self.nodes.get(key)
        if n and not n.known:
            n.acknowledged = value

    # ---------------------------------------------------------------- sync
    def _update_sync(self, f: Frame, info: cid.CanFrameInfo):
        name = info.src_name
        s = self.sync.get(name)
        if s is None:
            s = self.sync[name] = SyncState(name)
        seq = info.fields.pkt
        if info.kind == "SYNC":
            s.syncs += 1
            if s.last_seq is not None and seq != (s.last_seq + 1) % 128:
                s.seq_jumps += 1
            s.last_seq = seq
            s.sync_ts[seq] = f.ts
            if len(s.sync_ts) > 8:
                s.sync_ts.pop(next(iter(s.sync_ts)))
            return

        # FOLLOW_UP: payload = controller TX time of the SYNC with the same sequence
        s.follow_ups += 1
        zcu = s.sync_ts.get(seq)
        if zcu is None or len(f.data) < 4:
            return
        d = (f.ts - zcu) * 1e6
        s.delay.add(d)
        s.delay_max_us = d if s.delay_max_us is None else max(s.delay_max_us, d)
        ctrl_raw = int.from_bytes(f.data[:4], "little")
        s.last_ctrl_raw = ctrl_raw
        self._update_drift(s, zcu, ctrl_raw)

    def _update_drift(self, s: SyncState, zcu: float, ctrl_raw: int):
        if s.base_zcu is None or s.prev_zcu is None:
            self._new_baseline(s, zcu, ctrl_raw)
            return
        step_zcu = (zcu - s.prev_zcu) * 1e6
        step_ctrl = (ctrl_raw - s.prev_ctrl_raw) % (1 << 32)      # 32-bit microsecond counter
        if step_zcu <= 0 or abs(step_ctrl - step_zcu) > self.RESTART_TOL * step_zcu + 1000:
            s.restarts += 1
            self._log("WARN", f"{s.controller}: orologio del controller discontinuo rispetto alla ZCU "
                              f"(riavvio del controller o salto dell'orologio della ZCU), "
                              f"riferimento per la deriva azzerato")
            self._new_baseline(s, zcu, ctrl_raw)
            return
        s.ctrl_unwrapped += step_ctrl
        s.prev_zcu, s.prev_ctrl_raw = zcu, ctrl_raw
        span_us = (zcu - s.base_zcu) * 1e6
        if span_us >= self.DRIFT_MIN_SPAN_S * 1e6:
            s.drift_ppm = (s.ctrl_unwrapped / span_us - 1.0) * 1e6

    @staticmethod
    def _new_baseline(s: SyncState, zcu: float, ctrl_raw: int):
        s.base_zcu = s.prev_zcu = zcu
        s.prev_ctrl_raw = ctrl_raw
        s.ctrl_unwrapped = 0.0
        s.drift_ppm = None

    # ---------------------------------------------------------------- periodic
    def tick(self):
        """Call a few times per second: bus load and flow statuses."""
        if self.last_ts is None:
            return
        while self.bits_window and self.last_ts - self.bits_window[0][0] > 1.0:
            self.bits_window.popleft()
        self.load_history.append(self.bus_load())
        changes: Dict[Tuple[str, str], List[str]] = {}
        for fl in self.flows.values():
            old = fl.status
            fl.status = self._status(fl)
            if fl.status != old and old != "LEARN":
                changes.setdefault((old, fl.status), []).append(fl.name)
        for (old, new), names in changes.items():
            self._log("INFO" if new == "OK" else "WARN", f"{', '.join(names)}: {old} -> {new}")

    def _status(self, fl: CanFlow) -> str:
        if fl.nominal_us is None:
            return "ALIEN"
        if fl.last_start_ts is not None and (self.last_ts - fl.last_start_ts) * 1e6 > cfg.GAP_FACTOR * fl.nominal_us:
            return "SILENT"
        if fl.period.n < 2:
            return "LEARN"
        dev = abs(fl.period.mean - fl.nominal_us) / fl.nominal_us
        return "DRIFT" if dev > cfg.PERIOD_TOLERANCE else "OK"

    def flush(self):
        for fl in self.flows.values():
            self._close_message(fl)
            fl.cur_first_ts = None
            fl.cur_segments = 0

    # ---------------------------------------------------------------- queries
    def bus_load(self) -> float:
        bits = sum(b for _, b in self.bits_window)
        return min(100.0, bits / self.bitrate * 100.0)

    def fps(self) -> float:
        return float(len(self.bits_window))

    def flow_rows(self) -> List[CanFlow]:
        return sorted(self.flows.values(),
                      key=lambda fl: (KIND_ORDER.get(fl.kind, 9) >= 3, fl.channel or "", KIND_ORDER.get(fl.kind, 9), fl.name))


def _selftest():
    """python can_engine.py: a few frames through the engine."""
    e = CanEngine(on_log=lambda l: print(l.level, l.text))
    t0 = time.time()
    for k in range(30):
        t = t0 + k * 0.1
        e.ingest(Frame(t, time.monotonic(), 0x00081000 | (k & 7), True, 8, bytes(8)))
        for p, dlc in enumerate((8, 8, 5)):
            e.ingest(Frame(t + 0.0011 + p * 0.00015, time.monotonic(),
                           0x01001000 | p << 5 | (k & 7), True, dlc, bytes(dlc)))
    e.tick()
    for fl in e.flow_rows():
        print(fl.name, fl.frames, fl.messages, fl.seg_last, round(fl.period.mean), round(fl.on_bus.mean), fl.status)
    assert not math.isnan(e.bus_load())


if __name__ == "__main__":
    _selftest()
