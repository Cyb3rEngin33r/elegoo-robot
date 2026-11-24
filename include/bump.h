#pragma once
#include <Arduino.h>

// Bump detection with the gyro chip's accelerometer: hitting something gives
// a sharp jolt along the direction of travel (5-8 g into a wall at speed
// 150), far above normal driving (under about 2 g). Always on, guard or not.
//
// After a bump, a held drive key or remote button is refused in that
// direction; letting go and pressing again, stopping (x), reversing or
// turning clears it.
namespace Bump {
enum class Hit : uint8_t { None, Front, Rear };

// Call on every loop(); reports a new bump once.
Hit update();
// Whether a manual drive in this direction (+1 forward, -1 back) is allowed.
bool allows(int direction);
void clear();

uint16_t count();
float lastG();  // strength of the last bump (g, positive)
}  // namespace Bump
