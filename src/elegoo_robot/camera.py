"""Shares the car's camera stream.

The camera module serves its MJPEG stream to one viewer at a time, so the
dashboard reads it once here and hands the frames to the browser (re-served
from the dashboard) and to the person detector. Frames are passed on as the
car's JPEGs, without re-encoding.

The stream is only read while something wants frames, so the camera is free
for other viewers when the dashboard isn't using it.
"""

from __future__ import annotations

import threading
import time
import urllib.request

IDLE_S = 5.0          # nobody wanted frames this long: disconnect
RETRY_S = 2.0         # after a failed connection
READ_TIMEOUT_S = 5.0
MAX_FRAME_BYTES = 2_000_000


class CameraRelay:
    def __init__(self, url: str) -> None:
        self.url = url
        self.cond = threading.Condition()
        self.jpeg: bytes | None = None
        self.seq = 0
        self.frame_time = 0.0
        self.error: str | None = None
        self._wanted_at = 0.0

    def start(self) -> None:
        threading.Thread(target=self._run, name="camera-relay", daemon=True).start()

    def want(self) -> None:
        """Call regularly while frames are needed."""
        with self.cond:
            self._wanted_at = time.time()
            self.cond.notify_all()

    def latest(self) -> tuple[int, bytes | None, float]:
        with self.cond:
            return self.seq, self.jpeg, self.frame_time

    def wait_newer(self, seq: int, timeout: float) -> tuple[int, bytes | None, float]:
        """Wait (in a thread) for a frame newer than `seq`."""
        with self.cond:
            self.cond.wait_for(lambda: self.seq != seq, timeout)
            return self.seq, self.jpeg, self.frame_time

    def _wanted(self) -> bool:
        return time.time() - self._wanted_at < IDLE_S

    def _run(self) -> None:
        while True:
            with self.cond:
                self.cond.wait_for(self._wanted)
            try:
                self._read_stream()
            except (OSError, ValueError) as e:
                self.error = str(e)
                time.sleep(RETRY_S)

    def _read_stream(self) -> None:
        with urllib.request.urlopen(self.url, timeout=READ_TIMEOUT_S) as resp:
            self.error = None
            while self._wanted():
                # Each part: "--boundary", headers, a blank line, the JPEG.
                length = None
                while True:
                    line = resp.readline()
                    if not line:
                        raise OSError("camera stream ended")
                    line = line.strip()
                    if line.lower().startswith(b"content-length:"):
                        length = int(line.split(b":", 1)[1])
                    elif not line and length is not None:
                        break
                if not 0 < length <= MAX_FRAME_BYTES:
                    raise ValueError(f"bad frame size {length}")
                jpeg = resp.read(length)
                if len(jpeg) != length:
                    raise OSError("camera stream ended mid-frame")
                with self.cond:
                    self.jpeg = jpeg
                    self.seq += 1
                    self.frame_time = time.time()
                    self.cond.notify_all()
