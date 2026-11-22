#pragma once
#include <Arduino.h>

// Collision guard for manual driving (console, IR remote, dashboard): while
// driving forward, stop if something is within the stopping distance, and
// refuse new forward commands while it's there. Reverse and spins are never
// blocked, so the car can always back out.
//
// The distance sensor is on the camera head, so the guard pauses while the
// head is turned more than 15 degrees from centre (it can't see ahead then).
namespace Guard {
bool enabled();
void setEnabled(bool on);

// Before a manual forward drive: true if it may go.
bool allowForward(int speed);
// Call on every loop() while driving forward manually; true if it stopped.
bool check(int speed);
// A non-forward drive started: forget any earlier block.
void clear();

// "off", "on", "head turned" or "blocked", for status reports.
const __FlashStringHelper* stateName();
}  // namespace Guard
