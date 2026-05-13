// ================= ESP32 ULTRASONIC SENSOR =================
// Sends distance + door events to Flask /ultson_sensor
// Device: bridgette_esp | User: 224303635@edu.vut.ac.za

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ---------------- WIFI ----------------
const char* ssid     = "Hisense T2 Pro";
const char* password = "Ambani09";

IPAddress local_IP(192,168,159,68);
IPAddress gateway(192,168,159,62);
IPAddress subnet(255, 255, 255, 0);

// ---------------- PINS ----------------
const int ledPin   = 27;
const int trigPin  = 12;
const int echoPin  = 13;
const int servoPin = 14;

// ---------------- CONFIG ----------------
const String DEVICE_ID      = "bridgette_esp";
const float  OPEN_THRESHOLD = 8.0;    // cm — closer than this opens the door
const unsigned long INTERVAL = 200;   // ms between sensor reads

// ---------------- STATE ----------------
Servo  doorServo;
bool   doorOpen  = false;
String lastEvent = "";
unsigned long lastRead = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Send distance + event to Flask /ultson_sensor
// FIX: was posting to /log which doesn't exist in Flask
// ─────────────────────────────────────────────────────────────────────────────
void sendToFlask(float distance, const String& event) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down — skipping send");
    return;
  }

  HTTPClient http;
  http.begin("http://192.168.159.62:5000/ultson_sensor");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  // Build JSON matching Flask ultson_sensor handler
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  JsonObject readings = doc.createNestedObject("readings");
  readings["type"]  = "distance";
  readings["value"] = distance;
  readings["event"] = event;

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);
  Serial.printf("Flask response [%s]: %d\n", event.c_str(), code);
  if (code < 0) Serial.println(http.errorToString(code));
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Ultrasonic read — returns distance in cm, or -1 on timeout
// ─────────────────────────────────────────────────────────────────────────────
float readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long dur = pulseIn(echoPin, HIGH, 30000);  // 30 ms timeout
  if (dur == 0) return -1;
  return dur * 0.034f / 2.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Door control + event logging (only sends on state change)
// FIX: currentEvent was declared but never assigned — caused empty event logs
// FIX: fetchArmedState() was called in loop but the function was commented out
// ─────────────────────────────────────────────────────────────────────────────
void controlDoor(float distance) {
  if (distance < 0) return;  // bad reading

  String currentEvent;

  if (distance < OPEN_THRESHOLD) {
    digitalWrite(ledPin, HIGH);
    currentEvent = "door_open";
    if (!doorOpen) {
      doorServo.write(90);
      doorOpen = true;
      Serial.println("Door OPENED");
    }
  } else {
    digitalWrite(ledPin, LOW);
    currentEvent = "door_closed";
    if (doorOpen) {
      doorServo.write(0);
      doorOpen = false;
      Serial.println("Door CLOSED");
    }
  }

  // Only send to Flask when the event changes
  if (currentEvent != lastEvent) {
    sendToFlask(distance, currentEvent);
    lastEvent = currentEvent;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(ledPin,  OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  doorServo.attach(servoPin, 500, 2400);
  doorServo.write(0);  // start closed

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - lastRead < INTERVAL) return;
  lastRead = now;

  // FIX: fetchArmedState() removed — it was commented out but still called,
  // causing a compile error. Armed state can be added back via Flask GET later.

  float distance = readDistance();
  Serial.printf("Distance: %.1f cm\n", distance < 0 ? 0 : distance);

  controlDoor(distance);
}
