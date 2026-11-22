#include "follow.h"
#include "led.h"
#include "motors.h"
#include "sensors.h"

namespace {

// ---- Tuning ----
// Line sensor readings (V4: analog 0..1023) go UP over a dark line.
// Set these from the `l` readings over your floor and over your tape.
constexpr int LINE_THRESHOLD = 300;     // above this = sensor sees the line (floor ~40, tape ~700-900)
constexpr int OFF_GROUND = 980;         // all three above this = car picked up (lifted ~1020)

constexpr int FOLLOW_SPEED = 100;       // straight ahead
constexpr int CURVE_FAST = 120;         // outer wheels on a gentle correction
constexpr int CURVE_SLOW = 40;          // inner wheels on a gentle correction
constexpr int PIVOT_SPEED = 110;        // sharp correction / searching (spin in place)
constexpr unsigned long SEARCH_MS = 2000;       // give up after this long without a line
constexpr unsigned long START_DELAY_MS = 1000;
constexpr unsigned long READ_EVERY_MS = 5;

enum class State : uint8_t { Idle, Starting, Following, Searching };
enum class Side : int8_t { Left = -1, Centre = 0, Right = 1 };

State state = State::Idle;
unsigned long stateSince = 0;
unsigned long lastRead = 0;
Side lastSide = Side::Centre;
uint8_t lastPattern = 0xFF;   // bits: L=4, M=2, R=1; 0xFF = nothing printed yet

void enter(State s) {
  state = s;
  stateSince = millis();
  switch (s) {
    case State::Starting:  Led::set(Led::Color::White); break;
    case State::Following: Led::set(Led::Color::Green); break;
    case State::Searching: Led::set(Led::Color::Yellow); break;
    case State::Idle:
    default:               Led::set(Led::Color::Blue); break;
  }
}

void printPattern(uint8_t p, const __FlashStringHelper* action) {
  Serial.print(F("line "));
  Serial.print(p & 4 ? 'L' : '-');
  Serial.print(p & 2 ? 'M' : '-');
  Serial.print(p & 1 ? 'R' : '-');
  Serial.print(F(" -> "));
  Serial.println(action);
}

// Spin toward the side the line was last seen on (left if unknown).
void spinToward(Side s) {
  if (s == Side::Right) Motors::drive(PIVOT_SPEED, -PIVOT_SPEED);
  else Motors::drive(-PIVOT_SPEED, PIVOT_SPEED);
}

void step() {
  if (millis() - lastRead < READ_EVERY_MS) return;
  lastRead = millis();

  Sensors::Line ln = Sensors::line();
  if (ln.left > OFF_GROUND && ln.middle > OFF_GROUND && ln.right > OFF_GROUND) {
    Motors::stop();
    Serial.println(F("follow: all sensors very high, car lifted? stopping"));
    Follow::stop();
    return;
  }

  bool l = ln.left > LINE_THRESHOLD;
  bool m = ln.middle > LINE_THRESHOLD;
  bool r = ln.right > LINE_THRESHOLD;
  uint8_t pattern = (l << 2) | (m << 1) | r;

  if (pattern == 0) {
    if (state == State::Following) {
      printPattern(pattern, lastSide == Side::Right ? F("lost, searching right")
                                                    : F("lost, searching left"));
      lastPattern = pattern;
      spinToward(lastSide);
      enter(State::Searching);
    } else if (millis() - stateSince >= SEARCH_MS) {
      Motors::stop();
      Serial.println(F("follow: line not found, stopping"));
      Follow::stop();
    }
    return;
  }

  if (state != State::Following) enter(State::Following);
  if (pattern == lastPattern) return;
  lastPattern = pattern;

  switch (pattern) {
    case 0b010:  // middle only
    case 0b111:  // all three (wide line or crossing): carry straight on
    case 0b101:  // both edges, not middle (odd; treat as straight)
      Motors::drive(FOLLOW_SPEED, FOLLOW_SPEED);
      lastSide = Side::Centre;
      printPattern(pattern, F("straight"));
      break;
    case 0b110:  // left + middle: line drifting left, gentle left
      Motors::drive(CURVE_SLOW, CURVE_FAST);
      lastSide = Side::Left;
      printPattern(pattern, F("curve left"));
      break;
    case 0b011:  // middle + right: gentle right
      Motors::drive(CURVE_FAST, CURVE_SLOW);
      lastSide = Side::Right;
      printPattern(pattern, F("curve right"));
      break;
    case 0b100:  // left only: sharp left
      spinToward(Side::Left);
      lastSide = Side::Left;
      printPattern(pattern, F("sharp left"));
      break;
    case 0b001:  // right only: sharp right
      spinToward(Side::Right);
      lastSide = Side::Right;
      printPattern(pattern, F("sharp right"));
      break;
  }
}

}  // namespace

namespace Follow {

void begin() { state = State::Idle; }

bool active() { return state != State::Idle; }

void start() {
  if (active()) return;
  Serial.println(F("follow: ON, starting in 1 s (press button or send f to stop)"));
  Sensors::look(90);
  lastSide = Side::Centre;
  lastPattern = 0xFF;
  enter(State::Starting);
}

void stop() {
  if (!active()) return;
  Motors::stop();
  enter(State::Idle);
  Serial.println(F("follow: OFF"));
}

void update() {
  switch (state) {
    case State::Idle:
      break;
    case State::Starting:
      if (millis() - stateSince >= START_DELAY_MS) {
        lastRead = 0;
        // Start as "searching" so a car placed off the line stops after
        // SEARCH_MS instead of driving away.
        state = State::Searching;
        stateSince = millis();
        Led::set(Led::Color::Yellow);
        step();
      }
      break;
    case State::Following:
    case State::Searching:
      step();
      break;
  }
}

}  // namespace Follow
