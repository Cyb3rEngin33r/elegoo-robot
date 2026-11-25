#include "lift.h"
#include "imu.h"
#include "sensors.h"

namespace {
constexpr unsigned long CHECK_EVERY_MS = 50;
constexpr int OFF_GROUND = 980;            // line sensors: nothing below (lifted ~1020)
constexpr float TIPPED_DEG = 50;           // pitch or roll beyond this: tipped over
constexpr float UPRIGHT_DEG = 30;          // back below this: upright again
constexpr unsigned long LIFTED_MS = 150;   // must last this long to count
constexpr unsigned long TIPPED_MS = 500;   // longer: hard braking tilts the reading briefly
constexpr unsigned long SETTLED_MS = 500;  // back on its wheels this long: drive again

bool on = true;
Lift::State state = Lift::State::Ground;
unsigned long lastCheck = 0;
unsigned long airSince = 0, tiltSince = 0, groundSince = 0;  // 0 = not now

// Has `cond` held for `ms`? `since` tracks when it started.
bool lasted(bool cond, unsigned long& since, unsigned long ms) {
  if (!cond) {
    since = 0;
    return false;
  }
  if (!since) since = millis();
  return millis() - since >= ms;
}
}  // namespace

namespace Lift {

bool update() {
  if (millis() - lastCheck < CHECK_EVERY_MS) return false;
  lastCheck = millis();

  Sensors::Line ln = Sensors::line();
  bool inAir = ln.left > OFF_GROUND && ln.middle > OFF_GROUND && ln.right > OFF_GROUND;
  float pitch = 0, roll = 0;
  bool haveTilt = Imu::ok();
  if (haveTilt) Imu::tilt(pitch, roll);
  float lean = max(fabs(pitch), fabs(roll));
  bool tipped = haveTilt && (lean > TIPPED_DEG || Imu::upsideDown());
  bool upright = !haveTilt || (lean < UPRIGHT_DEG && !Imu::upsideDown());

  State before = state;
  if (lasted(tipped, tiltSince, TIPPED_MS)) state = State::Tipped;
  else if (state == State::Ground && lasted(inAir, airSince, LIFTED_MS)) state = State::Lifted;
  if (state != State::Ground && lasted(!inAir && upright, groundSince, SETTLED_MS))
    state = State::Ground;

  if (state == before) return false;
  if (state == State::Ground) {
    if (on) Serial.println(F("lift: back on its wheels, driving allowed"));
    return false;
  }
  if (!on) return false;
  Serial.println(state == State::Tipped ? F("lift: tipped over, motors off")
                                        : F("lift: picked up, motors off"));
  return true;
}

bool allowsDriving() { return !on || state == State::Ground; }

bool enabled() { return on; }

void setEnabled(bool enable) {
  on = enable;
  Serial.println(on ? F("lift stop: on") : F("lift stop: off (wheels may spin in the air)"));
}

const __FlashStringHelper* stateName() {
  if (!on) return F("off");
  switch (state) {
    case State::Lifted: return F("lifted");
    case State::Tipped: return F("tipped");
    default: return F("ground");
  }
}

}  // namespace Lift
