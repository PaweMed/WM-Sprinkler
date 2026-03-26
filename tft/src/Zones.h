#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

class Zones {
  static constexpr int MAX_ZONES = 8;
  int numZones;
  bool states[MAX_ZONES];
  // Kolejnosc logiczna stref 1..8 musi odpowiadac fizycznym kanalom 1..8
  // na plytkach 4- i 8-przekaznikowych. Wcześniej byla odwrotna (1->8, 2->7...).
#if defined(WMS_BOARD_ESP32C6_RELAY_X1_V11)
  // ESP32-C6 Relay X1 V1.1: przekaźnik na GPIO19 (1 kanał).
  int pins[MAX_ZONES] = {19, -1, -1, -1, -1, -1, -1, -1};
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  // Ogólne mapowanie testowe dla C6 (wielokanałowe płytki własne).
  int pins[MAX_ZONES] = {5, 6, 7, 8, 9, 10, 11, 14};
#else
  int pins[MAX_ZONES] = {32,33,25,26,27,14,12,13};
#endif
  unsigned long endTime[MAX_ZONES] = {0}; // kiedy wyłączyć
  int initialDurationSec[MAX_ZONES] = {0}; // pierwotny czas uruchomienia (sekundy)
  bool pendingStateChange = false;
  unsigned long lastStateChangeMs = 0;

  // Nazwy stref
  String zoneNames[MAX_ZONES];

  static int normalizeZoneCount(int count) {
    if (count < 1) return 1;
    if (count > MAX_ZONES) return MAX_ZONES;
    return count;
  }

  void markStateChange() {
    pendingStateChange = true;
    lastStateChangeMs = millis();
  }

  void loadZoneNames() {
    if (!LittleFS.exists("/zones-names.json")) {
      // Ustaw domyślne
      for (int i = 0; i < MAX_ZONES; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      saveZoneNames(); // od razu zapisz domyślne
      return;
    }
    File f = LittleFS.open("/zones-names.json", "r");
    if (!f) {
      for (int i = 0; i < MAX_ZONES; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      for (int i = 0; i < MAX_ZONES; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      return;
    }
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < MAX_ZONES; ++i) {
      if (i < arr.size() && arr[i].is<const char*>()) {
        zoneNames[i] = arr[i].as<const char*>();
      } else {
        zoneNames[i] = "Strefa " + String(i + 1);
      }
    }
  }

public:
  Zones(int n) : numZones(n) {
    // Domyślne
    numZones = normalizeZoneCount(n);
    for (int i = 0; i < MAX_ZONES; ++i) zoneNames[i] = "Strefa " + String(i + 1);
  }

  void begin() {
    for(int i=0; i<MAX_ZONES; i++) {
      if (pins[i] >= 0) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
      }
      states[i] = false;
      endTime[i] = 0;
      initialDurationSec[i] = 0;
    }
    loadZoneNames();
  }

  int getZoneCount() const { return numZones; }

  void setZoneCount(int count) {
    const int newCount = normalizeZoneCount(count);
    if (newCount == numZones) return;

    // Ensure disabled zones are physically off and inactive.
    bool changed = false;
    for (int i = newCount; i < MAX_ZONES; i++) {
      if (states[i] || endTime[i] != 0) changed = true;
      states[i] = false;
      endTime[i] = 0;
      initialDurationSec[i] = 0;
      if (pins[i] >= 0) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
      }
    }
    numZones = newCount;
    if (changed) markStateChange();
  }

  void toJson(JsonDocument& doc) {
    for(int i=0;i<numZones;i++) {
      JsonObject z = doc.add<JsonObject>();
      z["id"] = i;
      z["active"] = states[i];
      // POPRAWKA #1: zgodnie z frontendem zwracamy klucz "remaining" (sekundy)
      z["remaining"] = states[i] ? max(0, (int)((endTime[i] - millis())/1000)) : 0;
      z["name"] = zoneNames[i];
    }
  }

  void startZone(int idx, int durationSec) {
    if(idx<0||idx>=numZones) return;
    if (durationSec <= 0) durationSec = 1;
    const bool wasActive = states[idx];
    const unsigned long oldEnd = endTime[idx];
    const unsigned long newEnd = millis() + durationSec*1000UL;
    bool changedOther = false;

    // Hard single-zone lock: uruchomienie jednej strefy wygasza wszystkie pozostałe.
    for (int i = 0; i < numZones; i++) {
      if (i == idx) continue;
      if (states[i] || endTime[i] != 0) {
        states[i] = false;
        endTime[i] = 0;
        initialDurationSec[i] = 0;
        if (pins[i] >= 0) digitalWrite(pins[i], LOW);
        changedOther = true;
      }
    }

    Serial.printf("startZone(%d, %d)\n", idx, durationSec);
    states[idx] = true;
    if (pins[idx] >= 0) digitalWrite(pins[idx], HIGH);
    endTime[idx] = newEnd;
    initialDurationSec[idx] = durationSec;
    if (changedOther || !wasActive || oldEnd != newEnd) markStateChange();
  }

  void stopZone(int idx) {
    if(idx<0||idx>=numZones) return;
    const bool wasActive = states[idx];
    const unsigned long oldEnd = endTime[idx];
    Serial.printf("stopZone(%d)\n", idx);
    states[idx] = false;
    if (pins[idx] >= 0) digitalWrite(pins[idx], LOW);
    endTime[idx] = 0;
    initialDurationSec[idx] = 0;
    if (wasActive || oldEnd != 0) markStateChange();
  }

  void toggleZone(int idx) {
    if(idx<0||idx>=numZones) return;
    Serial.printf("toggleZone(%d) - before: %d\n", idx, states[idx]);
    if (states[idx]) stopZone(idx);
    else startZone(idx, 600); // domyślnie 10 min w manualu
    Serial.printf("toggleZone(%d) - after: %d\n", idx, states[idx]);
  }

  void loop() {
    unsigned long now = millis();
    for(int i=0; i<numZones; i++) {
      if(states[i] && now > endTime[i]) stopZone(i);
    }
  }

  bool getZoneState(int idx) { 
    if(idx<0||idx>=numZones) return false; 
    return states[idx]; 
  }

  // --- Nazwy stref ---

  // Zwraca nazwę strefy o podanym indeksie
  String getZoneName(int idx) {
    if(idx<0||idx>=numZones) return "";
    return zoneNames[idx];
  }

  // Zmienia nazwę strefy (nie zapisuje automatycznie!)
  void setZoneName(int idx, const String& name) {
    if(idx<0||idx>=numZones) return;
    zoneNames[idx] = name;
  }

  // Zapisuje aktualne nazwy do pliku
  void saveZoneNames() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < MAX_ZONES; ++i) arr.add(zoneNames[i]);
    File f = LittleFS.open("/zones-names.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
  }

  // Zwraca wszystkie nazwy jako tablicę JSON
  void toJsonNames(JsonArray& arr) {
    for (int i = 0; i < numZones; ++i) arr.add(zoneNames[i]);
  }

  // Ustawia wszystkie nazwy na raz (i od razu zapisuje)
  void setAllZoneNames(const JsonArray& arr) {
    for (int i = 0; i < numZones; ++i) {
      if (i < arr.size() && arr[i].is<const char*>()) {
        zoneNames[i] = arr[i].as<const char*>();
      } else {
        zoneNames[i] = "Strefa " + String(i + 1);
      }
    }
    saveZoneNames();
  }

  int getRemainingSeconds(int idx) {
    if (idx<0 || idx>=numZones) return 0;
    if (!states[idx]) return 0;
    long rem = (long)((endTime[idx] - millis())/1000);
    return rem > 0 ? (int)rem : 0;
  }

  int getZoneInitialDurationSeconds(int idx) const {
    if (idx < 0 || idx >= numZones) return 0;
    return initialDurationSec[idx];
  }

  bool consumeStateChange(unsigned long debounceMs = 0) {
    if (!pendingStateChange) return false;
    if (debounceMs > 0 && (unsigned long)(millis() - lastStateChangeMs) < debounceMs) return false;
    pendingStateChange = false;
    return true;
  }
};
