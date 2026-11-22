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
`l` line sensors, `[ ] \` servo, `o` obstacle avoidance on/off, `h` help.

## Obstacle avoidance

The car never drives on power-up. Press the shield's push button (or send `o`)
to start; it waits 1 s, then drives forward, slows down under 50 cm, and under
25 cm stops, looks left and right, and turns toward the clearer side. Press the
button again (or send `o`, `x` or any drive key) to stop. Each decision is
printed to the serial console, so you can test it on the desk with the wheels up
and then unplug USB and run it on the floor.

If one side spins backwards, flip `INVERT_LEFT` / `INVERT_RIGHT` in `include/motors.h`.

## Ideas for next steps

- Line following with the three line sensors
- IR remote control (IRremote library, pin `PIN_IR_RX`)
- Bluetooth/app control over the serial port
