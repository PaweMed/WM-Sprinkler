#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>
#include <esp_wifi.h>
#include "Settings.h"
#include "PushoverClient.h"

class Config {
  // Na klasycznej płytce ESP32 status LED jest na GPIO23 (D20).
  // Dla testowego ESP32-C6 wyłączamy status LED, aby nie sterować przypadkowo
  // obcym obwodem (np. wejściem przekaźnika na płytce adaptera).
#if defined(CONFIG_IDF_TARGET_ESP32C6)
  static constexpr int STATUS_LED_PIN = -1;
#else
  static constexpr int STATUS_LED_PIN = 23;
#endif
  // Jeśli LED będzie działać odwrotnie, zmień na true.
  static constexpr bool STATUS_LED_ACTIVE_LOW = false;
#if defined(WMS_BOARD_ESP32C6_RELAY_X1_V11)
  // ESP32 C6 RELAY X1 V1.1: przycisk BOOT jest na GPIO9.
  static constexpr int AP_BUTTON_PIN  = 9;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
  // Dla innych płytek C6 pin BOOT może się różnić - wyłączamy obsługę
  // przycisku AP, aby uniknąć fałszywych przełączeń trybu.
  static constexpr int AP_BUTTON_PIN  = -1;
#else
  static constexpr int AP_BUTTON_PIN  = 0;   // BOOT / IO0
#endif
  static constexpr int LEDC_CH        = 0;
  static constexpr int LEDC_FREQ_HZ   = 5000;
  static constexpr int LEDC_BITS      = 8;

  Settings settings;
  bool wifiConfigured = false;
  bool inAPMode = false;
  bool wifiConnecting = false;
  bool io0LastState = true;
  unsigned long io0DebounceTs = 0;
  unsigned long apBlinkTs = 0;
  bool apBlinkOn = false;
  bool pinsInited = false;
  bool ledStateKnown = false;
  uint8_t ledLastValue = 0;
  unsigned long lastWiFiCheck = 0;
  unsigned int failedWiFiAttempts = 0;
  static const int maxWiFiAttempts = 4;
  static constexpr unsigned long kApModeDurationMs = 5UL * 60UL * 1000UL;
  unsigned long apModeStartedMs = 0;
  String lastWiFiError = "";
  PushoverClient* pushover = nullptr;
  bool wifiEventRegistered = false;
  int lastDisconnectReasonCode = -1;
  volatile bool wifiDisconnectEventPending = false;
  volatile int wifiDisconnectEventReason = -1;
  volatile bool wifiStaConnectedEventPending = false;
  volatile bool wifiGotIpEventPending = false;
  bool wifiStaLinkUp = false;
  bool wifiManuallyDisconnected = false;
  unsigned long wifiLastDisconnectMs = 0;
  unsigned long wifiLastGotIpMs = 0;
  unsigned long wifiLastUiActionMs = 0;
  bool wifiEverConnected = false;
  bool wifiRadioPrepared = false;
  unsigned long wifiAttemptStartedMs = 0;
  unsigned long wifiNextRetryMs = 0;
  static constexpr unsigned long kC6ConnectAttemptTimeoutMs = 12000UL;
  static constexpr unsigned long kC6RetryIntervalMs = 8000UL;

  const char* wlStatusText(wl_status_t s) const {
    switch (s) {
      case WL_NO_SHIELD: return "WL_NO_SHIELD";
      case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
      case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
      case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
      case WL_CONNECTED: return "WL_CONNECTED";
      case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
      case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
      case WL_DISCONNECTED: return "WL_DISCONNECTED";
      default: return "WL_UNKNOWN";
    }
  }

  const char* wifiDisconnectReasonText(int reason) const {
    const char* name = WiFi.disconnectReasonName((wifi_err_reason_t)reason);
    if (name && name[0] != '\0') return name;
    return "UNKNOWN_REASON";
  }

  String macToString(const uint8_t mac[6]) const {
    if (!mac) return "";
    char b[20];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(b);
  }

  void applyLegacyStaCompatibilityProfile(const String& ssid, const String& pass) {
    // Część domowych routerów (w tym niektóre FunBox) bywa problematyczna
    // przy nowocześniejszych negocjacjach bezpieczeństwa.
    esp_err_t perr = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    if (perr != ESP_OK) {
      Serial.print("[WiFi] WARN set_protocol failed: ");
      Serial.println((int)perr);
    }

    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));

    // Ustaw pełną konfigurację STA jawnie, żeby uniknąć stanu "sta is connecting"
    // oraz mieć pełną kontrolę nad parametrami kompatybilności.
    strncpy((char*)conf.sta.ssid, ssid.c_str(), sizeof(conf.sta.ssid) - 1);
    strncpy((char*)conf.sta.password, pass.c_str(), sizeof(conf.sta.password) - 1);
    conf.sta.channel = 0;
    conf.sta.listen_interval = 0;
    conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    conf.sta.threshold.rssi = -127;

    // Dla maksymalnej kompatybilności:
    // - PMF nie jest wymagane ani deklarowane jako "capable".
    // - auth threshold otwarte (nie blokuj etapowo).
    conf.sta.pmf_cfg.capable = false;
    conf.sta.pmf_cfg.required = false;
    conf.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_err_t serr = esp_wifi_set_config(WIFI_IF_STA, &conf);
    if (serr != ESP_OK) {
      Serial.print("[WiFi] WARN set_config failed: ");
      Serial.println((int)serr);
      return;
    }
    Serial.println("[WiFi] Applied legacy STA compatibility profile (PMF off, BGN, auth threshold open).");
  }

  void ensureWiFiEventHandler() {
    if (wifiEventRegistered) return;
    // W callbacku eventu WiFi zapisujemy tylko lekkie flagi/stan.
    // Logowanie i Stringi robimy później w loop(), poza taskiem WiFi.
    WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t info) {
      wifiDisconnectEventReason = (int)info.wifi_sta_disconnected.reason;
      wifiDisconnectEventPending = true;
      wifiStaLinkUp = false;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t) {
      wifiStaConnectedEventPending = true;
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent([this](WiFiEvent_t, WiFiEventInfo_t) {
      wifiGotIpEventPending = true;
      wifiStaLinkUp = true;
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    wifiEventRegistered = true;
  }

  void processPendingWiFiEvents() {
    const bool wasConnecting = wifiConnecting;
    const bool hadStaConnected = wifiStaConnectedEventPending;
    if (hadStaConnected) wifiStaConnectedEventPending = false;

    const bool hadDisconnected = wifiDisconnectEventPending;
    int disconnectReason = -1;
    if (hadDisconnected) {
      disconnectReason = wifiDisconnectEventReason;
      wifiDisconnectEventPending = false;
    }

    const bool hadGotIp = wifiGotIpEventPending;
    if (hadGotIp) wifiGotIpEventPending = false;

    if (hadStaConnected) {
      Serial.println("[WiFi] STA connected (link up, waiting for IP).");
    }

    if (hadDisconnected) {
      lastDisconnectReasonCode = disconnectReason;
      wifiConfigured = false;
      wifiConnecting = false;
      wifiLastGotIpMs = 0;
      wifiLastDisconnectMs = millis();
      wifiNextRetryMs = wifiLastDisconnectMs + kC6RetryIntervalMs;
      String reasonText = wifiDisconnectReasonText(disconnectReason);
      lastWiFiError = "WiFi disconnected: " + String(disconnectReason) + " (" + reasonText + ")";
      Serial.print("[WiFi] STA disconnected, reason=");
      Serial.print(disconnectReason);
      Serial.print(" (");
      Serial.print(reasonText);
      Serial.println(")");
      if (!inAPMode && !wifiManuallyDisconnected && wasConnecting) {
        if (failedWiFiAttempts < 250) failedWiFiAttempts++;
        if (failedWiFiAttempts >= maxWiFiAttempts) {
          Serial.println("[WiFi] Too many failures, switching to AP mode!");
          setupWiFiAPMode();
          return;
        }
      }
    }

    if (hadGotIp) {
      wifiLastGotIpMs = millis();
      lastDisconnectReasonCode = -1;
      wifiConnecting = false;
      wifiConfigured = true;
      inAPMode = false;
      wifiManuallyDisconnected = false;
      failedWiFiAttempts = 0;
      wifiEverConnected = true;
      wifiRadioPrepared = true;
      wifiNextRetryMs = 0;
      apModeStartedMs = 0;
      lastWiFiError = "";
      Serial.print("[WiFi] Connected! IP: ");
      Serial.println(WiFi.localIP());
    }
  }

  void initStatusPins() {
    if (pinsInited) return;
    if (AP_BUTTON_PIN >= 0) pinMode(AP_BUTTON_PIN, INPUT_PULLUP);
#if !defined(CONFIG_IDF_TARGET_ESP32C6)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcAttachChannel(STATUS_LED_PIN, LEDC_FREQ_HZ, LEDC_BITS, LEDC_CH);
#else
    ledcSetup(LEDC_CH, LEDC_FREQ_HZ, LEDC_BITS);
    ledcAttachPin(STATUS_LED_PIN, LEDC_CH);
#endif
    setLed(0);
#else
    ledStateKnown = true;
    ledLastValue = 0;
#endif
    pinsInited = true;
  }

  void setLed(uint8_t v) {
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    (void)v;
    return;
#else
    const uint8_t hw = STATUS_LED_ACTIVE_LOW ? (uint8_t)(255 - v) : v;
    if (ledStateKnown && hw == ledLastValue) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWrite(STATUS_LED_PIN, hw);
#else
    ledcWrite(LEDC_CH, hw);
#endif
    ledLastValue = hw;
    ledStateKnown = true;
#endif
  }

  uint8_t heartbeatLedValue() const {
    // ~70 bpm => 857 ms na "uderzenie", podwójny miękki impuls jak bicie serca.
    const unsigned long periodMs = 857;
    const unsigned long phase = millis() % periodMs;

    auto smoothPulse = [](float x, float start, float dur, float amp) -> float {
      if (x < start || x > start + dur) return 0.0f;
      const float t = (x - start) / dur;            // 0..1
      const float s = sinf(3.14159265f * t);        // miękko: 0->1->0
      return amp * s;
    };

    float b = 0.0f;
    b += smoothPulse((float)phase, 20.0f, 170.0f, 255.0f);   // pierwszy mocniejszy impuls
    b += smoothPulse((float)phase, 255.0f, 160.0f, 150.0f);  // drugi słabszy impuls
    if (b > 255.0f) b = 255.0f;
    if (b < 0.0f) b = 0.0f;
    return (uint8_t)b;
  }

  void updateStatusLed() {
    if (inAPMode) {
      // 180/min => 3 Hz => okres 333ms (szybkie miganie).
      if (millis() - apBlinkTs >= 166) {
        apBlinkTs = millis();
        apBlinkOn = !apBlinkOn;
        setLed(apBlinkOn ? 255 : 0);
      }
      return;
    }

    if (wifiConnecting || WiFi.status() != WL_CONNECTED) {
      setLed(heartbeatLedValue());
      return;
    }

    // Po połączeniu: stałe światło.
    setLed(255);
  }

  bool io0PressedEdge() {
    if (AP_BUTTON_PIN < 0) return false;
    const bool nowState = (digitalRead(AP_BUTTON_PIN) == HIGH); // true = puszczony
    if (nowState != io0LastState) {
      io0DebounceTs = millis();
      io0LastState = nowState;
    }
    if (millis() - io0DebounceTs < 35) return false;
    // Zdarzenie: przejście do stanu wciśniętego (LOW)
    static bool consumedPress = false;
    if (!nowState && !consumedPress) {
      consumedPress = true;
      return true;
    }
    if (nowState) consumedPress = false;
    return false;
  }

  bool apModeWindowExpired() {
    if (!inAPMode) return false;
    if (apModeStartedMs == 0) return false;
    if (!isWiFiConfigured()) return false;
    return (millis() - apModeStartedMs) >= kApModeDurationMs;
  }

public:
  void load() { settings.load(); initStatusPins(); }

  // WiFi
  bool isWiFiConfigured() { return settings.getSSID() != ""; }
  String getSSID() { return settings.getSSID(); }
  String getPass() { return settings.getPass(); }

  // OWM
  String getOwmApiKey() { return settings.getOwmApiKey(); }
  String getOwmLocation() { return settings.getOwmLocation(); }

  // Pushover
  String getPushoverUser() { return settings.getPushoverUser(); }
  String getPushoverToken() { return settings.getPushoverToken(); }
  bool   getEnablePushover() { return settings.getEnablePushover(); }

  // MQTT
  String getMqttServer()    { return settings.getMqttServer(); }
  int    getMqttPort()      { return settings.getMqttPort(); }
  String getMqttUser()      { return settings.getMqttUser(); }
  String getMqttPass()      { return settings.getMqttPass(); }
  String getMqttClientId()  { return settings.getMqttClientId(); }
  bool   getEnableMqtt()    { return settings.getEnableMqtt(); }
  String getMqttTopicBase() { return settings.getMqttTopicBase(); }

  // Automatyka
  bool   getAutoMode() { return settings.getAutoMode(); }
  int    getZoneCount() { return settings.getZoneCount(); }

  // TZ
  String getTimezone() { return settings.getTimezone(); }
  void   setTimezone(const String& tz) { settings.setTimezone(tz); }

  // Pogoda
  bool getEnableWeatherApi() { return settings.getEnableWeatherApi(); }
  int  getWeatherUpdateIntervalMin() { return settings.getWeatherUpdateIntervalMin(); }
  float getIrrigationTempSkipC() { return settings.getIrrigationTempSkipC(); }
  float getIrrigationTempLowMaxC() { return settings.getIrrigationTempLowMaxC(); }
  float getIrrigationTempMidMaxC() { return settings.getIrrigationTempMidMaxC(); }
  float getIrrigationTempHighMaxC() { return settings.getIrrigationTempHighMaxC(); }
  float getIrrigationTempFactorLow() { return settings.getIrrigationTempFactorLow(); }
  float getIrrigationTempFactorMid() { return settings.getIrrigationTempFactorMid(); }
  float getIrrigationTempFactorHigh() { return settings.getIrrigationTempFactorHigh(); }
  float getIrrigationTempFactorVeryHigh() { return settings.getIrrigationTempFactorVeryHigh(); }
  float getIrrigationRainSkipMm() { return settings.getIrrigationRainSkipMm(); }
  float getIrrigationRainHighMinMm() { return settings.getIrrigationRainHighMinMm(); }
  float getIrrigationRainMidMinMm() { return settings.getIrrigationRainMidMinMm(); }
  float getIrrigationRainFactorHigh() { return settings.getIrrigationRainFactorHigh(); }
  float getIrrigationRainFactorMid() { return settings.getIrrigationRainFactorMid(); }
  float getIrrigationRainFactorLow() { return settings.getIrrigationRainFactorLow(); }
  float getIrrigationHumidityHighPercent() { return settings.getIrrigationHumidityHighPercent(); }
  float getIrrigationHumidityFactorHigh() { return settings.getIrrigationHumidityFactorHigh(); }
  float getIrrigationWindSkipKmh() { return settings.getIrrigationWindSkipKmh(); }
  float getIrrigationWindFactor() { return settings.getIrrigationWindFactor(); }
  int getIrrigationPercentMin() { return settings.getIrrigationPercentMin(); }
  int getIrrigationPercentMax() { return settings.getIrrigationPercentMax(); }

  void saveFromJson(JsonDocument& doc) { settings.saveFromJson(doc); }
  void toJson(JsonDocument& doc) { settings.toJson(doc); }
  void applyCloudDefaults(const String& deviceId) { settings.ensureMqttDefaults(deviceId); }

  // WiFi init
  void initWiFi(PushoverClient* pClient = nullptr) {
    pushover = pClient;
    initStatusPins();
    ensureWiFiEventHandler();
    if (!isWiFiConfigured()) {
      setupWiFiAPMode();
    } else {
      connectWiFi(true);
    }
  }

  void connectWiFi(bool startupAttempt = false) {
    initStatusPins();
    wifiManuallyDisconnected = false;
    if (inAPMode) {
      WiFi.softAPdisconnect(true);
      delay(80);
      inAPMode = false;
      apModeStartedMs = 0;
      wifiRadioPrepared = false;
    }

#if defined(CONFIG_IDF_TARGET_ESP32C6)
    const String ssidC6 = getSSID();
    const String passC6 = getPass();
    if (ssidC6.length() == 0) {
      wifiConnecting = false;
      lastWiFiError = "Brak zapisanych danych WiFi";
      return;
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
#if defined(WIFI_POWER_19_5dBm)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
#endif

    if (!wifiRadioPrepared) {
      WiFi.mode(WIFI_STA);
      delay(20);
      wifiRadioPrepared = true;
    }

    wifiConnecting = true;
    wifiConfigured = false;
    wifiStaLinkUp = false;
    lastDisconnectReasonCode = -1;
    wifiAttemptStartedMs = millis();
    wifiNextRetryMs = wifiAttemptStartedMs + kC6RetryIntervalMs;
    lastWiFiError = "";

    Serial.print("[WiFi] C6 STA attempt -> ");
    Serial.print(ssidC6);
    Serial.print(" (startup=");
    Serial.print(startupAttempt ? "yes" : "no");
    Serial.println(")");
    WiFi.begin(ssidC6.c_str(), passC6.c_str());
    return;
#endif

    wifiConnecting = true;

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    // Odpowiednik "output_power", ale ustawiony na maksimum dla stabilnego zasięgu.
#if defined(WIFI_POWER_19_5dBm)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
#endif
    // Szeroka kompatybilność z domowymi routerami.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
    WiFi.mode(WIFI_STA);
    wifiStaLinkUp = false;
    // Wyczyść poprzednią sesję radiową przed nowym połączeniem.
    WiFi.disconnect(false, true);
    delay(250);

    const String ssid = getSSID();
    const String pass = getPass();

    int bestIdx = -1;
    int bestRssi = -200;
    int bestChannel = 0;
    wifi_auth_mode_t bestAuth = WIFI_AUTH_OPEN;
    uint8_t bestBssid[6] = {0};

    // Jednorazowy scan przed łączeniem: wybierz najmocniejszy BSSID dla SSID.
    int n = WiFi.scanNetworks(false, true);
    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) != ssid) continue;
        const int rssi = WiFi.RSSI(i);
        if (rssi > bestRssi) {
          bestRssi = rssi;
          bestIdx = i;
          bestChannel = WiFi.channel(i);
          bestAuth = WiFi.encryptionType(i);
          uint8_t* b = WiFi.BSSID(i);
          if (b) memcpy(bestBssid, b, 6);
        }
      }
    }
    WiFi.scanDelete();

    const unsigned long attemptTimeoutMs = startupAttempt ? 12000UL : 15000UL;
    auto waitForConnect = [this, attemptTimeoutMs]() -> wl_status_t {
      unsigned long startAttemptTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < attemptTimeoutMs) {
        updateStatusLed();
        delay(20);
        if ((millis() - startAttemptTime) % 100 < 20) Serial.print(".");
      }
      return WiFi.status();
    };

    wl_status_t finalStatus = WL_DISCONNECTED;
    lastDisconnectReasonCode = -1;

    if (bestIdx >= 0) {
      Serial.print("[WiFi] Connecting to "); Serial.print(ssid);
      Serial.print(" (pass_len="); Serial.print(pass.length());
      Serial.print(", channel="); Serial.print(bestChannel);
      Serial.print(", auth="); Serial.print((int)bestAuth);
      Serial.print(", bssid="); Serial.print(macToString(bestBssid));
      Serial.println(")");
      WiFi.begin(ssid.c_str(), pass.c_str(), bestChannel, bestBssid, true);
      finalStatus = waitForConnect();
      if (finalStatus != WL_CONNECTED) {
        Serial.println("\n[WiFi] Retry without pinned BSSID/channel...");
        lastDisconnectReasonCode = -1;
        WiFi.disconnect(false, false);
        delay(250);
        WiFi.begin(ssid.c_str(), pass.c_str());
        finalStatus = waitForConnect();
      }
    } else {
      Serial.print("[WiFi] Connecting to "); Serial.print(ssid);
      Serial.print(" (pass_len="); Serial.print(pass.length());
      Serial.println(", network not found in pre-scan)");
      WiFi.begin(ssid.c_str(), pass.c_str());
      finalStatus = waitForConnect();
    }

    const bool canTryLegacyFallback = pass.length() > 0;
    if (finalStatus != WL_CONNECTED && canTryLegacyFallback) {
      Serial.println("\n[WiFi] Retry with legacy compatibility profile...");
      lastDisconnectReasonCode = -1;
      WiFi.disconnect(false, false);
      delay(250);
      applyLegacyStaCompatibilityProfile(ssid, pass);
      esp_wifi_connect();
      finalStatus = waitForConnect();
    }

    wifiConnecting = false;
    if (finalStatus == WL_CONNECTED) {
      wifiConfigured = true; inAPMode = false; failedWiFiAttempts = 0;
      wifiStaLinkUp = true;
      wifiLastGotIpMs = millis();
      Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
      lastWiFiError = "";
    } else {
      wifiConfigured = false;
      String reasonPart = "";
      if (lastDisconnectReasonCode >= 0) {
        reasonPart = ", reason=" + String(lastDisconnectReasonCode) + " (" + wifiDisconnectReasonText(lastDisconnectReasonCode) + ")";
      }
      lastWiFiError = "WiFi connect failed: " + String(wlStatusText(finalStatus)) + reasonPart;
      Serial.print("\n[WiFi] Connection failed! status=");
      Serial.print((int)finalStatus);
      Serial.print(" (");
      Serial.print(wlStatusText(finalStatus));
      Serial.print(")");
      if (lastDisconnectReasonCode >= 0) {
        Serial.print(", reason=");
        Serial.print(lastDisconnectReasonCode);
        Serial.print(" (");
        Serial.print(wifiDisconnectReasonText(lastDisconnectReasonCode));
        Serial.print(")");
      }
      Serial.println();
      failedWiFiAttempts++;
      const bool shouldSwitchToAp = (failedWiFiAttempts >= maxWiFiAttempts);
      if (shouldSwitchToAp) {
        Serial.println("[WiFi] Too many failures, switching to AP mode!");
        setupWiFiAPMode();
      } else {
        Serial.println("[WiFi] Keeping STA mode, next retry in wifiLoop.");
      }
    }
  }

  // Akcje WiFi wywoływane z lokalnego UI TFT (enkoder).
  void uiConnectWiFi() {
    const unsigned long now = millis();
    // Ochrona przed wielokrotnym "Połącz" z powodu drgań/przypadkowych klików.
    if (now - wifiLastUiActionMs < 900UL) return;
    wifiLastUiActionMs = now;
    wifiManuallyDisconnected = false;

    if (!isWiFiConfigured()) {
      lastWiFiError = "Brak zapisanych danych WiFi";
      setupWiFiAPMode();
      return;
    }

    if (inAPMode) {
      // Delikatne wyjście z AP przed próbą STA.
      // Najpierw rozłączamy AP, potem lekka przerwa na ustabilizowanie stosu.
      WiFi.softAPdisconnect(true);
      delay(80);
      inAPMode = false;
      apModeStartedMs = 0;
      wifiRadioPrepared = false;
    }
    failedWiFiAttempts = 0;
    connectWiFi(true);
  }

  void uiDisconnectWiFi() {
    initStatusPins();
    WiFi.setAutoReconnect(false);
    wifiManuallyDisconnected = true;
    wifiConnecting = false;
    wifiConfigured = false;
    wifiStaLinkUp = false;
    wifiLastGotIpMs = 0;
    wifiLastDisconnectMs = millis();
    wifiRadioPrepared = false;
    wifiAttemptStartedMs = 0;
    wifiNextRetryMs = 0;
    apModeStartedMs = 0;
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    if (inAPMode) {
      WiFi.softAPdisconnect(true);
      delay(80);
    }
    inAPMode = false;
    WiFi.mode(WIFI_STA);
    delay(60);
    WiFi.disconnect(false, false);
    delay(80);
#else
    inAPMode = false;
    WiFi.disconnect(false, false);
#endif
    failedWiFiAttempts = 0;
    lastWiFiError = "WiFi rozlaczone z UI";
  }

  void setupWiFiAPMode() {
    initStatusPins();
    inAPMode = true; wifiConfigured = false;
    wifiConnecting = false;
    wifiStaLinkUp = false;
    wifiManuallyDisconnected = false;
    wifiLastGotIpMs = 0;
    wifiLastDisconnectMs = millis();
    wifiRadioPrepared = false;
    wifiAttemptStartedMs = 0;
    wifiNextRetryMs = 0;
    failedWiFiAttempts = 0;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true, false);
    delay(120);
    WiFi.mode(WIFI_AP);
    delay(20);
    String apName = "WMSprinkler-AP";
    WiFi.softAP(apName.c_str(), "12345678");
    Serial.println("[WiFi] AP Mode: " + apName + " IP: " + WiFi.softAPIP().toString());
    apModeStartedMs = millis();
    lastWiFiError = "AP Mode enabled (no WiFi config)";
  }

  void wifiLoop() {
    processPendingWiFiEvents();
    updateStatusLed();

    if (inAPMode && io0PressedEdge()) {
      Serial.println("[WiFi] IO0 pressed in AP mode -> retrying STA connect");
      if (!isWiFiConfigured()) {
        Serial.println("[WiFi] Brak zapisanej konfiguracji WiFi, pozostaję w AP.");
      } else {
        WiFi.softAPdisconnect(true);
        inAPMode = false;
        apModeStartedMs = 0;
        failedWiFiAttempts = 0;
        connectWiFi(true);
      }
    }

    if (inAPMode) {
      if (apModeWindowExpired()) {
        Serial.println("[WiFi] AP window elapsed (5 min) -> retrying STA connect");
        WiFi.softAPdisconnect(true);
        inAPMode = false;
        apModeStartedMs = 0;
        failedWiFiAttempts = 0;
        connectWiFi(false);
      }
      return;
    }

#if defined(CONFIG_IDF_TARGET_ESP32C6)
    if (wifiManuallyDisconnected) return;
    if (!isWiFiConfigured()) return;

    const unsigned long now = millis();
    if (isWiFiOnline()) {
      failedWiFiAttempts = 0;
      wifiConfigured = true;
      return;
    }

    if (wifiConnecting && wifiAttemptStartedMs != 0 &&
        (now - wifiAttemptStartedMs) >= kC6ConnectAttemptTimeoutMs) {
      wifiConnecting = false;
      wifiConfigured = false;
      wifiLastDisconnectMs = now;
      wifiNextRetryMs = now + kC6RetryIntervalMs;
      failedWiFiAttempts++;
      lastWiFiError = "WiFi connect timeout: " + getSSID();
      wifiRadioPrepared = false;
      Serial.print("[WiFi] C6 connect timeout, attempt=");
      Serial.println(failedWiFiAttempts);
      if (failedWiFiAttempts >= maxWiFiAttempts) {
        Serial.println("[WiFi] Too many failures, switching to AP mode!");
        setupWiFiAPMode();
        return;
      }
    }

    if (!wifiConnecting && (wifiNextRetryMs == 0 || now >= wifiNextRetryMs)) {
      Serial.print("[WiFi] C6 reconnect scheduling attempt ");
      Serial.println(failedWiFiAttempts + 1);
      connectWiFi(false);
    }
    return;
#endif

    if (!inAPMode && !wifiManuallyDisconnected && millis() - lastWiFiCheck > 10000UL) {
      lastWiFiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.print("[WiFi] Lost connection! Attempt: "); Serial.println(failedWiFiAttempts + 1);
        connectWiFi(false);
      } else {
        failedWiFiAttempts = 0;
        wifiConfigured = true;
      }
    }
  }

  bool isInAPMode() const { return inAPMode; }
  String getWiFiStatus() const {
    if (inAPMode) return "Tryb AP";
    if (wifiManuallyDisconnected) return "Rozlaczono";
    if (WiFi.status() == WL_CONNECTED) return "Połączono";
    return "Brak połączenia";
  }
  bool isWiFiManuallyDisconnected() const { return wifiManuallyDisconnected; }
  bool isWiFiOnline() const {
    return !inAPMode && wifiStaLinkUp && (WiFi.status() == WL_CONNECTED);
  }
  bool isWiFiStable(unsigned long minStableMs = 1500UL) const {
    if (!isWiFiOnline()) return false;
    if (wifiLastGotIpMs == 0) return false;
    return (millis() - wifiLastGotIpMs) >= minStableMs;
  }
  unsigned long millisSinceLastDisconnect() const {
    if (wifiLastDisconnectMs == 0) return 0xFFFFFFFFUL;
    return millis() - wifiLastDisconnectMs;
  }
  String getWiFiError() const { return lastWiFiError; }
  int getFailedAttempts() const { return failedWiFiAttempts; }

  // Dla PushoverClient (w main.cpp)
  Settings* getSettingsPtr() { return &settings; }
};
