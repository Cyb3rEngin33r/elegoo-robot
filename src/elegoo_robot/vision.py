"""Person detection and following, on the laptop.

Frames come from the car's camera (via CameraRelay); a small neural network
(YOLOX-S from the OpenCV model zoo, ~60 ms a frame on a laptop CPU) finds
people. When following, the car turns to face the chosen person and drives
toward them, stopping at a set distance, using the car's "M" command (left
and right speed levels, 0.4 s per command, so it stops by itself if the
laptop goes quiet). The car's own protections (collision guard, bump, lift
and battery) still apply to every command.

Following stops when the person is lost for a while, on any manual drive
command from the page, or when no dashboard page is open.
"""

from __future__ import annotations

import hashlib
import threading
import time
import urllib.request
from pathlib import Path

import cv2
import numpy as np

from .camera import CameraRelay

MODEL_URL = (
    "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/"
    "object_detection_yolox/object_detection_yolox_2022nov.onnx"
)
MODEL_SHA256 = "c5c2d13e59ae883e6af3b45daea64af4833a4951c92d116ec270d9ddbe998063"
DEFAULT_MODEL = Path(__file__).resolve().parents[2] / "models" / "yolox_s.onnx"

# Detector input: the camera's 4:3 frame scaled to 416 x 320 (multiples of 32).
INPUT_W, INPUT_H = 416, 320
STRIDES = (8, 16, 32)
PERSON = 0              # COCO class 0
MIN_SCORE = 0.45
NMS_IOU = 0.45

# ---- Following ----
# The car spins fast (a 0.4 s spin turns it 50-100 degrees) and the video
# arrives ~0.1-0.3 s late, so steering by "spin until centred" overshoots.
# Instead the laptop works out the angle to the person and the car turns
# exactly that far with its gyro ("L"/"R" + degrees), then the laptop waits
# for video taken after the turn before deciding again. Between turns the
# car drives straight, holding its heading with the gyro ("M77").
# The camera runs at 320x240 while detecting: at 640x480 the Wi-Fi link only
# managed ~7 frames/s, with delays up to a second; 320x240 gives ~20.
HFOV_DEG = 125.0        # camera's horizontal field of view: wide-angle lens (measured 121-136)
MIRRORED = False        # True if the image is mirrored left-right
TURN_MIN_DEG = 7        # further off than this: turn to face them
TURN_MIN_CLOSE_DEG = 15 # ...but once close, only for bigger moves (no twitching)
TURN_MAX_DEG = 60
FRESH_VIDEO_S = 0.25    # after a turn ends, ignore video older than this
TURN_TIMEOUT_S = 4.0
# Close enough: stop. With the wide lens, a person 60 cm away fills the
# frame and is only found half the time, so stop at about 1 m: by the
# ultrasonic distance, or (if the sound misses their legs) by box width
# (~85 px of 320 at 95 cm, ~110 at 75 cm, the whole frame at 60 cm).
STOP_CM = 100
CLOSE_WIDTH = 0.33      # box wider than this fraction of the frame
LOST_S = 1.0            # no person this long: stop and wait
PAGE_GONE_S = 3.0       # no dashboard page this long: stop following
DETECT_FRAMESIZE = "qvga"
VIEW_FRAMESIZE = "vga"
TRACK_IOU = 0.2         # keep following the same person between frames


def download_model(path: Path = DEFAULT_MODEL) -> Path:
    """Fetch the detector from the OpenCV model zoo and check its hash."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".part")
    print(f"Downloading the person detector (36 MB) to {path} ...")
    with urllib.request.urlopen(MODEL_URL, timeout=60) as resp, open(tmp, "wb") as f:
        while chunk := resp.read(1 << 20):
            f.write(chunk)
    digest = hashlib.sha256(tmp.read_bytes()).hexdigest()
    if digest != MODEL_SHA256:
        tmp.unlink()
        raise RuntimeError(f"downloaded model has the wrong checksum ({digest})")
    tmp.replace(path)
    return path


class PersonDetector:
    def __init__(self, model: Path) -> None:
        self.net = cv2.dnn.readNet(str(model))
        grids, strides = [], []
        for s in STRIDES:
            xv, yv = np.meshgrid(np.arange(INPUT_W // s), np.arange(INPUT_H // s))
            g = np.stack((xv, yv), 2).reshape(-1, 2)
            grids.append(g)
            strides.append(np.full((len(g), 1), s))
        self.grids = np.concatenate(grids).astype(np.float32)
        self.strides = np.concatenate(strides).astype(np.float32)

    def detect(self, bgr: np.ndarray) -> list[tuple[float, float, float, float, float]]:
        """People in the frame: (x, y, w, h, score) in frame pixels."""
        h, w = bgr.shape[:2]
        r = min(INPUT_W / w, INPUT_H / h)
        sw, sh = int(w * r), int(h * r)
        pad = np.full((INPUT_H, INPUT_W, 3), 114.0, np.float32)
        pad[:sh, :sw] = cv2.resize(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), (sw, sh))
        self.net.setInput(np.transpose(pad, (2, 0, 1))[None])
        d = self.net.forward()[0]
        score = d[:, 4] * d[:, 5 + PERSON]
        keep = score > MIN_SCORE
        if not keep.any():
            return []
        d, score = d[keep], score[keep]
        xy = (d[:, :2] + self.grids[keep]) * self.strides[keep] / r
        wh = np.exp(d[:, 2:4]) * self.strides[keep] / r
        boxes = np.concatenate([xy - wh / 2, wh], axis=1)
        idx = cv2.dnn.NMSBoxes(boxes.tolist(), score.tolist(), MIN_SCORE, NMS_IOU)
        return [(*map(float, boxes[i]), float(score[i])) for i in np.array(idx).flatten()]


def iou(a, b) -> float:
    ax2, ay2, bx2, by2 = a[0] + a[2], a[1] + a[3], b[0] + b[2], b[1] + b[3]
    iw = max(0.0, min(ax2, bx2) - max(a[0], b[0]))
    ih = max(0.0, min(ay2, by2) - max(a[1], b[1]))
    inter = iw * ih
    return inter / (a[2] * a[3] + b[2] * b[3] - inter + 1e-9)


FORWARD = b"M77"  # both sides at speed level 7 (150): the car holds its heading


def turn_command(degrees: int) -> bytes:
    """Exact gyro turn; positive = left."""
    d = max(1, min(abs(degrees), TURN_MAX_DEG))
    return (b"L" if degrees > 0 else b"R") + f"{d:02d}".encode()


class Vision:
    """Runs detection (and following) in a background thread."""

    def __init__(self, relay: CameraRelay, link, model: Path = DEFAULT_MODEL) -> None:
        self.relay = relay
        self.link = link          # CarLink: send(), status, page_seen
        self.model = model
        self.lock = threading.Lock()
        self.detecting = False
        self.following = False
        self.version = 0
        self.state: dict = {"persons": [], "target": None, "action": "", "fps": 0.0, "size": None}
        self.error: str | None = None
        self._detector: PersonDetector | None = None
        self._target_box = None
        self._last_seen = 0.0
        self._last_cmd = b""
        self._turn_sent = 0.0     # when the last gyro turn was sent (0 = none)
        self._turn_done = 0.0
        # Commands go out from one thread, newest first: a slow send never
        # delays detection, and an old command can't overtake a newer one.
        self._outbox: bytes | None = None
        self._outbox_ready = threading.Condition()

    def start(self) -> None:
        self._set_framesize(VIEW_FRAMESIZE)  # in case an earlier run left it small
        threading.Thread(target=self._run, name="vision", daemon=True).start()
        threading.Thread(target=self._sender, name="vision-send", daemon=True).start()

    # ---- Controls (from the web page) ----
    def set_detecting(self, on: bool) -> None:
        with self.lock:
            if on != self.detecting:
                self._set_framesize(DETECT_FRAMESIZE if on else VIEW_FRAMESIZE)
            self.detecting = on
            if not on:
                self._stop_following("detection off")
            self._changed()

    def set_following(self, on: bool) -> str | None:
        """Returns an error message if following can't start."""
        with self.lock:
            if on:
                if not self.model.exists():
                    return "person detector not downloaded (see README)"
                if not self.detecting:
                    self._set_framesize(DETECT_FRAMESIZE)
                self.detecting = True
                self.following = True
                self._turn_sent = self._turn_done = 0.0
                self._last_cmd = b""
                self._target_box = None
                self._last_seen = time.time()
                self.state["action"] = "looking for a person"
            else:
                self._stop_following("stopped")
            self._changed()
        return None

    def manual_override(self) -> None:
        """A drive command from the page: the person at the keys wins."""
        with self.lock:
            if self.following:
                self._stop_following("manual control")
                self._changed()

    def snapshot(self) -> dict:
        with self.lock:
            return {**self.state, "detecting": self.detecting, "following": self.following,
                    "error": self.error, "model": self.model.exists()}

    # ---- Internals ----
    def _set_framesize(self, size: str) -> None:
        # The camera's /control page is on port 80 of the same host.
        url = self.relay.url.split("://", 1)[1].split("/", 1)[0].split(":")[0]
        url = f"http://{url}/control?framesize={size}"

        def go():
            try:
                urllib.request.urlopen(url, timeout=5).read()
            except OSError as e:
                self.error = f"couldn't set the camera's frame size: {e}"
        threading.Thread(target=go, daemon=True).start()

    def _changed(self) -> None:
        self.version += 1

    def _stop_following(self, why: str) -> None:
        if self.following:
            self.following = False
            self.state["action"] = f"follow off: {why}"
            self._post(b"x")

    def _run(self) -> None:
        seq = 0
        while True:
            with self.lock:
                active = self.detecting
            if not active:
                time.sleep(0.1)
                continue
            if self._detector is None:
                try:
                    self._detector = PersonDetector(self.model)
                    self.error = None
                except cv2.error as e:
                    with self.lock:
                        self.error = f"can't load the person detector: {e}"
                        self.detecting = self.following = False
                        self._changed()
                    continue
            self.relay.want()
            seq, jpeg, frame_time = self.relay.wait_newer(seq, 1.0)
            if jpeg is None:
                continue
            frame = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
            if frame is None:
                continue
            t0 = time.time()
            persons = self._detector.detect(frame)
            dt = time.time() - t0
            with self.lock:
                self._update(frame.shape, persons, dt, frame_time)

    def _update(self, shape, persons, dt: float, frame_time: float) -> None:
        h, w = shape[:2]
        target = self._pick_target(persons)
        fps = self.state["fps"]
        self.state.update(
            persons=[[round(v, 1) for v in p] for p in persons],
            target=target,
            size=[w, h],
            fps=round(0.8 * fps + 0.2 / max(dt, 1e-3), 1) if fps else round(1 / max(dt, 1e-3), 1),
        )
        if self.following and time.time() - self.link.page_seen > PAGE_GONE_S:
            self._stop_following("no dashboard page open")
        if self.following:
            self._follow(persons, target, w, h, frame_time)
        self._changed()

    def _pick_target(self, persons) -> int | None:
        if not persons:
            return None
        if self._target_box is not None:
            best = max(range(len(persons)), key=lambda i: iou(persons[i], self._target_box))
            if iou(persons[best], self._target_box) >= TRACK_IOU:
                self._target_box = persons[best]
                return best
        # New target: the biggest (usually the nearest) person.
        best = max(range(len(persons)), key=lambda i: persons[i][2] * persons[i][3])
        self._target_box = persons[best]
        return best

    def _follow(self, persons, target, w: int, h: int, frame_time: float) -> None:
        now = time.time()
        status = self.link.status or {}
        if self._turn_sent:
            # Wait for the turn to finish, then for video taken after it.
            if now - self._turn_sent > TURN_TIMEOUT_S:
                self._turn_sent = 0.0
            elif now - self._turn_sent < 0.3 or status.get("steer") == "turning":
                return
            else:
                if not self._turn_done:
                    self._turn_done = now
                if frame_time < self._turn_done + FRESH_VIDEO_S:
                    return
                self._turn_sent = self._turn_done = 0.0
                self._last_seen = now
        if target is None:
            if now - self._last_seen > LOST_S:
                self._target_box = None
                self._send(b"x", "lost the person: waiting")
            return
        self._last_seen = now
        x, y, bw, bh, _ = persons[target]
        offset = ((x + bw / 2) / w - 0.5) * 2   # -1 left edge .. 1 right edge
        if MIRRORED:
            offset = -offset
        bearing = -offset * HFOV_DEG / 2          # degrees, left positive
        dist = status.get("dist")
        close = (dist is not None and dist < STOP_CM) or bw / w > CLOSE_WIDTH
        if abs(bearing) > (TURN_MIN_CLOSE_DEG if close else TURN_MIN_DEG):
            deg = round(bearing)
            self._send(turn_command(deg), f"turning {abs(deg)}° {'left' if deg > 0 else 'right'} to face them")
            self._turn_sent = now
            self._turn_done = 0.0
        elif close:
            self._send(b"x", f"close enough{f' ({dist} cm)' if dist is not None else ''}: waiting")
        else:
            self._send(FORWARD, f"following{f' ({dist} cm away)' if dist is not None else ''}")

    def _send(self, cmd: bytes, action: str) -> None:
        self.state["action"] = action
        # Drive commands are repeated every frame (they last 0.4 s on the
        # car); a stop is sent once.
        if cmd != FORWARD and cmd == self._last_cmd and not cmd.startswith((b"L", b"R")):
            return
        self._post(cmd)

    def _post(self, cmd: bytes) -> None:
        self._last_cmd = cmd
        with self._outbox_ready:
            self._outbox = cmd
            self._outbox_ready.notify()

    def _sender(self) -> None:
        while True:
            with self._outbox_ready:
                self._outbox_ready.wait_for(lambda: self._outbox is not None)
                cmd, self._outbox = self._outbox, None
            self.link.send(cmd)
