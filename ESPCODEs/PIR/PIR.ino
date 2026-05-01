#include <WiFi.h>
#include <HTTPClient.h>

#define PIR_PIN    26
#define BUZZER_PIN 17
#define LED_PIN    25
#define BUTTON_PIN 16

int pirState = LOW;
int lastState = LOW;

bool isArmed = false;
bool lastPrintedState = false;
volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200; // ms

// WiFi
const char* ssid = "A67C";
const char* password = "ga_group7";

IPAddress local_IP(10, 10, 10, 3);
IPAddress subnet(255, 255, 255, 0);

void sendMotionEvent(int isInMotion) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("http://10.10.10.1:5000/pir_sensor");
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"device_id\":\"sobonga_esp\",";
  json += "\"readings\":{";
  json += "\"type\":\"motion\",";
  json += "\"value\":" + String(isInMotion ? "true" : "false") + ",";
  json += "\"isArmed\":" + String(isArmed ? "true" : "false");
  json += "}";
  json += "}";

  int code = http.POST(json);

  Serial.print("HTTP Response: ");
  Serial.println(code);

  http.end();
}

// -------------------- ALARM ACTION --------------------
void triggerAlarm() {
  Serial.println("UNAUTHORIZED MOTION DETECTED!");

  // short alert burst pattern
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);
    delay(150);
  }


}

void pirMonitoring() {
  pirState = digitalRead(PIR_PIN);
  Serial.print("PIR reading: ");
  Serial.println(pirState);

  // motion detected (rising edge)
  if (pirState == HIGH && lastState == LOW) {
    Serial.println("\nMotion Detected");
    sendMotionEvent(1);
    if (isArmed) {
      triggerAlarm();
    }
    lastState = HIGH;
  }
  // motion ended (falling edge)
  else if (pirState == LOW && lastState == HIGH) {
    Serial.println("\nMotion ended");
    sendMotionEvent(0);
    lastState = LOW;
  }
}

void IRAM_ATTR handleButton() {
  unsigned long currentTime = millis();

  // Debounce check
  if (currentTime - lastInterruptTime > debounceDelay) {
    isArmed = !isArmed;  // toggle
    lastInterruptTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButton, FALLING);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  if (!WiFi.config(local_IP, subnet)) {
    Serial.println("Static Network Config Failed!!!");
    return;
  }


  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nConnected!");

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Security system ready");
}

void loop() {
  pirMonitoring();
  if (lastPrintedState != isArmed) {
    Serial.print("--------------------\nArmed state: ");
    Serial.println(isArmed);
    Serial.println("--------------------");
    lastPrintedState = isArmed;
  }
  delay(200);
}
