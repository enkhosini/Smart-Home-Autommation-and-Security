#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>

// ======================================================
// WIFI
// ======================================================

const char* ssid     = "A67C";
const char* password = "ga_group7";

// ======================================================
// FLASK SERVER
// ======================================================

const String SERVER = "http://10.227.19.61:5000";

// ======================================================
// CAMERA WEB SERVER
// ======================================================

WebServer server(80);

// ======================================================
// STREAM SETTINGS
// ======================================================

const unsigned long STREAM_INTERVAL_MS = 150;  // Slightly increased to reduce load

const int STREAM_TIMEOUT_MS  = 3000;  // Increased from 800ms to allow processing

const int CAPTURE_TIMEOUT_MS = 8000;  // Increased from 5000ms

// ======================================================
// AI THINKER PIN MAP
// ======================================================

#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ======================================================
// TIMERS
// ======================================================

unsigned long lastStream = 0;

unsigned long frameCount = 0;

unsigned long lastFpsLog = 0;

unsigned long streamErrors = 0;

// ======================================================
// CAMERA INIT
// ======================================================

void startCamera() {

  Serial.println("[CAM] Initialising...");

  camera_config_t cfg;

  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;

  cfg.pin_d0 = Y2_GPIO_NUM;
  cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM;
  cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM;
  cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM;
  cfg.pin_d7 = Y9_GPIO_NUM;

  cfg.pin_xclk = XCLK_GPIO_NUM;
  cfg.pin_pclk = PCLK_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM;
  cfg.pin_href = HREF_GPIO_NUM;

  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;

  cfg.pin_pwdn  = PWDN_GPIO_NUM;
  cfg.pin_reset = RESET_GPIO_NUM;

  cfg.xclk_freq_hz = 20000000;

  cfg.pixel_format = PIXFORMAT_JPEG;

  cfg.frame_size = FRAMESIZE_QVGA;  // QVGA = 320x240

  cfg.jpeg_quality = 12;  // Lower quality = smaller file = faster upload

  cfg.fb_count = 1;

  cfg.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&cfg);

  if (err != ESP_OK) {

    Serial.println("[CAM] Init FAILED");

    while (true) delay(1000);
  }

  sensor_t* s = esp_camera_sensor_get();

  if (s) {

    s->set_brightness(s, 1);

    s->set_contrast(s, 1);

    s->set_saturation(s, 0);

    s->set_sharpness(s, 1);

    s->set_whitebal(s, 1);

    s->set_awb_gain(s, 1);

    s->set_exposure_ctrl(s, 1);

    s->set_gain_ctrl(s, 1);

    s->set_hmirror(s, 0);

    s->set_vflip(s, 0);
  }

  Serial.println("[CAM] Ready");
}

// ======================================================
// POST JPEG
// ======================================================

int postJpeg(
    const String& endpoint,
    const uint8_t* buf,
    size_t len,
    int timeoutMs
) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] WiFi disconnected!");
    return -99;
  }

  HTTPClient http;

  http.begin(SERVER + endpoint);

  http.addHeader(
      "Content-Type",
      "application/octet-stream"
  );

  http.setTimeout(timeoutMs);

  int code = http.POST(
      const_cast<uint8_t*>(buf),
      len
  );

  if (code < 0) {

    Serial.printf(
      "[NET] POST %s failed: %s (code: %d)\n",
      endpoint.c_str(),
      http.errorToString(code).c_str(),
      code
    );
  }

  http.end();

  return code;
}

// ======================================================
// SEND STREAM FRAME
// ======================================================

void sendStreamFrame() {

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {

    Serial.println("[STREAM] Frame capture failed");

    return;
  }

  Serial.printf("[STREAM] Sending %d bytes...\n", fb->len);

  int code = postJpeg(
      "/cam_stream",
      fb->buf,
      fb->len,
      STREAM_TIMEOUT_MS
  );

  esp_camera_fb_return(fb);

  if (code == 200) {

    frameCount++;

  } else {

    streamErrors++;

    if (streamErrors % 10 == 0) {
      Serial.printf("[STREAM] Error rate: %lu errors\n", streamErrors);
    }
  }
}

// ======================================================
// SEND MOTION CAPTURE
// ======================================================

void sendMotionCapture() {

  Serial.println("[CAPTURE] Taking photo...");

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {

    Serial.println("[CAPTURE] Frame capture failed");

    return;
  }

  Serial.printf("[CAPTURE] Frame size: %d bytes\n", fb->len);

  int code = postJpeg(
      "/motion_capture",
      fb->buf,
      fb->len,
      CAPTURE_TIMEOUT_MS
  );

  esp_camera_fb_return(fb);

  if (code == 200) {

    Serial.println("[CAPTURE] Saved ✓");

  } else if (code == 500) {

    Serial.printf(
      "[CAPTURE] Server error 500 - check Flask logs\n"
    );

  } else {

    Serial.printf(
      "[CAPTURE] Upload failed: %d\n",
      code
    );
  }
}

// ======================================================
// HANDLE PIR REQUEST
// ======================================================

void handleCapture() {

  Serial.println("[SERVER] PIR requested capture");

  sendMotionCapture();

  server.send(
      200,
      "text/plain",
      "Photo captured"
  );
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println("================================");
  Serial.println("     ESP32-CAM SECURITY");
  Serial.println("================================");

  WiFi.begin(ssid, password);

  Serial.print("[WIFI] Connecting");

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println("[WIFI] Connected");

  Serial.print("[WIFI] IP: ");

  Serial.println(WiFi.localIP());

  startCamera();

  // ==================================================
  // CAMERA ENDPOINT
  // ==================================================

  server.on(
      "/capture",
      HTTP_GET,
      handleCapture
  );

  server.begin();

  Serial.println("[SERVER] Ready");
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  // HANDLE INCOMING PIR REQUESTS
  server.handleClient();

  unsigned long now = millis();

  // ==================================================
  // LIVE STREAM
  // ==================================================

  if (now - lastStream >= STREAM_INTERVAL_MS) {

    lastStream = now;

    sendStreamFrame();
  }

  // ==================================================
  // FPS LOG
  // ==================================================

  if (now - lastFpsLog >= 5000) {

    Serial.printf(
      "[STREAM] %.1f FPS | %lu errors\n",
      frameCount / 5.0f,
      streamErrors
    );

    frameCount = 0;

    streamErrors = 0;

    lastFpsLog = now;
  }

  delay(5);
}
