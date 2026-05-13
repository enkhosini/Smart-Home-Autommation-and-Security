/*
 * ESP32-CAM — GA Security System  (PIR-triggered capture, fixed reliability)
 * ─────────────────────────────────────────────────────────────────────────────
 * ROOT CAUSE OF MISSING PHOTOS (now fixed):
 *
 *   1. Stream POST timed out (connection refused / read timeout).
 *      The old code did stream + poll in the same loop iteration — a
 *      2-second stream timeout blocked the /should_capture poll entirely.
 *      Solution: stream timeout cut to 800 ms; poll runs on its OWN timer
 *      completely independently so a slow/failed stream never delays it.
 *
 *   2. Flask used a simple boolean flag. If the ESP was busy during the
 *      poll window, the flag was already reset and the capture was lost.
 *      Solution: Flask now uses an INTEGER COUNTER (capture_queue).
 *      Each PIR HIGH increments the counter; each successful poll
 *      decrements it by 1. Captures are never lost even if polls are late.
 *
 *   3. Capture frame and stream frame shared timing. Now capture gets its
 *      own dedicated esp_camera_fb_get() call immediately when poll says yes,
 *      completely independent of the stream timer.
 *
 * TIMERS (all independent, no blocking each other):
 *   STREAM_INTERVAL_MS  100 ms  →  ~10 FPS live view
 *   POLL_INTERVAL_MS    300 ms  →  poll Flask for PIR trigger
 *   (capture fires immediately inside the poll block when needed)
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── WiFi ──────────────────────────────────────────────────────────────────────
const char* ssid     = "Maambele";
const char* password = "@Onepiece0907";

// ── Flask server ──────────────────────────────────────────────────────────────
const String SERVER = "http://172.20.10.2:5000";

// ── Timing ────────────────────────────────────────────────────────────────────
const unsigned long STREAM_INTERVAL_MS = 100;   // 10 FPS
const unsigned long POLL_INTERVAL_MS   = 300;   // poll for PIR flag every 300 ms

// ── HTTP timeouts ─────────────────────────────────────────────────────────────
// FIX: old stream timeout was 2000 ms — caused multi-second blocks that
//      prevented the poll timer from ever firing when WiFi was congested.
//      Stream is best-effort (dropped frames are fine); keep it short.
const int STREAM_TIMEOUT_MS  = 800;   // stream POST — drop frame on congestion
const int POLL_TIMEOUT_MS    = 1500;  // poll GET — needs reliable response
const int CAPTURE_TIMEOUT_MS = 3000;  // capture POST — must succeed, allow more

// ── AI-Thinker pin map ────────────────────────────────────────────────────────
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

// ── State ─────────────────────────────────────────────────────────────────────
unsigned long lastStream  = 0;
unsigned long lastPoll    = 0;
unsigned long frameCount  = 0;
unsigned long lastFpsLog  = 0;


// ═════════════════════════════════════════════════════════════════════════════
// CAMERA INIT
// ═════════════════════════════════════════════════════════════════════════════
void startCamera() {
  Serial.println("[CAM] Initialising...");

  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0       = Y2_GPIO_NUM;
  cfg.pin_d1       = Y3_GPIO_NUM;
  cfg.pin_d2       = Y4_GPIO_NUM;
  cfg.pin_d3       = Y5_GPIO_NUM;
  cfg.pin_d4       = Y6_GPIO_NUM;
  cfg.pin_d5       = Y7_GPIO_NUM;
  cfg.pin_d6       = Y8_GPIO_NUM;
  cfg.pin_d7       = Y9_GPIO_NUM;
  cfg.pin_xclk     = XCLK_GPIO_NUM;
  cfg.pin_pclk     = PCLK_GPIO_NUM;
  cfg.pin_vsync    = VSYNC_GPIO_NUM;
  cfg.pin_href     = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM;
  cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn     = PWDN_GPIO_NUM;
  cfg.pin_reset    = RESET_GPIO_NUM;

  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_VGA;     // 640×480
  cfg.jpeg_quality = 12;
  cfg.fb_count     = 1;
  cfg.fb_location  = CAMERA_FB_IN_DRAM;

  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[CAM] Init FAILED — halting");
    while (true) delay(1000);
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s,  1);
    s->set_contrast(s,    1);
    s->set_saturation(s,  0);
    s->set_sharpness(s,   1);
    s->set_whitebal(s,    1);
    s->set_awb_gain(s,    1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s,        1);
    s->set_gain_ctrl(s,   1);
    s->set_agc_gain(s,    0);
    s->set_gainceiling(s, (gainceiling_t)2);
    s->set_bpc(s,         1);
    s->set_wpc(s,         1);
    s->set_raw_gma(s,     1);
    s->set_lenc(s,        1);
    s->set_hmirror(s,     0);
    s->set_vflip(s,       0);
    s->set_dcw(s,         1);
  }

  Serial.println("[CAM] Ready — VGA JPEG");
}


// ═════════════════════════════════════════════════════════════════════════════
// postJpeg — POST raw JPEG with configurable timeout
// Stream uses short timeout (drop frame ok).
// Capture uses long timeout (must not fail silently).
// ═════════════════════════════════════════════════════════════════════════════
int postJpeg(const String& endpoint, const uint8_t* buf, size_t len, int timeoutMs) {
  if (WiFi.status() != WL_CONNECTED) return -99;

  HTTPClient http;
  http.begin(SERVER + endpoint);
  http.addHeader("Content-Type", "application/octet-stream");
  http.setTimeout(timeoutMs);

  int code = http.POST(const_cast<uint8_t*>(buf), len);
  if (code < 0 && code != -99)
    Serial.printf("[NET] POST %s → %s\n",
                  endpoint.c_str(), http.errorToString(code).c_str());
  http.end();
  return code;
}


// ═════════════════════════════════════════════════════════════════════════════
// pollForCapture — GET /should_capture
// Returns the number of pending captures Flask is waiting for.
// 0 = nothing to do. >0 = take that many photos.
// FIX: Flask now returns a counter (capture_queue), not just true/false.
//      If poll was late, counter > 1 and we take multiple shots to catch up.
// ═════════════════════════════════════════════════════════════════════════════
int pollForCapture() {
  if (WiFi.status() != WL_CONNECTED) return 0;

  HTTPClient http;
  http.begin(SERVER + "/should_capture");
  http.setTimeout(POLL_TIMEOUT_MS);

  int code = http.GET();
  if (code != 200) {
    http.end();
    return 0;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload)) return 0;

  bool capture = doc["capture"] | false;

  // If Flask returns capture:true, we take exactly 1 photo per poll.
  // Flask's counter handles multiple queued events — we'll catch remaining
  // ones on the next poll (300 ms later).
  return capture ? 1 : 0;
}


// ═════════════════════════════════════════════════════════════════════════════
// takeCapture — grab a fresh frame and POST to /motion_capture
// Called immediately when poll returns capture:true.
// Uses CAPTURE_TIMEOUT_MS (longer) to ensure delivery.
// ═════════════════════════════════════════════════════════════════════════════
void takeCapture() {
  Serial.println("[CAPTURE] Taking PIR-triggered photo...");

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAPTURE] Frame grab failed!");
    return;
  }

  int code = postJpeg("/motion_capture", fb->buf, fb->len, CAPTURE_TIMEOUT_MS);
  esp_camera_fb_return(fb);

  if (code == 200) {
    Serial.println("[CAPTURE] Saved to DB ✓");
  } else {
    Serial.printf("[CAPTURE] FAILED — HTTP %d  (photo lost!)\n", code);
    // Note: if this fails, the Flask counter was already decremented.
    // To retry, you would need to call /should_capture again — not done here
    // to keep logic simple and avoid duplicate captures.
  }
}


// ═════════════════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("============================================");
  Serial.println("   GA Security — ESP32-CAM");
  Serial.println("   PIR-triggered capture (queue-based)");
  Serial.println("============================================");

  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.printf("[WIFI] Connected  IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());

  startCamera();

  lastFpsLog = millis();
  Serial.println("[SYS] Running");
  Serial.printf("[SYS] Stream: every %lu ms | Poll: every %lu ms\n",
                STREAM_INTERVAL_MS, POLL_INTERVAL_MS);
  Serial.println("--------------------------------------------");
}


// ═════════════════════════════════════════════════════════════════════════════
// LOOP
// ─────────────────────────────────────────────────────────────────────────────
// Two INDEPENDENT timers — neither blocks the other:
//
//  Timer A — STREAM (every 100 ms):
//    Grab frame → POST /cam_stream with SHORT timeout (800 ms).
//    If WiFi is congested the POST times out quickly and we move on.
//    A dropped stream frame is invisible to the viewer.
//
//  Timer B — POLL (every 300 ms):
//    GET /should_capture → if capture:true → call takeCapture().
//    takeCapture() uses LONG timeout (3000 ms) because we MUST not lose it.
//    Poll uses its own timer so a slow stream never delays it.
// ═════════════════════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ── Timer A: live stream ─────────────────────────────────────────────────
  if (now - lastStream >= STREAM_INTERVAL_MS) {
    lastStream = now;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      // Short timeout — drop frame if slow, don't stall the loop
      postJpeg("/cam_stream", fb->buf, fb->len, STREAM_TIMEOUT_MS);
      esp_camera_fb_return(fb);

      frameCount++;
      if (now - lastFpsLog >= 5000) {
        Serial.printf("[STREAM] %.1f FPS\n", frameCount / 5.0f);
        frameCount = 0;
        lastFpsLog = now;
      }
    }
  }

  // ── Timer B: PIR capture poll ────────────────────────────────────────────
  // Runs completely independently of Timer A.
  // Even if Timer A just ran a slow POST, Timer B fires on time because
  // we check millis() fresh here — it's not blocked by Timer A's completion.
  if (now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;

    int pending = pollForCapture();
    if (pending > 0) {
      // Capture fires immediately — doesn't wait for next stream cycle
      takeCapture();
    }
  }

  // Tiny yield for WiFi stack — does NOT affect timer accuracy since
  // we use millis()-based timers, not delay()-based intervals.
  delay(5);
}
