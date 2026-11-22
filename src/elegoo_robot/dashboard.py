"""Live status dashboard for the Elegoo robot car.

Reads the car's status reports from its serial port and serves a web page
that shows them live. Run with:

    car-dashboard                  # auto-detects the car's USB serial port
    car-dashboard --port /dev/ttyUSB1

then open http://127.0.0.1:8000 in a browser.

Opening the serial port restarts the car's board (that's how Arduino USB
works), so any running mode stops when the dashboard connects.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import threading
import time
from collections import deque
from importlib import resources

import serial
import serial.tools.list_ports
import uvicorn
from starlette.applications import Starlette
from starlette.requests import Request
from starlette.responses import HTMLResponse, StreamingResponse
from starlette.routing import Route

BAUD = 115200
CH340_VID = 0x1A86  # USB-serial chip on the Elegoo UNO board
BOOT_WAIT_S = 2.5  # the board restarts when the port opens
STATUS_TIMEOUT_S = 3.0  # no report for this long: ask for reports again
LOG_LINES = 200


def find_port() -> str | None:
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if p.vid == CH340_VID:
            return p.device
    for p in ports:
        if p.device.startswith(("/dev/ttyUSB", "/dev/ttyACM")):
            return p.device
    return None


class CarLink:
    """Background thread that talks to the car and keeps the latest state."""

    def __init__(self, port: str | None) -> None:
        self.fixed_port = port
        self.lock = threading.Lock()
        self.port: str | None = None
        self.connected = False
        self.status: dict | None = None
        self.status_time = 0.0
        self.log: deque[tuple[int, float, str]] = deque(maxlen=LOG_LINES)
        self.log_seq = 0
        self.version = 0  # bumped on every change, so streams know when to send

    def start(self) -> None:
        threading.Thread(target=self._run, name="car-link", daemon=True).start()

    def _changed(self) -> None:
        self.version += 1

    def _add_log(self, text: str) -> None:
        with self.lock:
            self.log_seq += 1
            self.log.append((self.log_seq, time.time(), text))
            self._changed()

    def _set_connected(self, connected: bool, port: str | None) -> None:
        with self.lock:
            self.connected = connected
            self.port = port
            if not connected:
                self.status = None
            self._changed()

    def _run(self) -> None:
        while True:
            port = self.fixed_port or find_port()
            if not port:
                time.sleep(2)
                continue
            try:
                self._session(port)
            except (serial.SerialException, OSError) as e:
                self._add_log(f"[dashboard] lost connection to {port}: {e}")
            self._set_connected(False, None)
            time.sleep(2)

    def _session(self, port: str) -> None:
        with serial.Serial(port, BAUD, timeout=0.2) as ser:
            self._set_connected(True, port)
            self._add_log(f"[dashboard] connected to {port}, waiting for the car to start")
            time.sleep(BOOT_WAIT_S)
            ser.write(b"j")
            asked_at = time.time()
            buf = b""
            while True:
                buf += ser.read(512)
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    self._handle_line(raw.decode(errors="replace").strip())
                # The car restarted or stopped reporting: ask again.
                now = time.time()
                if now - max(self.status_time, asked_at) > STATUS_TIMEOUT_S:
                    ser.write(b"j")
                    asked_at = now

    def _handle_line(self, line: str) -> None:
        if not line:
            return
        if line.startswith("{"):
            try:
                data = json.loads(line)
            except json.JSONDecodeError:
                data = None
            if isinstance(data, dict) and data.get("status") == 1:
                with self.lock:
                    self.status = data
                    self.status_time = time.time()
                    self._changed()
                return
        self._add_log(line)

    def snapshot(self, after_seq: int) -> tuple[int, dict]:
        with self.lock:
            now = time.time()
            return self.version, {
                "connected": self.connected,
                "port": self.port,
                "status": self.status,
                "statusAge": round(now - self.status_time, 1) if self.status else None,
                "log": [
                    {"seq": seq, "time": t, "text": text}
                    for seq, t, text in self.log
                    if seq > after_seq
                ],
            }


def create_app(link: CarLink) -> Starlette:
    page = resources.files("elegoo_robot").joinpath("static/index.html").read_text()

    async def index(request: Request) -> HTMLResponse:
        return HTMLResponse(page)

    async def events(request: Request) -> StreamingResponse:
        async def stream():
            last_version = -1
            last_seq = 0
            last_sent = 0.0
            while not await request.is_disconnected():
                version, snap = link.snapshot(last_seq)
                # Send on every change, and at least once a second so the
                # page can show how old the last report is.
                if version != last_version or time.time() - last_sent >= 1.0:
                    if snap["log"]:
                        last_seq = snap["log"][-1]["seq"]
                    last_version = version
                    last_sent = time.time()
                    yield f"data: {json.dumps(snap)}\n\n"
                await asyncio.sleep(0.1)

        return StreamingResponse(
            stream(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-cache"},
        )

    return Starlette(routes=[Route("/", index), Route("/events", events)])


def main() -> None:
    parser = argparse.ArgumentParser(description="Live status dashboard for the robot car")
    parser.add_argument("--port", help="serial port (default: auto-detect)")
    parser.add_argument("--host", default="127.0.0.1", help="web server address (default: 127.0.0.1)")
    parser.add_argument("--http-port", type=int, default=8000, help="web server port (default: 8000)")
    args = parser.parse_args()

    link = CarLink(args.port)
    link.start()
    print(f"Dashboard: http://{args.host}:{args.http_port}")
    uvicorn.run(create_app(link), host=args.host, port=args.http_port, log_level="warning")


if __name__ == "__main__":
    main()
