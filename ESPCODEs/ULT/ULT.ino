#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ===== WiFi =====
const char* ssid = "A67C";
const char* password = "ga_group7";

IPAddress local_IP(10, 10, 10, 7);
IPAddress subnet(255, 255, 255, 0);

// ===== Pins =====

int led = 27;
int trigPin = 12;
int echoPin = 13;
int servoPin = 14;

// ===== Variables =====
long duration;
float distance;

Servo doorServo;
bool armed = false;

String device_id = "fanelo_esp";

// ===== State Tracking =====
bool doorOpen = false;
String lastEvent = "";

// ===== Setup =====
void setup() {
  Serial.begin(115200);


  pinMode(led, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  doorServo.attach(servoPin, 500, 2400);
  doorServo.write(0); // door closed

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
}

// ===== Fetch armed state from Flask =====
// void fetchArmedState() {
//   if (WiFi.status() != WL_CONNECTED) return;

//   HTTPClient http;
//   http.begin("http://10.10.10.1:5000/ultson_sensor");

//   int code = http.GET();

//   if (code == 200) {
//     String payload = http.getString();

//     StaticJsonDocument<200> doc;
//     DeserializationError err = deserializeJson(doc, payload);

//     if (!err) {
//       armed = doc["armed"];
//       Serial.print("Armed: ");
//       Serial.println(armed);
//     }
//   } else {
//     Serial.println("Failed to fetch armed state");
//   }

//   http.end();
// }

// ===== Send JSON log to Flask =====
void sendLog(float distance, String event) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin("http://10.10.10.1:5000/log");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;

  doc["device_id"] = device_id;

  JsonObject readings = doc.createNestedObject("readings");
  readings["type"] = "distance";
  readings["value"] = distance;
  readings["event"] = event;

  String json;
  serializeJson(doc, json);

  int code = http.POST(json);

  Serial.print("Log sent, code: ");
  Serial.println(code);

  http.end();
}

// ===== Read ultrasonic sensor =====
float readDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout

  if (duration == 0) return -1; // invalid reading

  return duration * 0.034 / 2;
}

// ===== Control logic =====
void controlDoor(float distance) {

  if (distance < 0) return; // ignore bad readings

  String currentEvent;

  // ===== Determine state =====
  if (distance > 0 && distance < 8) {

    digitalWrite(led, HIGH);

    if (!armed && !doorOpen) {
      doorServo.write(90);
      doorOpen = true;
      Serial.println("Door OPENED");
    }

  } else {

    digitalWrite(led, LOW);

    if (doorOpen) {
      doorServo.write(0);
      doorOpen = false;
      Serial.println("Door CLOSED");
    }
  }

  // ===== Log ONLY on state change =====
  if (currentEvent != lastEvent) {
    sendLog(distance, currentEvent);
    lastEvent = currentEvent;

    Serial.print("Event changed → Logged: ");
    Serial.println(currentEvent);
  }
}

// ===== Main loop =====
void loop() {
  fetchArmedState();

  distance = readDistance();

  Serial.print("Distance: ");
  Serial.println(distance);

  controlDoor(distance);

  delay(200);
}