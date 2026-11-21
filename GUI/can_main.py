"""
Start-up of the monitor when candump is involved (called by analyzer.py).

  listener + candump   one window, two tabs: "HALO — listener" and "Bus CAN — candump"
  candump only         one window with the Bus CAN view

Sources
  listener:  --halo-replay FILE | --halo-tcp PORT
  candump:   --candump-replay FILE | --candump-tcp PORT | --candump-sim LISTENER_LOG
  --halo-speed X applies to every replay and to the simulation.

Example without the board (both tabs, same timing):
  python analyzer.py --halo-replay output.txt --candump-sim output.txt
Live (on the ZCU104 see README):
  python analyzer.py --halo-tcp 5000 --candump-tcp 5001
"""

from __future__ import annotations

import os
import queue
import sys


def main_monitor(args):
    from analyzer import check_deps
    pkgs = [("PySide6", "PySide6")] + ([("serial", "pyserial")] if args.candump_serial else [])
    check_deps(tuple(pkgs))

    from PySide6.QtWidgets import QApplication

    from can_engine import CanEngine
    from can_sources import CandumpReplay, CandumpSimulator, CandumpTcpReader

    app = QApplication.instance() or QApplication(sys.argv)
    app.setApplicationName("HALO Monitor")

    # --- candump side
    frames: "queue.Queue" = queue.Queue()
    can_engine = CanEngine(bitrate=args.bitrate)
    if args.candump_tcp is not None:
        can_source = CandumpTcpReader(frames, args.candump_tcp)
        try:
            can_source.open()
        except OSError as e:
            sys.exit(f"error: cannot listen on TCP port {args.candump_tcp}: {e}")
        can_live = True
    elif args.candump_serial:
        from can_sources import CandumpSerialReader
        can_source = CandumpSerialReader(frames, args.candump_serial, args.baud,
                                         save_path=args.candump_save)
        can_live = True
    elif args.candump_replay:
        if not os.path.exists(args.candump_replay):
            sys.exit(f"error: file not found: {args.candump_replay}")
        can_source = CandumpReplay(frames, path=args.candump_replay, speed=args.halo_speed)
        can_live = False
    else:
        if not os.path.exists(args.candump_sim):
            sys.exit(f"error: file not found: {args.candump_sim}")
        can_source = CandumpSimulator(frames, args.candump_sim, speed=args.halo_speed)
        can_live = False

    from can_ui import CanBusView, CanWindow
    can_view = CanBusView(can_engine, can_source, frames, args.accent, live=can_live)

    # --- listener side (optional)
    halo_source = None
    halo_engine = None
    if args.halo_replay is not None or args.halo_tcp is not None:
        from halo_engine import HaloEngine
        from halo_ui import HaloWindow
        from sources import HaloReplay, HaloTcpReader

        lines: "queue.Queue" = queue.Queue()
        on_dev = (lambda text: print(f"DEV  {text}", flush=True)) if args.dev else None
        halo_engine = HaloEngine(on_dev=on_dev)
        if args.halo_replay:
            if not os.path.exists(args.halo_replay):
                sys.exit(f"error: file not found: {args.halo_replay}")
            halo_source = HaloReplay(lines, args.halo_replay, speed=args.halo_speed)
            live, host = False, "replay"
        else:
            halo_source = HaloTcpReader(lines, args.halo_tcp)
            try:
                halo_source.open()
            except OSError as e:
                sys.exit(f"error: cannot listen on TCP port {args.halo_tcp}: {e}")
            live, host = True, "zcu104"
        win = HaloWindow(halo_engine, halo_source, lines, args.accent, live=live,
                         channel_label=host, can_view=can_view)
    else:
        win = CanWindow(can_view, args.accent)

    # start the two sources together, so replays stay aligned
    if halo_source is not None:
        halo_source.start()
    can_source.start()
    win.show()
    rc = app.exec()
    can_source.stop()
    if halo_source is not None:
        halo_source.stop()
    if args.dev and halo_engine is not None:
        c = halo_engine.check
        print(f"DEV  confronto con #STAT: {c['checked']} finestre, uguali {c['match']}, "
              f"diverse {c['differ']}, non confrontabili per gap {c['skipped_gap']}")
    return rc
