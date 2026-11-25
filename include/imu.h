#pragma once
#include <Arduino.h>

// Gyro on the V4 shield, used for heading (which way the car points).
// Earlier shields carry an MPU6050 (I2C 0x68), newer ones a QMI8658C
// (I2C 0x6B); both are detected at power-up. I2C is on A4/A5.
//
// Only the Z (yaw) axis is used. Its zero point is measured at power-up,
// waiting (up to 5 s) until the car is still, and is kept up to date
// whenever the car stands still.
namespace Imu {
bool begin();     // detects the chip and measures the zero point
void update();    // call on every loop()
bool ok();
const __FlashStringHelper* name();  // "MPU6050", "QMI8658C" or "none"

// Heading in degrees since power-up, counter-clockwise (left) positive.
// Not wrapped: two full left turns read 720.
float heading();
float rate();     // degrees per second, left positive

// Largest sudden sideways / fore-aft acceleration (g, signed, gravity and
// slow changes removed) since the last call; resets it. For status reports.
void takeJolt(float& x, float& y);
// Lowest and highest fore-aft acceleration (g, forward positive) since the
// last call; resets them. For the bump detector.
void takeSurge(float& lowest, float& highest);

// Tilt in degrees, averaged over ~0.4 s: pitch nose up positive, roll right
// side down positive. And whether the car is upside down.
void tilt(float& pitch, float& roll);
bool upsideDown();

// Diagnostics: failed reads, and times the chip had to be set up again.
uint16_t errors();
uint16_t restartCount();
}  // namespace Imu
