#include "camera_stream.h"

#include <esp_camera.h>
#include <esp_http_server.h>

namespace {

// Camera wiring of the Elegoo ESP32-S3 module, read from Elegoo's original
// firmware (2026-11-22). It is Espressif's standard ESP32-S3-EYE wiring.
// The sensor on this kit is an OV3660.
constexpr int PIN_PWDN = -1, PIN_RESET = -1, PIN_XCLK = 15;
constexpr int PIN_SIOD = 4, PIN_SIOC = 5;
constexpr int PIN_D7 = 16, PIN_D6 = 17, PIN_D5 = 18, PIN_D4 = 12;
constexpr int PIN_D3 = 10, PIN_D2 = 8, PIN_D1 = 9, PIN_D0 = 11;
constexpr int PIN_VSYNC = 6, PIN_HREF = 7, PIN_PCLK = 13;

#define PART_BOUNDARY "elegoocarframe"
const char* STREAM_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

bool cameraOk = false;
String cameraInfo = "not started";
volatile uint32_t framesSent = 0;
volatile uint8_t viewers = 0;
httpd_handle_t streamServer = nullptr;
httpd_handle_t controlServer = nullptr;

void allowAnyOrigin(httpd_req_t* req) {
  // The dashboard page is served by the laptop, so allow cross-origin use.
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

esp_err_t streamHandler(httpd_req_t* req) {
  if (!cameraOk) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not available");
    return ESP_FAIL;
  }
  allowAnyOrigin(req);
  httpd_resp_set_type(req, STREAM_TYPE);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  viewers++;
  esp_err_t res = ESP_OK;
  char part[64];
  while (res == ESP_OK) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      int n = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
      res = httpd_resp_send_chunk(req, part, n);
    }
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res == ESP_OK) framesSent++;
  }
  viewers--;
  return res;  // the viewer went away
}

esp_err_t captureHandler(httpd_req_t* req) {
  camera_fb_t* fb = cameraOk ? esp_camera_fb_get() : nullptr;
  if (!fb) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not available");
    return ESP_FAIL;
  }
  allowAnyOrigin(req);
  httpd_resp_set_type(req, "image/jpeg");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

framesize_t parseFramesize(const char* v) {
  if (!strcmp(v, "qvga")) return FRAMESIZE_QVGA;   // 320x240
  if (!strcmp(v, "vga")) return FRAMESIZE_VGA;     // 640x480
  if (!strcmp(v, "svga")) return FRAMESIZE_SVGA;   // 800x600
  if (!strcmp(v, "xga")) return FRAMESIZE_XGA;     // 1024x768
  if (!strcmp(v, "hd")) return FRAMESIZE_HD;       // 1280x720
  return FRAMESIZE_INVALID;
}

// /control?framesize=vga&quality=12&vflip=1&hmirror=1 (any subset)
esp_err_t controlHandler(httpd_req_t* req) {
  allowAnyOrigin(req);
  sensor_t* s = cameraOk ? esp_camera_sensor_get() : nullptr;
  if (!s) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not available");
    return ESP_FAIL;
  }
  char query[128] = {0}, val[16];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  if (httpd_query_key_value(query, "framesize", val, sizeof(val)) == ESP_OK) {
    framesize_t f = parseFramesize(val);
    if (f != FRAMESIZE_INVALID) s->set_framesize(s, f);
  }
  if (httpd_query_key_value(query, "quality", val, sizeof(val)) == ESP_OK)
    s->set_quality(s, constrain(atoi(val), 4, 63));
  if (httpd_query_key_value(query, "vflip", val, sizeof(val)) == ESP_OK)
    s->set_vflip(s, atoi(val) ? 1 : 0);
  if (httpd_query_key_value(query, "hmirror", val, sizeof(val)) == ESP_OK)
    s->set_hmirror(s, atoi(val) ? 1 : 0);
  char out[160];
  snprintf(out, sizeof(out),
           "{\"framesize\":%d,\"quality\":%d,\"vflip\":%d,\"hmirror\":%d}",
           s->status.framesize, s->status.quality, s->status.vflip, s->status.hmirror);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, out, strlen(out));
}

}  // namespace

namespace CameraStream {

bool begin() {
  camera_config_t c = {};
  c.pin_pwdn = PIN_PWDN;
  c.pin_reset = PIN_RESET;
  c.pin_xclk = PIN_XCLK;
  c.pin_sccb_sda = PIN_SIOD;
  c.pin_sccb_scl = PIN_SIOC;
  c.pin_d7 = PIN_D7; c.pin_d6 = PIN_D6; c.pin_d5 = PIN_D5; c.pin_d4 = PIN_D4;
  c.pin_d3 = PIN_D3; c.pin_d2 = PIN_D2; c.pin_d1 = PIN_D1; c.pin_d0 = PIN_D0;
  c.pin_vsync = PIN_VSYNC;
  c.pin_href = PIN_HREF;
  c.pin_pclk = PIN_PCLK;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_JPEG;
  c.grab_mode = CAMERA_GRAB_LATEST;  // always send the newest frame
  if (psramFound()) {
    c.frame_size = FRAMESIZE_VGA;  // 640x480: smooth over Wi-Fi
    c.jpeg_quality = 12;           // lower = better quality, bigger frames
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 15;
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    cameraInfo = "init failed: error 0x" + String(err, HEX);
    Serial.println("Camera " + cameraInfo);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    // As in Espressif's CameraWebServer example: the OV3660 image comes out
    // upside down, a little dark and oversaturated by default.
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  char pid[12];
  snprintf(pid, sizeof(pid), "0x%04X", s ? s->id.PID : 0);
  const char* model = !s ? "?" : s->id.PID == OV3660_PID ? "OV3660"
                     : s->id.PID == OV2640_PID ? "OV2640" : s->id.PID == OV5640_PID ? "OV5640" : "other";
  cameraInfo = String(model) + " (PID " + pid + ")" + (psramFound() ? ", PSRAM " : ", no PSRAM ") +
               String(ESP.getPsramSize() / (1024 * 1024)) + " MB";
  Serial.println("Camera ready: " + cameraInfo);
  cameraOk = true;
  return true;
}

void startServers() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.ctrl_port = 32768;
  httpd_uri_t capture = {"/capture", HTTP_GET, captureHandler, nullptr};
  httpd_uri_t control = {"/control", HTTP_GET, controlHandler, nullptr};
  if (httpd_start(&controlServer, &cfg) == ESP_OK) {
    httpd_register_uri_handler(controlServer, &capture);
    httpd_register_uri_handler(controlServer, &control);
  }
  // The stream handler loops for as long as someone watches, so it gets its
  // own server; otherwise it would block /capture and /control.
  cfg.server_port = 81;
  cfg.ctrl_port = 32769;
  httpd_uri_t stream = {"/stream", HTTP_GET, streamHandler, nullptr};
  if (httpd_start(&streamServer, &cfg) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &stream);
  }
  Serial.println("Camera: stream on :81/stream, capture and control on :80");
}

String statusLine() {
  return "camera: " + cameraInfo + ", viewers " + String(viewers) + ", frames sent " +
         String(framesSent);
}

}  // namespace CameraStream
