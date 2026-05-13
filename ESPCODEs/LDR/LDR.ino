// ================= ESP32 LDR SENSOR =================
// Sends LDR value + status to Flask /ldr_sensor
// Device: fanelo_esp | User: 218541309@edu.vut.ac.za

#include <WiFi.h>
#include <HTTPClient.h>

// ---------------- WIFI ----------------
const char* ssid     = "A67C";
const char* password = "ga_group7";

// Static IP so the server always knows where to reach this ESP
IPAddress local_IP(10, 192, 156, 65);
IPAddress gateway(10, 192, 156, 61);
IPAddress subnet(255, 255, 255, 0);


// ---------------- FLASK ----------------
const char* serverURL = "http://10.192.156.61:5000/ldr_sensor";

// ---------------- PINS ----------------
const int ldrPin    = 34;
const int redLED    = 23;
const int greenLED  = 12;

// ---------------- CONFIG ----------------
const int    threshold       = 500;
const String DEVICE_ID       = "fanelo_esp";   // FIX: was "maambele_esp"
const unsigned long INTERVAL = 3000;           // ms between readings

unsigned long lastSend = 0;

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(redLED,   OUTPUT);
  pinMode(greenLED, OUTPUT);

  // Turn both LEDs off at boot
  digitalWrite(redLED,   LOW);
  digitalWrite(greenLED, LOW);

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("Static IP config failed");
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ─────────────────────────────────────────────────────────────────────────────
void sendToFlask(int ldrValue, const String& lightState) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi down — skipping send");
    return;
  }

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  // JSON structure Flask /ldr_sensor expects
  String json = "{";
  json += "\"device_id\":\"" + DEVICE_ID + "\",";
  json += "\"readings\":{";
  json += "\"type\":\"light\",";
  json += "\"value\":"  + String(ldrValue) + ",";
  json += "\"event\":\"" + lightState + "\"";
  json += "}}";

  int code = http.POST(json);
  Serial.print("Server response: ");
  Serial.println(code);
  if (code < 0) Serial.println(http.errorToString(code));
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  int ldrValue = analogRead(ldrPin);
  String lightState;

  if (ldrValue >= threshold) {
    digitalWrite(greenLED, HIGH);
    digitalWrite(redLED,   LOW);
    lightState = "bright";
  } else if(ldrValue <= threshold) {
    digitalWrite(redLED,   HIGH);
    digitalWrite(greenLED, LOW);
    lightState = "dark";
  }

  Serial.printf("LDR: %d  |  State: %s\n", ldrValue, lightState.c_str());
  Serial.println(analogRead(ldrPin));

  unsigned long now = millis();
  if (now - lastSend >= INTERVAL) {
    lastSend = now;
    sendToFlask(ldrValue, lightState);
  }
}
