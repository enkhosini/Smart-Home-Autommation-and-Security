// ================= ESP32 DHT22 + PWM FAN CONTROL =================
// Device: muzi_esp
// Sends temperature + humidity to Flask server
// Controls fan speed based on temperature

#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h"

// =====================================================
// WIFI
// =====================================================
const char* ssid     = "A67C";
const char* password = "ga_group7";

IPAddress local_IP(10, 192, 156, 66);
IPAddress gateway(10, 192, 156, 61);
IPAddress subnet(255, 255, 255, 0);

// =====================================================
// FLASK SERVER
// =====================================================
const char* serverURL = "http://10.192.156.61:5000/dht22_sensor";

// =====================================================
// PINS
// =====================================================
const int DHT_PIN = 23;
const int FAN_PIN = 5;

// =====================================================
// CONFIG
// =====================================================
const String DEVICE_ID = "muzi_esp";
const unsigned long INTERVAL = 3000;

// =====================================================
// DHT SENSOR
// =====================================================
DHTesp dhtSensor;

unsigned long lastSend = 0;

// ─────────────────────────────────────────────────────
void sendToFlask(float temp, float hum, const String& fanStatus) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected");
    return;
  }

  HTTPClient http;

  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  // JSON
  String json = "{";
  json += "\"device_id\":\"" + DEVICE_ID + "\",";
  json += "\"readings\":[";

  // Temperature
  json += "{";
  json += "\"type\":\"temperature_reading\",";
  json += "\"value\":" + String(temp, 2) + ",";
  json += "\"event\":\"" + fanStatus + "\"";
  json += "},";

  // Humidity
  json += "{";
  json += "\"type\":\"humidity_reading\",";
  json += "\"value\":" + String(hum, 1);
  json += "}";

  json += "]}";

  int code = http.POST(json);

  Serial.print("HTTP Response: ");
  Serial.println(code);

  if (code < 0) {
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ─────────────────────────────────────────────────────
void setup() {

  Serial.begin(115200);

  // DHT22 setup
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  // PWM FAN setup
  ledcAttach(FAN_PIN, 5000, 8);

  // Static IP
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP Failed");
  }

  // Connect WiFi
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────────────────
void loop() {

  unsigned long now = millis();

  if (now - lastSend < INTERVAL) return;

  lastSend = now;

  // Read sensor
  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  // Check sensor reading
  if (isnan(data.temperature) || isnan(data.humidity)) {
    Serial.println("DHT22 read failed");
    return;
  }

  float temp = data.temperature;
  float hum  = data.humidity;

  Serial.println("--------------------------------");

  Serial.print("Temperature: ");
  Serial.print(temp);
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.println(" %");

  // =====================================================
  // FAN SPEED CONTROL
  // =====================================================

  int fanSpeed = 0;
  String fanStatus = "";

  // Temperature-based speed
  if (temp <= 20) {

    fanSpeed = 80;
    fanStatus = "fan_slow";

  }
  else if (temp <= 30) {

    fanSpeed = 150;
    fanStatus = "fan_medium";

  }
  else {

    fanSpeed = 255;
    fanStatus = "fan_fast";
  }

  // Apply PWM
  ledcWrite(FAN_PIN, fanSpeed);

  Serial.print("Fan Speed: ");
  Serial.println(fanSpeed);

  Serial.print("Fan Status: ");
  Serial.println(fanStatus);

  // Send to Flask
  sendToFlask(temp, hum, fanStatus);

  Serial.println("--------------------------------");
}
