#pragma once
#include <Arduino.h>

// IR remote (the Elegoo kit remote, NEC protocol) on the IR receiver.
// Holding a button sends repeats about every 110 ms; read() reports each one,
// so a caller can keep driving while a button is held.
namespace Remote {
enum class Button : uint8_t {
  None, Up, Down, Left, Right, Ok,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  Star, Hash, Unknown
};

void begin();
// Returns the button just received (None if nothing new). `repeat` is set
// when it's a "still held" repeat rather than a fresh press.
Button read(bool& repeat);
const __FlashStringHelper* name(Button b);
}  // namespace Remote
