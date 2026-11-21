"""
Listener output read from the serial console of the ZCU104 (no network needed).

The listener runs on the ZCU104 and prints on its console as usual; on the Mac this
reader takes the place of the terminal (screen) on the same USB-serial port and pushes
("halo", line) items into the queue, like HaloReplay / HaloTcpReader in sources.py.
Lines that are not listener output (shell prompt, kernel messages) are ignored by the parser.

Optional: every line received is also written to a file on the Mac (save_path), so the
measurement is kept without copying anything from the board.

Needs pyserial:  pip install pyserial
On macOS use the /dev/cu.* device (not /dev/tty.*): it opens without waiting for carrier detect.
"""

from __future__ import annotations

import queue
import threading
import time
from typing import Optional


class HaloSerialReader(threading.Thread):
    def __init__(self, out: "queue.Queue", port: str, baud: int = 115200,
                 save_path: Optional[str] = None):
        super().__init__(daemon=True, name="halo-serial")
        self.out = out
        self.port = port
        self.baud = baud
        self.save_path = save_path
        self.stop_evt = threading.Event()
        self.paused = threading.Event()     # ignored: lines are never dropped, the GUI stops refreshing
        self.ser = None
        self.description = f"listener ZCU104 via seriale {port} @ {baud}"

    def open(self):
        import serial                        # pyserial
        self.ser = serial.Serial(self.port, self.baud, timeout=0.2)

    def run(self):
        import serial
        save = open(self.save_path, "a", encoding="utf-8") if self.save_path else None
        buf = b""
        warned = False
        try:
            while not self.stop_evt.is_set():
                if self.ser is None:
                    try:
                        self.open()
                        self.out.put(("halo_status", f"seriale {self.port} aperta a {self.baud} baud"))
                        warned = False
                    except (serial.SerialException, OSError) as e:
                        if not warned:
                            self.out.put(("halo_status", f"seriale {self.port} non disponibile ({e}), riprovo"))
                            warned = True
                        time.sleep(1.0)
                        continue
                try:
                    waiting = self.ser.in_waiting
                    chunk = self.ser.read(waiting if waiting else 1)   # 1 byte: waits at most the timeout
                except (serial.SerialException, OSError) as e:
                    self.out.put(("halo_status", f"seriale scollegata ({e}), riprovo"))
                    self._close()
                    time.sleep(1.0)
                    continue
                if not chunk:
                    continue
                buf += chunk
                *lines, buf = buf.split(b"\n")
                for raw in lines:
                    line = raw.decode("utf-8", errors="replace").rstrip("\r")
                    if save:
                        save.write(line + "\n")
                        save.flush()
                    self.out.put(("halo", line))
        finally:
            self._close()
            if save:
                save.close()

    def _close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:  # noqa: BLE001
                pass
            self.ser = None

    def stop(self):
        self.stop_evt.set()
