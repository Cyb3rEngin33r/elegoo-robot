#pragma once
#include <Arduino.h>

// Pin maps for the Elegoo Smart Robot Car kits (UNO R3 + expansion shield).
// Select the kit with -D KIT_VERSION in platformio.ini.

#ifndef KIT_VERSION
#define KIT_VERSION 4
#endif

#if KIT_VERSION == 4
// ---- V4.0: TB6612FNG motor driver on the Smart Car Shield ----
#define MOTOR_DRIVER_TB6612
constexpr uint8_t PIN_MOTOR_STBY = 3;   // driver enable (HIGH = on)
constexpr uint8_t PIN_MOTOR_PWMA = 5;   // right side speed
constexpr uint8_t PIN_MOTOR_AIN1 = 7;   // right side direction
constexpr uint8_t PIN_MOTOR_PWMB = 6;   // left side speed
constexpr uint8_t PIN_MOTOR_BIN1 = 8;   // left side direction

constexpr uint8_t PIN_US_TRIG = 13;
constexpr uint8_t PIN_US_ECHO = 12;
constexpr uint8_t PIN_SERVO   = 10;     // ultrasonic pan servo

constexpr uint8_t PIN_LINE_L = A2;      // ITR20001 analog line sensors
constexpr uint8_t PIN_LINE_M = A1;
constexpr uint8_t PIN_LINE_R = A0;

constexpr uint8_t PIN_IR_RX   = 9;
constexpr uint8_t PIN_RGB_LED = 4;      // single WS2812 (needs FastLED)
constexpr uint8_t PIN_BUTTON  = 2;
constexpr uint8_t PIN_VOLTAGE = A3;

#elif KIT_VERSION == 3
// ---- V3.0: L298N motor driver ----
#define MOTOR_DRIVER_L298N
constexpr uint8_t PIN_MOTOR_ENA = 5;    // right side speed
constexpr uint8_t PIN_MOTOR_IN1 = 7;
constexpr uint8_t PIN_MOTOR_IN2 = 8;
constexpr uint8_t PIN_MOTOR_ENB = 6;    // left side speed
constexpr uint8_t PIN_MOTOR_IN3 = 9;
constexpr uint8_t PIN_MOTOR_IN4 = 11;

constexpr uint8_t PIN_US_TRIG = A5;
constexpr uint8_t PIN_US_ECHO = A4;
constexpr uint8_t PIN_SERVO   = 3;

constexpr uint8_t PIN_LINE_L = 2;       // digital line sensors
constexpr uint8_t PIN_LINE_M = 4;
constexpr uint8_t PIN_LINE_R = 10;

constexpr uint8_t PIN_IR_RX = 12;

#else
#error "Unsupported KIT_VERSION (use 3 or 4)"
#endif
