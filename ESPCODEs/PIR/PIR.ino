#include <WiFi.h>
#include <HTTPClient.h>

#define PIR_PIN    26
#define BUZZER_PIN 17
#define LED_PIN    25
#define BUTTON_PIN 16

const char* ssid = "A67C";
const char* password = "ga_group7";

IPAddress local_IP(10, 192, 156, 63);
IPAddress gateway(10, 192, 156, 61);
IPAddress subnet(255, 255, 255, 0);

const String DEVICE_ID = "maambele_esp";

// ESP32-CAM IP
const String CAMERA_URL = "http://10.192.156.84/capture";

int pirState = LOW;
int lastState = LOW;

bool isArmed = false;
bool lastPrinted = false;

volatile unsigned long lastInterruptTime = 0;

const unsigned long DEBOUNCE_MS = 200;

// ======================================================
// SEND TO FLASK
// ======================================================

void sendMotionEvent(int isInMotion) {

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  http.begin("http://10.192.156.61:5000/pir_sensor");

  http.addHeader("Content-Type", "application/json");

  http.setTimeout(3000);

  String json = "{";

  json += "\"device_id\":\"" + DEVICE_ID + "\",";

  json += "\"readings\":{";

  json += "\"type\":\"motion\",";

  json += "\"value\":" +
          String(isInMotion ? "true" : "false") + ",";

  json += "\"isArmed\":" +
          String(isArmed ? "true" : "false");

  json += "}}";

  int code = http.POST(json);

  Serial.print("Flask response: ");

  Serial.println(code);

  http.end();
}

// ======================================================
// TRIGGER CAMERA DIRECTLY
// ======================================================

void triggerCamera() {

  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;

  http.begin(CAMERA_URL);

  http.setTimeout(5000);

  int code = http.GET();

  Serial.print("Camera trigger response: ");

  Serial.println(code);

  http.end();
}

// ======================================================
// ALARM
// ======================================================

void triggerAlarm() {

  Serial.println("UNAUTHORIZED MOTION!");

  for (int i = 0; i < 5; i++) {

    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);

    delay(150);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);

    delay(150);
  }

  digitalWrite(LED_PIN, HIGH);
}

// ======================================================
// PIR MONITOR
// ======================================================

void pirMonitoring() {

  pirState = digitalRead(PIR_PIN);

  // MOTION START
  if (pirState == HIGH && lastState == LOW) {

    Serial.println("Motion DETECTED");

    sendMotionEvent(1);

    // DIRECT CAMERA TRIGGER
    triggerCamera();

    if (isArmed) triggerAlarm();

    lastState = HIGH;
  }

  // MOTION END
  else if (pirState == LOW && lastState == HIGH) {

    Serial.println("Motion ENDED");

    sendMotionEvent(0);

    digitalWrite(
      LED_PIN,
      isArmed ? HIGH : LOW
    );

    lastState = LOW;
  }
}

// ======================================================
// BUTTON INTERRUPT
// ======================================================

void IRAM_ATTR handleButton() {

  unsigned long now = millis();

  if (now - lastInterruptTime > DEBOUNCE_MS) {

    isArmed = !isArmed;

    lastInterruptTime = now;
  }
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(LED_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(BUTTON_PIN),
    handleButton,
    FALLING
  );

  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(LED_PIN, LOW);

  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(ssid, password);

  Serial.print("Connecting");

  while (WiFi.status() != WL_CONNECTED) {

    delay(300);

    Serial.print(".");
  }

  Serial.println();

  Serial.println("Connected!");

  Serial.print("IP: ");

  Serial.println(WiFi.localIP());
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  pirMonitoring();

  if (lastPrinted != isArmed) {

    Serial.println("--------------------");

    Serial.print("Armed: ");

    Serial.println(isArmed ? "YES" : "NO");

    Serial.println("--------------------");

    if (pirState == LOW) {

      digitalWrite(
        LED_PIN,
        isArmed ? HIGH : LOW
      );
    }

    lastPrinted = isArmed;
  }

  delay(200);
}
