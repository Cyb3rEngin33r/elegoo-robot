#include "remote.h"
#include "pins.h"

#define DECODE_NEC   // the Elegoo remote
#define DECODE_HASH  // any other IR signal: report it as "unknown signal" for diagnosis
#include <IRremote.hpp>

namespace {

// NEC command codes of the Elegoo kit remote. If a button isn't recognised,
// its code is printed as "IR: unknown ..." so it can be added here.
struct Mapping { uint8_t command; Remote::Button button; };
const Mapping MAP[] = {
  {0x46, Remote::Button::Up},   {0x15, Remote::Button::Down},
  {0x44, Remote::Button::Left}, {0x43, Remote::Button::Right},
  {0x40, Remote::Button::Ok},
  {0x52, Remote::Button::Num0}, {0x16, Remote::Button::Num1},
  {0x19, Remote::Button::Num2}, {0x0D, Remote::Button::Num3},
  {0x0C, Remote::Button::Num4}, {0x18, Remote::Button::Num5},
  {0x5E, Remote::Button::Num6}, {0x08, Remote::Button::Num7},
  {0x1C, Remote::Button::Num8}, {0x5A, Remote::Button::Num9},
  {0x42, Remote::Button::Star}, {0x4A, Remote::Button::Hash},
};

}  // namespace

namespace Remote {

void begin() { IrReceiver.begin(PIN_IR_RX, DISABLE_LED_FEEDBACK); }

Button read(bool& repeat) {
  repeat = false;
  if (!IrReceiver.decode()) return Button::None;

  const IRData& d = IrReceiver.decodedIRData;
  Button result = Button::None;
  if (d.protocol == NEC) {
    repeat = d.flags & IRDATA_FLAGS_IS_REPEAT;
    result = Button::Unknown;
    for (const Mapping& m : MAP) {
      if (m.command == d.command) {
        result = m.button;
        break;
      }
    }
    if (result == Button::Unknown && !repeat) {
      Serial.print(F("IR: unknown NEC address 0x"));
      Serial.print(d.address, HEX);
      Serial.print(F(" command 0x"));
      Serial.println(d.command, HEX);
    }
  } else if (d.protocol == UNKNOWN && !(d.flags & IRDATA_FLAGS_IS_REPEAT)) {
    Serial.print(F("IR: unknown signal, not NEC (hash 0x"));
    Serial.print((uint32_t)d.decodedRawData, HEX);
    Serial.print(F(", "));
    Serial.print(d.rawlen);
    Serial.println(F(" edges)"));
  }
  IrReceiver.resume();
  return result;
}

const __FlashStringHelper* name(Button b) {
  switch (b) {
    case Button::Up:    return F("up");
    case Button::Down:  return F("down");
    case Button::Left:  return F("left");
    case Button::Right: return F("right");
    case Button::Ok:    return F("ok");
    case Button::Num0:  return F("0");
    case Button::Num1:  return F("1");
    case Button::Num2:  return F("2");
    case Button::Num3:  return F("3");
    case Button::Num4:  return F("4");
    case Button::Num5:  return F("5");
    case Button::Num6:  return F("6");
    case Button::Num7:  return F("7");
    case Button::Num8:  return F("8");
    case Button::Num9:  return F("9");
    case Button::Star:  return F("*");
    case Button::Hash:  return F("#");
    case Button::Unknown: return F("unknown");
    case Button::None:
    default:            return F("none");
  }
}

}  // namespace Remote
