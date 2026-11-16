# Project setup conversation (2026-11-15)

A record of the Claude Code session that started this project.

## Request 1: "can you start a project on the elegoo uno r3 robot"

### Environment checks
- No Arduino toolchain installed (`arduino-cli`, `pio`, `avrdude` all missing).
- No board connected (no `/dev/ttyUSB*` or `/dev/ttyACM*`).
- User isn't in the `dialout` group, so serial port access needs setup.
- `git` isn't installed, so the project isn't a repository yet.

### Decisions
- **PlatformIO** project (`platformio.ini`, `src/`, `include/`) targeting the Arduino UNO R3.
- **Supports both kit versions** with one setting, since the pin layout depends on the kit:
  - `KIT_VERSION=4` (default): V4.0, TB6612 motor driver on the Smart Car Shield.
  - `KIT_VERSION=3`: V3.0, L298N motor driver.
- **First firmware is a serial test console**, not autonomous driving, so each part
  (motors, ultrasonic sensor, servo, line tracker) can be checked safely before
  building behaviours on top. Drive commands run in 400 ms bursts.
- Only dependency is the Arduino `Servo` library (FastLED for the V4 RGB LED left out for now).
- Range-based `for` over a brace list was swapped for plain arrays, because the AVR
  toolchain doesn't ship `<initializer_list>`.

### Files created
| File | Purpose |
|---|---|
| `platformio.ini` | Board config and `KIT_VERSION` setting |
| `include/pins.h` | Pin maps for V4.0 and V3.0 |
| `include/motors.h`, `src/motors.cpp` | `Motors::drive(left, right)`, speeds -255..255, per-side invert flags |
| `include/sensors.h`, `src/sensors.cpp` | Ultrasonic distance (cm), line tracker, pan servo |
| `src/main.cpp` | Serial test console at 115200 baud |
| `README.md` | Setup, flashing and usage instructions |

### Pin maps
| Function | V4.0 (TB6612) | V3.0 (L298N) |
|---|---|---|
| Motor driver enable | STBY 3 | — |
| Right motor speed / direction | PWMA 5 / AIN1 7 | ENA 5 / IN1 7, IN2 8 |
| Left motor speed / direction | PWMB 6 / BIN1 8 | ENB 6 / IN3 9, IN4 11 |
| Ultrasonic trig / echo | 13 / 12 | A5 / A4 |
| Pan servo | 10 | 3 |
| Line tracker L / M / R | A2 / A1 / A0 (analog) | 2 / 4 / 10 (digital) |
| IR receiver | 9 | 12 |
| RGB LED / button / battery voltage | 4 / 2 / A3 | — |

### Console commands
`w s a d` drive, `x` stop, `+ -` speed, `p` ping distance, `l` read line sensors,
`[ ] \` move servo, `h` help.

### Status
Not compiled or flashed yet, because there's no toolchain or board on this machine.

### Next steps
1. `pipx install platformio` and `sudo usermod -aG dialout $USER` (then log out and back in).
2. Set `KIT_VERSION=3` in `platformio.ini` if the kit is V3.
3. `pio run -t upload`, then `pio device monitor`. On the V4 shield, move the upload switch
   to the upload position while flashing.
4. Test with the wheels off the table and the battery switched on.
5. Optionally `sudo apt install git && git init`.

Open questions for the user: which kit version, and what the robot should do first
(obstacle avoidance, line following or IR remote control).

## Request 2: "Can yo save this conversation to the project?"
Saved as this file, `docs/conversation-2026-11-15.md`.

## Request 3: use uv for the Python tooling and add a direnv config

- PlatformIO is now a uv dependency (`platformio>=6.1`, 6.2.0 installed, Python 3.12 from `.python-version`).
- `.envrc` runs `uv sync`, turns on `.venv` (`VIRTUAL_ENV` + `PATH_add`), and re-runs when
  `pyproject.toml`, `uv.lock` or `.python-version` changes.
- While this was being set up, the user ran `git init` and `uv init --package`, which created
  `src/elegoo_robot/` and `.python-version`. Claude's first `pyproject.toml` overwrote the
  `uv init` one; it was restored from the git index and PlatformIO added with `uv add`.
- `.gitignore` now also ignores `.direnv/`, `.pio/`, `.vscode/` and `*.swp`.

## Request 4: "What is my next step. What do I have to install?"

- First build ran: PlatformIO downloaded the AVR toolchain and Arduino core into `~/.platformio`.
- Firmware compiles cleanly for both kits:
  - V4: RAM 11.8% (241 B), flash 15.0% (4846 B)
  - V3 (tested in a scratch copy): flash 15.1% (4870 B)
- direnv is already loaded from `~/.bashrc`.

## Status before reboot (2026-11-15)

Done:
- [x] Project scaffold, firmware for V3 and V4, README
- [x] uv + PlatformIO installed, `.envrc` written
- [x] Firmware builds

Run before the reboot (needs sudo):
```bash
sudo usermod -aG dialout $USER
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
```

After the reboot:
1. `groups` should now list `dialout`.
2. `cd ~/elegoo-robot && direnv allow`
3. Plug in the robot, then `pio device list` (expect `/dev/ttyUSB0` or `/dev/ttyACM0`).
   If no port appears: `sudo apt remove brltty`, then unplug and replug the board.
4. If the kit is V3, set `KIT_VERSION=3` in `platformio.ini`.
5. V4 only: set the shield's upload switch to the upload position.
   Then `pio run -t upload` and `pio device monitor`.
6. Battery switch on, wheels off the table, then test with `p`, `l`, `w a s d`.

Git housekeeping before the first commit:
- `git rm --cached .README.md.swp` (vim swap file got staged)
- `git add .envrc uv.lock`

Still open: which kit version the user has, and what the robot should do first
(obstacle avoidance, line following or IR remote control).
