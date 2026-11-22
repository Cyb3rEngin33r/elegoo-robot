#include "sensors.h"
#include <EEPROM.h>
#include <Servo.h>
#include "pins.h"

namespace {
Servo panServo;
int lastDistance = -1;
int panTrim = 0;
int panAngle = 90;  // last requested angle, before trim

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
  panServo.attach(PIN_SERVO);
  look(90);
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
  panServo.write(constrain(angle + panTrim, 0, 180));
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
