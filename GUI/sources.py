"""
Frame sources. Each runs in its own thread and pushes engine.Frame objects
(or ("exception", text) tuples) into a queue the GUI drains.

  BusReader  - live traffic through python-can (any supported interface)
  Simulator  - synthetic traffic with drift, timeouts, unknown IDs and
               SocketCAN-style error frames, for testing without hardware

HALO listener sources (text lines instead of frames, see halo_engine.py):
  HaloReplay     - replays a listener log at real speed
  HaloTcpReader  - live listener output from the ZCU104 over TCP
"""

from __future__ import annotations

import heapq
import queue
import random
import threading
import time
from typing import Optional

from engine import (
    CAN_ERR_ACK, CAN_ERR_BUSERROR, CAN_ERR_CNT, CAN_ERR_CRTL, CAN_ERR_PROT, Frame,
)


class BusReader(threading.Thread):
    def __init__(self, out: "queue.Queue", interface: str, channel: str, bitrate: int,
                 fd: bool = False, extra: Optional[dict] = None):
        super().__init__(daemon=True, name="can-reader")
        self.out = out
        self.stop_evt = threading.Event()
        self.paused = threading.Event()
        self.kwargs = dict(interface=interface, channel=channel, bitrate=bitrate)
        if fd:
            self.kwargs["fd"] = True
        if extra:
            self.kwargs.update(extra)
        self.bus = None
        self.description = f"{interface}:{channel} @ {bitrate // 1000} kbit/s"

    def open(self):
        import can  # imported lazily so --simulate works without drivers
        self.bus = can.Bus(**self.kwargs)
        return self.bus

    def run(self):
        import can
        try:
            if self.bus is None:
                self.open()
        except Exception as e:  # noqa: BLE001
            self.out.put(("exception", f"could not open bus: {e}"))
            return
        while not self.stop_evt.is_set():
            try:
                msg = self.bus.recv(timeout=0.2)
            except can.CanError as e:
                self.out.put(("exception", str(e)))
                time.sleep(0.2)
                continue
            except Exception as e:  # noqa: BLE001
                self.out.put(("exception", repr(e)))
                time.sleep(0.5)
                continue
            if msg is None or self.paused.is_set():
                continue
            self.out.put(Frame(
                ts=msg.timestamp or time.time(),
                rx_mono=time.monotonic(),
                can_id=msg.arbitration_id,
                extended=bool(msg.is_extended_id),
                dlc=msg.dlc,
                data=bytes(msg.data),
                is_error=bool(msg.is_error_frame),
                is_remote=bool(msg.is_remote_frame),
            ))

    def stop(self):
        self.stop_evt.set()
        if self.bus is not None:
            try:
                self.bus.shutdown()
            except Exception:  # noqa: BLE001
                pass


class Simulator(threading.Thread):
    """Generates believable traffic from a DBC (or a built-in set)."""

    BUILTIN = [
        (0x0C0, 10), (0x0C8, 20), (0x1A0, 50), (0x1F5, 10), (0x220, 20),
        (0x2B0, 10), (0x3C0, 100), (0x420, 200), (0x510, 100), (0x5F0, 1000),
    ]
    UNKNOWN = [  # (id, period_ms, appears_after_s)
        (0x6F1, 33, 4.0),
        (0x3A7, 250, 17.0),
        (0x3A8, 500, 17.5),
    ]
    RATES = {"quiet": 0.004, "normal": 0.02, "storm": 0.18}  # error frames per 50 ms

    def __init__(self, out: "queue.Queue", db=None, error_rate: str = "normal"):
        super().__init__(daemon=True, name="can-sim")
        self.out = out
        self.stop_evt = threading.Event()
        self.paused = threading.Event()
        self.error_rate = error_rate
        self.description = "simulator @ 500 kbit/s"
        self.tec = 0
        self.rec = 0
        msgs = []
        if db is not None:
            for m in db.messages:
                period = getattr(m, "cycle_time", None) or 100
                msgs.append((m.frame_id, float(period), bool(m.is_extended_frame), m.length))
        else:
            msgs = [(i, float(p), False, 8) for i, p in self.BUILTIN]
        self.msgs = msgs
        # pick one fast-ish message to drift and one slow one to drop out
        by_period = sorted(msgs, key=lambda m: m[1])
        self.drift_id = by_period[len(by_period) // 2][0] if msgs else None
        slow = [m for m in by_period if 100 <= m[1] <= 500]
        self.timeout_id = slow[-1][0] if slow else None
        self.payload = {m[0]: bytearray(random.getrandbits(8) for _ in range(m[3])) for m in msgs}

    def stop(self):
        self.stop_evt.set()

    def _period(self, can_id: int, base: float, t: float) -> float:
        p = base * (1 + random.gauss(0, 0.012))
        if can_id == self.drift_id and 13 < t % 30 < 21:
            p = base * (1.17 + random.gauss(0, 0.02))
        return max(1.0, p)

    def _error_frame(self, now: float):
        kind = random.random()
        data = bytearray(8)
        if kind < 0.55:  # protocol error seen on reception
            cls = CAN_ERR_PROT | CAN_ERR_BUSERROR | CAN_ERR_CNT
            data[2] = random.choice([0x02, 0x04, 0x00, 0x01])
            data[3] = 0x08 if data[2] == 0 else random.choice([0x0A, 0x0B, 0x0F])
            self.rec = min(255, self.rec + 1)
        elif kind < 0.85:  # our TX side: ACK / bit errors
            if random.random() < 0.5:
                cls = CAN_ERR_ACK | CAN_ERR_BUSERROR | CAN_ERR_CNT
            else:
                cls = CAN_ERR_PROT | CAN_ERR_BUSERROR | CAN_ERR_CNT
                data[2] = 0x80 | random.choice([0x08, 0x10])
                data[3] = 0x0A
            self.tec = min(300, self.tec + 8)
        else:
            cls = CAN_ERR_PROT | CAN_ERR_BUSERROR | CAN_ERR_CNT
            data[2] = 0x04
            data[3] = 0x0A
            self.rec = min(255, self.rec + 1)
        m = max(self.tec, self.rec)
        if m > 127:
            cls |= CAN_ERR_CRTL
            data[1] = 0x20 if self.tec > 127 else 0x10
        elif m > 95:
            cls |= CAN_ERR_CRTL
            data[1] = 0x08 if self.tec > 95 else 0x04
        data[6] = min(255, self.tec)
        data[7] = self.rec
        self.out.put(Frame(time.time(), now, cls, False, 8, bytes(data), is_error=True))

    def run(self):
        start = time.monotonic()
        heap = []
        for can_id, period, ext, dlc in self.msgs:
            heapq.heappush(heap, (start + random.random() * period / 1000, can_id, period, ext, dlc))
        for can_id, period, appear in self.UNKNOWN:
            heapq.heappush(heap, (start + appear, can_id, float(period), False, 8))
        next_err = start + 0.05
        while not self.stop_evt.is_set():
            now = time.monotonic()
            if not heap:
                time.sleep(0.05)
                continue
            due, can_id, period, ext, dlc = heap[0]
            if due > now:
                time.sleep(min(due - now, 0.005))
                continue
            heapq.heappop(heap)
            t = now - start
            if self.paused.is_set():
                heapq.heappush(heap, (due + period / 1000, can_id, period, ext, dlc))
                continue
            silent = can_id == self.timeout_id and 33 < t % 45 < 36.5
            if not silent:
                payload = self.payload.get(can_id)
                if payload is None:  # unknown source
                    data = bytes([0x02, 0x10, 0x03, 0, 0, 0, 0, 0]) if can_id == 0x6F1 else bytes(
                        random.getrandbits(8) for _ in range(dlc))
                else:
                    payload[0] = (payload[0] + 1) & 0xFF  # rolling counter
                    if len(payload) > 2:
                        payload[2] = random.getrandbits(8)
                    data = bytes(payload)
                self.out.put(Frame(time.time(), now, can_id, ext, len(data), data))
            nxt = self._period(can_id, period, t) if can_id not in (u[0] for u in self.UNKNOWN) \
                else period * (1 + random.gauss(0, 0.05))
            heapq.heappush(heap, (due + nxt / 1000, can_id, period, ext, dlc))

            if now >= next_err:
                next_err += 0.05
                if random.random() < self.RATES.get(self.error_rate, 0.02):
                    self._error_frame(now)
                # successful traffic slowly heals the counters
                if random.random() < 0.5:
                    self.rec = max(0, self.rec - 1)
                if random.random() < 0.5:
                    self.tec = max(0, self.tec - 1)


# --------------------------------------------------------------------------
# HALO listener sources
#
# Both push ("halo", line) tuples into the queue: one text line of the
# listener stdout. Status messages go as ("halo_status", text); the end of a
# replay as ("halo_eof", path).
# --------------------------------------------------------------------------
class HaloReplay(threading.Thread):
    """Replays a listener log at the speed of its own event timestamps.

    Event lines are released when (wall time since start) x speed reaches their
    timestamp; '#' lines (header, #STAT, #TOUT, #ALIEN) are released as soon as
    the line before them has been released, so they keep the order of the file.
    """

    def __init__(self, out: "queue.Queue", path: str, speed: float = 1.0):
        super().__init__(daemon=True, name="halo-replay")
        self.out = out
        self.path = path
        self.speed = max(speed, 1e-3)
        self.stop_evt = threading.Event()
        self.paused = threading.Event()
        self.description = f"replay {path}" + (f" ×{speed:g}" if speed != 1.0 else "")

    def run(self):
        try:
            fh = open(self.path, encoding="utf-8", errors="replace")
        except OSError as e:
            self.out.put(("halo_status", f"impossibile aprire {self.path}: {e}"))
            return
        start = time.monotonic()
        paused_total = 0.0
        with fh:
            for line in fh:
                if self.stop_evt.is_set():
                    return
                t = self._event_time(line)
                if t is not None:
                    while not self.stop_evt.is_set():
                        if self.paused.is_set():
                            p0 = time.monotonic()
                            while self.paused.is_set() and not self.stop_evt.is_set():
                                time.sleep(0.05)
                            paused_total += time.monotonic() - p0
                            continue
                        wait = t / self.speed - (time.monotonic() - start - paused_total)
                        if wait <= 0:
                            break
                        time.sleep(min(wait, 0.05))
                self.out.put(("halo", line))
        self.out.put(("halo_eof", self.path))

    @staticmethod
    def _event_time(line: str):
        """Timestamp of an event line, None for '#' lines and anything else."""
        s = line.lstrip()
        if not s or s.startswith("#"):
            return None
        try:
            return float(s.split(None, 1)[0])
        except ValueError:
            return None

    def stop(self):
        self.stop_evt.set()


class HaloTcpReader(threading.Thread):
    """TCP server for the live listener output.

    On the ZCU104:  tail -n +1 -F /tmp/halo.log | nc <mac_ip> <port>
    One connection at a time; when it closes the reader waits for the next one.
    The pause flag is ignored on purpose: lines are never dropped, the GUI only
    stops refreshing.
    """

    def __init__(self, out: "queue.Queue", port: int, host: str = "0.0.0.0"):
        super().__init__(daemon=True, name="halo-tcp")
        self.out = out
        self.host = host
        self.port = port
        self.stop_evt = threading.Event()
        self.paused = threading.Event()
        self.srv = None
        self.description = f"listener ZCU104 via TCP :{port}"

    def open(self):
        import socket
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind((self.host, self.port))
        self.srv.listen(1)
        self.srv.settimeout(0.5)

    def run(self):
        import socket
        try:
            if self.srv is None:
                self.open()
        except OSError as e:
            self.out.put(("halo_status", f"porta {self.port} non disponibile: {e}"))
            return
        self.out.put(("halo_status", f"in attesa della ZCU sulla porta {self.port}"))
        while not self.stop_evt.is_set():
            try:
                conn, addr = self.srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            self.out.put(("halo_status", f"ZCU collegata da {addr[0]}"))
            conn.settimeout(0.5)
            buf = b""
            with conn:
                while not self.stop_evt.is_set():
                    try:
                        chunk = conn.recv(65536)
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    if not chunk:
                        break
                    buf += chunk
                    *lines, buf = buf.split(b"\n")
                    for raw in lines:
                        self.out.put(("halo", raw.decode("utf-8", errors="replace")))
            if buf:
                self.out.put(("halo", buf.decode("utf-8", errors="replace")))
            self.out.put(("halo_status", "connessione chiusa, in attesa di una nuova"))

    def stop(self):
        self.stop_evt.set()
        if self.srv is not None:
            try:
                self.srv.close()
            except OSError:
                pass
