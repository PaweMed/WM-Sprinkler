#include <Arduino.h>
#include "FS.h"
#include "LittleFS.h"
#include <time.h>

#include "Config.h"
#include "Zones.h"
#include "Programs.h"
#include "Weather.h"
#include "Logs.h"
#include "PushoverClient.h"
#include "WebServerUI.h"
#include "MQTTClient.h"
#include "DeviceIdentity.h"
#include "OtaSecurity.h"
#include "OtaStateBackup.h"
#include "TftPanelUI.h"

// --- Obiekty globalne ---
Config config;
Zones zones(8);
Weather weather;
Logs logs;
PushoverClient pushover(config.getSettingsPtr());
Programs programs;
MQTTClient mqtt;  // JEDYNA definicja globalnego klienta MQTT
DeviceIdentity deviceIdentity;
TftPanelUI panelUi;

static Weather::SmartIrrigationConfig buildSmartIrrigationConfig(Config& cfg) {
  Weather::SmartIrrigationConfig sc;
  sc.tempSkipC = cfg.getIrrigationTempSkipC();
  sc.tempLowMaxC = cfg.getIrrigationTempLowMaxC();
  sc.tempMidMaxC = cfg.getIrrigationTempMidMaxC();
  sc.tempHighMaxC = cfg.getIrrigationTempHighMaxC();
  sc.tempFactorLow = cfg.getIrrigationTempFactorLow();
  sc.tempFactorMid = cfg.getIrrigationTempFactorMid();
  sc.tempFactorHigh = cfg.getIrrigationTempFactorHigh();
  sc.tempFactorVeryHigh = cfg.getIrrigationTempFactorVeryHigh();
  sc.rainSkipMm = cfg.getIrrigationRainSkipMm();
  sc.rainHighMinMm = cfg.getIrrigationRainHighMinMm();
  sc.rainMidMinMm = cfg.getIrrigationRainMidMinMm();
  sc.rainFactorHigh = cfg.getIrrigationRainFactorHigh();
  sc.rainFactorMid = cfg.getIrrigationRainFactorMid();
  sc.rainFactorLow = cfg.getIrrigationRainFactorLow();
  sc.humidityHighPercent = cfg.getIrrigationHumidityHighPercent();
  sc.humidityFactorHigh = cfg.getIrrigationHumidityFactorHigh();
  sc.windSkipKmh = cfg.getIrrigationWindSkipKmh();
  sc.windFactor = cfg.getIrrigationWindFactor();
  sc.percentMin = cfg.getIrrigationPercentMin();
  sc.percentMax = cfg.getIrrigationPercentMax();
  return sc;
}

static void restoreAfterOtaIfNeeded() {
  String restoreDetail;
  if (!OtaStateBackup::restoreIfPending(&config, restoreDetail)) {
    Serial.println("[OTA-RESTORE] ERROR: " + restoreDetail);
    return;
  }
  if (restoreDetail == "restored") {
    Serial.println("[OTA-RESTORE] Odtworzono dane użytkownika po OTA.");
  }
}

// Pomocnicza konwersja "+HH[:MM]" / "-HH[:MM]" -> POSIX "UTC-xx[:yy]"
static String offsetToPosixTZ(const String& tz)
{
  if (tz.length() < 2) return "";
  char s = tz.charAt(0);
  if (s != '+' && s != '-') return "";

  int hh = 0, mm = 0;
  int colon = tz.indexOf(':');
  bool ok = true;

  String hpart = "";
  String mpart = "";
  if (colon > 0) { hpart = tz.substring(1, colon); mpart = tz.substring(colon + 1); }
  else { hpart = tz.substring(1); }

  for (size_t i = 0; i < hpart.length(); i++) { if (!isDigit(hpart[i])) { ok = false; break; } }
  if (!ok || hpart.length() == 0) return "";
  hh = hpart.toInt(); if (hh < 0 || hh > 23) return "";

  if (mpart.length() > 0) {
    for (size_t i = 0; i < mpart.length(); i++) { if (!isDigit(mpart[i])) { ok = false; break; } }
    if (!ok) return "";
    mm = mpart.toInt(); if (mm < 0 || mm > 59) return "";
  }

  char buf[16];
  char outSign = (s == '+') ? '-' : '+';
  if (mm > 0) snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", outSign, hh, mm);
  else        snprintf(buf, sizeof(buf), "UTC%c%d", outSign, hh);
  return String(buf);
}

static const char* namedTimezoneToPosixTZ(const String& tz)
{
  if (tz == "Europe/Warsaw" || tz == "Europe/Berlin") {
    return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  }
  if (tz == "Europe/Helsinki") {
    return "EET-2EEST,M3.5.0/3,M10.5.0/4";
  }
  if (tz == "Europe/London") {
    return "GMT0BST,M3.5.0/1,M10.5.0/2";
  }
  if (tz == "America/New_York") {
    return "EST5EDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "America/Chicago") {
    return "CST6CDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "America/Denver") {
    return "MST7MDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "America/Los_Angeles") {
    return "PST8PDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "America/Anchorage") {
    return "AKST9AKDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "Atlantic/Azores") {
    return "AZOT1AZOST,M3.5.0/0,M10.5.0/1";
  }
  if (tz == "Australia/Sydney") {
    return "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
  }
  if (tz == "Pacific/Auckland") {
    return "NZST-12NZDT,M9.5.0/2,M4.1.0/3";
  }
  if (tz == "Etc/UTC") {
    return "UTC0";
  }
  if (tz == "Asia/Tokyo") {
    return "JST-9";
  }
  if (tz == "Asia/Almaty") {
    return "ALMT-5";
  }
  return nullptr;
}

static bool timezoneSupportsAutoDst(const String& tz)
{
  return tz == "Europe/Warsaw"
      || tz == "Europe/Berlin"
      || tz == "Europe/Helsinki"
      || tz == "Europe/London"
      || tz == "America/New_York"
      || tz == "America/Chicago"
      || tz == "America/Denver"
      || tz == "America/Los_Angeles"
      || tz == "America/Anchorage"
      || tz == "Atlantic/Azores"
      || tz == "Australia/Sydney"
      || tz == "Pacific/Auckland";
}

void setTimezone() {
  String tz = config.getTimezone();
  Serial.print("Strefa czasowa ustawiana na: "); Serial.println(tz);

  if (!tz.length()) tz = "Europe/Warsaw";

  const char* namedPosix = namedTimezoneToPosixTZ(tz);
  if (namedPosix) {
    setenv("TZ", namedPosix, 1);
  } else {
    const String posix = offsetToPosixTZ(tz);
    if (posix.length() > 0) setenv("TZ", posix.c_str(), 1);
    else setenv("TZ", tz.c_str(), 1);
  }

  tzset();
  Serial.print("Aktualny TZ z getenv: "); Serial.println(getenv("TZ"));

  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po zmianie strefy: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
  char zoneBuf[16] = {0};
  strftime(zoneBuf, sizeof(zoneBuf), "%Z", &t);
  if (timezoneSupportsAutoDst(tz)) {
    Serial.printf("Tryb czasu: %s (%s)\n", t.tm_isdst > 0 ? "czas letni" : "czas zimowy", zoneBuf);
  } else {
    Serial.printf("Tryb czasu: czas staly (%s)\n", zoneBuf);
  }
}

void syncNtp() {
  Serial.println("Synchronizacja czasu NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 8 * 3600 * 2 && millis() - t0 < 20000) {
    delay(500);
    now = time(nullptr);
    Serial.print("[NTP] Synchronizacja czasu... "); Serial.println(now);
  }
  struct tm t; localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po synchronizacji: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
}

static void maintainClockSync() {
  static bool lastWifiConnected = false;
  static unsigned long lastAttemptMs = 0;

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    lastWifiConnected = false;
    return;
  }

  time_t now = time(nullptr);
  if (now >= 100000) {
    lastWifiConnected = true;
    return;
  }

  const unsigned long nowMs = millis();
  if (!lastWifiConnected) {
    Serial.println("[NTP] WiFi połączone po starcie - próbuję zsynchronizować czas.");
    lastWifiConnected = true;
    lastAttemptMs = 0;
  }

  if (lastAttemptMs != 0 && (nowMs - lastAttemptMs) < 15000UL) return;
  lastAttemptMs = nowMs;

  syncNtp();
  now = time(nullptr);
  if (now >= 100000) {
    setTimezone();
    Serial.println("[NTP] Zegar gotowy - nowe logi będą miały datę i godzinę.");
  } else {
    Serial.println("[NTP] Synchronizacja nadal nieudana - spróbuję ponownie później.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  const bool fsMounted = LittleFS.begin();
  if (!fsMounted) {
    Serial.println("[FS] ERROR: LittleFS mount failed.");
  } else {
    Serial.println("[FS] LittleFS mounted.");
    OtaSecurity::handleBootStart();
    Serial.println("Pliki w LittleFS:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
      Serial.print("  "); Serial.print(file.name());
      Serial.print(" ("); Serial.print(file.size()); Serial.println(" bajtów)");
      file = root.openNextFile();
    }
  }

  config.load();
  if (fsMounted) restoreAfterOtaIfNeeded();
  config.load(); // odśwież po ewentualnym restore
  zones.setZoneCount(config.getZoneCount());
  deviceIdentity.begin();
  Serial.println("[DEVICE] Device identity:");
  Serial.println("[DEVICE] device_id: " + deviceIdentity.getDeviceId());
  Serial.println("[DEVICE] claim_code: " + deviceIdentity.getClaimCode());
  config.applyCloudDefaults(deviceIdentity.getDeviceId());

  zones.begin();

  // *** WAŻNE: wczytaj trwałe logi z /logs.json ***
  logs.begin();
  // Weather: natychmiastowa próba, retry po 60s, potem co X min wg ustawień
  weather.begin(
    config.getOwmApiKey(),
    config.getOwmLocation(),
    config.getEnableWeatherApi(),
    config.getWeatherUpdateIntervalMin()
  );
  weather.applySmartIrrigationConfig(buildSmartIrrigationConfig(config));

  pushover.begin();

  // Programs – teraz z dostępem do Config
  programs.begin(&zones, &weather, &logs, &pushover, &config);

  mqtt.begin(&zones, &programs, &weather, &logs, &config, &deviceIdentity, &pushover);
  panelUi.begin(&zones, &weather, &config, &mqtt, &logs);

  // Na ESP32-C6 unikamy nakładania ciężkich odczytów z LittleFS (UI/logo)
  // i startu radia WiFi w tej samej fazie bootu.
  config.initWiFi(&pushover);

  if (WiFi.status() == WL_CONNECTED) {
    syncNtp();
  } else {
    Serial.println("[NTP] Pomijam synchronizację czasu - brak połączenia WiFi.");
  }
  setTimezone();

  WebServerUI::begin(
    &config, nullptr, &zones, &weather, &pushover, &programs, &logs, &deviceIdentity
  );

  if (fsMounted) {
    OtaSecurity::confirmBootIfPending();

    String activatedVersion;
    String versionErr;
    if (!FirmwareVersionStore::activatePendingVersion(activatedVersion, versionErr)) {
      Serial.println("[FWVER] WARN activate pending: " + versionErr);
    } else if (activatedVersion.length() > 0) {
      Serial.println("[FWVER] Active OTA version: " + activatedVersion);
    }
  }

  Serial.println("[MAIN] System uruchomiony.");
}

extern "C" void setTimezoneFromWeb() { setTimezone(); }

void loop() {
  config.wifiLoop();
  maintainClockSync();
  logs.backfillPendingTimestamps();
  zones.loop();
  programs.loop();
  const bool wifiStableForNet = config.isWiFiStable(2500UL);
  if (wifiStableForNet) weather.loop();
  else weather.offlineTick();
  mqtt.loop();
  panelUi.loop();
  // Pozwala schedulerowi uruchomić taski systemowe (w tym idle), co stabilizuje WDT.
  delay(1);
}
