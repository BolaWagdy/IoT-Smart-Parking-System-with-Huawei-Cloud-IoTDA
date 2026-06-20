// ============================================================
// IoT Smart Parking System — Huawei Cloud IoTDA
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
//  >>>  WIFI CREDENTIALS  <
// ============================================================
const char* ssid          = "YOUR_WIFI_SSID";
const char* wifi_password = "YOUR_WIFI_PASSWORD";

// ============================================================
//  >>>  HUAWEI IoTDA CREDENTIALS (from Download file)  <
// ============================================================
#define MQTT_SERVER   "YOUR_IOTDA_ENDPOINT"
#define MQTT_PORT     8883

#define MQTT_USERNAME "YOUR_DEVICE_ID_SECRET"
#define MQTT_PASSWORD "YOUR_DEVICE_PASSWORD"
#define MQTT_CLIENTID "YOUR_CLIENT_ID"

#define DEVICE_ID     "YOUR_DEVICE_ID"
#define SERVICE_ID    "parking"

// ============================================================
// Topics
// ============================================================
String topicReport   = String("$oc/devices/") + DEVICE_ID + "/sys/properties/report";
String topicCommands = String("$oc/devices/") + DEVICE_ID + "/sys/commands/#";
String topicPropsRsp = String("$oc/devices/") + DEVICE_ID + "/sys/properties/report/#";
String topicUserDown = String("$oc/devices/") + DEVICE_ID + "/user/#";

// ---------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define IR_ENTRY    18
#define IR_EXIT     19
#define SERVO_PIN   25
#define GREEN_LED   26
#define RED_LED     27

Servo             barrierServo;
WebServer         server(80);
WiFiClientSecure  espClient;
PubSubClient      mqttClient(espClient);

// Parking state
int    totalSlots     = 4;
int    availableSlots = 4;
int    flagEntry      = 0;
int    flagExit       = 0;
String gateStatus     = "Closed";

// Non-blocking gate timing
unsigned long gateOpenTime = 0;
bool          gateIsOpen   = false;

unsigned long lastPublish     = 0;
const unsigned long PUB_EVERY = 5000;

// ---------------------------------------------------------------
// MQTT Callback (receive commands from cloud)
// ---------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n===== MQTT MESSAGE RECEIVED =====");
  Serial.print("Topic: "); Serial.println(topic);
  Serial.print("Payload: ");
  for (unsigned int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println("\n=================================");
}

// ---------------------------------------------------------------
// MQTT reconnect
// ---------------------------------------------------------------
void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("\nConnecting to Huawei IoTDA (MQTTS 8883)...");
    Serial.print("clientId: "); Serial.println(MQTT_CLIENTID);
    Serial.print("username: "); Serial.println(MQTT_USERNAME);

    espClient.stop();
    delay(200);

    bool ok = mqttClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD);
    if (ok) {
      Serial.println("MQTT Connected!");

      mqttClient.subscribe(topicCommands.c_str());
      mqttClient.subscribe(topicPropsRsp.c_str());
      mqttClient.subscribe(topicUserDown.c_str());

      Serial.println("Subscribed to topics:");
      Serial.println(topicCommands);
      Serial.println(topicPropsRsp);
      Serial.println(topicUserDown);

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("IoTDA Connected!");
      delay(1500);
      updateLCD();
    } else {
      Serial.print("Failed, state=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

// ---------------------------------------------------------------
// Publish parking state to Huawei IoTDA
// ---------------------------------------------------------------
void publishToCloud() {
  if (!mqttClient.connected()) connectMQTT();

  String payload =
    String("{\"services\":[{\"service_id\":\"") + SERVICE_ID +
    "\",\"properties\":{\"available_slots\":" + String(availableSlots) +
    ",\"occupied_slots\":"  + String(totalSlots - availableSlots) +
    ",\"gate_status\":\""   + gateStatus +
    "\"}}]}";

  Serial.println("\n===== SENDING DATA =====");
  Serial.print("Topic: ");   Serial.println(topicReport);
  Serial.print("Payload: "); Serial.println(payload);
  Serial.println("========================");

  if (mqttClient.publish(topicReport.c_str(), payload.c_str())) {
    Serial.println("Publish Success!");
  } else {
    Serial.println("Publish Failed!");
  }
}

// ---------------------------------------------------------------
// LCD + LEDs
// ---------------------------------------------------------------
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Slots:");
  lcd.print(availableSlots);
  lcd.print("/");
  lcd.print(totalSlots);

  lcd.setCursor(0, 1);
  lcd.print("Gate:");
  lcd.print(gateStatus.c_str());

  if (availableSlots > 0) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED,   LOW);
  } else {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   HIGH);
  }
}

void showFullMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Parking is FULL!");
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   HIGH);
  delay(2000);
  updateLCD();
}

// ---------------------------------------------------------------
// Web server — /data endpoint
// ---------------------------------------------------------------
void handleData() {
  String json = "{";
  json += "\"available\":"  + String(availableSlots) + ",";
  json += "\"occupied\":"   + String(totalSlots - availableSlots) + ",";
  json += "\"gate\":\""     + gateStatus + "\",";
  json += "\"occupied_slots\":[";
  int occ = totalSlots - availableSlots;
  for (int i = 1; i <= occ; i++) {
    json += String(i);
    if (i < occ) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------
// Web server — main HTML page
// ---------------------------------------------------------------
String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IoT Smart Parking System</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;700&display=swap');
    body { font-family:'Inter',sans-serif; }
    .glow-green { text-shadow:0 0 5px #4ade80,0 0 10px #4ade80; }
    .glow-red   { text-shadow:0 0 5px #f87171,0 0 10px #f87171; }
    .glow-lime  { text-shadow:0 0 5px #a3e635,0 0 10px #a3e635; }
    .border-glow-green { box-shadow:0 0 10px #a3e635,0 0 20px rgba(163,230,53,.5); border-color:#a3e635; }
    .border-glow-red   { box-shadow:0 0 10px #f87171,0 0 20px rgba(248,113,113,.5); border-color:#f87171; }
    .card-bg  { background-color:#161b22; }
    .body-bg  { background-color:#0d1117; }
  </style>
</head>
<body class="body-bg text-white min-h-screen flex flex-col items-center justify-center p-4">
  <div class="text-center mb-8">
    <h1 class="text-4xl md:text-5xl font-extrabold text-teal-400 mb-2">IoT Smart Parking System</h1>
    <p class="text-gray-400 text-lg">Real-time Parking Management + Huawei Cloud IoTDA</p>
  </div>
  <div class="w-full max-w-5xl mb-8">
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Available Slots</h2>
        <p id="available" class="text-5xl md:text-6xl font-bold text-green-400 glow-green">0</p>
        <p class="text-gray-400 text-sm mt-1">Out of )rawliteral";
  html += String(totalSlots);
  html += R"rawliteral( Total Slots</p>
      </div>
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Occupied Slots</h2>
        <p id="occupied" class="text-5xl md:text-6xl font-bold text-red-400 glow-red">0</p>
        <p class="text-gray-400 text-sm mt-1">Currently Parked</p>
      </div>
      <div class="card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl flex flex-col items-start justify-between">
        <h2 class="text-xl font-semibold text-gray-200 mb-2">Gate Status</h2>
        <p id="gate" class="text-4xl md:text-5xl font-bold text-red-400 glow-red transition-colors duration-300">Closed</p>
        <p class="text-gray-400 text-sm mt-auto">Entry Gate</p>
      </div>
    </div>
  </div>
  <div class="w-full max-w-5xl card-bg border border-gray-700 rounded-3xl p-6 md:p-8 shadow-2xl mb-8">
    <h2 class="text-2xl md:text-3xl font-bold text-gray-200 mb-6">Parking Layout</h2>
    <div id="slots" class="grid grid-cols-2 lg:grid-cols-4 gap-4 md:gap-6 w-full"></div>
  </div>
  <script>
    const totalSlots = )rawliteral" + String(totalSlots) + R"rawliteral(;
    async function updateData() {
      try {
        const res  = await fetch('/data');
        const data = await res.json();
        document.getElementById("available").innerText = data.available;
        document.getElementById("occupied").innerText  = data.occupied;
        const g = document.getElementById("gate");
        g.innerText = data.gate;
        g.className = data.gate === "Open"
          ? "text-4xl md:text-5xl font-bold text-green-400 glow-green transition-colors duration-300"
          : "text-4xl md:text-5xl font-bold text-red-400 glow-red  transition-colors duration-300";
        let html = "";
        for (let i = 1; i <= totalSlots; i++) {
          const occ  = data.occupied_slots.includes(i);
          const cls  = occ ? "bg-red-900 border-glow-red"   : "bg-green-900 border-glow-green";
          const sub  = occ ? "text-red-200" : "text-green-200";
          const txt  = occ ? "Occupied"     : "Available";
          html += `<div class="p-4 rounded-xl border-2 ${cls} flex flex-col items-center justify-center text-center">
            <div class="text-xl md:text-2xl font-bold text-gray-200">Slot ${i}</div>
            <div class="text-sm md:text-base font-semibold mt-1H ${sub}">${txt}</div></div>`;
        }
        document.getElementById("slots").innerHTML = html;
      } catch(e) { console.error("Fetch failed", e); }
    }
    setInterval(updateData, 2000);
    window.onload = updateData;
  </script>
</body>
</html>
)rawliteral";
  return html;
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(IR_ENTRY,  INPUT);
  pinMode(IR_EXIT,   INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);

  barrierServo.attach(SERVO_PIN);
  barrierServo.write(90);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Smart Parking");
  lcd.setCursor(0, 1); lcd.print("Connecting WiFi");

  // 1. Connect WiFi
  WiFi.begin(ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WiFi Connected");
  lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString().c_str());
  delay(2000);

  // 2. TLS — skip certificate verification
  espClient.setInsecure();
  espClient.setHandshakeTimeout(30);
  espClient.setTimeout(30000);

  // 3. Configure MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(30);
  mqttClient.setCallback(mqttCallback);

  Serial.println("Topic Report: " + topicReport);

  // 4. Connect to Huawei IoTDA
  connectMQTT();

  // 5. Start local web server
  server.on("/",     []() { server.send(200, "text/html", getHTML()); });
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started at http://" + WiFi.localIP().toString());

  updateLCD();
}

// ---------------------------------------------------------------
// Loop
// ---------------------------------------------------------------
void loop() {
  // Keep MQTT alive
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // Serve local dashboard
  server.handleClient();

  // ── Non-blocking gate close ──────────────────────────────────
  if (gateIsOpen && (millis() - gateOpenTime >= 2000)) {
    barrierServo.write(90);
    gateIsOpen = false;
    gateStatus = "Closed";
    updateLCD();
    publishToCloud();
  }

  // Periodic cloud publish every 5 seconds
  unsigned long now = millis();
  if (now - lastPublish >= PUB_EVERY) {
    lastPublish = now;
    publishToCloud();
  }

  // ── Entry sensor ─────────────────────────────────────────────
  if (digitalRead(IR_ENTRY) == LOW && flagEntry == 0 && !gateIsOpen) {
    flagEntry = 1;
    if (availableSlots > 0) {
      availableSlots--;
      gateStatus   = "Open";
      gateIsOpen   = true;
      gateOpenTime = millis();
      barrierServo.write(180);
      updateLCD();
      publishToCloud();
    } else {
      showFullMessage();
      publishToCloud();
    }
  }
  if (digitalRead(IR_ENTRY) == HIGH && flagEntry == 1) {
    flagEntry = 0;
    updateLCD();
  }

  // ── Exit sensor ──────────────────────────────────────────────
  if (digitalRead(IR_EXIT) == LOW && flagExit == 0 && !gateIsOpen) {
    flagExit = 1;
    if (availableSlots < totalSlots) {
      availableSlots++;
      gateStatus   = "Open";
      gateIsOpen   = true;
      gateOpenTime = millis();
      barrierServo.write(180);
      updateLCD();
      publishToCloud();
    }
  }
  if (digitalRead(IR_EXIT) == HIGH) flagExit = 0;
}