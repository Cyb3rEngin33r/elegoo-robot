#pragma once
#include <Arduino.h>

// Obstacle avoidance: drive forward, slow down near obstacles, and when
// something is too close, stop, look left and right with the pan servo and
// turn toward the clearer side. Prints each decision to Serial.
//
// update() never blocks for long, so the console and button stay responsive.
namespace Avoid {
void begin();
void update();   // call on every loop()
void start();    // waits 1 s, then starts driving
void stop();
bool active();
const __FlashStringHelper* stateName();  // for status reports
}  // namespace Avoid
