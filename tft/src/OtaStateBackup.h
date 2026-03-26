#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>

#include "Config.h"

class OtaStateBackup {
  struct FileSpec {
    const char* path;
    const char* key;
    const char* presentKey;
  };

  static constexpr const char* kNamespace = "ota_state";
  static constexpr const char* kPendingKey = "pending";
  static constexpr const char* kTargetKey = "target";
  static constexpr const char* kVersionKey = "ver";
  static constexpr const char* kTimestampKey = "ts";
  static constexpr const char* kConfigKey = "cfg";

  static const FileSpec* fileSpecs() {
    static const FileSpec specs[] = {
      { "/programs.json",        "prog",  "has_prog"  },
      { "/zones-names.json",     "zones", "has_zones" },
      { "/device-identity.json", "devid", "has_devid" },
      { "/fw-version.json",      "fwver", "has_fwver" },
      { "/rain-history.json",    "rain",  "has_rain"  },
    };
    return specs;
  }

  static constexpr size_t fileSpecCount() {
    return 5;
  }

  static String timestampNow() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    snprintf(
      buf,
      sizeof(buf),
      "%04d-%02d-%02d %02d:%02d:%02d",
      t.tm_year + 1900,
      t.tm_mon + 1,
      t.tm_mday,
      t.tm_hour,
      t.tm_min,
      t.tm_sec
    );
    return String(buf);
  }

  static bool readFsFile(const char* path, String& out, String& errOut) {
    out = "";
    if (!LittleFS.exists(path)) return true;

    File f = LittleFS.open(path, "r");
    if (!f) {
      errOut = String("cannot open ") + path;
      return false;
    }

    out.reserve((size_t)f.size());
    while (f.available()) {
      const int c = f.read();
      if (c < 0) break;
      out += char(c);
    }
    f.close();
    return true;
  }

  static bool writeFsFile(const char* path, const String& content, String& errOut) {
    File f = LittleFS.open(path, "w");
    if (!f) {
      errOut = String("cannot write ") + path;
      return false;
    }

    const size_t len = content.length();
    if (len > 0 && f.write((const uint8_t*)content.c_str(), len) != len) {
      f.close();
      errOut = String("short write ") + path;
      return false;
    }

    f.flush();
    f.close();
    return true;
  }

  static bool putStringChecked(Preferences& prefs, const char* key, const String& value, String& errOut) {
    const size_t written = prefs.putString(key, value);
    if (written == 0 && value.length() > 0) {
      errOut = String("prefs write failed: ") + key;
      return false;
    }
    return true;
  }

public:
  static void clear() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    prefs.clear();
    prefs.end();
  }

  static bool stage(Config* config, const String& target, const String& version, String& errOut) {
    if (!config) {
      errOut = "config missing";
      return false;
    }

    JsonDocument cfgDoc;
    config->toJson(cfgDoc);
    if (WiFi.status() == WL_CONNECTED) {
      const String liveSsid = WiFi.SSID();
      const String livePass = WiFi.psk();
      if (liveSsid.length() > 0) cfgDoc["ssid"] = liveSsid;
      if (livePass.length() > 0) cfgDoc["pass"] = livePass;
    }

    String cfgJson;
    serializeJson(cfgDoc, cfgJson);
    if (cfgJson.length() == 0) {
      errOut = "cannot serialize config backup";
      return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
      errOut = "prefs begin failed";
      return false;
    }

    prefs.clear();

    if (!putStringChecked(prefs, kConfigKey, cfgJson, errOut)) {
      prefs.clear();
      prefs.end();
      return false;
    }
    if (!putStringChecked(prefs, kTargetKey, target, errOut)) {
      prefs.clear();
      prefs.end();
      return false;
    }
    if (!putStringChecked(prefs, kVersionKey, version, errOut)) {
      prefs.clear();
      prefs.end();
      return false;
    }
    if (!putStringChecked(prefs, kTimestampKey, timestampNow(), errOut)) {
      prefs.clear();
      prefs.end();
      return false;
    }

    const FileSpec* specs = fileSpecs();
    for (size_t i = 0; i < fileSpecCount(); ++i) {
      const bool exists = LittleFS.exists(specs[i].path);
      prefs.putBool(specs[i].presentKey, exists);
      if (!exists) {
        prefs.remove(specs[i].key);
        continue;
      }

      String data;
      if (!readFsFile(specs[i].path, data, errOut)) {
        prefs.clear();
        prefs.end();
        return false;
      }
      if (!putStringChecked(prefs, specs[i].key, data, errOut)) {
        prefs.clear();
        prefs.end();
        return false;
      }
    }

    prefs.putBool(kPendingKey, true);
    prefs.end();
    return true;
  }

  static bool restoreIfPending(Config* config, String& detailOut) {
    detailOut = "";
    if (!config) {
      detailOut = "config missing";
      return false;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
      detailOut = "prefs begin failed";
      return false;
    }

    if (!prefs.getBool(kPendingKey, false)) {
      prefs.end();
      return true;
    }

    bool restoredAnything = false;

    const String cfgJson = prefs.getString(kConfigKey, "");
    if (cfgJson.length() > 0) {
      JsonDocument cfgDoc;
      DeserializationError err = deserializeJson(cfgDoc, cfgJson);
      if (err || !cfgDoc.is<JsonObject>()) {
        detailOut = String("invalid config backup: ") + err.c_str();
        prefs.end();
        return false;
      }
      config->saveFromJson(cfgDoc);
      restoredAnything = true;
    }

    const FileSpec* specs = fileSpecs();
    for (size_t i = 0; i < fileSpecCount(); ++i) {
      if (!prefs.getBool(specs[i].presentKey, false)) continue;
      String data = prefs.getString(specs[i].key, "");
      if (!writeFsFile(specs[i].path, data, detailOut)) {
        prefs.end();
        return false;
      }
      restoredAnything = true;
    }

    prefs.clear();
    prefs.end();

    detailOut = restoredAnything ? "restored" : "nothing to restore";
    return true;
  }
};
