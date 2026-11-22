#pragma once
#include <Arduino.h>

// Status LED (the single RGB LED on the V4 shield). Does nothing on V3 kits.
//
//   Blue    idle, waiting for the button or `o`
//   White   starting (the 1 s delay before driving)
//   Green   driving forward
//   Yellow  looking around or turning away
//   Red     backing up (boxed in)
namespace Led {
enum class Color : uint8_t { Off, Blue, White, Green, Yellow, Red };

void begin();
void set(Color c);  // cheap to call repeatedly; only updates the LED on a change
Color current();
const __FlashStringHelper* name(Color c);
}  // namespace Led
