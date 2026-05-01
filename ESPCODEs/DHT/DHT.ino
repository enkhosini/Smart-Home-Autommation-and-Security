#include <WiFi.h>
#include <HTTPClient.h>
#include "DHTesp.h"

// ==================================================
// WIFI SETTINGS
// ==================================================
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

// Flask server IP
const char* serverURL = "http://10.10.10.1:5000/dht22_sensor";

// ==================================================
// PIN DEFINITIONS
// ==================================================
const int DHT_PIN = 13;      // DHT22 Data pin
const int FAN_PIN = 14;      // Fan relay / transistor

DHTesp dhtSensor;

// ==================================================
// SETUP
// ==================================================
void setup() {
  Serial.begin(115200);

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);   // Fan OFF initially

  // ---------------- WIFI ----------------
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("ESP IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==================================================
// LOOP
// ==================================================
void loop() {

  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  if (isnan(data.temperature) || isnan(data.humidity)) {
    Serial.println("Failed to read from DHT22!");
    delay(2000);
    return;
  }

  float temp = data.temperature;
  float hum = data.humidity;

  Serial.println("Temperature: " + String(temp, 2) + " C");
  Serial.println("Humidity   : " + String(hum, 1) + " %");

  String fanStatus = "";

  // ==================================================
  // FAN CONTROL
  // ==================================================
  if (temp > 35 && hum > 70) {
    digitalWrite(FAN_PIN, HIGH);
    fanStatus = "fan_on";
    Serial.println("Fan ON");
  }
  else {
    digitalWrite(FAN_PIN, LOW);
    fanStatus = "fan_off";
    Serial.println("Fan OFF");
  }

  // ==================================================
  // SEND TO FLASK SERVER
  // ==================================================
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;

    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    // JSON format Flask already expects
    String jsonData = "{";
    jsonData += "\"device_id\":\"muzi_esp\",";
    jsonData += "\"readings\":[";
    
    jsonData += "{";
    jsonData += "\"type\":\"temperature_reading\",";
    jsonData += "\"value\":" + String(temp,2) + ",";
    jsonData += "\"event\":\"" + fanStatus + "\"";
    jsonData += "},";

    jsonData += "{";
    jsonData += "\"type\":\"humidity_reading\",";
    jsonData += "\"value\":" + String(hum,1);
    jsonData += "}";

    jsonData += "]}";

    int responseCode = http.POST(jsonData);

    Serial.print("HTTP Response Code: ");
    Serial.println(responseCode);

    http.end();
  }
  else {
    Serial.println("WiFi Disconnected");
  }

  Serial.println("----------------------------");

  delay(3000);
}
