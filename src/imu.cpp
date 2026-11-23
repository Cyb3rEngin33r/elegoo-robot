#include "imu.h"
#include <Wire.h>
#include "motors.h"

namespace {

enum class Chip : uint8_t { None, Mpu6050, Qmi8658 };

// If the car turns left but heading() goes down, flip this.
constexpr float YAW_SIGN = 1.0f;

constexpr unsigned long READ_EVERY_US = 4000;  // ~250 readings a second
// Zero point at power-up: average blocks of readings until two blocks in a
// row are still (little spread) and agree; give up after ~5 s.
constexpr int BLOCK_SAMPLES = 50;              // 0.2 s
constexpr float STILL_SPREAD_DPS = 2.0f;       // max - min within a still block
constexpr float BLOCK_AGREE_DPS = 0.3f;
constexpr unsigned long CALIBRATION_MAX_MS = 5000;
// While the motors are off and the reading is this close to the zero point,
// let the zero point follow it slowly (gyro drift changes with temperature).
constexpr float STILL_DPS = 1.5f;
constexpr float BIAS_FOLLOW = 0.002f;
// Below this the reading is treated as noise while standing still.
constexpr float NOISE_DPS = 0.3f;

Chip chip = Chip::None;
uint8_t addr = 0;
float countsPerDps = 1;
float bias = 0;         // raw counts at rest
float yaw = 0;          // degrees
float yawRate = 0;      // degrees per second
unsigned long lastRead = 0;

// Health: the motors can upset the chip or the I2C bus. Failed reads, a
// chip that has reset itself (it then sleeps) or a reading frozen at one
// value all lead to setting the chip up again.
constexpr uint8_t MAX_FAILED_READS = 10;
constexpr uint8_t MAX_SAME_READINGS = 60;      // ~0.25 s of identical values
constexpr unsigned long CHECK_EVERY_MS = 500;
uint16_t readErrors = 0, restarts = 0;
uint8_t failStreak = 0, sameCount = 0;
int16_t previousZ = 0;
unsigned long lastCheck = 0;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

int readReg(uint8_t a, uint8_t reg) {
  addr = a;
  uint8_t v;
  return readRegs(reg, &v, 1) ? v : -1;
}

bool initMpu6050() {
  // Wake up, clock from the gyro's PLL; 44 Hz filter; +/-500 deg/s.
  if (!writeReg(0x6B, 0x01)) return false;
  delay(50);
  writeReg(0x1A, 0x03);
  writeReg(0x1B, 0x08);
  countsPerDps = 65.5f;
  return true;
}

bool initQmi8658() {
  writeReg(0x60, 0xB0);  // soft reset
  delay(20);
  // Register auto-increment, little-endian; gyro +/-512 deg/s at 224 Hz;
  // gyro on, accelerometer off.
  if (!writeReg(0x02, 0x40)) return false;
  writeReg(0x04, 0x55);
  writeReg(0x08, 0x02);
  delay(100);  // gyro start-up
  countsPerDps = 64.0f;
  return true;
}

bool readRaw(int16_t& z);

// Average of one block of readings; false if the car moved during it.
bool stillBlock(float& mean) {
  long sum = 0;
  int16_t lo = INT16_MAX, hi = INT16_MIN;
  int n = 0;
  for (int i = 0; i < BLOCK_SAMPLES; i++) {
    int16_t z;
    if (readRaw(z)) {
      sum += z;
      lo = min(lo, z);
      hi = max(hi, z);
      n++;
    }
    delay(4);
  }
  if (n < BLOCK_SAMPLES / 2) return false;
  mean = (float)sum / n;
  return (hi - lo) / countsPerDps <= STILL_SPREAD_DPS;
}

bool calibrate() {
  float previous = 0, mean = 0;
  bool havePrevious = false;
  unsigned long start = millis();
  while (millis() - start < CALIBRATION_MAX_MS) {
    if (!stillBlock(mean)) {
      havePrevious = false;
      continue;
    }
    if (havePrevious && fabs(mean - previous) / countsPerDps <= BLOCK_AGREE_DPS) {
      bias = (mean + previous) / 2;
      return true;
    }
    previous = mean;
    havePrevious = true;
  }
  bias = mean;  // best guess; it corrects itself slowly while standing still
  return false;
}

bool readRaw(int16_t& z) {
  uint8_t b[2];
  if (chip == Chip::Mpu6050) {
    if (!readRegs(0x47, b, 2)) return false;  // GYRO_ZOUT, big-endian
    z = (int16_t)((b[0] << 8) | b[1]);
  } else {
    if (!readRegs(0x3F, b, 2)) return false;  // GZ, little-endian
    z = (int16_t)((b[1] << 8) | b[0]);
  }
  return true;
}

// The register that setup wrote last, and its value: if it reads back
// differently, the chip has reset.
bool configIntact() {
  uint8_t v;
  if (chip == Chip::Mpu6050) return readRegs(0x6B, &v, 1) && v == 0x01;
  return readRegs(0x08, &v, 1) && v == 0x02;
}

void restart(const __FlashStringHelper* why) {
  restarts++;
  Serial.print(F("imu: "));
  Serial.print(why);
  Serial.println(F(", restarting the gyro"));
  if (chip == Chip::Mpu6050) initMpu6050();
  else initQmi8658();
  failStreak = sameCount = 0;
  lastRead = micros();
}

void printScan() {
  Serial.print(F("imu: no gyro found; I2C devices:"));
  bool any = false;
  for (uint8_t a = 8; a < 120; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x"));
      Serial.print(a, HEX);
      any = true;
    }
  }
  Serial.println(any ? F("") : F(" none"));
}

}  // namespace

namespace Imu {

bool begin() {
  Wire.begin();
  Wire.setClock(400000);
  Wire.setWireTimeout(3000, true);  // a stuck bus must not hang the car

  const uint8_t mpuAddrs[] = {0x68, 0x69};
  for (uint8_t a : mpuAddrs) {
    int who = readReg(a, 0x75);
    if (who == 0x68 || who == 0x70 || who == 0x72 || who == 0x98) {
      if (initMpu6050()) chip = Chip::Mpu6050;
      break;
    }
  }
  if (chip == Chip::None) {
    const uint8_t qmiAddrs[] = {0x6B, 0x6A};
    for (uint8_t a : qmiAddrs) {
      if (readReg(a, 0x00) == 0x05) {
        if (initQmi8658()) chip = Chip::Qmi8658;
        break;
      }
    }
  }
  if (chip == Chip::None) {
    printScan();
    return false;
  }

  bool still = calibrate();
  lastRead = micros();
  Serial.print(F("imu: "));
  Serial.print(name());
  Serial.print(F(" at 0x"));
  Serial.print(addr, HEX);
  Serial.print(F(", zero point "));
  Serial.print(bias / countsPerDps, 2);
  Serial.println(still ? F(" deg/s") : F(" deg/s (car was moving: may be off)"));
  return true;
}

void update() {
  if (chip == Chip::None) return;
  unsigned long now = micros();
  unsigned long dt = now - lastRead;
  if (dt < READ_EVERY_US) return;
  int16_t z;
  if (!readRaw(z)) {
    readErrors++;
    if (++failStreak >= MAX_FAILED_READS) restart(F("reads failing"));
    return;
  }
  failStreak = 0;
  sameCount = (z == previousZ) ? sameCount + 1 : 0;
  previousZ = z;
  if (sameCount >= MAX_SAME_READINGS) {
    restart(F("reading frozen"));
    return;
  }
  if (millis() - lastCheck >= CHECK_EVERY_MS) {
    lastCheck = millis();
    if (!configIntact()) {
      restart(F("chip reset"));
      return;
    }
  }
  lastRead = now;

  bool still = Motors::lastLeft() == 0 && Motors::lastRight() == 0;
  float dps = (z - bias) / countsPerDps;
  if (still && fabs(dps) < STILL_DPS) {
    bias += (z - bias) * BIAS_FOLLOW;
    if (fabs(dps) < NOISE_DPS) dps = 0;
  }
  yawRate = YAW_SIGN * dps;
  // Long gaps (a slow ultrasonic ping) are still integrated, but capped.
  yaw += yawRate * min(dt, 50000UL) * 1e-6f;
}

bool ok() { return chip != Chip::None; }

const __FlashStringHelper* name() {
  switch (chip) {
    case Chip::Mpu6050: return F("MPU6050");
    case Chip::Qmi8658: return F("QMI8658C");
    default: return F("none");
  }
}

float heading() { return yaw; }
uint16_t errors() { return readErrors; }
uint16_t restartCount() { return restarts; }
float rate() { return yawRate; }

}  // namespace Imu
