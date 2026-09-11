#include <WiFi.h>
#include <WebSocketsClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>
#include <PID_v1.h>

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* WS_HOST = "your-cloud-server.example.com";
const uint16_t WS_PORT = 8080;
const char* WS_PATH = "/";
const bool WS_USE_SSL = true;
const char* PAIR_ID = "kettle-001";
const char* DEVICE_TOKEN = "REPLACE_WITH_PER_DEVICE_SECRET";
const int ONE_WIRE_PIN = 4;
const int SSR_PIN = 5;

const float ABSOLUTE_MAX_TEMP_C = 98.0;
const unsigned long DATA_TIMEOUT_MS = 8000;
const unsigned long MAX_CONTINUOUS_HEAT_MS = 20UL * 60UL * 1000UL;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
WebSocketsClient webSocket;

double targetTemp = 25.0;
double currentTemp = 25.0;
double pidOutput = 0;
double Kp = 15.0, Ki = 0.5, Kd = 2.0;
PID myPID(&currentTemp, &pidOutput, &targetTemp, Kp, Ki, Kd, DIRECT);

unsigned long lastDataReceived = 0;
unsigned long heatStartTime = 0;
bool heaterCurrentlyOn = false;
bool wsConnected = false;
bool sourceOffline = false;

void sendHello() {
  StaticJsonDocument<200> doc;
  doc["type"] = "hello";
  doc["role"] = "B";
  doc["pair_id"] = PAIR_ID;
  doc["token"] = DEVICE_TOKEN;
  String out;
  serializeJson(doc, out);
  webSocket.sendTXT(out);
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("[WS] Connected to cloud server");
    wsConnected = true;
    sendHello();
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("[WS] Disconnected — will auto-retry");
    wsConnected = false;
  } else if (type == WStype_TEXT) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) return;
    const char* msgType = doc["type"];
    if (msgType && strcmp(msgType, "target_temp") == 0) {
      targetTemp = doc["value"].as<double>();
      lastDataReceived = millis();
      sourceOffline = false;
      Serial.printf("[RECV] target_temp=%.2f\n", targetTemp);
    } else if (msgType && strcmp(msgType, "source_offline") == 0) {
      Serial.println("[RECV] Source (kettle A) reported offline");
      sourceOffline = true;
    }
  }
}

void setHeater(bool on) {
  digitalWrite(SSR_PIN, on ? HIGH : LOW);
  if (on && !heaterCurrentlyOn) heatStartTime = millis();
  heaterCurrentlyOn = on;
}

void setup() {
  Serial.begin(115200);
  pinMode(SSR_PIN, OUTPUT);
  setHeater(false);
  sensors.begin();
  myPID.SetMode(AUTOMATIC);
  myPID.SetOutputLimits(0, 255);
  myPID.SetSampleTime(1000);
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
  lastDataReceived = millis();
}

const unsigned long WINDOW_MS = 5000;
unsigned long windowStartTime = 0;

void loop() {
  webSocket.loop();
  sensors.requestTemperatures();
  float t = sensors.getTempCByIndex(0);
  if (t > -100) currentTemp = t;

  unsigned long now = millis();
  bool safetyTrip = false;

  if (currentTemp >= ABSOLUTE_MAX_TEMP_C) {
    Serial.println("[SAFETY] Absolute max temp exceeded — cutting heater");
    safetyTrip = true;
  }
  if (!wsConnected || (now - lastDataReceived > DATA_TIMEOUT_MS)) {
    Serial.println("[SAFETY] No fresh data from source — cutting heater");
    safetyTrip = true;
  }
  if (sourceOffline) safetyTrip = true;
  if (heaterCurrentlyOn && (now - heatStartTime > MAX_CONTINUOUS_HEAT_MS)) {
    Serial.println("[SAFETY] Max continuous heat time exceeded — cutting heater");
    safetyTrip = true;
  }

  if (safetyTrip) {
    setHeater(false);
    myPID.SetMode(MANUAL);
    pidOutput = 0;
    delay(200);
    return;
  } else {
    if (myPID.GetMode() != AUTOMATIC) myPID.SetMode(AUTOMATIC);
  }

  myPID.Compute();
  if (now - windowStartTime > WINDOW_MS) windowStartTime = now;
  bool shouldHeat = (pidOutput > (now - windowStartTime));
  setHeater(shouldHeat);

  static unsigned long lastLog = 0;
  if (now - lastLog > 2000) {
    lastLog = now;
    Serial.printf("[STATUS] current=%.2f target=%.2f pid=%.1f heater=%d\n",
                  currentTemp, targetTemp, pidOutput, heaterCurrentlyOn);
  }
}
