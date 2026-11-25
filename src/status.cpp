#include "status.h"
#include "battery.h"
#include "bump.h"
#include "guard.h"
#include "imu.h"
#include "lift.h"
#include "led.h"
#include "motors.h"
#include "pins.h"
#include "sensors.h"
#include "steer.h"

namespace {
constexpr unsigned long REPORT_EVERY_MS = 250;
constexpr unsigned long GYRO_EVERY_MS = 100;

bool enabled = false;
unsigned long lastReport = 0;
unsigned long lastGyro = 0;

float wrap180(float h) {
  h = fmod(h, 360.0f);
  if (h > 180) h -= 360;
  if (h <= -180) h += 360;
  return h;
}

// Short and quick to send (~45 characters), so it can go out often.
void gyroReport() {
  if (!Imu::ok() || millis() - lastGyro < GYRO_EVERY_MS) return;
  lastGyro = millis();
  float t;
  Serial.print(F("{\"gyro\":1,\"h\":"));
  Serial.print(wrap180(Imu::heading()), 1);
  Serial.print(F(",\"r\":"));
  Serial.print(Imu::rate(), 1);
  Serial.print(F(",\"t\":"));
  if (Steer::target(t)) Serial.print(wrap180(t), 1);
  else Serial.print(F("null"));
  Serial.print(F(",\"c\":"));
  Serial.print(Steer::correction());
  float pitch, roll;
  Imu::tilt(pitch, roll);
  Serial.print(F(",\"tp\":"));
  Serial.print(pitch, 1);
  Serial.print(F(",\"tr\":"));
  Serial.print(roll, 1);
  float jx, jy;
  Imu::takeJolt(jx, jy);
  Serial.print(F(",\"jx\":"));
  Serial.print(jx, 2);
  Serial.print(F(",\"jy\":"));
  Serial.print(jy, 2);
  Serial.println('}');
}
}  // namespace

namespace Status {

void setEnabled(bool on) {
  enabled = on;
  lastReport = 0;
}

void update(const __FlashStringHelper* mode, const __FlashStringHelper* state, int speed) {
  if (!enabled) return;
  gyroReport();
  if (millis() - lastReport < REPORT_EVERY_MS) return;
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
  Serial.print(F("],\"batt\":"));
  Serial.print(Battery::volts(), 2);
  Serial.print(F(",\"battRest\":"));
  Serial.print(Battery::restVolts(), 2);
  Serial.print(F(",\"battState\":\""));
  Serial.print(Battery::stateName());
  Serial.print('"');
#else
  Serial.print(F("],\"batt\":null"));
#endif
  Serial.print(F(",\"speed\":"));
  Serial.print(speed);
  Serial.print(F(",\"motors\":["));
  Serial.print(Motors::lastLeft()); Serial.print(',');
  Serial.print(Motors::lastRight());
  Serial.print(F("],\"guard\":\""));
  Serial.print(Guard::stateName());
  Serial.print(F("\",\"trim\":"));
  Serial.print(Sensors::trim());
  Serial.print(F(",\"imu\":\""));
  Serial.print(Imu::name());
  Serial.print(F("\",\"heading\":"));
  if (Imu::ok()) {
    Serial.print(wrap180(Imu::heading()), 1);  // -180..180, left positive
  } else {
    Serial.print(F("null"));
  }
  Serial.print(F(",\"imuErr\":["));
  Serial.print(Imu::errors()); Serial.print(',');
  Serial.print(Imu::restartCount());
  Serial.print(']');
  Serial.print(F(",\"bump\":["));
  Serial.print(Bump::count()); Serial.print(',');
  Serial.print(Bump::lastG(), 1);
  Serial.print(']');
  Serial.print(F(",\"lift\":\""));
  Serial.print(Lift::stateName());
  Serial.print('"');
  Serial.print(F(",\"steer\":\""));
  Serial.print(Steer::stateName());
  Serial.print(F("\""));
  Serial.print(F(",\"led\":\""));
  Serial.print(Led::name(Led::current()));
  Serial.println(F("\"}"));
}

}  // namespace Status
