"""
HALO network description for the GUI: the "HALO DBC".

Values copied from the code of the project:
  - nodes and channels:  HALO/Config/Src/HALO_Nodes.c, HALO_Channels.c
  - nominal periods:     portenta.ino (REQUEST_PERIOD_MS, LANE1_PERIOD_MS)
  - lane0_rx_timeout:    HALO_Channels.c (10000 us)

If the header of the listener log reports the analysis window or the
lane0_rx_timeout of a channel, the values in the header win (see
HaloEngine.ingest_header).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict


@dataclass(frozen=True)
class HaloNode:
    name: str
    node_id: int


@dataclass(frozen=True)
class HaloChannel:
    name: str
    global_id: int
    controller: str
    responder: str
    lane0_rx_timeout_us: int


# --- Topology --------------------------------------------------------------

NODES: Dict[int, HaloNode] = {
    0x00: HaloNode("controller", 0x00),
    0x01: HaloNode("responder1", 0x01),
    0x02: HaloNode("responder2", 0x02),
    0x03: HaloNode("listener", 0x03),
}

CHANNELS: Dict[str, HaloChannel] = {
    "channel1": HaloChannel("channel1", 0x0020, "controller", "responder1", 10_000),
    "channel2": HaloChannel("channel2", 0x0040, "controller", "responder2", 10_000),
}

# --- Flows -----------------------------------------------------------------
# One flow = one event type on one channel.
#   REQ  request  lane 0  controller -> responder
#   RSP  response lane 0  responder  -> controller
#   MSG  message  lane 1  controller -> responder

FLOW_TYPES = ("REQ", "RSP", "MSG")

# Nominal period of each flow [us] (portenta.ino: 100 ms for lane 0 and lane 1).
NOMINAL_PERIOD_US: Dict[str, float] = {
    "REQ": 100_000.0,
    "RSP": 100_000.0,
    "MSG": 100_000.0,
}

# --- Thresholds ------------------------------------------------------------

# Allowed deviation of the mean period from the nominal one (0.1 % = 100 us).
# The ~+17 ppm measured on the bench (Portenta crystal vs ZCU clock) is well inside.
PERIOD_TOLERANCE = 0.001

# An interval longer than GAP_FACTOR x nominal period is a GAP (e.g. a controller
# restart): it is counted, but it does NOT enter the statistics.
GAP_FACTOR = 2.0

# Analysis window used when the log header does not report it [s].
DEFAULT_WINDOW_S = 10.0
