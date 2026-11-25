#pragma once
#include <Arduino.h>

// Low-battery protection (V4: battery voltage on A3). The pack is two
// 18650 cells: 8.4 V full, about 6.4 V empty.
//
// Decisions use the resting voltage (measured with the motors off for
// 0.5 s), because driving pulls the voltage down by about 1 V:
//   ok        7.0 V and up
//   low       under 7.0 V: warning, still drives
//   critical  under 6.6 V (or under 6.0 V for 2 s while driving): stops
//             everything and refuses to drive until charged
//   off       under 5.5 V: the battery switch is off (USB power only);
//             the motors can't run, and trying resets the board
namespace Battery {
enum class State : uint8_t { Ok, Low, Critical, Off };

// Call on every loop(); true when driving has just become unsafe (the
// caller stops everything).
bool update();
bool allowsDriving();
State state();
float volts();      // latest reading
float restVolts();  // resting voltage the decisions use
const __FlashStringHelper* stateName();  // "ok", "low", "critical", "off"
}  // namespace Battery
