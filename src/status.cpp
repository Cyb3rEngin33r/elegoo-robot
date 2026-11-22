#include "status.h"
#include "led.h"
#include "motors.h"
#include "pins.h"
#include "sensors.h"

namespace {
constexpr unsigned long REPORT_EVERY_MS = 250;

// V4 battery divider (10k / 1.5k) on A3, with Elegoo's ~8% correction.
// Approximate; check against a multimeter if it matters.
constexpr float VOLTS_PER_COUNT = 0.0375f * 1.08f;

bool enabled = false;
unsigned long lastReport = 0;
}  // namespace

namespace Status {

void setEnabled(bool on) {
  enabled = on;
  lastReport = 0;
}

void update(const __FlashStringHelper* mode, const __FlashStringHelper* state, int speed) {
  if (!enabled || millis() - lastReport < REPORT_EVERY_MS) return;
  lastReport = millis();

  // A fresh ping takes up to 30 ms, which would disturb line following, and
  // obstacle avoidance pings on its own; in those modes report its last reading.
  bool modeRunning = strcmp_P("idle", (PGM_P)mode) != 0 && strcmp_P("manual", (PGM_P)mode) != 0;
  int dist = modeRunning ? Sensors::lastDistanceCm() : Sensors::distanceCm();
  Sensors::Line ln = Sensors::line();

  Serial.print(F("{\"status\":1,\"up\":"));
  Serial.print(millis());
  Serial.print(F(",\"mode\":\""));
  Serial.print(mode);
  Serial.print(F("\",\"state\":\""));
  Serial.print(state);
  Serial.print(F("\",\"dist\":"));
  if (dist < 0) Serial.print(F("null"));
  else Serial.print(dist);
  Serial.print(F(",\"line\":["));
  Serial.print(ln.left); Serial.print(',');
  Serial.print(ln.middle); Serial.print(',');
  Serial.print(ln.right);
#if KIT_VERSION == 4
  int raw = analogRead(PIN_VOLTAGE);
  Serial.print(F("],\"batt\":"));
  Serial.print(raw * VOLTS_PER_COUNT, 2);
  Serial.print(F(",\"battRaw\":"));
  Serial.print(raw);
#else
  Serial.print(F("],\"batt\":null"));
#endif
  Serial.print(F(",\"speed\":"));
  Serial.print(speed);
  Serial.print(F(",\"motors\":["));
  Serial.print(Motors::lastLeft()); Serial.print(',');
  Serial.print(Motors::lastRight());
  Serial.print(F("],\"led\":\""));
  Serial.print(Led::name(Led::current()));
  Serial.println(F("\"}"));
}

}  // namespace Status
