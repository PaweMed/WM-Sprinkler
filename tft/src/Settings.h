#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "CloudDefaults.h"

class Settings {
  Preferences prefs;
  static int normalizeZoneCount(int count) {
    if (count < 1) return 1;
    if (count > 8) return 8;
    return count;
  }

  static String normalizeTimezoneValue(String tz) {
    tz.trim();
    if (!tz.length()) return "Europe/Warsaw";

    // Legacy values from the old UI stored only raw UTC offsets.
    // For zones that observe DST, migrate them to a real IANA zone so DST
    // can be applied automatically.
    if (tz == "UTC") return "Etc/UTC";
    if (tz == "+00:00") return "Europe/London";
    if (tz == "+01:00") return "Europe/Berlin";
    if (tz == "+02:00") return "Europe/Warsaw";
    if (tz == "-09:00") return "America/Anchorage";
    if (tz == "-08:00") return "America/Los_Angeles";
    if (tz == "-07:00") return "America/Denver";
    if (tz == "-06:00") return "America/Chicago";
    if (tz == "-05:00") return "America/New_York";
    if (tz == "-02:00") return "Atlantic/Azores";
    if (tz == "+06:00") return "Asia/Almaty";
    if (tz == "+09:00") return "Asia/Tokyo";
    if (tz == "+10:00") return "Australia/Sydney";
    if (tz == "+12:00") return "Pacific/Auckland";

    // Keep canonical UTC spelling.
    if (tz == "Etc/GMT" || tz == "Etc/UTC") return "Etc/UTC";
    return tz;
  }

  static String timezoneLegacyApiValue(const String& tz) {
    if (tz == "Europe/London") return "+00:00";
    if (tz == "Europe/Berlin") return "+01:00";
    if (tz == "Europe/Warsaw") return "+02:00";
    if (tz == "Europe/Helsinki") return "+02:00";
    if (tz == "America/Anchorage") return "-09:00";
    if (tz == "America/Los_Angeles") return "-08:00";
    if (tz == "America/Denver") return "-07:00";
    if (tz == "America/Chicago") return "-06:00";
    if (tz == "America/New_York") return "-05:00";
    if (tz == "Atlantic/Azores") return "-02:00";
    if (tz == "Asia/Almaty") return "+06:00";
    if (tz == "Asia/Tokyo") return "+09:00";
    if (tz == "Australia/Sydney") return "+10:00";
    if (tz == "Pacific/Auckland") return "+12:00";
    if (tz == "Etc/UTC") return "UTC";
    return tz;
  }

  // WiFi
  String ssid, pass;

  // OpenWeatherMap
  String owmApiKey, owmLocation;

  // Pushover
  String pushoverUser, pushoverToken;
  bool   enablePushover = true;

  // MQTT
  String mqttServer, mqttUser, mqttPass, mqttClientId;
  int    mqttPort = 1883;
  bool   enableMqtt = true;
  String mqttTopicBase = "sprinkler"; // baza topiców

  // Automatyka
  bool   autoMode = true;
  int    zoneCount = 8;

  // Strefa czasowa
  String timezone = "Europe/Warsaw";

  // Pogoda – sterowanie
  bool   enableWeatherApi = true;
  int    weatherUpdateIntervalMin = 60; // minuty

  // Smart irrigation v2 - hard stop + mnożniki
  float irrigationTempSkipC = 5.0f;
  float irrigationTempLowMaxC = 18.0f;
  float irrigationTempMidMaxC = 25.0f;
  float irrigationTempHighMaxC = 30.0f;
  float irrigationTempFactorLow = 0.5f;
  float irrigationTempFactorMid = 1.0f;
  float irrigationTempFactorHigh = 1.3f;
  float irrigationTempFactorVeryHigh = 1.5f;
  float irrigationRainSkipMm = 6.0f;
  float irrigationRainHighMinMm = 5.0f;
  float irrigationRainMidMinMm = 2.0f;
  float irrigationRainFactorHigh = 0.3f;
  float irrigationRainFactorMid = 0.6f;
  float irrigationRainFactorLow = 1.0f;
  float irrigationHumidityHighPercent = 90.0f;
  float irrigationHumidityFactorHigh = 0.75f;
  float irrigationWindSkipKmh = 25.0f;
  float irrigationWindFactor = 1.0f;
  int   irrigationPercentMin = 0;
  int   irrigationPercentMax = 160;

  static float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
  }

  static int clampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
  }

  void normalizeIrrigationRules() {
    irrigationTempSkipC = clampFloat(irrigationTempSkipC, -20.0f, 40.0f);
    irrigationTempLowMaxC = clampFloat(irrigationTempLowMaxC, -20.0f, 50.0f);
    irrigationTempMidMaxC = clampFloat(irrigationTempMidMaxC, -20.0f, 60.0f);
    irrigationTempHighMaxC = clampFloat(irrigationTempHighMaxC, -20.0f, 70.0f);

    if (irrigationTempLowMaxC < irrigationTempSkipC) irrigationTempLowMaxC = irrigationTempSkipC;
    if (irrigationTempMidMaxC < irrigationTempLowMaxC) irrigationTempMidMaxC = irrigationTempLowMaxC;
    if (irrigationTempHighMaxC < irrigationTempMidMaxC) irrigationTempHighMaxC = irrigationTempMidMaxC;

    irrigationTempFactorLow = clampFloat(irrigationTempFactorLow, 0.0f, 3.0f);
    irrigationTempFactorMid = clampFloat(irrigationTempFactorMid, 0.0f, 3.0f);
    irrigationTempFactorHigh = clampFloat(irrigationTempFactorHigh, 0.0f, 3.0f);
    irrigationTempFactorVeryHigh = clampFloat(irrigationTempFactorVeryHigh, 0.0f, 3.0f);

    irrigationRainMidMinMm = clampFloat(irrigationRainMidMinMm, 0.0f, 100.0f);
    irrigationRainHighMinMm = clampFloat(irrigationRainHighMinMm, 0.0f, 100.0f);
    if (irrigationRainHighMinMm < irrigationRainMidMinMm) irrigationRainHighMinMm = irrigationRainMidMinMm;

    irrigationRainSkipMm = clampFloat(irrigationRainSkipMm, 0.0f, 200.0f);
    if (irrigationRainSkipMm < irrigationRainHighMinMm) irrigationRainSkipMm = irrigationRainHighMinMm;

    irrigationRainFactorHigh = clampFloat(irrigationRainFactorHigh, 0.0f, 2.0f);
    irrigationRainFactorMid = clampFloat(irrigationRainFactorMid, 0.0f, 2.0f);
    irrigationRainFactorLow = clampFloat(irrigationRainFactorLow, 0.0f, 2.0f);

    irrigationHumidityHighPercent = clampFloat(irrigationHumidityHighPercent, 0.0f, 100.0f);
    irrigationHumidityFactorHigh = clampFloat(irrigationHumidityFactorHigh, 0.0f, 2.0f);

    irrigationWindSkipKmh = clampFloat(irrigationWindSkipKmh, 0.0f, 200.0f);
    irrigationWindFactor = clampFloat(irrigationWindFactor, 0.0f, 2.0f);

    irrigationPercentMin = clampInt(irrigationPercentMin, 0, 300);
    irrigationPercentMax = clampInt(irrigationPercentMax, 0, 300);
    if (irrigationPercentMax < irrigationPercentMin) irrigationPercentMax = irrigationPercentMin;
  }

public:  // GETTERY (używane przez Config/MQTT/Weather/itp.)
  String getSSID() { return ssid; }
  String getPass() { return pass; }

  String getOwmApiKey() { return owmApiKey; }
  String getOwmLocation() { return owmLocation; }

  String getPushoverUser() { return pushoverUser; }
  String getPushoverToken() { return pushoverToken; }
  bool   getEnablePushover() { return enablePushover; }

  String getMqttServer() { return mqttServer; }
  int    getMqttPort()   { return mqttPort; }
  String getMqttUser()   { return mqttUser; }
  String getMqttPass()   { return mqttPass; }
  String getMqttClientId() { return mqttClientId; }
  bool   getEnableMqtt() { return enableMqtt; }
  String getMqttTopicBase() { return mqttTopicBase; }

  bool   getAutoMode() { return autoMode; }
  int    getZoneCount() { return zoneCount; }

  String getTimezone() { return timezone; }
  void   setTimezone(const String& tz) { timezone = tz; }

  bool   getEnableWeatherApi() { return enableWeatherApi; }
  int    getWeatherUpdateIntervalMin() { return weatherUpdateIntervalMin; }
  float  getIrrigationTempSkipC() { return irrigationTempSkipC; }
  float  getIrrigationTempLowMaxC() { return irrigationTempLowMaxC; }
  float  getIrrigationTempMidMaxC() { return irrigationTempMidMaxC; }
  float  getIrrigationTempHighMaxC() { return irrigationTempHighMaxC; }
  float  getIrrigationTempFactorLow() { return irrigationTempFactorLow; }
  float  getIrrigationTempFactorMid() { return irrigationTempFactorMid; }
  float  getIrrigationTempFactorHigh() { return irrigationTempFactorHigh; }
  float  getIrrigationTempFactorVeryHigh() { return irrigationTempFactorVeryHigh; }
  float  getIrrigationRainSkipMm() { return irrigationRainSkipMm; }
  float  getIrrigationRainHighMinMm() { return irrigationRainHighMinMm; }
  float  getIrrigationRainMidMinMm() { return irrigationRainMidMinMm; }
  float  getIrrigationRainFactorHigh() { return irrigationRainFactorHigh; }
  float  getIrrigationRainFactorMid() { return irrigationRainFactorMid; }
  float  getIrrigationRainFactorLow() { return irrigationRainFactorLow; }
  float  getIrrigationHumidityHighPercent() { return irrigationHumidityHighPercent; }
  float  getIrrigationHumidityFactorHigh() { return irrigationHumidityFactorHigh; }
  float  getIrrigationWindSkipKmh() { return irrigationWindSkipKmh; }
  float  getIrrigationWindFactor() { return irrigationWindFactor; }
  int    getIrrigationPercentMin() { return irrigationPercentMin; }
  int    getIrrigationPercentMax() { return irrigationPercentMax; }

  // --- LOAD/SAVE ---
  void load() {
    prefs.begin("ews", false);
    auto getStringSafe = [this](const char* key, const char* fallback) -> String {
      return prefs.isKey(key) ? prefs.getString(key, fallback) : String(fallback);
    };
    auto getBoolSafe = [this](const char* key, bool fallback) -> bool {
      return prefs.isKey(key) ? prefs.getBool(key, fallback) : fallback;
    };
    auto getIntSafe = [this](const char* key, int fallback) -> int {
      return prefs.isKey(key) ? prefs.getInt(key, fallback) : fallback;
    };
    auto getFloatSafe = [this](const char* key, float fallback) -> float {
      return prefs.isKey(key) ? prefs.getFloat(key, fallback) : fallback;
    };

    ssid = getStringSafe("ssid", "");
    pass = getStringSafe("pass", "");

    owmApiKey   = getStringSafe("owmApiKey", "");
    owmLocation = getStringSafe("owmLocation", "Szczecin,PL");

    pushoverUser   = getStringSafe("pushoverUser", "");
    pushoverToken  = getStringSafe("pushoverToken", "");
    enablePushover = getBoolSafe("enablePushover", true);

    mqttServer    = getStringSafe("mqttServer", WMS_DEFAULT_MQTT_SERVER);
    mqttUser      = getStringSafe("mqttUser", WMS_DEFAULT_MQTT_USER);
    mqttPass      = getStringSafe("mqttPass", WMS_DEFAULT_MQTT_PASS);
    mqttClientId  = getStringSafe("mqttClientId", "");
    mqttPort      = getIntSafe("mqttPort", WMS_DEFAULT_MQTT_PORT);
    enableMqtt    = getBoolSafe("enableMqtt", true);
    mqttTopicBase = getStringSafe("mqttTopicBase", "");

    autoMode  = getBoolSafe("autoMode", true);
    zoneCount = normalizeZoneCount(getIntSafe("zoneCount", 8));

    timezone  = normalizeTimezoneValue(getStringSafe("timezone", "Europe/Warsaw"));

    // NVS key names in Preferences should stay short (<=15 chars on ESP32).
    enableWeatherApi          = getBoolSafe("enWeatherApi", true);
    weatherUpdateIntervalMin  = getIntSafe("weatherUpdMin", 60);
    irrigationTempSkipC = getFloatSafe("irTmpSkip", 5.0f);
    // Migracja progu hard-stop temperatury: 10°C -> 5°C.
    if (irrigationTempSkipC > 9.999f && irrigationTempSkipC < 10.001f) {
      irrigationTempSkipC = 5.0f;
      prefs.putFloat("irTmpSkip", irrigationTempSkipC);
      Serial.println("[Settings] Migracja: irTmpSkip 10.0C -> 5.0C");
    }
    irrigationTempLowMaxC = getFloatSafe("irTmpLowMx", 18.0f);
    irrigationTempMidMaxC = getFloatSafe("irTmpMidMx", 25.0f);
    irrigationTempHighMaxC = getFloatSafe("irTmpHiMx", 30.0f);
    irrigationTempFactorLow = getFloatSafe("irTmpLowF", 0.5f);
    irrigationTempFactorMid = getFloatSafe("irTmpMidF", 1.0f);
    irrigationTempFactorHigh = getFloatSafe("irTmpHiF", 1.3f);
    irrigationTempFactorVeryHigh = getFloatSafe("irTmpVHiF", 1.5f);
    irrigationRainSkipMm = getFloatSafe("irRainSkp", 6.0f);
    irrigationRainHighMinMm = getFloatSafe("irRainHiMn", 5.0f);
    irrigationRainMidMinMm = getFloatSafe("irRainMdMn", 2.0f);
    irrigationRainFactorHigh = getFloatSafe("irRainHiF", 0.3f);
    irrigationRainFactorMid = getFloatSafe("irRainMdF", 0.6f);
    irrigationRainFactorLow = getFloatSafe("irRainLoF", 1.0f);
    irrigationHumidityHighPercent = getFloatSafe("irHumHi", 90.0f);
    irrigationHumidityFactorHigh = getFloatSafe("irHumHiF", 0.75f);
    irrigationWindSkipKmh = getFloatSafe("irWndSkp", 25.0f);
    irrigationWindFactor = getFloatSafe("irWndFac", 1.0f);
    irrigationPercentMin = getIntSafe("irPctMin", 0);
    irrigationPercentMax = getIntSafe("irPctMax", 160);
    normalizeIrrigationRules();
    prefs.end();
  }

  void ensureMqttDefaults(const String& deviceId) {
    prefs.begin("ews", false);
    bool changed = false;

    if (mqttServer.length() == 0) {
      mqttServer = WMS_DEFAULT_MQTT_SERVER;
      prefs.putString("mqttServer", mqttServer);
      changed = true;
    }
    if (mqttPort <= 0) {
      mqttPort = WMS_DEFAULT_MQTT_PORT;
      prefs.putInt("mqttPort", mqttPort);
      changed = true;
    }
    // Migration fix: older configs used 1883, but cloud broker for this project is TLS on 8883.
    if (mqttServer == WMS_DEFAULT_MQTT_SERVER && mqttPort != WMS_DEFAULT_MQTT_PORT) {
      mqttPort = WMS_DEFAULT_MQTT_PORT;
      prefs.putInt("mqttPort", mqttPort);
      changed = true;
    }
    if (mqttUser.length() == 0) {
      mqttUser = WMS_DEFAULT_MQTT_USER;
      prefs.putString("mqttUser", mqttUser);
      changed = true;
    }
    if (mqttPass.length() == 0) {
      mqttPass = WMS_DEFAULT_MQTT_PASS;
      prefs.putString("mqttPass", mqttPass);
      changed = true;
    }
    if (mqttClientId.length() == 0) {
      mqttClientId = deviceId;
      prefs.putString("mqttClientId", mqttClientId);
      changed = true;
    }
    const String desiredTopic = String(WMS_DEFAULT_MQTT_PREFIX) + "/" + deviceId;
    if (mqttTopicBase.length() == 0 || mqttTopicBase == "sprinkler") {
      mqttTopicBase = desiredTopic;
      prefs.putString("mqttTopicBase", mqttTopicBase);
      changed = true;
    }
    if (!enableMqtt) {
      enableMqtt = true;
      prefs.putBool("enableMqtt", true);
      changed = true;
    }

    prefs.end();
    if (changed) {
      Serial.println("[Settings] Zastosowano domyślne ustawienia MQTT cloud.");
    }
  }

  void saveFromJson(JsonDocument& doc) {
    prefs.begin("ews", false);

    // WiFi
    if (doc["ssid"].is<const char*>()) {
      const String nextSsid = String(doc["ssid"].as<const char*>());
      if (nextSsid.length() > 0) {
        ssid = nextSsid;
        prefs.putString("ssid", ssid);
      } else {
        Serial.println("[Settings] Ignoring empty WiFi SSID update.");
      }
    }
    if (doc["pass"].is<const char*>()) {
      const String nextPass = String(doc["pass"].as<const char*>());
      if (nextPass.length() > 0) {
        pass = nextPass;
        prefs.putString("pass", pass);
      } else {
        Serial.println("[Settings] Ignoring empty WiFi password update.");
      }
    } else if (doc["password"].is<const char*>()) {
      const String nextPass = String(doc["password"].as<const char*>());
      if (nextPass.length() > 0) {
        pass = nextPass;
        prefs.putString("pass", pass);
      } else {
        Serial.println("[Settings] Ignoring empty WiFi password alias update.");
      }
    }

    // OWM
    if (doc["owmApiKey"].is<const char*>())   { owmApiKey = doc["owmApiKey"].as<const char*>(); prefs.putString("owmApiKey", owmApiKey); }
    if (doc["owmLocation"].is<const char*>()) { owmLocation = doc["owmLocation"].as<const char*>(); prefs.putString("owmLocation", owmLocation); }

    // Pushover
    if (doc["pushoverUser"].is<const char*>())  { pushoverUser = doc["pushoverUser"].as<const char*>(); prefs.putString("pushoverUser", pushoverUser); }
    if (doc["pushoverToken"].is<const char*>()) { pushoverToken = doc["pushoverToken"].as<const char*>(); prefs.putString("pushoverToken", pushoverToken); }
    if (doc["enablePushover"].is<bool>())       { enablePushover = doc["enablePushover"].as<bool>(); prefs.putBool("enablePushover", enablePushover); }

    // MQTT
    if (doc["mqttServer"].is<const char*>())   { mqttServer = doc["mqttServer"].as<const char*>(); prefs.putString("mqttServer", mqttServer); }
    if (doc["mqttUser"].is<const char*>())     { mqttUser = doc["mqttUser"].as<const char*>(); prefs.putString("mqttUser", mqttUser); }
    if (doc["mqttPass"].is<const char*>())     { mqttPass = doc["mqttPass"].as<const char*>(); prefs.putString("mqttPass", mqttPass); }
    if (doc["mqttClientId"].is<const char*>()) { mqttClientId = doc["mqttClientId"].as<const char*>(); prefs.putString("mqttClientId", mqttClientId); }
    if (doc["mqttPort"].is<int>())             { mqttPort = doc["mqttPort"].as<int>(); prefs.putInt("mqttPort", mqttPort); }
    if (doc["enableMqtt"].is<bool>())          { enableMqtt = doc["enableMqtt"].as<bool>(); prefs.putBool("enableMqtt", enableMqtt); }
    if (doc["mqttTopic"].is<const char*>())    { mqttTopicBase = doc["mqttTopic"].as<const char*>(); prefs.putString("mqttTopicBase", mqttTopicBase); }

    // Automatyka
    if (doc["autoMode"].is<bool>()) { autoMode = doc["autoMode"].as<bool>(); prefs.putBool("autoMode", autoMode); }
    if (doc["zoneCount"].is<int>()) {
      zoneCount = normalizeZoneCount(doc["zoneCount"].as<int>());
      prefs.putInt("zoneCount", zoneCount);
    }

    // Strefa czasowa
    if (doc["timezone"].is<const char*>()) {
      timezone = normalizeTimezoneValue(doc["timezone"].as<const char*>());
      prefs.putString("timezone", timezone);
    }

    // Pogoda
    if (doc["enableWeatherApi"].is<bool>()) { enableWeatherApi = doc["enableWeatherApi"].as<bool>(); prefs.putBool("enWeatherApi", enableWeatherApi); }
    if (doc["weatherUpdateInterval"].is<int>()) {
      weatherUpdateIntervalMin = doc["weatherUpdateInterval"].as<int>();
      if (weatherUpdateIntervalMin < 5) weatherUpdateIntervalMin = 5; // minimalne 5 min
      prefs.putInt("weatherUpdMin", weatherUpdateIntervalMin);
    }

    // Smart irrigation v2
    bool irrigationChanged = false;
    auto applyFloatSetting = [&](const char* jsonKey, float& field) {
      if (doc[jsonKey].is<float>() || doc[jsonKey].is<int>()) {
        field = doc[jsonKey].as<float>();
        irrigationChanged = true;
      }
    };
    auto applyIntSetting = [&](const char* jsonKey, int& field) {
      if (doc[jsonKey].is<int>()) {
        field = doc[jsonKey].as<int>();
        irrigationChanged = true;
      }
    };

    applyFloatSetting("irrigationTempSkipC", irrigationTempSkipC);
    applyFloatSetting("irrigationTempLowMaxC", irrigationTempLowMaxC);
    applyFloatSetting("irrigationTempMidMaxC", irrigationTempMidMaxC);
    applyFloatSetting("irrigationTempHighMaxC", irrigationTempHighMaxC);
    applyFloatSetting("irrigationTempFactorLow", irrigationTempFactorLow);
    applyFloatSetting("irrigationTempFactorMid", irrigationTempFactorMid);
    applyFloatSetting("irrigationTempFactorHigh", irrigationTempFactorHigh);
    applyFloatSetting("irrigationTempFactorVeryHigh", irrigationTempFactorVeryHigh);
    applyFloatSetting("irrigationRainSkipMm", irrigationRainSkipMm);
    applyFloatSetting("irrigationRainHighMinMm", irrigationRainHighMinMm);
    applyFloatSetting("irrigationRainMidMinMm", irrigationRainMidMinMm);
    applyFloatSetting("irrigationRainFactorHigh", irrigationRainFactorHigh);
    applyFloatSetting("irrigationRainFactorMid", irrigationRainFactorMid);
    applyFloatSetting("irrigationRainFactorLow", irrigationRainFactorLow);
    applyFloatSetting("irrigationHumidityHighPercent", irrigationHumidityHighPercent);
    applyFloatSetting("irrigationHumidityFactorHigh", irrigationHumidityFactorHigh);
    applyFloatSetting("irrigationWindSkipKmh", irrigationWindSkipKmh);
    applyFloatSetting("irrigationWindFactor", irrigationWindFactor);
    applyIntSetting("irrigationPercentMin", irrigationPercentMin);
    applyIntSetting("irrigationPercentMax", irrigationPercentMax);

    if (irrigationChanged) {
      normalizeIrrigationRules();
      prefs.putFloat("irTmpSkip", irrigationTempSkipC);
      prefs.putFloat("irTmpLowMx", irrigationTempLowMaxC);
      prefs.putFloat("irTmpMidMx", irrigationTempMidMaxC);
      prefs.putFloat("irTmpHiMx", irrigationTempHighMaxC);
      prefs.putFloat("irTmpLowF", irrigationTempFactorLow);
      prefs.putFloat("irTmpMidF", irrigationTempFactorMid);
      prefs.putFloat("irTmpHiF", irrigationTempFactorHigh);
      prefs.putFloat("irTmpVHiF", irrigationTempFactorVeryHigh);
      prefs.putFloat("irRainSkp", irrigationRainSkipMm);
      prefs.putFloat("irRainHiMn", irrigationRainHighMinMm);
      prefs.putFloat("irRainMdMn", irrigationRainMidMinMm);
      prefs.putFloat("irRainHiF", irrigationRainFactorHigh);
      prefs.putFloat("irRainMdF", irrigationRainFactorMid);
      prefs.putFloat("irRainLoF", irrigationRainFactorLow);
      prefs.putFloat("irHumHi", irrigationHumidityHighPercent);
      prefs.putFloat("irHumHiF", irrigationHumidityFactorHigh);
      prefs.putFloat("irWndSkp", irrigationWindSkipKmh);
      prefs.putFloat("irWndFac", irrigationWindFactor);
      prefs.putInt("irPctMin", irrigationPercentMin);
      prefs.putInt("irPctMax", irrigationPercentMax);
    }

    prefs.end();
  }

  void toJson(JsonDocument& doc) {
    // WiFi
    doc["ssid"] = ssid; doc["pass"] = pass;

    // OWM
    doc["owmApiKey"]   = owmApiKey;
    doc["owmLocation"] = owmLocation;

    // Pushover
    doc["pushoverUser"]   = pushoverUser;
    doc["pushoverToken"]  = pushoverToken;
    doc["enablePushover"] = enablePushover;

    // MQTT
    doc["mqttServer"]    = mqttServer;
    doc["mqttUser"]      = mqttUser;
    doc["mqttPass"]      = mqttPass;
    doc["mqttClientId"]  = mqttClientId;
    doc["mqttPort"]      = mqttPort;
    doc["enableMqtt"]    = enableMqtt;
    doc["mqttTopic"]     = mqttTopicBase;

    // Automatyka
    doc["autoMode"] = autoMode;
    doc["zoneCount"] = zoneCount;

    // Strefa czasowa
    doc["timezone"] = timezoneLegacyApiValue(timezone);
    doc["timezoneCanonical"] = timezone;

    // Pogoda
    doc["enableWeatherApi"]      = enableWeatherApi;
    doc["weatherUpdateInterval"] = weatherUpdateIntervalMin;

    // Smart irrigation v2
    doc["irrigationTempSkipC"] = irrigationTempSkipC;
    doc["irrigationTempLowMaxC"] = irrigationTempLowMaxC;
    doc["irrigationTempMidMaxC"] = irrigationTempMidMaxC;
    doc["irrigationTempHighMaxC"] = irrigationTempHighMaxC;
    doc["irrigationTempFactorLow"] = irrigationTempFactorLow;
    doc["irrigationTempFactorMid"] = irrigationTempFactorMid;
    doc["irrigationTempFactorHigh"] = irrigationTempFactorHigh;
    doc["irrigationTempFactorVeryHigh"] = irrigationTempFactorVeryHigh;
    doc["irrigationRainSkipMm"] = irrigationRainSkipMm;
    doc["irrigationRainHighMinMm"] = irrigationRainHighMinMm;
    doc["irrigationRainMidMinMm"] = irrigationRainMidMinMm;
    doc["irrigationRainFactorHigh"] = irrigationRainFactorHigh;
    doc["irrigationRainFactorMid"] = irrigationRainFactorMid;
    doc["irrigationRainFactorLow"] = irrigationRainFactorLow;
    doc["irrigationHumidityHighPercent"] = irrigationHumidityHighPercent;
    doc["irrigationHumidityFactorHigh"] = irrigationHumidityFactorHigh;
    doc["irrigationWindSkipKmh"] = irrigationWindSkipKmh;
    doc["irrigationWindFactor"] = irrigationWindFactor;
    doc["irrigationPercentMin"] = irrigationPercentMin;
    doc["irrigationPercentMax"] = irrigationPercentMax;
  }
};
