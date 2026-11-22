// Elegoo Smart Robot Car: serial test console + obstacle avoidance.
//
// Open the serial monitor at 115200 baud and send single-letter commands:
//   w/s/a/d  forward / back / spin left / spin right (runs for 400 ms)
//   x        stop
//   + / -    change speed
//   p        ping the ultrasonic sensor
//   l        read the line tracker
//   [ / ] /  pan servo left / right / center ( '\' )
//   o        obstacle avoidance on/off (the shield's push button does the same)
//   h        help
//
// The car never drives on power-up. Obstacle avoidance starts only when you
// press the button (or send o), and waits 1 s before moving.
// Any drive command or x also stops obstacle avoidance.
//
// Put the car on a stand or hold it with the wheels off the table for the first test.

#include <Arduino.h>
#include "avoid.h"
#include "motors.h"
#include "pins.h"
#include "sensors.h"

namespace {
int speed = 150;
int panAngle = 90;
unsigned long driveUntil = 0;

void help() {
  Serial.println(F("Commands: w s a d (drive) x (stop) +/- (speed) p (ping) "
                   "l (line) [ ] \\ (servo) o (avoid on/off) h (help)"));
}

void driveFor(int left, int right, unsigned long ms = 400) {
  Avoid::stop();
  Motors::drive(left, right);
  driveUntil = millis() + ms;
}

void toggleAvoid() {
  if (Avoid::active()) {
    Avoid::stop();
  } else {
    Motors::stop();
    driveUntil = 0;
    panAngle = 90;
    Avoid::start();
  }
}

// Push button on the shield: active LOW, debounced, fires once per press.
#if KIT_VERSION == 4  // only the V4 shield has the push button
constexpr unsigned long DEBOUNCE_MS = 30;
bool buttonDown = false;
bool lastReading = false;
unsigned long readingSince = 0;

bool buttonPressed() {
  bool reading = digitalRead(PIN_BUTTON) == LOW;
  if (reading != lastReading) {
    lastReading = reading;
    readingSince = millis();
  }
  if (millis() - readingSince >= DEBOUNCE_MS && reading != buttonDown) {
    buttonDown = reading;
    return buttonDown;  // true only on the press, not the release
  }
  return false;
}
#endif

void handleCommand(char c) {
  switch (c) {
    case 'w': driveFor(speed, speed); break;
    case 's': driveFor(-speed, -speed); break;
    case 'a': driveFor(-speed, speed); break;
    case 'd': driveFor(speed, -speed); break;
    case 'x': Avoid::stop(); Motors::stop(); driveUntil = 0; break;
    case 'o': toggleAvoid(); break;
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
}  // namespace

void setup() {
  Serial.begin(115200);
  Motors::begin();
  Sensors::begin();
  Avoid::begin();
#if KIT_VERSION == 4  // only the V4 shield has the push button
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  buttonDown = lastReading = digitalRead(PIN_BUTTON) == LOW;
#endif
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

#if KIT_VERSION == 4  // only the V4 shield has the push button
  if (buttonPressed()) toggleAvoid();
#endif

  Avoid::update();

  if (Serial.available()) handleCommand(Serial.read());
}
