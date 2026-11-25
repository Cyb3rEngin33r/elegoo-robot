#pragma once
#include <Arduino.h>

// Picked-up and tipped-over protection. Lifted: all three line sensors see
// nothing below (as when the car is in the air). Tipped: tilted more than
// 50 degrees, or upside down. Either way the motors stop and driving is
// refused until the car is back on its wheels for half a second.
//
// Bench tests with the wheels in the air need it off: `u` toggles it.
namespace Lift {
enum class State : uint8_t { Ground, Lifted, Tipped };

// Call on every loop(); true when the car has just left the ground or
// tipped (the caller stops everything).
bool update();
// False while lifted or tipped (and the protection is on).
bool allowsDriving();

bool enabled();
void setEnabled(bool on);
// "off", "ground", "lifted" or "tipped", for status reports.
const __FlashStringHelper* stateName();
}  // namespace Lift
