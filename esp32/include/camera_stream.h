#pragma once
#include <Arduino.h>

// Camera on the Elegoo ESP32-S3 module, served over HTTP:
//   http://<car>:81/stream    MJPEG live video (one viewer at a time)
//   http://<car>/capture      single JPEG
//   http://<car>/control?framesize=vga&quality=12&vflip=1&hmirror=0
// If the camera doesn't start, everything else keeps working.
namespace CameraStream {
bool begin();          // starts the camera; call once
void startServers();   // starts the HTTP servers; call once Wi-Fi is up
String statusLine();   // for the tools port
}  // namespace CameraStream
