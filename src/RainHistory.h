#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

class RainHistory {
private:
    struct RainRecord {
        time_t timestamp;
        float rain_mm;
    };

    static const int BUFFER_HOURS = 24;
    static const int MAX_RECORDS = BUFFER_HOURS;
    RainRecord records[MAX_RECORDS];
    int count = 0;

public:
    void begin() {
        loadFromFS();
    }

    void addRainMeasurement(float rain_mm, time_t sampleTs = 0) {
        if (rain_mm <= 0.0f) return;
        time_t now = (sampleTs > 0) ? sampleTs : time(nullptr);

        // Jeśli ostatni rekord jest z tej samej godziny, zsumuj opady
        if (count > 0) {
            struct tm last_tm{}, now_tm{};
            localtime_r(&records[count-1].timestamp, &last_tm);
            localtime_r(&now, &now_tm);

            if (last_tm.tm_year == now_tm.tm_year &&
                last_tm.tm_yday == now_tm.tm_yday &&
                last_tm.tm_hour == now_tm.tm_hour) {
                records[count-1].rain_mm += rain_mm;
                if (now > records[count-1].timestamp) records[count-1].timestamp = now;
                cleanupOld(now);
                saveToFS();
                return;
            }
        }

        // Dodaj nowy rekord
        if (count < MAX_RECORDS) {
            records[count] = {now, rain_mm};
            count++;
        } else {
            // Przesuń rekordy i dodaj nowy na końcu
            for (int i = 0; i < MAX_RECORDS - 1; i++) {
                records[i] = records[i+1];
            }
            records[MAX_RECORDS-1] = {now, rain_mm};
        }

        // Usuń stare rekordy starsze niż 24 godziny
        cleanupOld(now);

        saveToFS();
    }

    float getLast24hRain() const {
        float sum = 0.0f;
        time_t now = time(nullptr);
        for (int i = 0; i < count; i++) {
            if (now - records[i].timestamp <= 24 * 3600) {
                sum += records[i].rain_mm;
            }
        }
        return sum;
    }

    time_t getLastTimestamp() const {
        if (count <= 0) return 0;
        return records[count - 1].timestamp;
    }

    void toJson(JsonDocument& doc) const {
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < count; i++) {
            JsonObject obj = arr.add<JsonObject>();
            obj["time"] = records[i].timestamp;
            obj["rain"] = round(records[i].rain_mm * 100) / 100.0; // Zaokrąglenie do 0.01 mm
        }
    }

private:
    void loadFromFS() {
        if (!LittleFS.exists("/rain-history.json")) {
            count = 0;
            return;
        }

        File f = LittleFS.open("/rain-history.json", "r");
        if (!f) {
            count = 0;
            return;
        }

        StaticJsonDocument<2048> doc; // rozmiar dopasowany do MAX_RECORDS
        DeserializationError err = deserializeJson(doc, f);
        f.close();

        if (err) {
            Serial.print("[RainHistory] Błąd odczytu JSON: ");
            Serial.println(err.c_str());
            count = 0;
            return;
        }

        count = 0;
        for (JsonVariant v : doc.as<JsonArray>()) {
            if (count >= MAX_RECORDS) break;
            records[count].timestamp = v["time"] | 0;
            records[count].rain_mm = v["rain"] | 0.0f;
            count++;
        }

        // Usuń stare rekordy (starsze niż 24 godziny)
        cleanupOld();

        // POPRAWKA #10: po czyszczeniu natychmiast utrwal stan w pliku
        saveToFS();
    }

    void saveToFS() {
        StaticJsonDocument<2048> doc; // 24 rekordy × ~30B każdy
        toJson(doc);

        File f = LittleFS.open("/rain-history.json", "w");
        if (!f) {
            Serial.println("[RainHistory] Nie można otworzyć pliku do zapisu!");
            return;
        }
        if (serializeJson(doc, f) == 0) {
            Serial.println("[RainHistory] Błąd zapisu JSON!");
        }
        f.close();
    }

    void cleanupOld(time_t nowTs = 0) {
        time_t now = (nowTs > 0) ? nowTs : time(nullptr);
        RainRecord tmp[MAX_RECORDS];
        int valid = 0;
        for (int i = 0; i < count; i++) {
            if (now - records[i].timestamp <= 24 * 3600) {
                tmp[valid++] = records[i];
            }
        }
        for (int i = 0; i < valid; i++) {
            records[i] = tmp[i];
        }
        count = valid;
    }
};
