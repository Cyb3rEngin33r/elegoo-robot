# Notes for Claude Code

Firmware and tools for an Elegoo Smart Robot Car V4.0: an Arduino UNO
(`src/`, `include/`), an ESP32-S3 camera module (`esp32/`) and a Python
dashboard (`src/elegoo_robot/`). Read `README.md` for how things work and
`docs/history.md` for hardware facts and past pitfalls before changing
anything hardware-related.

## Working with the owner

- Every change is tested on the real car before it's committed. Work on a
  feature branch; when the owner says "commit and merge", commit there,
  fast-forward `main`, delete the branch. Push only when asked.
- Flashing the UNO needs the owner to plug in USB and set the shield's switch
  to "upload". Ask, then wait for "go ahead". Afterwards they switch back to
  "Cam" for Wi-Fi.
- Moving tests need the owner's say-so (wheels up, car on the floor, room to
  move). Say what the car will do before doing it.
- Commits use the GitHub noreply email (set in this repo's git config). The
  repo is public: never commit `esp32/include/secrets.h`, `esp32/backup/`,
  `models/` or `.pio/`, and never print the Wi-Fi or OTA passwords. Read the
  OTA password into an environment variable without echoing it.

## Commands

```bash
pio run                                   # build the UNO firmware
pio run -t upload --upload-port /dev/ttyUSB0
cd esp32 && pio run -e ota -t upload      # camera module over Wi-Fi (needs CAR_OTA_PASSWORD)
.venv/bin/car-dashboard --wifi 192.168.68.20   # dashboard at http://127.0.0.1:8000
.venv/bin/car-dashboard --get-model       # person detector, once per machine
```

- The UNO's flash is ~92% full: check `pio run` sizes after changes.
- Opening the USB serial port resets the UNO; the gyro then recalibrates, so
  the car should be still.
- The dashboard reads its page at start-up: restart it after editing
  `static/index.html`. It holds the car's only Wi-Fi bridge connection, so
  stop it before talking to port 2323 directly. Stop it by PID, not
  `pkill -f`.
- Status JSON from the car is documented in `include/status.h`.
