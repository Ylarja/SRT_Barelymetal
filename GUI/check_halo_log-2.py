#!/usr/bin/env python3
"""
Developer tool (not part of the GUI): feeds a listener log to HaloEngine and
prints the comparison with the #STAT lines and a summary of what the GUI will show.

    python check_halo_log.py output.txt
    python check_halo_log.py output.txt --quiet     # summary only
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from halo_engine import HaloEngine  # noqa: E402


def main(argv=None):
    p = argparse.ArgumentParser(description="Check HaloEngine against the #STAT lines of a listener log")
    p.add_argument("log", help="listener log (stdout of Listener_ZCU104)")
    p.add_argument("--quiet", action="store_true", help="print only the summary")
    a = p.parse_args(argv)

    def on_log(line):
        if not a.quiet:
            print(f"[{line.t:9.3f}] {line.level:<4} {line.text}")

    def on_dev(text):
        if not a.quiet:
            print(f"            DEV  {text}")

    eng = HaloEngine(on_log=on_log, on_dev=on_dev)
    with open(a.log, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            eng.feed_line(line)
    eng.flush()

    print("\n=== flussi (intera esecuzione, gap esclusi) ===")
    print(f"{'channel':<10} {'type':<4} {'eventi':>6} {'gap':>4} {'gap max[s]':>10} "
          f"{'T_mean[us]':>11} {'T_sd[us]':>9} {'T_min':>7} {'T_max':>7} {'fuori tol':>9}")
    for f in eng.flow_rows():
        # whole-run statistics: merge of the closed windows
        n = sum(w.period.n for w in f.closed)
        if n:
            mean = sum(w.period.mean * w.period.n for w in f.closed) / n
            m2 = sum(w.period.m2 + w.period.n * (w.period.mean - mean) ** 2 for w in f.closed)
            sd = (m2 / (n - 1)) ** 0.5 if n > 1 else float("nan")
            tmin = min(w.min_us for w in f.closed if w.min_us is not None)
            tmax = max(w.max_us for w in f.closed if w.max_us is not None)
        else:
            mean = sd = float("nan")
            tmin = tmax = 0
        oot = sum(w.out_of_tolerance for w in f.closed)
        print(f"{f.channel:<10} {f.type:<4} {f.total_events:>6} {f.total_gaps:>4} "
              f"{f.longest_gap_us / 1e6:>10.3f} {mean:>11.1f} {sd:>9.1f} {tmin:>7} {tmax:>7} {oot:>9}")

    print("\n=== latenza REQ->RSP ===")
    for ch, L in eng.latency.items():
        if L.total.n:
            print(f"{ch:<10} n={L.total.n}  media {L.total.mean:.1f} us  sd {L.total.sd:.1f}  max {L.max_us}")

    print("\n=== timeout (#TOUT, calcolati dal listener) ===")
    for ch, t in eng.timeouts.items():
        print(f"{ch:<10} T={t.timeout_us}  ok={t.ok} grigia={t.gray} tardive={t.late} mancanti={t.missing}")

    print("\n=== nodi estranei (#ALIEN) ===")
    print(f"finestre {eng.alien_windows}, con intrusioni {eng.alien_windows_with_frames}, "
          f"frame {eng.alien_frames_total} (di cui non registrati {eng.alien_untracked_total})")
    for a_ in eng.aliens.values():
        print(f"  0x{a_.can_id:08X}: {a_.frames} frame in {a_.windows} finestre")

    c = eng.check
    print(f"\n=== confronto con #STAT ===\nconfrontate {c['checked']}: uguali {c['match']}, "
          f"diverse {c['differ']}, non confrontabili per gap {c['skipped_gap']}")
    print(f"righe lette {eng.lines}, non riconosciute {eng.unparsed}")
    return 0 if c["differ"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
