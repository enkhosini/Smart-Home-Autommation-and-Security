// ================= ESP32 LDR SENSOR + FLASK COMMUNICATION =================
// Sends LDR value + status to Flask server

#include <WiFi.h>
#include <HTTPClient.h>

// ---------------- WIFI ----------------
const char* ssid = "A67C";
const char* password = "ga_group7";

// ---------------- FLASK SERVER ----------------
// Change to laptop IP address
const char* serverURL = "http://10.10.10.1:5000/ldr_sensor";

// ---------------- PINS ----------------
const int ldrPin = 2;     
const int redLED = 23;    
const int greenLED = 12;  

int threshold = 500;

void setup() {
  Serial.begin(115200);

  pinMode(redLED, OUTPUT);
  pinMode(greenLED, OUTPUT);

  WiFi.begin(ssid, password);

  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());
}

void loop() {

  int ldrValue = analogRead(ldrPin);
  String lightState = "";

  if (ldrValue >= threshold) {
    digitalWrite(greenLED, HIGH);
    digitalWrite(redLED, LOW);
    lightState = "bright";
  } 
  else {
    digitalWrite(redLED, HIGH);
    digitalWrite(greenLED, LOW);
    lightState = "dark";
  }

  Serial.print("LDR Value: ");
  Serial.println(ldrValue);

  // ---------- SEND TO FLASK ----------
  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    String jsonData = "{";
    jsonData += "\"device_id\":\"maambele_esp\",";
    jsonData += "\"readings\":{";
    jsonData += "\"type\":\"light\",";
    jsonData += "\"value\":" + String(ldrValue) + ",";
    jsonData += "\"event\":\"" + lightState + "\"";
    jsonData += "}}";

    int httpResponseCode = http.POST(jsonData);

    Serial.print("Server Response: ");
    Serial.println(httpResponseCode);

    http.end();
  }

  delay(3000);
}