"""
Decoding of the 29-bit extended CAN ID used by HALO, for the frame-level view (candump).

Layout taken from the shift macros in HALO/PAL/Private/PAL_CAN_Config.h, with the widths
of HALO/Config/Private/HALO_Params.h (NODE_ID_BITS 5, LOCAL_CHANNEL_ID_BITS 5):

    bit  28-24  23-19  18-14  13    12          11-5    4-3                   2-0
         SRC    DST    CH     LANE  SYNC_FRAME  PKT_ID  RETRANS / SYNC_TYPE   TAG

SYNC_FRAME is active low: 0 = sync frame (PKT_ID = sequence, RETRANS = 0 SYNC / 1 FOLLOW_UP).

Every frame is classified as:
    REQ / RSP / MSG   data frame of a configured channel (lane 0 C->R, lane 0 R->C, lane 1 C->R)
    SYNC / FOLLOW_UP  time-sync burst of a known controller
    ALIEN             anything else: unknown source, no matching channel, wrong direction,
                      11-bit ID, sync from an unknown controller
The payload is never decoded here.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Optional, Tuple

import halo_config as cfg

# --- layout (HALO_Params.h / PAL_CAN_Config.h) ---------------------------------
NODE_ID_BITS = 5
CHANNEL_ID_BITS = 5

SRC_SHIFT = 29 - NODE_ID_BITS                      # 24
DST_SHIFT = SRC_SHIFT - NODE_ID_BITS               # 19
CH_SHIFT = DST_SHIFT - CHANNEL_ID_BITS             # 14
LANE_SHIFT = CH_SHIFT - 1                          # 13
SYNC_SHIFT = LANE_SHIFT - 1                        # 12
PKT_SHIFT = SYNC_SHIFT - 7                         # 5
RETRANS_SHIFT = PKT_SHIFT - 2                      # 3
TAG_SHIFT = 0

NODE_MASK = (1 << NODE_ID_BITS) - 1
CH_MASK = (1 << CHANNEL_ID_BITS) - 1

SYNC_TYPE_SYNC = 0
SYNC_TYPE_FOLLOW_UP = 1

# Fields that change from frame to frame of the same flow (segment, tag, sync sequence).
# Masking them gives one "base ID" per flow.
VARIABLE_FIELDS_MASK = (0x7F << PKT_SHIFT) | (0x03 << RETRANS_SHIFT) | 0x07

# HALO_SYNC_PERIOD_US in HALO_Params.h
SYNC_PERIOD_US = 1_000_000.0


@dataclass(frozen=True)
class CanIdFields:
    src: int
    dst: int
    ch: int
    lane: int
    sync_frame: bool        # True = sync frame (SYNC_FRAME bit = 0)
    pkt: int                # segment index, or sync sequence
    retrans: int            # RETRANS field, or SYNC_TYPE on sync frames
    tag: int


def decode(can_id: int) -> CanIdFields:
    return CanIdFields(
        src=(can_id >> SRC_SHIFT) & NODE_MASK,
        dst=(can_id >> DST_SHIFT) & NODE_MASK,
        ch=(can_id >> CH_SHIFT) & CH_MASK,
        lane=(can_id >> LANE_SHIFT) & 0x01,
        sync_frame=((can_id >> SYNC_SHIFT) & 0x01) == 0,
        pkt=(can_id >> PKT_SHIFT) & 0x7F,
        retrans=(can_id >> RETRANS_SHIFT) & 0x03,
        tag=can_id & 0x07,
    )


def base_id(can_id: int) -> int:
    """ID without segment / tag / sequence: the same value for every frame of a flow.
    On sync frames the SYNC_TYPE field is kept, so SYNC and FOLLOW_UP stay distinct."""
    if decode(can_id).sync_frame:
        return can_id & ~((0x7F << PKT_SHIFT) | 0x07)
    return can_id & ~VARIABLE_FIELDS_MASK


# --- topology from halo_config -----------------------------------------------
NODE_BY_NAME: Dict[str, int] = {n.name: nid for nid, n in cfg.NODES.items()}


def _channel_table() -> Dict[Tuple[int, int, int], str]:
    """(controller_id, responder_id, local_channel_id) -> channel name.
    The local channel ID is the low CHANNEL_ID_BITS of the global ID (HALO_Channels.c)."""
    out = {}
    for name, ch in cfg.CHANNELS.items():
        c = NODE_BY_NAME.get(ch.controller)
        r = NODE_BY_NAME.get(ch.responder)
        if c is not None and r is not None:
            out[(c, r, ch.global_id & CH_MASK)] = name
    return out


CHANNEL_BY_KEY = _channel_table()
CONTROLLER_IDS = {c for (c, _r, _l) in CHANNEL_BY_KEY}


@dataclass(frozen=True)
class CanFrameInfo:
    kind: str                   # REQ | RSP | MSG | SYNC | FOLLOW_UP | ALIEN
    channel: Optional[str]      # channel name for REQ / RSP / MSG
    fields: Optional[CanIdFields]   # None for 11-bit IDs
    src_name: Optional[str]     # known node that transmitted the frame, None for ALIEN
    reason: str = ""            # why a frame is ALIEN

    @property
    def flow(self) -> str:
        """Name of the flow the frame belongs to (row of the table)."""
        if self.kind in ("REQ", "RSP", "MSG"):
            return f"{self.channel} {self.kind}"
        if self.kind in ("SYNC", "FOLLOW_UP"):
            return f"{self.kind} {self.src_name}"
        if self.fields is None:
            return "ESTRANEO 11 bit"
        return f"ESTRANEO SRC 0x{self.fields.src:02X}"


def classify(can_id: int, extended: bool) -> CanFrameInfo:
    if not extended:
        return CanFrameInfo("ALIEN", None, None, None, "ID a 11 bit")
    f = decode(can_id)
    src_node = cfg.NODES.get(f.src)

    if f.sync_frame:
        if f.src in CONTROLLER_IDS and f.retrans in (SYNC_TYPE_SYNC, SYNC_TYPE_FOLLOW_UP):
            kind = "SYNC" if f.retrans == SYNC_TYPE_SYNC else "FOLLOW_UP"
            return CanFrameInfo(kind, None, f, src_node.name)
        return CanFrameInfo("ALIEN", None, f, None, "sync da un controller sconosciuto")

    if src_node is None:
        return CanFrameInfo("ALIEN", None, f, None, "sorgente sconosciuta")
    # data frame: the channel is identified by its two nodes and the local channel ID
    ch = CHANNEL_BY_KEY.get((f.src, f.dst, f.ch))      # controller -> responder
    if ch is not None:
        return CanFrameInfo("REQ" if f.lane == 0 else "MSG", ch, f, src_node.name)
    ch = CHANNEL_BY_KEY.get((f.dst, f.src, f.ch))      # responder -> controller
    if ch is not None and f.lane == 0:
        return CanFrameInfo("RSP", ch, f, src_node.name)
    reason = "lane 1 dal responder" if ch is not None else "nessun canale configurato"
    return CanFrameInfo("ALIEN", None, f, None, reason)
