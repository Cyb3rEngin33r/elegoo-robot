#include "sensors.h"
#include <Servo.h>
#include "pins.h"

namespace {
Servo panServo;
}

namespace Sensors {

void begin() {
  pinMode(PIN_US_TRIG, OUTPUT);
  pinMode(PIN_US_ECHO, INPUT);
  pinMode(PIN_LINE_L, INPUT);
  pinMode(PIN_LINE_M, INPUT);
  pinMode(PIN_LINE_R, INPUT);
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
  if (us == 0) return -1;
  return us / 58;
}

Line line() {
#if KIT_VERSION == 4
  return {analogRead(PIN_LINE_L), analogRead(PIN_LINE_M), analogRead(PIN_LINE_R)};
#else
  return {digitalRead(PIN_LINE_L), digitalRead(PIN_LINE_M), digitalRead(PIN_LINE_R)};
#endif
}

void look(int angle) { panServo.write(constrain(angle, 0, 180)); }

}  // namespace Sensors
