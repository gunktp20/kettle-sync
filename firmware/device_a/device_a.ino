#include <WiFi.h>
#include <WebSocketsClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* WS_HOST = "your-cloud-server.example.com";
const uint16_t WS_PORT = 8080;
const char* WS_PATH = "/";
const bool WS_USE_SSL = true;
const char* PAIR_ID = "kettle-001";
const char* DEVICE_TOKEN = "REPLACE_WITH_PER_DEVICE_SECRET";
const int ONE_WIRE_PIN = 4;
const unsigned long SEND_INTERVAL_MS = 1000;
const float BOILING_THRESHOLD_C = 95.0;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
WebSocketsClient webSocket;
unsigned long lastSend = 0;
bool wsConnected = false;

void sendHello() {
  StaticJsonDocument<200> doc;
  doc["type"] = "hello";
  doc["role"] = "A";
  doc["pair_id"] = PAIR_ID;
  doc["token"] = DEVICE_TOKEN;
  String out;
  serializeJson(doc, out);
  webSocket.sendTXT(out);
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[WS] Connected to cloud server");
      wsConnected = true;
      sendHello();
      break;
    case WStype_DISCONNECTED:
      Serial.println("[WS] Disconnected — will auto-retry");
      wsConnected = false;
      break;
    case WStype_TEXT:
      Serial.printf("[WS] Server says: %s\n", payload);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  sensors.begin();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
  if (WS_USE_SSL) {
    webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  } else {
    webSocket.begin(WS_HOST, WS_PORT, WS_PATH);
  }
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(3000);
}

void loop() {
  webSocket.loop();
  unsigned long now = millis();
  if (wsConnected && (now - lastSend >= SEND_INTERVAL_MS)) {
    lastSend = now;
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);
    if (tempC <= -100) {
      Serial.println("[SENSOR] Read error, skipping this cycle");
      return;
    }
    bool boiling = (tempC >= BOILING_THRESHOLD_C);
    StaticJsonDocument<200> doc;
    doc["type"] = "temp";
    doc["value"] = tempC;
    doc["boiling"] = boiling;
    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(out);
    Serial.printf("[SEND] temp=%.2f boiling=%d\n", tempC, boiling);
  }
}