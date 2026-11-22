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
src/follow.cpp      line following (thresholds and speeds at the top)
src/led.cpp         status LED (V4 shield RGB LED)
src/remote.cpp      IR remote (button codes at the top)
src/main.cpp        serial test console
```

## Setup (Linux)

```bash
pipx install platformio            # or: python3 -m pip install --user platformio
sudo usermod -aG dialout $USER     # serial port access, then log out and back in
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
```

## Build, flash, test

```bash
pio run                      # compile
pio run -t upload            # flash (USB cable plugged in)
pio device monitor           # 115200 baud serial console
```

Turn the car's battery switch on for the motors to run (USB alone won't power them).
On V4.0 kits, set the shield's upload switch to the upload position while flashing
(it disconnects the Bluetooth module from RX/TX), then switch it back.

Console commands: `w s a d` drive, `x` stop, `+ -` speed, `p` ping,
`l` line sensors, `[ ] \` servo, `o` obstacle avoidance on/off, `f` line following on/off, `h` help.

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

## IR remote

The kit's IR remote works at any time:

| Button | Action |
|---|---|
| ▲ ▼ ◀ ▶ (hold) | drive forward / back / spin left / spin right; stops ~0.25 s after release |
| OK | stop everything |
| 1 | obstacle avoidance on/off |
| 2 | line following on/off |
| * / # | slower / faster |

Like the shield button, 1 or 2 stops whichever mode is running. Buttons that
aren't recognised are printed on the serial console (`IR: unknown ...`), so
codes for another remote can be added to `src/remote.cpp`.

## Ideas for next steps

- Bluetooth/app control over the serial port
