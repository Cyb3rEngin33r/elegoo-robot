#pragma once
#include <Arduino.h>

// Drive both sides of the car. Speeds are -255..255 (negative = reverse).
// If a side spins the wrong way, flip its INVERT flag below.
namespace Motors {
constexpr bool INVERT_LEFT  = false;
constexpr bool INVERT_RIGHT = false;

void begin();
void drive(int left, int right);
void stop();
int lastLeft();   // last commanded speeds, for status reports
int lastRight();
}  // namespace Motors
