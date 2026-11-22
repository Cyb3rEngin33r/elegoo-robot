#pragma once
#include <Arduino.h>

// Periodic status report for the dashboard: one JSON object per line, e.g.
//   {"status":1,"up":1234,"mode":"avoid","state":"forward","dist":54,
//    "line":[40,41,45],"batt":7.62,"battRaw":188,"speed":150,
//    "motors":[130,130],"led":"green"}
// Reports are off by default so the serial console stays readable;
// `j` turns them on and `J` turns them off.
namespace Status {
void setEnabled(bool on);
// mode: "idle", "manual", "avoid" or "follow". Call on every loop().
void update(const __FlashStringHelper* mode, const __FlashStringHelper* state, int speed);
}  // namespace Status
