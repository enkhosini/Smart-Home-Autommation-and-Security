// ================= ESP32 PIR SENSOR =================
// Sends motion events to Flask /pir_sensor
// Device: maambele_esp | User: 240254260@edu.vut.ac.za

#include <WiFi.h>
#include <HTTPClient.h>

// ---------------- PINS ----------------
#define PIR_PIN    26
#define BUZZER_PIN 17
#define LED_PIN    25
#define BUTTON_PIN 16

// ---------------- WIFI ----------------
const char* ssid     = "Maambele";
const char* password = "@Onepiece0907";

IPAddress local_IP(172, 20, 10, 3);
IPAddress gateway(172, 20, 10, 2);    // FIX: gateway was missing
IPAddress subnet(255, 255, 255, 0);

// ---------------- CONFIG ----------------
const String DEVICE_ID = "maambele_esp";  // FIX: was "sobonga_esp"

// ---------------- STATE ----------------
int  pirState    = LOW;
int  lastState   = LOW;
bool isArmed     = false;
bool lastPrinted = false;

volatile unsigned long lastInterruptTime = 0;
const unsigned long    DEBOUNCE_MS       = 200;

// ─────────────────────────────────────────────────────────────────────────────
// Send motion event to Flask
// isInMotion: 1 = motion started, 0 = motion ended
// ─────────────────────────────────────────────────────────────────────────────
void sendMotionEvent(int isInMotion) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("http://172.20.10.2:5000/pir_sensor");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  String json = "{";
  json += "\"device_id\":\"" + DEVICE_ID + "\",";
  json += "\"readings\":{";
  json += "\"type\":\"motion\",";
  json += "\"value\":"    + String(isInMotion ? "true" : "false") + ",";
  json += "\"isArmed\":"  + String(isArmed    ? "true" : "false");
  json += "}}";

  int code = http.POST(json);
  Serial.print("HTTP Response: ");
  Serial.println(code);
  if (code < 0) Serial.println(http.errorToString(code));
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
void triggerAlarm() {
  Serial.println("UNAUTHORIZED MOTION DETECTED!");
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN,    LOW);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN,    HIGH);
    delay(150);
  }
  // Leave LED on to show armed state visually
  digitalWrite(LED_PIN, HIGH);
}

// ─────────────────────────────────────────────────────────────────────────────
void pirMonitoring() {
  pirState = digitalRead(PIR_PIN);

  // Rising edge — motion started
  if (pirState == HIGH && lastState == LOW) {
    Serial.println("Motion DETECTED");
    sendMotionEvent(1);
    if (isArmed) triggerAlarm();
    lastState = HIGH;
  }
  // Falling edge — motion ended
  else if (pirState == LOW && lastState == HIGH) {
    Serial.println("Motion ENDED");
    sendMotionEvent(0);
    // Restore LED to armed indicator state
    digitalWrite(LED_PIN, isArmed ? HIGH : LOW);
    lastState = LOW;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Button ISR — toggle armed state with debounce
// ─────────────────────────────────────────────────────────────────────────────
void IRAM_ATTR handleButton() {
  unsigned long now = millis();
  if (now - lastInterruptTime > DEBOUNCE_MS) {
    isArmed = !isArmed;
    lastInterruptTime = now;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN,    INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN,    OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButton, FALLING);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN,    LOW);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Security system ready.");
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  pirMonitoring();

  // Print armed state only when it changes
  if (lastPrinted != isArmed) {
    Serial.println("--------------------");
    Serial.print("Armed: ");
    Serial.println(isArmed ? "YES" : "NO");
    Serial.println("--------------------");
    // Update LED to show armed state when no motion
    if (pirState == LOW) {
      digitalWrite(LED_PIN, isArmed ? HIGH : LOW);
    }
    lastPrinted = isArmed;
  }

  delay(200);
}
