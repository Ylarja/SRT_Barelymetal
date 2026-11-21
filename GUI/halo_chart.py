"""
Live chart of the REQ -> RSP latency per channel, against the timeout of the listener.

One point per response (the lat column of the listener, as received by HaloEngine).
  scale "µs"      : zoom on the latencies (jitter in microseconds); points outside the
                    range (GRIGIA / TARDIVA) are drawn as triangles on the top edge
  scale "soglia"  : 0 .. T + 3 ms, with the timeout T (lane0_rx_timeout) and the grey zone
                    T ± 2 ms of the listener: shows the margin from the timeout
  span "60 s"     : last 60 s, scrolling        span "tutto": the whole measurement
Interruptions (no response for more than GAP_FACTOR x the nominal period) are shaded.
Drawn with QPainter, no extra libraries.
"""

from __future__ import annotations

import math
from collections import deque
from typing import Deque, Dict, List, Optional, Tuple

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QPainter, QPainterPath, QPen
from PySide6.QtWidgets import QButtonGroup, QFrame, QHBoxLayout, QPushButton, QVBoxLayout, QWidget

import halo_config as cfg
from halo_engine import HaloEngine
from halo_log import HaloEvent
from ui import BORDER, BORDER2, DRIFT, ERR, INFO, MUTED, OK, SOFT, TEXT, label, mono, qc

SERIES_COLORS = [OK, DRIFT, INFO, "#7EE0A1"]
GRAY_ZONE_US = 2000          # TIMEOUT_DELTA_US of Listener_ZCU104.cpp
SPAN_S = 60.0
MAX_POINTS = 50_000          # per channel (~80 min at 10 Hz)


def nice_step(span: float, ticks: int = 5) -> float:
    """Round step (1, 2, 5 x 10^k) giving about `ticks` divisions."""
    if span <= 0:
        return 1.0
    raw = span / ticks
    k = 10 ** math.floor(math.log10(raw))
    for m in (1, 2, 5, 10):
        if raw <= m * k:
            return m * k
    return 10 * k


class _Canvas(QWidget):
    ML, MR, MT, MB = 70, 18, 12, 26     # plot margins

    def __init__(self, chart: "LatencyChart"):
        super().__init__()
        self.chart = chart
        self.setMinimumHeight(150)

    def paintEvent(self, _):
        c = self.chart
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        plot = QRectF(self.ML, self.MT, w - self.ML - self.MR, h - self.MT - self.MB)

        now = c.engine.now_s
        if c.span_all:
            t_first = min((d[0][0] for d in c.data.values() if d), default=now)
            x0, x1 = t_first, max(now, t_first + 1.0)
        else:
            x0 = max(0.0, now - SPAN_S)          # at start-up the axis begins at 0
            x1 = x0 + SPAN_S
        timeout = c.timeout_us()

        visible = [(ch, t, lat, out) for ch, d in c.data.items() for (t, lat, out) in d if x0 <= t <= x1]
        y0, y1 = self._y_range(visible, timeout)

        def X(t):
            return plot.left() + (t - x0) / (x1 - x0) * plot.width()

        def Y(v):
            return plot.bottom() - (v - y0) / (y1 - y0) * plot.height()

        # frame and grid
        p.setPen(QPen(qc(BORDER), 1))
        p.setBrush(Qt.NoBrush)
        p.drawRect(plot)
        p.setFont(mono(10))
        ystep = nice_step(y1 - y0, 4)
        v = math.ceil(y0 / ystep) * ystep
        while v <= y1 + 1e-9:
            yy = Y(v)
            p.setPen(QPen(qc(BORDER), 1, Qt.DotLine))
            p.drawLine(QPointF(plot.left(), yy), QPointF(plot.right(), yy))
            p.setPen(qc(MUTED))
            txt = f"{v / 1000:g} ms" if c.scale_threshold else f"{v:.0f} µs"
            p.drawText(QRectF(0, yy - 8, self.ML - 8, 16), Qt.AlignRight | Qt.AlignVCenter, txt)
            v += ystep
        xstep = nice_step(x1 - x0, 6)
        t = math.ceil(x0 / xstep) * xstep
        while t <= x1 + 1e-9:
            xx = X(t)
            p.setPen(QPen(qc(BORDER), 1, Qt.DotLine))
            p.drawLine(QPointF(xx, plot.top()), QPointF(xx, plot.bottom()))
            p.setPen(qc(MUTED))
            p.drawText(QRectF(xx - 30, plot.bottom() + 4, 60, 16), Qt.AlignCenter, f"{t:g} s")
            t += xstep

        p.save()
        p.setClipRect(plot)
        # interruptions
        for g0, g1 in c.gaps:
            if g1 < x0 or g0 > x1:
                continue
            r = QRectF(QPointF(X(max(g0, x0)), plot.top()), QPointF(X(min(g1, x1)), plot.bottom()))
            p.fillRect(r, qc(BORDER2, 120))
            if r.width() > 50:
                p.setPen(qc(MUTED))
                p.drawText(r.adjusted(2, 2, -2, -2), Qt.AlignTop | Qt.AlignHCenter, "interruzione")
        # timeout and grey zone
        if c.scale_threshold and timeout:
            band = QRectF(QPointF(plot.left(), Y(timeout + GRAY_ZONE_US)),
                          QPointF(plot.right(), Y(timeout - GRAY_ZONE_US)))
            p.fillRect(band, qc(DRIFT, 30))
            pen = QPen(qc(ERR), 1.5, Qt.DashLine)
            p.setPen(pen)
            p.drawLine(QPointF(plot.left(), Y(timeout)), QPointF(plot.right(), Y(timeout)))
            p.setPen(qc(ERR))
            p.drawText(QPointF(plot.left() + 6, Y(timeout) - 5), f"timeout T = {timeout / 1000:g} ms")
            p.setPen(qc(DRIFT))
            p.drawText(QPointF(plot.left() + 6, Y(timeout - GRAY_ZONE_US) - 4), "zona grigia T ± 2 ms")
        # points
        for ch, t, lat, out in visible:
            col = c.color(ch)
            x = X(t)
            if lat > y1 or lat < y0:         # outside the zoom: triangle on the top / bottom edge
                up = lat > y1
                y = plot.top() + 6 if up else plot.bottom() - 6
                d = -5 if up else 5
                tri = QPainterPath()
                tri.moveTo(x, y + d)
                tri.lineTo(x - 5, y - d * 0.8)
                tri.lineTo(x + 5, y - d * 0.8)
                tri.closeSubpath()
                p.fillPath(tri, qc(ERR if out == "TARDIVA" else DRIFT if up else col))
                continue
            y = Y(lat)
            if out in ("GRIGIA", "TARDIVA"):
                p.setPen(QPen(qc(ERR if out == "TARDIVA" else DRIFT), 2))
                p.setBrush(Qt.NoBrush)
                p.drawEllipse(QPointF(x, y), 6, 6)
            p.setPen(Qt.NoPen)
            p.setBrush(qc(col, 210))
            p.drawEllipse(QPointF(x, y), 2.6, 2.6)
        p.restore()

        if not visible:
            p.setPen(qc(MUTED))
            p.setFont(mono(12))
            p.drawText(plot, Qt.AlignCenter, "in attesa di risposte…")

    def _y_range(self, visible, timeout) -> Tuple[float, float]:
        c = self.chart
        if c.scale_threshold:
            top = (timeout + 3000) if timeout else max((v[2] for v in visible), default=1000) * 1.3
            return 0.0, float(top)
        # detail: on-time responses only; the others become triangles on the top edge
        limit = (timeout - GRAY_ZONE_US) if timeout else float("inf")
        vals = sorted(v[2] for v in visible if v[2] <= limit)
        if not vals:
            return 0.0, 2000.0
        # 1st-99th percentile: one odd response does not flatten the others
        k = len(vals) // 100 if len(vals) >= 50 else 0
        lo, hi = vals[k], vals[-1 - k]
        pad = max(10.0, (hi - lo) * 0.15)
        if hi - lo < 40:
            mid = (hi + lo) / 2
            lo, hi = mid - 20, mid + 20
        return lo - pad, hi + pad


class LatencyChart(QFrame):
    def __init__(self, engine: HaloEngine):
        super().__init__()
        self.setObjectName("panel")
        self.engine = engine
        self.data: Dict[str, Deque[Tuple[float, float, Optional[str]]]] = {}
        self.last_t: Dict[str, float] = {}
        self.gaps: List[List[float]] = []
        self.scale_threshold = False
        self.span_all = False
        self._colors: Dict[str, str] = {}

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        head = QFrame()
        head.setObjectName("panelHead")
        hl = QHBoxLayout(head)
        hl.setContentsMargins(16, 8, 12, 8)
        hl.setSpacing(8)
        hl.addWidget(label("Latenza richiesta → risposta", "h2"))
        self.legend = label("", font=mono(11), color=MUTED)
        self.legend.setTextFormat(Qt.RichText)
        hl.addWidget(self.legend)
        hl.addStretch(1)
        self.scale_btns = self._toggle(hl, [("µs", False), ("soglia", True)], self._set_scale)
        hl.addSpacing(10)
        self.span_btns = self._toggle(hl, [("60 s", False), ("tutto", True)], self._set_span)
        lay.addWidget(head)
        self.canvas = _Canvas(self)
        lay.addWidget(self.canvas, 1)

    # ------------------------------------------------------------- building
    def _toggle(self, layout, options, callback) -> Dict[bool, QPushButton]:
        group = QButtonGroup(self)
        group.setExclusive(True)
        btns = {}
        for text, value in options:
            b = QPushButton(text)
            b.setObjectName("filter")
            b.setCheckable(True)
            b.setCursor(Qt.PointingHandCursor)
            b.setChecked(not value)
            b.clicked.connect(lambda _=False, v=value: callback(v))
            group.addButton(b)
            layout.addWidget(b)
            btns[value] = b
        self._style(btns, False)
        return btns

    @staticmethod
    def _style(btns: Dict[bool, QPushButton], selected: bool):
        for value, b in btns.items():
            on = value == selected
            b.setChecked(on)
            b.setStyleSheet(f"color: {TEXT if on else MUTED}; border-color: {OK if on else BORDER2};")

    def _set_scale(self, threshold: bool):
        self.scale_threshold = threshold
        self._style(self.scale_btns, threshold)
        self.canvas.update()

    def _set_span(self, all_: bool):
        self.span_all = all_
        self._style(self.span_btns, all_)
        self.canvas.update()

    # ------------------------------------------------------------- data
    def color(self, channel: str) -> str:
        if channel not in self._colors:
            self._colors[channel] = SERIES_COLORS[len(self._colors) % len(SERIES_COLORS)]
        return self._colors[channel]

    def timeout_us(self) -> Optional[int]:
        ts = [t.timeout_us for t in self.engine.timeouts.values() if t.timeout_us]
        return max(ts) if ts else None

    def add(self, ev: HaloEvent):
        """Called for every event accepted by HaloEngine; keeps the responses."""
        if ev.type != "RSP" or ev.lat_us is None:
            return
        d = self.data.get(ev.channel)
        if d is None:
            d = self.data[ev.channel] = deque(maxlen=MAX_POINTS)
            self.color(ev.channel)
        prev = self.last_t.get(ev.channel)
        if prev is not None and (ev.t - prev) * 1e6 > cfg.GAP_FACTOR * cfg.NOMINAL_PERIOD_US["RSP"]:
            self._add_gap(prev, ev.t)
        self.last_t[ev.channel] = ev.t
        d.append((ev.t, float(ev.lat_us), ev.outcome))

    def _add_gap(self, g0: float, g1: float):
        for g in self.gaps:                  # same interruption seen on another channel
            if g0 <= g[1] and g1 >= g[0]:
                g[0], g[1] = min(g[0], g0), max(g[1], g1)
                return
        self.gaps.append([g0, g1])

    def refresh(self):
        parts = []
        for ch, d in self.data.items():
            last = f"{d[-1][1]:.0f} µs" if d else "—"
            parts.append(f"<span style='color:{self.color(ch)}'>●</span> {ch} "
                         f"<span style='color:{SOFT}'>{last}</span>")
        t = self.timeout_us()
        parts.append(f"<span style='color:{ERR}'>- -</span> T {t / 1000:g} ms" if t else "timeout off")
        self.legend.setText("&nbsp;&nbsp;&nbsp;".join(parts))
        self.canvas.update()

