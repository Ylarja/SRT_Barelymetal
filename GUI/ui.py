"""PySide6 user interface for the CAN analyzer."""

from __future__ import annotations

import html
import math
import queue
import random
import time
from typing import Dict, List, Optional

from PySide6.QtCore import (
    QAbstractTableModel, QModelIndex, QPointF, QRectF, QSortFilterProxyModel, Qt, QTimer, Signal,
)
from PySide6.QtGui import (
    QBrush, QColor, QFont, QFontDatabase, QFontMetrics, QPainter, QPainterPath, QPen,
)
from PySide6.QtWidgets import (
    QAbstractItemView, QButtonGroup, QFrame, QGridLayout, QHBoxLayout, QHeaderView, QLabel,
    QLineEdit, QMainWindow, QPlainTextEdit, QPushButton, QScrollArea, QSizePolicy, QSplitter,
    QStyle, QStyledItemDelegate, QTableView, QToolTip, QVBoxLayout, QWidget,
)

from engine import Engine, Frame, IdStats, Node, hex_id

# ------------------------------------------------------------------ theme
BG = "#0A0E13"
PANEL = "#10151C"
CARD = "#0D1218"
TERM_BG = "#070A0E"
BORDER = "#1E2631"
BORDER2 = "#2A3441"
TEXT = "#E6EDF3"
SOFT = "#C3CDD8"
MUTED = "#8B98A8"
DIM = "#7D8A99"
OK = "#4CC2FF"
DRIFT = "#B9A4FF"
ERR = "#FF6B78"
INFO = "#6FAFCC"
STATUS_COLOR = {"OK": OK, "DRIFT": DRIFT, "TIMEOUT": ERR, "LEARN": MUTED}
LEVELS = ["ALL", "ERR", "WARN", "UNK", "INFO"]


def qc(hex_color: str, alpha: Optional[int] = None) -> QColor:
    c = QColor(hex_color)
    if alpha is not None:
        c.setAlpha(alpha)
    return c


def rgba(hex_color: str, alpha: float) -> str:
    c = QColor(hex_color)
    return f"rgba({c.red()},{c.green()},{c.blue()},{alpha:.3f})"


def pick_mono() -> str:
    fams = set(QFontDatabase.families())
    for f in ["JetBrains Mono", "Cascadia Mono", "Fira Code", "SF Mono", "Menlo", "Consolas",
              "DejaVu Sans Mono", "Liberation Mono", "Ubuntu Mono"]:
        if f in fams:
            return f
    return QFontDatabase.systemFont(QFontDatabase.FixedFont).family()


MONO = "monospace"


def mono(px: int, bold: bool = False) -> QFont:
    f = QFont(MONO)
    f.setPixelSize(px)
    f.setStyleHint(QFont.Monospace)
    f.setBold(bold)
    return f


def stylesheet(accent: str) -> str:
    return f"""
    QMainWindow, QWidget#root {{ background: {BG}; }}
    QWidget {{ color: {TEXT}; font-size: 13px; }}
    QFrame#panel {{ background: {PANEL}; border: 1px solid {BORDER}; border-radius: 12px; }}
    QFrame#unkPanel {{ background: {PANEL}; border: 1px solid {rgba(accent, .35)}; border-radius: 12px; }}
    QFrame#tile {{ background: {PANEL}; border: 1px solid {BORDER}; border-radius: 10px; }}
    QFrame#term {{ background: {TERM_BG}; border: 1px solid {BORDER}; border-radius: 12px; }}
    QFrame#termBar {{ background: #0D1218; border: none; border-bottom: 1px solid {BORDER};
                      border-top-left-radius: 12px; border-top-right-radius: 12px; }}
    QFrame#panelHead {{ border: none; border-bottom: 1px solid {BORDER}; background: transparent; }}
    QLabel {{ background: transparent; border: none; }}
    QLabel#tileTitle {{ color: {MUTED}; font-size: 11px; letter-spacing: 1px; }}
    QLabel#h2 {{ font-size: 15px; font-weight: 600; }}
    QLabel#muted {{ color: {MUTED}; }}
    QPushButton {{ min-height: 34px; padding: 0 14px; border-radius: 8px; border: 1px solid {BORDER2};
                   background: {PANEL}; color: {TEXT}; font-weight: 600; }}
    QPushButton:hover {{ border-color: {MUTED}; }}
    QPushButton:focus {{ outline: none; border-color: {OK}; }}
    QPushButton#primary {{ background: {OK}; border-color: {OK}; color: #071018; }}
    QPushButton#primary:hover {{ background: #7ED3FF; }}
    QPushButton#filter {{ min-height: 26px; padding: 0 8px; border-radius: 5px; font-size: 11px;
                          font-family: '{MONO}'; background: transparent; }}
    QPushButton#ack {{ min-height: 30px; border-color: {accent}; background: {accent}; color: #0A0E13; }}
    QPushButton#ack[acked="true"] {{ background: transparent; color: {accent}; }}
    QLineEdit {{ background: {CARD}; border: 1px solid {BORDER2}; border-radius: 7px; padding: 5px 9px;
                 color: {TEXT}; selection-background-color: {OK}; }}
    QLineEdit:focus {{ border-color: {OK}; }}
    QLineEdit#prompt {{ background: transparent; border: none; padding: 0; font-family: '{MONO}';
                        font-size: 12px; color: {SOFT}; }}
    QTableView {{ background: {PANEL}; border: none; gridline-color: transparent;
                  selection-background-color: {rgba(OK, .14)}; selection-color: {TEXT};
                  font-family: '{MONO}'; font-size: 12px; outline: 0; }}
    QHeaderView::section {{ background: {PANEL}; color: {MUTED}; border: none;
                            border-bottom: 1px solid {BORDER}; padding: 6px 8px; font-size: 11px;
                            font-family: '{MONO}'; }}
    QPlainTextEdit {{ background: {TERM_BG}; border: none; color: {SOFT}; font-family: '{MONO}';
                      font-size: 12px; selection-background-color: {rgba(OK, .3)}; }}
    QScrollArea {{ background: transparent; border: none; }}
    QScrollArea > QWidget > QWidget {{ background: transparent; }}
    QScrollBar:vertical {{ background: transparent; width: 10px; margin: 2px; }}
    QScrollBar::handle:vertical {{ background: {BORDER2}; border-radius: 4px; min-height: 30px; }}
    QScrollBar:horizontal {{ background: transparent; height: 10px; margin: 2px; }}
    QScrollBar::handle:horizontal {{ background: {BORDER2}; border-radius: 4px; min-width: 30px; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
    QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}
    QSplitter::handle {{ background: transparent; }}
    QToolTip {{ background: {CARD}; color: {TEXT}; border: 1px solid {BORDER2}; padding: 6px;
                font-family: '{MONO}'; font-size: 12px; }}
    """


# ------------------------------------------------------------------ small widgets
class Spark(QWidget):
    """Tiny line chart of a list of floats."""

    def __init__(self, color: str = OK, parent=None):
        super().__init__(parent)
        self.values: List[float] = []
        self.color = color
        self.setFixedHeight(22)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def set_values(self, values):
        self.values = list(values)
        self.update()

    def paintEvent(self, _):
        if len(self.values) < 2:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()
        hi = max(max(self.values), 1e-6)
        n = len(self.values)
        path = QPainterPath()
        for i, v in enumerate(self.values):
            x = i * (w - 1) / (n - 1)
            y = h - 2 - (v / hi) * (h - 4)
            path.lineTo(x, y) if i else path.moveTo(x, y)
        fill = QPainterPath(path)
        fill.lineTo(w - 1, h)
        fill.lineTo(0, h)
        p.fillPath(fill, qc(self.color, 40))
        p.setPen(QPen(qc(self.color), 1.5))
        p.drawPath(path)


class Logo(QWidget):
    """A small square with a waveform, drawn so it works without special fonts."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(40, 40)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.setPen(QPen(qc(BORDER), 1))
        p.setBrush(qc(PANEL))
        p.drawRoundedRect(QRectF(0.5, 0.5, 39, 39), 10, 10)
        pts = [(8, 20), (13, 20), (16, 12), (20, 28), (24, 15), (27, 20), (32, 20)]
        path = QPainterPath()
        path.moveTo(*pts[0])
        for x, y in pts[1:]:
            path.lineTo(x, y)
        pen = QPen(qc(OK), 2)
        pen.setCapStyle(Qt.RoundCap)
        pen.setJoinStyle(Qt.RoundJoin)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        p.drawPath(path)


class PulseDot(QWidget):
    """LED with an optional expanding ring, used for unrecognized nodes."""

    def __init__(self, color: str, parent=None):
        super().__init__(parent)
        self.color = color
        self.pulsing = True
        self.setFixedSize(22, 22)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        c = QPointF(11, 11)
        if self.pulsing:
            ph = (time.monotonic() % 1.4) / 1.4
            p.setPen(Qt.NoPen)
            p.setBrush(qc(self.color, int(150 * (1 - ph))))
            r = 4.5 + ph * 6
            p.drawEllipse(c, r, r)
        p.setBrush(qc(self.color))
        p.setPen(Qt.NoPen)
        p.drawEllipse(c, 4.5, 4.5)


def label(text="", obj=None, font: Optional[QFont] = None, color: Optional[str] = None) -> QLabel:
    lb = QLabel(text)
    if obj:
        lb.setObjectName(obj)
    if font:
        lb.setFont(font)
    if color:
        lb.setStyleSheet(f"color: {color};")
    return lb


class Tile(QFrame):
    def __init__(self, title: str):
        super().__init__()
        self.setObjectName("tile")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(16, 12, 16, 12)
        lay.setSpacing(4)
        lay.addWidget(label(title.upper(), "tileTitle"))
        self.value = label("—", font=mono(24, True))
        self.value.setTextFormat(Qt.RichText)
        lay.addWidget(self.value)
        self.sub = label("", font=mono(11), color=MUTED)
        self.sub.setTextFormat(Qt.RichText)
        lay.addWidget(self.sub)
        self.setMinimumWidth(150)


# ------------------------------------------------------------------ topology
class TopologyWidget(QWidget):
    ackRequested = Signal(str)

    CH = 66
    STUB = 26

    def __init__(self, engine: Engine, accent: str):
        super().__init__()
        self.engine = engine
        self.accent = accent
        self.setMouseTracking(True)
        self.setMinimumHeight(300)
        self._hits: List[tuple] = []
        self.anim = QTimer(self)
        self.anim.timeout.connect(self.update)
        self.anim.start(66)

    def _layout(self, nodes: List[Node]):
        known = sorted([n for n in nodes if n.known], key=lambda n: (not n.ids, n.key))
        unknown = sorted([n for n in nodes if not n.known], key=lambda n: n.first_seen)
        ordered = known + unknown
        top = ordered[0::2]
        bottom = ordered[1::2]
        avail = max(200, self.width() - 2 * 70)
        gap = 14
        cw = 166
        per_side = max(len(top), len(bottom), 1)
        while cw > 124 and per_side * (cw + gap) - gap > avail:
            cw -= 4
        cap = max(1, int((avail + gap) // (cw + gap)))
        return top, bottom, cw, gap, cap

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        nodes = self.engine.active_nodes()
        top, bottom, cw, gap, cap = self._layout(nodes)
        rows_top = math.ceil(len(top) / cap) if top else 0
        rows_bot = math.ceil(len(bottom) / cap) if bottom else 0
        need = 60 + (rows_top + rows_bot) * (self.CH + 12) + 2 * self.STUB
        if self.minimumHeight() != max(300, need):
            self.setMinimumHeight(max(300, need))

        w, h = self.width(), self.height()
        bus_y = h / 2 + (rows_top - rows_bot) * (self.CH + 12) / 2
        x0, x1 = 64, w - 64
        mono_now = time.monotonic()
        self._hits = []

        # terminators + bus lines
        p.setFont(mono(10))
        for tx in (16, w - 60):
            r = QRectF(tx, bus_y - 14, 44, 28)
            p.setPen(QPen(qc(BORDER2), 1))
            p.setBrush(qc(BG))
            p.drawRoundedRect(r, 5, 5)
            p.setPen(qc(MUTED))
            p.drawText(r, Qt.AlignCenter, "120Ω")
        p.setPen(QPen(qc(OK), 3))
        p.drawLine(QPointF(x0, bus_y - 4), QPointF(x1, bus_y - 4))
        p.setPen(QPen(qc("#2B5A74"), 3))
        p.drawLine(QPointF(x0, bus_y + 4), QPointF(x1, bus_y + 4))
        p.setPen(qc(DIM))
        p.setFont(mono(9))
        p.drawText(QPointF(x0 + 4, bus_y - 10), "CAN_H")
        p.drawText(QPointF(x0 + 4, bus_y + 18), "CAN_L")

        if not nodes:
            p.setPen(qc(MUTED))
            p.setFont(mono(12))
            p.drawText(QRectF(0, bus_y + 30, w, 30), Qt.AlignCenter, "waiting for traffic…")
            return

        for side, items in (("top", top), ("bottom", bottom)):
            for i, n in enumerate(items):
                row, col = divmod(i, cap)
                in_row = min(cap, len(items) - row * cap)
                total = in_row * cw + (in_row - 1) * gap
                x = w / 2 - total / 2 + col * (cw + gap)
                if side == "top":
                    y = bus_y - 4 - self.STUB - self.CH - row * (self.CH + 12)
                else:
                    y = bus_y + 4 + self.STUB + row * (self.CH + 12)
                self._draw_node(p, n, QRectF(x, y, cw, self.CH), side, bus_y, mono_now)

    def _draw_node(self, p: QPainter, n: Node, r: QRectF, side: str, bus_y: float, mono_now: float):
        acc = self.accent
        unk = not n.known
        silent = not n.ids
        active = (mono_now - n.last_mono) < 0.15 and random.random() < 0.8
        col = acc if unk else OK
        cx = r.center().x()

        # stub
        stub_col = qc(acc) if unk else (qc("#5FA8D3") if active else qc("#2E6B8C"))
        if silent:
            stub_col = qc(BORDER2)
        p.setPen(QPen(stub_col, 2))
        if side == "top":
            p.drawLine(QPointF(cx, r.bottom()), QPointF(cx, bus_y - 5))
            jy = bus_y - 4
        else:
            p.drawLine(QPointF(cx, r.top()), QPointF(cx, bus_y + 5))
            jy = bus_y + 4
        p.setPen(Qt.NoPen)
        p.setBrush(stub_col)
        p.drawEllipse(QPointF(cx, jy), 3, 3)

        # halo for unacknowledged unknown nodes
        if unk and not n.acknowledged:
            ph = (mono_now % 2.0) / 2.0
            grow = ph * 8
            p.setPen(QPen(qc(acc, int(170 * (1 - ph))), 2))
            p.setBrush(Qt.NoBrush)
            p.drawRoundedRect(r.adjusted(-grow, -grow, grow, grow), 9 + grow, 9 + grow)

        # card
        p.setBrush(qc(acc, 26) if unk else qc(CARD))
        pen = QPen(qc(acc) if unk else qc(BORDER2 if silent else "#2A3B4C"), 1.2)
        if unk:
            pen.setStyle(Qt.DashLine)
        p.setPen(pen)
        p.drawRoundedRect(r, 9, 9)
        self._hits.append((r, n))

        alpha = 110 if silent else 255
        # LED
        led_c = QPointF(r.left() + 15, r.top() + 17)
        p.setPen(Qt.NoPen)
        if active:
            p.setBrush(qc(col, 70))
            p.drawEllipse(led_c, 8, 8)
        p.setBrush(qc(col) if active else qc(col, 60))
        p.drawEllipse(led_c, 4.5, 4.5)

        # key
        p.setFont(mono(13, True))
        tag_w = 44 if unk else 0
        fm = QFontMetrics(p.font())
        key = fm.elidedText(n.key, Qt.ElideRight, int(r.width() - 30 - tag_w - 6))
        p.setPen(qc(acc if unk else TEXT, alpha))
        p.drawText(QPointF(r.left() + 27, r.top() + 22), key)
        if unk:
            tr = QRectF(r.right() - tag_w - 8, r.top() + 10, tag_w, 15)
            p.setBrush(qc(acc))
            p.setPen(Qt.NoPen)
            p.drawRoundedRect(tr, 3, 3)
            p.setPen(qc(BG))
            p.setFont(mono(9, True))
            p.drawText(tr, Qt.AlignCenter, "UNREC")

        # second line
        f2 = QFont(self.font())
        f2.setPixelSize(11)
        p.setFont(f2)
        if unk:
            line2 = " ".join(hex_id(i) for i in n.ids[:3]) + (" …" if len(n.ids) > 3 else "") + " · not in DBC"
        else:
            line2 = n.title if n.title != n.key else ("silent — in DBC" if silent else "in DBC")
            if silent:
                line2 = "silent — no frames"
        fm2 = QFontMetrics(f2)
        p.setPen(qc(SOFT, alpha))
        p.drawText(QPointF(r.left() + 12, r.top() + 41), fm2.elidedText(line2, Qt.ElideRight, int(r.width() - 22)))

        # third line
        p.setFont(mono(11))
        p.setPen(qc(MUTED, alpha))
        fps = n.fps(mono_now)
        line3 = f"{len(n.ids)} ID{'s' if len(n.ids) != 1 else ''} · {fps:.0f} fps"
        p.drawText(QPointF(r.left() + 12, r.top() + 57), line3)

    def mouseMoveEvent(self, e):
        pos = e.position()
        for r, n in self._hits:
            if r.contains(pos):
                ids = "\n".join(
                    f"  {hex_id(s.can_id, s.extended)}  {s.name:<16} {s.count} frames"
                    for s in self.engine.ids.values() if s.node == n.key
                ) or "  (no frames yet)"
                head = f"{n.key} — {'UNRECOGNIZED' if not n.known else 'known'}"
                extra = "\n\nclick to acknowledge" if not n.known and not n.acknowledged else ""
                QToolTip.showText(e.globalPosition().toPoint(), f"{head}\n{ids}{extra}", self)
                self.setCursor(Qt.PointingHandCursor if not n.known else Qt.ArrowCursor)
                return
        self.setCursor(Qt.ArrowCursor)
        QToolTip.hideText()

    def mousePressEvent(self, e):
        for r, n in self._hits:
            if r.contains(e.position()) and not n.known:
                self.ackRequested.emit(n.key)
                return


# ------------------------------------------------------------------ unknown panel
class UnknownCard(QFrame):
    def __init__(self, node: Node, accent: str, on_ack):
        super().__init__()
        self.key = node.key
        self.accent = accent
        self.setStyleSheet(
            f"UnknownCard {{ border: 1px dashed {accent}; border-radius: 10px; background: {rgba(accent, .08)}; }}"
        )
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12, 10, 12, 12)
        lay.setSpacing(8)
        head = QHBoxLayout()
        head.setSpacing(6)
        self.dot = PulseDot(accent)
        head.addWidget(self.dot)
        head.addWidget(label(node.key, font=mono(13, True), color=accent))
        head.addStretch(1)
        self.ids = label("", font=mono(11))
        self.ids.setStyleSheet(f"background: {BG}; border: 1px solid {BORDER2}; border-radius: 4px; padding: 2px 6px;")
        head.addWidget(self.ids)
        lay.addLayout(head)

        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(1)
        self.vals = {}
        for c, name in enumerate(["FIRST SEEN", "FRAMES", "PERIOD"]):
            grid.addWidget(label(name, font=mono(9), color=MUTED), 0, c)
            v = label("—", font=mono(12))
            grid.addWidget(v, 1, c)
            self.vals[name] = v
        lay.addLayout(grid)

        self.payload = label("", font=mono(11), color=SOFT)
        self.payload.setStyleSheet(f"background: {BG}; border-radius: 6px; padding: 6px 8px; color: {SOFT};")
        self.payload.setTextInteractionFlags(Qt.TextSelectableByMouse)
        lay.addWidget(self.payload)

        row = QHBoxLayout()
        self.btn = QPushButton("Acknowledge")
        self.btn.setObjectName("ack")
        self.btn.setCursor(Qt.PointingHandCursor)
        self.btn.clicked.connect(lambda: on_ack(self.key))
        row.addWidget(self.btn)
        row.addStretch(1)
        lay.addLayout(row)

    def refresh(self, node: Node, engine: Engine):
        stats = [s for s in engine.ids.values() if s.node == node.key]
        self.ids.setText(" · ".join(hex_id(s.can_id, s.extended) for s in stats[:4]) + (" …" if len(stats) > 4 else ""))
        self.vals["FIRST SEEN"].setText(f"+{node.first_seen:.2f}s")
        self.vals["FRAMES"].setText(str(sum(s.count for s in stats)))
        meas = [s.measured_ms for s in stats if s.measured_ms]
        self.vals["PERIOD"].setText(f"~{min(meas):.0f} ms" if meas else "—")
        last = max(stats, key=lambda s: s.last_mono, default=None)
        if last:
            self.payload.setText(f"<span style='color:{MUTED}'>last</span> {hex_id(last.can_id, last.extended)} "
                                 f"[{last.dlc}] {last.last_data.hex(' ').upper()}")
        self.dot.pulsing = not node.acknowledged
        self.dot.update()
        acked = "true" if node.acknowledged else "false"
        if self.btn.property("acked") != acked:
            self.btn.setProperty("acked", acked)
            self.btn.setText("Acknowledged" if node.acknowledged else "Acknowledge")
            self.btn.style().unpolish(self.btn)
            self.btn.style().polish(self.btn)


class UnknownPanel(QFrame):
    def __init__(self, engine: Engine, accent: str, on_ack):
        super().__init__()
        self.setObjectName("unkPanel")
        self.engine = engine
        self.accent = accent
        self.on_ack = on_ack
        self.cards: Dict[str, UnknownCard] = {}
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        head = QFrame()
        head.setObjectName("panelHead")
        hl = QHBoxLayout(head)
        hl.setContentsMargins(16, 12, 16, 12)
        warn = label("▲", color=accent)
        warn.setFont(mono(13, True))
        hl.addWidget(warn)
        hl.addWidget(label("Unrecognized nodes", "h2"))
        hl.addStretch(1)
        self.badge = label("0", font=mono(12, True))
        self.badge.setAlignment(Qt.AlignCenter)
        self.badge.setMinimumWidth(28)
        self.badge.setStyleSheet(f"background: {accent}; color: {BG}; border-radius: 10px; padding: 2px 8px;")
        hl.addWidget(self.badge)
        lay.addWidget(head)

        sub = label("Traffic on IDs that aren't in the DBC (or the learned baseline), grouped by inferred source.",
                    "muted")
        sub.setWordWrap(True)
        sub.setContentsMargins(16, 10, 16, 4)
        sub.setStyleSheet(f"color: {MUTED}; font-size: 12px;")
        lay.addWidget(sub)

        self.scroll = QScrollArea()
        self.scroll.setWidgetResizable(True)
        self.scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        inner = QWidget()
        self.list = QVBoxLayout(inner)
        self.list.setContentsMargins(12, 8, 12, 12)
        self.list.setSpacing(10)
        self.empty = label("No unmapped traffic — listening…", font=mono(12), color=MUTED)
        self.empty.setAlignment(Qt.AlignCenter)
        self.empty.setMinimumHeight(120)
        self.empty.setStyleSheet(f"color: {MUTED}; border: 1px dashed {BORDER2}; border-radius: 10px;")
        self.list.addWidget(self.empty)
        self.list.addStretch(1)
        self.scroll.setWidget(inner)
        lay.addWidget(self.scroll, 1)
        self.setMinimumWidth(320)

    def refresh(self):
        nodes = self.engine.unknown_nodes()
        self.badge.setText(str(len(nodes)))
        self.empty.setVisible(not nodes)
        for n in nodes:
            card = self.cards.get(n.key)
            if card is None:
                card = UnknownCard(n, self.accent, self.on_ack)
                self.cards[n.key] = card
                self.list.insertWidget(self.list.count() - 1, card)
            card.refresh(n, self.engine)

    def tick_anim(self):
        for c in self.cards.values():
            if c.dot.pulsing:
                c.dot.update()


# ------------------------------------------------------------------ frequency table
COLS = ["ID", "Message", "TX node", "Expected", "Measured", "Rate", "Jitter", "Deviation", "Trend", "Frames", "Status"]
SORT_ROLE = Qt.UserRole + 1
RAW_ROLE = Qt.UserRole + 2


class FreqModel(QAbstractTableModel):
    def __init__(self, engine: Engine, accent: str):
        super().__init__()
        self.engine = engine
        self.accent = accent
        self.keys: List[tuple] = []

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.keys)

    def columnCount(self, parent=QModelIndex()):
        return len(COLS)

    def headerData(self, section, orient, role=Qt.DisplayRole):
        if orient == Qt.Horizontal and role == Qt.DisplayRole:
            return COLS[section].upper()
        if orient == Qt.Horizontal and role == Qt.TextAlignmentRole:
            return int(Qt.AlignLeft | Qt.AlignVCenter)
        return None

    def refresh(self):
        keys = sorted(self.engine.ids.keys())
        if keys != self.keys:
            self.beginResetModel()
            self.keys = keys
            self.endResetModel()
        elif keys:
            self.dataChanged.emit(self.index(0, 0), self.index(len(keys) - 1, len(COLS) - 1))

    @staticmethod
    def _ms(v):
        if v is None:
            return "—"
        return f"{v / 1000:.2f} s" if v >= 1000 else f"{v:.1f} ms"

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        st: IdStats = self.engine.ids[self.keys[index.row()]]
        c = index.column()
        meas = st.measured_ms
        if role == Qt.DisplayRole:
            if c == 0:
                return hex_id(st.can_id, st.extended)
            if c == 1:
                return st.name if st.known else "— not in DBC"
            if c == 2:
                return st.node
            if c == 3:
                if st.expected_ms:
                    return f"{st.expected_ms:.0f} ms"
                return f"~{st.learned_ms:.0f} ms" if st.learned_ms else "learning…"
            if c == 4:
                return "silent" if st.status == "TIMEOUT" else self._ms(meas)
            if c == 5:
                if st.status == "TIMEOUT" or not meas:
                    return "0 Hz"
                hz = 1000 / meas
                return f"{hz:.0f} Hz" if hz >= 10 else f"{hz:.2f} Hz"
            if c == 6:
                j = st.jitter_ms
                return "—" if j is None else f"±{j:.2f}"
            if c == 9:
                return f"{st.count:,}"
            return ""
        if role == RAW_ROLE:
            return st
        if role == SORT_ROLE:
            dev = st.deviation_pct()
            return [st.can_id, st.name, st.node, st.ref_ms or 0, meas or 0, (1000 / meas) if meas else 0,
                    st.jitter_ms or 0, abs(dev) if dev is not None else 0, 0, st.count,
                    ["OK", "LEARN", "DRIFT", "TIMEOUT"].index(st.status)][c]
        if role == Qt.ForegroundRole:
            if not st.known and c in (0, 2):
                return qc(self.accent)
            if c == 1 and not st.known:
                return qc(MUTED)
            if c in (3, 6):
                return qc(MUTED)
            if c == 0:
                return qc(TEXT)
            return qc(SOFT)
        if role == Qt.BackgroundRole:
            return qc(self.accent, 22) if not st.known else None
        if role == Qt.FontRole and c == 0:
            return mono(12, True)
        if role == Qt.ToolTipRole:
            ref = "from DBC" if st.expected_ms else ("learned from traffic" if st.learned_ms else "learning")
            return (f"{hex_id(st.can_id, st.extended)} {st.name}\nexpected period: {ref}\n"
                    f"last data: {st.last_data.hex(' ').upper()}")
        return None


class FreqProxy(QSortFilterProxyModel):
    def __init__(self):
        super().__init__()
        self.text = ""
        self.only_problems = False
        self.setSortRole(SORT_ROLE)

    def set_text(self, t):
        self.text = t.strip().lower()
        self.invalidateFilter()

    def set_problems(self, v):
        self.only_problems = v
        self.invalidateFilter()

    def filterAcceptsRow(self, row, parent):
        m = self.sourceModel()
        st: IdStats = m.data(m.index(row, 0, parent), RAW_ROLE)
        if self.only_problems and st.known and st.status in ("OK", "LEARN"):
            return False
        if not self.text:
            return True
        hay = f"{hex_id(st.can_id, st.extended)} {st.name} {st.node}".lower()
        return all(tok in hay for tok in self.text.split())


class CellDelegate(QStyledItemDelegate):
    """Paints row background/selection, then a custom cell body."""

    def __init__(self, accent, parent=None):
        super().__init__(parent)
        self.accent = accent

    def _bg(self, p, opt, index):
        bg = index.data(Qt.BackgroundRole)
        if bg is not None:
            p.fillRect(opt.rect, bg)
        if opt.state & QStyle.State_Selected:
            p.fillRect(opt.rect, qc(OK, 36))


class DeviationDelegate(CellDelegate):
    def paint(self, p, opt, index):
        self._bg(p, opt, index)
        st: IdStats = index.data(RAW_ROLE)
        dev = st.deviation_pct()
        r = opt.rect
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        bar = QRectF(r.left() + 8, r.center().y() - 3, 92, 6)
        p.setPen(Qt.NoPen)
        p.setBrush(qc(BORDER))
        p.drawRoundedRect(bar, 3, 3)
        # tolerance band (scale: ±30 %)
        tol = 8.0
        band_w = bar.width() * (tol / 30) / 2
        p.setBrush(qc(OK, 40))
        p.drawRect(QRectF(bar.center().x() - band_w, bar.top(), band_w * 2, bar.height()))
        p.setPen(QPen(qc(MUTED), 1))
        p.drawLine(QPointF(bar.center().x(), bar.top() - 3), QPointF(bar.center().x(), bar.bottom() + 3))
        col = STATUS_COLOR.get(st.status, MUTED)
        if st.status == "TIMEOUT":
            dev = None
            p.setPen(Qt.NoPen)
            p.setBrush(qc(col))
            p.drawEllipse(QPointF(bar.right(), bar.center().y()), 5, 5)
            txt = "silent"
        if dev is not None:
            d = max(-30.0, min(30.0, dev))
            x = bar.center().x() + d / 30 * bar.width() / 2
            p.setPen(Qt.NoPen)
            p.setBrush(qc(col))
            p.drawEllipse(QPointF(x, bar.center().y()), 5, 5)
            txt = f"{dev:+.1f}%" if abs(dev) < 1000 else ">+999%"
        elif st.status != "TIMEOUT":
            txt = "—"
        p.setFont(mono(11))
        p.setPen(qc(MUTED if st.status in ("OK", "LEARN") else col))
        p.drawText(QRectF(bar.right() + 8, r.top(), r.right() - bar.right() - 8, r.height()),
                   Qt.AlignVCenter | Qt.AlignLeft, txt)
        p.restore()


class SparkDelegate(CellDelegate):
    def paint(self, p, opt, index):
        self._bg(p, opt, index)
        st: IdStats = index.data(RAW_ROLE)
        vals = list(st.trend)
        if len(vals) < 2:
            return
        r = QRectF(opt.rect).adjusted(8, 6, -8, -6)
        lo, hi = min(vals), max(vals)
        ref = st.ref_ms or (lo + hi) / 2
        span = max(hi - lo, ref * 0.25)
        mid = (hi + lo) / 2
        lo, hi = mid - span / 2, mid + span / 2
        path = QPainterPath()
        n = len(vals)
        for i, v in enumerate(vals):
            x = r.left() + i * r.width() / (n - 1)
            y = r.bottom() - (min(max(v, lo), hi) - lo) / (hi - lo) * r.height()
            path.lineTo(x, y) if i else path.moveTo(x, y)
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        col = self.accent if not st.known else STATUS_COLOR.get(st.status, MUTED)
        p.setPen(QPen(qc(col), 1.4))
        p.drawPath(path)
        p.restore()


class StatusDelegate(CellDelegate):
    def paint(self, p, opt, index):
        self._bg(p, opt, index)
        st: IdStats = index.data(RAW_ROLE)
        col = STATUS_COLOR.get(st.status, MUTED)
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        p.setFont(mono(10, True))
        fm = QFontMetrics(p.font())
        w = fm.horizontalAdvance(st.status) + 14
        chip = QRectF(opt.rect.left() + 8, opt.rect.center().y() - 9, w, 18)
        p.setPen(QPen(qc(col), 1))
        p.setBrush(qc(col, 30))
        p.drawRoundedRect(chip, 4, 4)
        p.setPen(qc(col))
        p.drawText(chip, Qt.AlignCenter, st.status)
        p.restore()


class FrequencyPanel(QFrame):
    def __init__(self, engine: Engine, accent: str):
        super().__init__()
        self.setObjectName("panel")
        self.engine = engine
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        head = QFrame()
        head.setObjectName("panelHead")
        hl = QHBoxLayout(head)
        hl.setContentsMargins(16, 10, 12, 10)
        hl.setSpacing(10)
        hl.addWidget(label("Task frequency", "h2"))
        hl.addWidget(label("measured vs. DBC cycle time", color=MUTED))
        hl.addStretch(1)
        self.counts = label("", font=mono(11))
        self.counts.setTextFormat(Qt.RichText)
        hl.addWidget(self.counts)
        self.search = QLineEdit()
        self.search.setPlaceholderText("filter id / name / node")
        self.search.setFixedWidth(190)
        hl.addWidget(self.search)
        self.problems = QPushButton("Problems only")
        self.problems.setCheckable(True)
        self.problems.setObjectName("filter")
        self.problems.setMinimumHeight(30)
        hl.addWidget(self.problems)
        lay.addWidget(head)

        self.model = FreqModel(engine, accent)
        self.proxy = FreqProxy()
        self.proxy.setSourceModel(self.model)
        self.search.textChanged.connect(self.proxy.set_text)
        self.problems.toggled.connect(self.proxy.set_problems)
        self.problems.toggled.connect(lambda v: self.problems.setStyleSheet(
            f"color: {DRIFT}; border-color: {DRIFT};" if v else ""))

        t = QTableView()
        t.setModel(self.proxy)
        t.setSortingEnabled(True)
        t.sortByColumn(0, Qt.AscendingOrder)
        t.verticalHeader().hide()
        t.verticalHeader().setDefaultSectionSize(30)
        t.setShowGrid(False)
        t.setWordWrap(False)
        t.setTextElideMode(Qt.ElideRight)
        t.setSelectionBehavior(QAbstractItemView.SelectRows)
        t.setEditTriggers(QAbstractItemView.NoEditTriggers)
        t.setHorizontalScrollMode(QAbstractItemView.ScrollPerPixel)
        t.setVerticalScrollMode(QAbstractItemView.ScrollPerPixel)
        hh = t.horizontalHeader()
        widths = [72, 140, 92, 80, 84, 66, 62, 146, 80, 66, 86]
        for i, wd in enumerate(widths):
            hh.resizeSection(i, wd)
        hh.setStretchLastSection(True)
        hh.setHighlightSections(False)
        t.setItemDelegateForColumn(7, DeviationDelegate(accent, t))
        t.setItemDelegateForColumn(8, SparkDelegate(accent, t))
        t.setItemDelegateForColumn(10, StatusDelegate(accent, t))
        self.table = t
        lay.addWidget(t, 1)

    def refresh(self):
        self.model.refresh()
        sts = list(self.engine.ids.values())
        c = {k: sum(1 for s in sts if s.status == k) for k in ("OK", "DRIFT", "TIMEOUT", "LEARN")}
        learn = f"&nbsp;&nbsp;<span style='color:{MUTED}'>{c['LEARN']} LEARN</span>" if c["LEARN"] else ""
        self.counts.setText(
            f"<span style='color:{MUTED}'>tol ±{self.engine.tol:g}%</span>&nbsp;&nbsp;"
            f"<span style='color:{OK}'>{c['OK']} OK</span>&nbsp;&nbsp;"
            f"<span style='color:{DRIFT}'>{c['DRIFT']} DRIFT</span>&nbsp;&nbsp;"
            f"<span style='color:{ERR}'>{c['TIMEOUT']} TIMEOUT</span>{learn}"
        )


# ------------------------------------------------------------------ terminal
class Terminal(QFrame):
    command = Signal(str)

    def __init__(self, engine: Engine, accent: str, host: str = "can0"):
        super().__init__()
        self.setObjectName("term")
        self.engine = engine
        self.accent = accent
        self.filter = "ALL"
        self.pending: list = []
        self.level_color = {"ERR": ERR, "WARN": DRIFT, "UNK": accent, "INFO": INFO}
        self.text_color = {"ERR": "#F2C6CB", "WARN": "#DCD2FF", "UNK": "#F5E2BD", "INFO": SOFT}

        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        bar = QFrame()
        bar.setObjectName("termBar")
        bl = QHBoxLayout(bar)
        bl.setContentsMargins(14, 8, 10, 8)
        bl.setSpacing(6)
        for _ in range(3):
            d = QLabel()
            d.setFixedSize(10, 10)
            d.setStyleSheet(f"background: {BORDER2}; border-radius: 5px;")
            bl.addWidget(d)
        bl.addSpacing(6)
        bl.addWidget(label("errors — candump -e", font=mono(12), color=SOFT))
        bl.addStretch(1)
        self.group = QButtonGroup(self)
        self.group.setExclusive(True)
        self.buttons = {}
        for lvl in LEVELS:
            b = QPushButton(lvl)
            b.setObjectName("filter")
            b.setCheckable(True)
            b.setCursor(Qt.PointingHandCursor)
            b.setChecked(lvl == "ALL")
            b.clicked.connect(lambda _=False, l=lvl: self.set_filter(l))
            self.group.addButton(b)
            self.buttons[lvl] = b
            bl.addWidget(b)
        lay.addWidget(bar)

        self.view = QPlainTextEdit()
        self.view.setReadOnly(True)
        self.view.setMaximumBlockCount(4000)
        self.view.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.view.document().setDocumentMargin(10)
        self.view.setFont(mono(12))
        lay.addWidget(self.view, 1)

        prow = QHBoxLayout()
        prow.setContentsMargins(14, 6, 14, 10)
        prow.setSpacing(6)
        prow.addWidget(label(f"{host}@analyzer", font=mono(12), color=OK))
        prow.addWidget(label(":~$", font=mono(12), color=MUTED))
        self.prompt = QLineEdit()
        self.prompt.setObjectName("prompt")
        self.prompt.setPlaceholderText("type 'help'")
        self.prompt.returnPressed.connect(self._enter)
        prow.addWidget(self.prompt, 1)
        lay.addLayout(prow)
        self._style_buttons()

    def _style_buttons(self):
        counts = {l: 0 for l in LEVELS}
        for ln in self.engine.log:
            counts[ln.level] = counts.get(ln.level, 0) + 1
        counts["ALL"] = len(self.engine.log)
        for lvl, b in self.buttons.items():
            c = TEXT if lvl == "ALL" else self.level_color[lvl]
            on = lvl == self.filter
            b.setText(f"{lvl} {counts[lvl]}")
            b.setStyleSheet(
                f"color: {c}; border-color: {c if on else BORDER2}; background: {rgba(c, .14) if on else 'transparent'};"
            )

    def _fmt(self, ln) -> str:
        nb = lambda t: html.escape(t).replace(" ", "&nbsp;")  # noqa: E731
        lc = self.level_color.get(ln.level, SOFT)
        tc = self.text_color.get(ln.level, SOFT)
        return (f"<span style='color:{DIM}'>{nb(f'[{ln.t:9.3f}]')}</span>&nbsp;"
                f"<span style='color:{lc};font-weight:700'>{nb(f'{ln.level:<4}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{tc}'>{nb(ln.text)}</span>")

    def on_log(self, ln):
        self.pending.append(ln)

    def flush(self):
        if not self.pending:
            return
        sb = self.view.verticalScrollBar()
        follow = sb.value() >= sb.maximum() - 4
        for ln in self.pending:
            if self.filter == "ALL" or ln.level == self.filter:
                self.view.appendHtml(self._fmt(ln))
        self.pending.clear()
        if follow:
            sb.setValue(sb.maximum())
        self._style_buttons()

    def set_filter(self, lvl: str):
        self.filter = lvl
        self.pending.clear()
        self.view.clear()
        lines = [ln for ln in self.engine.log if lvl == "ALL" or ln.level == lvl][-3000:]
        self.view.appendHtml("<br>".join(self._fmt(ln) for ln in lines)) if lines else None
        self.view.verticalScrollBar().setValue(self.view.verticalScrollBar().maximum())
        self.buttons[lvl].setChecked(True)
        self._style_buttons()

    def clear(self):
        self.engine.log.clear()
        self.pending.clear()
        self.view.clear()
        self._style_buttons()

    def echo(self, text: str, color: str = SOFT):
        self.view.appendHtml(f"<span style='color:{color}'>{html.escape(text).replace(' ', '&nbsp;')}</span>")
        sb = self.view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _enter(self):
        cmd = self.prompt.text().strip()
        self.prompt.clear()
        if cmd:
            self.echo(f"$ {cmd}", OK)
            self.command.emit(cmd)


# ------------------------------------------------------------------ main window
class MainWindow(QMainWindow):
    def __init__(self, engine: Engine, source, frame_q: "queue.Queue", accent: str, simulated: bool,
                 channel_label: str):
        super().__init__()
        global MONO
        MONO = pick_mono()
        self.engine = engine
        self.source = source
        self.q = frame_q
        self.accent = accent
        self.running = True
        self.setWindowTitle("CAN Bus Analyzer")
        self.resize(1560, 980)
        self.setStyleSheet(stylesheet(accent))

        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(20, 16, 20, 18)
        outer.setSpacing(14)

        # --- top bar
        top = QHBoxLayout()
        top.setSpacing(12)
        top.addWidget(Logo())
        tl = QVBoxLayout()
        tl.setSpacing(0)
        title = label("CAN Bus Analyzer")
        title.setStyleSheet("font-size: 20px; font-weight: 700;")
        tl.addWidget(title)
        tl.addWidget(label(source.description, font=mono(11), color=MUTED))
        top.addLayout(tl)
        if simulated:
            chip = label("SIMULATED FEED", font=mono(10))
            chip.setStyleSheet(f"color: {MUTED}; border: 1px solid {BORDER2}; border-radius: 4px; padding: 3px 7px;")
            top.addWidget(chip)
        top.addStretch(1)
        self.pause_btn = QPushButton("❚❚  Pause capture")
        self.pause_btn.setObjectName("primary")
        self.pause_btn.setCursor(Qt.PointingHandCursor)
        self.pause_btn.clicked.connect(self.toggle_pause)
        reset = QPushButton("Reset stats")
        reset.clicked.connect(self.engine.reset_stats)
        clear = QPushButton("Clear log")
        clear.clicked.connect(lambda: self.term.clear())
        for b in (reset, clear):
            b.setCursor(Qt.PointingHandCursor)
        top.addWidget(self.pause_btn)
        top.addWidget(reset)
        top.addWidget(clear)
        outer.addLayout(top)

        # --- tiles
        tiles = QHBoxLayout()
        tiles.setSpacing(12)
        self.t_load = Tile("Bus load")
        self.load_spark = Spark(OK)
        self.t_load.layout().addWidget(self.load_spark)
        self.t_load.sub.hide()
        self.t_fps = Tile("Frames / s")
        self.t_nodes = Tile("Nodes")
        self.t_err = Tile("Error frames")
        self.t_cnt = Tile("REC / TEC")
        self.t_up = Tile("Uptime")
        for t in (self.t_load, self.t_fps, self.t_nodes, self.t_err, self.t_cnt, self.t_up):
            tiles.addWidget(t, 1)
        outer.addLayout(tiles)

        # --- middle: topology + unknown
        self.topo = TopologyWidget(engine, accent)
        self.topo.ackRequested.connect(self.toggle_ack)
        topo_panel = QFrame()
        topo_panel.setObjectName("panel")
        tpl = QVBoxLayout(topo_panel)
        tpl.setContentsMargins(0, 0, 0, 0)
        tpl.setSpacing(0)
        th = QFrame()
        th.setObjectName("panelHead")
        thl = QHBoxLayout(th)
        thl.setContentsMargins(16, 12, 16, 12)
        thl.addWidget(label("Bus topology", "h2"))
        thl.addWidget(label("live TX activity · hover a node for its IDs", color=MUTED))
        thl.addStretch(1)
        legend = label(
            f"<span style='color:{OK}'>■</span> <span style='color:{MUTED}'>in DBC</span>&nbsp;&nbsp;&nbsp;"
            f"<span style='color:{accent}'>▢</span> <span style='color:{MUTED}'>unrecognized</span>&nbsp;&nbsp;&nbsp;"
            f"<span style='color:{BORDER2}'>■</span> <span style='color:{MUTED}'>silent</span>")
        legend.setTextFormat(Qt.RichText)
        thl.addWidget(legend)
        tpl.addWidget(th)
        tpl.addWidget(self.topo, 1)

        self.unknown = UnknownPanel(engine, accent, self.toggle_ack)
        mid = QSplitter(Qt.Horizontal)
        mid.addWidget(topo_panel)
        mid.addWidget(self.unknown)
        mid.setStretchFactor(0, 3)
        mid.setStretchFactor(1, 1)
        mid.setSizes([1100, 400])
        mid.setHandleWidth(14)

        # --- bottom: frequency + terminal
        self.freq = FrequencyPanel(engine, accent)
        self.term = Terminal(engine, accent, channel_label)
        self.term.command.connect(self.run_command)
        bot = QSplitter(Qt.Horizontal)
        bot.addWidget(self.freq)
        bot.addWidget(self.term)
        bot.setStretchFactor(0, 3)
        bot.setStretchFactor(1, 2)
        bot.setSizes([1080, 460])
        bot.setHandleWidth(14)

        vs = QSplitter(Qt.Vertical)
        vs.addWidget(mid)
        vs.addWidget(bot)
        vs.setSizes([380, 480])
        vs.setHandleWidth(14)
        outer.addWidget(vs, 1)

        engine.on_log = self.term.on_log
        self.term.set_filter("ALL")

        # --- timers
        self.drain_timer = QTimer(self)
        self.drain_timer.timeout.connect(self.drain)
        self.drain_timer.start(50)
        self.ui_timer = QTimer(self)
        self.ui_timer.timeout.connect(self.refresh)
        self.ui_timer.start(250)
        self.anim_timer = QTimer(self)
        self.anim_timer.timeout.connect(self.unknown.tick_anim)
        self.anim_timer.start(50)

    # ------------------------------------------------------------- data flow
    def drain(self):
        n = 0
        try:
            while n < 50000:
                item = self.q.get_nowait()
                n += 1
                if isinstance(item, Frame):
                    if self.running:
                        self.engine.ingest(item)
                elif isinstance(item, tuple) and item and item[0] == "exception":
                    self.engine.bus_exception(item[1])
        except queue.Empty:
            pass
        self.term.flush()

    def refresh(self):
        if self.running:
            self.engine.tick()
        e = self.engine
        load = e.bus_load()
        self.t_load.value.setText(f"{load:.1f}<span style='font-size:13px;color:{MUTED}'> %</span>")
        self.load_spark.set_values(e.load_history)
        self.t_fps.value.setText(f"{e.fps():.0f}")
        self.t_fps.sub.setText(f"{e.total_frames:,} total · {len(e.ids)} IDs")
        known = sum(1 for n in e.active_nodes() if n.known and n.ids)
        silent = sum(1 for n in e.active_nodes() if n.known and not n.ids)
        unk = len(e.unknown_nodes())
        self.t_nodes.value.setText(
            f"{known}<span style='font-size:12px;color:{MUTED}'> known </span>"
            f"<span style='color:{self.accent}'>{unk}</span>"
            f"<span style='font-size:12px;color:{self.accent}'> unknown</span>")
        self.t_nodes.sub.setText(f"{silent} silent in DBC" if silent else "all DBC nodes talking")
        self.t_err.value.setText(f"<span style='color:{ERR}'>{e.error_frames}</span>")
        self.t_err.sub.setText(f"{len(e.error_times)} in last 60 s")
        if e.tec is None:
            self.t_cnt.value.setText(f"—<span style='color:{MUTED}'> / </span>—")
        else:
            self.t_cnt.value.setText(f"{e.rec}<span style='color:{MUTED}'> / </span>{e.tec}")
        sc = OK if e.ctrl_state == "ERROR ACTIVE" else (DRIFT if e.ctrl_state == "ERROR WARNING" else ERR)
        self.t_cnt.sub.setText(f"<span style='color:{sc};font-weight:700'>{e.ctrl_state}</span>")
        up = e.now()
        self.t_up.value.setText(f"{int(up // 3600):02d}:{int(up % 3600 // 60):02d}:{int(up % 60):02d}")
        rc = OK if self.running else DRIFT
        self.t_up.sub.setText(f"<span style='color:{rc}'>● {'capturing' if self.running else 'paused'}</span>")
        self.freq.refresh()
        self.unknown.refresh()

    # ------------------------------------------------------------- actions
    def toggle_pause(self):
        self.running = not self.running
        if self.source is not None:
            (self.source.paused.clear if self.running else self.source.paused.set)()
        self.pause_btn.setText("❚❚  Pause capture" if self.running else "▶  Resume capture")
        self.engine.info("capture resumed" if self.running else "capture paused")

    def toggle_ack(self, key: str):
        n = self.engine.nodes.get(key)
        if n:
            self.engine.acknowledge(key, not n.acknowledged)
            self.unknown.refresh()

    def run_command(self, cmd: str):
        parts = cmd.split()
        c, args = parts[0].lower(), parts[1:]
        e = self.engine
        if c == "help":
            for ln in ["help                 this list",
                       "clear                clear the log",
                       "reset                reset timing statistics",
                       "pause | resume       stop / continue capture",
                       "filter <lvl>         all | err | warn | unk | info",
                       "ack <node|all>       acknowledge unrecognized node(s)",
                       "unknown              list unrecognized IDs",
                       "id <0x123>           details for one ID",
                       "save <file>          write the log to a text file"]:
                self.term.echo(ln, MUTED)
        elif c == "clear":
            self.term.clear()
        elif c == "reset":
            e.reset_stats()
        elif c in ("pause", "resume"):
            if (c == "pause") == self.running:
                self.toggle_pause()
        elif c == "filter" and args and args[0].upper() in LEVELS:
            self.term.set_filter(args[0].upper())
        elif c == "ack":
            targets = [n.key for n in e.unknown_nodes()] if not args or args[0] == "all" else [" ".join(args)]
            for k in targets:
                if k in e.nodes and not e.nodes[k].known:
                    e.acknowledge(k, True)
                else:
                    self.term.echo(f"no unrecognized node '{k}'", ERR)
        elif c == "unknown":
            rows = [s for s in e.ids.values() if not s.known]
            if not rows:
                self.term.echo("no unrecognized IDs", MUTED)
            for s in sorted(rows, key=lambda s: s.can_id):
                m = s.measured_ms
                self.term.echo(f"{hex_id(s.can_id, s.extended):<11} {s.node:<12} {s.count:>7} frames  "
                               f"{(f'{m:.1f} ms' if m else '—'):>9}  {s.last_data.hex(' ').upper()}", self.accent)
        elif c == "id" and args:
            try:
                want = int(args[0], 16)
            except ValueError:
                self.term.echo("usage: id 0x123", ERR)
                return
            hits = [s for s in e.ids.values() if s.can_id == want]
            if not hits:
                self.term.echo(f"{args[0]} not seen", ERR)
            for s in hits:
                self.term.echo(f"{hex_id(s.can_id, s.extended)} {s.name}  node={s.node}  status={s.status}", SOFT)
                self.term.echo(f"  expected={s.ref_ms}  measured={s.measured_ms and round(s.measured_ms, 2)} ms  "
                               f"jitter={s.jitter_ms and round(s.jitter_ms, 3)} ms  frames={s.count}", SOFT)
                self.term.echo(f"  last data [{s.dlc}] {s.last_data.hex(' ').upper()}", SOFT)
        elif c == "save" and args:
            path = " ".join(args)
            try:
                with open(path, "w", encoding="utf-8") as fh:
                    for ln in e.log:
                        fh.write(f"[{ln.t:10.3f}] {ln.level:<4} {ln.text}\n")
                self.term.echo(f"saved {len(e.log)} lines to {path}", OK)
            except OSError as ex:
                self.term.echo(f"save failed: {ex}", ERR)
        else:
            self.term.echo(f"unknown command '{cmd}' — try 'help'", ERR)

    def closeEvent(self, ev):
        if self.source is not None:
            self.source.stop()
        super().closeEvent(ev)
