#include "steer.h"
#include "imu.h"
#include "motors.h"

namespace {

// ---- Straight driving ----
// Correction = KP * error + KI * integral - KD * turn rate, in motor counts
// added to one side and taken from the other. Error is in degrees.
constexpr float KP = 4.0f;
constexpr float KI = 2.0f;
constexpr float KD = 0.25f;
constexpr int MAX_CORRECTION = 60;   // also never more than half the speed
// The integral learns a steady mismatch between the motors; capped so a
// bump can't wind it up into a big overshoot.
constexpr int MAX_INTEGRAL_CORRECTION = 25;
constexpr unsigned long STEER_EVERY_MS = 10;

// ---- Turns ----
constexpr int TURN_MIN_SPEED = 90;   // slower than this the wheels just skid
constexpr int SLOW_FROM_DEG = 35;    // start slowing this far from the target
constexpr float COAST_S = 0.07f;     // the car keeps turning ~70 ms after stopping
constexpr float DONE_DEG = 2.0f;     // close enough
constexpr unsigned long SETTLE_MS = 250;
constexpr uint8_t MAX_NUDGES = 3;
constexpr unsigned long TURN_TIMEOUT_MS = 6000;
// Stalled (on carpet a wheel can stop): if it turns slower than this for
// STALL_MS, add STALL_BOOST to the power, up to full.
constexpr float STALL_DPS = 8.0f;
constexpr unsigned long STALL_MS = 250;
constexpr int STALL_BOOST = 30;

enum class Mode : uint8_t { Idle, Straight, Turn, Settle };
Mode mode = Mode::Idle;
bool holdOn = true;

int base = 0;            // straight: signed speed
float targetHeading = 0;  // heading to hold or reach
float integral = 0;
unsigned long lastSteer = 0;
int lastCorrection = 0;

int turnSpeed = 0;
int boost = 0;
uint8_t nudges = 0;
unsigned long turnStarted = 0, settleStarted = 0, lastProgress = 0;

int sgn(float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

void spin(int s) { Motors::drive(-s, s); }  // positive = left

void finishTurn(const __FlashStringHelper* how) {
  Motors::stop();
  mode = Mode::Idle;
  Serial.print(F("turn: "));
  Serial.print(how);
  Serial.print(F(", "));
  Serial.print(targetHeading - Imu::heading(), 1);
  Serial.print(F(" deg off, boost "));
  Serial.println(boost);
}

void updateStraight() {
  unsigned long now = millis();
  float dt = (now - lastSteer) * 1e-3f;
  if (now - lastSteer < STEER_EVERY_MS) return;
  lastSteer = now;
  float err = targetHeading - Imu::heading();   // positive: needs to turn left
  int limit = min(MAX_CORRECTION, abs(base) / 2);
  float iLimit = min(limit, MAX_INTEGRAL_CORRECTION) / KI;
  integral = constrain(integral + err * dt, -iLimit, iLimit);
  float c = KP * err + KI * integral - KD * Imu::rate();
  int ci = constrain((int)c, -limit, limit);
  lastCorrection = ci;
  // Turning left needs the right side faster, forward or backward alike.
  Motors::drive(base - ci, base + ci);
}

void updateTurn() {
  float remaining = targetHeading - Imu::heading();
  if (millis() - turnStarted > TURN_TIMEOUT_MS) {
    finishTurn(F("timed out"));
    return;
  }
  int dir = sgn(remaining);
  // Stop early by the distance it will coast at the current rate.
  float coast = fabs(Imu::rate()) * COAST_S;
  if (fabs(remaining) <= DONE_DEG || fabs(remaining) <= coast) {
    Motors::stop();
    mode = Mode::Settle;
    settleStarted = millis();
    return;
  }
  unsigned long now = millis();
  if (fabs(Imu::rate()) >= STALL_DPS) {
    lastProgress = now;
  } else if (now - lastProgress >= STALL_MS && turnSpeed + boost < 255) {
    boost += STALL_BOOST;
    lastProgress = now;
  }
  int s = turnSpeed;
  if (fabs(remaining) < SLOW_FROM_DEG)
    s = TURN_MIN_SPEED + (int)((turnSpeed - TURN_MIN_SPEED) * fabs(remaining) / SLOW_FROM_DEG);
  if (nudges) s = TURN_MIN_SPEED;
  spin(dir * min(s + boost, 255));
}

void updateSettle() {
  if (millis() - settleStarted < SETTLE_MS) return;
  float remaining = targetHeading - Imu::heading();
  if (fabs(remaining) <= DONE_DEG + 1) {
    finishTurn(F("done"));
  } else if (nudges >= MAX_NUDGES) {
    finishTurn(F("gave up"));
  } else {
    nudges++;
    lastProgress = millis();
    mode = Mode::Turn;
  }
}

}  // namespace

namespace Steer {

void straight(int speed) {
  if (!holdOn || !Imu::ok() || speed == 0) {
    mode = Mode::Idle;
    Motors::drive(speed, speed);
    return;
  }
  // Same direction as the drive in progress: keep its heading.
  if (mode != Mode::Straight || sgn(speed) != sgn(base)) {
    targetHeading = Imu::heading();
    integral = 0;
    lastCorrection = 0;
    lastSteer = millis();
    Motors::drive(speed, speed);
  }
  mode = Mode::Straight;
  base = speed;
}

bool turn(int degrees, int speed) {
  if (!Imu::ok()) {
    Serial.println(F("turn: no gyro"));
    return false;
  }
  targetHeading = Imu::heading() + degrees;
  turnSpeed = max(abs(speed), TURN_MIN_SPEED);
  nudges = 0;
  boost = 0;
  turnStarted = lastProgress = millis();
  mode = Mode::Turn;
  updateTurn();
  return true;
}

bool turning() { return mode == Mode::Turn || mode == Mode::Settle; }

void cancel() {
  mode = Mode::Idle;
  lastCorrection = 0;
}

bool target(float& heading) {
  heading = targetHeading;
  return mode != Mode::Idle;
}

int correction() { return mode == Mode::Straight ? lastCorrection : 0; }

void update() {
  switch (mode) {
    case Mode::Straight: updateStraight(); break;
    case Mode::Turn:     updateTurn(); break;
    case Mode::Settle:   updateSettle(); break;
    default: break;
  }
}

bool holdEnabled() { return holdOn; }

void setHoldEnabled(bool on) {
  holdOn = on;
  Serial.println(on ? F("straight: on") : F("straight: off"));
}

const __FlashStringHelper* stateName() {
  if (turning()) return F("turning");
  if (!Imu::ok()) return F("no gyro");
  if (!holdOn) return F("off");
  return mode == Mode::Straight ? F("holding") : F("on");
}

}  // namespace Steer
