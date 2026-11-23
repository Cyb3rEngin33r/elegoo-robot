#pragma once
#include <Arduino.h>

// Gyro-assisted driving (needs the gyro; see imu.h).
//
// Straight: while driving forward or back, the car holds the heading it had
// when the drive started, speeding up one side a little to correct drift.
// Repeated drive commands (a held key or remote button) keep the same
// heading. Can be switched off to compare.
//
// Turns: spin on the spot by an exact angle, slowing near the end and
// nudging back if it overshoots.
namespace Steer {
// Start or continue a straight drive at `speed` (-255..255).
void straight(int speed);
// Spin by `degrees` (positive = left) at about `speed`. False if no gyro.
bool turn(int degrees, int speed);
bool turning();
// Forget the held heading and any turn. Doesn't touch the motors.
void cancel();
// Call on every loop(). Drives the motors while straight() or turn() is active.
void update();

bool holdEnabled();
void setHoldEnabled(bool on);
// "off", "no gyro", "on", "holding" or "turning", for status reports.
const __FlashStringHelper* stateName();
}  // namespace Steer
