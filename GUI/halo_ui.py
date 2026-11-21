"""
PySide6 window for the HALO level: what the listener on the ZCU104 sees,
computed by HaloEngine. It reuses theme and widgets of ui.py (unchanged).

Layout
  top bar      source, pause, clear log
  tiles        events/s, flows, REQ->RSP latency, timeouts, external nodes, gaps, listener time
  flows        one row per channel x REQ/RSP/MSG: period, sd, min/max, frequency, ppm, status
  stream       next to flows: every REQ / RSP / MSG as it arrives, like the listener log
  channels     latency and timeout outcomes (#TOUT) per channel
  external     CAN IDs from #ALIEN
  terminal     warnings, gaps, timeouts, external nodes
"""

from __future__ import annotations

import html
import queue
import time
from collections import deque
from typing import List, Optional

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QAbstractItemView, QFrame, QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton,
    QSplitter, QTabWidget, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

import halo_config as cfg
import ui
from halo_chart import LatencyChart
from halo_engine import Flow, HaloEngine
from halo_log import HaloEvent
from ui import (
    BORDER2, DIM, DRIFT, ERR, INFO, MUTED, OK, SOFT, TEXT, Logo, Terminal, Tile, label, mono, stylesheet,
)

STATUS_COLOR = {"OK": OK, "DRIFT": DRIFT, "SILENT": ERR, "LEARN": MUTED}
STATUS_TEXT = {"OK": "OK", "DRIFT": "FUORI TOL.", "SILENT": "SILENZIO", "LEARN": "IN ATTESA"}
TYPE_COLOR = {"REQ": OK, "RSP": TEXT, "MSG": INFO}
OUTCOME_COLOR = {"OK": OK, "GRIGIA": DRIFT, "TARDIVA": ERR, "?": MUTED}
TAB_CSS = f"""
    QTabWidget::pane {{ border: none; padding-top: 10px; }}
    QTabBar::tab {{ background: {ui.PANEL}; color: {MUTED}; border: 1px solid {ui.BORDER}; border-radius: 8px;
                    padding: 7px 18px; margin-right: 8px; font-weight: 600; }}
    QTabBar::tab:selected {{ color: {TEXT}; border-color: {OK}; }}
"""


# ------------------------------------------------------------------ formatting
def f_ms(us: Optional[float], dec: int = 3) -> str:
    return "—" if us is None else f"{us / 1000:.{dec}f} ms"


def f_us(us: Optional[float], dec: int = 1) -> str:
    return "—" if us is None else f"{us:.{dec}f} µs"


def f_ppm(mean_us: Optional[float], nominal_us: float) -> str:
    if mean_us is None:
        return "—"
    return f"{(mean_us - nominal_us) / nominal_us * 1e6:+.0f} ppm"


# ------------------------------------------------------------------ helpers
def make_panel(title: str, subtitle: str = ""):
    """Panel with a header line, same look as the panels of ui.py."""
    panel = QFrame()
    panel.setObjectName("panel")
    lay = QVBoxLayout(panel)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)
    head = QFrame()
    head.setObjectName("panelHead")
    hl = QHBoxLayout(head)
    hl.setContentsMargins(16, 10, 16, 10)
    hl.setSpacing(10)
    hl.addWidget(label(title, "h2"))
    if subtitle:
        hl.addWidget(label(subtitle, color=MUTED))
    hl.addStretch(1)
    lay.addWidget(head)
    return panel, lay, hl


def make_table(headers: List[str], widths: List[int]) -> QTableWidget:
    t = QTableWidget(0, len(headers))
    t.setHorizontalHeaderLabels([h.upper() for h in headers])
    t.verticalHeader().hide()
    t.verticalHeader().setDefaultSectionSize(30)
    t.setShowGrid(False)
    t.setWordWrap(False)
    t.setSelectionBehavior(QAbstractItemView.SelectRows)
    t.setEditTriggers(QAbstractItemView.NoEditTriggers)
    t.setFocusPolicy(Qt.NoFocus)
    hh = t.horizontalHeader()
    hh.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
    hh.setHighlightSections(False)
    for i, w in enumerate(widths):
        hh.resizeSection(i, w)
    hh.setStretchLastSection(True)
    return t


def set_cell(t: QTableWidget, row: int, col: int, text: str, color: str = SOFT, bold: bool = False):
    it = t.item(row, col)
    if it is None:
        it = QTableWidgetItem()
        t.setItem(row, col, it)
    if it.text() != text:
        it.setText(text)
    it.setForeground(QColor(color))
    if bold:
        it.setFont(mono(12, True))


# ------------------------------------------------------------------ message stream
class EventStream(QFrame):
    """Every REQ / RSP / MSG in arrival order, one line each, as in the listener log:

        time[s]  channel  lane  type  tag  lat[us]  esito  payload

    lat and esito are filled only on RSP lines. Lines are shown in the order the
    listener printed them (an RSP may appear after a later event of another channel).
    """

    MAX_LINES = 3000

    def __init__(self):
        super().__init__()
        self.setObjectName("panel")
        self.pending: deque = deque(maxlen=self.MAX_LINES)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        head = QFrame()
        head.setObjectName("panelHead")
        hl = QHBoxLayout(head)
        hl.setContentsMargins(16, 10, 16, 10)
        hl.addWidget(label("Flusso dei messaggi", "h2"))
        hl.addWidget(label("controller ↔ responder", color=MUTED))
        hl.addStretch(1)
        legend = label(f"<span style='color:{OK}'>REQ</span> L0 C→R&nbsp;&nbsp;"
                       f"<span style='color:{TEXT}'>RSP</span> L0 R→C&nbsp;&nbsp;"
                       f"<span style='color:{INFO}'>MSG</span> L1 C→R", font=mono(11), color=MUTED)
        legend.setTextFormat(Qt.RichText)
        hl.addWidget(legend)
        lay.addWidget(head)

        cols = label(self._nb(f"{'time[s]':>12}  {'channel':<9} {'lane':<4} {'type':<4} {'tag':<3} "
                              f"{'lat[us]':>7}  {'esito':<7}  payload"), font=mono(12), color=MUTED)
        cols.setTextFormat(Qt.RichText)
        # the global stylesheet sets font-size on every QWidget: force the same font as the view
        cols.setStyleSheet(f"color: {MUTED}; font-family: '{ui.MONO}'; font-size: 12px;")
        cols.setContentsMargins(14, 6, 10, 0)
        lay.addWidget(cols)

        self.view = QPlainTextEdit()
        self.view.setReadOnly(True)
        self.view.setMaximumBlockCount(self.MAX_LINES)
        self.view.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.view.document().setDocumentMargin(10)
        self.view.setFont(mono(12))
        lay.addWidget(self.view, 1)

    @staticmethod
    def _nb(text: str) -> str:
        return html.escape(text).replace(" ", "&nbsp;")

    def add(self, ev: HaloEvent):
        self.pending.append(ev)

    def _fmt(self, ev: HaloEvent) -> str:
        nb = self._nb
        tag = "-" if ev.tag is None else str(ev.tag)
        if ev.type == "RSP":
            lat = "-" if ev.lat_us is None else str(ev.lat_us)
            out = ev.outcome or ""
        else:
            lat, out = "", ""
        oc = OUTCOME_COLOR.get(out, MUTED)
        return (f"<span style='color:{DIM}'>{nb(f'{ev.t:12.6f}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{SOFT}'>{nb(f'{ev.channel:<9}')}</span>&nbsp;"
                f"<span style='color:{MUTED}'>{nb(f'L{ev.lane:<3}')}</span>&nbsp;"
                f"<span style='color:{TYPE_COLOR.get(ev.type, SOFT)};font-weight:700'>{nb(f'{ev.type:<4}')}</span>&nbsp;"
                f"<span style='color:{SOFT}'>{nb(f'{tag:<3}')}</span>&nbsp;"
                f"<span style='color:{SOFT}'>{nb(f'{lat:>7}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{oc}'>{nb(f'{out:<7}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{MUTED}'>{nb(ev.payload or '-')}</span>")

    def flush(self):
        """Writes the pending lines; keeps following the bottom unless the user scrolled up."""
        if not self.pending:
            return
        sb = self.view.verticalScrollBar()
        follow = sb.value() >= sb.maximum() - 4
        self.view.appendHtml("<br>".join(self._fmt(ev) for ev in self.pending))
        self.pending.clear()
        if follow:
            sb.setValue(sb.maximum())


# ------------------------------------------------------------------ main window
class HaloWindow(QMainWindow):
    FLOW_COLS = ["Canale", "Flusso", "Direzione", "Periodo medio", "Dev. std", "Min", "Max",
                 "Frequenza", "Scostamento", "Finestra corrente", "Stato"]
    FLOW_W = [78, 50, 70, 98, 70, 84, 84, 92, 84, 150, 84]

    CH_COLS = ["Canale", "Nodi", "REQ", "RSP", "MSG", "Timeout", "Latenza media", "Latenza max",
               "OK", "Grigie", "Tardive", "Mancanti"]
    CH_W = [78, 176, 54, 54, 54, 64, 134, 86, 50, 56, 60, 70]

    AL_COLS = ["ID CAN (29 bit)", "Frame", "Finestre", "Ultima fin.", "Prima vista", "Ultima vista"]
    AL_W = [112, 62, 66, 78, 82, 82]

    def __init__(self, engine: HaloEngine, source, line_q: "queue.Queue", accent: str,
                    live: bool, channel_label: str = "zcu104", can_view: Optional[QWidget] = None):
        super().__init__()
        ui.MONO = ui.pick_mono()           # same font choice as ui.MainWindow
        self.engine = engine
        self.source = source
        self.q = line_q
        self.accent = accent
        self.live = live
        self.running = True
        self._last_events = 0
        self._last_wall = time.monotonic()
        self._rate = 0.0

        self.setWindowTitle("HALO Monitor")
        self.resize(1600, 1000)
        self.setStyleSheet(stylesheet(accent) + TAB_CSS)

        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(20, 16, 20, 18)
        outer.setSpacing(14)

        outer.addLayout(self._top_bar(source.description))
        page = QWidget()                   # HALO content (a tab when can_view is given)
        page_lay = QVBoxLayout(page)
        page_lay.setContentsMargins(0, 0, 0, 0)
        page_lay.setSpacing(14)
        page_lay.addLayout(self._tiles())
        # flows (top) / channels + external nodes (middle) / terminal (bottom)
        flows_panel, fl, self.flows_head = make_panel(
            "Flussi HALO", "C = controller, R = responder")
        self.flows_info = label("", font=mono(11), color=MUTED)
        self.flows_info.setTextFormat(Qt.RichText)
        self.flows_head.addWidget(self.flows_info)
        self.flow_table = make_table(self.FLOW_COLS, self.FLOW_W)
        fl.addWidget(self.flow_table, 1)

        ch_panel, cl, _ = make_panel("Canali", "messaggi scambiati, latenza richiesta → risposta, esiti del timeout")
        self.ch_table = make_table(self.CH_COLS, self.CH_W)
        cl.addWidget(self.ch_table, 1)

        al_panel, al, al_head = make_panel("Nodi estranei", "ID non appartenenti alla rete")
        self.al_badge = label("0", font=mono(12, True))
        self.al_badge.setAlignment(Qt.AlignCenter)
        self.al_badge.setMinimumWidth(28)
        self.al_badge.setStyleSheet(f"background: {BORDER2}; color: {TEXT}; border-radius: 10px; padding: 2px 8px;")
        al_head.addWidget(self.al_badge)
        self.al_table = make_table(self.AL_COLS, self.AL_W)
        self.al_empty = label("Nessun nodo estraneo rilevato", font=mono(12), color=MUTED)
        self.al_empty.setAlignment(Qt.AlignCenter)
        self.al_empty.setMinimumHeight(60)
        self.al_note = label("", font=mono(11), color=MUTED)
        self.al_note.setContentsMargins(16, 6, 16, 8)
        al.addWidget(self.al_empty)
        al.addWidget(self.al_table, 1)
        al.addWidget(self.al_note)

        self.stream = EventStream()
        self.chart = LatencyChart(engine)
        engine.on_event = self._on_event      # message stream + latency chart
        top = QSplitter(Qt.Horizontal)
        top.addWidget(flows_panel)
        top.addWidget(self.stream)
        top.setSizes([1020, 580])
        top.setHandleWidth(14)

        mid = QSplitter(Qt.Horizontal)
        mid.addWidget(ch_panel)
        mid.addWidget(al_panel)
        mid.setSizes([1060, 540])
        mid.setHandleWidth(14)

        self.term = Terminal(engine, accent, channel_label)
        for lb in self.term.findChildren(QLabel):
            if lb.text().startswith("errors"):
                lb.setText("avvisi — listener HALO")
        self.term.command.connect(self.run_command)

        vs = QSplitter(Qt.Vertical)
        vs.addWidget(top)
        vs.addWidget(self.chart)
        vs.addWidget(mid)
        vs.addWidget(self.term)
        vs.setSizes([320, 210, 190, 100])   # smaller terminal: the chart takes its space
        vs.setHandleWidth(14)
        page_lay.addWidget(vs, 1)
        if can_view is None:
            outer.addWidget(page, 1)
        else:
            # listener and candump in one window: one tab each
            tabs = QTabWidget()
            tabs.addTab(page, "HALO — listener")
            tabs.addTab(can_view, "Bus CAN — candump")
            outer.addWidget(tabs, 1)

        engine.on_log = self.term.on_log
        self.term.set_filter("ALL")

        self.drain_timer = QTimer(self)
        self.drain_timer.timeout.connect(self.drain)
        self.drain_timer.start(50)
        self.ui_timer = QTimer(self)
        self.ui_timer.timeout.connect(self.refresh)
        self.ui_timer.start(250)

    # ------------------------------------------------------------- building
    def _top_bar(self, description: str) -> QHBoxLayout:
        top = QHBoxLayout()
        top.setSpacing(12)
        top.addWidget(Logo())
        tl = QVBoxLayout()
        tl.setSpacing(0)
        title = label("HALO Monitor")
        title.setStyleSheet("font-size: 20px; font-weight: 700;")
        tl.addWidget(title)
        tl.addWidget(label(f"{description} · nodo listener ZCU104", font=mono(11), color=MUTED))
        top.addLayout(tl)
        chip = label("LIVE" if self.live else "REPLAY", font=mono(10))
        c = OK if self.live else MUTED
        chip.setStyleSheet(f"color: {c}; border: 1px solid {c}; border-radius: 4px; padding: 3px 7px;")
        top.addWidget(chip)
        top.addStretch(1)
        self.pause_btn = QPushButton("❚❚  Pausa")
        self.pause_btn.setObjectName("primary")
        self.pause_btn.setCursor(Qt.PointingHandCursor)
        self.pause_btn.clicked.connect(self.toggle_pause)
        clear = QPushButton("Pulisci avvisi")
        clear.setCursor(Qt.PointingHandCursor)
        clear.clicked.connect(lambda: self.term.clear())
        top.addWidget(self.pause_btn)
        top.addWidget(clear)
        return top

    def _tiles(self) -> QHBoxLayout:
        tiles = QHBoxLayout()
        tiles.setSpacing(12)
        self.t_rate = Tile("Eventi / s")
        self.t_flows = Tile("Flussi")
        self.t_lat = Tile("Latenza REQ→RSP")
        self.t_tout = Tile("Timeout")
        self.t_alien = Tile("Nodi estranei")
        self.t_gap = Tile("Interruzioni")
        self.t_time = Tile("Tempo listener")
        for t in (self.t_rate, self.t_flows, self.t_lat, self.t_tout, self.t_alien, self.t_gap, self.t_time):
            tiles.addWidget(t, 1)
        return tiles

    # ------------------------------------------------------------- data flow
    def drain(self):
        n = 0
        try:
            while n < 20000:
                item = self.q.get_nowait()
                n += 1
                kind = item[0]
                if kind == "halo":
                    self.engine.feed_line(item[1])
                elif kind == "halo_status":
                    self.engine.info(item[1])
                elif kind == "halo_eof":
                    self.engine.flush()
                    self.engine.info(f"replay terminato: {item[1]}")
        except queue.Empty:
            pass
        self.term.flush()
        if self.running:
            self.stream.flush()

    def refresh(self):
        if not self.running:
            return
        e = self.engine
        e.tick()
        self._refresh_tiles(e)
        self._refresh_flows(e)
        self._refresh_channels(e)
        self._refresh_aliens(e)
        self.chart.refresh()

    def _on_event(self, ev: HaloEvent):
        """Every event accepted by the engine goes to the message stream and the chart."""
        self.stream.add(ev)
        self.chart.add(ev)

    def _refresh_tiles(self, e: HaloEngine):
        now = time.monotonic()
        total = e.total_events()
        dt = now - self._last_wall
        if dt >= 1.0:
            self._rate = (total - self._last_events) / dt
            self._last_events, self._last_wall = total, now
        self.t_rate.value.setText(f"{self._rate:.0f}")
        self.t_rate.sub.setText(f"{total:,} eventi totali")

        flows = list(e.flows.values())
        cnt = {k: sum(1 for f in flows if f.status == k) for k in STATUS_COLOR}
        self.t_flows.value.setText(
            f"<span style='color:{OK}'>{cnt['OK']}</span>"
            f"<span style='font-size:13px;color:{MUTED}'> / {len(flows)} OK</span>")
        self.t_flows.sub.setText(
            f"<span style='color:{DRIFT}'>{cnt['DRIFT']} drift</span> · "
            f"<span style='color:{ERR}'>{cnt['SILENT']} silenti</span>")

        lats = [(ch, L) for ch, L in e.latency.items() if L.total.n]
        if lats:
            worst = max(L.total.mean for _, L in lats)
            self.t_lat.value.setText(f"{worst / 1000:.2f}<span style='font-size:13px;color:{MUTED}'> ms</span>")
            self.t_lat.sub.setText(" · ".join(f"{ch.replace('channel', 'ch')} {L.total.mean:.0f}"
                                              for ch, L in lats) + " µs")
        else:
            self.t_lat.value.setText("—")
            self.t_lat.sub.setText("nessuna risposta")

        tt = list(e.timeouts.values())
        bad = sum(t.missing + t.late for t in tt)
        gray = sum(t.gray for t in tt)
        ok = sum(t.ok for t in tt)
        ts = sorted({t.timeout_us for t in tt if t.timeout_us})
        t_txt = ", ".join(f"{v / 1000:g} ms" for v in ts) if ts else "off"
        col = ERR if bad else (DRIFT if gray else OK)
        self.t_tout.value.setText(f"<span style='color:{col}'>{bad}</span>"
                                  f"<span style='font-size:13px;color:{MUTED}'> KO · T {t_txt}</span>")
        self.t_tout.sub.setText(f"{ok} OK · {gray} grigie")
        self.t_tout.setToolTip("KO = richieste senza risposta + risposte tardive (#TOUT del listener)\n"
                               "grigie = risposte vicine al timeout (T ± 2 ms)")

        n_al = len(e.aliens)
        acol = self.accent if n_al or e.alien_untracked_total else OK
        self.t_alien.value.setText(f"<span style='color:{acol}'>{n_al}</span>"
                                   f"<span style='font-size:13px;color:{MUTED}'> ID</span>")
        self.t_alien.sub.setText(f"{e.alien_frames_total:,} frame · "
                                 f"{e.alien_windows_with_frames}/{e.alien_windows} fin.")

        gaps = max((f.total_gaps for f in flows), default=0)
        longest = max((f.longest_gap_us for f in flows), default=0)
        self.t_gap.value.setText(f"<span style='color:{DRIFT if gaps else OK}'>{gaps}</span>")
        self.t_gap.sub.setText(f"la più lunga {longest / 1e6:.3f} s" if gaps else "nessuna")

        t = e.now_s
        self.t_time.value.setText(f"{int(t // 3600):02d}:{int(t % 3600 // 60):02d}:{int(t % 60):02d}")
        rc = OK if self.running else DRIFT
        self.t_time.sub.setText(f"<span style='color:{rc}'>● finestra {e.window_s:g} s</span>")

    def _direction(self, e: HaloEngine, f: Flow):
        """Compact direction for the table (C = controller, R = responder) and the full one."""
        ch = e.channels.get(f.channel)
        if ch is None:
            return "—", ""
        if f.type == "RSP":
            return "R → C", f"{ch.responder} → {ch.controller}"
        return "C → R", f"{ch.controller} → {ch.responder}"

    def _refresh_flows(self, e: HaloEngine):
        rows = e.flow_rows()
        t = self.flow_table
        if t.rowCount() != len(rows):
            t.setRowCount(len(rows))
        for r, f in enumerate(rows):
            tot = f.total
            mean = tot.mean if tot.n else None
            st = f.status
            sc = STATUS_COLOR.get(st, MUTED)
            set_cell(t, r, 0, f.channel, TEXT, bold=True)
            set_cell(t, r, 1, f.type, TEXT)
            short, full = self._direction(e, f)
            set_cell(t, r, 2, short, SOFT)
            t.item(r, 2).setToolTip(full)
            set_cell(t, r, 3, f_ms(mean), TEXT)
            set_cell(t, r, 4, f_us(tot.sd), SOFT)
            set_cell(t, r, 5, f_ms(f.total_min_us), SOFT)
            set_cell(t, r, 6, f_ms(f.total_max_us), SOFT)
            set_cell(t, r, 7, "—" if mean is None else f"{1e6 / mean:.5f} Hz", SOFT)
            ppm_col = DRIFT if st == "DRIFT" else MUTED
            set_cell(t, r, 8, f_ppm(mean, f.nominal_us), ppm_col)
            w = f.current
            if w is not None and w.period.n:
                sd = f" ± {w.period.sd:.1f} µs" if w.period.sd is not None else ""
                wtxt = f"{w.period.mean / 1000:.3f} ms{sd}"
            else:
                wtxt = "—"
            set_cell(t, r, 9, wtxt, MUTED)
            set_cell(t, r, 10, STATUS_TEXT.get(st, st), sc, bold=True)
        tol = cfg.PERIOD_TOLERANCE * 100
        noms = sorted({f.nominal_us for f in rows})
        nom = " / ".join(f"{v / 1000:g} ms" for v in noms)
        self.flows_info.setText(f"<span style='color:{MUTED}'>nominale {nom} · tolleranza ±{tol:g}% · "
                                f"interruzioni (&gt; {cfg.GAP_FACTOR:g}× periodo) escluse</span>")

    def _refresh_channels(self, e: HaloEngine):
        names = sorted(e.channels)
        t = self.ch_table
        if t.rowCount() != len(names):
            t.setRowCount(len(names))
        for r, ch in enumerate(names):
            c = e.channels[ch]
            L = e.latency.get(ch)
            T = e.timeouts.get(ch)
            set_cell(t, r, 0, ch, TEXT, bold=True)
            set_cell(t, r, 1, f"{c.controller} ↔ {c.responder}", SOFT)
            n_req = e.flows[(ch, "REQ")].total_events if (ch, "REQ") in e.flows else 0
            n_rsp = e.flows[(ch, "RSP")].total_events if (ch, "RSP") in e.flows else 0
            n_msg = e.flows[(ch, "MSG")].total_events if (ch, "MSG") in e.flows else 0
            set_cell(t, r, 2, f"{n_req:,}", SOFT)
            # more than one request without its response (the last one may be in flight)
            set_cell(t, r, 3, f"{n_rsp:,}", DRIFT if n_req - n_rsp > 1 else SOFT)
            set_cell(t, r, 4, f"{n_msg:,}", SOFT)
            set_cell(t, r, 5, f_ms(T.timeout_us, 0) if T and T.timeout_us else "off", MUTED)
            if L and L.total.n:
                sd = f" ± {L.total.sd:.1f}" if L.total.sd is not None else ""
                set_cell(t, r, 6, f"{L.total.mean:.1f}{sd} µs", TEXT)
                set_cell(t, r, 7, f_us(L.max_us, 0), SOFT)
            else:
                for col in (6, 7):
                    set_cell(t, r, col, "—", MUTED)
            if T:
                set_cell(t, r, 8, str(T.ok), OK if T.ok else MUTED)
                set_cell(t, r, 9, str(T.gray), DRIFT if T.gray else MUTED)
                set_cell(t, r, 10, str(T.late), ERR if T.late else MUTED)
                set_cell(t, r, 11, str(T.missing), ERR if T.missing else MUTED)

    def _refresh_aliens(self, e: HaloEngine):
        ids = sorted(e.aliens.values(), key=lambda a: a.first_seen_s)
        self.al_badge.setText(str(len(ids)))
        self.al_badge.setStyleSheet(
            f"background: {self.accent if ids else BORDER2}; color: {'#0A0E13' if ids else TEXT}; "
            f"border-radius: 10px; padding: 2px 8px;")
        self.al_empty.setVisible(not ids)
        self.al_table.setVisible(bool(ids))
        t = self.al_table
        if t.rowCount() != len(ids):
            t.setRowCount(len(ids))
        for r, a in enumerate(ids):
            set_cell(t, r, 0, f"0x{a.can_id:08X}", self.accent, bold=True)
            set_cell(t, r, 1, f"{a.frames:,}", SOFT)
            set_cell(t, r, 2, str(a.windows), SOFT)
            set_cell(t, r, 3, str(a.last_window_frames), self.accent if a.last_window_frames else MUTED)
            set_cell(t, r, 4, f"{a.first_seen_s:.1f} s", MUTED)
            set_cell(t, r, 5, f"{a.last_seen_s:.1f} s", MUTED)
        notes = []
        if e.alien_untracked_total:
            notes.append(f"+{e.alien_untracked_total:,} frame da ID oltre il limite della tabella "
                         f"({18} ID per finestra)")
        notes.append(f"aggiornato a fine finestra ({e.window_s:g} s)")
        self.al_note.setText(" · ".join(notes))

    # ------------------------------------------------------------- actions
    def toggle_pause(self):
        self.running = not self.running
        if self.source is not None:
            (self.source.paused.clear if self.running else self.source.paused.set)()
        self.pause_btn.setText("❚❚  Pausa" if self.running else "▶  Riprendi")
        self.engine.info("visualizzazione ripresa" if self.running else
                         "visualizzazione in pausa" + (" (i dati continuano ad arrivare)" if self.live else ""))

    def run_command(self, cmd: str):
        parts = cmd.split()
        c, args = parts[0].lower(), parts[1:]
        if c == "help":
            for ln in ["help                 questo elenco",
                       "clear                pulisce gli avvisi",
                       "pause | resume       ferma / riprende la visualizzazione",
                       "filter <livello>     all | err | warn | unk | info",
                       "save <file>          salva gli avvisi su file"]:
                self.term.echo(ln, MUTED)
        elif c == "clear":
            self.term.clear()
        elif c in ("pause", "resume"):
            if (c == "pause") == self.running:
                self.toggle_pause()
        elif c == "filter" and args and args[0].upper() in ui.LEVELS:
            self.term.set_filter(args[0].upper())
        elif c == "save" and args:
            path = " ".join(args)
            try:
                with open(path, "w", encoding="utf-8") as fh:
                    for ln in self.engine.log:
                        fh.write(f"[{ln.t:10.3f}] {ln.level:<4} {ln.text}\n")
                self.term.echo(f"salvate {len(self.engine.log)} righe in {path}", OK)
            except OSError as ex:
                self.term.echo(f"salvataggio fallito: {ex}", ERR)
        else:
            self.term.echo(f"comando sconosciuto '{cmd}' — prova 'help'", ERR)

    def closeEvent(self, ev):
        if self.source is not None:
            self.source.stop()
        super().closeEvent(ev)
