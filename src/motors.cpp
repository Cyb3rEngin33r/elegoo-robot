#include "motors.h"
#include "pins.h"

namespace {

#if defined(MOTOR_DRIVER_TB6612)
void setSide(uint8_t pwmPin, uint8_t dirPin, int speed) {
  digitalWrite(dirPin, speed >= 0 ? HIGH : LOW);
  analogWrite(pwmPin, constrain(abs(speed), 0, 255));
}
#elif defined(MOTOR_DRIVER_L298N)
void setSide(uint8_t enPin, uint8_t inA, uint8_t inB, int speed) {
  digitalWrite(inA, speed > 0 ? HIGH : LOW);
  digitalWrite(inB, speed < 0 ? HIGH : LOW);
  analogWrite(enPin, constrain(abs(speed), 0, 255));
}
#endif

}  // namespace

namespace Motors {

namespace {
int commandedLeft = 0, commandedRight = 0;
}

int lastLeft() { return commandedLeft; }
int lastRight() { return commandedRight; }

void begin() {
#if defined(MOTOR_DRIVER_TB6612)
  const uint8_t pins[] = {PIN_MOTOR_STBY, PIN_MOTOR_PWMA, PIN_MOTOR_AIN1,
                          PIN_MOTOR_PWMB, PIN_MOTOR_BIN1};
  for (uint8_t p : pins) pinMode(p, OUTPUT);
  digitalWrite(PIN_MOTOR_STBY, HIGH);
#elif defined(MOTOR_DRIVER_L298N)
  const uint8_t pins[] = {PIN_MOTOR_ENA, PIN_MOTOR_IN1, PIN_MOTOR_IN2,
                          PIN_MOTOR_ENB, PIN_MOTOR_IN3, PIN_MOTOR_IN4};
  for (uint8_t p : pins) pinMode(p, OUTPUT);
#endif
  stop();
}

void drive(int left, int right) {
  commandedLeft = left;
  commandedRight = right;
  if (INVERT_LEFT) left = -left;
  if (INVERT_RIGHT) right = -right;
#if defined(MOTOR_DRIVER_TB6612)
  setSide(PIN_MOTOR_PWMA, PIN_MOTOR_AIN1, right);
  setSide(PIN_MOTOR_PWMB, PIN_MOTOR_BIN1, left);
#elif defined(MOTOR_DRIVER_L298N)
  setSide(PIN_MOTOR_ENA, PIN_MOTOR_IN1, PIN_MOTOR_IN2, right);
  setSide(PIN_MOTOR_ENB, PIN_MOTOR_IN3, PIN_MOTOR_IN4, left);
#endif
}

void stop() { drive(0, 0); }

}  // namespace Motors
