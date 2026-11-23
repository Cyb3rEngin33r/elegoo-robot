# Elegoo Smart Robot Car (UNO R3)

PlatformIO firmware for the Elegoo Smart Robot Car kit. It starts as a serial
test console that checks the motors, ultrasonic sensor, pan servo and line
tracker one at a time.

## Layout

```
platformio.ini      board config; set KIT_VERSION (4 = V4.0/TB6612, 3 = V3.0/L298N)
include/pins.h      pin maps for both kit versions
src/motors.cpp      differential drive, speeds -255..255
src/sensors.cpp     ultrasonic distance, line tracker, pan servo
src/avoid.cpp       obstacle avoidance (tuning constants at the top)
src/guard.cpp       collision guard for manual driving
src/imu.cpp         gyro on the shield (MPU6050 or QMI8658C): heading
src/steer.cpp       gyro straight-line driving and exact turns (tuning at the top)
src/follow.cpp      line following (thresholds and speeds at the top)
src/led.cpp         status LED (V4 shield RGB LED)
src/remote.cpp      IR remote (button codes at the top)
src/status.cpp      JSON status reports for the dashboard
src/elegoo_robot/   Python status dashboard (car-dashboard)
src/main.cpp        serial test console
esp32/              camera module (ESP32-S3) firmware: Wi-Fi serial bridge + video
.githooks/          pre-commit hook that blocks Wi-Fi credentials
```

## Setup (Linux)

```bash
pipx install platformio            # or: python3 -m pip install --user platformio
sudo usermod -aG dialout $USER     # serial port access, then log out and back in
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
git config core.hooksPath .githooks   # once per clone: blocks committing Wi-Fi secrets
```

## Build, flash, test

```bash
pio run                      # compile
pio run -t upload            # flash (USB cable plugged in)
pio device monitor           # 115200 baud serial console
```

Turn the car's battery switch on for the motors to run (USB alone won't power them).
On V4.0 kits, set the shield's upload switch to the upload position while flashing
(it disconnects the camera module from RX/TX), then switch it back to "Cam".

Console commands: `w s a d` drive, `x` stop, `q e` turn exactly 90° left/right, `k` straight-line hold on/off, `+ -` speed, `p` ping,
`l` line sensors, `[ ] \` servo, `< >` servo centre trim (1° left/right, saved in EEPROM), `g` collision guard on/off, `o` obstacle avoidance on/off, `f` line following on/off, `j`/`J` status reports on/off, `h` help.
Only isolated characters count as commands: a character is acted on once the
line has been quiet for 5 ms before and after it, so bursts of text (like the
camera module's `{Heartbeat}`) can't be mistaken for commands.

Shield push button (V4): **short press** starts obstacle avoidance, **hold 1 s**
starts line following, and any press while a mode is running stops it.

## Obstacle avoidance

The car never drives on power-up. Short-press the shield's push button (or send
`o`) to start; it waits 1 s, then drives forward, slows down under 50 cm, and under
25 cm stops, looks left and right, and turns toward the clearer side. Press the
button again (or send `o`, `x` or any drive key) to stop. Each decision is
printed to the serial console, so you can test it on the desk with the wheels up
and then unplug USB and run it on the floor.

On V4 kits the shield's RGB LED shows what it's doing:

| Colour | Meaning |
|---|---|
| Blue | idle, waiting for the button or `o` |
| White | starting (1 s delay) |
| Green | driving forward |
| Yellow | looking around or turning away |
| Red | boxed in, backing up |

If one side spins backwards, flip `INVERT_LEFT` / `INVERT_RIGHT` in `include/motors.h`.

## Line following

Follows a dark tape line (black electrical tape works well) on a light floor.
Put the car with the tape under its middle, then hold the button for 1 s (or
send `f`). It drives straight while only the middle sensor sees the line, curves
gently when a side sensor also sees it, and spins when only a side sensor does.
If it loses the line it spins toward where it last saw it, and stops after 2 s
without finding it. It also stops if it's picked up.

The thresholds in `src/follow.cpp` were calibrated on one floor and tape
(floor reads about 40, tape about 700 to 900, lifted about 1020). On a different
surface, check the readings with `l` and adjust `LINE_THRESHOLD`.

## Gyro: straight lines and exact turns

The V4 shield carries a gyro (an MPU6050 at I2C 0x68 on this car; newer
shields use a QMI8658C, which is also supported). The car uses it to know
which way it points:

- **Straight lines.** Forward and reverse drives (`w`/`s`, the remote's ▲▼,
  the dashboard) hold the heading the car had when the drive started, speeding
  one side up a little to cancel drift. A held key keeps the same heading.
  `k` turns this off, to compare.
- **Exact turns.** `q`/`e` (dashboard `Ctrl`+`←`/`→`, remote 4/6) spin
  90° on the spot, slowing near the end and nudging back if they overshoot;
  within about 2° on carpet. If the car stops rotating (a wheel stalls on
  carpet), the power goes up step by step; a turn gives up after 6 s.

**Keep the car still for a moment at power-up**: the gyro measures its zero
point then, waiting (up to 5 s) until the car is still. It keeps the zero
point up to date whenever the car stands still. The head servo buzzes while
it holds a position, which the gyro feels, so the servo's signal is switched
off 0.6 s after each head move; its gears hold the head.

Status reports include `heading` (degrees since power-up, left positive,
-180..180), `steer` and `imuErr` (failed reads, gyro restarts). If the gyro
isn't found, the I2C devices present are printed at start-up.

A flat or switched-off battery shows up as stalls: on USB power alone the
motors get about 2 V and the UNO can reset. Check `batt` (7-8.4 V is good).

## IR remote

The kit's IR remote works at any time:

| Button | Action |
|---|---|
| ▲ ▼ ◀ ▶ (hold) | drive forward / back / spin left / spin right; stops ~0.25 s after release |
| OK | stop everything |
| 1 | obstacle avoidance on/off |
| 2 | line following on/off |
| 4 / 6 | turn exactly 90° left / right (gyro) |
| * / # | slower / faster |

Like the shield button, 1 or 2 stops whichever mode is running. Buttons that
aren't recognised are printed on the serial console (`IR: unknown ...`), so
codes for another remote can be added to `src/remote.cpp`.

## Status dashboard

A live web page showing the car's mode, what it's doing, distance ahead, line
sensors, motor speeds, battery voltage and a log of its decisions. Over Wi-Fi it
also shows live video from the car's camera, and it can drive the car.

```bash
car-dashboard                       # over USB, auto-detects the car's port
car-dashboard --port /dev/ttyUSB1
car-dashboard --wifi 192.168.68.20  # over Wi-Fi via the camera module
car-dashboard --wifi                # same, using elegoo-car.local
```

Then open http://127.0.0.1:8000.

- Over USB, connecting restarts the car's board, so a running mode stops.
  Over Wi-Fi it doesn't.
- The video comes straight from the car to the browser, and the camera serves
  one viewer at a time: a second tab shows "Camera not reachable" until the
  first one pauses or closes. `--camera URL` overrides the stream address,
  `--camera none` hides it.
- `elegoo-car.local` doesn't always resolve on a mesh network; the IP address
  always works. The car uses a fixed address (192.168.68.20 here) set in
  `esp32/include/secrets.h`; see the Wi-Fi section.
- While the dashboard runs it holds the serial port: stop it before `pio run -t upload`.
- The battery voltage is approximate (Elegoo's divider formula). It reads about
  7.2 V on a charged pack, which is plausible, but it isn't checked with a meter.

**Driving from the dashboard** (the Drive panel, under the camera):

| Control | Keys | Action |
|---|---|---|
| ▲ ◀ ▶ ▼ (hold) | `W A S D` or arrows | drive while held, stop on release |
| STOP | `Space` / `Esc` | stop everything |
| Speed − / + | `-` / `+` | slower / faster |
| Look ◀ / Ahead / ▶ | `Shift`+`←` `→`, `Shift`+`↑` | turn / centre the camera head |
| Trim ◀ 1° / 1° ▶ | | nudge the head's centre (saved on the car) |
| Turn ⟲ 90° / 90° ⟳ | `Ctrl`+`←` `→` | exact 90° turn (gyro); shows the heading |
| Straight | `K` | gyro straight-line hold on/off |
| Guard | `G` | collision guard on/off (see below) |
| Obstacle avoidance / Line following | | start / stop a mode (green while running) |

Drive commands last 0.4 s on the car, and the page repeats them every 0.2 s
while held, so the car stops by itself if the page, browser or Wi-Fi goes
away. The page also sends a stop on release, on switching tabs and when the
window loses focus. The panel is disabled when no live status is coming in.
Only the dashboard's own page can send commands (JSON from its own origin),
and only drive, turn, mode, speed and head commands are accepted.

**Collision guard.** Manual forward driving (dashboard, IR remote or console)
stops when something is within the stopping distance (15 cm + speed/10: 30 cm
at the default speed 150, 40 cm at 255), and further forward commands are
refused while it's there. Reverse and spins always work, so the car can back
out. The distance sensor sits on the camera head, so the guard pauses while
the head is turned more than 15° from centre. It's on at power-up; `g` toggles
it. Soft or angled things (fabric, chair legs) can fool the ultrasonic sensor.

The car sends one JSON line every 250 ms after `j` (see `include/status.h`);
the dashboard sends `j` itself and asks again if the reports stop.

## Wi-Fi (camera module)

Newer V4.0 kits have an **ESP32-S3** camera module (older ones have an ESP32).
`esp32/` replaces Elegoo's firmware with a Wi-Fi serial bridge: the module joins
your home Wi-Fi as `elegoo-car`, passes the car's serial link to TCP port 2323
(characters to the car are paced 10 ms apart, so the car's burst filter sees
each command on its own)
so the dashboard (or anything else) can talk to the car without a cable, and
streams video from the camera. Elegoo's phone app no longer works.

**Setup**

1. `cp esp32/include/secrets.h.example esp32/include/secrets.h` and fill in your
   2.4 GHz Wi-Fi name, password, and a password for wireless updates. The file
   is in `.gitignore` and the pre-commit hook refuses to commit it.
   Optionally set `STATIC_IP` (plus gateway, subnet and DNS) for a fixed
   address; pick one outside the router's DHCP range (here the Deco hands out
   .50-.250, so the car uses .20). Without it the module
   gets an address by DHCP.
2. Take the module off the car and connect its USB-C port to the laptop with a
   **USB-A to USB-C data cable** (C-to-C cables don't power it). Put it in
   download mode: hold BOOT, tap RST, release BOOT.
3. `cd esp32 && pio run -e usb -t upload`, then press RST.
4. Put the module back, set the upload switch to "Cam", battery on.

Later updates go over Wi-Fi with the module on the car:

```bash
cd esp32
read -rs CAR_OTA_PASSWORD && export CAR_OTA_PASSWORD   # typed, not shown or saved
pio run -e ota -t upload --upload-port 192.168.68.20
```

**Link pins.** Elegoo doesn't document them; on this module they were found
with the firmware's tools: the car's output arrives on **GPIO3**, and **GPIO40**
goes to the car. These are the defaults. TCP port 2324 has the tools (`status`,
`scan`, `edges`, `listen`, `findtx`, `pins`) in case another module differs:

```bash
nc 192.168.68.20 2324
```

**Camera.** The sensor is an OV3660, wired like Espressif's ESP32-S3-EYE (the
pin numbers were read from Elegoo's original firmware; see
`esp32/src/camera_stream.cpp`). Frames are kept in the chip's 8 MB octal PSRAM.

| URL | |
|---|---|
| `http://192.168.68.20:81/stream` | MJPEG live video, 640x480, ~24 fps |
| `http://192.168.68.20/capture` | one JPEG |
| `http://192.168.68.20/control?framesize=svga&quality=10` | change size (`qvga` `vga` `svga` `xga` `hd`), JPEG quality (4-63, lower is better), `vflip`, `hmirror` (0/1) |

Settings from `/control` last until the module restarts.

**Elegoo's original firmware** was backed up to `esp32/backup/` (8 MB, not in
git, SHA-256 alongside). To restore it, put the module in download mode and run:

```bash
~/.platformio/packages/tool-esptool/esptool --port /dev/ttyACM0 \
  write-flash 0 esp32/backup/elegoo-original-esp32s3-8MB.bin
```

## Ideas for next steps

- Bluetooth/app control over the serial port
