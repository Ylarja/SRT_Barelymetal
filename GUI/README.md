# CAN Bus Analyzer

A desktop CAN monitor built with Python (PySide6 + python-can + cantools). One window shows:

- **Status tiles**: bus load, frames/s, known vs. unknown nodes, error frames, REC/TEC counters and controller state.
- **Bus topology**: every node from your DBC hangs off a CAN_H/CAN_L line. Each node's LED flickers when it transmits. Nodes that are in the DBC but send nothing are drawn dimmed as *silent*. **Unrecognized sources** (IDs that aren't in the DBC) get a dashed amber card with a pulsing halo. Hover over a node to see its IDs; click an unknown one to acknowledge it.
- **Unrecognized nodes panel**: one card per unknown source with first-seen time, frame count, period estimate, last payload and an *Acknowledge* button.
- **Task frequency table**: for every ID, expected cycle time (from `GenMsgCycleTime` in the DBC, or learned from traffic), measured period, rate, jitter, a deviation bar with the tolerance band, a trend sparkline, and an OK / DRIFT / TIMEOUT / LEARN status. It's sortable, has a search filter and a *Problems only* toggle.
- **Error terminal**: decoded error frames (bit, stuff, form, CRC, ACK, arbitration, controller warnings/passive/bus-off), timing drift and timeouts, and new unknown IDs. You can filter by level, and it has a command prompt (type `help`).

## Install

```bash
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

Your adapter may also need its vendor driver (PCAN-Basic, Vector XL, Kvaser CANlib, …). See the python-can docs for your interface.

## Run

Try it without hardware:

```bash
python analyzer.py --simulate                 # uses sample.dbc
python analyzer.py --simulate --sim-errors storm
```

On a real bus:

```bash
# Linux / SocketCAN — enable bus-error reporting so error frames reach the analyzer
sudo ip link set can0 up type can bitrate 500000 berr-reporting on
python analyzer.py -i socketcan -c can0 -b 500000 --dbc your_car.dbc

# PEAK PCAN-USB (Windows/Linux)
python analyzer.py -i pcan -c PCAN_USBBUS1 -b 500000 --dbc your_car.dbc

# Vector, Kvaser, slcan (CANable etc.)
python analyzer.py -i vector -c 0 -b 500000 --dbc your_car.dbc --bus-arg app_name=CANalyzer
python analyzer.py -i kvaser -c 0 -b 500000 --dbc your_car.dbc
python analyzer.py -i slcan  -c /dev/ttyACM0 -b 500000 --dbc your_car.dbc
```

No DBC? The tool learns a baseline instead. IDs seen in the first N seconds count as known; anything new after that is flagged:

```bash
python analyzer.py -i socketcan -c can0 --learn 10
```

## Options

| Option | Default | What it does |
|---|---|---|
| `--dbc FILE` | – | DBC/ARXML/KCD/SYM database; defines known IDs, sender nodes and cycle times |
| `--learn SECONDS` | 0 with DBC, 5 without | Baseline learning window |
| `--tolerance PCT` | 8 | Cycle-time deviation that counts as DRIFT |
| `--timeout-factor X` | 3 | Silence longer than X × cycle time → TIMEOUT |
| `--group-bits N` | 4 | Groups unknown IDs into one node per 2^N block (`0` = one node per ID) |
| `--accent HEX` | `#FFB020` | Highlight colour for unrecognized nodes |
| `--log-file FILE` | – | Also appends every log line to a file |
| `--fd` | off | Opens the bus in CAN FD mode |
| `--bus-arg K=V` | – | Extra keyword passed to `can.Bus()` (repeatable) |

## Terminal commands

`help`, `clear`, `reset`, `pause`, `resume`, `filter err|warn|unk|info|all`, `ack <node|all>`, `unknown`, `id 0x1A0`, `save log.txt`

## How it works

```
sources.py   BusReader (python-can thread) / Simulator  ──►  queue
engine.py    Engine: per-ID timing, node activity, unknown detection, error decoding
ui.py        Qt widgets; drains the queue every 50 ms, refreshes views every 250 ms
analyzer.py  command line + wiring
```

- **Measured period** is the median of the last 12 inter-frame intervals, using the adapter's timestamps, so it isn't fooled by one late frame. **Jitter** is the standard deviation of the last 24.
- **Timeouts** are measured against the newest frame received, so GUI latency isn't mistaken for a missing message.
- **Node mapping** comes from the DBC's message senders. CAN frames carry no source address, so unknown IDs are grouped into "inferred sources" by ID block (`--group-bits`).
- **Error frames** are decoded from the SocketCAN layout (`linux/can/error.h`), including TEC/REC when the kernel reports them (CAN_ERR_CNT, kernel ≥ 5.19). Other interfaces report errors differently, so details and counters may be limited there.
- **Bus load** is estimated from frame count and DLC (base frame bits + ~10 % stuffing) over the last second.

The engine has no Qt dependency, so you can reuse it for a headless logger or tests.



## HALO mode (listener node on the ZCU104)

In HALO mode the GUI shows what the **listener node** (`Listener_ZCU104.cpp`) sees on the
HALO network: requests, responses and lane-1 messages per channel, their timing, the
timeout outcomes and the external nodes. It opens a separate window, **HALO Monitor**;
the CAN-level window above is not used. Only `PySide6` is needed (no python-can, no cantools).

### Run

```bash
# replay a recorded listener log at real speed (no board needed)
python analyzer.py --halo-replay output.txt
python analyzer.py --halo-replay output.txt --halo-speed 5     # 5x faster

# live from the ZCU104 over TCP (the Mac listens, the ZCU connects)
python analyzer.py --halo-tcp 5000

# developer check: comparison with the #STAT lines of the listener, printed on stdout
python analyzer.py --halo-replay output.txt --dev
python check_halo_log.py output.txt            # same check without the GUI
```

On the **ZCU104** (PetaLinux), for the live mode:

```bash
ip link set can0 down
ip link set can0 type can bitrate 1000000
ip link set can0 up
./ListenerZCU104.o 10 > /tmp/halo.log &                       # your listener binary, 10 s window
tail -n +1 -F /tmp/halo.log | nc <MAC_IP> 5000
```

The listener writes to a file first, so it keeps running if the network or the GUI
goes down; `nc` can be started again at any time. On macOS, allow incoming connections
for Python the first time (firewall prompt). The Mac IP is in
*System Settings → Network* or `ipconfig getifaddr en0`.

### What the window shows

| Panel | Content | Source |
|---|---|---|
| Tiles | events/s, flows OK, mean REQ→RSP latency, timeouts (KO = missing + late), external nodes, interruptions, listener time | engine |
| Flussi HALO | per channel × REQ/RSP/MSG: mean period, sample std dev, min/max, frequency, deviation from nominal in ppm, current window, status (OK / FUORI TOL. / SILENZIO) | engine |
| Flusso dei messaggi | every REQ / RSP / MSG as printed by the listener (time, channel, lane, type, tag, latency and outcome on RSP, payload) | listener event lines |
| Canali | REQ / RSP / MSG counts, timeout T, latency mean ± sd and max, OK / grigie / tardive / mancanti | engine + `#TOUT` |
| Nodi estranei | external 29-bit CAN IDs: frames, windows, first/last seen | `#ALIEN` |
| Avvisi | interruptions, status changes, GRIGIA / TARDIVA responses, new external IDs | engine |

### How the numbers are computed

- **Statistics are computed by the GUI** (`halo_engine.py`) from the event lines, with the
  same algorithm as the listener: Welford mean/variance, sample standard deviation (n−1),
  consecutive windows of the listener's length, the interval across two windows counted
  in the second. Timestamps are taken on the ZCU, so network delay does not affect them.
- **Interruptions**: an interval longer than 2× the nominal period (e.g. a controller
  restart) is counted but **kept out** of the statistics. Interruptions of several flows
  close in time are reported as one warning.
- **Timeouts**: the listener is the reference (`#TOUT`, T = `lane0_rx_timeout` = 10 ms).
  The latency statistics include OK and GRIGIA responses; TARDIVA responses are counted
  only in the timeout columns.
- **External nodes**: taken as they are from `#ALIEN`, so they update once per window.
- **`#STAT`** is never shown: it is only compared with the GUI values (`--dev`). Windows
  with an interruption are "not comparable" by design, since the listener includes it.
- Thresholds and topology are in `halo_config.py` (nominal 100 ms, tolerance ±0.1 %,
  interruption > 2× period); the analysis window and T come from the log header.

### Limits

- Restarting the listener restarts its clock from 0: restart the GUI too. A reconnection
  of `nc` (which sends the file again from the start) is handled: repeated lines are ignored.
- The message stream keeps the listener's print order: an RSP may appear after a later
  event of the other channel.
- Both log formats are accepted: current (10 columns, with src/dst/lat/esito) and old
  (6 columns).

### Files

```
halo_config.py      HALO network for the GUI: nodes, channels, nominal periods, thresholds
halo_log.py         parser of the listener output (events, #STAT, #TOUT, #ALIEN, header)
halo_engine.py      statistics, interruptions, latency, timeouts, external nodes, #STAT check
halo_ui.py          HALO Monitor window (reuses theme and widgets of ui.py)
sources.py          + HaloReplay (log replay) and HaloTcpReader (live over TCP)
analyzer.py         + --halo-replay, --halo-tcp, --halo-speed, --dev; default bitrate 1 Mbit/s
check_halo_log.py   developer tool: engine vs #STAT on a log file, no GUI
```


## candump mode (frame-level view, tab "Bus CAN")

`candump` on the ZCU104 sees **every frame** on the bus, including what the listener does
not show: SYNC / FOLLOW_UP, single segments, external nodes in real time. It runs next to
the listener and does not change HALO (a read-only SocketCAN socket gets a copy of each frame).
With both sources the window has two tabs, **HALO — listener** and **Bus CAN — candump**.

### Run

```bash
# without the board: listener replay + frames simulated from the same log, aligned
python analyzer.py --halo-replay output.txt --candump-sim output.txt
python can_sources.py output.txt > raw_sim.log          # save the simulated frames
python analyzer.py --candump-replay raw_sim.log         # Bus CAN view only

# live, both tabs
python analyzer.py --halo-tcp 5000 --candump-tcp 5001
```

On the **ZCU104**, next to the listener commands above:

```bash
candump -L can0 > /tmp/raw.log &
tail -n +1 -F /tmp/raw.log | nc <MAC_IP> 5001
```

`candump -L` is required (log format with timestamps). If `candump` is missing, add the
`can-utils` package to the PetaLinux rootfs (`petalinux-config -c rootfs`).

### What the Bus CAN tab shows

| Panel | Content |
|---|---|
| Tiles | bus load, frames/s, active / external nodes, SYNC period and sequence jumps, controller clock drift vs ZCU (ppm), bus time |
| Bus | live bus graphic of `ui.py`: nodes of `halo_config.py` (LED on every frame), external sources as dashed cards (click to acknowledge); the listener never transmits |
| Sincronizzazione | per controller: SYNC count, last sequence, jumps, period, SYNC→FOLLOW_UP delay, drift, restarts |
| Flussi sul bus | per flow: base ID, frames, frames/s, segments per message, DLC, message period, sd, time on bus (first→last segment), frames with RETRANS ≠ 0, status |
| Frame sul bus | every frame as candump shows it (payload not decoded) plus the decoded ID: type, channel, SRC→DST, lane, pkt, tag |

### How the frames are read

- **ID layout** (`can_id.py`, from the macros of `PAL_CAN_Config.h` and the widths in
  `HALO_Params.h`): `SRC 28-24 | DST 23-19 | CH 18-14 | LANE 13 | SYNC_FRAME 12 | PKT 11-5 |
  RETRANS/SYNC_TYPE 4-3 | TAG 2-0`. A flow is identified by its **base ID** (PKT, TAG and the
  sync sequence masked), otherwise one message would spread over dozens of IDs.
- **External**: unknown SRC, no configured channel, lane 1 from a responder, sync from an
  unknown controller, 11-bit ID.
- **Period** = first segment to first segment; interruptions (> 2× nominal, or a SYNC
  sequence that does not continue) are counted and kept out of the statistics.
- **Drift**: controller TX times carried by FOLLOW_UP vs candump arrival times of the
  matching SYNC, over a growing baseline (shown after 5 s). Negative = controller clock
  slower than the ZCU. A controller restart resets the baseline. This is the only payload
  field that is read.
- candump timestamps are the ZCU `CLOCK_REALTIME`: a step backwards is reported and the
  intervals across it are dropped. The two tabs use separate clocks and are not correlated.
- After a TCP reconnection (`tail -n +1` sends the file again) frames not newer than the
  last one are dropped.
- Frames/s on the node cards follows the arrival on the Mac: in a fast replay (`--halo-speed`)
  it is multiplied by the speed; the tables use the candump timestamps.

### Files

```
can_id.py        decoding and classification of the 29-bit HALO ID
can_engine.py    frame-level statistics, SYNC/FOLLOW_UP, drift, nodes for the bus graphic
can_sources.py   CandumpTcpReader, CandumpReplay, simulator from a listener log
can_ui.py        Bus CAN view (and window when the listener is not used)
can_main.py      start-up with candump, alone or together with the listener
```