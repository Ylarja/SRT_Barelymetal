"""
Parser for the stdout of Listener_ZCU104.cpp.

One function, parse_line(), turns one text line into one record:

  HaloEvent      event line (REQ / RSP / MSG)
  StatLine       #STAT   period statistics of the listener (dev check only)
  ToutLine       #TOUT   timeout outcomes per window (the listener is the reference)
  AlienLine      #ALIEN  external CAN IDs per window
  HeaderWindow   "# ... finestra di analisi: X s"
  HeaderChannel  "# channelX (global id 0x...): ... lane0_rx_timeout N us"
  None           anything else (column headers, legend, empty lines)

Two event-line formats are accepted:
  current (10 columns): time channel src dst lane type tag lat[us] esito payload
  old     (6 columns):  time channel lane type tag payload
'-' means "not applicable" and becomes None.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import List, Optional, Tuple, Union


# --------------------------------------------------------------------------
# Records
# --------------------------------------------------------------------------
@dataclass
class HaloEvent:
    t: float                    # seconds since listener start (ZCU clock, 1 us)
    channel: str
    lane: int
    type: str                   # REQ | RSP | MSG
    tag: Optional[int]          # 0-7 on lane 0, None on lane 1
    payload: Optional[str]
    src: Optional[int] = None   # None in the old format
    dst: Optional[int] = None
    lat_us: Optional[int] = None    # RSP only, computed by the listener
    outcome: Optional[str] = None   # RSP only: OK | GRIGIA | TARDIVA | ? (no matching REQ)


@dataclass
class StatLine:
    w_start: float
    w_end: float
    channel: str
    type: str
    n: int
    freq_hz: Optional[float]
    t_mean_us: Optional[float]
    t_sd_us: Optional[float]
    t_min_us: Optional[float]
    t_max_us: Optional[float]
    f_sd_hz: Optional[float]


@dataclass
class ToutLine:
    w_start: float
    w_end: float
    channel: str
    timeout_us: Optional[int]   # None = check disabled ("off")
    ok: int
    gray: int
    late: int
    missing: int
    lat_mean_us: Optional[float]
    lat_max_us: Optional[float]


@dataclass
class AlienLine:
    w_start: float
    w_end: float
    ids: List[Tuple[int, int]] = field(default_factory=list)   # (29-bit CAN ID, frames)
    untracked_frames: int = 0
    error_code: Optional[int] = None   # set when the listener printed "n/d (code N)"


@dataclass
class HeaderWindow:
    window_s: float


@dataclass
class HeaderChannel:
    channel: str
    global_id: int
    controller: str
    controller_id: int
    responder: str
    responder_id: int
    timeout_us: Optional[int]   # missing in the old header


Record = Union[HaloEvent, StatLine, ToutLine, AlienLine, HeaderWindow, HeaderChannel, None]


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def _opt_float(tok: str) -> Optional[float]:
    return None if tok == "-" else float(tok)


def _opt_int(tok: str) -> Optional[int]:
    return None if tok == "-" else int(tok, 0)


def _window(tok: str) -> Tuple[float, float]:
    a, b = tok.split("-")
    return float(a), float(b)


_RE_WINDOW = re.compile(r"finestra di analisi:\s*([0-9.]+)\s*s")
_RE_CHANNEL = re.compile(
    r"^#\s+(\S+)\s+\(global id (0x[0-9A-Fa-f]+)\):\s+"
    r"controller\s+(\S+)\s+\((0x[0-9A-Fa-f]+)\)\s+<->\s+"
    r"responder\s+(\S+)\s+\((0x[0-9A-Fa-f]+)\)"
    r"(?:,\s+lane0_rx_timeout\s+(\d+)\s+us)?"
)
_RE_UNTRACKED = re.compile(r"\+(\d+) frame")
_RE_CODE = re.compile(r"code (-?\d+)")


# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------
def parse_line(line: str) -> Record:
    line = line.rstrip("\r\n")
    s = line.strip()
    if not s:
        return None
    try:
        if s.startswith("#STAT"):
            return _parse_stat(s)
        if s.startswith("#TOUT"):
            return _parse_tout(s)
        if s.startswith("#ALIEN"):
            return _parse_alien(s)
        if s.startswith("#"):
            return _parse_header(s)
        return _parse_event(s)
    except (ValueError, IndexError):
        # Column headers ("#STAT  window[s] ...") and truncated lines end here.
        return None


def _parse_stat(s: str) -> StatLine:
    f = s.split()
    w0, w1 = _window(f[1])
    return StatLine(w0, w1, f[2], f[3], int(f[4]),
                    _opt_float(f[5]), _opt_float(f[6]), _opt_float(f[7]),
                    _opt_float(f[8]), _opt_float(f[9]), _opt_float(f[10]))


def _parse_tout(s: str) -> ToutLine:
    f = s.split()
    w0, w1 = _window(f[1])
    timeout = None if f[3] == "off" else int(f[3])
    return ToutLine(w0, w1, f[2], timeout, int(f[4]), int(f[5]), int(f[6]), int(f[7]),
                    _opt_float(f[8]), _opt_float(f[9]))


def _parse_alien(s: str) -> AlienLine:
    f = s.split()
    w0, w1 = _window(f[1])
    rec = AlienLine(w0, w1)
    if f[2] == "n/d":
        m = _RE_CODE.search(s)
        rec.error_code = int(m.group(1)) if m else -1
        return rec
    for tok in f[3:]:
        if tok.startswith("0x") and ":" in tok:
            can_id, frames = tok.split(":")
            rec.ids.append((int(can_id, 16), int(frames)))
    m = _RE_UNTRACKED.search(s)
    if m:
        rec.untracked_frames = int(m.group(1))
    return rec


def _parse_header(s: str) -> Record:
    m = _RE_WINDOW.search(s)
    if m:
        return HeaderWindow(float(m.group(1)))
    m = _RE_CHANNEL.match(s)
    if m:
        return HeaderChannel(
            channel=m.group(1), global_id=int(m.group(2), 16),
            controller=m.group(3), controller_id=int(m.group(4), 16),
            responder=m.group(5), responder_id=int(m.group(6), 16),
            timeout_us=int(m.group(7)) if m.group(7) else None,
        )
    return None


def _parse_event(s: str) -> HaloEvent:
    head = s.split(None, 2)
    if len(head) < 3:
        raise ValueError("short line")
    if head[2].startswith("0x"):
        # current format: time channel src dst lane type tag lat esito payload
        f = s.split(None, 9)
        payload = f[9] if len(f) > 9 else "-"
        return HaloEvent(
            t=float(f[0]), channel=f[1],
            src=int(f[2], 16), dst=int(f[3], 16),
            lane=int(f[4].lstrip("L")), type=f[5], tag=_opt_int(f[6]),
            lat_us=_opt_int(f[7]),
            outcome=None if f[8] == "-" else f[8],
            payload=None if payload == "-" else payload,
        )
    # old format: time channel lane type tag payload
    f = s.split(None, 5)
    payload = f[5] if len(f) > 5 else "-"
    return HaloEvent(
        t=float(f[0]), channel=f[1],
        lane=int(f[2].lstrip("L")), type=f[3], tag=_opt_int(f[4]),
        payload=None if payload == "-" else payload,
    )
