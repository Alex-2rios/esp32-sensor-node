#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "history.h"

using telemetry::History;
using telemetry::Sample;
using telemetry::Stats;

DHT dht(PIN_DHT, DHT22);
WebServer server(80);

static History<HISTORY_SIZE> history;
static Sample latest;
static bool haveReading = false;

static uint32_t sampleCount = 0;
static uint32_t errorCount = 0;
static unsigned long lastSample = 0;
static unsigned long lastWifiCheck = 0;

static void readSensors() {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    uint16_t light = analogRead(PIN_LDR);

    sampleCount++;

    if (!telemetry::reading_is_plausible(temperature, humidity)) {
        errorCount++;
        Serial.printf("[%lu] sensor read rejected (%lu of %lu)\n",
                      millis() / 1000, errorCount, sampleCount);
        return;
    }

    Sample sample;
    sample.uptime_s = millis() / 1000;
    sample.temperature_c = temperature;
    sample.humidity_pct = humidity;
    sample.light_raw = light;

    latest = sample;
    haveReading = true;
    history.push(sample);

    Serial.printf("[%lu] %.1f C  %.1f %%RH  light %u\n",
                  sample.uptime_s, sample.temperature_c, sample.humidity_pct, sample.light_raw);
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
    doc["history_size"] = history.size();

    JsonObject reading = doc["reading"].to<JsonObject>();
    if (haveReading) {
        reading["temperature_c"] = round(latest.temperature_c * 10) / 10.0;
        reading["humidity_pct"] = round(latest.humidity_pct * 10) / 10.0;
        reading["light_raw"] = latest.light_raw;
        reading["light_pct"] = round(telemetry::light_percent(latest.light_raw) * 10) / 10.0;
        reading["age_s"] = (millis() / 1000) - latest.uptime_s;
    } else {
        reading["error"] = "no valid reading yet";
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void addStats(JsonObject parent, const char *key, const Stats &stats) {
    JsonObject node = parent[key].to<JsonObject>();
    node["min"] = round(stats.min * 10) / 10.0;
    node["max"] = round(stats.max * 10) / 10.0;
    node["mean"] = round(stats.mean * 10) / 10.0;
}

static void sendHistory() {
    JsonDocument doc;
    JsonArray items = doc["samples"].to<JsonArray>();

    for (uint16_t i = 0; i < history.size(); i++) {
        const Sample &sample = history.at(i);
        JsonObject item = items.add<JsonObject>();
        item["t"] = sample.uptime_s;
        item["temperature_c"] = round(sample.temperature_c * 10) / 10.0;
        item["humidity_pct"] = round(sample.humidity_pct * 10) / 10.0;
        item["light_raw"] = sample.light_raw;
    }

    doc["count"] = history.size();
    doc["capacity"] = history.capacity();
    doc["interval_s"] = SAMPLE_INTERVAL_MS / 1000;

    JsonObject stats = doc["stats"].to<JsonObject>();
    addStats(stats, "temperature_c", history.temperature_stats());
    addStats(stats, "humidity_pct", history.humidity_stats());

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
        server.send(haveReading ? 200 : 503,
                    "text/plain",
                    haveReading ? "ok" : "no sensor data");
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
