#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "RainHistory.h"

class Weather {
public:
  struct SmartIrrigationConfig {
    float tempSkipC = 5.0f;
    float tempLowMaxC = 18.0f;
    float tempMidMaxC = 25.0f;
    float tempHighMaxC = 30.0f;
    float tempFactorLow = 0.5f;
    float tempFactorMid = 1.0f;
    float tempFactorHigh = 1.3f;
    float tempFactorVeryHigh = 1.5f;

    float rainSkipMm = 6.0f;
    float rainHighMinMm = 5.0f;
    float rainMidMinMm = 2.0f;
    float rainFactorHigh = 0.3f;
    float rainFactorMid = 0.6f;
    float rainFactorLow = 1.0f;

    float humidityHighPercent = 90.0f;
    float humidityFactorHigh = 0.75f;

    float windSkipKmh = 25.0f;
    float windFactor = 1.0f;

    int percentMin = 0;
    int percentMax = 160;
  };

  struct IrrigationDecision {
    bool hardStop = false;
    String hardStopReasonCode = "none";
    String hardStopReasonText = "";

    float rain24hMm = 0.0f;
    float tempNowC = 0.0f;
    float humidityNowPercent = 0.0f;
    float windNowKmh = 0.0f;

    float tempFactor = 1.0f;
    float rainFactor = 1.0f;
    float humidityFactor = 1.0f;
    float windFactor = 1.0f;
    float totalFactor = 1.0f;

    int percent = 100;
    bool allowed = true;
  };

private:
  String apiKey, location;

  // Dane aktualne
  float temp = 0, feels_like = 0, temp_min = 0, temp_max = 0;
  float humidity = 0, pressure = 0, wind = 0, wind_deg = 0, clouds = 0, visibility = 0;
  String weather_desc = "", icon = "";
  float rain = 0;

  // Prognozy
  float rain_1h_forecast = 0, rain_24h_forecast = 0;
  float temp_min_tomorrow = 0, temp_max_tomorrow = 0;
  float humidity_tomorrow_max = 0; // maksymalna prognozowana wilgotność na jutro

  // Wschód/zachód
  String sunrise = "", sunset = "";

  // Sterowanie
  bool   enabled = true;
  unsigned long intervalMs = 60UL * 60UL * 1000UL;  // domyślnie 1h
  static constexpr uint16_t kHttpConnectTimeoutMs = 2500;
  static constexpr uint16_t kHttpReadTimeoutMs = 2500;

  // Terminy pobrań
  unsigned long nextWeatherDue  = 0;
  unsigned long nextForecastDue = 0;

  // Stan
  bool everSucceededWeather  = false;
  bool everSucceededForecast = false;

  // Cache GEO
  float cachedLat = 0.0f;
  float cachedLon = 0.0f;
  bool  coordsValid = false;

  RainHistory rainHistory; // historia opadów (rolling 24h, trwała w LittleFS)
  SmartIrrigationConfig smartCfg;
  time_t lastRainSampleTs = 0;
  bool rainSamplingInitialized = false;

  // --- Pomocnicze: proste URL-encode (wystarczy do spacji, przecinków itd.)
  static String urlEncode(const String& s) {
    String out;
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (('a' <= c && c <= 'z') ||
          ('A' <= c && c <= 'z') ||
          ('0' <= c && c <= '9') ||
          c=='-' || c=='_' || c=='.' || c=='~') {
        out += c;
      } else if (c == ' ') {
        out += "%20";
      } else {
        out += '%';
        out += hex[(c >> 4) & 0xF];
        out += hex[c & 0xF];
      }
    }
    return out;
  }

  bool resolveCoords() {
    if (coordsValid && cachedLat != 0.0f && cachedLon != 0.0f) return true;
    if (apiKey.isEmpty() || location.isEmpty()) {
      Serial.println("[Weather] Brak apiKey lub location – pomijam GEO.");
      return false;
    }

    // HTTPS + WiFiClientSecure
    WiFiClientSecure client;
    client.setInsecure();

    String urlGeo = "https://api.openweathermap.org/geo/1.0/direct?q=" + urlEncode(location) + "&limit=1&appid=" + apiKey;
    HTTPClient httpGeo;
    if (!httpGeo.begin(client, urlGeo)) {
      Serial.println("[Weather] Nie można zainicjować żądania GEO (begin).");
      return false;
    }
    httpGeo.setConnectTimeout(kHttpConnectTimeoutMs);
    httpGeo.setTimeout(kHttpReadTimeoutMs);
    int codeGeo = httpGeo.GET();
    if (codeGeo == HTTP_CODE_OK) {
      String respGeo = httpGeo.getString();
      JsonDocument docGeo;
      DeserializationError err = deserializeJson(docGeo, respGeo);
      if (!err) {
        if (docGeo.is<JsonArray>() && docGeo.size() > 0) {
          JsonObject obj = docGeo[0];
          cachedLat = obj["lat"].as<float>();
          cachedLon = obj["lon"].as<float>();
          coordsValid = (cachedLat != 0.0f || cachedLon != 0.0f);
        } else {
          Serial.println("[Weather] GEO: pusty wynik dla podanej lokalizacji.");
          coordsValid = false;
        }
      } else {
        Serial.print("[Weather] Błąd JSON GEO: "); Serial.println(err.c_str());
        coordsValid = false;
      }
    } else {
      Serial.print("[Weather] Błąd pobierania GEO! Kod HTTP: "); Serial.println(codeGeo);
      coordsValid = false;
    }
    httpGeo.end();

    if (!coordsValid) Serial.println("[Weather] Błąd: Brak współrzędnych!");
    return coordsValid;
  }

  int ydayTomorrow() {
    time_t now_ts = time(nullptr);
    now_ts += 24 * 60 * 60;
    struct tm t;
    localtime_r(&now_ts, &t);
    return t.tm_yday;
  }

  void scheduleRetryEarly(bool forWeather) {
    if (forWeather) {
      if (!everSucceededWeather) nextWeatherDue = millis() + 60000UL;
      else                       nextWeatherDue = millis() + intervalMs;
    } else {
      if (!everSucceededForecast) nextForecastDue = millis() + 60000UL;
      else                        nextForecastDue = millis() + intervalMs;
    }
  }

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

  void normalizeSmartConfig() {
    smartCfg.tempSkipC = clampFloat(smartCfg.tempSkipC, -20.0f, 40.0f);
    smartCfg.tempLowMaxC = clampFloat(smartCfg.tempLowMaxC, -20.0f, 50.0f);
    smartCfg.tempMidMaxC = clampFloat(smartCfg.tempMidMaxC, -20.0f, 60.0f);
    smartCfg.tempHighMaxC = clampFloat(smartCfg.tempHighMaxC, -20.0f, 70.0f);
    if (smartCfg.tempLowMaxC < smartCfg.tempSkipC) smartCfg.tempLowMaxC = smartCfg.tempSkipC;
    if (smartCfg.tempMidMaxC < smartCfg.tempLowMaxC) smartCfg.tempMidMaxC = smartCfg.tempLowMaxC;
    if (smartCfg.tempHighMaxC < smartCfg.tempMidMaxC) smartCfg.tempHighMaxC = smartCfg.tempMidMaxC;

    smartCfg.tempFactorLow = clampFloat(smartCfg.tempFactorLow, 0.0f, 3.0f);
    smartCfg.tempFactorMid = clampFloat(smartCfg.tempFactorMid, 0.0f, 3.0f);
    smartCfg.tempFactorHigh = clampFloat(smartCfg.tempFactorHigh, 0.0f, 3.0f);
    smartCfg.tempFactorVeryHigh = clampFloat(smartCfg.tempFactorVeryHigh, 0.0f, 3.0f);

    smartCfg.rainMidMinMm = clampFloat(smartCfg.rainMidMinMm, 0.0f, 100.0f);
    smartCfg.rainHighMinMm = clampFloat(smartCfg.rainHighMinMm, 0.0f, 100.0f);
    if (smartCfg.rainHighMinMm < smartCfg.rainMidMinMm) smartCfg.rainHighMinMm = smartCfg.rainMidMinMm;
    smartCfg.rainSkipMm = clampFloat(smartCfg.rainSkipMm, 0.0f, 200.0f);
    if (smartCfg.rainSkipMm < smartCfg.rainHighMinMm) smartCfg.rainSkipMm = smartCfg.rainHighMinMm;

    smartCfg.rainFactorHigh = clampFloat(smartCfg.rainFactorHigh, 0.0f, 2.0f);
    smartCfg.rainFactorMid = clampFloat(smartCfg.rainFactorMid, 0.0f, 2.0f);
    smartCfg.rainFactorLow = clampFloat(smartCfg.rainFactorLow, 0.0f, 2.0f);

    smartCfg.humidityHighPercent = clampFloat(smartCfg.humidityHighPercent, 0.0f, 100.0f);
    smartCfg.humidityFactorHigh = clampFloat(smartCfg.humidityFactorHigh, 0.0f, 2.0f);

    smartCfg.windSkipKmh = clampFloat(smartCfg.windSkipKmh, 0.0f, 200.0f);
    smartCfg.windFactor = clampFloat(smartCfg.windFactor, 0.0f, 2.0f);

    smartCfg.percentMin = clampInt(smartCfg.percentMin, 0, 300);
    smartCfg.percentMax = clampInt(smartCfg.percentMax, 0, 300);
    if (smartCfg.percentMax < smartCfg.percentMin) smartCfg.percentMax = smartCfg.percentMin;
  }

  float resolveTempFactor(float nowTemp) const {
    if (nowTemp < smartCfg.tempLowMaxC) return smartCfg.tempFactorLow;
    if (nowTemp < smartCfg.tempMidMaxC) return smartCfg.tempFactorMid;
    if (nowTemp < smartCfg.tempHighMaxC) return smartCfg.tempFactorHigh;
    return smartCfg.tempFactorVeryHigh;
  }

  float resolveRainFactor(float rain24hMm) const {
    if (rain24hMm >= smartCfg.rainHighMinMm) return smartCfg.rainFactorHigh;
    if (rain24hMm >= smartCfg.rainMidMinMm) return smartCfg.rainFactorMid;
    return smartCfg.rainFactorLow;
  }

  void updateRainHistoryFromObserved1h(float rainLastHourMm, time_t observedTs) {
    float observed = rainLastHourMm;
    if (observed < 0.0f) observed = 0.0f;

    time_t sampleTs = (observedTs > 0) ? observedTs : time(nullptr);
    if (!rainSamplingInitialized) {
      lastRainSampleTs = rainHistory.getLastTimestamp();
      rainSamplingInitialized = true;
    }

    // OWM "rain.1h" to okno kroczące 1h - zapisujemy maks. jedną próbkę na ~godzinę,
    // żeby nie dublować opadu przez nakładające się okna.
    if (lastRainSampleTs > 0) {
      if (sampleTs <= lastRainSampleTs) return;
      if ((sampleTs - lastRainSampleTs) < (55 * 60)) return;
    }

    if (observed > 0.0f) rainHistory.addRainMeasurement(observed, sampleTs);
    lastRainSampleTs = sampleTs;
  }

public:
  void begin(const String& key, const String& loc, bool en=true, int intervalMin=60) {
    apiKey = key;
    location = loc;
    enabled = en;
    if (intervalMin < 5) intervalMin = 5;
    intervalMs = (unsigned long)intervalMin * 60UL * 1000UL;

    nextWeatherDue  = 0;
    nextForecastDue = 0;
    everSucceededWeather  = false;
    everSucceededForecast = false;

    coordsValid = false;
    cachedLat = cachedLon = 0.0f;
    lastRainSampleTs = 0;
    rainSamplingInitialized = false;
    normalizeSmartConfig();

    rainHistory.begin(); // wczytaj historię z pliku
    lastRainSampleTs = rainHistory.getLastTimestamp();
    rainSamplingInitialized = true;
  }

  void applySettings(const String& key, const String& loc, bool en, int intervalMin) {
    begin(key, loc, en, intervalMin);
  }

  void applySmartIrrigationConfig(const SmartIrrigationConfig& cfg) {
    smartCfg = cfg;
    normalizeSmartConfig();
  }

  SmartIrrigationConfig getSmartIrrigationConfig() const {
    return smartCfg;
  }

  IrrigationDecision getIrrigationDecision() const {
    IrrigationDecision d;
    d.rain24hMm = getLast24hRain();
    d.tempNowC = temp;
    d.humidityNowPercent = humidity;
    d.windNowKmh = wind * 3.6f;

    // Bez danych pogodowych (np. po starcie lub gdy API wyłączone) nie blokujemy podlewania.
    if (!enabled || !everSucceededWeather) {
      d.hardStop = false;
      d.hardStopReasonCode = "weather_data_unavailable";
      d.hardStopReasonText = "Brak danych pogodowych";
      d.tempFactor = 1.0f;
      d.rainFactor = 1.0f;
      d.humidityFactor = 1.0f;
      d.windFactor = 1.0f;
      d.totalFactor = 1.0f;
      d.percent = clampInt(100, smartCfg.percentMin, smartCfg.percentMax);
      d.allowed = d.percent > 0;
      return d;
    }

    // Hard stop (priorytet): temp -> wiatr -> opady.
    if (d.tempNowC < smartCfg.tempSkipC) {
      d.hardStop = true;
      d.hardStopReasonCode = "temp_below_threshold";
      d.hardStopReasonText = "Temperatura poniżej progu";
    } else if (d.windNowKmh > smartCfg.windSkipKmh) {
      d.hardStop = true;
      d.hardStopReasonCode = "wind_above_threshold";
      d.hardStopReasonText = "Wiatr powyżej progu";
    } else if (d.rain24hMm > smartCfg.rainSkipMm) {
      d.hardStop = true;
      d.hardStopReasonCode = "rain_above_threshold";
      d.hardStopReasonText = "Opady z 24h powyżej progu";
    }

    d.tempFactor = resolveTempFactor(d.tempNowC);
    d.rainFactor = resolveRainFactor(d.rain24hMm);
    d.humidityFactor = (d.humidityNowPercent > smartCfg.humidityHighPercent) ? smartCfg.humidityFactorHigh : 1.0f;
    d.windFactor = smartCfg.windFactor;

    if (d.hardStop) {
      d.totalFactor = 0.0f;
      d.percent = 0;
      d.allowed = false;
      return d;
    }

    d.totalFactor = d.tempFactor * d.rainFactor * d.humidityFactor * d.windFactor;
    const int rawPercent = (int)roundf(d.totalFactor * 100.0f);
    d.percent = clampInt(rawPercent, smartCfg.percentMin, smartCfg.percentMax);
    d.allowed = d.percent > 0;
    return d;
  }

  void offlineTick() {
    const unsigned long nowMs = millis();
    if (nowMs >= nextWeatherDue)  nextWeatherDue = nowMs + 10000UL;
    if (nowMs >= nextForecastDue) nextForecastDue = nowMs + 12000UL;
  }

  void loop() {
    if (!enabled) return;
    unsigned long nowMs = millis();

    // W trybie offline nie wykonujemy żądań HTTP/DNS.
    // To odciąża CPU i stabilizuje pracę przy zaniku WiFi.
    if (WiFi.status() != WL_CONNECTED) {
      offlineTick();
      return;
    }

    // --- AKTUALNA ---
    if (nowMs >= nextWeatherDue) {
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam aktualne dane – brak apiKey/location.");
        nextWeatherDue = nowMs + intervalMs;
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie AKTUALNEJ pogody OWM...");
        WiFiClientSecure client;
        client.setInsecure();
        String url = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(cachedLat, 6) +
                     "&lon=" + String(cachedLon, 6) + "&units=metric&appid=" + apiKey + "&lang=pl";
        HTTPClient http;
        if (!http.begin(client, url)) {
          Serial.println("[Weather] Nie można zainicjować żądania weather (begin).");
          scheduleRetryEarly(true);
        } else {
          http.setConnectTimeout(kHttpConnectTimeoutMs);
          http.setTimeout(kHttpReadTimeoutMs);
          int code = http.GET();
          if (code == HTTP_CODE_OK) {
            String resp = http.getString();
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, resp);
            if (!err) {
              temp        = doc["main"]["temp"]        | 0.0;
              feels_like  = doc["main"]["feels_like"]  | 0.0;
              temp_min    = doc["main"]["temp_min"]    | 0.0;
              temp_max    = doc["main"]["temp_max"]    | 0.0;
              humidity    = doc["main"]["humidity"]    | 0.0;
              pressure    = doc["main"]["pressure"]    | 0.0;
              wind        = doc["wind"]["speed"]       | 0.0;
              wind_deg    = doc["wind"]["deg"]         | 0.0;
              clouds      = doc["clouds"]["all"]       | 0.0;
              visibility  = doc["visibility"]          | 0.0;
              rain        = doc["rain"]["1h"]          | 0.0;

              weather_desc = "";
              icon = "";
              if (doc["weather"].is<JsonArray>() && doc["weather"].size() > 0) {
                weather_desc = doc["weather"][0]["description"].as<const char*>();
                icon         = doc["weather"][0]["icon"].as<const char*>();
              }

              // Wschód/zachód
              time_t sunrise_ts = (time_t)(doc["sys"]["sunrise"] | 0);
              time_t sunset_ts  = (time_t)(doc["sys"]["sunset"]  | 0);
              char buf[8];
              struct tm t;
              if (sunrise_ts) {
                localtime_r(&sunrise_ts, &t);
                snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
                sunrise = String(buf);
              } else sunrise = "";
              if (sunset_ts) {
                localtime_r(&sunset_ts, &t);
                snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
                sunset = String(buf);
              } else sunset = "";

              // Aktualizacja historii opadów na podstawie rzeczywistej próbki 1h z OWM.
              // Bazujemy na znaczniku "dt", aby unikać dublowania przy częstszych odczytach.
              const time_t obsTs = (time_t)(doc["dt"] | 0);
              updateRainHistoryFromObserved1h(rain, obsTs);

              everSucceededWeather = true;
              nextWeatherDue = nowMs + intervalMs;
            } else {
              Serial.print("[Weather] Błąd JSON weather: "); Serial.println(err.c_str());
              scheduleRetryEarly(true);
            }
          } else {
            Serial.print("[Weather] Błąd pobierania weather! Kod HTTP: "); Serial.println(code);
            scheduleRetryEarly(true);
          }
          http.end();
        }
      } else {
        scheduleRetryEarly(true);
      }
    }

    // --- PROGNOZA ---
    if (nowMs >= nextForecastDue) {
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam prognozę – brak apiKey/location.");
        nextForecastDue = nowMs + intervalMs;
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie prognozy OWM...");
        WiFiClientSecure clientF;
        clientF.setInsecure();
        String urlF = "https://api.openweathermap.org/data/2.5/forecast?lat=" + String(cachedLat, 6) +
                      "&lon=" + String(cachedLon, 6) + "&appid=" + apiKey + "&units=metric";
        HTTPClient httpF;
        if (!httpF.begin(clientF, urlF)) {
          Serial.println("[Weather] Nie można zainicjować żądania forecast (begin).");
          scheduleRetryEarly(false);
        } else {
          httpF.setConnectTimeout(kHttpConnectTimeoutMs);
          httpF.setTimeout(kHttpReadTimeoutMs);
          int codeF = httpF.GET();
          if (codeF == HTTP_CODE_OK) {
            String respF = httpF.getString();
            JsonDocument docF;
            DeserializationError err = deserializeJson(docF, respF);
            if (!err) {
              rain_1h_forecast = 0;
              rain_24h_forecast = 0;
              temp_min_tomorrow = 0;
              temp_max_tomorrow = 0;
              humidity_tomorrow_max = 0;

              if (docF["list"].is<JsonArray>() && docF["list"].size() >= 2) {
                float rain3h_0 = docF["list"][0]["rain"]["3h"] | 0.0;
                rain_1h_forecast = rain3h_0 / 3.0f;
                int slots = 0;
                for (JsonVariant v : docF["list"].as<JsonArray>()) {
                  if (slots >= 8) break; // 8 * 3h = 24h
                  rain_24h_forecast += (v["rain"]["3h"] | 0.0f);
                  slots++;
                }
              }

              int targetYday = ydayTomorrow();
              float min_t = 1000.0f, max_t = -1000.0f;
              float max_h = 0.0f;

              for (JsonVariant v : docF["list"].as<JsonArray>()) {
                time_t ts = (time_t)(v["dt"] | 0);
                struct tm tt;
                localtime_r(&ts, &tt);
                if (tt.tm_yday == targetYday) {
                  float t_min = v["main"]["temp_min"] | 0.0;
                  float t_max = v["main"]["temp_max"] | 0.0;
                  float h_val = v["main"]["humidity"] | 0.0;
                  if (t_min < min_t) min_t = t_min;
                  if (t_max > max_t) max_t = t_max;
                  if (h_val > max_h) max_h = h_val;
                }
              }
              temp_min_tomorrow = (min_t < 1000.0f) ? min_t : temp_min;
              temp_max_tomorrow = (max_t > -1000.0f) ? max_t : temp_max;
              humidity_tomorrow_max = max_h;

              everSucceededForecast = true;
              nextForecastDue = nowMs + intervalMs;
            } else {
              Serial.print("[Weather] Błąd JSON forecast: "); Serial.println(err.c_str());
              scheduleRetryEarly(false);
            }
          } else {
            Serial.print("[Weather] Błąd pobierania forecast! Kod HTTP: "); Serial.println(codeF);
            scheduleRetryEarly(false);
          }
          httpF.end();
        }
      } else {
        scheduleRetryEarly(false);
      }
    }
  }

  void toJson(JsonDocument& doc) {
    const IrrigationDecision decision = getIrrigationDecision();

    doc["temp"] = temp;
    doc["feels_like"] = feels_like;
    doc["humidity"] = humidity;
    doc["pressure"] = pressure;
    doc["wind"] = wind;
    doc["wind_kmh"] = wind * 3.6f;
    doc["wind_deg"] = wind_deg;
    doc["clouds"] = clouds;
    doc["visibility"] = (int)(visibility / 1000);
    doc["weather_desc"] = weather_desc;
    doc["icon"] = icon;
    doc["rain"] = rain;
    doc["rain_24h_observed"] = rainHistory.getLast24hRain();
    doc["rain_1h_forecast"] = rain_1h_forecast;
    doc["rain_24h_forecast"] = rain_24h_forecast;
    doc["sunrise"] = sunrise;
    doc["sunset"] = sunset;
    doc["temp_min"] = temp_min;
    doc["temp_max"] = temp_max;
    doc["temp_min_tomorrow"] = temp_min_tomorrow;
    doc["temp_max_tomorrow"] = temp_max_tomorrow;
    doc["humidity_tomorrow_max"] = humidity_tomorrow_max;
    // Frontend ma korzystać z decyzji backendu 1:1.
    doc["watering_percent"] = decision.percent;
    doc["watering_allowed"] = decision.allowed;
    doc["watering_hard_stop"] = decision.hardStop;
    doc["watering_hard_stop_reason"] = decision.hardStopReasonCode;
    doc["watering_total_factor"] = decision.totalFactor;
    doc["watering_factor_temp"] = decision.tempFactor;
    doc["watering_factor_rain"] = decision.rainFactor;
    doc["watering_factor_humidity"] = decision.humidityFactor;
    doc["watering_factor_wind"] = decision.windFactor;
  }

  // API dla WebServerUI / innych modułów
  void rainHistoryToJson(JsonDocument& doc) const { rainHistory.toJson(doc); }
  float getLast24hRain() const { return rainHistory.getLast24hRain(); }
  float getDailyMaxTemp() const { return temp_max_tomorrow; }
  float getDailyHumidityForecast() const { return humidity_tomorrow_max; }

  // --- Bieżące (rzeczywiste) parametry, jeśli chcesz je gdzieś wyświetlać ---
  float getCurrentTemp() const { return temp; }
  float getCurrentHumidity() const { return humidity; }
  float getCurrentWindKmh() const { return wind * 3.6f; }
  const String& getCurrentIconCode() const { return icon; }

  void irrigationDecisionToJson(JsonDocument& doc) const {
    const IrrigationDecision d = getIrrigationDecision();
    doc["percent"] = d.percent;
    doc["allowed"] = d.allowed;
    doc["hard_stop"] = d.hardStop;
    doc["hard_stop_reason_code"] = d.hardStopReasonCode;
    doc["hard_stop_reason"] = d.hardStopReasonText;
    doc["rain_24h"] = d.rain24hMm;
    doc["temp_now"] = d.tempNowC;
    doc["humidity_now"] = d.humidityNowPercent;
    doc["wind_now_kmh"] = d.windNowKmh;
    doc["factor_temp"] = d.tempFactor;
    doc["factor_rain"] = d.rainFactor;
    doc["factor_humidity"] = d.humidityFactor;
    doc["factor_wind"] = d.windFactor;
    doc["factor_total"] = d.totalFactor;
    doc["threshold_temp_skip_c"] = smartCfg.tempSkipC;
    doc["threshold_wind_skip_kmh"] = smartCfg.windSkipKmh;
    doc["threshold_rain_skip_mm"] = smartCfg.rainSkipMm;
    doc["threshold_humidity_high_percent"] = smartCfg.humidityHighPercent;
    doc["percent_min"] = smartCfg.percentMin;
    doc["percent_max"] = smartCfg.percentMax;
  }

  int getWateringPercent() {
    return getIrrigationDecision().percent;
  }

  bool wateringAllowed() { return getIrrigationDecision().allowed; }

  String getWateringDecisionExplain() {
    const IrrigationDecision d = getIrrigationDecision();
    if (d.hardStopReasonCode == "weather_data_unavailable") {
      return "AUTO: brak danych pogodowych -> 100% (fallback).";
    }
    if (d.hardStop) {
      if (d.hardStopReasonCode == "temp_below_threshold") {
        return "HARD STOP: T=" + String(d.tempNowC, 1) + "°C < " + String(smartCfg.tempSkipC, 1) + "°C.";
      }
      if (d.hardStopReasonCode == "wind_above_threshold") {
        return "HARD STOP: wiatr=" + String(d.windNowKmh, 1) + " km/h > " + String(smartCfg.windSkipKmh, 1) + " km/h.";
      }
      if (d.hardStopReasonCode == "rain_above_threshold") {
        return "HARD STOP: opady 24h=" + String(d.rain24hMm, 1) + " mm > " + String(smartCfg.rainSkipMm, 1) + " mm.";
      }
      return "HARD STOP: " + d.hardStopReasonText;
    }

    return String("AUTO: ")
      + "T=" + String(d.tempNowC, 1) + "°C (x" + String(d.tempFactor, 2) + "), "
      + "R24=" + String(d.rain24hMm, 1) + "mm (x" + String(d.rainFactor, 2) + "), "
      + "H=" + String(d.humidityNowPercent, 0) + "% (x" + String(d.humidityFactor, 2) + "), "
      + "W=" + String(d.windNowKmh, 1) + "km/h (x" + String(d.windFactor, 2) + ") "
      + "=> " + String(d.percent) + "%";
  }
};
