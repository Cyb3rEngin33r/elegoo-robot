#include "led.h"
#include "pins.h"

#if KIT_VERSION == 4
#include <Adafruit_NeoPixel.h>

namespace {
constexpr uint8_t BRIGHTNESS = 40;  // 0..255; the LED is very bright at full power
Adafruit_NeoPixel pixel(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
Led::Color currentColor = Led::Color::Off;

uint32_t rgb(Led::Color c) {
  switch (c) {
    case Led::Color::Blue:   return Adafruit_NeoPixel::Color(0, 0, 255);
    case Led::Color::White:  return Adafruit_NeoPixel::Color(255, 255, 255);
    case Led::Color::Green:  return Adafruit_NeoPixel::Color(0, 255, 0);
    case Led::Color::Yellow: return Adafruit_NeoPixel::Color(255, 160, 0);
    case Led::Color::Red:    return Adafruit_NeoPixel::Color(255, 0, 0);
    case Led::Color::Off:
    default:                 return 0;
  }
}
}  // namespace

namespace Led {

void begin() {
  pixel.begin();
  pixel.setBrightness(BRIGHTNESS);
  pixel.clear();
  pixel.show();
  currentColor = Color::Off;
}

void set(Color c) {
  // show() briefly pauses interrupts (which can twitch the servo), so only
  // send to the LED when the colour actually changes.
  if (c == currentColor) return;
  currentColor = c;
  pixel.setPixelColor(0, rgb(c));
  pixel.show();
}

Color current() { return currentColor; }

}  // namespace Led

#else  // V3 kits have no status LED

namespace Led {
void begin() {}
void set(Color) {}
Color current() { return Color::Off; }
}  // namespace Led

#endif

namespace Led {
const __FlashStringHelper* name(Color c) {
  switch (c) {
    case Color::Blue:   return F("blue");
    case Color::White:  return F("white");
    case Color::Green:  return F("green");
    case Color::Yellow: return F("yellow");
    case Color::Red:    return F("red");
    case Color::Off:
    default:            return F("off");
  }
}
}  // namespace Led
