#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <DHT.h>
#include <ArduinoJson.h>

#include "config.h"

struct Sample {
    uint32_t uptime;
    float temperature;
    float humidity;
    uint16_t light;
    bool valid;
};

DHT dht(PIN_DHT, DHT22);
WebServer server(80);

static Sample history[HISTORY_SIZE];
static size_t historyHead = 0;
static size_t historyCount = 0;
static Sample latest = {0, NAN, NAN, 0, false};

static uint32_t sampleCount = 0;
static uint32_t errorCount = 0;
static unsigned long lastSample = 0;
static unsigned long lastWifiCheck = 0;

static void pushSample(const Sample &s) {
    history[historyHead] = s;
    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) {
        historyCount++;
    }
}

static void readSensors() {
    Sample s;
    s.uptime = millis() / 1000;
    s.temperature = dht.readTemperature();
    s.humidity = dht.readHumidity();
    s.light = analogRead(PIN_LDR);
    s.valid = !isnan(s.temperature) && !isnan(s.humidity);

    sampleCount++;
    if (!s.valid) {
        errorCount++;
        Serial.printf("[%lu] dht read failed (%u total)\n", s.uptime, errorCount);
        return;
    }

    latest = s;
    pushSample(s);
    Serial.printf("[%lu] %.1f C  %.1f %%RH  light %u\n",
                  s.uptime, s.temperature, s.humidity, s.light);
}

static void sendTelemetry() {
    JsonDocument doc;
    doc["device"] = DEVICE_NAME;
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["rssi_dbm"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["samples"] = sampleCount;
    doc["read_errors"] = errorCount;

    JsonObject reading = doc["reading"].to<JsonObject>();
    if (latest.valid) {
        reading["temperature_c"] = round(latest.temperature * 10) / 10.0;
        reading["humidity_pct"] = round(latest.humidity * 10) / 10.0;
        reading["light_raw"] = latest.light;
        reading["light_pct"] = round((latest.light / 4095.0) * 1000) / 10.0;
        reading["age_s"] = (millis() / 1000) - latest.uptime;
    } else {
        reading["error"] = "no valid reading yet";
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void sendHistory() {
    JsonDocument doc;
    JsonArray items = doc["samples"].to<JsonArray>();

    size_t start = (historyHead + HISTORY_SIZE - historyCount) % HISTORY_SIZE;
    for (size_t i = 0; i < historyCount; i++) {
        const Sample &s = history[(start + i) % HISTORY_SIZE];
        JsonObject item = items.add<JsonObject>();
        item["t"] = s.uptime;
        item["temperature_c"] = round(s.temperature * 10) / 10.0;
        item["humidity_pct"] = round(s.humidity * 10) / 10.0;
        item["light_raw"] = s.light;
    }
    doc["count"] = historyCount;
    doc["interval_s"] = SAMPLE_INTERVAL_MS / 1000;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void sendIndex() {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
        server.send(500, "text/plain", "index.html missing, run: pio run -t uploadfs");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

static void connectWifi() {
    Serial.printf("connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        digitalWrite(PIN_LED, !digitalRead(PIN_LED));
        delay(300);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("wifi failed, restarting in 5 s");
        delay(5000);
        ESP.restart();
    }

    digitalWrite(PIN_LED, HIGH);
    Serial.printf("connected, ip %s, rssi %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_LDR, ADC_11db);

    dht.begin();

    if (!LittleFS.begin(true)) {
        Serial.println("littlefs mount failed");
    }

    connectWifi();

    if (MDNS.begin(DEVICE_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mdns ready at http://%s.local\n", DEVICE_NAME);
    }

    server.on("/", HTTP_GET, sendIndex);
    server.on("/api/telemetry", HTTP_GET, sendTelemetry);
    server.on("/api/history", HTTP_GET, sendHistory);
    server.on("/api/health", HTTP_GET, []() {
        server.send(latest.valid ? 200 : 503,
                    "text/plain",
                    latest.valid ? "ok" : "no sensor data");
    });
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });

    server.begin();
    Serial.println("http server listening on port 80");

    readSensors();
    lastSample = millis();
}

void loop() {
    server.handleClient();

    unsigned long now = millis();

    if (now - lastSample >= SAMPLE_INTERVAL_MS) {
        lastSample = now;
        readSensors();
    }

    if (now - lastWifiCheck >= 30000) {
        lastWifiCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("wifi dropped, reconnecting");
            digitalWrite(PIN_LED, LOW);
            WiFi.disconnect();
            connectWifi();
        }
    }
}
