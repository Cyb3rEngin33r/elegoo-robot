#pragma once
#include <Arduino.h>

// Periodic status report for the dashboard: one JSON object per line, e.g.
//   {"status":1,"up":1234,"mode":"avoid","state":"forward","dist":54,
//    "line":[40,41,45],"batt":7.62,"battRaw":188,"speed":150,
//    "motors":[130,130],"guard":"on","trim":2,"imu":"MPU6050",
//    "heading":-12.5,"imuErr":[0,0],"bump":[1,6.2],"lift":"ground","steer":"holding",
//    "led":"green"}
// imuErr: failed gyro reads, gyro restarts. bump: bumps so far, last one (g).
// lift: "ground", "lifted", "tipped" or "off" (check switched off).
// While reports are on, a short gyro report also goes out 10 times a second
// (only if the gyro was found):
//   {"gyro":1,"h":-12.3,"r":4.5,"t":-10.0,"c":-8,"tp":1.5,"tr":-0.8,"jx":0.21,"jy":-0.48}
// h heading, r turn rate (deg/s), t heading being held or turned to (null
// if none), c straight-line correction; angles -180..180, left positive.
// tp, tr: tilt (pitch nose up, roll right side down, degrees).
// jx, jy: biggest sideways / fore-aft jolt since the last report (g).
// Reports are off by default so the serial console stays readable;
// `j` turns them on and `J` turns them off.
namespace Status {
void setEnabled(bool on);
// mode: "idle", "manual", "avoid" or "follow". Call on every loop().
void update(const __FlashStringHelper* mode, const __FlashStringHelper* state, int speed);
}  // namespace Status
