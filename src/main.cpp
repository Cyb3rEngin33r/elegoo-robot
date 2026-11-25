// Elegoo Smart Robot Car: serial test console + obstacle avoidance.
//
// Open the serial monitor at 115200 baud and send single-letter commands:
//   w/s/a/d  forward / back / spin left / spin right (runs for 400 ms);
//            w and s hold a straight line using the gyro
//   q / e    turn exactly 90 degrees left / right (gyro)
//   k        straight-line hold on/off (on at power-up)
//   u        lift stop on/off: motors stop when the car is picked up or
//            tipped over (on at power-up; turn off for wheels-up bench tests)
//   x        stop
//   + / -    change speed
//   p        ping the ultrasonic sensor
//   l        read the line tracker
//   [ / ] /  pan servo left / right / center ( '\' )
//   < / >    nudge the servo's centre trim 1 degree left / right (saved)
//   g        collision guard on/off (stops manual forward driving before
//            obstacles; on at power-up)
//   o        obstacle avoidance on/off
//   f        line following on/off
//   j / J    status reports for the dashboard on / off (JSON, 4 per second)
//   h        help
//
// Shield push button (V4): short press = obstacle avoidance, hold 1 s = line
// following; any press while a mode is running stops it.
//
// IR remote: hold an arrow to drive (stops ~0.25 s after release), OK = stop,
// 1 = obstacle avoidance, 2 = line following, 4 / 6 = turn 90 degrees
// left / right, * / # = slower / faster.
//
// Only isolated characters are commands: a character is acted on once the
// line has been quiet for 5 ms before and after it. Bursts of text, such as
// the camera module's startup messages or {Heartbeat}, are ignored as a whole,
// so their letters can't be mistaken for drive commands.
//
// Keep the car still for the first second after power-up: the gyro measures
// its zero point then.
//
// Bumps (accelerometer, always on): hitting something stops a manual drive
// and refuses a held forward key; obstacle avoidance backs up and looks
// around; line following stops.
//
// The car never drives on power-up. Both modes wait 1 s before moving.
// Any drive command or x also stops them.
//
// Put the car on a stand or hold it with the wheels off the table for the first test.

#include <Arduino.h>
#include "avoid.h"
#include "bump.h"
#include "follow.h"
#include "guard.h"
#include "imu.h"
#include "led.h"
#include "lift.h"
#include "motors.h"
#include "pins.h"
#include "remote.h"
#include "status.h"
#include "sensors.h"
#include "steer.h"

namespace {
int speed = 150;
int panAngle = 90;
unsigned long driveUntil = 0;
// Serial command filter (see the comment at the top).
constexpr unsigned long QUIET_US = 5000;
char pendingCommand = 0;
unsigned long lastByteAt = 0;

void help() {
  Serial.println(F("Commands: w s a d (drive) x (stop) +/- (speed) p (ping) "
                   "q e (turn 90) k (straight hold) u (lift stop) l (line) [ ] \\ (servo) < > (trim) g (guard) o (avoid) f (follow line) j/J (status on/off) h (help)"));
}

bool modeActive() { return Avoid::active() || Follow::active(); }

void stopAll() {
  Avoid::stop();
  Follow::stop();
  Bump::clear();
  Steer::cancel();
  Motors::stop();
  driveUntil = 0;
}

void stopDriving() {
  Steer::cancel();
  Motors::stop();
  driveUntil = 0;
}

// Picked up or tipped over: say so (not on every repeated command).
bool refuseOffGround() {
  if (Lift::allowsDriving()) return false;
  static unsigned long lastSaid = 0;
  if (!lastSaid || millis() - lastSaid > 2000) {
    lastSaid = millis();
    Serial.println(F("lift: off its wheels, not driving (u turns this check off)"));
  }
  return true;
}

void driveFor(int left, int right, unsigned long ms = 400) {
  Avoid::stop();
  Follow::stop();
  if (refuseOffGround()) {
    stopDriving();
    return;
  }
  int direction = (left > 0 && right > 0) ? 1 : (left < 0 && right < 0) ? -1 : 0;
  if (!Bump::allows(direction)) {  // just hit something this way: refuse
    stopDriving();
    return;
  }
  Bump::clear();
  if (direction > 0) {
    if (!Guard::allowForward(speed)) {  // obstacle ahead: refuse
      stopDriving();
      return;
    }
  } else {
    Guard::clear();
  }
  if (left == right) {
    Steer::straight(left);
  } else {
    Steer::cancel();
    Motors::drive(left, right);
  }
  driveUntil = millis() + ms;
}

// Gyro turn on the spot; positive = left.
void turnBy(int degrees) {
  stopAll();  // also forgets a bump: turning away is how to get clear
  if (refuseOffGround()) return;
  Guard::clear();
  Steer::turn(degrees, speed);
}

// Start a mode, or stop everything if any mode is already running.
void toggleMode(bool lineFollowing) {
  if (modeActive()) {
    stopAll();
    return;
  }
  stopAll();
  if (refuseOffGround()) return;
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
        case Remote::Button::Num4: turnBy(90); break;
        case Remote::Button::Num6: turnBy(-90); break;
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
    case 'q': turnBy(90); break;
    case 'e': turnBy(-90); break;
    case 'k': Steer::setHoldEnabled(!Steer::holdEnabled()); break;
    case 'u': Lift::setEnabled(!Lift::enabled()); break;
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
    case 'j': Status::setEnabled(true); break;
    case 'J': Status::setEnabled(false); break;
    case 'g': Guard::setEnabled(!Guard::enabled()); break;
    case '<':
    case '>':
      Sensors::setTrim(Sensors::trim() + (c == '<' ? 1 : -1));
      Serial.print(F("servo trim: "));
      Serial.println(Sensors::trim());
      break;
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
  Imu::begin();
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
  Imu::update();
  Sensors::update();
  if (driveUntil && (long)(millis() - driveUntil) >= 0) stopDriving();
  // Collision guard while driving forward manually.
  if (driveUntil && Motors::lastLeft() > 0 && Motors::lastRight() > 0 && Guard::check(speed)) {
    stopDriving();
  }
  Steer::update();

  // Picked up or tipped over: stop everything.
  if (Lift::update()) stopAll();

  // Bumps (accelerometer): stop whatever is driving.
  Bump::Hit hit = Bump::update();
  if (hit != Bump::Hit::None) {
    if (Avoid::active()) {
      Avoid::bumped(hit == Bump::Hit::Front);
    } else if (Follow::active()) {
      Follow::stop();
    } else {
      stopDriving();
    }
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

  // Status report: which mode is in charge, and what it's doing.
  if (Avoid::active())       Status::update(F("avoid"), Avoid::stateName(), speed);
  else if (Follow::active()) Status::update(F("follow"), Follow::stateName(), speed);
  else if (Steer::turning()) Status::update(F("manual"), F("turning"), speed);
  else if (driveUntil)       Status::update(F("manual"), F("driving"), speed);
  else                       Status::update(F("idle"), F("idle"), speed);

  while (Serial.available()) {
    char c = Serial.read();
    unsigned long now = micros();
    // Part of a burst: drop it, and drop the character that started it.
    pendingCommand = (now - lastByteAt < QUIET_US) ? 0 : c;
    lastByteAt = now;
  }
  if (pendingCommand && micros() - lastByteAt >= QUIET_US) {
    handleCommand(pendingCommand);
    pendingCommand = 0;
  }
}
