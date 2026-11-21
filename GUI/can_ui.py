"""
"Bus CAN" view: the HALO bus frame by frame, from candump (can_engine.py).
Reuses theme and widgets of ui.py (unchanged), including the live bus graphic.

Layout
  header       source, pause
  tiles        bus load, frames/s, nodes, SYNC, controller clock drift, bus time
  bus graphic  nodes of halo_config + external sources, LED on every frame   | SYNC per controller
                                                                             | warnings
  flows        frames, frames/s, segments, DLC, period, sd, time on bus,    | frame stream
               RETRANS != 0, status                                          | (candump, decoded ID)
"""

from __future__ import annotations

import html
import queue
from collections import deque

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QFrame, QHBoxLayout, QLabel, QMainWindow, QPlainTextEdit, QPushButton, QSplitter, QVBoxLayout,
    QWidget,
)

import halo_config as cfg
import ui
from can_engine import CanEngine, CanFlow
from can_id import CanFrameInfo
from engine import Frame
from halo_ui import make_panel, make_table, set_cell
from ui import (
    BORDER2, DIM, DRIFT, ERR, INFO, MUTED, OK, SOFT, TEXT, Spark, Terminal, Tile, TopologyWidget,
    label, mono, stylesheet,
)

STATUS_TEXT = {"OK": "OK", "DRIFT": "FUORI TOL.", "SILENT": "SILENZIO", "LEARN": "IN ATTESA",
               "ALIEN": "ESTRANEO"}


def kind_color(kind: str, accent: str) -> str:
    return {"REQ": OK, "RSP": TEXT, "MSG": INFO, "SYNC": DRIFT, "FOLLOW_UP": DRIFT}.get(kind, accent)


# ------------------------------------------------------------------ frame stream
class FrameStream(QFrame):
    """Every frame as candump shows it, plus the decoded ID:

        time[s]  ID  [DLC]  data  type  channel  SRC→DST  lane  pkt  tag

    time = seconds since the first frame. The data bytes are not decoded.
    """

    MAX_LINES = 3000

    def __init__(self, accent: str):
        super().__init__()
        self.setObjectName("panel")
        self.accent = accent
        self.pending: deque = deque(maxlen=self.MAX_LINES)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)
        head = QFrame()
        head.setObjectName("panelHead")
        hl = QHBoxLayout(head)
        hl.setContentsMargins(16, 10, 16, 10)
        hl.addWidget(label("Frame sul bus", "h2"))
        hl.addWidget(label("candump, ID decodificato", color=MUTED))
        hl.addStretch(1)
        lay.addWidget(head)
        cols = label(self._nb(f"{'time[s]':>11}  {'ID':<8}  {'DLC':<3}  {'dati':<23}  "
                              f"{'tipo':<9} {'canale':<9} {'SRC→DST':<9} L  pkt tag"), color=MUTED)
        cols.setTextFormat(Qt.RichText)
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

    def add(self, f: Frame, info: CanFrameInfo, t_rel: float):
        self.pending.append((f, info, t_rel))

    def _fmt(self, f: Frame, info: CanFrameInfo, t: float) -> str:
        nb = self._nb
        idtxt = f"{f.can_id:08X}" if f.extended else f"{f.can_id:03X}"
        data = f.data.hex(" ").upper()
        fl = info.fields
        if info.kind in ("REQ", "RSP", "MSG"):
            detail = (f"{info.channel:<9} 0x{fl.src:02X}→0x{fl.dst:02X} {fl.lane}  "
                      f"{fl.pkt:<3} {fl.tag}")
        elif info.kind in ("SYNC", "FOLLOW_UP"):
            detail = f"{'':<9} 0x{fl.src:02X}→all  -  seq {fl.pkt}"
        else:
            detail = info.reason
        kc = kind_color(info.kind, self.accent)
        kind = "ESTRANEO" if info.kind == "ALIEN" else info.kind
        return (f"<span style='color:{DIM}'>{nb(f'{t:11.6f}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{SOFT}'>{nb(f'{idtxt:<8}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{MUTED}'>{nb(f'[{f.dlc}]')}</span>&nbsp;&nbsp;"
                f"<span style='color:{SOFT}'>{nb(f'{data:<23}')}</span>&nbsp;&nbsp;"
                f"<span style='color:{kc};font-weight:700'>{nb(f'{kind:<9}')}</span>&nbsp;"
                f"<span style='color:{MUTED}'>{nb(detail)}</span>")

    def flush(self):
        if not self.pending:
            return
        sb = self.view.verticalScrollBar()
        follow = sb.value() >= sb.maximum() - 4
        self.view.appendHtml("<br>".join(self._fmt(*p) for p in self.pending))
        self.pending.clear()
        if follow:
            sb.setValue(sb.maximum())


# ------------------------------------------------------------------ view
class CanBusView(QWidget):
    FLOW_COLS = ["Flusso", "ID base", "Frame", "Frame/s", "Segm.", "DLC", "Periodo medio",
                 "Dev. std", "Durata bus", "RETRANS≠0", "Stato"]
    FLOW_W = [140, 84, 56, 58, 50, 44, 98, 66, 80, 76, 80]

    SYNC_COLS = ["Controller", "SYNC", "Seq", "Salti seq", "Periodo", "SYNC→FOLLOW_UP",
                 "Deriva clock", "Riavvii"]
    SYNC_W = [90, 50, 46, 70, 150, 116, 96, 60]

    def __init__(self, engine: CanEngine, source, frame_q: "queue.Queue", accent: str, live: bool):
        super().__init__()
        ui.MONO = ui.pick_mono()
        self.engine = engine
        self.source = source
        self.q = frame_q
        self.accent = accent
        self.live = live
        self.running = True

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(14)
        outer.addLayout(self._header(source.description))
        outer.addLayout(self._tiles())

        # bus graphic
        topo_panel, tl, th = make_panel("Bus", "attività dal vivo · passa sopra un nodo per i suoi ID")
        legend = label(
            f"<span style='color:{OK}'>■</span> <span style='color:{MUTED}'>rete HALO</span>&nbsp;&nbsp;&nbsp;"
            f"<span style='color:{accent}'>▢</span> <span style='color:{MUTED}'>estraneo</span>&nbsp;&nbsp;&nbsp;"
            f"<span style='color:{BORDER2}'>■</span> <span style='color:{MUTED}'>non trasmette</span>")
        legend.setTextFormat(Qt.RichText)
        th.addWidget(legend)
        self.topo = TopologyWidget(engine, accent)
        self.topo.ackRequested.connect(self._toggle_ack)
        tl.addWidget(self.topo, 1)

        # SYNC + warnings on the right of the bus graphic
        sync_panel, sl, _ = make_panel("Sincronizzazione", "SYNC / FOLLOW_UP del controller")
        self.sync_table = make_table(self.SYNC_COLS, self.SYNC_W)
        sl.addWidget(self.sync_table, 1)
        self.term = Terminal(engine, accent, "can0")
        for lb in self.term.findChildren(QLabel):
            if lb.text().startswith("errors"):
                lb.setText("avvisi — bus CAN")
        self.term.command.connect(self._command)
        right = QSplitter(Qt.Vertical)
        right.addWidget(sync_panel)
        right.addWidget(self.term)
        right.setSizes([150, 220])
        right.setHandleWidth(14)

        top = QSplitter(Qt.Horizontal)
        top.addWidget(topo_panel)
        top.addWidget(right)
        top.setSizes([840, 760])
        top.setHandleWidth(14)

        # flows + frame stream
        flows_panel, fl, self.flows_head = make_panel("Flussi sul bus", "")
        self.flows_info = label("", font=mono(11), color=MUTED)
        self.flows_info.setTextFormat(Qt.RichText)
        self.flows_head.addWidget(self.flows_info)
        self.flow_table = make_table(self.FLOW_COLS, self.FLOW_W)
        fl.addWidget(self.flow_table, 1)
        self.stream = FrameStream(accent)
        engine.on_frame = self.stream.add
        bottom = QSplitter(Qt.Horizontal)
        bottom.addWidget(flows_panel)
        bottom.addWidget(self.stream)
        bottom.setSizes([1000, 600])
        bottom.setHandleWidth(14)

        vs = QSplitter(Qt.Vertical)
        vs.addWidget(top)
        vs.addWidget(bottom)
        vs.setSizes([420, 360])
        vs.setHandleWidth(14)
        outer.addWidget(vs, 1)

        engine.on_log = self.term.on_log
        self.term.set_filter("ALL")

        self.drain_timer = QTimer(self)
        self.drain_timer.timeout.connect(self.drain)
        self.drain_timer.start(50)
        self.ui_timer = QTimer(self)
        self.ui_timer.timeout.connect(self.refresh)
        self.ui_timer.start(250)

    # ------------------------------------------------------------- building
    def _header(self, description: str) -> QHBoxLayout:
        h = QHBoxLayout()
        h.setSpacing(12)
        h.addWidget(label(f"{description} · bus CAN a {self.engine.bitrate // 1000} kbit/s",
                          font=mono(11), color=MUTED))
        chip = label("LIVE" if self.live else "REPLAY", font=mono(10))
        c = OK if self.live else MUTED
        chip.setStyleSheet(f"color: {c}; border: 1px solid {c}; border-radius: 4px; padding: 3px 7px;")
        h.addWidget(chip)
        h.addStretch(1)
        self.pause_btn = QPushButton("❚❚  Pausa bus")
        self.pause_btn.setCursor(Qt.PointingHandCursor)
        self.pause_btn.clicked.connect(self.toggle_pause)
        h.addWidget(self.pause_btn)
        return h

    def _tiles(self) -> QHBoxLayout:
        tiles = QHBoxLayout()
        tiles.setSpacing(12)
        self.t_load = Tile("Carico bus")
        self.load_spark = Spark(OK)
        self.t_load.layout().addWidget(self.load_spark)
        self.t_load.sub.hide()
        self.t_fps = Tile("Frame / s")
        self.t_nodes = Tile("Nodi")
        self.t_sync = Tile("SYNC")
        self.t_drift = Tile("Deriva clock")
        self.t_time = Tile("Tempo bus")
        for t in (self.t_load, self.t_fps, self.t_nodes, self.t_sync, self.t_drift, self.t_time):
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
                if kind == "can":
                    self.engine.ingest(item[1])
                elif kind == "can_status":
                    self.engine.info(item[1])
                elif kind == "can_connected":
                    self.engine.skip_seen = True   # the stream restarts from the first line
                elif kind == "can_eof":
                    self.engine.flush()
                    self.engine.info(f"fine: {item[1]}")
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
        self._refresh_sync(e)

    def _refresh_tiles(self, e: CanEngine):
        self.t_load.value.setText(f"{e.bus_load():.2f}<span style='font-size:13px;color:{MUTED}'> %</span>")
        self.load_spark.set_values(e.load_history)
        self.t_fps.value.setText(f"{e.fps():.0f}")
        self.t_fps.sub.setText(f"{e.total_frames:,} frame totali")

        known = sum(1 for n in e.nodes.values() if n.known and n.ids)
        n_known = sum(1 for n in e.nodes.values() if n.known)
        unk = len(e.unknown_nodes())
        ucol = self.accent if unk else MUTED
        self.t_nodes.value.setText(
            f"{known}<span style='font-size:12px;color:{MUTED}'> / {n_known} attivi </span>"
            f"<span style='color:{ucol}'>{unk}</span>"
            f"<span style='font-size:12px;color:{ucol}'> estr.</span>")
        self.t_nodes.sub.setText(f"{e.alien_frames:,} frame estranei" if unk else "nessun nodo estraneo")

        syncs = list(e.sync.values())
        sflow = next((fl for fl in e.flows.values() if fl.kind == "SYNC"), None)
        if sflow is not None and sflow.period.n:
            self.t_sync.value.setText(f"{sflow.period.mean / 1e6:.6f}"
                                      f"<span style='font-size:13px;color:{MUTED}'> s</span>")
        else:
            self.t_sync.value.setText("—")
        jumps = sum(s.seq_jumps for s in syncs)
        self.t_sync.sub.setText(f"{sum(s.syncs for s in syncs)} ricevuti · "
                                f"<span style='color:{DRIFT if jumps else MUTED}'>{jumps} salti seq</span>")

        drift = next((s.drift_ppm for s in syncs if s.drift_ppm is not None), None)
        self.t_drift.value.setText("—" if drift is None else
                                   f"{drift:+.2f}<span style='font-size:13px;color:{MUTED}'> ppm</span>")
        self.t_drift.sub.setText("controller vs ZCU")
        self.t_drift.setToolTip("Dai timestamp del controller nei FOLLOW_UP confrontati con l'arrivo dei SYNC "
                                "su candump.\nNegativo = l'orologio del controller va più lento di quello della ZCU.")

        t = e.now()
        self.t_time.value.setText(f"{int(t // 3600):02d}:{int(t % 3600 // 60):02d}:{int(t % 60):02d}")
        jc = DRIFT if e.clock_jumps else MUTED
        self.t_time.sub.setText(f"<span style='color:{jc}'>{e.clock_jumps} salti orologio</span>"
                                if e.clock_jumps else "dal primo frame")

    def _refresh_flows(self, e: CanEngine):
        rows = e.flow_rows()
        t = self.flow_table
        if t.rowCount() != len(rows):
            t.setRowCount(len(rows))
        for r, fl in enumerate(rows):
            alien = fl.kind == "ALIEN"
            name_col = self.accent if alien else TEXT
            set_cell(t, r, 0, fl.name, name_col, bold=True)
            set_cell(t, r, 1, f"0x{fl.base_id:08X}" if fl.extended else f"0x{fl.base_id:03X}",
                     self.accent if alien else SOFT)
            set_cell(t, r, 2, f"{fl.frames:,}", SOFT)
            set_cell(t, r, 3, f"{fl.fps(e.last_ts):.0f}", SOFT)
            set_cell(t, r, 4, str(fl.seg_last or fl.cur_segments or "—"), SOFT)
            set_cell(t, r, 5, ",".join(str(d) for d in sorted(fl.dlcs)), MUTED)
            set_cell(t, r, 6, self._period(fl), TEXT)
            set_cell(t, r, 7, "—" if fl.period.sd is None else f"{fl.period.sd:.1f} µs", SOFT)
            if fl.on_bus.n:
                set_cell(t, r, 8, f"{fl.on_bus.mean:.0f} µs", SOFT)
                t.item(r, 8).setToolTip(f"dal primo all'ultimo segmento: media {fl.on_bus.mean:.1f} µs, "
                                        f"max {fl.on_bus_max_us:.0f} µs")
            else:
                set_cell(t, r, 8, "—", MUTED)
            set_cell(t, r, 9, str(fl.retrans_nonzero), DRIFT if fl.retrans_nonzero else MUTED)
            sc = {"OK": OK, "DRIFT": DRIFT, "SILENT": ERR, "ALIEN": self.accent}.get(fl.status, MUTED)
            set_cell(t, r, 10, STATUS_TEXT.get(fl.status, fl.status), sc, bold=True)
            t.item(r, 0).setToolTip(f"{fl.name}\nnominale: "
                                    f"{'—' if fl.nominal_us is None else f'{fl.nominal_us / 1000:g} ms'}\n"
                                    f"interruzioni escluse: {fl.gaps}"
                                    + (f"\nmotivo: {fl.reason}" if alien else ""))
        tol = cfg.PERIOD_TOLERANCE * 100
        self.flows_info.setText(f"<span style='color:{MUTED}'>periodo tra primi segmenti · "
                                f"tolleranza ±{tol:g}%</span>")

    @staticmethod
    def _period(fl: CanFlow) -> str:
        if not fl.period.n:
            return "—"
        m = fl.period.mean
        return f"{m / 1e6:.6f} s" if m >= 1e6 else f"{m / 1000:.3f} ms"

    def _refresh_sync(self, e: CanEngine):
        rows = sorted(e.sync.values(), key=lambda s: s.controller)
        t = self.sync_table
        if t.rowCount() != len(rows):
            t.setRowCount(len(rows))
        for r, s in enumerate(rows):
            sflow = next((fl for fl in e.flows.values() if fl.kind == "SYNC" and fl.node_key == s.controller), None)
            set_cell(t, r, 0, s.controller, TEXT, bold=True)
            set_cell(t, r, 1, str(s.syncs), SOFT)
            set_cell(t, r, 2, "—" if s.last_seq is None else str(s.last_seq), SOFT)
            set_cell(t, r, 3, str(s.seq_jumps), DRIFT if s.seq_jumps else MUTED)
            if sflow is not None and sflow.period.n:
                sd = f" ± {sflow.period.sd:.0f} µs" if sflow.period.sd is not None else ""
                set_cell(t, r, 4, f"{sflow.period.mean / 1e6:.6f} s{sd}", TEXT)
            else:
                set_cell(t, r, 4, "—", MUTED)
            if s.delay.n:
                set_cell(t, r, 5, f"{s.delay.mean:.0f} µs (max {s.delay_max_us:.0f})", SOFT)
            else:
                set_cell(t, r, 5, "—", MUTED)
            set_cell(t, r, 6, "—" if s.drift_ppm is None else f"{s.drift_ppm:+.2f} ppm", TEXT)
            set_cell(t, r, 7, str(s.restarts), DRIFT if s.restarts else MUTED)

    # ------------------------------------------------------------- actions
    def _toggle_ack(self, key: str):
        n = self.engine.nodes.get(key)
        if n:
            self.engine.acknowledge(key, not n.acknowledged)

    def toggle_pause(self):
        self.running = not self.running
        if self.source is not None:
            (self.source.paused.clear if self.running else self.source.paused.set)()
        self.pause_btn.setText("❚❚  Pausa bus" if self.running else "▶  Riprendi bus")
        self.engine.info("visualizzazione ripresa" if self.running else
                         "visualizzazione in pausa" + (" (i frame continuano ad arrivare)" if self.live else ""))

    def _command(self, cmd: str):
        parts = cmd.split()
        c, args = parts[0].lower(), parts[1:]
        if c == "help":
            for ln in ["help                 questo elenco",
                       "clear                pulisce gli avvisi",
                       "filter <livello>     all | err | warn | unk | info",
                       "save <file>          salva gli avvisi su file"]:
                self.term.echo(ln, MUTED)
        elif c == "clear":
            self.term.clear()
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

    def stop(self):
        if self.source is not None:
            self.source.stop()


class CanWindow(QMainWindow):
    """Window with the Bus CAN view only (candump without the listener)."""

    def __init__(self, view: CanBusView, accent: str):
        super().__init__()
        self.view = view
        self.setWindowTitle("HALO Monitor — Bus CAN")
        self.resize(1600, 1000)
        self.setStyleSheet(stylesheet(accent))
        root = QWidget()
        root.setObjectName("root")
        lay = QVBoxLayout(root)
        lay.setContentsMargins(20, 16, 20, 18)
        title = label("Bus CAN")
        title.setStyleSheet("font-size: 20px; font-weight: 700;")
        lay.addWidget(title)
        lay.addWidget(view, 1)
        self.setCentralWidget(root)

    def closeEvent(self, ev):
        self.view.stop()
        super().closeEvent(ev)
