// Elegoo Smart Robot Car: serial test console + obstacle avoidance.
//
// Open the serial monitor at 115200 baud and send single-letter commands:
//   w/s/a/d  forward / back / spin left / spin right (runs for 400 ms)
//   x        stop
//   + / -    change speed
//   p        ping the ultrasonic sensor
//   l        read the line tracker
//   [ / ] /  pan servo left / right / center ( '\' )
//   o        obstacle avoidance on/off
//   f        line following on/off
//   h        help
//
// Shield push button (V4): short press = obstacle avoidance, hold 1 s = line
// following; any press while a mode is running stops it.
//
// IR remote: hold an arrow to drive (stops ~0.25 s after release), OK = stop,
// 1 = obstacle avoidance, 2 = line following, * / # = slower / faster.
//
// The car never drives on power-up. Both modes wait 1 s before moving.
// Any drive command or x also stops them.
//
// Put the car on a stand or hold it with the wheels off the table for the first test.

#include <Arduino.h>
#include "avoid.h"
#include "follow.h"
#include "led.h"
#include "motors.h"
#include "pins.h"
#include "remote.h"
#include "sensors.h"

namespace {
int speed = 150;
int panAngle = 90;
unsigned long driveUntil = 0;

void help() {
  Serial.println(F("Commands: w s a d (drive) x (stop) +/- (speed) p (ping) "
                   "l (line) [ ] \\ (servo) o (avoid) f (follow line) h (help)"));
}

bool modeActive() { return Avoid::active() || Follow::active(); }

void stopAll() {
  Avoid::stop();
  Follow::stop();
  Motors::stop();
  driveUntil = 0;
}

void driveFor(int left, int right, unsigned long ms = 400) {
  Avoid::stop();
  Follow::stop();
  Motors::drive(left, right);
  driveUntil = millis() + ms;
}

// Start a mode, or stop everything if any mode is already running.
void toggleMode(bool lineFollowing) {
  if (modeActive()) {
    stopAll();
    return;
  }
  stopAll();
  panAngle = 90;
  if (lineFollowing) Follow::start();
  else Avoid::start();
}

// Push button on the shield: active LOW, debounced.
// Short press (released before 1 s) or long press (held 1 s; fires while held).
#if KIT_VERSION == 4  // only the V4 shield has the push button
enum class Press : uint8_t { None, Short, Long };
constexpr unsigned long DEBOUNCE_MS = 30;
constexpr unsigned long LONG_PRESS_MS = 1000;
bool buttonDown = false;
bool lastReading = false;
bool longFired = false;
unsigned long readingSince = 0;
unsigned long pressedAt = 0;

Press buttonEvent() {
  bool reading = digitalRead(PIN_BUTTON) == LOW;
  if (reading != lastReading) {
    lastReading = reading;
    readingSince = millis();
  }
  if (millis() - readingSince >= DEBOUNCE_MS && reading != buttonDown) {
    buttonDown = reading;
    if (buttonDown) {
      pressedAt = millis();
      longFired = false;
    } else if (!longFired) {
      return Press::Short;
    }
  }
  if (buttonDown && !longFired && millis() - pressedAt >= LONG_PRESS_MS) {
    longFired = true;
    return Press::Long;
  }
  return Press::None;
}
#endif

void handleCommand(char c);

// IR remote. Drive buttons repeat while held, so each repeat extends the drive.
constexpr unsigned long REMOTE_HOLD_MS = 250;

void handleRemote() {
  bool repeat;
  Remote::Button b = Remote::read(repeat);
  if (b == Remote::Button::None || b == Remote::Button::Unknown) return;
  if (!repeat) {
    Serial.print(F("IR: "));
    Serial.println(Remote::name(b));
  }
  switch (b) {
    case Remote::Button::Up:    driveFor(speed, speed, REMOTE_HOLD_MS); break;
    case Remote::Button::Down:  driveFor(-speed, -speed, REMOTE_HOLD_MS); break;
    case Remote::Button::Left:  driveFor(-speed, speed, REMOTE_HOLD_MS); break;
    case Remote::Button::Right: driveFor(speed, -speed, REMOTE_HOLD_MS); break;
    default:
      if (repeat) return;  // the rest act once per press
      switch (b) {
        case Remote::Button::Ok:   stopAll(); break;
        case Remote::Button::Num1: toggleMode(false); break;
        case Remote::Button::Num2: toggleMode(true); break;
        case Remote::Button::Star: handleCommand('-'); break;
        case Remote::Button::Hash: handleCommand('+'); break;
        default: break;  // other buttons unused for now
      }
  }
}

void handleCommand(char c) {
  switch (c) {
    case 'w': driveFor(speed, speed); break;
    case 's': driveFor(-speed, -speed); break;
    case 'a': driveFor(-speed, speed); break;
    case 'd': driveFor(speed, -speed); break;
    case 'x': stopAll(); break;
    case 'o': toggleMode(false); break;
    case 'f': toggleMode(true); break;
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
  Led::begin();
  Avoid::begin();  // sets the LED to blue (idle)
  Follow::begin();
  Remote::begin();
#if KIT_VERSION == 4  // only the V4 shield has the push button
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  buttonDown = lastReading = digitalRead(PIN_BUTTON) == LOW;
  longFired = buttonDown;  // a button held at power-up does nothing
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
  switch (buttonEvent()) {
    case Press::Short: toggleMode(false); break;
    case Press::Long:  toggleMode(true); break;
    default: break;
  }
#endif

  Avoid::update();
  Follow::update();
  handleRemote();

  if (Serial.available()) handleCommand(Serial.read());
}
