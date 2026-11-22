#include "led.h"
#include "pins.h"

#if KIT_VERSION == 4
#include <Adafruit_NeoPixel.h>

namespace {
constexpr uint8_t BRIGHTNESS = 40;  // 0..255; the LED is very bright at full power
Adafruit_NeoPixel pixel(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
Led::Color current = Led::Color::Off;

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
  current = Color::Off;
}

void set(Color c) {
  // show() briefly pauses interrupts (which can twitch the servo), so only
  // send to the LED when the colour actually changes.
  if (c == current) return;
  current = c;
  pixel.setPixelColor(0, rgb(c));
  pixel.show();
}

}  // namespace Led

#else  // V3 kits have no status LED

namespace Led {
void begin() {}
void set(Color) {}
}  // namespace Led

#endif
