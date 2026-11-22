#include "guard.h"
#include "sensors.h"

namespace {
constexpr int HEAD_TOLERANCE_DEG = 15;      // guard only while the head looks ahead
constexpr unsigned long PING_EVERY_MS = 60; // ultrasonic needs ~60 ms between pings

bool on = true;
bool blocked = false;
unsigned long lastPing = 0;

// Faster needs more room to stop: 30 cm at the default speed 150, 40 cm at 255.
int stopDistanceCm(int speed) { return 15 + speed / 10; }

bool headAhead() { return abs(Sensors::lookAngle() - 90) <= HEAD_TOLERANCE_DEG; }

bool tooClose(int speed, int& cm) {
  lastPing = millis();
  cm = Sensors::distanceCm();
  return cm >= 0 && cm < stopDistanceCm(speed);
}

void reportBlocked(int cm) {
  if (blocked) return;  // say it once, not on every repeated command
  blocked = true;
  Serial.print(F("guard: obstacle "));
  Serial.print(cm);
  Serial.println(F(" cm, stopped"));
}
}  // namespace

namespace Guard {

bool enabled() { return on; }

void setEnabled(bool enable) {
  on = enable;
  blocked = false;
  Serial.println(on ? F("guard: on") : F("guard: off"));
}

bool allowForward(int speed) {
  if (!on || !headAhead()) {
    blocked = false;
    return true;
  }
  int cm;
  if (tooClose(speed, cm)) {
    reportBlocked(cm);
    return false;
  }
  blocked = false;
  return true;
}

bool check(int speed) {
  if (!on || !headAhead() || millis() - lastPing < PING_EVERY_MS) return false;
  int cm;
  if (tooClose(speed, cm)) {
    reportBlocked(cm);
    return true;
  }
  blocked = false;
  return false;
}

void clear() { blocked = false; }

const __FlashStringHelper* stateName() {
  if (!on) return F("off");
  if (!headAhead()) return F("head turned");
  return blocked ? F("blocked") : F("on");
}

}  // namespace Guard
