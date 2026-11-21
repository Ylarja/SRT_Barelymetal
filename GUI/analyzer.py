#!/usr/bin/env python3
"""
CAN Bus Analyzer — live task frequency, node map with unrecognized-node
highlighting, and an error terminal.

HALO mode (events of the listener node on the ZCU104, see halo_engine.py):
  python analyzer.py --halo-replay output.txt              # replay a recorded log
  python analyzer.py --halo-replay output.txt --halo-speed 4
  python analyzer.py --halo-tcp 5000                       # live; on the ZCU104:
      tail -n +1 -F /tmp/halo.log | nc <mac_ip> 5000

Examples
  python analyzer.py --simulate
  python analyzer.py -i socketcan -c can0 -b 1000000 --dbc car.dbc
  python analyzer.py -i pcan -c PCAN_USBBUS1 -b 1000000 --dbc car.dbc
  python analyzer.py -i socketcan -c can0 --learn 10      # no DBC: learn a baseline
  python analyzer.py --halo-replay output.txt
  python analyzer.py --halo-tcp 5000
  python analyzer.py --halo-replay output.txt --candump-sim output.txt   # both tabs, no board
  python analyzer.py --halo-tcp 5000 --candump-tcp 5001                  # live, both tabs
"""

from __future__ import annotations

import argparse
import os
import queue
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        prog="analyzer.py",
        description="CAN bus analyzer GUI (python-can + PySide6)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Examples", 1)[1] if "Examples" in __doc__ else "",
    )
    src = p.add_argument_group("source")
    src.add_argument("--simulate", action="store_true", help="generate synthetic traffic (no hardware needed)")
    src.add_argument("-i", "--interface", default="socketcan",
                     help="python-can interface: socketcan, pcan, vector, kvaser, slcan, ixxat, gs_usb, … "
                          "(default: socketcan)")
    src.add_argument("-c", "--channel", default="can0", help="channel, e.g. can0, PCAN_USBBUS1, COM5 (default: can0)")
    src.add_argument("-b", "--bitrate", type=int, default=1_000_000,
                     help="nominal bitrate in bit/s (default: 1000000, the HALO bitrate)")
    src.add_argument("--fd", action="store_true", help="open the bus in CAN FD mode")
    src.add_argument("--bus-arg", action="append", default=[], metavar="KEY=VALUE",
                     help="extra keyword for can.Bus(), repeatable (e.g. --bus-arg app_name=CANalyzer)")

    halo = p.add_argument_group("HALO listener")
    hsrc = halo.add_mutually_exclusive_group()
    hsrc.add_argument("--halo-replay", metavar="FILE",
                      help="replay a log of Listener_ZCU104 at real speed")
    hsrc.add_argument("--halo-tcp", type=int, metavar="PORT",
                      help="receive the live listener output on this TCP port")
    hsrc.add_argument("--halo-serial", metavar="DEVICE",
                      help="read the listener output from the ZCU104 serial console, e.g. /dev/cu.usbserial-XXXX")
    halo.add_argument("--baud", type=int, default=115200, help="serial baud rate (default: 115200)")
    halo.add_argument("--halo-save", metavar="FILE",
                      help="with --halo-serial: also save every received line to FILE")
    halo.add_argument("--halo-speed", type=float, default=1.0, metavar="X",
                      help="replay speed factor (default: 1 = real time)")
    halo.add_argument("--dev", action="store_true",
                      help="print the developer check (#STAT comparison) on stdout")
    
    can = p.add_argument_group("candump (frame-level view)")
    csrc = can.add_mutually_exclusive_group()
    csrc.add_argument("--candump-tcp", type=int, metavar="PORT",
                      help="receive candump -L output from the ZCU104 on this TCP port")
    csrc.add_argument("--candump-replay", metavar="FILE", help="replay a candump -L log")
    csrc.add_argument("--candump-sim", metavar="LISTENER_LOG",
                      help="simulate the bus frames from a listener log (no board needed)")
    csrc.add_argument("--candump-serial", metavar="DEVICE",
                      help="read candump -L from the ZCU104 serial console, e.g. /dev/cu.usbserial-XXXX")
    can.add_argument("--candump-save", metavar="FILE",
                     help="with --candump-serial: also save every received line to FILE")

    ana = p.add_argument_group("analysis")
    ana.add_argument("--dbc", help="DBC (or ARXML/KCD/SYM) file; IDs not in it are flagged as unrecognized")
    ana.add_argument("--learn", type=float, default=None, metavar="SECONDS",
                     help="treat IDs seen in the first N seconds as known baseline "
                          "(default: 0 with a DBC, 5 without one)")
    ana.add_argument("--tolerance", type=float, default=8.0, metavar="PCT",
                     help="cycle-time deviation that counts as drift, in %% (default: 8)")
    ana.add_argument("--timeout-factor", type=float, default=3.0, metavar="X",
                     help="an ID is TIMEOUT after X × its cycle time of silence (default: 3)")
    ana.add_argument("--group-bits", type=int, default=4, metavar="N",
                     help="group unrecognized IDs into one node per 2^N ID block; 0 = one node per ID (default: 4)")

    ui = p.add_argument_group("display / logging")
    ui.add_argument("--accent", default="#FFB020", help="highlight colour for unrecognized nodes (default: #FFB020)")
    ui.add_argument("--log-file", help="also append every log line to this file")
    ui.add_argument("--sim-errors", choices=["quiet", "normal", "storm"], default="normal",
                    help="error-frame rate in --simulate mode (default: normal)")
    return p.parse_args(argv)


def load_db(path):
    import cantools
    try:
        return cantools.database.load_file(path, strict=False)
    except Exception as e:  # noqa: BLE001
        sys.exit(f"error: could not load DBC '{path}': {e}")


def parse_bus_args(items):
    out = {}
    for it in items:
        if "=" not in it:
            sys.exit(f"error: --bus-arg expects KEY=VALUE, got '{it}'")
        k, v = it.split("=", 1)
        for cast in (int, float):
            try:
                v = cast(v)
                break
            except ValueError:
                pass
        if v in ("true", "True"):
            v = True
        elif v in ("false", "False"):
            v = False
        out[k] = v
    return out


def check_deps(packages=(("PySide6", "PySide6"), ("can", "python-can"), ("cantools", "cantools"))):
    """Fail early with the exact install command for *this* Python."""
    import importlib.util
    missing = [pkg for mod, pkg in packages if importlib.util.find_spec(mod) is None]
    if missing:
        sys.exit(
            f"error: missing package(s): {', '.join(missing)}\n"
            f"This Python is: {sys.executable}\n"
            f"Install them into it with:\n"
            f'  "{sys.executable}" -m pip install {" ".join(missing)}'
        )


def main_halo(args):
    """HALO mode: listener events from a replayed log or from the ZCU104 over TCP."""
    pkgs = [("PySide6", "PySide6")] + ([("serial", "pyserial")] if args.halo_serial else [])
    check_deps(tuple(pkgs))     # python-can and cantools are not needed here

    from PySide6.QtWidgets import QApplication
    from halo_engine import HaloEngine
    from halo_ui import HaloWindow
    from sources import HaloReplay, HaloTcpReader

    lines: "queue.Queue" = queue.Queue()
    on_dev = (lambda text: print(f"DEV  {text}", flush=True)) if args.dev else None
    engine = HaloEngine(on_dev=on_dev)

    if args.halo_replay:
        if not os.path.exists(args.halo_replay):
            sys.exit(f"error: file not found: {args.halo_replay}")
        source = HaloReplay(lines, args.halo_replay, speed=args.halo_speed)
        live, host = False, "replay"
    elif args.halo_serial:
        from halo_serial import HaloSerialReader
        source = HaloSerialReader(lines, args.halo_serial, args.baud, save_path=args.halo_save)
        live, host = True, "zcu104"
    else:
        source = HaloTcpReader(lines, args.halo_tcp)
        try:
            source.open()
        except OSError as e:
            sys.exit(f"error: cannot listen on TCP port {args.halo_tcp}: {e}")
        live, host = True, "zcu104"

    log_fh = open(args.log_file, "a", encoding="utf-8") if args.log_file else None

    app = QApplication.instance() or QApplication(sys.argv)
    app.setApplicationName("HALO Monitor")
    win = HaloWindow(engine, source, lines, args.accent, live=live, channel_label=host)

    if log_fh:
        gui_cb = engine.on_log

        def tee(line):
            gui_cb(line)
            log_fh.write(f"[{line.t:10.3f}] {line.level:<4} {line.text}\n")
            log_fh.flush()
        engine.on_log = tee

    source.start()
    win.show()
    rc = app.exec()
    source.stop()
    if args.dev:
        c = engine.check
        print(f"DEV  confronto con #STAT: {c['checked']} finestre, uguali {c['match']}, "
              f"diverse {c['differ']}, non confrontabili per gap {c['skipped_gap']}")
    if log_fh:
        log_fh.close()
    return rc


def main(argv=None):
    args = parse_args(argv)
    if args.candump_tcp is not None or args.candump_replay or args.candump_sim or args.candump_serial:
        from can_main import main_monitor          # candump view, alone or with the listener
        return main_monitor(args)
    if args.halo_replay is not None or args.halo_tcp is not None or args.halo_serial is not None:
        return main_halo(args)
    check_deps()

    db = None
    dbc_path = args.dbc
    if dbc_path is None and args.simulate:
        sample = os.path.join(HERE, "sample.dbc")
        if os.path.exists(sample):
            dbc_path = sample
    if dbc_path:
        db = load_db(dbc_path)

    learn = args.learn if args.learn is not None else (0.0 if db is not None else 5.0)

    from PySide6.QtWidgets import QApplication
    from engine import Engine
    from sources import BusReader, Simulator
    from ui import MainWindow

    log_fh = open(args.log_file, "a", encoding="utf-8") if args.log_file else None

    frames: "queue.Queue" = queue.Queue()
    engine = Engine(
        db=db,
        bitrate=args.bitrate,
        tolerance_pct=args.tolerance,
        timeout_factor=args.timeout_factor,
        learn_seconds=learn,
        unknown_group_bits=args.group_bits,
    )
    if dbc_path:
        engine.info(f"DBC file: {os.path.basename(dbc_path)}")

    if args.simulate:
        source = Simulator(frames, db=db, error_rate=args.sim_errors)
        if db is not None:
            source.description = f"simulator · {os.path.basename(dbc_path)} @ {args.bitrate // 1000} kbit/s"
        host = "sim"
    else:
        source = BusReader(frames, args.interface, args.channel, args.bitrate, fd=args.fd,
                           extra=parse_bus_args(args.bus_arg))
        if dbc_path:
            source.description += f" · {os.path.basename(dbc_path)}"
        host = str(args.channel)
        try:
            source.open()
        except Exception as e:  # noqa: BLE001
            sys.exit(f"error: could not open {args.interface}:{args.channel} — {e}\n"
                     f"hint: on Linux bring the link up first, e.g.\n"
                     f"  sudo ip link set {args.channel} up type can bitrate {args.bitrate} berr-reporting on\n"
                     f"or run with --simulate to try the UI without hardware.")
        engine.info(f"opened {source.description}")

    app = QApplication.instance() or QApplication(sys.argv)
    app.setApplicationName("CAN Bus Analyzer")
    win = MainWindow(engine, source, frames, args.accent, simulated=args.simulate, channel_label=host)

    if log_fh:
        gui_cb = engine.on_log

        def tee(line):
            gui_cb(line)
            log_fh.write(f"[{line.t:10.3f}] {line.level:<4} {line.text}\n")
            log_fh.flush()
        engine.on_log = tee
        for line in list(engine.log):
            log_fh.write(f"[{line.t:10.3f}] {line.level:<4} {line.text}\n")

    source.start()
    win.show()
    rc = app.exec()
    source.stop()
    if log_fh:
        log_fh.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
