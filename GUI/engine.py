"""
Analysis engine: turns a stream of CAN frames into per-ID timing stats,
node activity, unrecognized-source detection and a log of error events.

The engine is pure Python (no Qt) so it can be unit-tested or reused
in a headless logger.
"""

from __future__ import annotations

import statistics
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional

# --------------------------------------------------------------------------
# SocketCAN error-frame layout (linux/can/error.h)
# --------------------------------------------------------------------------
CAN_ERR_TX_TIMEOUT = 0x001
CAN_ERR_LOSTARB = 0x002
CAN_ERR_CRTL = 0x004
CAN_ERR_PROT = 0x008
CAN_ERR_TRX = 0x010
CAN_ERR_ACK = 0x020
CAN_ERR_BUSOFF = 0x040
CAN_ERR_BUSERROR = 0x080
CAN_ERR_RESTARTED = 0x100
CAN_ERR_CNT = 0x200  # data[6] = TEC, data[7] = REC (kernel >= 5.19)

CTRL_BITS = {
    0x01: "RX buffer overflow",
    0x02: "TX buffer overflow",
    0x04: "RX error warning",
    0x08: "TX error warning",
    0x10: "RX error passive",
    0x20: "TX error passive",
    0x40: "back to error active",
}
PROT_TYPE = {
    0x01: "Bit error",
    0x02: "Form error",
    0x04: "Stuff error",
    0x08: "Bit0 error",
    0x10: "Bit1 error",
    0x20: "Overload",
    0x40: "Active error flag",
    0x80: "TX error",
}
PROT_LOC = {
    0x03: "SOF", 0x02: "ID28-21", 0x06: "ID20-18", 0x04: "SRTR", 0x05: "IDE",
    0x07: "ID17-13", 0x0F: "ID12-05", 0x0E: "ID04-00", 0x0C: "RTR", 0x0D: "RES1",
    0x09: "RES0", 0x0B: "DLC", 0x0A: "DATA", 0x08: "CRC", 0x18: "CRC delim",
    0x19: "ACK slot", 0x1B: "ACK delim", 0x1A: "EOF", 0x12: "intermission",
}


def hex_id(can_id: int, extended: bool = False) -> str:
    return f"0x{can_id:08X}" if extended else f"0x{can_id:03X}"


def decode_error_frame(err_class: int, data: bytes) -> dict:
    """Decode a SocketCAN error frame into a short description + counters."""
    d = bytes(data) + bytes(8 - len(data)) if len(data) < 8 else bytes(data)
    parts: List[str] = []
    rx_side = True
    if err_class & CAN_ERR_PROT:
        kinds = [name for bit, name in PROT_TYPE.items() if d[2] & bit and bit != 0x80]
        if d[2] & 0x80:
            rx_side = False
        loc = PROT_LOC.get(d[3])
        txt = "/".join(kinds) if kinds else "Protocol violation"
        if loc:
            txt += f" @ {loc}"
        if d[3] == 0x08 and not kinds:
            txt = "CRC error"
        parts.append(txt)
    if err_class & CAN_ERR_ACK:
        parts.append("ACK error (no ACK)")
        rx_side = False
    if err_class & CAN_ERR_TX_TIMEOUT:
        parts.append("TX timeout")
        rx_side = False
    if err_class & CAN_ERR_LOSTARB:
        parts.append(f"Arbitration lost @ bit {d[0]}" if d[0] else "Arbitration lost")
    if err_class & CAN_ERR_CRTL:
        ctrl = [name for bit, name in CTRL_BITS.items() if d[1] & bit]
        parts.append("Controller: " + (", ".join(ctrl) if ctrl else "problem"))
    if err_class & CAN_ERR_TRX:
        parts.append("Transceiver problem")
    if err_class & CAN_ERR_BUSOFF:
        parts.append("BUS OFF")
    if err_class & CAN_ERR_BUSERROR and not parts:
        parts.append("Bus error")
    if err_class & CAN_ERR_RESTARTED:
        parts.append("Controller restarted")
    tec = rec = None
    if err_class & CAN_ERR_CNT:
        tec, rec = d[6], d[7]
    return {
        "text": "; ".join(parts) if parts else f"Error frame class=0x{err_class:03X}",
        "tec": tec,
        "rec": rec,
        "busoff": bool(err_class & CAN_ERR_BUSOFF),
        "restarted": bool(err_class & CAN_ERR_RESTARTED),
        "ctrl": d[1] if err_class & CAN_ERR_CRTL else 0,
        "rx": rx_side,
        "minor": err_class == CAN_ERR_LOSTARB,  # lost arbitration is normal
    }


def frame_bits(dlc: int, extended: bool) -> float:
    """Approximate on-wire bits of a data frame incl. average bit stuffing."""
    base = (67 if extended else 47) + 8 * dlc
    return base * 1.1


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------
@dataclass
class Frame:
    ts: float  # source timestamp (seconds), used for interval measurement
    rx_mono: float  # time.monotonic() at reception, used for timeouts
    can_id: int
    extended: bool
    dlc: int
    data: bytes
    is_error: bool = False
    is_remote: bool = False


@dataclass
class LogLine:
    t: float  # seconds since start
    level: str  # ERR | WARN | UNK | INFO
    text: str


@dataclass
class IdStats:
    can_id: int
    extended: bool
    name: str
    node: str
    known: bool
    expected_ms: Optional[float]  # from DBC; None -> learned
    first_seen: float
    count: int = 0
    last_ts: Optional[float] = None
    last_mono: float = 0.0
    last_data: bytes = b""
    dlc: int = 0
    intervals: Deque[float] = field(default_factory=lambda: deque(maxlen=48))
    trend: Deque[float] = field(default_factory=lambda: deque(maxlen=48))
    learned_ms: Optional[float] = None
    status: str = "OK"  # OK | DRIFT | TIMEOUT | LEARN

    @property
    def ref_ms(self) -> Optional[float]:
        return self.expected_ms if self.expected_ms else self.learned_ms

    @property
    def measured_ms(self) -> Optional[float]:
        if not self.intervals:
            return None
        # median of recent intervals: robust against single late frames
        recent = list(self.intervals)[-12:]
        return statistics.median(recent) * 1000.0

    @property
    def jitter_ms(self) -> Optional[float]:
        if len(self.intervals) < 4:
            return None
        recent = list(self.intervals)[-24:]
        return statistics.pstdev(recent) * 1000.0

    def deviation_pct(self) -> Optional[float]:
        ref, meas = self.ref_ms, self.measured_ms
        if not ref or meas is None:
            return None
        return (meas - ref) / ref * 100.0


@dataclass
class Node:
    key: str
    title: str
    known: bool
    ids: List[int] = field(default_factory=list)
    first_seen: float = 0.0
    frames_window: Deque[float] = field(default_factory=lambda: deque())
    last_mono: float = 0.0
    acknowledged: bool = False
    last_reminder: float = 0.0
    in_dbc: bool = True  # False for nodes that exist only because traffic appeared

    def fps(self, now: float) -> float:
        while self.frames_window and now - self.frames_window[0] > 1.0:
            self.frames_window.popleft()
        return float(len(self.frames_window))


# --------------------------------------------------------------------------
# Engine
# --------------------------------------------------------------------------
class Engine:
    def __init__(
        self,
        db=None,
        bitrate: int = 500_000,
        tolerance_pct: float = 8.0,
        timeout_factor: float = 3.0,
        learn_seconds: float = 0.0,
        unknown_group_bits: int = 4,
        on_log: Optional[Callable[[LogLine], None]] = None,
    ):
        self.db = db
        self.bitrate = bitrate
        self.tol = tolerance_pct
        self.timeout_factor = timeout_factor
        self.learn_seconds = learn_seconds
        self.group_bits = max(0, unknown_group_bits)
        self.on_log = on_log

        self.start_mono = time.monotonic()
        self.ids: Dict[tuple, IdStats] = {}
        self.nodes: Dict[str, Node] = {}
        self.log: Deque[LogLine] = deque(maxlen=5000)

        self.total_frames = 0
        self.error_frames = 0
        self.error_times: Deque[float] = deque()
        self.bits_window: Deque[tuple] = deque()  # (mono, bits)
        self.load_history: Deque[float] = deque(maxlen=120)
        self.tec: Optional[int] = None
        self.rec: Optional[int] = None
        self.ctrl_state = "ERROR ACTIVE"
        self.last_stats_log = 0.0
        self.learned_ids: set = set()
        self.latest_rx = 0.0  # monotonic time of the newest frame of any ID

        self._dbc_by_id: Dict[tuple, object] = {}
        if db is not None:
            for node in getattr(db, "nodes", []):
                self._node(node.name, getattr(node, "comment", None) or node.name, known=True, in_dbc=True)
            for msg in db.messages:
                key = (msg.frame_id, bool(msg.is_extended_frame))
                self._dbc_by_id[key] = msg
                for s in msg.senders or []:
                    self._node(s, s, known=True, in_dbc=True)

        self.info(f"bitrate {bitrate} bit/s, tolerance ±{tolerance_pct:g}%, timeout ×{timeout_factor:g}")
        if db is not None:
            self.info(f"DBC loaded: {len(db.messages)} messages, {len(getattr(db, 'nodes', []))} nodes")
        else:
            self.info("no DBC loaded")
        if learn_seconds > 0:
            self.info(f"learning baseline for {learn_seconds:g} s — IDs seen afterwards are flagged")

    # ---------------------------------------------------------------- logging
    def now(self) -> float:
        return time.monotonic() - self.start_mono

    def _log(self, level: str, text: str):
        line = LogLine(self.now(), level, text)
        self.log.append(line)
        if self.on_log:
            self.on_log(line)

    def info(self, t): self._log("INFO", t)
    def warn(self, t): self._log("WARN", t)
    def err(self, t): self._log("ERR", t)
    def unk(self, t): self._log("UNK", t)

    # ---------------------------------------------------------------- nodes
    def _node(self, key, title, known, in_dbc=False) -> Node:
        n = self.nodes.get(key)
        if n is None:
            n = Node(key=key, title=title, known=known, first_seen=self.now(), in_dbc=in_dbc)
            self.nodes[key] = n
        return n

    def _unknown_group(self, can_id: int, extended: bool) -> str:
        if self.group_bits == 0:
            return "UNK " + hex_id(can_id, extended)
        block = can_id >> self.group_bits
        width = 8 if extended else 3
        digits = f"{block << self.group_bits:0{width}X}"
        nx = (self.group_bits + 3) // 4
        return "UNK 0x" + digits[: len(digits) - nx] + "x" * nx

    def acknowledge(self, key: str, value: bool = True):
        n = self.nodes.get(key)
        if n and not n.known:
            n.acknowledged = value
            self.info(f"{key} {'acknowledged' if value else 'un-acknowledged'} by user")

    # ---------------------------------------------------------------- ingest
    def ingest(self, f: Frame):
        if f.is_error:
            self._ingest_error(f)
            return
        self.total_frames += 1
        self.latest_rx = max(self.latest_rx, f.rx_mono)
        self.bits_window.append((f.rx_mono, frame_bits(f.dlc, f.extended)))
        key = (f.can_id, f.extended)
        st = self.ids.get(key)
        if st is None:
            st = self._new_id(f)
        if st.last_ts is not None:
            dt = f.ts - st.last_ts
            if dt > 0:
                st.intervals.append(dt)
        st.last_ts = f.ts
        st.last_mono = f.rx_mono
        st.last_data = f.data
        st.dlc = f.dlc
        st.count += 1
        node = self.nodes.get(st.node)
        if node:
            node.frames_window.append(f.rx_mono)
            node.last_mono = f.rx_mono

    def _new_id(self, f: Frame) -> IdStats:
        key = (f.can_id, f.extended)
        msg = self._dbc_by_id.get(key)
        t = self.now()
        if msg is not None:
            sender = (msg.senders or ["(no sender)"])[0]
            node = self._node(sender, sender, known=True, in_dbc=True)
            cycle = getattr(msg, "cycle_time", None)
            st = IdStats(f.can_id, f.extended, msg.name, node.key, True, float(cycle) if cycle else None, t)
        elif self.learn_seconds > 0 and t <= self.learn_seconds:
            self.learned_ids.add(key)
            grp = self._unknown_group(f.can_id, f.extended).replace("UNK", "BASE")
            node = self._node(grp, grp, known=True)
            st = IdStats(f.can_id, f.extended, "(baseline)", node.key, True, None, t)
        else:
            grp = self._unknown_group(f.can_id, f.extended)
            node = self.nodes.get(grp)
            if node is None:
                node = self._node(grp, grp, known=False)
                node.acknowledged = False
            st = IdStats(f.can_id, f.extended, "—", node.key, False, None, t)
            self.unk(
                f"new ID {hex_id(f.can_id, f.extended)} not in DBC  dlc={f.dlc}  "
                f"data={f.data.hex(' ').upper()}  -> {node.key}"
            )
        node = self.nodes[st.node]
        if f.can_id not in node.ids:
            node.ids.append(f.can_id)
            node.ids.sort()
        self.ids[key] = st
        return st

    def _ingest_error(self, f: Frame):
        info = decode_error_frame(f.can_id, f.data)
        if info["tec"] is not None:
            self.tec, self.rec = info["tec"], info["rec"]
        if info["minor"]:
            return  # arbitration loss is normal bus behaviour, don't count it
        self.error_frames += 1
        self.error_times.append(f.rx_mono)
        cnt = f"  TEC={self.tec} REC={self.rec}" if self.tec is not None else ""
        side = "rx" if info["rx"] else "tx"
        self.err(f"{info['text']:<26} {side}{cnt}")
        state = self.ctrl_state
        c = info["ctrl"]
        if info["busoff"]:
            state = "BUS OFF"
        elif info["restarted"] or c & 0x40:
            state = "ERROR ACTIVE"
        elif c & 0x30:
            state = "ERROR PASSIVE"
        elif c & 0x0C:
            state = "ERROR WARNING"
        elif self.tec is not None:
            state = self._state_from_counters()
        self._set_state(state)

    def _state_from_counters(self) -> str:
        tec, rec = self.tec or 0, self.rec or 0
        if tec > 255:
            return "BUS OFF"
        m = max(tec, rec)
        if m > 127:
            return "ERROR PASSIVE"
        if m > 95:
            return "ERROR WARNING"
        return "ERROR ACTIVE"

    def _set_state(self, state: str):
        if state != self.ctrl_state:
            (self.info if state == "ERROR ACTIVE" else self.warn)(
                f"controller state {self.ctrl_state.lower()} -> {state.lower()}"
            )
            self.ctrl_state = state

    def bus_exception(self, text: str):
        self.err(f"bus: {text}")

    # ---------------------------------------------------------------- periodic
    def tick(self):
        """Call a few times per second: timeouts, status changes, trends."""
        mono = time.monotonic()
        t = self.now()
        while self.error_times and mono - self.error_times[0] > 60:
            self.error_times.popleft()
        while self.bits_window and mono - self.bits_window[0][0] > 1.0:
            self.bits_window.popleft()
        self.load_history.append(self.bus_load())

        # Measure silence against the newest frame we have, not the wall clock,
        # so queue/GUI latency doesn't look like a missing frame. If the whole
        # bus has gone quiet, fall back to the wall clock.
        ref_now = self.latest_rx if mono - self.latest_rx < 0.5 else mono
        for st in self.ids.values():
            # learn a reference period for IDs without a DBC cycle time
            if st.expected_ms is None and st.learned_ms is None and len(st.intervals) >= 16:
                st.learned_ms = statistics.median(st.intervals) * 1000.0
            ref = st.ref_ms
            meas = st.measured_ms
            silent_ms = max(0.0, ref_now - st.last_mono) * 1000.0
            if ref is None:
                new = "LEARN"
            elif silent_ms > max(ref * self.timeout_factor, ref + 100):
                new = "TIMEOUT"
            else:
                dev = abs((meas - ref) / ref * 100.0) if meas else 0.0
                new = "DRIFT" if dev > self.tol else "OK"
            if new == "TIMEOUT":
                st.trend.append(silent_ms)
            elif meas is not None:
                st.trend.append(meas)
            if new != st.status:
                name = f"{hex_id(st.can_id, st.extended)} {st.name}"
                if new == "TIMEOUT":
                    self.err(f"{name} timeout  silent {silent_ms:.0f} ms (exp {ref:.0f} ms)")
                elif new == "DRIFT":
                    self.warn(f"{name} cycle drift  {meas:.1f} ms vs {ref:.0f} ms ({(meas - ref) / ref * 100:+.1f}%)")
                elif new == "OK" and st.status in ("DRIFT", "TIMEOUT"):
                    self.info(f"{name} back in tolerance  {meas:.1f} ms")
                st.status = new

        for n in self.nodes.values():
            if not n.known and not n.acknowledged and t - n.last_reminder > 60 and t - n.first_seen > 5:
                n.last_reminder = t
                frames = sum(s.count for s in self.ids.values() if s.node == n.key)
                self.unk(f"{n.key} still unacknowledged  {frames} frames on {len(n.ids)} ID(s)")

        if t - self.last_stats_log >= 30:
            self.last_stats_log = t
            if self.total_frames:
                self.info(
                    f"stats  load {self.bus_load():.1f}%  {self.fps():.0f} fps  "
                    f"{len(self.ids)} IDs  err/min {len(self.error_times)}"
                )

    # ---------------------------------------------------------------- queries
    def bus_load(self) -> float:
        bits = sum(b for _, b in self.bits_window)
        return min(100.0, bits / self.bitrate * 100.0)

    def fps(self) -> float:
        return float(len(self.bits_window))

    def active_nodes(self) -> List[Node]:
        """Nodes from the DBC plus anything that has actually transmitted."""
        return [n for n in self.nodes.values() if n.in_dbc or n.ids]

    def unknown_nodes(self) -> List[Node]:
        return sorted((n for n in self.nodes.values() if not n.known), key=lambda n: n.first_seen)

    def reset_stats(self):
        for st in self.ids.values():
            st.intervals.clear()
            st.trend.clear()
            st.count = 0
            st.status = "OK" if st.ref_ms else "LEARN"
        self.total_frames = 0
        self.error_frames = 0
        self.error_times.clear()
        self.info("statistics reset")
