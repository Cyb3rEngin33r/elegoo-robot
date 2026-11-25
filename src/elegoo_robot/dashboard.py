"""Live status dashboard for the Elegoo robot car.

Reads the car's status reports from its serial port and serves a web page
that shows them live. Run with:

    car-dashboard                  # auto-detects the car's USB serial port
    car-dashboard --port /dev/ttyUSB1
    car-dashboard --wifi           # over Wi-Fi via the camera module (elegoo-car.local)
    car-dashboard --wifi 192.168.1.50

Over Wi-Fi the page also shows the camera's live video. The camera serves
one viewer at a time, so the dashboard reads its stream and re-serves it
(/camera), and can look for people in it and follow one (see vision.py).
The person detector is downloaded once with:

    car-dashboard --get-model

The page can also drive the car (POST /command). Only the page itself can
send commands: requests must be JSON from the dashboard's own origin.

then open http://127.0.0.1:8000 in a browser.

Opening the USB serial port restarts the car's board (that's how Arduino USB
works), so any running mode stops when the dashboard connects. Connecting over
Wi-Fi doesn't restart it.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import socket
import threading
import time
from collections import deque
from importlib import resources

from pathlib import Path

import serial
import serial.tools.list_ports
import uvicorn
from starlette.applications import Starlette
from starlette.concurrency import run_in_threadpool
from starlette.requests import Request
from starlette.responses import HTMLResponse, JSONResponse, StreamingResponse
from starlette.routing import Route

from .camera import CameraRelay

BAUD = 115200
CH340_VID = 0x1A86  # USB-serial chip on the Elegoo UNO board
BOOT_WAIT_S = 2.5  # the board restarts when the port opens
STATUS_TIMEOUT_S = 3.0  # no report for this long: ask for reports again
LOG_LINES = 200
# Commands the page may send: drive, stop, speed, modes, camera head, head
# trim, collision guard.
ALLOWED_COMMANDS = set("wasdx+-of[]\\<>gqeku")
# Commands that move the car: these take over from person following.
DRIVE_COMMANDS = set("wasdxqeof")
COMMAND_GAP_S = 0.012  # the car ignores characters that arrive within 5 ms of each other
WIFI_HOST = "elegoo-car.local"
WIFI_PORT = 2323
CAMERA_STREAM = "http://{host}:81/stream"


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
        self.page_seen = 0.0  # last time a dashboard page was connected
        # Short gyro reports, 10 a second (see include/status.h).
        self.gyro: dict | None = None
        self.gyro_seq = 0
        self.log: deque[tuple[int, float, str]] = deque(maxlen=LOG_LINES)
        self.log_seq = 0
        self.version = 0  # bumped on every change, so streams know when to send
        self.ser: serial.SerialBase | None = None
        self.write_lock = threading.Lock()
        self.last_write = 0.0

    def start(self) -> None:
        threading.Thread(target=self._run, name="car-link", daemon=True).start()

    def send(self, data: bytes) -> bool:
        """Write to the car, one character at a time with a gap between them."""
        with self.write_lock:
            ser = self.ser
            if ser is None:
                return False
            try:
                for ch in data:
                    wait = COMMAND_GAP_S - (time.time() - self.last_write)
                    if wait > 0:
                        time.sleep(wait)
                    ser.write(bytes([ch]))
                    self.last_write = time.time()
            except (serial.SerialException, OSError):
                return False
            return True

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
                self.gyro = None
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
        # `port` is a device (/dev/ttyUSB1) or a pyserial URL (socket://host:port).
        over_wifi = port.startswith("socket://")
        with serial.serial_for_url(port, BAUD, timeout=0.2) as ser:
            sock = getattr(ser, "_socket", None)
            if sock is not None:
                # Send each character at once: commands like "L15" must reach
                # the car within 200 ms, and TCP would otherwise hold back
                # small writes while it waits for an acknowledgement.
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.ser = ser
            try:
                self._run_session(ser, port, over_wifi)
            finally:
                self.ser = None

    def _run_session(self, ser: serial.SerialBase, port: str, over_wifi: bool) -> None:
        self._set_connected(True, port)
        if over_wifi:
            self._add_log(f"[dashboard] connected to {port}")
        else:
            self._add_log(f"[dashboard] connected to {port}, waiting for the car to start")
            time.sleep(BOOT_WAIT_S)
        self.send(b"j")
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
                self.send(b"j")
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
            if isinstance(data, dict) and data.get("gyro") == 1:
                with self.lock:
                    self.gyro_seq += 1
                    self.gyro = {**data, "seq": self.gyro_seq, "time": time.time()}
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
                "gyro": self.gyro,
                "log": [
                    {"seq": seq, "time": t, "text": text}
                    for seq, t, text in self.log
                    if seq > after_seq
                ],
            }


def same_origin_json(request: Request) -> JSONResponse | None:
    """Only the dashboard's own page may control the car. Requiring JSON makes
    browsers preflight cross-site requests (which we don't allow), and the
    Origin check rejects any other site outright."""
    origin = request.headers.get("origin")
    if origin is not None and origin != f"http://{request.headers.get('host')}":
        return JSONResponse({"ok": False, "error": "forbidden origin"}, status_code=403)
    if request.headers.get("content-type", "").split(";")[0].strip() != "application/json":
        return JSONResponse({"ok": False, "error": "expected JSON"}, status_code=415)
    return None


def create_app(link: CarLink, relay: CameraRelay | None = None, vision=None) -> Starlette:
    page = resources.files("elegoo_robot").joinpath("static/index.html").read_text()

    async def index(request: Request) -> HTMLResponse:
        return HTMLResponse(page)

    async def events(request: Request) -> StreamingResponse:
        async def stream():
            last_version = -1
            last_seq = 0
            last_sent = 0.0
            while not await request.is_disconnected():
                link.page_seen = time.time()
                version, snap = link.snapshot(last_seq)
                if vision is not None:
                    snap["vision"] = vision.snapshot()
                    version = (version, vision.version)
                # Send on every change, and at least once a second so the
                # page can show how old the last report is.
                if version != last_version or time.time() - last_sent >= 1.0:
                    if snap["log"]:
                        last_seq = snap["log"][-1]["seq"]
                    last_version = version
                    last_sent = time.time()
                    yield f"data: {json.dumps(snap)}\n\n"
                await asyncio.sleep(0.05)  # gyro reports come 10 a second

        return StreamingResponse(
            stream(),
            media_type="text/event-stream",
            headers={"Cache-Control": "no-cache"},
        )

    async def config(request: Request) -> JSONResponse:
        return JSONResponse({"camera": "/camera" if relay else None, "vision": vision is not None})

    async def camera(request: Request) -> StreamingResponse:
        # The car's JPEGs, re-served as MJPEG to any number of pages.
        async def frames():
            seq = 0
            while not await request.is_disconnected():
                relay.want()
                new_seq, jpeg, _ = relay.latest()
                if jpeg is None or new_seq == seq:
                    await asyncio.sleep(0.02)
                    continue
                seq = new_seq
                yield (b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                       + str(len(jpeg)).encode() + b"\r\n\r\n" + jpeg + b"\r\n")

        if relay is None:
            return JSONResponse({"error": "no camera"}, status_code=404)
        return StreamingResponse(
            frames(), media_type="multipart/x-mixed-replace; boundary=frame",
            headers={"Cache-Control": "no-store"},
        )

    async def vision_control(request: Request) -> JSONResponse:
        if (bad := same_origin_json(request)) is not None:
            return bad
        if vision is None:
            return JSONResponse({"ok": False, "error": "no camera"}, status_code=404)
        try:
            body = await request.json()
        except json.JSONDecodeError:
            body = None
        if not isinstance(body, dict):
            return JSONResponse({"ok": False, "error": "expected an object"}, status_code=400)
        if isinstance(body.get("detect"), bool):
            vision.set_detecting(body["detect"])
        if isinstance(body.get("follow"), bool):
            if body["follow"] and not link.connected:
                return JSONResponse({"ok": False, "error": "car not connected"}, status_code=503)
            if (err := vision.set_following(body["follow"])) is not None:
                return JSONResponse({"ok": False, "error": err}, status_code=409)
        return JSONResponse({"ok": True})

    async def command(request: Request) -> JSONResponse:
        if (bad := same_origin_json(request)) is not None:
            return bad
        try:
            cmd = (await request.json()).get("cmd")
        except (json.JSONDecodeError, AttributeError):
            cmd = None
        if not isinstance(cmd, str) or len(cmd) != 1 or cmd not in ALLOWED_COMMANDS:
            return JSONResponse({"ok": False, "error": "unknown command"}, status_code=400)
        if vision is not None and cmd in DRIVE_COMMANDS:
            vision.manual_override()
        if not await run_in_threadpool(link.send, cmd.encode()):
            return JSONResponse({"ok": False, "error": "car not connected"}, status_code=503)
        return JSONResponse({"ok": True})

    return Starlette(routes=[
        Route("/", index),
        Route("/events", events),
        Route("/config", config),
        Route("/command", command, methods=["POST"]),
        Route("/camera", camera),
        Route("/vision", vision_control, methods=["POST"]),
    ])


def main() -> None:
    parser = argparse.ArgumentParser(description="Live status dashboard for the robot car")
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--port", help="USB serial port (default: auto-detect)")
    where.add_argument(
        "--wifi", nargs="?", const=WIFI_HOST, metavar="HOST",
        help=f"connect over Wi-Fi via the camera module (default host: {WIFI_HOST})",
    )
    parser.add_argument(
        "--camera", metavar="URL",
        help="camera stream URL (default with --wifi: http://HOST:81/stream; 'none' to hide)",
    )
    parser.add_argument("--model", type=Path, help="person detector (default: models/yolox_s.onnx)")
    parser.add_argument("--get-model", action="store_true", help="download the person detector and exit")
    parser.add_argument("--host", default="127.0.0.1", help="web server address (default: 127.0.0.1)")
    parser.add_argument("--http-port", type=int, default=8000, help="web server port (default: 8000)")
    args = parser.parse_args()
    if args.get_model:
        from .vision import DEFAULT_MODEL, download_model
        print(f"Saved {download_model(args.model or DEFAULT_MODEL)}")
        return

    port = f"socket://{args.wifi}:{WIFI_PORT}" if args.wifi else args.port
    camera_url = args.camera or (CAMERA_STREAM.format(host=args.wifi) if args.wifi else None)
    if camera_url == "none":
        camera_url = None
    link = CarLink(port)
    link.start()
    relay = vision = None
    if camera_url:
        relay = CameraRelay(camera_url)
        relay.start()
        from .vision import DEFAULT_MODEL, Vision
        vision = Vision(relay, link, args.model or DEFAULT_MODEL)
        vision.start()
    print(f"Dashboard: http://{args.host}:{args.http_port}")
    # Open pages keep their live streams open; don't wait for them on Ctrl+C.
    uvicorn.run(create_app(link, relay, vision), host=args.host, port=args.http_port,
                log_level="warning", timeout_graceful_shutdown=2)


if __name__ == "__main__":
    main()
