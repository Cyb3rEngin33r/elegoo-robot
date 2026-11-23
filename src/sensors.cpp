#include "sensors.h"
#include <EEPROM.h>
#include <Servo.h>
#include "pins.h"

namespace {
Servo panServo;
int lastDistance = -1;
int panTrim = 0;
int panAngle = 90;  // last requested angle, before trim
// The servo buzzes while it holds a position, and the gyro feels it, so its
// signal is switched off once it has had time to get there (a 180-degree
// swing takes ~0.5 s). Its gears hold the head in place.
constexpr unsigned long SERVO_SETTLE_MS = 600;
unsigned long servoReleaseAt = 0;

// EEPROM layout: a marker byte, then the trim as a signed byte.
constexpr int EEPROM_MARKER_ADDR = 0;
constexpr int EEPROM_TRIM_ADDR = 1;
constexpr uint8_t EEPROM_MARKER = 0xA5;
constexpr int MAX_TRIM = 20;
}

namespace Sensors {

void begin() {
  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);
  pinMode(PIN_LINE_L, INPUT);
  pinMode(PIN_LINE_M, INPUT);
  pinMode(PIN_LINE_R, INPUT);
  if (EEPROM.read(EEPROM_MARKER_ADDR) == EEPROM_MARKER) {
    panTrim = constrain((int8_t)EEPROM.read(EEPROM_TRIM_ADDR), -MAX_TRIM, MAX_TRIM);
  }
  look(90);
  delay(SERVO_SETTLE_MS);
  update();
}

void update() {
  if (panServo.attached() && (long)(millis() - servoReleaseAt) >= 0) panServo.detach();
}

int distanceCm() {
  digitalWrite(PIN_US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_US_TRIG, LOW);
  unsigned long us = pulseIn(PIN_US_ECHO, HIGH, 30000UL);  // ~5 m timeout
  lastDistance = us == 0 ? -1 : (int)(us / 58);
  return lastDistance;
}

int lastDistanceCm() { return lastDistance; }

Line line() {
#if KIT_VERSION == 4
  return {analogRead(PIN_LINE_L), analogRead(PIN_LINE_M), analogRead(PIN_LINE_R)};
#else
  return {digitalRead(PIN_LINE_L), digitalRead(PIN_LINE_M), digitalRead(PIN_LINE_R)};
#endif
}

void look(int angle) {
  panAngle = angle;
  // Set the position before attaching, so the first pulse is already right.
  panServo.write(constrain(angle + panTrim, 0, 180));
  if (!panServo.attached()) panServo.attach(PIN_SERVO);
  servoReleaseAt = millis() + SERVO_SETTLE_MS;
}

void setTrim(int degrees) {
  panTrim = constrain(degrees, -MAX_TRIM, MAX_TRIM);
  EEPROM.update(EEPROM_MARKER_ADDR, EEPROM_MARKER);  // update() skips unchanged bytes
  EEPROM.update(EEPROM_TRIM_ADDR, (uint8_t)(int8_t)panTrim);
  look(panAngle);  // apply straight away
}

int trim() { return panTrim; }

int lookAngle() { return panAngle; }

}  // namespace Sensors
