#include "bump.h"
#include "imu.h"
#include "motors.h"

namespace {
constexpr float BUMP_G = 3.0f;               // normal driving stays under ~2 g
constexpr unsigned long STILL_MOVING_MS = 300;  // the car coasts after the motors stop
constexpr unsigned long REBOUND_MS = 500;    // one hit shakes for a while: count it once
// A held key or remote button repeats every 0.1-0.2 s. While the repeats
// keep coming the way the car hit, they're refused; after a pause this long
// (the key was let go) the next press is allowed.
constexpr unsigned long LET_GO_MS = 600;

unsigned long forwardUntil = 0, reverseUntil = 0, lastHitAt = 0, lastRefused = 0;
int blocked = 0;   // +1 forward, -1 back, 0 none
uint16_t hits = 0;
float strength = 0;

bool recent(unsigned long until) { return (long)(until - millis()) > 0; }
}  // namespace

namespace Bump {

Hit update() {
  float lowest, highest;
  Imu::takeSurge(lowest, highest);
  int l = Motors::lastLeft(), r = Motors::lastRight();
  if (l > 0 && r > 0) forwardUntil = millis() + STILL_MOVING_MS;
  if (l < 0 && r < 0) reverseUntil = millis() + STILL_MOVING_MS;
  if (hits && millis() - lastHitAt < REBOUND_MS) return Hit::None;

  Hit hit = Hit::None;
  if (recent(forwardUntil) && lowest < -BUMP_G) {
    hit = Hit::Front;
    strength = -lowest;
  } else if (recent(reverseUntil) && highest > BUMP_G) {
    hit = Hit::Rear;
    strength = highest;
  }
  if (hit == Hit::None) return hit;
  hits++;
  lastHitAt = lastRefused = millis();
  blocked = hit == Hit::Front ? 1 : -1;
  Serial.print(hit == Hit::Front ? F("bump: front, ") : F("bump: rear, "));
  Serial.print(strength, 1);
  Serial.println(F(" g, stopped"));
  return hit;
}

bool allows(int direction) {
  if (direction == 0 || direction != blocked) return true;
  if (millis() - lastRefused >= LET_GO_MS) {  // a new press, not the held one
    blocked = 0;
    return true;
  }
  lastRefused = millis();
  return false;
}
void clear() { blocked = 0; }
uint16_t count() { return hits; }
float lastG() { return strength; }

}  // namespace Bump
