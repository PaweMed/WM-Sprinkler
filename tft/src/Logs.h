#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include "EventMessages.h"

class Logs {
  static const int MAX_LOGS = 50;
  String logs[MAX_LOGS];
  String timestamps[MAX_LOGS];
  uint32_t addedAtMs_[MAX_LOGS] = {};
  bool pendingBackfill_[MAX_LOGS] = {};
  int count = 0;
  uint32_t revision_ = 1;
  mutable bool visibleCacheDirty_ = true;
  mutable int visibleCacheCount_ = 0;
  mutable int visibleStorageOrder_[MAX_LOGS] = {};

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

  bool hasCloudTimestampPrefix(const String& text) const {
    String s = text;
    s.trim();
    if (s.length() < 19) return false;
    auto isDigitAt = [&s](int idx) -> bool {
      return idx >= 0 && idx < (int)s.length() && isDigit((unsigned char)s[idx]);
    };
    return isDigitAt(0) && isDigitAt(1) && isDigitAt(2) && isDigitAt(3) &&
           s[4] == '-' &&
           isDigitAt(5) && isDigitAt(6) &&
           s[7] == '-' &&
           isDigitAt(8) && isDigitAt(9) &&
           s[10] == ' ' &&
           isDigitAt(11) && isDigitAt(12) &&
           s[13] == ':' &&
           isDigitAt(14) && isDigitAt(15) &&
           s[16] == ':' &&
           isDigitAt(17) && isDigitAt(18);
  }

  String extractTimestamp(const String& entry) const {
    String s = entry;
    s.trim();
    if (!hasCloudTimestampPrefix(s)) return "";
    return s.substring(0, 19);
  }

  bool shouldHideForUi(const String& entry) const {
    String text = entry;
    if (hasCloudTimestampPrefix(text)) text = text.substring(21);
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
    const uint32_t nowMs = millis();
    const bool needsBackfill = !ts.length();

    if (count < MAX_LOGS) {
      logs[count] = entry;
      timestamps[count] = ts;
      addedAtMs_[count] = nowMs;
      pendingBackfill_[count] = needsBackfill;
      count++;
    } else {
      // FIFO – przesunięcie w lewo
      for (int i = 1; i < MAX_LOGS; i++) {
        logs[i - 1] = logs[i];
        timestamps[i - 1] = timestamps[i];
        addedAtMs_[i - 1] = addedAtMs_[i];
        pendingBackfill_[i - 1] = pendingBackfill_[i];
      }
      logs[MAX_LOGS - 1] = entry;
      timestamps[MAX_LOGS - 1] = ts;
      addedAtMs_[MAX_LOGS - 1] = nowMs;
      pendingBackfill_[MAX_LOGS - 1] = needsBackfill;
    }
    markChanged();
    saveToFS();
  }

  void clear() {
    for (int i = 0; i < count; ++i) {
      timestamps[i] = "";
      addedAtMs_[i] = 0;
      pendingBackfill_[i] = false;
    }
    count = 0;
    markChanged();
    saveToFS();
  }

  bool backfillPendingTimestamps() {
    time_t now = time(nullptr);
    if (now < 100000) return false;

    const uint32_t nowMs = millis();
    bool changed = false;
    for (int i = 0; i < count; ++i) {
      if (!pendingBackfill_[i] || timestamps[i].length()) continue;

      const uint32_t ageMs = (nowMs >= addedAtMs_[i]) ? (nowMs - addedAtMs_[i]) : 0;
      time_t eventTs = now - (time_t)(ageMs / 1000UL);
      if (eventTs < 100000) eventTs = now;

      struct tm t;
      localtime_r(&eventTs, &t);
      char buf[20];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
               t.tm_year + 1900,
               t.tm_mon + 1,
               t.tm_mday,
               t.tm_hour,
               t.tm_min,
               t.tm_sec);

      timestamps[i] = String(buf);
      if (!hasCloudTimestampPrefix(logs[i])) {
        logs[i] = timestamps[i] + ": " + logs[i];
      }
      pendingBackfill_[i] = false;
      changed = true;
    }

    if (changed) {
      markChanged();
      saveToFS();
    }
    return changed;
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

  int visibleCount() const {
    rebuildVisibleCache();
    return visibleCacheCount_;
  }

  String visibleAt(int newestIndex) const {
    const int storageIndex = visibleStorageIndexAt(newestIndex);
    if (storageIndex < 0) return "";
    return EventMessages::displayMessageText(logs[storageIndex]);
  }

  String visibleRawAt(int newestIndex) const {
    const int storageIndex = visibleStorageIndexAt(newestIndex);
    if (storageIndex < 0) return "";
    return logs[storageIndex];
  }

  String visibleTimestampAt(int newestIndex) const {
    const int storageIndex = visibleStorageIndexAt(newestIndex);
    if (storageIndex < 0) return "";
    if (timestamps[storageIndex].length()) return timestamps[storageIndex];
    return extractTimestamp(logs[storageIndex]);
  }

  uint32_t revision() const { return revision_; }

private:
  void markChanged() {
    visibleCacheDirty_ = true;
    revision_++;
  }

  void rebuildVisibleCache() const {
    if (!visibleCacheDirty_) return;
    visibleCacheCount_ = 0;
    for (int i = count - 1; i >= 0; --i) {
      if (shouldHideForUi(logs[i])) continue;
      visibleStorageOrder_[visibleCacheCount_++] = i;
    }
    // Zachowujemy porządek jak w cloud:
    // - wpisy z timestampem malejąco po czasie,
    // - wpisy bez timestampu zachowują kolejność newest-first.
    for (int pass = 0; pass < visibleCacheCount_ - 1; ++pass) {
      bool swapped = false;
      for (int i = 0; i < visibleCacheCount_ - 1 - pass; ++i) {
        const int aIdx = visibleStorageOrder_[i];
        const int bIdx = visibleStorageOrder_[i + 1];
        const String aTs = timestamps[aIdx].length() ? timestamps[aIdx] : extractTimestamp(logs[aIdx]);
        const String bTs = timestamps[bIdx].length() ? timestamps[bIdx] : extractTimestamp(logs[bIdx]);

        bool shouldSwap = false;
        if (aTs.length() && bTs.length()) {
          shouldSwap = aTs < bTs;
        } else if (!aTs.length() && bTs.length()) {
          shouldSwap = true;
        }

        if (shouldSwap) {
          const int tmp = visibleStorageOrder_[i];
          visibleStorageOrder_[i] = visibleStorageOrder_[i + 1];
          visibleStorageOrder_[i + 1] = tmp;
          swapped = true;
        }
      }
      if (!swapped) break;
    }
    visibleCacheDirty_ = false;
  }

  int visibleStorageIndexAt(int newestIndex) const {
    rebuildVisibleCache();
    if (newestIndex < 0 || newestIndex >= visibleCacheCount_) return -1;
    return visibleStorageOrder_[newestIndex];
  }

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
      logs[count] = v.as<String>();
      timestamps[count] = extractTimestamp(logs[count]);
      addedAtMs_[count] = 0;
      pendingBackfill_[count] = false;
      count++;
    }
    markChanged();
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
