#include "battery.h"
#include "motors.h"
#include "pins.h"

namespace {
#if KIT_VERSION == 4
// V4 battery divider (10k / 1.5k) on A3, with Elegoo's ~8% correction.
constexpr float VOLTS_PER_COUNT = 0.0375f * 1.08f;
#endif

constexpr unsigned long READ_EVERY_MS = 100;
constexpr unsigned long RESTING_AFTER_MS = 500;  // motors off this long
constexpr float FOLLOW = 0.1f;                   // ~1 s average

// Thresholds (resting volts); each state clears a little higher than it
// starts, so it doesn't flicker at the boundary.
constexpr float LOW_V = 7.0f, LOW_CLEAR_V = 7.2f;
constexpr float CRITICAL_V = 6.6f, CRITICAL_CLEAR_V = 6.9f;
constexpr float OFF_V = 5.5f, OFF_CLEAR_V = 6.0f;
constexpr float SAG_V = 6.0f;                    // while driving
constexpr unsigned long SAG_MS = 2000;
constexpr unsigned long REMIND_EVERY_MS = 30000;

Battery::State current = Battery::State::Ok;
float latest = 0, resting = 0, loaded = 0;
bool haveReading = false;
unsigned long lastRead = 0, motorsOffSince = 0, sagSince = 0, lastRemind = 0;

void say(Battery::State s) {
  Serial.print(F("battery: "));
  switch (s) {
    case Battery::State::Ok:       Serial.print(F("ok")); break;
    case Battery::State::Low:      Serial.print(F("low, charge soon")); break;
    case Battery::State::Critical: Serial.print(F("too low, motors off until charged")); break;
    case Battery::State::Off:      Serial.print(F("switch off (USB power only), motors off")); break;
  }
  Serial.print(F(" ("));
  Serial.print(resting, 2);
  Serial.println(F(" V)"));
}

Battery::State next(Battery::State s, bool sagging) {
  using S = Battery::State;
  if (resting < OFF_V) return S::Off;
  if (s == S::Off && resting < OFF_CLEAR_V) return S::Off;
  if (resting < CRITICAL_V || sagging) return S::Critical;
  if ((s == S::Critical || s == S::Off) && resting < CRITICAL_CLEAR_V) return S::Critical;
  if (resting < LOW_V) return S::Low;
  if (s != S::Ok && resting < LOW_CLEAR_V) return S::Low;
  return S::Ok;
}
}  // namespace

namespace Battery {

bool update() {
#if KIT_VERSION == 4
  if (millis() - lastRead < READ_EVERY_MS) return false;
  lastRead = millis();
  latest = analogRead(PIN_VOLTAGE) * VOLTS_PER_COUNT;
  bool motorsOff = Motors::lastLeft() == 0 && Motors::lastRight() == 0;
  if (!motorsOff) motorsOffSince = 0;
  else if (!motorsOffSince) motorsOffSince = millis();

  // Switching the battery on or off is an instant jump: follow it straight
  // away instead of averaging through "low" and "critical" on the way.
  bool switched = motorsOff && (latest < OFF_V || (current == State::Off && latest >= OFF_CLEAR_V));
  if (!haveReading || switched) {
    resting = loaded = latest;
    haveReading = true;
  } else if (motorsOff && millis() - motorsOffSince >= RESTING_AFTER_MS) {
    resting += (latest - resting) * FOLLOW;
  }
  bool sagging = false;
  if (!motorsOff) {
    loaded += (latest - loaded) * FOLLOW;
    if (loaded < SAG_V) {
      if (!sagSince) sagSince = millis();
      sagging = millis() - sagSince >= SAG_MS;
    } else {
      sagSince = 0;
    }
  } else {
    loaded = latest;
    sagSince = 0;
  }

  State before = current;
  current = next(current, sagging);
  if (current != before) {
    say(current);
    lastRemind = millis();
    return !allowsDriving() && (before == State::Ok || before == State::Low);
  }
  if (current != State::Ok && millis() - lastRemind >= REMIND_EVERY_MS) {
    say(current);
    lastRemind = millis();
  }
#endif
  return false;
}

bool allowsDriving() { return current == State::Ok || current == State::Low; }
State state() { return current; }
float volts() { return latest; }
float restVolts() { return resting; }

const __FlashStringHelper* stateName() {
  switch (current) {
    case State::Low:      return F("low");
    case State::Critical: return F("critical");
    case State::Off:      return F("off");
    default:              return F("ok");
  }
}

}  // namespace Battery
