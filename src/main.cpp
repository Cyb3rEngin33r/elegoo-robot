// Elegoo Smart Robot Car: serial test console.
//
// Open the serial monitor at 115200 baud and send single-letter commands:
//   w/s/a/d  forward / back / spin left / spin right (runs for 400 ms)
//   x        stop
//   + / -    change speed
//   p        ping the ultrasonic sensor
//   l        read the line tracker
//   [ / ] /  pan servo left / right / center ( '\' )
//   h        help
//
// Put the car on a stand or hold it with the wheels off the table for the first test.

#include <Arduino.h>
#include "motors.h"
#include "pins.h"
#include "sensors.h"

namespace {
int speed = 150;
int panAngle = 90;
unsigned long driveUntil = 0;

void help() {
  Serial.println(F("Commands: w s a d (drive) x (stop) +/- (speed) p (ping) "
                   "l (line) [ ] \\ (servo) h (help)"));
}

void driveFor(int left, int right, unsigned long ms = 400) {
  Motors::drive(left, right);
  driveUntil = millis() + ms;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Motors::begin();
  Sensors::begin();
  Serial.print(F("Elegoo robot ready (kit V"));
  Serial.print(KIT_VERSION);
  Serial.println(F(")"));
  help();
}

void loop() {
  if (driveUntil && (long)(millis() - driveUntil) >= 0) {
    Motors::stop();
    driveUntil = 0;
  }

  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case 'w': driveFor(speed, speed); break;
    case 's': driveFor(-speed, -speed); break;
    case 'a': driveFor(-speed, speed); break;
    case 'd': driveFor(speed, -speed); break;
    case 'x': Motors::stop(); driveUntil = 0; break;
    case '+': speed = min(speed + 25, 255); Serial.print(F("speed ")); Serial.println(speed); break;
    case '-': speed = max(speed - 25, 50);  Serial.print(F("speed ")); Serial.println(speed); break;
    case 'p': {
      int d = Sensors::distanceCm();
      Serial.print(F("distance: "));
      if (d < 0) Serial.println(F("out of range"));
      else { Serial.print(d); Serial.println(F(" cm")); }
      break;
    }
    case 'l': {
      Sensors::Line ln = Sensors::line();
      Serial.print(F("line L/M/R: "));
      Serial.print(ln.left); Serial.print(' ');
      Serial.print(ln.middle); Serial.print(' ');
      Serial.println(ln.right);
      break;
    }
    case '[': panAngle = min(panAngle + 15, 180); Sensors::look(panAngle); break;
    case ']': panAngle = max(panAngle - 15, 0);   Sensors::look(panAngle); break;
    case '\\': panAngle = 90; Sensors::look(panAngle); break;
    case 'h': help(); break;
    default: break;  // ignore newlines etc.
  }
}
