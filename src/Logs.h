#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "EventMessages.h"

class Logs {
  static const int MAX_LOGS = 50;
  String logs[MAX_LOGS];
  int count = 0;

  bool shouldPersist(const String& txt) const {
    // Logujemy tylko zdarzenia operacyjne istotne dla użytkownika.
    static const char* structured[] = {
      "[STREFA]",
      "[HARMONOGRAM]",
      "[WMSC]",
      "[PROGRAM]",
      "[GNIAZDKO]",
      "[OTA]",
      "[SYSTEM]"
    };
    for (const char* prefix : structured) {
      if (txt.startsWith(prefix)) return true;
    }
    return false;
  }

  bool shouldHideForUi(const String& entry) const {
    String text = entry;
    const int sep = text.indexOf(": ");
    if (sep > 0) text = text.substring(sep + 2);
    text.trim();
    if (!text.length()) return false;
    if (text.startsWith("[FAILSAFE]")) return true;
    if (text.startsWith("[MQTT]")) return true;
    return text.startsWith("MQTT");
  }

public:
  void begin() {
    loadFromFS();
  }

  void add(const String& txt) {
    if (!shouldPersist(txt)) return;
    // Spójny format czasu dla logów lokalnych i cloud
    const String ts = getTimestamp();
    String entry = ts.length() ? (ts + ": " + txt) : txt;

    if (count < MAX_LOGS) {
      logs[count++] = entry;
    } else {
      // FIFO – przesunięcie w lewo
      for (int i = 1; i < MAX_LOGS; i++) {
        logs[i - 1] = logs[i];
      }
      logs[MAX_LOGS - 1] = entry;
    }
    saveToFS();
  }

  void clear() {
    count = 0;
    saveToFS();
  }

  void toJson(JsonDocument& doc) { toJson(doc, -1); }

  void toJson(JsonDocument& doc, int limit) {
    JsonArray arr = doc["logs"].to<JsonArray>();
    int added = 0;
    // Newest first for UI readability.
    for (int i = count - 1; i >= 0; i--) {
      if (shouldHideForUi(logs[i])) continue;
      if (limit > 0 && added >= limit) break;
      arr.add(EventMessages::displayMessageText(logs[i]));
      added++;
    }
  }

private:
  String getTimestamp() {
    time_t now = time(nullptr);
    if (now < 100000) return "";
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
    return String(buf);
  }

  void loadFromFS() {
    if (!LittleFS.exists("/logs.json")) {
      count = 0;
      return;
    }
    File f = LittleFS.open("/logs.json", "r");
    if (!f) {
      count = 0;
      return;
    }
    StaticJsonDocument<4096> doc; // powinno wystarczyć dla 30 wpisów
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print("[Logs] Błąd odczytu logs.json: ");
      Serial.println(err.c_str());
      count = 0;
      return;
    }
    count = 0;
    for (JsonVariant v : doc.as<JsonArray>()) {
      if (count >= MAX_LOGS) break;
      logs[count++] = v.as<String>();
    }
  }

  void saveToFS() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
      arr.add(logs[i]);
    }
    File f = LittleFS.open("/logs.json", "w");
    if (!f) {
      Serial.println("[Logs] Nie można otworzyć logs.json do zapisu!");
      return;
    }
    if (serializeJson(doc, f) == 0) {
      Serial.println("[Logs] Błąd zapisu logs.json!");
    }
    f.close();
  }
};
