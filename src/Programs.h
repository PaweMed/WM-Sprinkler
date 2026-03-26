#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>   // ArduinoJson v7: używaj JsonDocument
#include <LittleFS.h>
#include <time.h>
#include "Zones.h"
#include "Weather.h"
#include "Logs.h"
#include "PushoverClient.h"
#include "Config.h"
#include "EventMessages.h"

struct Program {
  uint8_t  zone = 0;
  String   time;         // "HH:MM"
  uint16_t duration = 0; // minuty
  String   days;         // CSV: "0,1,2"
  bool     active = true;
  time_t   lastRun = 0;  // UNIX time ostatniego uruchomienia (persist)
};

class Programs {
  static constexpr int MAX_PROGS = 32;

  Program progs[MAX_PROGS];
  int     numProgs = 0;

  Zones*          zones    = nullptr;
  Weather*        weather  = nullptr;
  Logs*           logs     = nullptr;
  PushoverClient* pushover = nullptr;
  Config*         config   = nullptr; // wskaźnik na Config

  String zoneObject(uint8_t zoneIdx) const {
    const String zoneName = zones ? zones->getZoneName(zoneIdx) : "";
    return EventMessages::zoneObject(zoneName, zoneIdx + 1);
  }

  void sendPush(const EventMessages::PushMessage& msg) {
    if (!pushover || !config || !config->getEnablePushover()) return;
    pushover->send(msg.title, msg.body);
  }

  void logScheduleSkipped(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, float rain24h, float tempC, int humidityPct, float windKmh, const String& reasonCode, const String& reasonText);
  void pushScheduleSkipped(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, float rain24h, float tempC, const String& reasonCode, const String& reasonText);
  void logScheduleStarted(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, int actualDuration, int wateringPercent, float rain24h, float tempC, int humidityPct, float windKmh, const String& explain);
  void pushScheduleStarted(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, int actualDuration, int wateringPercent, float rain24h, float tempC, int humidityPct, float windKmh);
  void logZoneStarted(const String& zoneObj, int actualDuration);

  static bool containsDay(const String& daysStr, int today) {
    int lastPos = 0, pos;
    while ((pos = daysStr.indexOf(',', lastPos)) != -1) {
      int dayNum = daysStr.substring(lastPos, pos).toInt();
      if (dayNum == today) return true;
      lastPos = pos + 1;
    }
    if (lastPos < (int)daysStr.length()) {
      int dayNum = daysStr.substring(lastPos).toInt();
      if (dayNum == today) return true;
    }
    return false;
  }

  static String daysArrayToCsv(JsonArray arr) {
    String d;
    for (auto v : arr) {
      if (d.length() > 0) d += ",";
      d += String((int)v);
    }
    return d;
  }

  static void csvToDaysArray(const String& csv, JsonArray out) {
    int lastPos = 0, pos;
    while ((pos = csv.indexOf(',', lastPos)) != -1) {
      out.add(csv.substring(lastPos, pos).toInt());
      lastPos = pos + 1;
    }
    if (lastPos < (int)csv.length()) out.add(csv.substring(lastPos).toInt());
  }

public:
  void begin(Zones* z, Weather* w, Logs* l, PushoverClient* p, Config* c) {
    zones = z;
    weather = w;
    logs = l;
    pushover = p;
    config = c;
    loadFromFS();
  }

  int size() const { return numProgs; }

  void toJson(JsonDocument& doc) {
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numProgs; i++) {
      JsonObject p = arr.add<JsonObject>();
      p["id"]       = i;
      p["zone"]     = progs[i].zone;
      p["time"]     = progs[i].time;
      p["duration"] = progs[i].duration;
      p["active"]   = progs[i].active;

      JsonArray daysArr = p["days"].to<JsonArray>();
      csvToDaysArray(progs[i].days, daysArr);
    }
  }

  bool edit(int idx, JsonDocument& doc, bool save=true, bool logIt=true) {
    if (idx < 0 || idx >= numProgs) return false;
    Program &P = progs[idx];
    if (doc["zone"].is<uint8_t>())      P.zone = doc["zone"].as<uint8_t>();
    if (doc["time"].is<const char*>())  P.time = doc["time"].as<const char*>();
    if (doc["duration"].is<uint16_t>()) P.duration = doc["duration"].as<uint16_t>();
    if (doc["active"].is<bool>())       P.active = doc["active"].as<bool>();
    if (doc["days"].is<JsonArray>())    P.days = daysArrayToCsv(doc["days"].as<JsonArray>());

    if (save) saveToFS();
    (void)logIt;
    return true;
  }

  bool remove(int idx, bool logIt=true) {
    if (idx < 0 || idx >= numProgs) return false;
    const Program removed = progs[idx];
    for (int i = idx; i < numProgs - 1; i++) progs[i] = progs[i + 1];
    numProgs--;
    saveToFS();
    (void)removed;
    (void)logIt;
    return true;
  }

  void clear() {
    numProgs = 0;
    saveToFS();
  }

  void addFromJson(JsonDocument& doc) {
    if (doc.is<JsonArray>()) {
      importFromJson(doc);
      return;
    }

    int idx = -1;
    if (doc["id"].is<int>())    idx = doc["id"].as<int>();
    if (doc["index"].is<int>()) idx = doc["index"].as<int>();
    if (idx >= 0) {
      edit(idx, doc, true, true);
      return;
    }

    if (numProgs < MAX_PROGS) {
      Program P;
      P.zone     = doc["zone"].as<uint8_t>();
      P.time     = doc["time"].as<const char*>();
      P.duration = doc["duration"].as<uint16_t>();
      P.days     = daysArrayToCsv(doc["days"].as<JsonArray>());
      P.active   = doc["active"].isNull() ? true : doc["active"].as<bool>();
      P.lastRun  = 0;

      progs[numProgs++] = P;
      saveToFS();
    } else {
    }
  }

  void importFromJson(JsonDocument& doc) {
    JsonArray arr = doc.as<JsonArray>();
    numProgs = 0;
    for (auto el : arr) {
      if (numProgs >= MAX_PROGS) break;
      Program P;
      P.zone     = el["zone"]     | 0;
      P.time     = el["time"]     | "06:00";
      P.duration = el["duration"] | 10;
      if (el["days"].is<JsonArray>()) {
        P.days = daysArrayToCsv(el["days"].as<JsonArray>());
      } else {
        P.days = el["days"] | "0,1,2,3,4,5,6";
      }
      P.active   = el["active"].isNull() ? true : el["active"].as<bool>();
      P.lastRun  = 0;
      progs[numProgs++] = P;
    }
    saveToFS();
  }

  void saveToFS() {
    File f = LittleFS.open("/programs.json", "w");
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numProgs; i++) {
      JsonObject p = arr.add<JsonObject>();
      p["zone"]     = progs[i].zone;
      p["time"]     = progs[i].time;
      p["duration"] = progs[i].duration;
      p["days"]     = progs[i].days;
      p["active"]   = progs[i].active;
      p["lastRun"]  = (long)progs[i].lastRun;
    }
    serializeJson(doc, f);
    f.close();
  }

  void loadFromFS() {
    numProgs = 0;
    if (!LittleFS.exists("/programs.json")) {
      // First boot: create an empty programs file to avoid repeated FS open errors.
      File nf = LittleFS.open("/programs.json", "w");
      if (nf) {
        nf.print("[]");
        nf.close();
      }
      return;
    }
    File f = LittleFS.open("/programs.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
      f.close();
      return;
    }
    f.close();
    if (doc.is<JsonArray>()) {
      for (auto el : doc.as<JsonArray>()) {
        if (numProgs >= MAX_PROGS) break;
        Program P;
        P.zone     = el["zone"]     | 0;
        P.time     = el["time"]     | "06:00";
        P.duration = el["duration"] | 10;
        if (el["days"].is<JsonArray>()) {
          P.days = daysArrayToCsv(el["days"].as<JsonArray>());
        } else {
          P.days = el["days"] | "0,1,2,3,4,5,6";
        }
        P.active  = el["active"].isNull() ? true : el["active"].as<bool>();
        if (!el["lastRun"].isNull()) {
          P.lastRun = (time_t)(el["lastRun"].as<long>());
        } else {
          P.lastRun = 0;
        }
        progs[numProgs++] = P;
      }
    }
  }

  void loop() {
    if (!config) return;
    const bool weatherAutoEnabled = config->getAutoMode();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 10000) return;
    lastCheck = millis();

    time_t now = ::time(nullptr);
    struct tm nowTm{};
    localtime_r(&now, &nowTm);

    int today   = nowTm.tm_wday;
    int nowMins = nowTm.tm_hour * 60 + nowTm.tm_min;

    for (int i = 0; i < numProgs; i++) {
      const Program& P = progs[i];
      if (!P.active) continue;

      bool runToday = containsDay(P.days, today);
      if (!runToday) continue;

      int pHour    = atoi(P.time.substring(0, 2).c_str());
      int pMin     = atoi(P.time.substring(3, 5).c_str());
      int progMins = pHour * 60 + pMin;

      int lastYday = -1;
      if (P.lastRun != 0) {
        struct tm lastTm{};
        localtime_r(&P.lastRun, &lastTm);
        lastYday = lastTm.tm_yday;
      }

      // Allow a small grace window so we do not miss a run when loop is delayed.
      const bool inScheduleWindow = (nowMins >= progMins && nowMins <= (progMins + 1));
      if (inScheduleWindow && (P.lastRun == 0 || lastYday != nowTm.tm_yday)) {
        int baseDuration = P.duration;
        int actualDuration = baseDuration;
        Weather::IrrigationDecision decision;
        if (weatherAutoEnabled && weather) {
          decision = weather->getIrrigationDecision();
        } else {
          decision.percent = 100;
          decision.allowed = true;
          decision.hardStop = false;
          decision.hardStopReasonCode = weatherAutoEnabled ? "weather_module_unavailable" : "weather_auto_disabled";
          decision.hardStopReasonText = weatherAutoEnabled ? "Brak modułu pogodowego" : "Automatyka pogodowa wyłączona";
        }
        int wateringPercent = decision.percent;
        const String zoneObj = zoneObject(P.zone);

        // Dane do logów – BIEŻĄCE, zgodnie z logiką decyzji
        float rain24h = decision.rain24hMm;
        float tNow   = decision.tempNowC;
        int   hNow   = (int)decision.humidityNowPercent;
        float wNow   = decision.windNowKmh;
        String explain = weatherAutoEnabled
          ? (weather ? weather->getWateringDecisionExplain() : "AUTO: brak danych pogodowych")
          : "AUTO pogodowe wyłączone -> 100%";

        const bool blockedByDecision = (wateringPercent <= 0 || !decision.allowed || decision.hardStop);
        if (blockedByDecision) {
          String reasonCode = decision.hardStopReasonCode;
          if (reasonCode.isEmpty() || reasonCode == "none") {
            if (wateringPercent <= 0) reasonCode = "watering_percent_zero";
            else if (!decision.allowed) reasonCode = "watering_not_allowed";
            else reasonCode = "blocked";
          }
          String reasonText = decision.hardStopReasonText;
          if (reasonText.isEmpty()) reasonText = explain;
          logScheduleSkipped(zoneObj, weatherAutoEnabled, baseDuration, rain24h, tNow, hNow, wNow, reasonCode, reasonText);
          pushScheduleSkipped(zoneObj, weatherAutoEnabled, baseDuration, rain24h, tNow, reasonCode, reasonText);

          // Oznacz harmonogram jako wykonany (pominięty z powodem), aby nie spamować logów
          // wielokrotnymi próbami w tej samej minucie.
          progs[i].lastRun = now;
          saveToFS();
          continue;
        } else {
          actualDuration = (actualDuration * wateringPercent) / 100;
          if (actualDuration < 1 && baseDuration > 0) actualDuration = 1;
          logScheduleStarted(zoneObj, weatherAutoEnabled, baseDuration, actualDuration, wateringPercent, rain24h, tNow, hNow, wNow, explain);
          pushScheduleStarted(zoneObj, weatherAutoEnabled, baseDuration, actualDuration, wateringPercent, rain24h, tNow, hNow, wNow);
        }

        zones->startZone(P.zone, actualDuration * 60);
        progs[i].lastRun = now;
        saveToFS();

        logZoneStarted(zoneObj, actualDuration);
      }
    }
  }
};

void Programs::logScheduleSkipped(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, float rain24h, float tempC, int humidityPct, float windKmh, const String& reasonCode, const String& reasonText) {
  if (!logs) return;
  (void)weatherAutoEnabled;
  (void)baseDuration;
  (void)rain24h;
  (void)tempC;
  (void)humidityPct;
  (void)windKmh;
  logs->add(EventMessages::logSentence("HARMONOGRAM", EventMessages::zoneCancelled(zoneObj, reasonText.length() ? reasonText : reasonCode)));
}

void Programs::pushScheduleSkipped(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, float rain24h, float tempC, const String& reasonCode, const String& reasonText) {
  (void)weatherAutoEnabled;
  (void)baseDuration;
  (void)rain24h;
  (void)tempC;
  sendPush(EventMessages::pushSentence(EventMessages::zoneCancelled(zoneObj, reasonText.length() ? reasonText : reasonCode)));
}

void Programs::logScheduleStarted(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, int actualDuration, int wateringPercent, float rain24h, float tempC, int humidityPct, float windKmh, const String& explain) {
  if (!logs) return;
  (void)weatherAutoEnabled;
  (void)baseDuration;
  (void)wateringPercent;
  (void)rain24h;
  (void)tempC;
  (void)humidityPct;
  (void)windKmh;
  (void)explain;
  logs->add(EventMessages::logSentence("HARMONOGRAM", EventMessages::zoneStarted(zoneObj, actualDuration)));
}

void Programs::pushScheduleStarted(const String& zoneObj, bool weatherAutoEnabled, int baseDuration, int actualDuration, int wateringPercent, float rain24h, float tempC, int humidityPct, float windKmh) {
  (void)weatherAutoEnabled;
  (void)baseDuration;
  (void)wateringPercent;
  (void)rain24h;
  (void)tempC;
  (void)humidityPct;
  (void)windKmh;
  sendPush(EventMessages::pushSentence(EventMessages::zoneStarted(zoneObj, actualDuration)));
}

void Programs::logZoneStarted(const String& zoneObj, int actualDuration) {
  if (!logs) return;
  logs->add(EventMessages::logSentence("HARMONOGRAM", EventMessages::zoneStarted(zoneObj, actualDuration)));
}
