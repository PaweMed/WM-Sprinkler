#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>
#include <cstring>
#include "mbedtls/sha256.h"

#ifndef mbedtls_sha256_starts_ret
#define mbedtls_sha256_starts_ret mbedtls_sha256_starts
#endif
#ifndef mbedtls_sha256_update_ret
#define mbedtls_sha256_update_ret mbedtls_sha256_update
#endif
#ifndef mbedtls_sha256_finish_ret
#define mbedtls_sha256_finish_ret mbedtls_sha256_finish
#endif

#include "Zones.h"
#include "Programs.h"
#include "Weather.h"
#include "Logs.h"
#include "Config.h"
#include "DeviceIdentity.h"
#include "FirmwareVersion.h"
#include "OtaSecurity.h"
#include "OtaStateBackup.h"
#include "PushoverClient.h"
#include "EventMessages.h"

// Tematy MQTT (base = np. "sprinkler/esp32-001"):
//  - <base>/global/status           (retained JSON)
//  - <base>/weather                 (retained JSON)
//  - <base>/zones                   (retained JSON array: [{id,active,remaining,name}, ...])
//  - <base>/programs                (retained JSON array)
//  - <base>/logs                    (retained JSON object {"logs":[...]})
//  - <base>/settings/public         (retained JSON object – bez haseł itp.)
//  - <base>/rain-history            (retained JSON array/object – zależnie od Twojej impl.)
//  - <base>/watering-percent        (retained JSON object)
//  - homeassistant/<component>/<device_id>/<object_id>/config (retained discovery)
// Kompatybilnie per-strefa:
//  - <base>/zones/<id>/status       (retained "0"/"1")
//  - <base>/zones/<id>/remaining    (retained sekundy)
//
// Komendy z backendu (subskrybowane):
//  - <base>/global/refresh          (pusty payload) → publikacja wszystkich snapshotów
//  - <base>/cmd/zones/<id>/toggle   ("1"|"" )       → start/stop/toggle
//  - <base>/cmd/zones/<id>/start    (liczba sekund) → start na czas
//  - <base>/cmd/zones/<id>/stop     (pusty)         → stop
//  - <base>/cmd/zones-names/set     (JSON array)    → zmiana nazw stref
//  - <base>/cmd/programs/import     (JSON)          → import programów
//  - <base>/cmd/programs/edit/<id>  (JSON)          → edycja programu
//  - <base>/cmd/programs/delete/<id>(pusty)         → kasowanie programu
//  - <base>/cmd/logs/clear          (pusty)         → czyszczenie logów
//  - <base>/cmd/settings/set        (JSON)          → zapis ustawień PUBLICZNYCH
//  - <base>/cmd/ota/start           (JSON)          → OTA firmware/FS z URL
//  - <base>/cmd/plug/event          (JSON)          → Pushover/log dla zdarzenia gniazdka

class MQTTClient {
public:
  MQTTClient() : mqttClient(espClientTLS) {}

  void begin(Zones* z, Programs* p, Weather* w, Logs* l, Config* c, DeviceIdentity* di, PushoverClient* po = nullptr) {
    zones = z; programs = p; weather = w; logs = l; config = c;
    deviceIdentity = di;
    pushover = po;
    loadConfig();
  }

  // Używane przez WebServerUI.h → przeładuj konfigurację z Config i przełącz połączenie
  void updateConfig() {
    if (!config) return;
    // zapamiętaj czy byłeś połączony
    const bool wasConnected = mqttClient.connected();
    // przeładuj ustawienia z Config
    loadConfig();

    // jeśli byłeś połączony – rozłącz, by wymusić nową sesję z nowymi parametrami
    if (wasConnected) {
      mqttClient.disconnect();
    }
    // ustaw znacznik, by pętla po 100 ms spróbowała połączyć się ponownie
    lastReconnectAttempt = millis() - 1000;
  }

  void loadConfig() {
    if (!config) return;
    enabled      = config->getEnableMqtt();
    mqttServer   = config->getMqttServer();    // np. b74e....s1.eu.hivemq.cloud
    mqttPort     = config->getMqttPort();      // 8883
    mqttUser     = config->getMqttUser();      // np. sprinkler-app
    mqttPass     = config->getMqttPass();
    mqttClientId = config->getMqttClientId();  // np. sprinkler-esp32-001
    baseTopic    = config->getMqttTopicBase(); // np. sprinkler/esp32-001

    // TLS – bez weryfikacji CA (na start; docelowo możesz dodać CA brokera)
    espClientTLS.setInsecure();

    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setBufferSize(8192); // logi + snapshoty mogą być większe niż 2 KB
    mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
      this->onMessage(topic, payload, length);
    });
  }

  void loop() {
    if (!enabled || mqttServer.length() == 0) {
      if (mqttClient.connected()) {
        publishStringRetained(topic("global/availability"), "offline");
        mqttClient.disconnect();
      }
      return;
    }
    if (!mqttClient.connected()) {
      const unsigned long now = millis();
      if (now - lastReconnectAttempt > 500) {
        lastReconnectAttempt = now;
        (void)reconnect(); // próba; wynik i tak logujemy/obsługujemy dalej
      }
    } else {
      mqttClient.loop();
      maybePublishZonesAfterLocalChange();
      publishGlobalStatus();      // co ~10s
      publishAllSnapshots();      // co ~15s
      publishHomeAssistantDiscovery(); // okresowe odświeżenie konfiguracji discovery
      processPendingOta();
    }

  }

  void updateAfterZonesChange()        { publishZonesSnapshot(); publishGlobalStatus(true); }
  void updateAfterProgramsChange()     { publishProgramsSnapshot(); }
  void updateAfterLogsChange()         { publishLogsSnapshot(); }
  void updateAfterSettingsChange()     { publishSettingsPublicSnapshot(); publishHomeAssistantDiscovery(true); publishGlobalStatus(true); }
  void updateAfterWeatherChange()      { publishWeatherSnapshot(); publishWateringPercentSnapshot(); }
  void updateAfterRainHistoryChange()  { publishRainHistorySnapshot(); }

private:
  WiFiClientSecure espClientTLS;
  PubSubClient mqttClient;

  Zones*    zones    = nullptr;
  Programs* programs = nullptr;
  Weather*  weather  = nullptr;
  Logs*     logs     = nullptr;
  Config*   config   = nullptr;
  DeviceIdentity* deviceIdentity = nullptr;
  PushoverClient* pushover = nullptr;

  String mqttServer, mqttUser, mqttPass, mqttClientId, baseTopic;
  int    mqttPort  = 8883;
  bool   enabled   = true;
  static constexpr unsigned long ZONES_CHANGE_DEBOUNCE_MS = 350;

  unsigned long lastReconnectAttempt = 0;
  unsigned long lastStatusUpdate     = 0;
  unsigned long lastSnapshotUpdate   = 0;
  bool hassDiscoveryPublished        = false;
  unsigned long lastDiscoveryPublish = 0;
  String lastZoneMetaSignature       = "";
  static constexpr unsigned long DISCOVERY_REFRESH_MS = 300000UL; // 5 min
  static constexpr int MAX_DISCOVERY_ZONES = 8;

  struct OtaRequest {
    bool pending = false;
    String commandId;
    String commandTopic;
    String campaignId;
    String firmwareId;
    String version;
    String url;
    String sha256;
    String signature;
    String signatureAlg;
    String target = "firmware";
    int expectedSize = 0;
  } ota;
  bool otaInProgress = false;

  bool stageOtaBackup(const OtaRequest& req, String& errOut) {
    return OtaStateBackup::stage(config, req.target, req.version, errOut);
  }

  String zoneObject(int idx) const {
    const String zoneName = zones ? zones->getZoneName(idx) : "";
    return EventMessages::zoneObject(zoneName, idx + 1);
  }

  void sendPush(const EventMessages::PushMessage& msg) {
    if (!pushover || !config || !config->getEnablePushover()) return;
    pushover->send(msg.title, msg.body);
  }

  int minutesFromSeconds(int secs) const;
  String buildPlugTelemetry(bool hasVoltage, float voltageV, bool hasCurrent, float currentA, bool hasPower, float powerW, bool hasEnergy, float energyKwh) const;
  void logZoneEvent(const char* category, const char* action, const String& zoneObj, const String& sourceCode, int mins, const String& detail);
  void pushZoneEvent(const char* category, const char* action, const String& zoneObj, const String& sourceCode, int mins);
  void recordZoneEvent(int zoneId, bool wasActive, bool isActive, const String& sourceCode, const String& zoneObj, int startedSecs, const String& detail);
  void recordPlugEvent(const String& actionLabel, const String& plugLabel, const String& modeCode, bool hasState, bool isOn, int secs, const String& telemetryPart);
  bool handlePlugEventCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc);
  bool handleZoneNamesCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc);
  bool handleZoneCommand(const String& top, const String& msg, const String& commandId, bool hasJson, JsonDocument& cmdDoc);
  bool handleProgramsCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc);
  bool handleLogsClearCommand(const String& top, const String& commandId);
  bool handleSettingsCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc);
  bool handleOtaCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc);

  // ---- Utils ----
  String topic(const String& leaf) const {
    if (baseTopic.length() == 0) return leaf;
    if (baseTopic.endsWith("/")) return baseTopic + leaf;
    return baseTopic + "/" + leaf;
  }

  Weather::SmartIrrigationConfig buildSmartIrrigationConfig() const {
    Weather::SmartIrrigationConfig sc;
    if (!config) return sc;
    sc.tempSkipC = config->getIrrigationTempSkipC();
    sc.tempLowMaxC = config->getIrrigationTempLowMaxC();
    sc.tempMidMaxC = config->getIrrigationTempMidMaxC();
    sc.tempHighMaxC = config->getIrrigationTempHighMaxC();
    sc.tempFactorLow = config->getIrrigationTempFactorLow();
    sc.tempFactorMid = config->getIrrigationTempFactorMid();
    sc.tempFactorHigh = config->getIrrigationTempFactorHigh();
    sc.tempFactorVeryHigh = config->getIrrigationTempFactorVeryHigh();
    sc.rainSkipMm = config->getIrrigationRainSkipMm();
    sc.rainHighMinMm = config->getIrrigationRainHighMinMm();
    sc.rainMidMinMm = config->getIrrigationRainMidMinMm();
    sc.rainFactorHigh = config->getIrrigationRainFactorHigh();
    sc.rainFactorMid = config->getIrrigationRainFactorMid();
    sc.rainFactorLow = config->getIrrigationRainFactorLow();
    sc.humidityHighPercent = config->getIrrigationHumidityHighPercent();
    sc.humidityFactorHigh = config->getIrrigationHumidityFactorHigh();
    sc.windSkipKmh = config->getIrrigationWindSkipKmh();
    sc.windFactor = config->getIrrigationWindFactor();
    sc.percentMin = config->getIrrigationPercentMin();
    sc.percentMax = config->getIrrigationPercentMax();
    return sc;
  }

  String deviceIdForDiscovery() const {
    if (deviceIdentity && deviceIdentity->getDeviceId().length() > 0) return deviceIdentity->getDeviceId();
    if (mqttClientId.length() > 0) return mqttClientId;
    return "wm_unknown";
  }

  String sanitizeForEntityId(const String& in) const {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
      char c = in[i];
      if ((c >= 'a' && c <= 'z') ||
          (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') ||
          c == '_' || c == '-') {
        out += c;
      } else {
        out += '_';
      }
    }
    while (out.indexOf("__") >= 0) out.replace("__", "_");
    if (out.length() == 0) out = "wm";
    return out;
  }

  String hassObjectId(const String& suffix) const {
    return sanitizeForEntityId("wm_" + deviceIdForDiscovery() + "_" + suffix);
  }

  String discoveryNodeId() const {
    return sanitizeForEntityId(deviceIdForDiscovery());
  }

  String discoveryTopic(const String& component, const String& objectId) const {
    // Use node_id=device_id so broker ACL can isolate discovery per device.
    return "homeassistant/" + component + "/" + discoveryNodeId() + "/" + objectId + "/config";
  }

  String hardwareId() const {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    return "esp32c6";
#elif defined(CONFIG_IDF_TARGET_ESP32)
    return "esp32";
#else
    return "esp32";
#endif
  }

  String hardwareModelName() const {
    const String hw = hardwareId();
    if (hw == "esp32c6") return "WM Sprinkler ESP32-C6";
    return "WM Sprinkler ESP32";
  }

  void fillHassDevice(JsonDocument& doc) {
    JsonObject dev = doc["device"].to<JsonObject>();
    JsonArray ids = dev["identifiers"].to<JsonArray>();
    ids.add(deviceIdForDiscovery());
    dev["name"] = "WM Sprinkler " + deviceIdForDiscovery();
    dev["manufacturer"] = "PaweMed";
    dev["model"] = hardwareModelName();
    dev["sw_version"] = FirmwareVersionStore::reportedVersion();
  }

  void fillHassAvailability(JsonDocument& doc) {
    doc["availability_topic"] = topic("global/availability");
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
  }

  bool publishDiscoveryConfig(const String& component, const String& objectId, JsonDocument& doc) {
    return publishJsonRetained(discoveryTopic(component, objectId), doc);
  }

  void clearDiscoveryConfig(const String& component, const String& objectId) {
    mqttClient.publish(discoveryTopic(component, objectId).c_str(), "", true);
  }

  String buildZoneMetaSignature(const JsonDocument& zonesDoc) {
    if (!zonesDoc.is<JsonArrayConst>()) return "";
    JsonArrayConst arr = zonesDoc.as<JsonArrayConst>();
    String sig = String(arr.size()) + "|";
    for (JsonVariantConst v : arr) {
      sig += String(v["name"] | "");
      sig += "|";
    }
    return sig;
  }

  void publishHomeAssistantDiscovery(bool force = false) {
    if (!mqttClient.connected()) return;
    const unsigned long now = millis();
    if (!force && hassDiscoveryPublished && (now - lastDiscoveryPublish < DISCOVERY_REFRESH_MS)) return;

    hassDiscoveryPublished = true;
    lastDiscoveryPublish = now;

    const String deviceId = deviceIdForDiscovery();
    const String weatherTopic = topic("weather");
    const String statusTopic = topic("global/status");
    const String wateringTopic = topic("watering-percent");

    // Connectivity binary sensor.
    {
      JsonDocument doc;
      doc["name"] = "Połączenie";
      doc["unique_id"] = hassObjectId("connectivity");
      doc["state_topic"] = topic("global/availability");
      doc["payload_on"] = "online";
      doc["payload_off"] = "offline";
      doc["device_class"] = "connectivity";
      doc["entity_category"] = "diagnostic";
      fillHassDevice(doc);
      publishDiscoveryConfig("binary_sensor", hassObjectId("connectivity"), doc);
    }

    // Device IP (diagnostic).
    {
      JsonDocument doc;
      doc["name"] = "Adres IP";
      doc["unique_id"] = hassObjectId("ip");
      doc["state_topic"] = statusTopic;
      doc["value_template"] = "{{ value_json.ip }}";
      doc["entity_category"] = "diagnostic";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("ip"), doc);
    }

    // Firmware version (diagnostic).
    {
      JsonDocument doc;
      doc["name"] = "Wersja FW";
      doc["unique_id"] = hassObjectId("fw_version");
      doc["state_topic"] = statusTopic;
      doc["value_template"] = "{{ value_json.fw_version }}";
      doc["entity_category"] = "diagnostic";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("fw_version"), doc);
    }

    // Weather: temperature.
    {
      JsonDocument doc;
      doc["name"] = "Temperatura";
      doc["unique_id"] = hassObjectId("temperature");
      doc["state_topic"] = weatherTopic;
      doc["value_template"] = "{{ value_json.temp | float(0) }}";
      doc["device_class"] = "temperature";
      doc["unit_of_measurement"] = "°C";
      doc["state_class"] = "measurement";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("temperature"), doc);
    }

    // Weather: humidity.
    {
      JsonDocument doc;
      doc["name"] = "Wilgotność";
      doc["unique_id"] = hassObjectId("humidity");
      doc["state_topic"] = weatherTopic;
      doc["value_template"] = "{{ value_json.humidity | float(0) }}";
      doc["device_class"] = "humidity";
      doc["unit_of_measurement"] = "%";
      doc["state_class"] = "measurement";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("humidity"), doc);
    }

    // Weather logic: watering percent.
    {
      JsonDocument doc;
      doc["name"] = "Procent podlewania";
      doc["unique_id"] = hassObjectId("watering_percent");
      doc["state_topic"] = wateringTopic;
      doc["value_template"] = "{{ value_json.percent | int(0) }}";
      doc["unit_of_measurement"] = "%";
      doc["state_class"] = "measurement";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("watering_percent"), doc);
    }

    // Rain 24h.
    {
      JsonDocument doc;
      doc["name"] = "Opad 24h";
      doc["unique_id"] = hassObjectId("rain_24h");
      doc["state_topic"] = wateringTopic;
      doc["value_template"] = "{{ value_json.rain_24h | float(0) }}";
      doc["unit_of_measurement"] = "mm";
      doc["state_class"] = "measurement";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("sensor", hassObjectId("rain_24h"), doc);
    }

    // Button to refresh snapshots.
    {
      JsonDocument doc;
      doc["name"] = "Odśwież dane";
      doc["unique_id"] = hassObjectId("refresh");
      doc["command_topic"] = topic("global/refresh");
      doc["payload_press"] = "1";
      doc["entity_category"] = "config";
      fillHassAvailability(doc);
      fillHassDevice(doc);
      publishDiscoveryConfig("button", hassObjectId("refresh"), doc);
    }

    const int zoneCount = zones ? zones->getZoneCount() : 0;
    for (int i = 0; i < MAX_DISCOVERY_ZONES; ++i) {
      const String zoneId1 = String(i + 1);
      const String switchObj = hassObjectId("zone_" + zoneId1);
      const String remainObj = hassObjectId("zone_" + zoneId1 + "_remaining");
      if (i < zoneCount) {
        const String zoneName = zones ? zones->getZoneName(i) : ("Strefa " + zoneId1);

        JsonDocument swDoc;
        swDoc["name"] = zoneName;
        swDoc["unique_id"] = switchObj;
        swDoc["state_topic"] = topic("zones/" + String(i) + "/status");
        swDoc["command_topic"] = topic("cmd/zones/" + String(i) + "/toggle");
        swDoc["state_on"] = "1";
        swDoc["state_off"] = "0";
        swDoc["payload_on"] = "1";
        swDoc["payload_off"] = "0";
        swDoc["icon"] = "mdi:sprinkler-variant";
        fillHassAvailability(swDoc);
        fillHassDevice(swDoc);
        publishDiscoveryConfig("switch", switchObj, swDoc);

        JsonDocument remDoc;
        remDoc["name"] = zoneName + " - pozostały czas";
        remDoc["unique_id"] = remainObj;
        remDoc["state_topic"] = topic("zones/" + String(i) + "/remaining");
        remDoc["value_template"] = "{{ value | int(0) }}";
        remDoc["unit_of_measurement"] = "s";
        remDoc["state_class"] = "measurement";
        remDoc["device_class"] = "duration";
        remDoc["entity_category"] = "diagnostic";
        fillHassAvailability(remDoc);
        fillHassDevice(remDoc);
        publishDiscoveryConfig("sensor", remainObj, remDoc);
      } else {
        // Usuń encje z discovery, jeśli liczba stref została zmniejszona.
        clearDiscoveryConfig("switch", switchObj);
        clearDiscoveryConfig("sensor", remainObj);
      }
    }
  }

  static bool parseIntSafe(const String& s, int& out) {
    if (s.length() == 0) return false;
    bool neg = false; long v = 0; int i = 0;
    if (s[0] == '-') { neg = true; i = 1; }
    for (; i < (int)s.length(); ++i) {
      char c = s[i]; if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0'); if (v > 10000000) return false;
    }
    out = (int)(neg ? -v : v);
    return true;
  }

  String currentTimestamp() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }

  bool parseJson(const String& msg, JsonDocument& doc) {
    return deserializeJson(doc, msg) == DeserializationError::Ok;
  }

  void publishCommandAck(const String& commandId, const String& commandTopic, bool accepted, const String& detail) {
    JsonDocument doc;
    doc["command_id"] = commandId;
    doc["command_topic"] = commandTopic;
    doc["status"] = accepted ? "accepted" : "failed";
    doc["detail"] = detail;
    doc["timestamp"] = currentTimestamp();
    String out;
    serializeJson(doc, out);
    mqttClient.publish(topic("ack").c_str(), out.c_str(), false);
  }

  String normalizeHex(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
      char c = in[i];
      if (c >= 'A' && c <= 'F') c = char(c - 'A' + 'a');
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) out += c;
    }
    return out;
  }

  String normalizeOtaTarget(String target) {
    target.trim();
    target.toLowerCase();
    if (target == "fs" || target == "spiffs" || target == "littlefs" || target == "filesystem") {
      return "fs";
    }
    return "firmware";
  }

  String normalizeOtaHardware(String hw) const {
    hw.trim();
    hw.toLowerCase();
    if (hw == "esp32c6" || hw == "esp32-c6" || hw == "c6") return "esp32c6";
    if (hw == "esp32" || hw == "esp-32" || hw == "esp32dev" || hw == "classic") return "esp32";
    return "";
  }

  String otaHardwareLabel(const String& hw) const {
    if (hw == "esp32c6") return "ESP32-C6";
    if (hw == "esp32") return "ESP32";
    return "unknown";
  }

  bool otaTargetIsFs(const OtaRequest& req) const {
    return req.target == "fs";
  }

  int otaUpdateCommand(const OtaRequest& req) const {
    return otaTargetIsFs(req) ? U_SPIFFS : U_FLASH;
  }

  String otaTargetLabel(const OtaRequest& req) const {
    return otaTargetIsFs(req) ? "systemu plików" : "firmware";
  }

  String hashToHex(const uint8_t hash[32]) {
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    hex[64] = '\0';
    return String(hex);
  }

  void publishOtaStatus(const String& stage, int progress, const String& detail, const OtaRequest* req = nullptr, const String& error = "") {
    JsonDocument doc;
    doc["stage"] = stage;
    if (progress >= 0) doc["progress"] = progress;
    doc["detail"] = detail;
    if (error.length() > 0) doc["error"] = error;
    doc["timestamp"] = currentTimestamp();
    if (deviceIdentity) doc["device_id"] = deviceIdentity->getDeviceId();
    if (req) {
      if (req->campaignId.length() > 0) doc["campaign_id"] = req->campaignId;
      if (req->firmwareId.length() > 0) doc["firmware_id"] = req->firmwareId;
      if (req->version.length() > 0) doc["version"] = req->version;
      if (req->expectedSize > 0) doc["size"] = req->expectedSize;
      if (req->target.length() > 0) doc["target"] = req->target;
    }
    String out;
    serializeJson(doc, out);
    mqttClient.publish(topic("ota/status").c_str(), out.c_str(), false);
    Serial.println("[OTA] " + stage + " " + String(progress) + "% - " + detail + (error.length() ? (" | " + error) : ""));
  }

  bool downloadAndHashPass(
    const OtaRequest& req,
    const String& stage,
    const String& detail,
    bool writeToFlash,
    const uint8_t* expectedHash,
    uint8_t outHash[32],
    size_t& outBytes,
    String& errOut
  ) {
    outBytes = 0;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, req.url)) {
      errOut = "HTTP begin failed";
      return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      errOut = "HTTP GET code " + String(code);
      http.end();
      return false;
    }

    const int total = http.getSize();
    if (req.expectedSize > 0 && total > 0 && req.expectedSize != total) {
      errOut = "size mismatch (expected " + String(req.expectedSize) + ", got " + String(total) + ")";
      http.end();
      return false;
    }

    if (writeToFlash) {
      if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN, otaUpdateCommand(req))) {
        StreamString ss; Update.printError(ss);
        errOut = "Update.begin: " + String(ss.c_str());
        http.end();
        return false;
      }
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buff[1024];
    unsigned long lastProgressTs = 0;
    int lastProgressPct = -1;

    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts_ret(&shaCtx, 0);

    while (http.connected() && (total < 0 || (int)outBytes < total)) {
      size_t avail = stream->available();
      if (!avail) {
        delay(1);
        continue;
      }

      int toRead = (int)min((size_t)sizeof(buff), avail);
      int readLen = stream->readBytes(buff, toRead);
      if (readLen <= 0) {
        delay(1);
        continue;
      }

      mbedtls_sha256_update_ret(&shaCtx, buff, readLen);
      if (writeToFlash) {
        size_t w = Update.write(buff, (size_t)readLen);
        if (w != (size_t)readLen) {
          StreamString ss; Update.printError(ss);
          errOut = "Update.write: " + String(ss.c_str());
          Update.end(false);
          http.end();
          mbedtls_sha256_free(&shaCtx);
          return false;
        }
      }

      outBytes += (size_t)readLen;
      int pct = 0;
      if (total > 0) pct = (int)((outBytes * 100UL) / (size_t)total);
      unsigned long nowMs = millis();
      if (pct != lastProgressPct && (lastProgressPct < 0 || pct - lastProgressPct >= 2 || nowMs - lastProgressTs > 1200)) {
        lastProgressPct = pct;
        lastProgressTs = nowMs;
        publishOtaStatus(stage, pct, detail, &req);
      }
      delay(1);
    }

    mbedtls_sha256_finish_ret(&shaCtx, outHash);
    mbedtls_sha256_free(&shaCtx);

    if (total > 0 && (int)outBytes != total) {
      errOut = "download incomplete (got " + String((int)outBytes) + ", expected " + String(total) + ")";
      if (writeToFlash) Update.end(false);
      http.end();
      return false;
    }
    if (req.expectedSize > 0 && req.expectedSize != (int)outBytes) {
      errOut = "size mismatch after download (expected " + String(req.expectedSize) + ", got " + String((int)outBytes) + ")";
      if (writeToFlash) Update.end(false);
      http.end();
      return false;
    }

    if (expectedHash && memcmp(outHash, expectedHash, 32) != 0) {
      errOut = "hash mismatch between verification and flash pass";
      if (writeToFlash) Update.end(false);
      http.end();
      return false;
    }

    if (writeToFlash) {
      if (!Update.end(true)) {
        StreamString ss; Update.printError(ss);
        errOut = "Update.end: " + String(ss.c_str());
        http.end();
        return false;
      }
    }

    http.end();
    return true;
  }

  bool executeOta(const OtaRequest& req, String& errOut) {
    const String expectedSha = normalizeHex(req.sha256);
    if (expectedSha.length() > 0 && expectedSha.length() != 64) {
      errOut = "invalid sha256 format";
      return false;
    }

    String sigAlg = req.signatureAlg;
    sigAlg.trim();
    sigAlg.toLowerCase();
    if (sigAlg.length() == 0) sigAlg = "ed25519";
    if ((req.signature.length() > 0 || OtaSecurity::requireSignature()) && sigAlg != "ed25519") {
      errOut = "unsupported signature algorithm: " + sigAlg;
      return false;
    }

    uint8_t verifiedHash[32];
    size_t verifiedBytes = 0;
    if (!downloadAndHashPass(req, "verifying", "Weryfikacja " + otaTargetLabel(req), false, nullptr, verifiedHash, verifiedBytes, errOut)) {
      return false;
    }

    const String gotSha = hashToHex(verifiedHash);
    if (expectedSha.length() == 64 && gotSha != expectedSha) {
      errOut = "sha256 mismatch";
      return false;
    }

    String sigErr;
    if (!OtaSecurity::verifyHashEd25519(verifiedHash, req.signature, sigErr)) {
      errOut = "signature verify failed: " + sigErr;
      return false;
    }
    publishOtaStatus("verified", 100, "Podpis OTA poprawny", &req);

    uint8_t flashedHash[32];
    size_t flashedBytes = 0;
    if (!downloadAndHashPass(req, "downloading", "Pobieranie " + otaTargetLabel(req), true, verifiedHash, flashedHash, flashedBytes, errOut)) {
      return false;
    }

    if (expectedSha.length() == 64 && hashToHex(flashedHash) != expectedSha) {
      errOut = "sha256 mismatch after flash write";
      return false;
    }

    return true;
  }

  void processPendingOta() {
    if (!ota.pending || otaInProgress) return;
    if (WiFi.status() != WL_CONNECTED) {
      publishOtaStatus("failed", 0, "Brak WiFi do OTA", &ota, "wifi offline");
      if (logs) {
        String logMsg = EventMessages::logTitle("OTA", "blad");
        EventMessages::appendLogField(logMsg, "powod", "brak WiFi");
        logs->add(logMsg);
      }
      ota.pending = false;
      return;
    }

    otaInProgress = true;
    OtaRequest req = ota;
    ota.pending = false;

    String backupErr;
    if (!stageOtaBackup(req, backupErr)) {
      publishOtaStatus("failed", 0, "Backup OTA nieudany", &req, backupErr);
      publishCommandAck(req.commandId, req.commandTopic, false, "OTA backup failed: " + backupErr);
      if (logs) {
        String logMsg = EventMessages::logTitle("OTA", "blad backupu");
        EventMessages::appendLogField(logMsg, "powod", backupErr);
        logs->add(logMsg);
      }
      otaInProgress = false;
      return;
    }

    if (logs) {
      String logMsg = EventMessages::logTitle("OTA", "start");
      EventMessages::appendLogField(logMsg, "target", req.target);
      EventMessages::appendLogField(logMsg, "wersja", req.version);
      logs->add(logMsg);
    }
    publishOtaStatus("verifying", 0, "Start OTA (" + otaTargetLabel(req) + ")", &req);
    String err;
    bool ok = executeOta(req, err);
    if (!ok) {
      OtaStateBackup::clear();
      publishOtaStatus("failed", 0, "OTA nieudane", &req, err);
      publishCommandAck(req.commandId, req.commandTopic, false, "OTA failed: " + err);
      if (logs) {
        String logMsg = EventMessages::logTitle("OTA", "blad");
        EventMessages::appendLogField(logMsg, "powod", err);
        logs->add(logMsg);
      }
      otaInProgress = false;
      return;
    }

    if (!otaTargetIsFs(req)) {
      String markerErr;
      if (!OtaSecurity::markBootPending(req.version, req.target, markerErr)) {
        Serial.println("[OTA] WARN boot marker: " + markerErr);
        if (logs) {
          String logMsg = EventMessages::logTitle("OTA", "ostrzezenie");
          EventMessages::appendLogField(logMsg, "powod", "boot marker");
          EventMessages::appendLogField(logMsg, "szczegoly", markerErr);
          logs->add(logMsg);
        }
      } else if (req.version.length() > 0) {
        String versionErr;
        if (!FirmwareVersionStore::stagePendingVersion(req.version, versionErr)) {
          Serial.println("[FWVER] WARN stage pending: " + versionErr);
          if (logs) {
            String logMsg = EventMessages::logTitle("OTA", "ostrzezenie");
            EventMessages::appendLogField(logMsg, "powod", "wersja OTA");
            EventMessages::appendLogField(logMsg, "szczegoly", versionErr);
            logs->add(logMsg);
          }
        }
      }
    }

    publishOtaStatus("done", 100, "OTA zakończone, restart", &req);
    publishCommandAck(req.commandId, req.commandTopic, true, "OTA done, restart");
    if (logs) {
      String logMsg = EventMessages::logTitle("OTA", "sukces");
      EventMessages::appendLogField(logMsg, "target", req.target);
      EventMessages::appendLogField(logMsg, "wersja", req.version);
      EventMessages::appendLogField(logMsg, "wynik", "restart");
      logs->add(logMsg);
    }
    otaInProgress = false;
    delay(600);
    ESP.restart();
  }

  // ---- Połączenie i subskrypcje ----
  bool reconnect() {
    bool ok = false;
    const String availability = topic("global/availability");
    if (mqttUser.length() > 0) {
      ok = mqttClient.connect(mqttClientId.c_str(), mqttUser.c_str(), mqttPass.c_str(), availability.c_str(), 1, true, "offline");
    } else {
      ok = mqttClient.connect(mqttClientId.c_str(), availability.c_str(), 1, true, "offline");
    }
    if (ok) {
      publishStringRetained(availability, "online");
      subscribeTopics();
      publishHomeAssistantDiscovery(true);
      publishGlobalStatus(true);
      publishAllSnapshots(true);
    } else {
      Serial.printf("[MQTT] Connect failed, state=%d\n", mqttClient.state());
    }
    return ok;
  }

  void subscribeTopics() {
    // Komendy
    const String t1  = topic("global/refresh");
    const String t2  = topic("cmd/zones/+/toggle");
    const String t3  = topic("cmd/zones/+/start");
    const String t4  = topic("cmd/zones/+/stop");
    const String t5  = topic("cmd/zones-names/set");
    const String t6  = topic("cmd/programs/import");
    const String t7  = topic("cmd/programs/add");
    const String t8  = topic("cmd/programs/edit/+");
    const String t9  = topic("cmd/programs/delete/+");
    const String t10 = topic("cmd/logs/clear");
    const String t11 = topic("cmd/settings/set");
    const String t12 = topic("cmd/ota/start");
    const String t13 = "homeassistant/status";
    const String t14 = topic("cmd/plug/event");

    mqttClient.subscribe(t1.c_str());
    mqttClient.subscribe(t2.c_str());
    mqttClient.subscribe(t3.c_str());
    mqttClient.subscribe(t4.c_str());
    mqttClient.subscribe(t5.c_str());
    mqttClient.subscribe(t6.c_str());
    mqttClient.subscribe(t7.c_str());
    mqttClient.subscribe(t8.c_str());
    mqttClient.subscribe(t9.c_str());
    mqttClient.subscribe(t10.c_str());
    mqttClient.subscribe(t11.c_str());
    mqttClient.subscribe(t12.c_str());
    mqttClient.subscribe(t13.c_str());
    mqttClient.subscribe(t14.c_str());
  }

  // ---- Publikacje (retained) ----
  bool publishJsonRetained(const String& t, const JsonDocument& doc) {
    String s; serializeJson(doc, s);
    return mqttClient.publish(t.c_str(), s.c_str(), true);
  }
  bool publishStringRetained(const String& t, const String& s) {
    return mqttClient.publish(t.c_str(), s.c_str(), true);
  }

  void publishGlobalStatus(bool force=false) {
    const unsigned long now = millis();
    if (!force && now - lastStatusUpdate < 10000) return; // co 10s
    lastStatusUpdate = now;

    JsonDocument doc;
    doc["wifi"]   = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
    doc["ip"]     = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
    if (WiFi.status() == WL_CONNECTED) {
      doc["ssid"] = WiFi.SSID();
      doc["bssid"] = WiFi.BSSIDstr();
      doc["gateway"] = WiFi.gatewayIP().toString();
    }

    time_t tnow = time(nullptr);
    struct tm t; localtime_r(&tnow, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    doc["time"]   = buf;
    doc["online"] = true;
    doc["fw_version"] = FirmwareVersionStore::reportedVersion();
    doc["hardware"] = hardwareId();
    doc["model"] = hardwareModelName();
    if (deviceIdentity) {
      doc["device_id"] = deviceIdentity->getDeviceId();
      doc["claim_code"] = deviceIdentity->getClaimCode();
    }

    publishJsonRetained(topic("global/status"), doc);
  }

  void publishZonesSnapshot() {
    if (!zones) return;
    // Snapshot został właśnie opublikowany, więc czyścimy lokalny "dirty flag".
    (void)zones->consumeStateChange(0);

    // 1) Pobierz pełny JSON stref z istniejącej implementacji:
    JsonDocument doc;
    zones->toJson(doc); // oczekujemy tablicy [{id,active,remaining,name}, ...]

    // 2) Opublikuj całą tablicę:
    publishJsonRetained(topic("zones"), doc);

    // 3) Dodatkowo opublikuj per-strefa na podstawie TEGO SAMEGO JSON-a
    if (doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      int idx = 0;
      for (JsonVariant v : arr) {
        int id = v.containsKey("id") ? (int)v["id"].as<int>() : idx;
        bool active = v.containsKey("active") ? v["active"].as<bool>() : false;
        int remaining = v.containsKey("remaining") ? v["remaining"].as<int>() : 0;

        publishStringRetained(topic("zones/" + String(id) + "/status"), active ? "1" : "0");
        publishStringRetained(topic("zones/" + String(id) + "/remaining"), String(remaining));
        ++idx;
      }
    }

    const String zoneSig = buildZoneMetaSignature(doc);
    if (zoneSig.length() > 0 && zoneSig != lastZoneMetaSignature) {
      lastZoneMetaSignature = zoneSig;
      publishHomeAssistantDiscovery(true);
    }
  }

  void publishProgramsSnapshot() {
    if (!programs) return;
    JsonDocument doc;
    programs->toJson(doc); // tablica/obiekt – zależnie od Twojej implementacji
    publishJsonRetained(topic("programs"), doc);
  }

  void publishLogsSnapshot() {
    if (!logs) return;
    // Logi potrafią rosnąć – publikujemy maksymalny możliwy zakres, aby zmieścić się w MQTT packet size.
    const int limits[] = {50, 40, 30, 20, 10, 5};
    for (int limit : limits) {
      JsonDocument doc;
      logs->toJson(doc, limit); // {"logs":[...]}
      if (publishJsonRetained(topic("logs"), doc)) {
        return;
      }
    }
  }

  void publishSettingsPublicSnapshot() {
    if (!config) return;
    JsonDocument doc;
    config->toJson(doc);
    doc["owmApiKeyConfigured"] = config->getOwmApiKey().length() > 0;
    doc["mqttPassConfigured"] = config->getMqttPass().length() > 0;
    doc["pushoverUserConfigured"] = config->getPushoverUser().length() > 0;
    doc["pushoverTokenConfigured"] = config->getPushoverToken().length() > 0;
    // Usuń wrażliwe pola:
    doc["pass"]          = "";
    doc["mqttPass"]      = "";
    doc["owmApiKey"]     = "";
    doc["pushoverUser"]  = "";
    doc["pushoverToken"] = "";
    publishJsonRetained(topic("settings/public"), doc);
  }

  void publishWeatherSnapshot() {
    if (!weather) return;
    JsonDocument doc;
    weather->toJson(doc);
    publishJsonRetained(topic("weather"), doc);
  }

  void publishRainHistorySnapshot() {
    if (!weather) return;
    JsonDocument doc;
    weather->rainHistoryToJson(doc);
    publishJsonRetained(topic("rain-history"), doc);
  }

  void publishWateringPercentSnapshot() {
    if (!weather) return;
    JsonDocument doc;
    weather->irrigationDecisionToJson(doc);
    doc["explain"] = weather->getWateringDecisionExplain();
    doc["daily_max_temp"] = weather->getDailyMaxTemp();
    doc["daily_humidity_forecast"] = weather->getDailyHumidityForecast();
    publishJsonRetained(topic("watering-percent"), doc);
  }

  void publishAllSnapshots(bool force=false) {
    const unsigned long now = millis();
    if (!force && now - lastSnapshotUpdate < 15000) return; // co 15s
    lastSnapshotUpdate = now;

    publishGlobalStatus(true);
    publishZonesSnapshot();
    publishProgramsSnapshot();
    publishLogsSnapshot();
    publishSettingsPublicSnapshot();
    publishWeatherSnapshot();
    publishRainHistorySnapshot();
    publishWateringPercentSnapshot();
  }

  void maybePublishZonesAfterLocalChange() {
    if (!zones) return;
    if (!zones->consumeStateChange(ZONES_CHANGE_DEBOUNCE_MS)) return;
    publishZonesSnapshot();
    publishGlobalStatus(true);
  }

  // ---- Obsługa komend ----
  void onMessage(char* topicC, byte* payload, unsigned int length) {
    const String top = String(topicC);
    String msg; msg.reserve(length);
    for (unsigned int i=0; i<length; ++i) msg += (char)payload[i];

    JsonDocument cmdDoc;
    const bool hasJson = parseJson(msg, cmdDoc);
    String commandId = "";
    if (hasJson && cmdDoc["command_id"].is<const char*>()) {
      commandId = cmdDoc["command_id"].as<const char*>();
    }
    if (commandId.length() == 0) commandId = "legacy_" + String(millis());

    if (top == "homeassistant/status") {
      if (msg == "online" || msg == "ONLINE") {
        publishHomeAssistantDiscovery(true);
        publishAllSnapshots(true);
      }
      return;
    }

    if (top == topic("global/refresh")) {
      publishAllSnapshots(true);
      publishCommandAck(commandId, top, true, "refresh wykonany");
      return;
    }

    if (handlePlugEventCommand(top, commandId, hasJson, cmdDoc)) return;
    if (handleZoneNamesCommand(top, commandId, hasJson, cmdDoc)) return;
    if (handleZoneCommand(top, msg, commandId, hasJson, cmdDoc)) return;
    if (handleProgramsCommand(top, commandId, hasJson, cmdDoc)) return;
    if (handleLogsClearCommand(top, commandId)) return;
    if (handleSettingsCommand(top, commandId, hasJson, cmdDoc)) return;
    if (handleOtaCommand(top, commandId, hasJson, cmdDoc)) return;
  }
};

bool MQTTClient::handlePlugEventCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  if (top != topic("cmd/plug/event")) return false;
  if (!hasJson) {
    publishCommandAck(commandId, top, false, "brak payloadu JSON");
    return true;
  }

  String plugDeviceId = "";
  if (cmdDoc["plug_device_id"].is<const char*>()) plugDeviceId = String(cmdDoc["plug_device_id"].as<const char*>());
  else if (cmdDoc["device_id"].is<const char*>()) plugDeviceId = String(cmdDoc["device_id"].as<const char*>());

  String plugName = "";
  if (cmdDoc["plug_name"].is<const char*>()) plugName = String(cmdDoc["plug_name"].as<const char*>());
  else if (cmdDoc["name"].is<const char*>()) plugName = String(cmdDoc["name"].as<const char*>());

  String modeLabel = "";
  if (cmdDoc["mode"].is<const char*>()) modeLabel = String(cmdDoc["mode"].as<const char*>());
  String source = "";
  if (cmdDoc["source"].is<const char*>()) source = String(cmdDoc["source"].as<const char*>());
  if (modeLabel.length() == 0) modeLabel = source;
  if (modeLabel.length() == 0) modeLabel = "GNIAZDKO";

  bool hasState = false;
  bool isOn = false;
  if (cmdDoc["on"].is<bool>()) {
    isOn = cmdDoc["on"].as<bool>();
    hasState = true;
  } else if (cmdDoc["relay_on"].is<bool>()) {
    isOn = cmdDoc["relay_on"].as<bool>();
    hasState = true;
  } else if (cmdDoc["on"].is<int>()) {
    isOn = cmdDoc["on"].as<int>() != 0;
    hasState = true;
  } else if (cmdDoc["relay_on"].is<int>()) {
    isOn = cmdDoc["relay_on"].as<int>() != 0;
    hasState = true;
  } else if (cmdDoc["state"].is<const char*>()) {
    String st = String(cmdDoc["state"].as<const char*>());
    st.toLowerCase();
    if (st == "on" || st == "1" || st == "true" || st == "start" || st == "started") {
      isOn = true;
      hasState = true;
    } else if (st == "off" || st == "0" || st == "false" || st == "stop" || st == "stopped") {
      isOn = false;
      hasState = true;
    }
  }
  if (!hasState && cmdDoc["action"].is<const char*>()) {
    String action = String(cmdDoc["action"].as<const char*>());
    action.toLowerCase();
    if (action == "on" || action == "start" || action == "enable") {
      isOn = true;
      hasState = true;
    } else if (action == "off" || action == "stop" || action == "disable") {
      isOn = false;
      hasState = true;
    }
  }

  int secs = 0;
  if (cmdDoc["seconds"].is<int>()) secs = cmdDoc["seconds"].as<int>();
  else if (cmdDoc["remaining_sec"].is<int>()) secs = cmdDoc["remaining_sec"].as<int>();
  if (secs < 0) secs = 0;

  bool hasPower = false, hasVoltage = false, hasCurrent = false, hasEnergy = false;
  float powerW = 0.0f, voltageV = 0.0f, currentA = 0.0f, energyKwh = 0.0f;
  if (cmdDoc["power_w"].is<float>() || cmdDoc["power_w"].is<int>()) {
    powerW = cmdDoc["power_w"].as<float>();
    hasPower = true;
  }
  if (cmdDoc["voltage_v"].is<float>() || cmdDoc["voltage_v"].is<int>()) {
    voltageV = cmdDoc["voltage_v"].as<float>();
    hasVoltage = true;
  }
  if (cmdDoc["current_a"].is<float>() || cmdDoc["current_a"].is<int>()) {
    currentA = cmdDoc["current_a"].as<float>();
    hasCurrent = true;
  }
  if (cmdDoc["energy_total_kwh"].is<float>() || cmdDoc["energy_total_kwh"].is<int>()) {
    energyKwh = cmdDoc["energy_total_kwh"].as<float>();
    hasEnergy = true;
  }

  const String plugLabel = plugName.length() > 0 ? plugName : (plugDeviceId.length() > 0 ? plugDeviceId : "gniazdko");
  const String actionLabel = hasState ? (isOn ? "wlaczono" : "wylaczono") : "aktualizacja";
  const String modeCode = EventMessages::sourceCode(modeLabel);

  String telemetryPart = buildPlugTelemetry(hasVoltage, voltageV, hasCurrent, currentA, hasPower, powerW, hasEnergy, energyKwh);
  recordPlugEvent(actionLabel, plugLabel, modeCode, hasState, isOn, secs, telemetryPart);

  publishCommandAck(commandId, top, true, "zdarzenie gniazdka przetworzone");
  return true;
}

bool MQTTClient::handleZoneNamesCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  if (top != topic("cmd/zones-names/set")) return false;
  if (!zones) {
    publishCommandAck(commandId, top, false, "moduł zones niedostępny");
    return true;
  }
  bool ok = false;
  if (hasJson && cmdDoc["names"].is<JsonArray>()) {
    zones->setAllZoneNames(cmdDoc["names"].as<JsonArray>());
    ok = true;
  } else if (hasJson && cmdDoc.is<JsonArray>()) {
    zones->setAllZoneNames(cmdDoc.as<JsonArray>());
    ok = true;
  }
  if (!ok) {
    publishCommandAck(commandId, top, false, "brak poprawnej tablicy names");
    return true;
  }
  if (logs) logs->add(EventMessages::logSentence("SYSTEM", EventMessages::zoneNamesSaved()));
  publishZonesSnapshot();
  publishHomeAssistantDiscovery(true);
  publishCommandAck(commandId, top, true, "nazwy stref zapisane");
  return true;
}

bool MQTTClient::handleZoneCommand(const String& top, const String& msg, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  const String p = topic("cmd/zones/");
  if (!top.startsWith(p)) return false;
  if (!zones) {
    publishCommandAck(commandId, top, false, "moduł zones niedostępny");
    return true;
  }
  const int idx1 = p.length();
  const int idx2 = top.indexOf('/', idx1);
  if (idx2 <= idx1) {
    publishCommandAck(commandId, top, false, "nieprawidłowy topic komendy");
    return true;
  }
  const int id = top.substring(idx1, idx2).toInt();
  const String action = top.substring(idx2 + 1);
  if (id < 0 || id >= zones->getZoneCount()) {
    publishCommandAck(commandId, top, false, "nieprawidłowy identyfikator strefy");
    return true;
  }

  bool ok = false;
  String detail = "nieobsługiwana komenda strefy";
  int startedSecs = 0;
  String source = "";
  if (hasJson && cmdDoc["source"].is<const char*>()) source = String(cmdDoc["source"].as<const char*>());
  source.toLowerCase();
  const bool wasActive = zones->getZoneState(id);

  if (action == "toggle") {
    if (hasJson) {
      const String mode = String(cmdDoc["action"] | "toggle");
      int secs = 600;
      if (cmdDoc["seconds"].is<int>()) secs = cmdDoc["seconds"].as<int>();
      if (mode == "start") {
        if (secs <= 0) secs = 600;
        zones->startZone(id, secs);
        detail = "uruchomiono strefę";
        startedSecs = secs;
        ok = true;
      } else if (mode == "stop") {
        zones->stopZone(id);
        detail = "zatrzymano strefę";
        ok = true;
      } else {
        zones->toggleZone(id);
        detail = "przełączono strefę";
        ok = true;
      }
    } else if (msg == "1" || msg == "ON" || msg == "on" || msg == "true") {
      const int secs = 600;
      zones->startZone(id, secs);
      detail = "uruchomiono strefę";
      startedSecs = secs;
      ok = true;
    } else if (msg.length() == 0) {
      zones->toggleZone(id);
      detail = "przełączono strefę";
      ok = true;
    } else {
      zones->stopZone(id);
      detail = "zatrzymano strefę";
      ok = true;
    }
  } else if (action == "start") {
    int secs = 0;
    if (hasJson) {
      if (cmdDoc["seconds"].is<int>()) secs = cmdDoc["seconds"].as<int>();
      else if (cmdDoc["value"].is<int>()) secs = cmdDoc["value"].as<int>();
    } else {
      parseIntSafe(msg, secs);
    }
    if (secs > 0) {
      zones->startZone(id, secs);
      detail = "uruchomiono strefę";
      startedSecs = secs;
      ok = true;
    } else {
      detail = "nieprawidłowy czas startu";
    }
  } else if (action == "stop") {
    zones->stopZone(id);
    detail = "zatrzymano strefę";
    ok = true;
  }

  if (ok) {
    const bool isActive = zones->getZoneState(id);
    const String zoneObj = zoneObject(id);
    const String sourceCode = source == "smart_climate" ? "WMSC" : EventMessages::sourceCode(source.length() > 0 ? source : "manual_cloud");
    recordZoneEvent(id, wasActive, isActive, sourceCode, zoneObj, startedSecs, detail);
    publishZonesSnapshot();
    publishCommandAck(commandId, top, true, detail);
  } else {
    publishCommandAck(commandId, top, false, detail);
  }
  return true;
}

bool MQTTClient::handleProgramsCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  if (top == topic("cmd/programs/import")) {
    if (!programs) {
      publishCommandAck(commandId, top, false, "moduł programs niedostępny");
      return true;
    }
    JsonDocument payloadDoc;
    bool ok = false;
    if (hasJson && cmdDoc["programs"].is<JsonArray>()) {
      payloadDoc.set(cmdDoc["programs"]);
      ok = true;
    } else if (hasJson && cmdDoc.is<JsonArray>()) {
      payloadDoc.set(cmdDoc.as<JsonArray>());
      ok = true;
    }
    if (!ok) {
      publishCommandAck(commandId, top, false, "brak tablicy programów");
      return true;
    }
    programs->importFromJson(payloadDoc);
    publishProgramsSnapshot();
    publishCommandAck(commandId, top, true, "programy zaimportowane");
    return true;
  }

  if (top == topic("cmd/programs/add")) {
    if (!programs) {
      publishCommandAck(commandId, top, false, "moduł programs niedostępny");
      return true;
    }
    JsonDocument payloadDoc;
    bool ok = false;
    if (hasJson && cmdDoc["program"].is<JsonObject>()) {
      payloadDoc.set(cmdDoc["program"]);
      ok = true;
    } else if (hasJson && !cmdDoc.is<JsonArray>()) {
      payloadDoc.set(cmdDoc);
      payloadDoc.remove("command_id");
      payloadDoc.remove("timestamp");
      ok = true;
    }
    if (!ok) {
      publishCommandAck(commandId, top, false, "nieprawidłowy payload programu");
      return true;
    }
    programs->addFromJson(payloadDoc);
    publishProgramsSnapshot();
    publishCommandAck(commandId, top, true, "program zapisany");
    return true;
  }

  const String pe = topic("cmd/programs/edit/");
  if (top.startsWith(pe)) {
    if (!programs) {
      publishCommandAck(commandId, top, false, "moduł programs niedostępny");
      return true;
    }
    const int id = top.substring(pe.length()).toInt();
    if (id < 0) {
      publishCommandAck(commandId, top, false, "nieprawidłowy identyfikator programu");
      return true;
    }
    JsonDocument payloadDoc;
    if (hasJson && cmdDoc["program"].is<JsonObject>()) {
      payloadDoc.set(cmdDoc["program"]);
    } else if (hasJson) {
      payloadDoc.set(cmdDoc);
      payloadDoc.remove("command_id");
      payloadDoc.remove("timestamp");
    } else {
      publishCommandAck(commandId, top, false, "brak payloadu programu");
      return true;
    }
    const bool edited = programs->edit(id, payloadDoc, true, true);
    if (!edited) {
      publishCommandAck(commandId, top, false, "nie udało się edytować programu");
      return true;
    }
    publishProgramsSnapshot();
    publishCommandAck(commandId, top, true, "program zaktualizowany");
    return true;
  }

  const String pd = topic("cmd/programs/delete/");
  if (!top.startsWith(pd)) return false;
  if (!programs) {
    publishCommandAck(commandId, top, false, "moduł programs niedostępny");
    return true;
  }
  const int id = top.substring(pd.length()).toInt();
  if (id < 0) {
    publishCommandAck(commandId, top, false, "nieprawidłowy identyfikator programu");
    return true;
  }
  const bool removed = programs->remove(id, true);
  if (!removed) {
    publishCommandAck(commandId, top, false, "nie udało się usunąć programu");
    return true;
  }
  publishProgramsSnapshot();
  publishCommandAck(commandId, top, true, "program usunięty");
  return true;
}

bool MQTTClient::handleLogsClearCommand(const String& top, const String& commandId) {
  if (top != topic("cmd/logs/clear")) return false;
  if (!logs) {
    publishCommandAck(commandId, top, false, "moduł logs niedostępny");
    return true;
  }
  logs->clear();
  publishLogsSnapshot();
  publishCommandAck(commandId, top, true, "logi wyczyszczone");
  return true;
}

bool MQTTClient::handleSettingsCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  if (top != topic("cmd/settings/set")) return false;
  if (!config || !weather) {
    publishCommandAck(commandId, top, false, "moduł config/weather niedostępny");
    return true;
  }
  JsonDocument payloadDoc;
  bool ok = false;
  if (hasJson && cmdDoc["settings"].is<JsonObject>()) {
    payloadDoc.set(cmdDoc["settings"]);
    ok = true;
  } else if (hasJson && cmdDoc.is<JsonObject>()) {
    payloadDoc.set(cmdDoc);
    payloadDoc.remove("command_id");
    payloadDoc.remove("timestamp");
    ok = true;
  }
  if (!ok) {
    publishCommandAck(commandId, top, false, "nieprawidłowy payload ustawień");
    return true;
  }

  if (payloadDoc["owmApiKey"].is<const char*>()) {
    const String v = String(payloadDoc["owmApiKey"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("owmApiKey");
  }
  if (payloadDoc["owmLocation"].is<const char*>()) {
    const String v = String(payloadDoc["owmLocation"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("owmLocation");
  }
  if (payloadDoc["mqttPass"].is<const char*>()) {
    const String v = String(payloadDoc["mqttPass"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("mqttPass");
  }
  if (payloadDoc["ssid"].is<const char*>()) {
    const String v = String(payloadDoc["ssid"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("ssid");
  }
  if (payloadDoc["pass"].is<const char*>()) {
    const String v = String(payloadDoc["pass"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("pass");
  }
  if (payloadDoc["password"].is<const char*>()) {
    const String v = String(payloadDoc["password"].as<const char*>());
    if (v.length() == 0) {
      payloadDoc.remove("password");
    } else if (!payloadDoc["pass"].is<const char*>()) {
      payloadDoc["pass"] = v;
      payloadDoc.remove("password");
    } else {
      payloadDoc.remove("password");
    }
  }
  if (payloadDoc["pushoverUser"].is<const char*>()) {
    const String v = String(payloadDoc["pushoverUser"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("pushoverUser");
  }
  if (payloadDoc["pushoverToken"].is<const char*>()) {
    const String v = String(payloadDoc["pushoverToken"].as<const char*>());
    if (v.length() == 0) payloadDoc.remove("pushoverToken");
  }

  config->saveFromJson(payloadDoc);
  if (zones) zones->setZoneCount(config->getZoneCount());
  weather->applySettings(
    config->getOwmApiKey(),
    config->getOwmLocation(),
    config->getEnableWeatherApi(),
    config->getWeatherUpdateIntervalMin()
  );
  weather->applySmartIrrigationConfig(buildSmartIrrigationConfig());
  publishZonesSnapshot();
  publishSettingsPublicSnapshot();
  publishHomeAssistantDiscovery(true);
  publishGlobalStatus(true);
  publishCommandAck(commandId, top, true, "ustawienia zapisane");
  return true;
}

bool MQTTClient::handleOtaCommand(const String& top, const String& commandId, bool hasJson, JsonDocument& cmdDoc) {
  if (top != topic("cmd/ota/start")) return false;
  if (!hasJson || !cmdDoc.is<JsonObject>()) {
    publishCommandAck(commandId, top, false, "nieprawidłowy payload OTA");
    return true;
  }
  if (otaInProgress || ota.pending) {
    publishCommandAck(commandId, top, false, "OTA już w trakcie");
    return true;
  }
  const String url = String(cmdDoc["url"] | "");
  if (url.length() == 0) {
    publishCommandAck(commandId, top, false, "brak URL OTA");
    return true;
  }

  String rawTarget = "firmware";
  if (cmdDoc["target"].is<const char*>()) rawTarget = String(cmdDoc["target"].as<const char*>());
  else if (cmdDoc["kind"].is<const char*>()) rawTarget = String(cmdDoc["kind"].as<const char*>());
  const String target = normalizeOtaTarget(rawTarget);

  String signature = "";
  if (cmdDoc["signature"].is<const char*>()) signature = String(cmdDoc["signature"].as<const char*>());
  else if (cmdDoc["sig"].is<const char*>()) signature = String(cmdDoc["sig"].as<const char*>());
  else if (cmdDoc["signature_ed25519"].is<const char*>()) signature = String(cmdDoc["signature_ed25519"].as<const char*>());

  String signatureAlg = "";
  if (cmdDoc["signature_alg"].is<const char*>()) signatureAlg = String(cmdDoc["signature_alg"].as<const char*>());
  else if (cmdDoc["sig_alg"].is<const char*>()) signatureAlg = String(cmdDoc["sig_alg"].as<const char*>());

  String rawHardware = "";
  if (cmdDoc["hardware"].is<const char*>()) rawHardware = String(cmdDoc["hardware"].as<const char*>());
  else if (cmdDoc["chip"].is<const char*>()) rawHardware = String(cmdDoc["chip"].as<const char*>());
  else if (cmdDoc["platform"].is<const char*>()) rawHardware = String(cmdDoc["platform"].as<const char*>());

  const String otaHardware = normalizeOtaHardware(rawHardware);
  const String localHardware = normalizeOtaHardware(hardwareId());
  if (rawHardware.length() > 0 && otaHardware.length() == 0) {
    publishCommandAck(commandId, top, false, "nieprawidłowy hardware OTA");
    return true;
  }
  if (otaHardware.length() > 0 && otaHardware != localHardware) {
    publishCommandAck(
      commandId,
      top,
      false,
      "niezgodny hardware OTA (fw: " + otaHardwareLabel(otaHardware) + ", urzadzenie: " + otaHardwareLabel(localHardware) + ")"
    );
    return true;
  }

  if (OtaSecurity::requireSignature() && signature.length() == 0) {
    publishCommandAck(commandId, top, false, "brak podpisu Ed25519");
    return true;
  }

  ota.pending = true;
  ota.commandId = commandId;
  ota.commandTopic = top;
  ota.url = url;
  ota.campaignId = String(cmdDoc["campaign_id"] | "");
  ota.firmwareId = String(cmdDoc["firmware_id"] | "");
  ota.version = String(cmdDoc["version"] | "");
  ota.sha256 = String(cmdDoc["sha256"] | "");
  ota.signature = signature;
  ota.signatureAlg = signatureAlg;
  ota.target = target;
  ota.expectedSize = cmdDoc["size"].is<int>() ? cmdDoc["size"].as<int>() : 0;

  publishCommandAck(commandId, top, true, "OTA accepted");
  publishOtaStatus("accepted", 0, "Przyjęto komendę OTA", &ota);
  return true;
}

int MQTTClient::minutesFromSeconds(int secs) const {
  if (secs <= 0) return 0;
  int mins = (secs + 59) / 60;
  return mins < 1 ? 1 : mins;
}

String MQTTClient::buildPlugTelemetry(bool hasVoltage, float voltageV, bool hasCurrent, float currentA, bool hasPower, float powerW, bool hasEnergy, float energyKwh) const {
  String telemetryPart = "";
  if (hasVoltage) telemetryPart += "U=" + String(voltageV, 1) + "V";
  if (hasCurrent) {
    if (telemetryPart.length() > 0) telemetryPart += ", ";
    telemetryPart += "I=" + String(currentA, 2) + "A";
  }
  if (hasPower) {
    if (telemetryPart.length() > 0) telemetryPart += ", ";
    telemetryPart += "P=" + String(powerW, 1) + "W";
  }
  if (hasEnergy) {
    if (telemetryPart.length() > 0) telemetryPart += ", ";
    telemetryPart += "E=" + String(energyKwh, 3) + "kWh";
  }
  return telemetryPart;
}

void MQTTClient::logZoneEvent(const char* category, const char* action, const String& zoneObj, const String& sourceCode, int mins, const String& detail) {
  if (!logs) return;
  (void)sourceCode;
  String sentence;
  if (String(action) == "start") {
    sentence = EventMessages::zoneStarted(zoneObj, mins);
  } else if (String(action) == "stop") {
    sentence = EventMessages::zoneStopped(zoneObj);
  } else {
    sentence = EventMessages::zoneUpdated(zoneObj, mins);
  }
  if (sentence.length()) logs->add(EventMessages::logSentence(category, sentence));
}

void MQTTClient::pushZoneEvent(const char* category, const char* action, const String& zoneObj, const String& sourceCode, int mins) {
  (void)category;
  (void)sourceCode;
  String sentence;
  if (String(action) == "start") {
    sentence = EventMessages::zoneStarted(zoneObj, mins);
  } else if (String(action) == "stop") {
    sentence = EventMessages::zoneStopped(zoneObj);
  } else {
    sentence = EventMessages::zoneUpdated(zoneObj, mins);
  }
  if (sentence.length()) sendPush(EventMessages::pushSentence(sentence));
}

void MQTTClient::recordZoneEvent(int zoneId, bool wasActive, bool isActive, const String& sourceCode, const String& zoneObj, int startedSecs, const String& detail) {
  const char* category = (sourceCode == "WMSC") ? "WMSC" : "STREFA";
  int mins = 0;
  if (isActive) {
    mins = minutesFromSeconds(zones ? zones->getRemainingSeconds(zoneId) : 0);
  }
  if (mins < 1 && startedSecs > 0) mins = minutesFromSeconds(startedSecs);

  if (!wasActive && isActive) {
    logZoneEvent(category, "start", zoneObj, sourceCode, mins, "");
    pushZoneEvent(category, "start", zoneObj, sourceCode, mins);
    return;
  }
  if (wasActive && !isActive) {
    logZoneEvent(category, "stop", zoneObj, sourceCode, 0, "");
    pushZoneEvent(category, "stop", zoneObj, sourceCode, 0);
    return;
  }
  if (isActive) {
    logZoneEvent(category, "aktualizacja", zoneObj, sourceCode, mins, detail);
  }
}

void MQTTClient::recordPlugEvent(const String& actionLabel, const String& plugLabel, const String& modeCode, bool hasState, bool isOn, int secs, const String& telemetryPart) {
  (void)modeCode;
  (void)telemetryPart;
  String sentence = (hasState && !isOn)
    ? EventMessages::plugStopped(plugLabel)
    : EventMessages::plugStarted(plugLabel, secs > 0 ? minutesFromSeconds(secs) : 0);
  if (logs) logs->add(EventMessages::logSentence("GNIAZDKO", sentence));
  sendPush(EventMessages::pushSentence(sentence));
}
