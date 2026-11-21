"""
HALO analysis engine: turns the records of the listener (halo_log.py) into the
values shown by the GUI. Pure Python, no Qt.

What is computed HERE, from the event lines (reference values for the GUI):
  - per flow (channel x REQ/RSP/MSG): period and frequency statistics per window,
    with the same algorithm as Listener_ZCU104.cpp (Welford, sample standard
    deviation, consecutive windows, the interval across two windows counted in
    the second one);
  - GAPs (interval > GAP_FACTOR x nominal period): counted, NOT used in the stats;
  - REQ->RSP latency per channel (the value computed by the listener).

What is taken AS IS from the listener (the listener is the reference):
  - #TOUT  timeout outcomes (OK / GRIGIA / TARDIVA / MANCANTE), T = lane0_rx_timeout;
  - #ALIEN external CAN IDs.

#STAT lines are used only as a developer check (on_dev callback): they are
compared with the windows computed here and never shown to the user.

Assumption: events of the SAME flow arrive in time order (the listener reads
requests from a FIFO and one message per lane at a time). Events of different
flows may be out of order, which does not matter because every flow has its own
state. An event not newer than the last one of its flow (out of order, or sent
again because the TCP stream restarted with tail -n +1) is skipped; the same holds
for #TOUT / #ALIEN windows already counted. A NEW listener run restarts from t = 0
and would be skipped too: restart the GUI when you restart the listener.
"""

from __future__ import annotations

import math
from collections import deque
from dataclasses import dataclass, field
from typing import Callable, Deque, Dict, List, Optional, Tuple

import halo_config as cfg
from engine import LogLine
from halo_log import (
    AlienLine, HaloEvent, HeaderChannel, HeaderWindow, StatLine, ToutLine, parse_line,
)


# --------------------------------------------------------------------------
# Statistics
# --------------------------------------------------------------------------
@dataclass
class RunningStats:
    """Mean and variance one sample at a time (Welford), as in the listener."""
    n: int = 0
    mean: float = 0.0
    m2: float = 0.0

    def add(self, x: float):
        self.n += 1
        d = x - self.mean
        self.mean += d / self.n
        self.m2 += d * (x - self.mean)

    @property
    def sd(self) -> Optional[float]:
        """Sample standard deviation (n - 1); None with fewer than 2 samples."""
        return math.sqrt(self.m2 / (self.n - 1)) if self.n >= 2 else None


@dataclass
class FlowWindow:
    """Statistics of one flow in one analysis window."""
    index: int
    start_s: float
    end_s: float
    period: RunningStats = field(default_factory=RunningStats)      # [us]
    frequency: RunningStats = field(default_factory=RunningStats)   # [Hz]
    min_us: Optional[int] = None
    max_us: Optional[int] = None
    events: int = 0
    gaps: int = 0
    out_of_tolerance: int = 0       # intervals outside nominal +/- tolerance

    def add_interval(self, interval_us: int, nominal_us: float):
        self.period.add(interval_us)
        self.frequency.add(1e6 / interval_us)
        self.min_us = interval_us if self.min_us is None else min(self.min_us, interval_us)
        self.max_us = interval_us if self.max_us is None else max(self.max_us, interval_us)
        if abs(interval_us - nominal_us) > nominal_us * cfg.PERIOD_TOLERANCE:
            self.out_of_tolerance += 1

    @property
    def freq_hz(self) -> Optional[float]:
        return 1e6 / self.period.mean if self.period.n else None


@dataclass
class Flow:
    """One event type on one channel."""
    channel: str
    type: str
    nominal_us: float
    last_us: Optional[int] = None
    current: Optional[FlowWindow] = None
    closed: Deque[FlowWindow] = field(default_factory=lambda: deque(maxlen=200))
    total: RunningStats = field(default_factory=RunningStats)      # whole run, gaps excluded [us]
    total_min_us: Optional[int] = None
    total_max_us: Optional[int] = None
    total_events: int = 0
    total_gaps: int = 0
    longest_gap_us: int = 0
    status: str = "LEARN"           # LEARN | OK | DRIFT | SILENT

    @property
    def key(self) -> Tuple[str, str]:
        return self.channel, self.type

    def reference_window(self) -> Optional[FlowWindow]:
        """Window used for the status: the current one if it has enough samples."""
        if self.current is not None and self.current.period.n >= 5:
            return self.current
        return self.closed[-1] if self.closed else self.current


@dataclass
class Latency:
    """REQ->RSP latency of one channel, from the lat column of the listener."""
    window: RunningStats = field(default_factory=RunningStats)
    total: RunningStats = field(default_factory=RunningStats)
    last_us: Optional[int] = None
    max_us: Optional[int] = None


@dataclass
class TimeoutTotals:
    """Timeout outcomes of one channel, summed from the #TOUT lines."""
    timeout_us: Optional[int] = None
    ok: int = 0
    gray: int = 0
    late: int = 0
    missing: int = 0
    last_window: Optional[ToutLine] = None


@dataclass
class AlienId:
    can_id: int
    frames: int = 0
    windows: int = 0
    first_seen_s: float = 0.0
    last_seen_s: float = 0.0
    last_window_frames: int = 0


# --------------------------------------------------------------------------
# Engine
# --------------------------------------------------------------------------
class HaloEngine:
    SILENT_FACTOR = cfg.GAP_FACTOR      # silence longer than this x nominal = SILENT
    CLOSE_MARGIN_S = 1.0                # a window is closed by time this long after its end

    def __init__(self,
                 on_log: Optional[Callable[[LogLine], None]] = None,
                 on_dev: Optional[Callable[[str], None]] = None):
        self.on_log = on_log            # user-facing log (GUI terminal)
        self.on_dev = on_dev            # developer check (#STAT comparison etc.)
        self.on_event: Optional[Callable[[HaloEvent], None]] = None   # every accepted event (GUI stream)

        self.window_s = cfg.DEFAULT_WINDOW_S
        self.window_from_header = False
        self.channels: Dict[str, cfg.HaloChannel] = dict(cfg.CHANNELS)

        self.flows: Dict[Tuple[str, str], Flow] = {}
        for ch in self.channels:
            for ty in cfg.FLOW_TYPES:
                self.flows[(ch, ty)] = Flow(ch, ty, cfg.NOMINAL_PERIOD_US[ty])

        self.latency: Dict[str, Latency] = {ch: Latency() for ch in self.channels}
        self._pending_req: Dict[str, Dict[int, int]] = {ch: {} for ch in self.channels}  # old format only
        self.timeouts: Dict[str, TimeoutTotals] = {
            ch: TimeoutTotals(timeout_us=c.lane0_rx_timeout_us) for ch, c in self.channels.items()
        }

        self.aliens: Dict[int, AlienId] = {}
        self.alien_frames_total = 0
        self.alien_untracked_total = 0
        self.alien_windows = 0
        self.alien_windows_with_frames = 0
        self.last_alien_window: Optional[AlienLine] = None

        self.log: Deque[LogLine] = deque(maxlen=5000)   # read by the GUI terminal
        self.recent_events: Deque[HaloEvent] = deque(maxlen=500)
        self.now_s = 0.0                # newest event timestamp seen (listener clock)
        self.lines = 0
        self.unparsed = 0
        self.duplicates = 0             # events not newer than the last one of their flow
        self._gap_group = None          # gaps of several flows close in time -> one warning
        self._last_tout_w: Dict[str, float] = {}
        self._last_alien_w = -1.0

        # developer check
        self._stat_lines: Dict[Tuple[str, str, int], StatLine] = {}
        self._engine_windows: Dict[Tuple[str, str, int], FlowWindow] = {}
        self.check = {"checked": 0, "match": 0, "differ": 0, "skipped_gap": 0}

    # ---------------------------------------------------------------- logging
    def _log(self, level: str, text: str, t: Optional[float] = None):
        line = LogLine(self.now_s if t is None else t, level, text)
        self.log.append(line)
        if self.on_log:
            self.on_log(line)

    def info(self, text: str):
        self._log("INFO", text)

    def _dev(self, text: str):
        if self.on_dev:
            self.on_dev(text)

    # ---------------------------------------------------------------- input
    def feed_line(self, line: str):
        """Entry point for sources that deliver raw text lines."""
        self.lines += 1
        rec = parse_line(line)
        if rec is None:
            if line.strip() and not line.lstrip().startswith("#"):
                self.unparsed += 1
                self._dev(f"riga non riconosciuta: {line.strip()[:80]}")
            return
        self.ingest(rec)

    def ingest(self, rec):
        if isinstance(rec, HaloEvent):
            self._on_event(rec)
        elif isinstance(rec, StatLine):
            self._on_stat(rec)
        elif isinstance(rec, ToutLine):
            self._on_tout(rec)
        elif isinstance(rec, AlienLine):
            self._on_alien(rec)
        elif isinstance(rec, HeaderWindow):
            self.window_s = rec.window_s
            self.window_from_header = True
            self._log("INFO", f"listener: finestra di analisi {rec.window_s:g} s")
        elif isinstance(rec, HeaderChannel):
            self._on_header_channel(rec)

    # ---------------------------------------------------------------- header
    def _on_header_channel(self, h: HeaderChannel):
        if h.channel not in self.channels:
            # channel not in halo_config: add it with nominal periods
            self.channels[h.channel] = cfg.HaloChannel(h.channel, h.global_id, h.controller,
                                                       h.responder, h.timeout_us or 0)
            for ty in cfg.FLOW_TYPES:
                self.flows[(h.channel, ty)] = Flow(h.channel, ty, cfg.NOMINAL_PERIOD_US[ty])
            self.latency[h.channel] = Latency()
            self._pending_req[h.channel] = {}
            self.timeouts[h.channel] = TimeoutTotals()
            self._log("WARN", f"{h.channel}: canale non presente in halo_config, aggiunto dal log")
        if h.timeout_us is not None:
            self.timeouts[h.channel].timeout_us = h.timeout_us or None
        t = self.timeouts[h.channel].timeout_us
        self._log("INFO", f"{h.channel}: {h.controller} (0x{h.controller_id:02X}) <-> "
                          f"{h.responder} (0x{h.responder_id:02X}), "
                          f"timeout {t if t else 'off'}{' us' if t else ''}")

    # ---------------------------------------------------------------- events
    def _window_index(self, t_s: float) -> int:
        return int(math.floor(t_s / self.window_s + 1e-9))

    def _new_window(self, idx: int) -> FlowWindow:
        return FlowWindow(idx, idx * self.window_s, (idx + 1) * self.window_s)

    def _on_event(self, ev: HaloEvent):
        flow = self.flows.get((ev.channel, ev.type))
        if flow is None:
            self._dev(f"evento di un flusso sconosciuto: {ev.channel} {ev.type}")
            return
        t_us = int(round(ev.t * 1e6))
        if flow.last_us is not None and t_us <= flow.last_us:
            # Same flow, not newer than the last event: duplicate (e.g. the TCP stream
            # restarted with tail -n +1) or out of order. Not counted.
            self.duplicates += 1
            if self.duplicates == 1 or self.duplicates % 1000 == 0:
                self._dev(f"{ev.channel} {ev.type}: evento non piu' recente dell'ultimo, ignorato "
                          f"({self.duplicates} finora)")
            return

        self.recent_events.append(ev)
        self.now_s = max(self.now_s, ev.t)
        idx = self._window_index(ev.t)

        # Window change: close the current one (and empty ones in between).
        if flow.current is None:
            flow.current = self._new_window(idx)
        elif idx > flow.current.index:
            self._close_until(flow, idx)

        flow.total_events += 1
        flow.current.events += 1

        if flow.last_us is not None:
            interval = t_us - flow.last_us
            if interval > self.SILENT_FACTOR * flow.nominal_us:
                # GAP: counted, kept out of the statistics.
                flow.current.gaps += 1
                flow.total_gaps += 1
                flow.longest_gap_us = max(flow.longest_gap_us, interval)
                self._add_gap(flow, ev.t, interval)
            elif interval > 0:
                flow.current.add_interval(interval, flow.nominal_us)
                flow.total.add(interval)
                flow.total_min_us = interval if flow.total_min_us is None else min(flow.total_min_us, interval)
                flow.total_max_us = interval if flow.total_max_us is None else max(flow.total_max_us, interval)
        flow.last_us = t_us

        if ev.type == "REQ" and ev.tag is not None:
            self._pending_req[ev.channel][ev.tag] = t_us
        elif ev.type == "RSP":
            self._on_response(ev, t_us)

        self._update_status(flow)
        if self.on_event:
            self.on_event(ev)

    def _on_response(self, ev: HaloEvent, t_us: int):
        lat = ev.lat_us
        if lat is None and ev.outcome is None and ev.tag is not None:
            # Old log format (no lat column): match the REQ with the same tag.
            req = self._pending_req[ev.channel].pop(ev.tag, None)
            if req is not None and 0 <= t_us - req <= self.SILENT_FACTOR * cfg.NOMINAL_PERIOD_US["REQ"]:
                lat = t_us - req
        if lat is not None:
            ev.lat_us = lat             # old format: shown in the GUI stream too
        if lat is not None and ev.outcome != "TARDIVA":
            # TARDIVA responses are counted in #TOUT, not in the latency statistics
            L = self.latency[ev.channel]
            L.window.add(lat)
            L.total.add(lat)
            L.last_us = lat
            L.max_us = lat if L.max_us is None else max(L.max_us, lat)
        if ev.outcome in ("GRIGIA", "TARDIVA"):
            self._log("WARN", f"{ev.channel} risposta tag {ev.tag}: {ev.outcome}, latenza {lat} us", ev.t)
        elif ev.outcome == "?":
            self._dev(f"{ev.channel} risposta tag {ev.tag} senza richiesta vista")

    def _close_until(self, flow: Flow, idx: int):
        """Closes flow.current and any empty window before idx; opens window idx."""
        while flow.current.index < idx:
            done = flow.current
            flow.closed.append(done)
            self._engine_windows[(flow.channel, flow.type, done.index)] = done
            self._try_check(flow.channel, flow.type, done.index)
            flow.current = self._new_window(done.index + 1)
        if flow.type == "RSP":
            self.latency[flow.channel].window = RunningStats()

    def _update_status(self, flow: Flow):
        if flow.last_us is None:
            flow.status = "LEARN"
            return
        silent_us = self.now_s * 1e6 - flow.last_us
        if silent_us > self.SILENT_FACTOR * flow.nominal_us:
            flow.status = "SILENT"
            return
        w = flow.reference_window()
        if w is None or w.period.n < 2:
            flow.status = "LEARN"
            return
        dev = abs(w.period.mean - flow.nominal_us) / flow.nominal_us
        flow.status = "DRIFT" if dev > cfg.PERIOD_TOLERANCE else "OK"

    def tick(self):
        """Call a few times per second: statuses and windows closed by time."""
        changes: Dict[Tuple[str, str], List[Flow]] = {}
        for flow in self.flows.values():
            old = flow.status
            if flow.current is not None and self.now_s > flow.current.end_s + self.CLOSE_MARGIN_S:
                self._close_until(flow, self._window_index(self.now_s))
            self._update_status(flow)
            if flow.status != old and old != "LEARN":
                changes.setdefault((old, flow.status), []).append(flow)
        # one line per kind of change, not one per flow
        for (old, new), flows in changes.items():
            level = "INFO" if new == "OK" else "WARN"
            self._log(level, f"{self._flow_list(flows)}: {old} -> {new}")
        self._flush_gaps()

    def flush(self):
        """End of input (replay finished): closes the last windows."""
        for flow in self.flows.values():
            if flow.current is not None and flow.current.events:
                self._close_until(flow, flow.current.index + 1)
        self._flush_gaps(force=True)

    # ---------------------------------------------------------------- gaps
    GAP_GROUP_S = 0.5   # gaps of different flows ending within this time are one interruption

    def _add_gap(self, flow: Flow, t_s: float, interval_us: int):
        g = self._gap_group
        if g is not None and t_s - g["t_end"] > self.GAP_GROUP_S:
            self._flush_gaps(force=True)
            g = None
        if g is None:
            g = self._gap_group = {"t_end": t_s, "flows": [], "longest_us": 0}
        g["flows"].append(flow)
        g["longest_us"] = max(g["longest_us"], interval_us)

    def _flush_gaps(self, force: bool = False):
        """Logs the pending interruption once no more flows can join it."""
        g = self._gap_group
        if g is None or (not force and self.now_s - g["t_end"] <= self.GAP_GROUP_S):
            return
        self._gap_group = None
        d = g["longest_us"] / 1e6
        self._log("WARN", f"interruzione di {d:.3f} s ({g['t_end'] - d:.3f} → {g['t_end']:.3f} s) "
                          f"su {self._flow_list(g['flows'])}, esclusa dalle statistiche", g["t_end"])

    def _flow_list(self, flows: List[Flow]) -> str:
        if len({f.key for f in flows}) == len(self.flows):
            return "tutti i flussi"
        return ", ".join(dict.fromkeys(f"{f.channel} {f.type}" for f in flows))   # no repeats

    # ---------------------------------------------------------------- #TOUT
    def _on_tout(self, r: ToutLine):
        if r.w_start <= self._last_tout_w.get(r.channel, -1.0):
            return                      # window already counted (stream sent again)
        self._last_tout_w[r.channel] = r.w_start
        tt = self.timeouts.setdefault(r.channel, TimeoutTotals())
        tt.timeout_us = r.timeout_us
        tt.ok += r.ok
        tt.gray += r.gray
        tt.late += r.late
        tt.missing += r.missing
        tt.last_window = r
        if r.missing or r.late:
            self._log("WARN", f"{r.channel} finestra {r.w_start:.1f}-{r.w_end:.1f} s: "
                              f"{r.missing} richieste senza risposta, {r.late} risposte tardive")

    # ---------------------------------------------------------------- #ALIEN
    def _on_alien(self, r: AlienLine):
        if r.w_start <= self._last_alien_w:
            return                      # window already counted (stream sent again)
        self._last_alien_w = r.w_start
        self.alien_windows += 1
        self.last_alien_window = r
        if r.error_code is not None:
            self._dev(f"#ALIEN non disponibile (code {r.error_code})")
            return
        for a in self.aliens.values():
            a.last_window_frames = 0
        window_frames = r.untracked_frames
        for can_id, frames in r.ids:
            a = self.aliens.get(can_id)
            if a is None:
                a = self.aliens[can_id] = AlienId(can_id, first_seen_s=r.w_start)
                self._log("UNK", f"nodo estraneo: ID 0x{can_id:08X}, {frames} frame "
                                 f"nella finestra {r.w_start:.1f}-{r.w_end:.1f} s", r.w_end)
            a.frames += frames
            a.windows += 1
            a.last_seen_s = r.w_end
            a.last_window_frames = frames
            window_frames += frames
        self.alien_untracked_total += r.untracked_frames
        self.alien_frames_total += window_frames
        if window_frames:
            self.alien_windows_with_frames += 1

    # ---------------------------------------------------------------- #STAT check
    def _on_stat(self, s: StatLine):
        if abs((s.w_end - s.w_start) - self.window_s) > 0.05 and s.w_start == 0.0:
            self._dev(f"finestra del listener {s.w_end - s.w_start:g} s diversa da quella "
                      f"dell'engine {self.window_s:g} s")
        idx = int(round(s.w_start / self.window_s))
        self._stat_lines[(s.channel, s.type, idx)] = s
        self._try_check(s.channel, s.type, idx)

    def _try_check(self, ch: str, ty: str, idx: int):
        key = (ch, ty, idx)
        s = self._stat_lines.get(key)
        w = self._engine_windows.get(key)
        if s is None or w is None:
            return
        del self._stat_lines[key]
        self.check["checked"] += 1
        tag = f"{ch} {ty} {s.w_start:.1f}-{s.w_end:.1f}"
        if w.gaps:
            self.check["skipped_gap"] += 1
            self._dev(f"CHECK {tag}: non confrontabile (gap escluso dall'engine)")
            return
        diffs = []
        if abs(s.n - w.period.n) > 1:
            diffs.append(f"n {s.n}/{w.period.n}")
        if s.t_mean_us is not None and w.period.n:
            if abs(s.t_mean_us - w.period.mean) > 1.0:
                diffs.append(f"T_mean {s.t_mean_us:.1f}/{w.period.mean:.1f}")
        if s.t_sd_us is not None and w.period.sd is not None:
            if abs(s.t_sd_us - w.period.sd) > max(1.0, 0.1 * s.t_sd_us):
                diffs.append(f"T_sd {s.t_sd_us:.1f}/{w.period.sd:.1f}")
        if diffs:
            self.check["differ"] += 1
            self._dev(f"CHECK {tag}: DIVERSO (listener/engine) " + ", ".join(diffs))
        else:
            self.check["match"] += 1
            self._dev(f"CHECK {tag}: ok")

    # ---------------------------------------------------------------- queries
    def flow_rows(self) -> List[Flow]:
        order = {ty: i for i, ty in enumerate(cfg.FLOW_TYPES)}
        return sorted(self.flows.values(), key=lambda f: (f.channel, order.get(f.type, 9)))

    def total_events(self) -> int:
        return sum(f.total_events for f in self.flows.values())
