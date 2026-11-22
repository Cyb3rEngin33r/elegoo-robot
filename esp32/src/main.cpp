// Elegoo Smart Robot Car V4.0 camera module (ESP32-S3): Wi-Fi serial bridge.
//
// Joins your home Wi-Fi as "elegoo-car" (elegoo-car.local).
//
// TCP 2323  bridge: everything the UNO prints goes to the client, everything
//           the client sends goes to the UNO. One client at a time; a new
//           connection replaces the old one. Needs the link pins (below).
// TCP 2324  tools, one command per line:
//             status            pins, Wi-Fi, byte counters
//             scan              list free pins pulled high externally (the
//                               shield's level shifter pulls its lines high)
//             edges <seconds>   count signal changes on every free pin; make
//                               the car print meanwhile (e.g. remote OK button)
//             findtx <rx> [pins...]  with the car's output on <rx>, try pins
//                               as our TX by sending `h` (help) and looking for
//                               the car's reply. Default pins skip the usual
//                               ESP32-S3 camera wiring (GPIO 4-13, 15-18).
//             pins <rx> <tx>    save the link pins and start the bridge
//             listen <s> [baud] [pinA pinB]  receive on two pins at once and
//                               show what arrives on each (default 115200 baud,
//                               GPIO43 and GPIO44). Read-only.
//
// Elegoo doesn't document which pins the S3 module uses for the UNO link.
// They were found with the tools above (2026-11-22): the car's output arrives
// on GPIO3 and GPIO40 goes to the car (the shield's level shifter holds it
// high). Those are the defaults; `pins` overrides them and saves to flash.
// GPIO43 is the chip's boot-log pin and is NOT connected to the car.
// The module's USB port prints the same diagnostics.
//
// Camera (see camera_stream.h): MJPEG video at http://elegoo-car:81/stream.

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

#include "camera_stream.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h and fill in your Wi-Fi details"
#endif

namespace {

constexpr const char* HOSTNAME = "elegoo-car";
constexpr uint16_t BRIDGE_PORT = 2323;
constexpr uint16_t TOOLS_PORT = 2324;
constexpr uint32_t UNO_BAUD = 115200;  // must match Serial.begin() in the UNO firmware

// ESP32-S3 pins that are safe to probe. Left out: 0, 3, 45, 46 (boot
// strapping), 19, 20 (USB), 26-32 (flash), 33-37 (built-in octal PSRAM).
// Probing only reads, except findtx, which briefly sends on pulled-high pins.
constexpr uint8_t CANDIDATES[] = {1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                  17, 18, 21, 38, 39, 40, 41, 42, 43, 44, 47, 48};
constexpr size_t NUM_CANDIDATES = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

WiFiServer bridgeServer(BRIDGE_PORT);
WiFiServer toolsServer(TOOLS_PORT);
WiFiClient bridgeClient;
WiFiClient toolsClient;
String toolsLine;
Preferences prefs;

constexpr int DEFAULT_RX_PIN = 3;   // from the car (UNO TX)
constexpr int DEFAULT_TX_PIN = 40;  // to the car (UNO RX)
int rxPin = -1, txPin = -1;  // UNO link
bool bridgeRunning = false;
bool networkStarted = false;
unsigned long lastWifiReport = 0;
volatile int lastDisconnectReason = 0;

// Common reasons a connection attempt fails (ESP-IDF wifi_err_reason_t).
const char* reasonText(int r) {
  switch (r) {
    case 2: case 202: return "authentication failed (wrong password?)";
    case 15: case 204: return "handshake timed out (wrong password?)";
    case 201: return "network not found (name wrong, out of range, or 5 GHz only?)";
    case 203: return "association failed";
    case 205: return "connection failed";
    case 8: return "left the network";
    default: return "see ESP-IDF wifi_err_reason_t";
  }
}

void reportWifi() {
  if (millis() - lastWifiReport < 5000) return;
  lastWifiReport = millis();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s, rssi %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    int r = lastDisconnectReason;
    Serial.printf("[wifi] not connected (status %d), last failure reason %d: %s\n",
                  (int)WiFi.status(), r, r ? reasonText(r) : "none yet");
  }
}
uint32_t bytesFromUno = 0, bytesToUno = 0;

// ---- output to both the USB log and the tools client ----
void say(const String& s) {
  Serial.println(s);
  if (toolsClient && toolsClient.connected()) toolsClient.println(s);
}

// ---- UNO link ----
void startBridge() {
  if (rxPin < 0 || txPin < 0) {
    say("Link pins not set yet: use the tools port (2324) to find them");
    return;
  }
  Serial1.end();
  Serial1.setRxBufferSize(1024);
  Serial1.begin(UNO_BAUD, SERIAL_8N1, rxPin, txPin);
  bridgeRunning = true;
  say("Bridge running: RX GPIO" + String(rxPin) + ", TX GPIO" + String(txPin) +
      ", " + String(UNO_BAUD) + " baud");
}

void stopBridge() {
  if (!bridgeRunning) return;
  Serial1.end();
  bridgeRunning = false;
}

// ---- pin-finding tools ----
volatile uint32_t edgeCounts[NUM_CANDIDATES];

void IRAM_ATTR onEdge(void* arg) { edgeCounts[(uintptr_t)arg]++; }

void releasePin(uint8_t pin) { pinMode(pin, INPUT); }

String pulledHighPins() {
  String out;
  for (uint8_t pin : CANDIDATES) {
    if (bridgeRunning && (pin == rxPin || pin == txPin)) continue;
    pinMode(pin, INPUT_PULLDOWN);
    delayMicroseconds(500);
    bool high = digitalRead(pin) == HIGH;
    releasePin(pin);
    if (high) out += String(pin) + " ";
  }
  return out;
}

void cmdScan() {
  String pins = pulledHighPins();
  say(pins.length() ? "Pulled high (likely serial or I2C lines): " + pins
                    : "No free pin is pulled high externally");
}

void cmdEdges(int seconds) {
  seconds = constrain(seconds, 1, 60);
  stopBridge();
  say("Counting signal changes for " + String(seconds) +
      " s: make the car print now (e.g. press OK on the remote a few times)");
  for (size_t i = 0; i < NUM_CANDIDATES; i++) {
    edgeCounts[i] = 0;
    pinMode(CANDIDATES[i], INPUT);
    attachInterruptArg(CANDIDATES[i], onEdge, (void*)(uintptr_t)i, CHANGE);
  }
  unsigned long until = millis() + seconds * 1000UL;
  while ((long)(until - millis()) > 0) {
    ArduinoOTA.handle();
    delay(10);
  }
  String report;
  for (size_t i = 0; i < NUM_CANDIDATES; i++) {
    detachInterrupt(CANDIDATES[i]);
    if (edgeCounts[i]) report += "GPIO" + String(CANDIDATES[i]) + "=" + String(edgeCounts[i]) + " ";
  }
  say(report.length() ? "Signal changes: " + report : "No signal changes on any free pin");
  if (rxPin >= 0) startBridge();
}

// Not used by the common ESP32-S3 camera wiring; nearest to GPIO40 first.
constexpr const char* DEFAULT_TX_CANDIDATES = "39 41 38 42 21 47 48 14 1 2 43 44";

void cmdFindTx(int rx, String candidates) {
  stopBridge();
  if (!candidates.length()) candidates = DEFAULT_TX_CANDIDATES;
  candidates += " ";
  say("Trying TX on GPIO " + candidates + "with RX GPIO" + String(rx));
  int found = -1;
  int start = 0;
  while (start < (int)candidates.length()) {
    int end = candidates.indexOf(' ', start);
    if (end < 0) end = candidates.length();
    String token = candidates.substring(start, end);
    start = end + 1;
    token.trim();
    if (!token.length()) continue;
    int tx = token.toInt();
    if (tx == rx) continue;

    Serial1.end();
    Serial1.begin(UNO_BAUD, SERIAL_8N1, rx, tx);
    delay(50);
    while (Serial1.available()) Serial1.read();
    Serial1.write('h');  // the car replies with its command list
    String reply;
    unsigned long until = millis() + 400;
    while ((long)(until - millis()) > 0) {
      while (Serial1.available()) reply += (char)Serial1.read();
      delay(5);
    }
    Serial1.end();
    releasePin(tx);
    bool ok = reply.indexOf("Commands") >= 0;
    say("  TX GPIO" + String(tx) + ": " + (ok ? "car replied" : "no reply"));
    if (ok) {
      found = tx;
      break;
    }
  }
  releasePin(rx);
  say(found >= 0 ? "Found it: send `pins " + String(rx) + " " + String(found) + "` to save"
                 : "No TX pin found");
  if (rxPin >= 0) startBridge();
}

String showBytes(const uint8_t* b, size_t n) {
  String text, hex;
  for (size_t i = 0; i < n; i++) {
    char c = (char)b[i];
    text += (c >= 32 && c < 127) ? String(c) : (c == '\n' ? String("\\n") : String("."));
    if (i < 48) {
      char h[4];
      snprintf(h, sizeof(h), "%02X ", b[i]);
      hex += h;
    }
  }
  return String(n) + " bytes: \"" + text.substring(0, 160) + "\"  hex: " + hex;
}

void cmdListen(int seconds, uint32_t baud, int pinA, int pinB) {
  seconds = constrain(seconds, 1, 60);
  stopBridge();
  Serial1.setRxBufferSize(2048);
  Serial2.setRxBufferSize(2048);
  Serial1.begin(baud, SERIAL_8N1, pinA, -1);  // receive only, nothing sent
  Serial2.begin(baud, SERIAL_8N1, pinB, -1);
  say("Listening on GPIO" + String(pinA) + " and GPIO" + String(pinB) + " at " + String(baud) +
      " baud for " + String(seconds) +
      " s: make the car print now");
  static uint8_t a[1024], b[1024];
  size_t na = 0, nb = 0;
  unsigned long until = millis() + seconds * 1000UL;
  while ((long)(until - millis()) > 0) {
    while (Serial1.available() && na < sizeof(a)) a[na++] = Serial1.read();
    while (Serial2.available() && nb < sizeof(b)) b[nb++] = Serial2.read();
    ArduinoOTA.handle();
    delay(2);
  }
  Serial1.end();
  Serial2.end();
  releasePin(pinA);
  releasePin(pinB);
  say("GPIO" + String(pinA) + ": " + showBytes(a, na));
  say("GPIO" + String(pinB) + ": " + showBytes(b, nb));
  if (rxPin >= 0) startBridge();
}

void cmdPins(int rx, int tx) {
  rxPin = rx;
  txPin = tx;
  prefs.putInt("rx", rx);
  prefs.putInt("tx", tx);
  say("Saved link pins RX GPIO" + String(rx) + ", TX GPIO" + String(tx));
  startBridge();
}

void cmdStatus() {
  say("pins: RX " + String(rxPin) + ", TX " + String(txPin) +
      (bridgeRunning ? " (bridge running)" : " (bridge stopped)"));
  say("wifi: " + String(WiFi.status() == WL_CONNECTED ? "up " : "down ") +
      WiFi.localIP().toString() + ", rssi " + String(WiFi.RSSI()) + " dBm");
  say("bytes from car " + String(bytesFromUno) + ", to car " + String(bytesToUno) +
      ", bridge client " + String(bridgeClient && bridgeClient.connected() ? "yes" : "no"));
  say(CameraStream::statusLine());
}

void runToolsCommand(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();

  if (cmd == "status") cmdStatus();
  else if (cmd == "scan") cmdScan();
  else if (cmd == "edges") cmdEdges(arg.length() ? arg.toInt() : 10);
  else if (cmd == "findtx" && arg.length()) {
    int sp2 = arg.indexOf(' ');
    cmdFindTx(arg.substring(0, sp2 < 0 ? arg.length() : sp2).toInt(),
              sp2 < 0 ? String("") : arg.substring(sp2 + 1));
  }
  else if (cmd == "pins" && arg.indexOf(' ') > 0)
    cmdPins(arg.substring(0, arg.indexOf(' ')).toInt(), arg.substring(arg.indexOf(' ') + 1).toInt());
  else if (cmd == "listen") {
    int v[4] = {10, 115200, 43, 44};
    int n = 0, start = 0;
    while (n < 4 && start < (int)arg.length()) {
      int end = arg.indexOf(' ', start);
      if (end < 0) end = arg.length();
      if (end > start) v[n++] = arg.substring(start, end).toInt();
      start = end + 1;
    }
    cmdListen(v[0], v[1] > 0 ? v[1] : 115200, v[2], v[3]);
  }
  else say("commands: status | scan | edges <s> | findtx <rx> [pins] | pins <rx> <tx> | listen <s> [baud] [pinA pinB]");
}

// ---- network ----
void startNetworkServices() {
  say("Wi-Fi connected, IP " + WiFi.localIP().toString());
  if (MDNS.begin(HOSTNAME)) say(String("mDNS: ") + HOSTNAME + ".local");
  else say("mDNS failed to start (use the IP address instead)");

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] { stopBridge(); Serial.println("OTA update starting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %u\n", e); });
  ArduinoOTA.begin();

  CameraStream::startServers();

  bridgeServer.begin();
  bridgeServer.setNoDelay(true);
  toolsServer.begin();
  say("Bridge on TCP " + String(BRIDGE_PORT) + ", tools on TCP " + String(TOOLS_PORT));
  networkStarted = true;
}

void acceptClients() {
  if (bridgeServer.hasClient()) {
    if (bridgeClient && bridgeClient.connected()) bridgeClient.stop();
    bridgeClient = bridgeServer.accept();
    bridgeClient.setNoDelay(true);
    // Lines starting with [esp32] come from the module, not the car.
    bridgeClient.printf("[esp32] connected to %s, %lu bytes received from the car so far%s\n",
                        HOSTNAME, (unsigned long)bytesFromUno,
                        bridgeRunning ? "" : " (link pins not set: bridge not running)");
  }
  if (toolsServer.hasClient()) {
    if (toolsClient && toolsClient.connected()) toolsClient.stop();
    toolsClient = toolsServer.accept();
    toolsLine = "";
    toolsClient.println("elegoo-car tools: status | scan | edges <s> | findtx <rx> [pins] | pins <rx> <tx> | listen <s> [baud] [pinA pinB]");
  }
}

void pumpBridge() {
  if (!bridgeRunning) return;
  uint8_t buf[256];
  size_t n = Serial1.available();
  if (n) {
    n = Serial1.readBytes(buf, min(n, sizeof(buf)));
    bytesFromUno += n;
    if (bridgeClient && bridgeClient.connected()) bridgeClient.write(buf, n);
  }
  if (bridgeClient && bridgeClient.connected()) {
    n = bridgeClient.available();
    if (n) {
      n = bridgeClient.read(buf, min(n, sizeof(buf)));
      bytesToUno += n;
      Serial1.write(buf, n);
    }
  }
}

void pumpTools() {
  if (!(toolsClient && toolsClient.connected())) return;
  while (toolsClient.available()) {
    char c = toolsClient.read();
    if (c == '\n') {
      runToolsCommand(toolsLine);
      toolsLine = "";
    } else if (c != '\r' && toolsLine.length() < 80) {
      toolsLine += c;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nElegoo car Wi-Fi bridge (ESP32-S3) starting");

  prefs.begin("bridge", false);
  rxPin = prefs.getInt("rx", DEFAULT_RX_PIN);
  txPin = prefs.getInt("tx", DEFAULT_TX_PIN);
  startBridge();
  CameraStream::begin();  // carries on without video if the camera fails

  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  // Without this the radio dozes between beacons and can miss ARP requests,
  // which makes the module unreachable on some (mesh) routers.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi...");
}

void loop() {
  if (!networkStarted && WiFi.status() == WL_CONNECTED) startNetworkServices();
  if (networkStarted) {
    ArduinoOTA.handle();
    acceptClients();
    pumpTools();
  }
  pumpBridge();
  reportWifi();
}
