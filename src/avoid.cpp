#include "avoid.h"
#include "motors.h"
#include "sensors.h"

namespace {

// ---- Tuning ----
constexpr int STOP_CM = 25;            // closer than this: stop and look around
constexpr int SLOW_CM = 50;            // closer than this: slow down
constexpr int CRUISE_SPEED = 130;      // cautious; motors go up to 255
constexpr int SLOW_SPEED = 100;
constexpr int TURN_SPEED = 150;
constexpr int BACK_SPEED = 120;
constexpr unsigned long TURN_MS = 450;          // how long to spin when turning away
constexpr unsigned long BACK_MS = 500;          // how long to reverse when boxed in
constexpr unsigned long START_DELAY_MS = 1000;  // time to move your hand away
constexpr unsigned long PING_EVERY_MS = 60;     // ultrasonic needs ~60 ms between pings
constexpr unsigned long SERVO_SETTLE_MS = 400;  // time for the servo to turn 60 degrees
constexpr int LOOK_AHEAD = 90, LOOK_LEFT = 150, LOOK_RIGHT = 30;
constexpr int FAR_CM = 400;            // used when there is no echo (nothing in range)

enum class State : uint8_t { Idle, Starting, Forward, LookLeft, LookRight, Backing, Turning };

State state = State::Idle;
unsigned long stateSince = 0;
unsigned long lastPing = 0;
int currentSpeed = 0;
int leftCm = 0, rightCm = 0;

void enter(State s) {
  state = s;
  stateSince = millis();
}

unsigned long timeInState() { return millis() - stateSince; }

int ping() {
  int d = Sensors::distanceCm();
  return d < 0 ? FAR_CM : d;
}

void printCm(int d) {
  if (d >= FAR_CM) {
    Serial.print(F("far"));
  } else {
    Serial.print(d);
    Serial.print(F(" cm"));
  }
}

// Spin toward the side with more room (left on a tie).
void turnToClearerSide() {
  if (leftCm >= rightCm) Motors::drive(-TURN_SPEED, TURN_SPEED);
  else Motors::drive(TURN_SPEED, -TURN_SPEED);
  enter(State::Turning);
}

void forwardStep() {
  if (millis() - lastPing < PING_EVERY_MS) return;
  lastPing = millis();
  int d = ping();

  if (d < STOP_CM) {
    Motors::stop();
    currentSpeed = 0;
    Serial.print(F("blocked "));
    printCm(d);
    Serial.println(F(" -> stop, looking around"));
    Sensors::look(LOOK_LEFT);
    enter(State::LookLeft);
    return;
  }

  int speed = d < SLOW_CM ? SLOW_SPEED : CRUISE_SPEED;
  if (speed != currentSpeed) {  // only log when something changes
    Motors::drive(speed, speed);
    currentSpeed = speed;
    Serial.print(speed == SLOW_SPEED ? F("close ") : F("clear "));
    printCm(d);
    Serial.println(speed == SLOW_SPEED ? F(" -> forward, slow") : F(" -> forward"));
  }
}

void decide() {
  Sensors::look(LOOK_AHEAD);
  Serial.print(F("  left "));
  printCm(leftCm);
  Serial.print(F(", right "));
  printCm(rightCm);

  const char* side = leftCm >= rightCm ? "left" : "right";
  if (leftCm < STOP_CM && rightCm < STOP_CM) {
    Serial.print(F(" -> boxed in, backing up, then turning "));
    Serial.println(side);
    Motors::drive(-BACK_SPEED, -BACK_SPEED);
    enter(State::Backing);
  } else {
    Serial.print(F(" -> turning "));
    Serial.println(side);
    turnToClearerSide();
  }
}

}  // namespace

namespace Avoid {

void begin() { enter(State::Idle); }

bool active() { return state != State::Idle; }

void start() {
  if (active()) return;
  Serial.println(F("avoid: ON, starting in 1 s (press button or send o to stop)"));
  Sensors::look(LOOK_AHEAD);
  enter(State::Starting);
}

void stop() {
  if (!active()) return;
  Motors::stop();
  currentSpeed = 0;
  Sensors::look(LOOK_AHEAD);
  enter(State::Idle);
  Serial.println(F("avoid: OFF"));
}

void update() {
  switch (state) {
    case State::Idle:
      break;

    case State::Starting:
      if (timeInState() >= START_DELAY_MS) {
        currentSpeed = 0;
        lastPing = 0;
        enter(State::Forward);
      }
      break;

    case State::Forward:
      forwardStep();
      break;

    case State::LookLeft:
      if (timeInState() >= SERVO_SETTLE_MS) {
        leftCm = ping();
        Sensors::look(LOOK_RIGHT);
        enter(State::LookRight);
      }
      break;

    case State::LookRight:
      // twice as far to travel: from left, past centre, to right
      if (timeInState() >= 2 * SERVO_SETTLE_MS) {
        rightCm = ping();
        decide();
      }
      break;

    case State::Backing:
      if (timeInState() >= BACK_MS) {
        Motors::stop();
        turnToClearerSide();
      }
      break;

    case State::Turning:
      if (timeInState() >= TURN_MS) {
        Motors::stop();
        currentSpeed = 0;
        lastPing = 0;
        enter(State::Forward);  // re-checks the way ahead straight away
      }
      break;
  }
}

}  // namespace Avoid
