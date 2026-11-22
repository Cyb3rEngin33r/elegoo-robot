#pragma once
#include <Arduino.h>

namespace Sensors {
void begin();

// Distance from the ultrasonic sensor in cm, or -1 if nothing within range.
int distanceCm();
// Result of the most recent distanceCm() call (no new measurement).
int lastDistanceCm();

// Raw line-tracker readings (analog 0..1023 on V4, 0/1 on V3).
struct Line { int left, middle, right; };
Line line();

// Point the ultrasonic pan servo (0..180, 90 = straight ahead).
void look(int angle);
}  // namespace Sensors
