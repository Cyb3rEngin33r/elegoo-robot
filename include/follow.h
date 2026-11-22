#pragma once
#include <Arduino.h>

// Line following with the three line sensors: steer to keep the middle
// sensor on a dark line. If the line is lost, spin toward the side it was
// last seen on; give up and stop after a couple of seconds. Stops if the car
// is picked up. Prints each change to Serial and sets the status LED.
namespace Follow {
void begin();
void update();   // call on every loop()
void start();    // waits 1 s, then starts following
void stop();
bool active();
}  // namespace Follow
