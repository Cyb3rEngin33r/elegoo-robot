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

// Point the ultrasonic pan servo (0..180, 90 = straight ahead). The centre
// trim is added, so 90 means straight ahead even if the servo horn sits a
// little off.
void look(int angle);
// The angle last passed to look() (before trim).
int lookAngle();

// Centre trim in degrees (positive turns the head left). Saved in EEPROM and
// restored at power-up; limited to +/-20.
void setTrim(int degrees);
int trim();
}  // namespace Sensors
