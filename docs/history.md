# Project history

How the project grew, and what was learned on the real car along the way.
The README says how things work now; this file records why, including the
measurements behind the numbers and the dead ends that cost time.
`docs/conversation-2026-11-15.md` has the first day in more detail.

## Timeline

| Date | Commit | What |
|---|---|---|
| 2026-11-15 | `b2bbe19` | PlatformIO project for the UNO, pin maps for kits V3 and V4, serial test console |
| 2026-11-22 | `b9ee945` | Obstacle avoidance (non-blocking state machine) |
| | `917058f` | Status LED (WS2812) showing what avoidance is doing |
| | `573aaa4` | Line following |
| | `4325923` | IR remote control |
| | `6bb7a8e` | Live status dashboard (Python, Starlette, server-sent events) |
| | `e7c8696` | Firmware for the ESP32-S3 camera module: Wi-Fi serial bridge, OTA updates |
| | `98a934a` | Camera video on the dashboard |
| | `9ebee74` | Fixed IP for the camera module (192.168.68.20) |
| | `652b297` | Driving from the dashboard; camera-head trim saved in EEPROM |
| | `e426568` | Collision guard for manual driving |
| | (publish) | Pushed to GitHub (public); history rewritten to a noreply email, branch renamed to `main` |
| 2026-11-23 | `fa13c0a` | Gyro: straight-line hold and exact 90-degree turns |
| | `dd3b539` | Gyro on the dashboard: compass, readouts, heading graph |
| | `659b4cc` | Bump detection with the accelerometer |
| 2026-11-24 | `e8cf90f` | Tilt display; picked-up / tipped-over protection |
| | `66b9b2f` | Low-battery protection |
| | `6c3de93` | Person following, with vision on the laptop |

## Hardware facts found by testing

- **Kit:** Elegoo Smart Robot Car **V4.0**. The UNO shield has a TB6612
  motor driver and an **MPU6050** gyro/accelerometer at I2C 0x68 (newer
  shields use a QMI8658C; both are supported).
- **Gyro orientation:** turning left reads positive on Z (no sign flip). Hand
  turns gave conflicting answers; letting the motors spin the car settled it.
  Accelerometer: +Y is forward, X right, Z up.
- **Camera module:** an **ESP32-S3** (8 MB flash, 8 MB PSRAM), not a plain
  ESP32, with an **OV3660** sensor wired like Espressif's ESP32-S3-EYE. The
  pins were read out of Elegoo's original firmware (disassembled near the
  "Camera init failed" string). That firmware is backed up in `esp32/backup/`
  (not in git).
- **Link between the boards:** module RX = GPIO3, TX = GPIO40. GPIO43/44, the
  obvious guess, carry only the module's boot log. Found with the module's
  own pin tools (port 2324: `listen`, `findtx`).
- **Camera lens:** wide-angle, about **125 degrees** across (measured 121 and
  136 degrees by turning the car a known angle and watching a person's box
  move). The image is upside down by default (vflip on) and not mirrored.
- **Camera stream:** one viewer at a time. At 640x480 the Wi-Fi link gave
  ~7 frames/s with delays up to a second; at 320x240, ~20 frames/s and under
  0.1 s.
- **Battery:** 2 x 18650, reads ~8.3 V full on Elegoo's divider formula
  (A3 x 0.0375 x 1.08); drops ~1 V while the motors run.
- **Spin speed:** high. A 0.4 s spin at power 150 turns the car ~100 degrees
  on a hard floor; even power 80 turns ~50 degrees. 90-degree gyro turns land
  within about 2 degrees.
- **Jolts:** normal driving stays under ~2 g fore-aft (0.5 g typical);
  driving into a wall at speed 150 gives 5-8 g. Hence the 3 g bump threshold.
- **Line sensors:** ~40-55 on the floor, ~1020 when the car is lifted.
- **Person detection** (YOLOX-S, laptop CPU): ~60 ms a frame at 416x320,
  ~12-15 frames/s overall. At 60 cm a person fills the wide-angle frame and
  is only found half the time, so following stops at ~1 m.

## Gotchas (each one cost time)

- **Upload switch on the shield:** "upload" disconnects the camera module
  from the UNO's RX only. The car's messages still reach Wi-Fi, but nothing
  from Wi-Fi reaches the car. It must be on "Cam" for the dashboard to drive.
- **Battery switch off:** on USB power alone the motors get ~2 V, the car
  stalls, serial output turns to garbage and the UNO resets. It looked like
  a gyro fault at first. The firmware now refuses to drive with the battery
  off.
- **Servo buzz:** the head servo buzzes while holding position, and the gyro
  feels it through the chassis (4x the noise). The servo signal is now
  switched off 0.6 s after each move.
- **Gyro zero point:** measured at power-up; moving the car then spoils it.
  Calibration now waits until the car is still.
- **ESP32-S3 flashing:** needs a USB-A to USB-C data cable (C-to-C and
  charge-only cables didn't work). First flash: hold BOOT, tap RST, release
  BOOT. After flashing, press RST, or it may sit in download mode.
- **Wi-Fi:** the module was unreachable until `WiFi.setSleep(false)`. mDNS
  (`elegoo-car.local`) is unreliable on the Deco mesh, hence the fixed IP,
  which sits outside the Deco's DHCP range (.50-.250).
- **TCP batching:** multi-character commands (`L15`, `M77`) must reach the
  car within 200 ms; TCP's Nagle algorithm sometimes held the digits back.
  The dashboard sets TCP_NODELAY.
- **Dashboard restarts:** the page is read at start-up, so page changes need
  a restart. Open pages kept the old server from exiting (fixed: 2 s limit),
  and a stuck old server holds the car's only Wi-Fi link.
- **`pkill -f` / `pio device monitor`** from scripts: `pkill -f` can match
  its own shell, and the monitor needs a real terminal. Use pyserial
  scripts and kill by PID.
- **Serial noise:** the camera module's boot messages and `{Heartbeat}`
  reached the car as text and its letters were taken as commands. Fixed
  with the burst filter (a character counts only if isolated by 5 ms).

## Decisions worth remembering

- **Single-character commands** with a burst filter, rather than a line
  protocol, so a terminal, the IR remote and the dashboard all work the
  same way. Two-digit commands (`M`, `L`, `R`) were added later for programs.
- **Drive commands expire after 0.4 s**, and the dashboard repeats them every
  0.2 s, so the car stops by itself if the page, browser or Wi-Fi goes away.
- **Safety lives on the car** (guard, bump, lift, battery), so it applies to
  every source of commands, including the laptop's person follower.
- **Vision runs on the laptop.** The UNO's flash is ~92% full and the
  ESP32-S3 is busy streaming; the laptop has the CPU.
- **Secrets** (`esp32/include/secrets.h`) and the firmware backup stay out of
  git; a pre-commit hook in `.githooks/` enforces it.
