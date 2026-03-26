#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecureBearSSL.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HLW8012.h>
#include <Updater.h>
#include <time.h>
#include <cmath>
#include <cstring>

#ifndef WMS_BWSHP6_CF1_GPIO
#define WMS_BWSHP6_CF1_GPIO 5
#endif

#ifndef WMS_BWSHP6_RELAY_ALT_GPIO
#define WMS_BWSHP6_RELAY_ALT_GPIO 14
#endif

#ifndef WMS_BWSHP6_FORCE_CF1_DEFAULT
#define WMS_BWSHP6_FORCE_CF1_DEFAULT 1
#endif

static constexpr const char* FW_VERSION = "WMS-BWSHP6-1.0.23";

// WYS-01-033-WIFI_V1.4 mapping (BW-SHP6)
static constexpr uint8_t PIN_RELAY = 15;
static constexpr int8_t PIN_RELAY_ALT = WMS_BWSHP6_RELAY_ALT_GPIO;
static constexpr uint8_t PIN_LED = 0;          // active-low
#ifndef WMS_BWSHP6_LED_AUX_GPIO
#define WMS_BWSHP6_LED_AUX_GPIO 2
#endif
static constexpr int8_t PIN_LED_AUX = WMS_BWSHP6_LED_AUX_GPIO; // active-low, optional second LED
#ifndef WMS_BWSHP6_LED_AUX2_GPIO
#define WMS_BWSHP6_LED_AUX2_GPIO 16
#endif
static constexpr int8_t PIN_LED_AUX2 = WMS_BWSHP6_LED_AUX2_GPIO; // active-low, fallback second LED
static constexpr uint8_t PIN_BUTTON = 13;      // active-low
static constexpr uint8_t PIN_BUTTON_ALT = 3;   // fallback for variant boards
static constexpr uint8_t PIN_BUTTON_ALT2 = 2;  // fallback for variant boards
static constexpr uint8_t PIN_BUTTON_ALT3 = 16; // fallback for variant boards
static constexpr uint8_t PIN_BUTTON_ALT4 = 0;  // fallback: shared with primary LED on some variants
#ifndef WMS_BWSHP6_ENABLE_BUTTON_FALLBACKS
#define WMS_BWSHP6_ENABLE_BUTTON_FALLBACKS 0
#endif
static constexpr uint8_t PIN_HLW_CF = 5;
static constexpr uint8_t PIN_HLW_CF1_DEFAULT = WMS_BWSHP6_CF1_GPIO;  // v1.4 default: 4
static constexpr uint8_t PIN_HLW_SEL = 12;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t MQTT_RETRY_MS = 1500;
static constexpr uint32_t MQTT_LOOP_INTERVAL_MS = 60;
static constexpr uint32_t MQTT_POST_CONNECT_SYNC_DELAY_MS = 1500;
static constexpr uint16_t MQTT_BUFFER_SIZE = 1024;
static constexpr size_t MQTT_JSON_BUFFER_SIZE = 1024;
static constexpr uint32_t MQTT_MIN_HEAP_FOR_PUBLISH = 2400;
static constexpr uint32_t STATUS_PUBLISH_MS = 10000;
static constexpr uint32_t LOG_FLUSH_MS = 15000;
static constexpr uint32_t ENERGY_FLUSH_MS = 60000;
static constexpr uint32_t BUTTON_PAIR_PRESS_MS = 3000;
static constexpr uint32_t BUTTON_FACTORY_RESET_MS = 10000;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 12;
static constexpr uint32_t BUTTON_MIN_CLICK_MS = 20;
static constexpr uint32_t BUTTON_PRIMARY_PULSE_HOLD_MS = 70;
static constexpr uint32_t BUTTON_PRIMARY_CONFIRM_LOW_MS = 20;
static constexpr uint32_t BUTTON_AUTO_TAP_RELEASE_MS = 220;
static constexpr uint32_t PAIRING_TIMEOUT_MS = 15UL * 60UL * 1000UL;
// Strobe (no WiFi): "policy light" effect.
// 62 ms + 62 ms => ~8.06 flashes/s per LED (naprzemiennie, bez przerwy).
static constexpr uint32_t LED_STROBE_HALF_MS = 62;
static constexpr uint32_t LED_STROBE_CYCLE_MS = LED_STROBE_HALF_MS * 2UL;
static constexpr uint32_t LED_WIFI_OK_TOTAL_MS = 3000;
static constexpr uint8_t LED_WIFI_OK_BLINKS = 10;
static constexpr uint32_t LED_WIFI_OK_CYCLE_MS = LED_WIFI_OK_TOTAL_MS / LED_WIFI_OK_BLINKS;
static constexpr uint32_t LED_WIFI_OK_ON_MS = 120;
static constexpr uint32_t LED_AP_HEART_CYCLE_MS = 1600;
static constexpr uint32_t TELEMETRY_DEBUG_MS = 3000;
static constexpr uint8_t WIFI_MAX_FAILS_BEFORE_PAIRING = 3;
static constexpr uint32_t WIFI_NO_SSID_SCAN_INTERVAL_MS = 120000;
static constexpr uint32_t AP_MODE_WINDOW_MS = 5UL * 60UL * 1000UL;

static constexpr float TELEMETRY_MAX_POWER_W = 5000.0f;
static constexpr float TELEMETRY_MAX_CURRENT_A = 20.0f;
static constexpr float TELEMETRY_MIN_VOLTAGE_V = 80.0f;
static constexpr float TELEMETRY_MAX_VOLTAGE_V = 280.0f;
static constexpr float TELEMETRY_IDLE_POWER_W = 3.5f;
static constexpr float TELEMETRY_IDLE_CURRENT_A = 0.05f;

static constexpr const char* DEFAULT_MQTT_SERVER = "wmsprinkler.pl";
static constexpr int DEFAULT_MQTT_PORT = 8883;
static constexpr const char* DEFAULT_MQTT_USER = "wms_device";
static constexpr const char* DEFAULT_MQTT_PASS = "9521mycode";
static constexpr const char* DEFAULT_TZ = "Europe/Warsaw";

static constexpr int MAX_PROGRAMS = 24;
static constexpr int MAX_LOG_LINES = 12;
static constexpr size_t MAX_LOG_LINE_CHARS = 120;

struct DeviceIdentity {
  String deviceId;
  String claimCode;
};

struct ProgramEntry {
  bool used = false;
  uint8_t zone = 0;
  String time = "06:00";
  uint16_t duration = 10; // minutes
  String days = "0,1,2,3,4,5,6";
  bool active = true;
  time_t lastRun = 0;
};

struct AppSettings {
  String ssid;
  String pass;

  bool enableMqtt = true;
  String mqttServer = DEFAULT_MQTT_SERVER;
  int mqttPort = DEFAULT_MQTT_PORT;
  String mqttUser = DEFAULT_MQTT_USER;
  String mqttPass = DEFAULT_MQTT_PASS;
  String mqttClientId;
  String mqttTopicBase;

  bool autoMode = true;
  String timezone = DEFAULT_TZ;

  uint16_t telemetryIntervalSec = 10;
  bool relayInverted = false;
  String zoneName = "Gniazdko";
  bool assignedToWms = false;
  String assignedAccount;

  uint8_t cf1Pin = PIN_HLW_CF1_DEFAULT;
  float hlwVoltageMultiplier = 0.0f;
  float hlwCurrentMultiplier = 0.0f;
  float hlwPowerMultiplier = 0.0f;
};

enum class PairState : uint8_t {
  UNASSIGNED = 0,
  PAIRING = 1,
  ASSIGNED = 2,
};

struct TelemetryState {
  float voltage = 0.0f;
  float current = 0.0f;
  float activePower = 0.0f;
  float apparentPower = 0.0f;
  float reactivePower = 0.0f;
  float powerFactor = 0.0f;

  float energyTotalWh = 0.0f;
  float energyTodayWh = 0.0f;
  float energySessionWh = 0.0f;
  float sessionStartWh = 0.0f;

  int dayOfYear = -1;
  int year = -1;
  unsigned long lastSampleMs = 0;
  unsigned long lastModeToggleMs = 0;
  bool modeCurrent = true;
};

struct OtaRequest {
  bool pending = false;
  String commandId;
  String commandTopic;
  String campaignId;
  String firmwareId;
  String version;
  String url;
  String sha256;
  String signature;
  String signatureAlg;
  String target = "firmware";
  int expectedSize = 0;
};

ESP8266WebServer server(80);
BearSSL::WiFiClientSecure tlsClient;
PubSubClient mqttClient(tlsClient);
HLW8012 hlw;

DeviceIdentity identity;
AppSettings settings;
TelemetryState telemetry;

ProgramEntry programs[MAX_PROGRAMS];
int programCount = 0;

String logsBuffer[MAX_LOG_LINES];
int logsCount = 0;
bool logsDirty = false;

bool relayOn = false;
unsigned long relayEndAtMs = 0;
bool ledPrimaryLogicalOn = false;
bool ledAuxLogicalOn = false;
unsigned long buttonSharedProbeStampMs = ULONG_MAX;
int buttonSharedProbePrimaryRaw = HIGH;
int buttonSharedProbeAuxRaw = HIGH;
int buttonSharedProbeAux2Raw = HIGH;

unsigned long lastWifiRetryMs = 0;
unsigned long lastMqttRetryMs = 0;
unsigned long lastStatusPublishMs = 0;
unsigned long lastTelemetryPublishMs = 0;
unsigned long lastSchedulerCheckMs = 0;
unsigned long lastSchedulerNoTimeLogMs = 0;
unsigned long lastLogFlushMs = 0;
unsigned long lastEnergyFlushMs = 0;
unsigned long mqttLastErrorLogMs = 0;
unsigned long mqttLastLoopMs = 0;
unsigned long mqttPostConnectAtMs = 0;
unsigned long mqttLowHeapLogMs = 0;

bool apEnabled = false;
String apSsid;
unsigned long apModeRetryAtMs = 0;
PairState pairState = PairState::UNASSIGNED;
unsigned long pairingStartedMs = 0;
unsigned long wifiConnectedBlinkStartMs = 0;
bool wifiConnectedPrev = false;
bool wifiConnectedBlinkDone = false;
unsigned long serialHeartbeatMs = 0;
unsigned long telemetryDebugLastMs = 0;
uint8_t wifiFailedAttempts = 0;
unsigned long wifiLastNoSsidScanMs = 0;
wl_status_t wifiLastObservedStatus = WL_IDLE_STATUS;
bool mqttConnectedFlag = false;
bool mqttPostConnectSyncPending = false;
bool schedulerAutoModeOffLogged = false;
OtaRequest ota;
bool otaInProgress = false;

bool buttonStablePressed = false;
bool buttonLastReading = false;
unsigned long buttonLastDebounceMs = 0;
unsigned long buttonPressStartMs = 0;
bool buttonLongHandled = false;
bool buttonPrimarySeenActivity = false;
bool buttonPrimaryConfirmedLow = false;
int buttonPrimaryLastRaw = HIGH;
unsigned long buttonPrimaryLowSinceMs = 0;
unsigned long buttonPrimaryPulseHoldUntilMs = 0;
volatile bool buttonPrimaryIrqLowSeen = false;
volatile uint32_t buttonPrimaryIrqLastUs = 0;

static bool toBoolFlexible(JsonVariantConst v, bool fallback) {
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<int>()) return v.as<int>() != 0;
  if (v.is<const char*>()) {
    const String s = String(v.as<const char*>());
    if (s.equalsIgnoreCase("1") || s.equalsIgnoreCase("true") || s.equalsIgnoreCase("on") || s.equalsIgnoreCase("yes")) return true;
    if (s.equalsIgnoreCase("0") || s.equalsIgnoreCase("false") || s.equalsIgnoreCase("off") || s.equalsIgnoreCase("no")) return false;
  }
  return fallback;
}

static String nowTimestamp() {
  time_t now = time(nullptr);
  if (now < 100000) {
    const unsigned long sec = millis() / 1000UL;
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "uptime:%lu", sec);
    return String(tmp);
  }
  struct tm t {};
  localtime_r(&now, &t);
  char out[80];
  snprintf(out, sizeof(out), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  return String(out);
}

static String wifiIpOrDash() {
  if (WiFi.status() != WL_CONNECTED) return "-";
  return WiFi.localIP().toString();
}

static String sanitizeTopicBase(String s) {
  s.trim();
  while (s.endsWith("/")) s.remove(s.length() - 1);
  return s;
}

static String baseTopic() {
  if (settings.mqttTopicBase.length() > 0) {
    return sanitizeTopicBase(settings.mqttTopicBase);
  }
  return String("wms/") + identity.deviceId;
}

static String topicLeaf(const String& leaf) {
  return baseTopic() + "/" + leaf;
}

static String formatDeviceIdFromChip() {
  const uint32_t chip = ESP.getChipId();
  const uint32_t suffix = chip % 100000UL;
  char out[16];
  snprintf(out, sizeof(out), "WMS_%05lu", static_cast<unsigned long>(suffix));
  return String(out);
}

static void addLogLine(const String& message) {
  String msg = message;
  if (msg.length() > MAX_LOG_LINE_CHARS) {
    msg = msg.substring(0, MAX_LOG_LINE_CHARS);
    msg += "...";
  }
  const String line = nowTimestamp() + ": " + msg;
  if (ESP.getFreeHeap() < 6000) {
    Serial.println(line);
    return;
  }
  if (logsCount < MAX_LOG_LINES) {
    logsBuffer[logsCount++] = line;
  } else {
    for (int i = 1; i < MAX_LOG_LINES; ++i) logsBuffer[i - 1] = logsBuffer[i];
    logsBuffer[MAX_LOG_LINES - 1] = line;
  }
  logsDirty = true;
  Serial.println(line);
}

static const char* pairStateLabel(PairState s) {
  switch (s) {
    case PairState::PAIRING: return "pairing";
    case PairState::ASSIGNED: return "assigned";
    case PairState::UNASSIGNED:
    default: return "unassigned";
  }
}

static const char* pairStateLabelForExternal() {
  if (
    pairState == PairState::UNASSIGNED
    && settings.ssid.length() > 0
    && WiFi.status() == WL_CONNECTED
  ) {
    return "discoverable";
  }
  return pairStateLabel(pairState);
}

static const char* wifiStatusLabel(wl_status_t st) {
  switch (st) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_WRONG_PASSWORD: return "WRONG_PASSWORD";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    default: return "UNKNOWN";
  }
}

static const char* httpMethodLabel(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "OTHER";
  }
}

static void serialHttpRequest(const char* tag) {
  Serial.printf(
    "[HTTP] %s %s %s args=%u hasPlain=%d\r\n",
    tag,
    httpMethodLabel(server.method()),
    server.uri().c_str(),
    static_cast<unsigned>(server.args()),
    server.hasArg("plain") ? 1 : 0
  );
}

static void serialBoot(const String& msg) {
  Serial.println(String("[BOOT] ") + msg);
}

static void IRAM_ATTR onPrimaryButtonEdgeISR() {
  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - buttonPrimaryIrqLastUs) < 25000UL) return;
  if (digitalRead(PIN_BUTTON) == LOW) {
    buttonPrimaryIrqLastUs = nowUs;
    buttonPrimaryIrqLowSeen = true;
  }
}

static bool auxLedEnabled() {
  return PIN_LED_AUX >= 0 && PIN_LED_AUX != static_cast<int8_t>(PIN_LED);
}

static bool auxLed2Enabled() {
  return PIN_LED_AUX2 >= 0
    && PIN_LED_AUX2 != static_cast<int8_t>(PIN_LED)
    && PIN_LED_AUX2 != PIN_LED_AUX;
}

static bool pinReservedForAuxLed(uint8_t pin) {
  const int8_t p = static_cast<int8_t>(pin);
  if (auxLedEnabled() && p == PIN_LED_AUX) return true;
  if (auxLed2Enabled() && p == PIN_LED_AUX2) return true;
  return false;
}

static bool pinIsAnyLedPin(uint8_t pin) {
  const int8_t p = static_cast<int8_t>(pin);
  if (p == static_cast<int8_t>(PIN_LED)) return true;
  if (auxLedEnabled() && p == PIN_LED_AUX) return true;
  if (auxLed2Enabled() && p == PIN_LED_AUX2) return true;
  return false;
}

static bool sharedButtonProbeAllowed() {
  return !apEnabled && WiFi.status() == WL_CONNECTED;
}

static int probeSharedLedPinImmediate(int8_t pin, bool restoreOn) {
  if (pin < 0) return HIGH;
  const uint8_t upin = static_cast<uint8_t>(pin);
  pinMode(upin, INPUT_PULLUP);
  delayMicroseconds(70);
  const int raw = digitalRead(upin);
  pinMode(upin, OUTPUT);
  digitalWrite(upin, restoreOn ? LOW : HIGH);
  return raw;
}

static void refreshSharedButtonProbe() {
  if (!sharedButtonProbeAllowed()) return;
  const unsigned long now = millis();
  if (buttonSharedProbeStampMs == now) return;
  buttonSharedProbeStampMs = now;

  buttonSharedProbePrimaryRaw = probeSharedLedPinImmediate(static_cast<int8_t>(PIN_LED), ledPrimaryLogicalOn);
  if (auxLedEnabled()) {
    buttonSharedProbeAuxRaw = probeSharedLedPinImmediate(PIN_LED_AUX, ledAuxLogicalOn);
  } else {
    buttonSharedProbeAuxRaw = HIGH;
  }
  if (auxLed2Enabled()) {
    buttonSharedProbeAux2Raw = probeSharedLedPinImmediate(PIN_LED_AUX2, ledAuxLogicalOn);
  } else {
    buttonSharedProbeAux2Raw = HIGH;
  }
}

static int readSharedLedProbeRaw(uint8_t pin) {
  if (!sharedButtonProbeAllowed()) return HIGH;
  refreshSharedButtonProbe();
  const int8_t p = static_cast<int8_t>(pin);
  if (p == static_cast<int8_t>(PIN_LED)) return buttonSharedProbePrimaryRaw;
  if (auxLedEnabled() && p == PIN_LED_AUX) return buttonSharedProbeAuxRaw;
  if (auxLed2Enabled() && p == PIN_LED_AUX2) return buttonSharedProbeAux2Raw;
  return HIGH;
}

#if WMS_BWSHP6_ENABLE_BUTTON_FALLBACKS
static bool readButtonPinPressed(uint8_t pin) {
  if (pinIsAnyLedPin(pin) || pinReservedForAuxLed(pin)) {
    return readSharedLedProbeRaw(pin) == LOW;
  }
  return digitalRead(pin) == LOW;
}
#endif

static int readButtonPinRaw(uint8_t pin) {
  if (pinIsAnyLedPin(pin) || pinReservedForAuxLed(pin)) {
    return readSharedLedProbeRaw(pin);
  }
  return digitalRead(pin);
}

static bool readButtonPressedRaw() {
  bool irqLow = false;
  noInterrupts();
  irqLow = buttonPrimaryIrqLowSeen;
  buttonPrimaryIrqLowSeen = false;
  interrupts();

  const unsigned long now = millis();
  if (irqLow && !buttonStablePressed) {
    buttonPrimaryPulseHoldUntilMs = now + BUTTON_PRIMARY_PULSE_HOLD_MS;
  }

  const int primaryRaw = readButtonPinRaw(PIN_BUTTON);
  if (primaryRaw != buttonPrimaryLastRaw) {
    buttonPrimaryLastRaw = primaryRaw;
  }

  if (primaryRaw == LOW) {
    if (buttonPrimaryLowSinceMs == 0) {
      buttonPrimaryLowSinceMs = now;
    }
  } else {
    buttonPrimaryLowSinceMs = 0;
  }

  const bool confirmedRawPress =
    (buttonPrimaryLowSinceMs > 0)
    && ((long)(now - buttonPrimaryLowSinceMs) >= static_cast<long>(BUTTON_PRIMARY_CONFIRM_LOW_MS));
  buttonPrimaryConfirmedLow = confirmedRawPress;
  if (confirmedRawPress) {
    buttonPrimarySeenActivity = true;
  }

  const bool primaryPressed = confirmedRawPress || ((long)(buttonPrimaryPulseHoldUntilMs - now) > 0);
  if (buttonPrimarySeenActivity) return primaryPressed;

#if WMS_BWSHP6_ENABLE_BUTTON_FALLBACKS
  const bool altPressed = readButtonPinPressed(PIN_BUTTON_ALT);
  const bool alt2Pressed = readButtonPinPressed(PIN_BUTTON_ALT2);
  const bool alt3Pressed = readButtonPinPressed(PIN_BUTTON_ALT3);
  const bool alt4Pressed = readButtonPinPressed(PIN_BUTTON_ALT4);
  return primaryPressed || altPressed || alt2Pressed || alt3Pressed || alt4Pressed;
#else
  return primaryPressed;
#endif
}

static void serialHeartbeat() {
  const unsigned long now = millis();
  if (now - serialHeartbeatMs < 5000UL) return;
  serialHeartbeatMs = now;
  const int btnPrimaryRaw = readButtonPinRaw(PIN_BUTTON);
  const int btnAltRaw = readButtonPinRaw(PIN_BUTTON_ALT);
  const int btnAlt2Raw = readButtonPinRaw(PIN_BUTTON_ALT2);
  const int btnAlt3Raw = readButtonPinRaw(PIN_BUTTON_ALT3);
  const int btnAlt4Raw = readButtonPinRaw(PIN_BUTTON_ALT4);
  const int relayPinRaw = digitalRead(PIN_RELAY);
  const int relayAltRaw = (PIN_RELAY_ALT >= 0) ? digitalRead(static_cast<uint8_t>(PIN_RELAY_ALT)) : -1;
  const int mqttConn = mqttConnectedFlag ? 1 : 0;
  const int mqttState = mqttClient.state();
  const uint32_t freeHeap = ESP.getFreeHeap();
  Serial.printf(
    "[HB] ms=%lu heap=%lu dev=%s pair=%s wifi=%d(%s) mqtt=%d(%d) ap=%d relay=%d ssid_len=%u rp%u=%d ra%d=%d btn%u=%d btn%u=%d btn%u=%d btn%u=%d btn%u=%d ip=%s p=%.2fW v=%.1fV i=%.3fA\r\n",
    now,
    static_cast<unsigned long>(freeHeap),
    identity.deviceId.c_str(),
    pairStateLabel(pairState),
    static_cast<int>(WiFi.status()),
    wifiStatusLabel(WiFi.status()),
    mqttConn,
    mqttState,
    apEnabled ? 1 : 0,
    relayOn ? 1 : 0,
    static_cast<unsigned>(settings.ssid.length()),
    static_cast<unsigned>(PIN_RELAY),
    relayPinRaw,
    static_cast<int>(PIN_RELAY_ALT),
    relayAltRaw,
    static_cast<unsigned>(PIN_BUTTON),
    btnPrimaryRaw,
    static_cast<unsigned>(PIN_BUTTON_ALT),
    btnAltRaw,
    static_cast<unsigned>(PIN_BUTTON_ALT2),
    btnAlt2Raw,
    static_cast<unsigned>(PIN_BUTTON_ALT3),
    btnAlt3Raw,
    static_cast<unsigned>(PIN_BUTTON_ALT4),
    btnAlt4Raw,
    wifiIpOrDash().c_str(),
    telemetry.activePower,
    telemetry.voltage,
    telemetry.current
  );
}

static void setLedPinDigital(int8_t pin, bool on) {
  if (pin < 0) return;
  pinMode(static_cast<uint8_t>(pin), OUTPUT);
  digitalWrite(static_cast<uint8_t>(pin), on ? LOW : HIGH);
}

static void setLedPatternState(bool primaryOn, bool auxOn) {
  ledPrimaryLogicalOn = primaryOn;
  ledAuxLogicalOn = auxOn;
  setLedPinDigital(static_cast<int8_t>(PIN_LED), primaryOn);
  if (auxLedEnabled()) {
    setLedPinDigital(PIN_LED_AUX, auxOn);
  }
  if (auxLed2Enabled()) {
    setLedPinDigital(PIN_LED_AUX2, auxOn);
  }
}

static void setPairState(PairState next, const String& reason = "") {
  if (pairState == next) return;
  pairState = next;
  if (next == PairState::PAIRING) {
    pairingStartedMs = millis();
  } else {
    pairingStartedMs = 0;
    setLedPatternState(false, false);
  }
  if (reason.length() > 0) addLogLine("Pair state -> " + String(pairStateLabel(next)) + " (" + reason + ")");
}

static void updateLedPattern() {
  const unsigned long now = millis();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;

  // AP mode: "heartbeat" pulse (double pulse).
  if (apEnabled) {
    const uint32_t cycle = LED_AP_HEART_CYCLE_MS;
    const uint32_t phase = now % cycle;
    const bool beat = (phase < 120UL) || (phase >= 240UL && phase < 360UL);
    setLedPatternState(beat, false);
    return;
  }

  if (wifiConnected && !wifiConnectedPrev) {
    wifiConnectedPrev = true;
    wifiConnectedBlinkStartMs = now;
    wifiConnectedBlinkDone = false;
  } else if (!wifiConnected) {
    wifiConnectedPrev = false;
    wifiConnectedBlinkStartMs = 0;
    wifiConnectedBlinkDone = false;
  }

  // STA connected: fast confirmation 10x / 3s, then LED follows relay state.
  if (wifiConnected) {
    if (!wifiConnectedBlinkDone && wifiConnectedBlinkStartMs > 0) {
      const unsigned long elapsed = now - wifiConnectedBlinkStartMs;
      if (elapsed < LED_WIFI_OK_TOTAL_MS) {
        const bool on = (elapsed % LED_WIFI_OK_CYCLE_MS) < LED_WIFI_OK_ON_MS;
        setLedPatternState(on, false);
        return;
      }
      wifiConnectedBlinkDone = true;
    }
    setLedPatternState(relayOn, false);
    return;
  }

  // No WiFi credentials/connection: fast alternating strobe.
  if (auxLedEnabled() || auxLed2Enabled()) {
    const uint32_t phase = now % LED_STROBE_CYCLE_MS;
    const bool primaryOn = phase < LED_STROBE_HALF_MS;
    const bool auxOn = !primaryOn;
    setLedPatternState(primaryOn, auxOn);
  } else {
    const uint32_t phase = now % LED_STROBE_CYCLE_MS;
    setLedPatternState(phase < LED_STROBE_HALF_MS, false);
  }
}

static void setRelayOutput(bool on) {
  relayOn = on;
  const bool relayLevel = settings.relayInverted ? !on : on;
  digitalWrite(PIN_RELAY, relayLevel ? HIGH : LOW);
  if (PIN_RELAY_ALT >= 0) {
    digitalWrite(static_cast<uint8_t>(PIN_RELAY_ALT), relayLevel ? HIGH : LOW);
  }

  const int pinLevel = digitalRead(PIN_RELAY);
  const int altLevel = (PIN_RELAY_ALT >= 0) ? digitalRead(static_cast<uint8_t>(PIN_RELAY_ALT)) : -1;
  addLogLine(
    "Relay output -> logical=" + String(on ? 1 : 0)
    + ", level=" + String(relayLevel ? 1 : 0)
    + ", inv=" + String(settings.relayInverted ? 1 : 0)
    + ", pin" + String(PIN_RELAY) + "=" + String(pinLevel)
    + ", pin" + String(PIN_RELAY_ALT) + "=" + String(altLevel)
  );

  if (on) {
    telemetry.sessionStartWh = telemetry.energyTotalWh;
  } else {
    telemetry.energySessionWh = 0.0f;
  }
}

static int relayRemainingSeconds() {
  if (!relayOn) return 0;
  if (relayEndAtMs == 0) return 0;
  const long rem = static_cast<long>((relayEndAtMs - millis()) / 1000UL);
  return rem > 0 ? static_cast<int>(rem) : 0;
}

static void setRelayState(bool on, uint32_t durationSec, const String& reason) {
  if (on) {
    setRelayOutput(true);
    relayEndAtMs = durationSec > 0 ? millis() + (durationSec * 1000UL) : 0;
    if (durationSec > 0) {
      addLogLine("Relay ON (" + reason + ") na " + String(durationSec) + " s");
    } else {
      addLogLine("Relay ON (" + reason + ")");
    }
  } else {
    setRelayOutput(false);
    relayEndAtMs = 0;
    addLogLine("Relay OFF (" + reason + ")");
  }
}

static bool parseDayInCsv(const String& csv, int day) {
  int pos = 0;
  while (pos < static_cast<int>(csv.length())) {
    const int comma = csv.indexOf(',', pos);
    const int end = (comma < 0) ? csv.length() : comma;
    const String token = csv.substring(pos, end);
    if (token.toInt() == day) return true;
    if (comma < 0) break;
    pos = comma + 1;
  }
  return false;
}

static bool parseTimeString(const String& in, int& hourOut, int& minOut) {
  if (in.length() != 5 || in.charAt(2) != ':') return false;
  const int hh = in.substring(0, 2).toInt();
  const int mm = in.substring(3, 5).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  hourOut = hh;
  minOut = mm;
  return true;
}

static String daysArrayToCsv(JsonArrayConst arr) {
  String out;
  for (JsonVariantConst v : arr) {
    int d = -1;
    if (v.is<int>()) d = v.as<int>();
    else if (v.is<const char*>()) d = String(v.as<const char*>()).toInt();
    if (d < 0 || d > 6) continue;
    if (out.length() > 0) out += ",";
    out += String(d);
  }
  if (out.length() == 0) out = "0,1,2,3,4,5,6";
  return out;
}

static void csvToDaysArray(const String& csv, JsonArray out) {
  int pos = 0;
  while (pos < static_cast<int>(csv.length())) {
    const int comma = csv.indexOf(',', pos);
    const int end = (comma < 0) ? csv.length() : comma;
    const int d = csv.substring(pos, end).toInt();
    if (d >= 0 && d <= 6) out.add(d);
    if (comma < 0) break;
    pos = comma + 1;
  }
}

static bool parseProgramFromVariant(JsonVariantConst src, ProgramEntry& out) {
  ProgramEntry tmp;

  if (src["zone"].is<int>()) tmp.zone = static_cast<uint8_t>(src["zone"].as<int>());
  if (tmp.zone > 0) tmp.zone = 0;  // smart plug has exactly one zone

  if (src["time"].is<const char*>()) {
    tmp.time = String(src["time"].as<const char*>());
  }
  int hh = 0;
  int mm = 0;
  if (!parseTimeString(tmp.time, hh, mm)) return false;

  if (src["duration"].is<int>()) {
    int v = src["duration"].as<int>();
    if (v < 1) v = 1;
    if (v > 1440) v = 1440;
    tmp.duration = static_cast<uint16_t>(v);
  }

  if (src["active"].is<bool>() || src["active"].is<int>() || src["active"].is<const char*>()) {
    tmp.active = toBoolFlexible(src["active"], true);
  }

  if (src["days"].is<JsonArrayConst>()) {
    tmp.days = daysArrayToCsv(src["days"].as<JsonArrayConst>());
  } else if (src["days"].is<const char*>()) {
    String s = String(src["days"].as<const char*>());
    s.trim();
    if (s.length() > 0) tmp.days = s;
  }

  if (src["lastRun"].is<long>() || src["lastRun"].is<int>()) {
    tmp.lastRun = static_cast<time_t>(src["lastRun"].as<long>());
  }

  out = tmp;
  out.used = true;
  return true;
}

static void defaultSettings() {
  settings = AppSettings();
  settings.mqttClientId = identity.deviceId;
  settings.mqttTopicBase = String("wms/") + identity.deviceId;
}

static void saveIdentity() {
  JsonDocument doc;
  doc["device_id"] = identity.deviceId;
  doc["claim_code"] = identity.claimCode;
  File f = LittleFS.open("/device-identity.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

static void loadIdentity() {
  identity.deviceId = formatDeviceIdFromChip();
  identity.claimCode = identity.deviceId;

  if (!LittleFS.exists("/device-identity.json")) {
    saveIdentity();
    return;
  }

  File f = LittleFS.open("/device-identity.json", "r");
  if (!f) {
    saveIdentity();
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    saveIdentity();
    return;
  }

  const String fileDeviceId = String(doc["device_id"] | "");
  const String fileClaimCode = String(doc["claim_code"] | "");

  if (fileDeviceId.startsWith("WMS_") && fileDeviceId.length() == 9) identity.deviceId = fileDeviceId;
  if (fileClaimCode.startsWith("WMS_") && fileClaimCode.length() == 9) identity.claimCode = fileClaimCode;

  // Keep deterministic code based on chip.
  identity.deviceId = formatDeviceIdFromChip();
  identity.claimCode = identity.deviceId;
  saveIdentity();
}

static void saveSettings() {
  JsonDocument doc;
  doc["ssid"] = settings.ssid;
  doc["pass"] = settings.pass;

  doc["enableMqtt"] = settings.enableMqtt;
  doc["mqttServer"] = settings.mqttServer;
  doc["mqttPort"] = settings.mqttPort;
  doc["mqttUser"] = settings.mqttUser;
  doc["mqttPass"] = settings.mqttPass;
  doc["mqttClientId"] = settings.mqttClientId;
  doc["mqttTopicBase"] = settings.mqttTopicBase;

  doc["autoMode"] = settings.autoMode;
  doc["timezone"] = settings.timezone;
  doc["telemetryIntervalSec"] = settings.telemetryIntervalSec;
  doc["relayInverted"] = settings.relayInverted;
  doc["zoneName"] = settings.zoneName;
  doc["assignedToWms"] = settings.assignedToWms;
  doc["assignedAccount"] = settings.assignedAccount;

  doc["cf1Pin"] = settings.cf1Pin;
  doc["hlwVoltageMultiplier"] = settings.hlwVoltageMultiplier;
  doc["hlwCurrentMultiplier"] = settings.hlwCurrentMultiplier;
  doc["hlwPowerMultiplier"] = settings.hlwPowerMultiplier;

  File f = LittleFS.open("/settings.json", "w");
  if (!f) {
    addLogLine("Błąd zapisu /settings.json (open failed)");
    return;
  }
  const size_t written = serializeJson(doc, f);
  f.close();
  addLogLine(
    "Zapisano /settings.json bytes=" + String(static_cast<unsigned long>(written))
    + " ssid_len=" + String(settings.ssid.length())
    + " pass_len=" + String(settings.pass.length())
    + " relayInv=" + String(settings.relayInverted ? 1 : 0)
    + " cf1=" + String(settings.cf1Pin)
  );
}

static void loadSettings() {
  defaultSettings();
  if (!LittleFS.exists("/settings.json")) {
    addLogLine("/settings.json nie istnieje - zapis domyślnych ustawień");
    saveSettings();
    return;
  }

  File f = LittleFS.open("/settings.json", "r");
  if (!f) {
    addLogLine("Błąd odczytu /settings.json (open failed) - zapis domyślnych");
    saveSettings();
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    addLogLine("Błąd JSON /settings.json: " + String(err.c_str()) + " - zapis domyślnych");
    saveSettings();
    return;
  }

  if (doc["ssid"].is<const char*>()) settings.ssid = String(doc["ssid"].as<const char*>());
  if (doc["pass"].is<const char*>()) settings.pass = String(doc["pass"].as<const char*>());

  if (doc["enableMqtt"].is<bool>() || doc["enableMqtt"].is<int>() || doc["enableMqtt"].is<const char*>()) {
    settings.enableMqtt = toBoolFlexible(doc["enableMqtt"], settings.enableMqtt);
  }
  if (doc["mqttServer"].is<const char*>()) settings.mqttServer = String(doc["mqttServer"].as<const char*>());
  if (doc["mqttPort"].is<int>()) settings.mqttPort = doc["mqttPort"].as<int>();
  if (doc["mqttUser"].is<const char*>()) settings.mqttUser = String(doc["mqttUser"].as<const char*>());
  if (doc["mqttPass"].is<const char*>()) settings.mqttPass = String(doc["mqttPass"].as<const char*>());
  if (doc["mqttClientId"].is<const char*>()) settings.mqttClientId = String(doc["mqttClientId"].as<const char*>());
  if (doc["mqttTopicBase"].is<const char*>()) settings.mqttTopicBase = String(doc["mqttTopicBase"].as<const char*>());

  if (doc["autoMode"].is<bool>() || doc["autoMode"].is<int>() || doc["autoMode"].is<const char*>()) {
    settings.autoMode = toBoolFlexible(doc["autoMode"], settings.autoMode);
  }
  if (doc["timezone"].is<const char*>()) settings.timezone = String(doc["timezone"].as<const char*>());
  if (doc["telemetryIntervalSec"].is<int>()) {
    int sec = doc["telemetryIntervalSec"].as<int>();
    if (sec < 2) sec = 2;
    if (sec > 300) sec = 300;
    settings.telemetryIntervalSec = static_cast<uint16_t>(sec);
  }
  if (doc["relayInverted"].is<bool>() || doc["relayInverted"].is<int>() || doc["relayInverted"].is<const char*>()) {
    settings.relayInverted = toBoolFlexible(doc["relayInverted"], settings.relayInverted);
  }
  if (doc["zoneName"].is<const char*>()) {
    settings.zoneName = String(doc["zoneName"].as<const char*>());
    settings.zoneName.trim();
    if (settings.zoneName.length() == 0) settings.zoneName = "Gniazdko";
  }
  if (doc["assignedToWms"].is<bool>() || doc["assignedToWms"].is<int>() || doc["assignedToWms"].is<const char*>()) {
    settings.assignedToWms = toBoolFlexible(doc["assignedToWms"], settings.assignedToWms);
  }
  if (doc["assignedAccount"].is<const char*>()) {
    settings.assignedAccount = String(doc["assignedAccount"].as<const char*>());
  }

  if (doc["cf1Pin"].is<int>()) {
    const int pin = doc["cf1Pin"].as<int>();
    if (pin == 4 || pin == 5) settings.cf1Pin = static_cast<uint8_t>(pin);
  }
  if (doc["hlwVoltageMultiplier"].is<float>() || doc["hlwVoltageMultiplier"].is<int>()) {
    settings.hlwVoltageMultiplier = doc["hlwVoltageMultiplier"].as<float>();
  }
  if (doc["hlwCurrentMultiplier"].is<float>() || doc["hlwCurrentMultiplier"].is<int>()) {
    settings.hlwCurrentMultiplier = doc["hlwCurrentMultiplier"].as<float>();
  }
  if (doc["hlwPowerMultiplier"].is<float>() || doc["hlwPowerMultiplier"].is<int>()) {
    settings.hlwPowerMultiplier = doc["hlwPowerMultiplier"].as<float>();
  }

  if (settings.mqttClientId.length() == 0) settings.mqttClientId = identity.deviceId;
  if (settings.mqttTopicBase.length() == 0) settings.mqttTopicBase = String("wms/") + identity.deviceId;
  settings.mqttTopicBase = sanitizeTopicBase(settings.mqttTopicBase);

  if (settings.mqttPort <= 0) settings.mqttPort = DEFAULT_MQTT_PORT;
  if (settings.timezone.length() == 0) settings.timezone = DEFAULT_TZ;

  addLogLine(
    "Wczytano /settings.json ssid_len=" + String(settings.ssid.length())
    + " pass_len=" + String(settings.pass.length())
    + " relayInv=" + String(settings.relayInverted ? 1 : 0)
    + " cf1=" + String(settings.cf1Pin)
  );
}

static void savePrograms() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < programCount; ++i) {
    const ProgramEntry& p = programs[i];
    if (!p.used) continue;
    JsonObject o = arr.add<JsonObject>();
    o["zone"] = p.zone;
    o["time"] = p.time;
    o["duration"] = p.duration;
    o["days"] = p.days;
    o["active"] = p.active;
    o["lastRun"] = static_cast<long>(p.lastRun);
  }
  File f = LittleFS.open("/programs.json", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

static void loadPrograms() {
  programCount = 0;
  if (!LittleFS.exists("/programs.json")) {
    savePrograms();
    return;
  }

  File f = LittleFS.open("/programs.json", "r");
  if (!f) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || !doc.is<JsonArrayConst>()) return;

  JsonArrayConst arr = doc.as<JsonArrayConst>();
  for (JsonVariantConst v : arr) {
    if (programCount >= MAX_PROGRAMS) break;
    ProgramEntry p;
    if (!parseProgramFromVariant(v, p)) continue;
    programs[programCount++] = p;
  }
}

static void saveLogs() {
  // ESP8266 has very limited heap; persisting logs as JSON can trigger OOM.
  // Keep logs in RAM + serial output.
  logsDirty = false;
}

static void loadLogs() {
  logsCount = 0;
  addLogLine("Uruchomiono firmware smart plug");
}

static void saveEnergy() {
  JsonDocument doc;
  doc["energyTotalWh"] = telemetry.energyTotalWh;
  doc["energyTodayWh"] = telemetry.energyTodayWh;
  doc["dayOfYear"] = telemetry.dayOfYear;
  doc["year"] = telemetry.year;

  File f = LittleFS.open("/energy.json", "w");
  if (!f) {
    addLogLine("Błąd zapisu /energy.json (open failed)");
    return;
  }
  const size_t written = serializeJson(doc, f);
  f.close();
  Serial.printf("[ENERGY] saved bytes=%u totalWh=%.4f todayWh=%.4f\r\n",
                static_cast<unsigned>(written),
                telemetry.energyTotalWh,
                telemetry.energyTodayWh);
}

static void loadEnergy() {
  telemetry.energyTotalWh = 0.0f;
  telemetry.energyTodayWh = 0.0f;
  telemetry.energySessionWh = 0.0f;
  telemetry.sessionStartWh = 0.0f;

  if (!LittleFS.exists("/energy.json")) {
    saveEnergy();
    return;
  }

  File f = LittleFS.open("/energy.json", "r");
  if (!f) return;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    addLogLine("Błąd JSON /energy.json: " + String(err.c_str()));
    return;
  }

  if (doc["energyTotalWh"].is<float>() || doc["energyTotalWh"].is<int>()) {
    telemetry.energyTotalWh = doc["energyTotalWh"].as<float>();
  }
  if (doc["energyTodayWh"].is<float>() || doc["energyTodayWh"].is<int>()) {
    telemetry.energyTodayWh = doc["energyTodayWh"].as<float>();
  }
  if (doc["dayOfYear"].is<int>()) telemetry.dayOfYear = doc["dayOfYear"].as<int>();
  if (doc["year"].is<int>()) telemetry.year = doc["year"].as<int>();

  if (!isfinite(telemetry.energyTotalWh) || telemetry.energyTotalWh < 0.0f || telemetry.energyTotalWh > 100000000.0f) {
    addLogLine("Nieprawidłowe energyTotalWh w pliku - reset do 0");
    telemetry.energyTotalWh = 0.0f;
  }
  if (!isfinite(telemetry.energyTodayWh) || telemetry.energyTodayWh < 0.0f || telemetry.energyTodayWh > telemetry.energyTotalWh) {
    addLogLine("Nieprawidłowe energyTodayWh w pliku - reset do 0");
    telemetry.energyTodayWh = 0.0f;
  }

  addLogLine(
    "Wczytano /energy.json totalWh=" + String(telemetry.energyTotalWh, 3)
    + " todayWh=" + String(telemetry.energyTodayWh, 3)
  );
}

static void applyBoardProfileMigrations() {
#if WMS_BWSHP6_FORCE_CF1_DEFAULT
  if (settings.cf1Pin != PIN_HLW_CF1_DEFAULT) {
    const uint8_t previousCf1 = settings.cf1Pin;
    settings.cf1Pin = PIN_HLW_CF1_DEFAULT;
    saveSettings();
    addLogLine(
      "Migracja profilu PCB: cf1Pin " + String(previousCf1)
      + " -> " + String(settings.cf1Pin)
    );

    telemetry.energyTotalWh = 0.0f;
    telemetry.energyTodayWh = 0.0f;
    telemetry.energySessionWh = 0.0f;
    telemetry.sessionStartWh = 0.0f;
    saveEnergy();
    addLogLine("Migracja profilu PCB: reset liczników energii");
  }
#endif
}

static String offsetToPosixTZ(const String& tz) {
  if (tz.length() < 2) return "";
  char s = tz.charAt(0);
  if (s != '+' && s != '-') return "";

  int hh = 0;
  int mm = 0;
  const int colon = tz.indexOf(':');
  String h = (colon > 0) ? tz.substring(1, colon) : tz.substring(1);
  String m = (colon > 0) ? tz.substring(colon + 1) : "";
  if (h.length() == 0) return "";
  for (size_t i = 0; i < h.length(); ++i) if (!isDigit(h[i])) return "";
  hh = h.toInt();
  if (hh < 0 || hh > 23) return "";
  if (m.length() > 0) {
    for (size_t i = 0; i < m.length(); ++i) if (!isDigit(m[i])) return "";
    mm = m.toInt();
    if (mm < 0 || mm > 59) return "";
  }

  char out[24];
  const char sign = (s == '+') ? '-' : '+';
  if (mm > 0) snprintf(out, sizeof(out), "UTC%c%02d:%02d", sign, hh, mm);
  else snprintf(out, sizeof(out), "UTC%c%d", sign, hh);
  return String(out);
}

static const char* namedTimezoneToPosix(const String& tz) {
  if (tz == "Europe/Warsaw" || tz == "Europe/Berlin") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if (tz == "Europe/London") return "GMT0BST,M3.5.0/1,M10.5.0/2";
  if (tz == "America/New_York") return "EST5EDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Chicago") return "CST6CDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Denver") return "MST7MDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Los_Angeles") return "PST8PDT,M3.2.0/2,M11.1.0/2";
  if (tz == "Etc/UTC") return "UTC0";
  if (tz == "Asia/Tokyo") return "JST-9";
  return nullptr;
}

static void applyTimezone() {
  String tz = settings.timezone;
  if (tz.length() == 0) tz = DEFAULT_TZ;

  const char* named = namedTimezoneToPosix(tz);
  if (named) {
    setenv("TZ", named, 1);
  } else {
    const String off = offsetToPosixTZ(tz);
    if (off.length() > 0) setenv("TZ", off.c_str(), 1);
    else setenv("TZ", tz.c_str(), 1);
  }
  tzset();
}

static void handleButton();
static void processPendingOta();

static void startNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

static bool connectWifiSta(uint32_t timeoutMs) {
  if (settings.ssid.length() == 0) {
    addLogLine("WiFi connect pominięty: brak SSID");
    return false;
  }

  addLogLine(
    "WiFi connect start: ssid=\"" + settings.ssid
    + "\" pass_len=" + String(settings.pass.length())
    + " timeoutMs=" + String(timeoutMs)
  );

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());

  const unsigned long start = millis();
  wl_status_t lastStatus = WiFi.status();
  unsigned long lastLedRefreshMs = 0;
  Serial.printf("[WIFI] begin status=%d(%s)\r\n", static_cast<int>(lastStatus), wifiStatusLabel(lastStatus));
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    const unsigned long now = millis();
    if (now - lastLedRefreshMs >= 10UL) {
      lastLedRefreshMs = now;
      updateLedPattern();
    }
    handleButton();
    if (apEnabled) {
      server.handleClient();
    }
    const wl_status_t cur = WiFi.status();
    if (cur != lastStatus) {
      Serial.printf("[WIFI] status=%d(%s) elapsed=%lu\r\n", static_cast<int>(cur), wifiStatusLabel(cur), now - start);
      lastStatus = cur;
    }
    delay(10);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiFailedAttempts = 0;
    addLogLine(
      "WiFi connected: ip=" + WiFi.localIP().toString()
      + " ssid=\"" + WiFi.SSID() + "\""
      + " bssid=" + WiFi.BSSIDstr()
      + " rssi=" + String(WiFi.RSSI())
    );
    return true;
  }

  if (wifiFailedAttempts < 250) ++wifiFailedAttempts;
  const wl_status_t st = WiFi.status();
  addLogLine(
    "WiFi connect fail: status=" + String(static_cast<int>(st))
    + " (" + String(wifiStatusLabel(st)) + ")"
    + " fails=" + String(wifiFailedAttempts)
  );
  if (st == WL_NO_SSID_AVAIL) {
    const unsigned long now = millis();
    if (settings.assignedToWms || pairState == PairState::ASSIGNED) {
      if ((wifiFailedAttempts % 3) == 1) {
        addLogLine("NO_SSID: siec niewidoczna (sprawdz 2.4 GHz / hotspot iPhone: Maks. zgodnosc)");
      }
      addLogLine("WiFi scan pominięty (urzadzenie przypisane) - kontynuuje retry");
      return false;
    }
    if (now - wifiLastNoSsidScanMs < WIFI_NO_SSID_SCAN_INTERVAL_MS) {
      addLogLine("WiFi scan pominięty (limit czasu)");
      return false;
    }
    wifiLastNoSsidScanMs = now;
    const int found = WiFi.scanNetworks(false, true);
    if (found <= 0) {
      addLogLine("WiFi scan: brak widocznych sieci 2.4 GHz");
    } else {
      String line = "WiFi scan:";
      const int limit = (found < 5) ? found : 5;
      for (int i = 0; i < limit; ++i) {
        line += " [" + WiFi.SSID(i) + " " + String(WiFi.RSSI(i)) + "dBm ch" + String(WiFi.channel(i)) + "]";
      }
      addLogLine(line);
      WiFi.scanDelete();
    }
  }
  return false;
}

static void ensureApMode() {
  if (apEnabled) {
    apModeRetryAtMs = millis() + AP_MODE_WINDOW_MS;
    return;
  }
  apSsid = "WMS_PLUG_" + identity.deviceId.substring(4);
  WiFi.disconnect(false);
  delay(60);
  WiFi.mode(WIFI_AP);
  WiFi.setOutputPower(20.5f);
  const bool ok = WiFi.softAP(apSsid.c_str(), "12345678", 1, false, 4);
  apEnabled = ok;
  if (ok) {
    apModeRetryAtMs = millis() + AP_MODE_WINDOW_MS;
    addLogLine(
      "AP enabled: " + apSsid + " / 12345678"
      + " ip=" + WiFi.softAPIP().toString()
      + " ch=" + String(WiFi.channel())
      + " window_s=300"
    );
  } else {
    apModeRetryAtMs = 0;
    addLogLine("AP enable failed");
  }
}

static void disableApMode(bool force = false) {
  if (!apEnabled) return;
  if (!force && pairState == PairState::PAIRING) return;
  if (!force && WiFi.status() != WL_CONNECTED) return;
  WiFi.softAPdisconnect(true);
  apEnabled = false;
  apModeRetryAtMs = 0;
  addLogLine(force ? "AP disabled after timeout, retrying STA" : "AP disabled after STA connection");
}

static void disableApIfPossible() {
  disableApMode(false);
}

static void configureHlw() {
  hlw.begin(PIN_HLW_CF, settings.cf1Pin, PIN_HLW_SEL, HIGH, false, 2000000);
  hlw.setMode(MODE_CURRENT);
  // Conservative defaults, can be overridden from settings/calibration.
  hlw.setResistors(0.001f, 5.0f * 470000.0f, 1000.0f);
  if (settings.hlwVoltageMultiplier > 0.0f) hlw.setVoltageMultiplier(settings.hlwVoltageMultiplier);
  if (settings.hlwCurrentMultiplier > 0.0f) hlw.setCurrentMultiplier(settings.hlwCurrentMultiplier);
  if (settings.hlwPowerMultiplier > 0.0f) hlw.setPowerMultiplier(settings.hlwPowerMultiplier);
  telemetry.modeCurrent = true;
  telemetry.lastModeToggleMs = millis();
}

static void fillZonesJson(JsonArray arr) {
  JsonObject z = arr.add<JsonObject>();
  z["id"] = 0;
  z["active"] = relayOn;
  z["remaining"] = relayRemainingSeconds();
  z["name"] = settings.zoneName;
}

static void fillProgramsJson(JsonArray arr, bool includeLastRun = false) {
  for (int i = 0; i < programCount; ++i) {
    const ProgramEntry& p = programs[i];
    if (!p.used) continue;
    JsonObject o = arr.add<JsonObject>();
    o["id"] = i;
    o["zone"] = p.zone;
    o["time"] = p.time;
    o["duration"] = p.duration;
    o["active"] = p.active;
    JsonArray days = o["days"].to<JsonArray>();
    csvToDaysArray(p.days, days);
    if (includeLastRun) o["lastRun"] = static_cast<long>(p.lastRun);
  }
}

static bool mqttPublishRaw(const String& leaf, const char* payload, bool retained) {
  if (!mqttConnectedFlag) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  const uint32_t heap = ESP.getFreeHeap();
  if (heap < MQTT_MIN_HEAP_FOR_PUBLISH) {
    const unsigned long now = millis();
    if (now - mqttLowHeapLogMs >= 10000UL) {
      mqttLowHeapLogMs = now;
      addLogLine("MQTT publish skip: low heap=" + String(heap));
    }
    return false;
  }
  const String topic = topicLeaf(leaf);
  const bool ok = mqttClient.publish(topic.c_str(), payload, retained);
  if (!ok && mqttConnectedFlag) {
    mqttConnectedFlag = false;
    addLogLine("MQTT publish failed, przejście do reconnect (leaf=" + leaf + ", state=" + String(mqttClient.state()) + ")");
  }
  return ok;
}

static bool mqttPublish(const String& leaf, const String& payload, bool retained) {
  return mqttPublishRaw(leaf, payload.c_str(), retained);
}

static bool mqttPublishJson(const String& leaf, JsonDocument& doc, bool retained) {
  char payload[MQTT_JSON_BUFFER_SIZE];
  const size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) {
    addLogLine("MQTT json too large/skipped: " + leaf + " size=" + String(static_cast<unsigned long>(measureJson(doc))));
    return false;
  }
  return mqttPublishRaw(leaf, payload, retained);
}

static void publishStatus(bool force = false) {
  if (!mqttConnectedFlag) return;
  const unsigned long now = millis();
  if (!force && now - lastStatusPublishMs < STATUS_PUBLISH_MS) return;
  lastStatusPublishMs = now;

  JsonDocument doc;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
  doc["ip"] = wifiIpOrDash();
  if (WiFi.status() == WL_CONNECTED) {
    doc["ssid"] = WiFi.SSID();
    doc["bssid"] = WiFi.BSSIDstr();
    doc["gateway"] = WiFi.gatewayIP().toString();
  }
  doc["time"] = nowTimestamp();
  doc["online"] = true;
  doc["device_id"] = identity.deviceId;
  doc["claim_code"] = identity.claimCode;
  doc["fw_version"] = FW_VERSION;
  doc["hardware"] = "bwshp6";
  doc["device_type"] = "smart_plug";
  doc["model"] = "BlitzWolf BW-SHP6";
  doc["relay_on"] = relayOn;
  doc["remaining_sec"] = relayRemainingSeconds();
  doc["power_w"] = telemetry.activePower;
  doc["energy_total_kwh"] = telemetry.energyTotalWh / 1000.0f;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assigned_to_wms"] = settings.assignedToWms;
  doc["assigned_account"] = settings.assignedAccount;
  doc["ble_supported"] = false;
  doc["provisioning_method"] = "wifi_ap";

  mqttPublishJson("global/status", doc, true);
}

static void publishZones() {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  fillZonesJson(arr);
  mqttPublishJson("zones", doc, true);

  mqttPublish("zones/0/status", relayOn ? "1" : "0", true);
  mqttPublish("zones/0/remaining", String(relayRemainingSeconds()), true);
}

static void publishPrograms() {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  fillProgramsJson(arr, false);
  mqttPublishJson("programs", doc, true);
}

static void publishLogs() {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  JsonArray arr = doc["logs"].to<JsonArray>();
  for (int i = 0; i < logsCount; ++i) arr.add(logsBuffer[i]);
  mqttPublishJson("logs", doc, true);
}

static void publishSettingsPublic() {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  doc["timezone"] = settings.timezone;
  doc["enableMqtt"] = settings.enableMqtt;
  doc["autoMode"] = settings.autoMode;
  doc["mqttServer"] = settings.mqttServer;
  doc["mqttBroker"] = settings.mqttServer;
  doc["mqttPort"] = settings.mqttPort;
  doc["mqttUser"] = settings.mqttUser;
  doc["mqttTopic"] = baseTopic();
  doc["mqttTopicBase"] = baseTopic();
  doc["mqttClientId"] = settings.mqttClientId;
  doc["mqttPassConfigured"] = settings.mqttPass.length() > 0;
  doc["zoneCount"] = 1;
  doc["telemetryIntervalSec"] = settings.telemetryIntervalSec;
  doc["relayInverted"] = settings.relayInverted;
  doc["zoneName"] = settings.zoneName;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assignedToWms"] = settings.assignedToWms;
  doc["assignedAccount"] = settings.assignedAccount;
  doc["bleSupported"] = false;
  doc["provisioningMethod"] = "wifi_ap";
  doc["cf1Pin"] = settings.cf1Pin;
  doc["hlwVoltageMultiplier"] = settings.hlwVoltageMultiplier;
  doc["hlwCurrentMultiplier"] = settings.hlwCurrentMultiplier;
  doc["hlwPowerMultiplier"] = settings.hlwPowerMultiplier;

  mqttPublishJson("settings/public", doc, true);
}

static void publishPlugTelemetry(bool force = false) {
  if (!mqttConnectedFlag) return;
  const unsigned long now = millis();
  if (!force && now - lastTelemetryPublishMs < (unsigned long)settings.telemetryIntervalSec * 1000UL) return;
  lastTelemetryPublishMs = now;

  JsonDocument doc;
  doc["relay_on"] = relayOn;
  doc["remaining_sec"] = relayRemainingSeconds();
  doc["power_w"] = telemetry.activePower;
  doc["voltage_v"] = telemetry.voltage;
  doc["current_a"] = telemetry.current;
  doc["apparent_power_va"] = telemetry.apparentPower;
  doc["reactive_power_var"] = telemetry.reactivePower;
  doc["power_factor"] = telemetry.powerFactor;
  doc["energy_total_kwh"] = telemetry.energyTotalWh / 1000.0f;
  doc["energy_today_kwh"] = telemetry.energyTodayWh / 1000.0f;
  doc["energy_session_kwh"] = telemetry.energySessionWh / 1000.0f;
  doc["sampled_at"] = nowTimestamp();
  doc["chip"] = "HLW8012/BL0937";
  doc["cf1_pin"] = settings.cf1Pin;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assigned_to_wms"] = settings.assignedToWms;

  mqttPublishJson("plug/telemetry", doc, true);
}

static void publishAllSnapshots() {
  publishStatus(true);
  publishZones();
  publishPrograms();
  publishSettingsPublic();
  publishPlugTelemetry(true);
}

static void mqttAck(const String& commandId, const String& commandTopic, const String& status, const String& detail) {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  doc["command_id"] = commandId.length() > 0 ? commandId : String("legacy_") + String(millis());
  doc["command_topic"] = commandTopic;
  doc["status"] = status;
  doc["detail"] = detail;
  doc["timestamp"] = nowTimestamp();
  mqttPublishJson("ack", doc, false);
}

static String normalizeHex(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) out += c;
  }
  return out;
}

static String normalizeOtaTarget(String target) {
  target.trim();
  target.toLowerCase();
  if (target == "fs" || target == "spiffs" || target == "littlefs" || target == "filesystem") return "fs";
  return "firmware";
}

static String normalizeOtaHardware(String hw) {
  hw.trim();
  hw.toLowerCase();
  if (hw == "bwshp6" || hw == "bw-shp6" || hw == "bw_shp6" || hw == "shp6" || hw == "blitzwolf-shp6") return "bwshp6";
  return "";
}

static String otaHardwareLabel(const String& hw) {
  if (hw == "bwshp6") return "BW-SHP6";
  return "unknown";
}

static bool otaTargetIsFs(const OtaRequest& req) {
  return req.target == "fs";
}

static int otaUpdateCommand(const OtaRequest& req) {
  return otaTargetIsFs(req) ? U_FS : U_FLASH;
}

static String otaTargetLabel(const OtaRequest& req) {
  return otaTargetIsFs(req) ? "systemu plikow" : "firmware";
}

static String hashToHex(const uint8_t hash[32]) {
  char hex[65];
  for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
  hex[64] = '\0';
  return String(hex);
}

static void publishOtaStatus(const String& stage, int progress, const String& detail, const OtaRequest* req = nullptr, const String& error = "") {
  if (!mqttConnectedFlag) return;
  JsonDocument doc;
  doc["stage"] = stage;
  if (progress >= 0) doc["progress"] = progress;
  doc["detail"] = detail;
  if (error.length() > 0) doc["error"] = error;
  doc["timestamp"] = nowTimestamp();
  doc["device_id"] = identity.deviceId;
  if (req) {
    if (req->campaignId.length() > 0) doc["campaign_id"] = req->campaignId;
    if (req->firmwareId.length() > 0) doc["firmware_id"] = req->firmwareId;
    if (req->version.length() > 0) doc["version"] = req->version;
    if (req->expectedSize > 0) doc["size"] = req->expectedSize;
    if (req->target.length() > 0) doc["target"] = req->target;
  }
  mqttPublishJson("ota/status", doc, false);
  Serial.println("[OTA] " + stage + " " + String(progress) + "% - " + detail + (error.length() ? (" | " + error) : ""));
}

static bool downloadAndHashPass(
  const OtaRequest& req,
  const String& stage,
  const String& detail,
  bool writeToFlash,
  const uint8_t* expectedHash,
  uint8_t outHash[32],
  size_t& outBytes,
  String& errOut
) {
  outBytes = 0;

  BearSSL::WiFiClientSecure httpClient;
  httpClient.setInsecure();
  HTTPClient http;
  if (!http.begin(httpClient, req.url)) {
    errOut = "HTTP begin failed";
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errOut = "HTTP GET code " + String(code);
    http.end();
    return false;
  }

  const int total = http.getSize();
  if (req.expectedSize > 0 && total > 0 && req.expectedSize != total) {
    errOut = "size mismatch (expected " + String(req.expectedSize) + ", got " + String(total) + ")";
    http.end();
    return false;
  }

  if (writeToFlash) {
    size_t beginSize = 0;
    if (total > 0) beginSize = static_cast<size_t>(total);
    else if (req.expectedSize > 0) beginSize = static_cast<size_t>(req.expectedSize);
    if (beginSize == 0) {
      errOut = "missing content length";
      http.end();
      return false;
    }
    if (!Update.begin(beginSize, otaUpdateCommand(req))) {
      errOut = "Update.begin error=" + String(Update.getError());
      http.end();
      return false;
    }
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[1024];
  unsigned long lastProgressTs = 0;
  int lastProgressPct = -1;

  BearSSL::HashSHA256 hash;
  hash.begin();

  while (http.connected() && (total < 0 || static_cast<int>(outBytes) < total)) {
    const size_t avail = stream->available();
    if (!avail) {
      mqttClient.loop();
      delay(1);
      continue;
    }

    const int toRead = static_cast<int>(min(sizeof(buff), avail));
    const int readLen = stream->readBytes(buff, toRead);
    if (readLen <= 0) {
      mqttClient.loop();
      delay(1);
      continue;
    }

    hash.add(buff, static_cast<uint32_t>(readLen));
    if (writeToFlash) {
      const size_t w = Update.write(buff, static_cast<size_t>(readLen));
      if (w != static_cast<size_t>(readLen)) {
        errOut = "Update.write error=" + String(Update.getError());
        Update.end(false);
        http.end();
        return false;
      }
    }

    outBytes += static_cast<size_t>(readLen);
    int pct = 0;
    if (total > 0) pct = static_cast<int>((outBytes * 100UL) / static_cast<size_t>(total));
    const unsigned long nowMs = millis();
    if (pct != lastProgressPct && (lastProgressPct < 0 || pct - lastProgressPct >= 2 || nowMs - lastProgressTs > 1200)) {
      lastProgressPct = pct;
      lastProgressTs = nowMs;
      publishOtaStatus(stage, pct, detail, &req);
    }

    mqttClient.loop();
    delay(1);
  }

  hash.end();
  const uint8_t* digest = reinterpret_cast<const uint8_t*>(hash.hash());
  if (!digest) {
    errOut = "sha256 digest unavailable";
    if (writeToFlash) Update.end(false);
    http.end();
    return false;
  }
  memcpy(outHash, digest, 32);

  if (total > 0 && static_cast<int>(outBytes) != total) {
    errOut = "download incomplete (got " + String(static_cast<int>(outBytes)) + ", expected " + String(total) + ")";
    if (writeToFlash) Update.end(false);
    http.end();
    return false;
  }
  if (req.expectedSize > 0 && req.expectedSize != static_cast<int>(outBytes)) {
    errOut = "size mismatch after download (expected " + String(req.expectedSize) + ", got " + String(static_cast<int>(outBytes)) + ")";
    if (writeToFlash) Update.end(false);
    http.end();
    return false;
  }
  if (expectedHash && memcmp(outHash, expectedHash, 32) != 0) {
    errOut = "hash mismatch between verification and flash pass";
    if (writeToFlash) Update.end(false);
    http.end();
    return false;
  }

  if (writeToFlash) {
    if (!Update.end(true)) {
      errOut = "Update.end error=" + String(Update.getError());
      http.end();
      return false;
    }
  }

  http.end();
  return true;
}

static bool executeOta(const OtaRequest& req, String& errOut) {
  const String expectedSha = normalizeHex(req.sha256);
  if (expectedSha.length() > 0 && expectedSha.length() != 64) {
    errOut = "invalid sha256 format";
    return false;
  }

  String sigAlg = req.signatureAlg;
  sigAlg.trim();
  sigAlg.toLowerCase();
  if (sigAlg.length() == 0) sigAlg = "ed25519";
  if (req.signature.length() > 0 && sigAlg != "ed25519") {
    errOut = "unsupported signature algorithm: " + sigAlg;
    return false;
  }

  uint8_t verifiedHash[32];
  size_t verifiedBytes = 0;
  if (!downloadAndHashPass(req, "verifying", "Weryfikacja " + otaTargetLabel(req), false, nullptr, verifiedHash, verifiedBytes, errOut)) {
    return false;
  }

  const String gotSha = hashToHex(verifiedHash);
  if (expectedSha.length() == 64 && gotSha != expectedSha) {
    errOut = "sha256 mismatch";
    return false;
  }

  if (req.signature.length() > 0) {
    addLogLine("OTA: podpis przeslany (alg=" + sigAlg + "), weryfikacja podpisu niedostepna na BW-SHP6");
  }
  publishOtaStatus("verified", 100, "Weryfikacja OTA poprawna", &req);

  uint8_t flashedHash[32];
  size_t flashedBytes = 0;
  if (!downloadAndHashPass(req, "downloading", "Pobieranie " + otaTargetLabel(req), true, verifiedHash, flashedHash, flashedBytes, errOut)) {
    return false;
  }

  if (expectedSha.length() == 64 && hashToHex(flashedHash) != expectedSha) {
    errOut = "sha256 mismatch after flash write";
    return false;
  }

  return true;
}

static void processPendingOta() {
  if (!ota.pending || otaInProgress) return;
  if (WiFi.status() != WL_CONNECTED) {
    publishOtaStatus("failed", 0, "Brak WiFi do OTA", &ota, "wifi offline");
    addLogLine("OTA: blad - brak WiFi");
    ota.pending = false;
    return;
  }

  otaInProgress = true;
  OtaRequest req = ota;
  ota.pending = false;

  addLogLine("OTA: start " + req.target + " " + req.version);
  publishOtaStatus("verifying", 0, "Start OTA (" + otaTargetLabel(req) + ")", &req);

  String err;
  const bool ok = executeOta(req, err);
  if (!ok) {
    publishOtaStatus("failed", 0, "OTA nieudane", &req, err);
    mqttAck(req.commandId, req.commandTopic, "failed", "OTA failed: " + err);
    addLogLine("OTA: blad - " + err);
    otaInProgress = false;
    return;
  }

  publishOtaStatus("done", 100, "OTA zakonczone, restart", &req);
  mqttAck(req.commandId, req.commandTopic, "accepted", "OTA done, restart");
  addLogLine("OTA: sukces, restart");
  otaInProgress = false;
  delay(600);
  ESP.restart();
}

static void applyTimezoneFromProgramCommand(JsonVariantConst root, const String& context) {
  if (!root["timezone"].is<const char*>()) return;
  String tz = String(root["timezone"].as<const char*>());
  tz.trim();
  if (tz.length() == 0 || tz.length() > 64) return;
  if (tz == settings.timezone) return;
  settings.timezone = tz;
  applyTimezone();
  saveSettings();
  addLogLine("MQTT CMD: timezone -> " + settings.timezone + " (" + context + ")");
}

static bool parseProgramIndexFromTopic(const String& leaf, const String& prefix, int& indexOut) {
  if (!leaf.startsWith(prefix)) return false;
  const String tail = leaf.substring(prefix.length());
  if (tail.length() == 0) return false;
  for (size_t i = 0; i < tail.length(); ++i) if (!isDigit(tail[i])) return false;
  indexOut = tail.toInt();
  return true;
}

static bool applySettingsPatch(JsonVariantConst src, bool fromRemoteCommand, bool& requiresMqttReconnect, bool& requiresWifiReconnect, bool& requiresReboot) {
  bool changed = false;

  if (src["ssid"].is<const char*>()) {
    String v = String(src["ssid"].as<const char*>());
    v.trim();
    if (v != settings.ssid) {
      settings.ssid = v;
      changed = true;
      requiresWifiReconnect = true;
      addLogLine("Settings patch: ssid_len=" + String(settings.ssid.length()));
    }
  }

  if (src["pass"].is<const char*>()) {
    const String v = String(src["pass"].as<const char*>());
    if (v != settings.pass) {
      settings.pass = v;
      changed = true;
      requiresWifiReconnect = true;
      addLogLine("Settings patch: pass_len=" + String(settings.pass.length()));
    }
  } else if (src["password"].is<const char*>()) {
    const String v = String(src["password"].as<const char*>());
    if (v != settings.pass) {
      settings.pass = v;
      changed = true;
      requiresWifiReconnect = true;
      addLogLine("Settings patch (alias): pass_len=" + String(settings.pass.length()));
    }
  }

  if (src["enableMqtt"].is<bool>() || src["enableMqtt"].is<int>() || src["enableMqtt"].is<const char*>()) {
    const bool v = toBoolFlexible(src["enableMqtt"], settings.enableMqtt);
    if (v != settings.enableMqtt) {
      settings.enableMqtt = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttServer"].is<const char*>()) {
    const String v = String(src["mqttServer"].as<const char*>());
    if (v.length() > 0 && v != settings.mqttServer) {
      settings.mqttServer = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  } else if (src["mqttBroker"].is<const char*>()) {
    const String v = String(src["mqttBroker"].as<const char*>());
    if (v.length() > 0 && v != settings.mqttServer) {
      settings.mqttServer = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttPort"].is<int>()) {
    int v = src["mqttPort"].as<int>();
    if (v < 1) v = DEFAULT_MQTT_PORT;
    if (v != settings.mqttPort) {
      settings.mqttPort = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttUser"].is<const char*>()) {
    const String v = String(src["mqttUser"].as<const char*>());
    if (v != settings.mqttUser) {
      settings.mqttUser = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttPass"].is<const char*>()) {
    const String v = String(src["mqttPass"].as<const char*>());
    if (v.length() > 0 && v != settings.mqttPass) {
      settings.mqttPass = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttClientId"].is<const char*>()) {
    const String v = String(src["mqttClientId"].as<const char*>());
    if (v.length() > 0 && v != settings.mqttClientId) {
      settings.mqttClientId = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["mqttTopic"].is<const char*>()) {
    const String v = sanitizeTopicBase(String(src["mqttTopic"].as<const char*>()));
    if (v.length() > 0 && v != settings.mqttTopicBase) {
      settings.mqttTopicBase = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  } else if (src["mqttTopicBase"].is<const char*>()) {
    const String v = sanitizeTopicBase(String(src["mqttTopicBase"].as<const char*>()));
    if (v.length() > 0 && v != settings.mqttTopicBase) {
      settings.mqttTopicBase = v;
      changed = true;
      requiresMqttReconnect = true;
    }
  }

  if (src["autoMode"].is<bool>() || src["autoMode"].is<int>() || src["autoMode"].is<const char*>()) {
    const bool v = toBoolFlexible(src["autoMode"], settings.autoMode);
    if (v != settings.autoMode) {
      settings.autoMode = v;
      changed = true;
    }
  }

  if (src["timezone"].is<const char*>()) {
    String v = String(src["timezone"].as<const char*>());
    v.trim();
    if (v.length() > 0 && v != settings.timezone) {
      settings.timezone = v;
      changed = true;
      applyTimezone();
    }
  }

  if (src["telemetryIntervalSec"].is<int>()) {
    int sec = src["telemetryIntervalSec"].as<int>();
    if (sec < 2) sec = 2;
    if (sec > 300) sec = 300;
    if (sec != settings.telemetryIntervalSec) {
      settings.telemetryIntervalSec = static_cast<uint16_t>(sec);
      changed = true;
    }
  }

  if (src["relayInverted"].is<bool>() || src["relayInverted"].is<int>() || src["relayInverted"].is<const char*>()) {
    const bool v = toBoolFlexible(src["relayInverted"], settings.relayInverted);
    if (v != settings.relayInverted) {
      settings.relayInverted = v;
      changed = true;
      setRelayOutput(relayOn);
    }
  }

  if (src["zoneName"].is<const char*>()) {
    String v = String(src["zoneName"].as<const char*>());
    v.trim();
    if (v.length() == 0) v = "Gniazdko";
    if (v != settings.zoneName) {
      settings.zoneName = v;
      changed = true;
    }
  }

  if (src["assignedToWms"].is<bool>() || src["assignedToWms"].is<int>() || src["assignedToWms"].is<const char*>()) {
    const bool v = toBoolFlexible(src["assignedToWms"], settings.assignedToWms);
    if (v != settings.assignedToWms) {
      settings.assignedToWms = v;
      changed = true;
      setPairState(settings.assignedToWms ? PairState::ASSIGNED : PairState::UNASSIGNED, "settings patch");
    }
  }

  if (src["assignedAccount"].is<const char*>()) {
    const String v = String(src["assignedAccount"].as<const char*>());
    if (v != settings.assignedAccount) {
      settings.assignedAccount = v;
      changed = true;
    }
  }

  if (src["cf1Pin"].is<int>()) {
    const int v = src["cf1Pin"].as<int>();
    if ((v == 4 || v == 5) && v != settings.cf1Pin) {
      settings.cf1Pin = static_cast<uint8_t>(v);
      changed = true;
      requiresReboot = true;
    }
  }

  if (src["hlwVoltageMultiplier"].is<float>() || src["hlwVoltageMultiplier"].is<int>()) {
    const float v = src["hlwVoltageMultiplier"].as<float>();
    if (v >= 0.0f && fabsf(v - settings.hlwVoltageMultiplier) > 0.0001f) {
      settings.hlwVoltageMultiplier = v;
      changed = true;
      requiresReboot = true;
    }
  }

  if (src["hlwCurrentMultiplier"].is<float>() || src["hlwCurrentMultiplier"].is<int>()) {
    const float v = src["hlwCurrentMultiplier"].as<float>();
    if (v >= 0.0f && fabsf(v - settings.hlwCurrentMultiplier) > 0.0001f) {
      settings.hlwCurrentMultiplier = v;
      changed = true;
      requiresReboot = true;
    }
  }

  if (src["hlwPowerMultiplier"].is<float>() || src["hlwPowerMultiplier"].is<int>()) {
    const float v = src["hlwPowerMultiplier"].as<float>();
    if (v >= 0.0f && fabsf(v - settings.hlwPowerMultiplier) > 0.0001f) {
      settings.hlwPowerMultiplier = v;
      changed = true;
      requiresReboot = true;
    }
  }

  if (changed) {
    saveSettings();
    addLogLine(fromRemoteCommand ? "MQTT CMD: zapisano ustawienia" : "Local API: zapisano ustawienia");
  }

  return changed;
}

static void mqttHandleCommand(const String& leaf, const String& payloadText) {
  JsonDocument doc;
  deserializeJson(doc, payloadText);

  const String commandId = String(doc["command_id"] | (String("legacy_") + String(millis())));
  const String commandTopic = leaf;

  if (leaf == "global/refresh") {
    publishAllSnapshots();
    mqttAck(commandId, commandTopic, "accepted", "refresh wykonany");
    return;
  }

  if (leaf.startsWith("cmd/zones/")) {
    const String tail = leaf.substring(String("cmd/zones/").length());
    const int slash = tail.indexOf('/');
    if (slash < 0) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowy topic strefy");
      return;
    }
    const int zoneId = tail.substring(0, slash).toInt();
    const String action = tail.substring(slash + 1);

    if (zoneId != 0) {
      mqttAck(commandId, commandTopic, "failed", "smart plug ma tylko strefe 0");
      return;
    }

    if (action == "toggle") {
      if (relayOn) setRelayState(false, 0, "zdalne toggle");
      else setRelayState(true, 0, "zdalne toggle");
      publishZones();
      publishStatus(true);
      publishPlugTelemetry(true);
      mqttAck(commandId, commandTopic, "accepted", "przelaczono przekaznik");
      return;
    }

    if (action == "start") {
      uint32_t secs = 600;
      if (doc["seconds"].is<int>()) secs = static_cast<uint32_t>(doc["seconds"].as<int>());
      else if (doc["duration"].is<int>()) secs = static_cast<uint32_t>(doc["duration"].as<int>());
      else if (doc["payload"].is<int>()) secs = static_cast<uint32_t>(doc["payload"].as<int>());
      if (secs > 7UL * 24UL * 3600UL) secs = 7UL * 24UL * 3600UL;
      setRelayState(true, secs, "zdalny start");
      publishZones();
      publishStatus(true);
      publishPlugTelemetry(true);
      mqttAck(commandId, commandTopic, "accepted", "relay uruchomiony");
      return;
    }

    if (action == "stop") {
      setRelayState(false, 0, "zdalny stop");
      publishZones();
      publishStatus(true);
      publishPlugTelemetry(true);
      mqttAck(commandId, commandTopic, "accepted", "relay zatrzymany");
      return;
    }

    mqttAck(commandId, commandTopic, "failed", "nieznana akcja strefy");
    return;
  }

  if (leaf == "cmd/zones-names/set") {
    if (!doc["names"].is<JsonArrayConst>()) {
      mqttAck(commandId, commandTopic, "failed", "brak tablicy names");
      return;
    }
    JsonArrayConst names = doc["names"].as<JsonArrayConst>();
    if (names.size() > 0 && names[0].is<const char*>()) {
      String next = String(names[0].as<const char*>());
      next.trim();
      if (next.length() == 0) next = "Gniazdko";
      settings.zoneName = next;
      saveSettings();
      addLogLine("MQTT CMD: zmieniono nazwe strefy");
      publishZones();
      publishSettingsPublic();
      mqttAck(commandId, commandTopic, "accepted", "nazwa strefy zapisana");
      return;
    }
    mqttAck(commandId, commandTopic, "failed", "names[0] puste");
    return;
  }

  if (leaf == "cmd/programs/import") {
    applyTimezoneFromProgramCommand(doc.as<JsonVariantConst>(), "programs/import");
    JsonArrayConst arr;
    if (doc["programs"].is<JsonArrayConst>()) arr = doc["programs"].as<JsonArrayConst>();
    else if (doc.is<JsonArrayConst>()) arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
      mqttAck(commandId, commandTopic, "failed", "brak tablicy programow");
      return;
    }

    int count = 0;
    const bool preserveLastRun = toBoolFlexible(doc["preserveLastRun"], false);
    for (JsonVariantConst v : arr) {
      if (count >= MAX_PROGRAMS) break;
      ProgramEntry p;
      if (!parseProgramFromVariant(v, p)) continue;
      if (!preserveLastRun) p.lastRun = 0;
      programs[count++] = p;
    }
    programCount = count;
    savePrograms();
    addLogLine("MQTT CMD: import programow");
    publishPrograms();
    mqttAck(commandId, commandTopic, "accepted", "programy zaimportowane");
    return;
  }

  if (leaf == "cmd/programs/add") {
    applyTimezoneFromProgramCommand(doc.as<JsonVariantConst>(), "programs/add");
    if (programCount >= MAX_PROGRAMS) {
      mqttAck(commandId, commandTopic, "failed", "osiagnieto limit programow");
      return;
    }
    JsonVariantConst src = doc["program"];
    if (src.isNull()) src = doc;
    ProgramEntry p;
    if (!parseProgramFromVariant(src, p)) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowy program");
      return;
    }
    p.lastRun = 0;
    programs[programCount++] = p;
    savePrograms();
    addLogLine("MQTT CMD: dodano program");
    publishPrograms();
    mqttAck(commandId, commandTopic, "accepted", "program dodany");
    return;
  }

  int progIndex = -1;
  if (parseProgramIndexFromTopic(leaf, "cmd/programs/edit/", progIndex)) {
    applyTimezoneFromProgramCommand(doc.as<JsonVariantConst>(), "programs/edit");
    if (progIndex < 0 || progIndex >= programCount) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowe id programu");
      return;
    }
    JsonVariantConst src = doc["program"];
    if (src.isNull()) src = doc;
    ProgramEntry p = programs[progIndex];
    if (!parseProgramFromVariant(src, p)) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowe dane programu");
      return;
    }
    if (!toBoolFlexible(doc["preserveLastRun"], false)) p.lastRun = 0;
    programs[progIndex] = p;
    savePrograms();
    addLogLine("MQTT CMD: edytowano program " + String(progIndex));
    publishPrograms();
    mqttAck(commandId, commandTopic, "accepted", "program zaktualizowany");
    return;
  }

  if (parseProgramIndexFromTopic(leaf, "cmd/programs/delete/", progIndex)) {
    if (progIndex < 0 || progIndex >= programCount) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowe id programu");
      return;
    }
    for (int i = progIndex; i < programCount - 1; ++i) programs[i] = programs[i + 1];
    --programCount;
    savePrograms();
    addLogLine("MQTT CMD: usunieto program " + String(progIndex));
    publishPrograms();
    mqttAck(commandId, commandTopic, "accepted", "program usuniety");
    return;
  }

  if (leaf == "cmd/logs/clear") {
    logsCount = 0;
    addLogLine("MQTT CMD: wyczyszczono logi");
    saveLogs();
    publishLogs();
    mqttAck(commandId, commandTopic, "accepted", "logi wyczyszczone");
    return;
  }

  if (leaf == "cmd/pair/start") {
    setPairState(PairState::PAIRING, "mqtt pair/start");
    ensureApMode();
    publishStatus(true);
    publishSettingsPublic();
    mqttAck(commandId, commandTopic, "accepted", "tryb parowania wlaczony");
    return;
  }

  if (leaf == "cmd/pair/stop") {
    setPairState(settings.assignedToWms ? PairState::ASSIGNED : PairState::UNASSIGNED, "mqtt pair/stop");
    if (pairState != PairState::PAIRING && apEnabled && WiFi.status() == WL_CONNECTED) {
      disableApIfPossible();
    }
    publishStatus(true);
    publishSettingsPublic();
    mqttAck(commandId, commandTopic, "accepted", "tryb parowania wylaczony");
    return;
  }

  if (leaf == "cmd/pair/claim") {
    settings.assignedToWms = true;
    if (doc["account_id"].is<const char*>()) settings.assignedAccount = String(doc["account_id"].as<const char*>());
    else if (doc["owner_user_id"].is<const char*>()) settings.assignedAccount = String(doc["owner_user_id"].as<const char*>());
    saveSettings();
    setPairState(PairState::ASSIGNED, "mqtt pair/claim");
    publishStatus(true);
    publishSettingsPublic();
    publishPlugTelemetry(true);
    mqttAck(commandId, commandTopic, "accepted", "urzadzenie przypisane");
    return;
  }

  if (leaf == "cmd/pair/unclaim") {
    settings.assignedToWms = false;
    settings.assignedAccount = "";
    saveSettings();
    setPairState(PairState::UNASSIGNED, "mqtt pair/unclaim");
    publishStatus(true);
    publishSettingsPublic();
    publishPlugTelemetry(true);
    mqttAck(commandId, commandTopic, "accepted", "urzadzenie odlaczone");
    return;
  }

  if (leaf == "cmd/pair/provision") {
    const String ssid = String(doc["ssid"] | "");
    if (ssid.length() == 0) {
      mqttAck(commandId, commandTopic, "failed", "ssid required");
      return;
    }

    settings.ssid = ssid;
    if (doc["pass"].is<const char*>()) settings.pass = String(doc["pass"].as<const char*>());
    if (doc["password"].is<const char*>()) settings.pass = String(doc["password"].as<const char*>());
    if (doc["mqttServer"].is<const char*>()) settings.mqttServer = String(doc["mqttServer"].as<const char*>());
    if (doc["mqttPort"].is<int>()) {
      const int p = doc["mqttPort"].as<int>();
      if (p > 0) settings.mqttPort = p;
    }
    if (doc["mqttUser"].is<const char*>()) settings.mqttUser = String(doc["mqttUser"].as<const char*>());
    if (doc["mqttPass"].is<const char*>()) {
      const String mp = String(doc["mqttPass"].as<const char*>());
      if (mp.length() > 0) settings.mqttPass = mp;
    }
    if (doc["mqttTopicBase"].is<const char*>()) {
      const String t = sanitizeTopicBase(String(doc["mqttTopicBase"].as<const char*>()));
      if (t.length() > 0) settings.mqttTopicBase = t;
    }
    if (doc["assignedAccount"].is<const char*>()) settings.assignedAccount = String(doc["assignedAccount"].as<const char*>());

    settings.assignedToWms = false;
    saveSettings();
    mqttAck(commandId, commandTopic, "accepted", "provisioning zapisany, restart");
    addLogLine("MQTT CMD: provisioning zapisany, restart");
    saveLogs();
    delay(200);
    ESP.restart();
    return;
  }

  if (leaf == "cmd/settings/set") {
    JsonVariantConst s = doc["settings"];
    if (s.isNull()) s = doc;
    if (s.isNull() || !s.is<JsonObjectConst>()) {
      mqttAck(commandId, commandTopic, "failed", "brak obiektu settings");
      return;
    }

    bool mqttReconnect = false;
    bool wifiReconnect = false;
    bool rebootRequired = false;
    const bool changed = applySettingsPatch(s, true, mqttReconnect, wifiReconnect, rebootRequired);

    if (changed) {
      publishSettingsPublic();
      publishStatus(true);
      publishZones();
      publishPlugTelemetry(true);
    }

    mqttAck(commandId, commandTopic, "accepted", changed ? "ustawienia zapisane" : "brak zmian");

    if (wifiReconnect) {
      WiFi.disconnect();
      delay(200);
      connectWifiSta(WIFI_CONNECT_TIMEOUT_MS);
    }

    if (mqttReconnect) {
      mqttClient.disconnect();
    }

    if (rebootRequired) {
      addLogLine("Restart po zmianie kalibracji/pinu CF1");
      saveLogs();
      delay(200);
      ESP.restart();
    }
    return;
  }

  if (leaf == "cmd/ota/start") {
    if (!doc.is<JsonObjectConst>()) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowy payload OTA");
      return;
    }
    if (otaInProgress || ota.pending) {
      mqttAck(commandId, commandTopic, "failed", "OTA juz w trakcie");
      return;
    }

    const String url = String(doc["url"] | "");
    if (url.length() == 0) {
      mqttAck(commandId, commandTopic, "failed", "brak URL OTA");
      return;
    }

    String rawTarget = "firmware";
    if (doc["target"].is<const char*>()) rawTarget = String(doc["target"].as<const char*>());
    else if (doc["kind"].is<const char*>()) rawTarget = String(doc["kind"].as<const char*>());
    const String target = normalizeOtaTarget(rawTarget);

    String signature = "";
    if (doc["signature"].is<const char*>()) signature = String(doc["signature"].as<const char*>());
    else if (doc["sig"].is<const char*>()) signature = String(doc["sig"].as<const char*>());
    else if (doc["signature_ed25519"].is<const char*>()) signature = String(doc["signature_ed25519"].as<const char*>());

    String signatureAlg = "";
    if (doc["signature_alg"].is<const char*>()) signatureAlg = String(doc["signature_alg"].as<const char*>());
    else if (doc["sig_alg"].is<const char*>()) signatureAlg = String(doc["sig_alg"].as<const char*>());

    String rawHardware = "";
    if (doc["hardware"].is<const char*>()) rawHardware = String(doc["hardware"].as<const char*>());
    else if (doc["chip"].is<const char*>()) rawHardware = String(doc["chip"].as<const char*>());
    else if (doc["platform"].is<const char*>()) rawHardware = String(doc["platform"].as<const char*>());

    const String otaHardware = normalizeOtaHardware(rawHardware);
    const String localHardware = String("bwshp6");
    if (rawHardware.length() > 0 && otaHardware.length() == 0) {
      mqttAck(commandId, commandTopic, "failed", "nieprawidlowy hardware OTA");
      return;
    }
    if (otaHardware.length() > 0 && otaHardware != localHardware) {
      mqttAck(
        commandId,
        commandTopic,
        "failed",
        "niezgodny hardware OTA (fw: " + otaHardwareLabel(otaHardware) + ", urzadzenie: " + otaHardwareLabel(localHardware) + ")"
      );
      return;
    }

    ota.pending = true;
    ota.commandId = commandId;
    ota.commandTopic = commandTopic;
    ota.url = url;
    ota.campaignId = String(doc["campaign_id"] | "");
    ota.firmwareId = String(doc["firmware_id"] | "");
    ota.version = String(doc["version"] | "");
    ota.sha256 = String(doc["sha256"] | "");
    ota.signature = signature;
    ota.signatureAlg = signatureAlg;
    ota.target = target;
    if (doc["size"].is<int>()) ota.expectedSize = doc["size"].as<int>();
    else if (doc["size"].is<long>()) ota.expectedSize = static_cast<int>(doc["size"].as<long>());
    else ota.expectedSize = 0;

    mqttAck(commandId, commandTopic, "accepted", "OTA accepted");
    publishOtaStatus("accepted", 0, "Przyjeto komende OTA", &ota);
    return;
  }

  if (leaf == "cmd/plug/set") {
    const bool desiredOn = toBoolFlexible(doc["on"], toBoolFlexible(doc["state"], relayOn));
    uint32_t seconds = 0;
    if (doc["seconds"].is<int>()) seconds = static_cast<uint32_t>(doc["seconds"].as<int>());
    if (desiredOn) setRelayState(true, seconds, "zdalne plug/set");
    else setRelayState(false, 0, "zdalne plug/set");
    publishZones();
    publishStatus(true);
    publishPlugTelemetry(true);
    mqttAck(commandId, commandTopic, "accepted", "plug ustawiony");
    return;
  }

  if (leaf == "cmd/energy/reset") {
    telemetry.energyTodayWh = 0.0f;
    telemetry.energySessionWh = 0.0f;
    telemetry.energyTotalWh = 0.0f;
    telemetry.sessionStartWh = 0.0f;
    saveEnergy();
    addLogLine("MQTT CMD: reset licznikow energii");
    publishPlugTelemetry(true);
    mqttAck(commandId, commandTopic, "accepted", "liczniki zresetowane");
    return;
  }

  mqttAck(commandId, commandTopic, "failed", "nieobslugiwane polecenie");
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String text;
  text.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) text += static_cast<char>(payload[i]);

  const String fullTopic(topic);
  const String base = baseTopic() + "/";
  if (!fullTopic.startsWith(base)) return;
  const String leaf = fullTopic.substring(base.length());
  mqttHandleCommand(leaf, text);
}

static void mqttConfigure() {
  tlsClient.setInsecure();
#if defined(ARDUINO_ARCH_ESP8266)
  tlsClient.setBufferSizes(512, 512);
#endif
  mqttClient.setServer(settings.mqttServer.c_str(), settings.mqttPort);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setCallback(mqttCallback);
}

static void mqttEnsureConnected() {
  if (!settings.enableMqtt) {
    if (mqttConnectedFlag) mqttClient.disconnect();
    mqttConnectedFlag = false;
    mqttPostConnectSyncPending = false;
    return;
  }
  const unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    mqttConnectedFlag = false;
    mqttPostConnectSyncPending = false;
    return;
  }

  if (mqttConnectedFlag) return;
  if (now - lastMqttRetryMs < MQTT_RETRY_MS) return;
  lastMqttRetryMs = now;

  const String lwtTopic = topicLeaf("global/availability");
  const bool ok = mqttClient.connect(
    settings.mqttClientId.c_str(),
    settings.mqttUser.c_str(),
    settings.mqttPass.c_str(),
    lwtTopic.c_str(),
    1,
    true,
    "offline"
  );

  if (!ok) {
    if (now - mqttLastErrorLogMs >= 10000UL) {
      mqttLastErrorLogMs = now;
      addLogLine(
        "MQTT connect failed: state=" + String(mqttClient.state())
        + " host=" + settings.mqttServer
        + ":" + String(settings.mqttPort)
        + " user=" + settings.mqttUser
      );
    }
    return;
  }

  mqttConnectedFlag = true;
  mqttPostConnectSyncPending = true;
  mqttPostConnectAtMs = now;
  mqttLastLoopMs = now;

  mqttClient.publish(lwtTopic.c_str(), "online", true);
  mqttClient.subscribe(topicLeaf("global/refresh").c_str());
  mqttClient.subscribe(topicLeaf("cmd/#").c_str());
  addLogLine("MQTT connected");
}

static void wifiEnsureConnected() {
  wl_status_t st = WiFi.status();
  if (st != wifiLastObservedStatus) {
    wifiLastObservedStatus = st;
    if (st == WL_CONNECTED) {
      addLogLine(
        "WiFi status -> CONNECTED ip=" + WiFi.localIP().toString()
        + " bssid=" + WiFi.BSSIDstr()
        + " rssi=" + String(WiFi.RSSI())
      );
    } else {
      addLogLine("WiFi status -> " + String(wifiStatusLabel(st)));
    }
  }

  const unsigned long now = millis();
  if (pairState == PairState::PAIRING && !apEnabled && settings.ssid.length() == 0) {
    ensureApMode();
  }

  if (apEnabled) {
    if (settings.ssid.length() == 0) return;
    if (apModeRetryAtMs != 0 && (long)(now - apModeRetryAtMs) < 0) return;
    addLogLine("AP window elapsed (5 min) -> retrying saved WiFi");
    disableApMode(true);
    st = WiFi.status();
    wifiLastObservedStatus = st;
    lastWifiRetryMs = 0;
  }

  if (st == WL_CONNECTED) {
    wifiFailedAttempts = 0;
    disableApIfPossible();
    return;
  }

  if (now - lastWifiRetryMs < WIFI_RETRY_MS) return;
  lastWifiRetryMs = now;

  if (settings.ssid.length() == 0) {
    if (pairState == PairState::PAIRING) ensureApMode();
    return;
  }

  if (!connectWifiSta(10000)) {
    const bool deviceAssigned = settings.assignedToWms || pairState == PairState::ASSIGNED;
    if (wifiFailedAttempts >= WIFI_MAX_FAILS_BEFORE_PAIRING) {
      if (!deviceAssigned && pairState != PairState::PAIRING) {
        setPairState(PairState::PAIRING, "wifi retry failed");
      }
      ensureApMode();
      if (deviceAssigned) {
        addLogLine("WiFi: wiele błędów -> AP na 5 min, potem automatyczny retry STA");
      } else {
        addLogLine("WiFi: wiele błędów -> pairing/AP na 5 min, potem retry STA");
      }
    }
  }
}

static void loopProgramScheduler() {
  if (!settings.autoMode) {
    if (!schedulerAutoModeOffLogged) {
      addLogLine("Harmonogram: autoMode OFF - pomijam uruchamianie");
      schedulerAutoModeOffLogged = true;
    }
    return;
  }
  if (schedulerAutoModeOffLogged) {
    addLogLine("Harmonogram: autoMode ON");
    schedulerAutoModeOffLogged = false;
  }

  const unsigned long nowMs = millis();
  if (nowMs - lastSchedulerCheckMs < 10000UL) return;
  lastSchedulerCheckMs = nowMs;

  time_t now = time(nullptr);
  if (now < 100000) {
    if (nowMs - lastSchedulerNoTimeLogMs >= 300000UL) {
      lastSchedulerNoTimeLogMs = nowMs;
      addLogLine("Harmonogram: brak poprawnego czasu (NTP) - pomijam");
    }
    return;
  }

  struct tm localTm {};
  localtime_r(&now, &localTm);
  const int today = localTm.tm_wday;
  const int nowMins = localTm.tm_hour * 60 + localTm.tm_min;

  bool changed = false;

  for (int i = 0; i < programCount; ++i) {
    ProgramEntry& p = programs[i];
    if (!p.used || !p.active) continue;
    if (!parseDayInCsv(p.days, today)) continue;

    int hh = 0;
    int mm = 0;
    if (!parseTimeString(p.time, hh, mm)) continue;
    const int progMins = hh * 60 + mm;
    if (progMins != nowMins) continue;

    bool alreadyToday = false;
    if (p.lastRun > 0) {
      struct tm runTm {};
      localtime_r(&p.lastRun, &runTm);
      alreadyToday = (runTm.tm_yday == localTm.tm_yday && runTm.tm_year == localTm.tm_year);
    }
    if (alreadyToday) {
      if (localTm.tm_sec < 10) {
        addLogLine("Harmonogram #" + String(i) + ": pomijam - juz wykonany dzisiaj");
      }
      continue;
    }

    const uint32_t seconds = static_cast<uint32_t>(p.duration) * 60UL;
    setRelayState(true, seconds, "harmonogram #" + String(i));
    p.lastRun = now;
    changed = true;

    publishZones();
    publishStatus(true);
    publishPlugTelemetry(true);
  }

  if (changed) {
    savePrograms();
    publishPrograms();
  }
}

static void loopRelayTimer() {
  if (!relayOn || relayEndAtMs == 0) return;
  if ((long)(millis() - relayEndAtMs) >= 0) {
    setRelayState(false, 0, "timer");
    publishZones();
    publishStatus(true);
    publishPlugTelemetry(true);
  }
}

static void loopTelemetry() {
  static unsigned long lastInvalidLogMs = 0;
  static unsigned long lastIdleNoiseLogMs = 0;
  const unsigned long now = millis();

  if (now - telemetry.lastModeToggleMs >= 2000UL) {
    telemetry.lastModeToggleMs = now;
    hlw.toggleMode();
    telemetry.modeCurrent = !telemetry.modeCurrent;
  }

  if (telemetry.lastSampleMs == 0) telemetry.lastSampleMs = now;
  const unsigned long dtMs = now - telemetry.lastSampleMs;
  if (dtMs < 1000UL) return;
  telemetry.lastSampleMs = now;

  float rawPower = hlw.getActivePower();
  float rawVoltage = hlw.getVoltage();
  float rawCurrent = hlw.getCurrent();
  float rawPf = hlw.getPowerFactor();

  bool powerValid = isfinite(rawPower) && rawPower >= 0.0f && rawPower <= TELEMETRY_MAX_POWER_W;
  bool currentValid = isfinite(rawCurrent) && rawCurrent >= 0.0f && rawCurrent <= TELEMETRY_MAX_CURRENT_A;
  bool voltageValid = isfinite(rawVoltage) && rawVoltage >= 0.0f
                     && (rawVoltage == 0.0f || (rawVoltage >= TELEMETRY_MIN_VOLTAGE_V && rawVoltage <= TELEMETRY_MAX_VOLTAGE_V));
  bool pfValid = isfinite(rawPf) && rawPf >= 0.0f && rawPf <= 1.2f;

  if (!powerValid || !currentValid || !voltageValid || !pfValid) {
    if (now - lastInvalidLogMs >= 3000UL) {
      lastInvalidLogMs = now;
      Serial.printf(
        "[TEL] invalid raw sample p=%.3f v=%.3f i=%.3f pf=%.3f valid[p=%d v=%d i=%d pf=%d]\r\n",
        rawPower, rawVoltage, rawCurrent, rawPf,
        powerValid ? 1 : 0, voltageValid ? 1 : 0, currentValid ? 1 : 0, pfValid ? 1 : 0
      );
    }
  }

  telemetry.activePower = powerValid ? rawPower : 0.0f;
  telemetry.voltage = voltageValid ? rawVoltage : 0.0f;
  telemetry.current = currentValid ? rawCurrent : 0.0f;
  telemetry.powerFactor = pfValid ? rawPf : 0.0f;

  if (!relayOn && (telemetry.activePower > TELEMETRY_IDLE_POWER_W || telemetry.current > TELEMETRY_IDLE_CURRENT_A)) {
    if (now - lastIdleNoiseLogMs >= 3000UL) {
      lastIdleNoiseLogMs = now;
      Serial.printf(
        "[TEL] relay OFF but non-zero sample p=%.3f v=%.3f i=%.4f -> forced to 0\r\n",
        telemetry.activePower, telemetry.voltage, telemetry.current
      );
    }
    telemetry.activePower = 0.0f;
    telemetry.current = 0.0f;
    telemetry.powerFactor = 0.0f;
  }

  telemetry.apparentPower = telemetry.voltage * telemetry.current;
  const float sq = telemetry.apparentPower * telemetry.apparentPower - telemetry.activePower * telemetry.activePower;
  telemetry.reactivePower = (sq > 0.0f) ? sqrtf(sq) : 0.0f;

  const float dtHours = static_cast<float>(dtMs) / 3600000.0f;
  const float addWh = telemetry.activePower * dtHours;
  if (relayOn && isfinite(addWh) && addWh >= 0.0f && addWh < 5.0f) {
    telemetry.energyTotalWh += addWh;
    telemetry.energyTodayWh += addWh;
  } else if (relayOn && (!isfinite(addWh) || addWh < 0.0f || addWh >= 5.0f)) {
    addLogLine("Pominięto podejrzany przyrost energii addWh=" + String(addWh, 6));
  }

  if (relayOn) {
    telemetry.energySessionWh = telemetry.energyTotalWh - telemetry.sessionStartWh;
    if (telemetry.energySessionWh < 0.0f) telemetry.energySessionWh = 0.0f;
  }

  time_t nowTime = time(nullptr);
  if (nowTime > 100000) {
    struct tm t {};
    localtime_r(&nowTime, &t);
    if (telemetry.dayOfYear < 0 || telemetry.year < 0) {
      telemetry.dayOfYear = t.tm_yday;
      telemetry.year = t.tm_year;
    } else if (t.tm_yday != telemetry.dayOfYear || t.tm_year != telemetry.year) {
      telemetry.dayOfYear = t.tm_yday;
      telemetry.year = t.tm_year;
      telemetry.energyTodayWh = 0.0f;
      saveEnergy();
    }
  }

  if (now - lastEnergyFlushMs >= ENERGY_FLUSH_MS) {
    lastEnergyFlushMs = now;
    saveEnergy();
  }

  if (now - telemetryDebugLastMs >= TELEMETRY_DEBUG_MS) {
    telemetryDebugLastMs = now;
    Serial.printf(
      "[TEL] relay=%d raw[p=%.3f v=%.3f i=%.4f pf=%.3f] filt[p=%.3f v=%.3f i=%.4f pf=%.3f] addWh=%.6f totalWh=%.3f todayWh=%.3f cf1=%u mode=%s\r\n",
      relayOn ? 1 : 0,
      rawPower, rawVoltage, rawCurrent, rawPf,
      telemetry.activePower, telemetry.voltage, telemetry.current, telemetry.powerFactor,
      addWh, telemetry.energyTotalWh, telemetry.energyTodayWh,
      static_cast<unsigned>(settings.cf1Pin),
      telemetry.modeCurrent ? "current" : "voltage"
    );
  }
}

static void maybeFlushLogs() {
  const unsigned long now = millis();
  if (!logsDirty) return;
  if (now - lastLogFlushMs < LOG_FLUSH_MS) return;
  lastLogFlushMs = now;
  saveLogs();
}

static void loopPairingState() {
  if (pairState != PairState::PAIRING) return;
  if (pairingStartedMs == 0) pairingStartedMs = millis();
  if (millis() - pairingStartedMs < PAIRING_TIMEOUT_MS) return;

  setPairState(settings.assignedToWms ? PairState::ASSIGNED : PairState::UNASSIGNED, "pairing timeout");
  if (WiFi.status() == WL_CONNECTED) disableApIfPossible();
}

static void handleButton() {
  const bool reading = readButtonPressedRaw();

  if (reading != buttonLastReading) {
    buttonLastDebounceMs = millis();
    const int btnPrimaryRaw = readButtonPinRaw(PIN_BUTTON);
    const int btnAltRaw = readButtonPinRaw(PIN_BUTTON_ALT);
    const int btnAlt2Raw = readButtonPinRaw(PIN_BUTTON_ALT2);
    const int btnAlt3Raw = readButtonPinRaw(PIN_BUTTON_ALT3);
    const int btnAlt4Raw = readButtonPinRaw(PIN_BUTTON_ALT4);
    Serial.printf(
      "[BTN] raw change -> %d (btn%u=%d btn%u=%d btn%u=%d btn%u=%d btn%u=%d)\r\n",
      reading ? 1 : 0,
      static_cast<unsigned>(PIN_BUTTON), btnPrimaryRaw,
      static_cast<unsigned>(PIN_BUTTON_ALT), btnAltRaw,
      static_cast<unsigned>(PIN_BUTTON_ALT2), btnAlt2Raw,
      static_cast<unsigned>(PIN_BUTTON_ALT3), btnAlt3Raw,
      static_cast<unsigned>(PIN_BUTTON_ALT4), btnAlt4Raw
    );
    buttonLastReading = reading;
  }

  if (millis() - buttonLastDebounceMs < BUTTON_DEBOUNCE_MS) return;

  if (reading != buttonStablePressed) {
    buttonStablePressed = reading;
    if (buttonStablePressed) {
      buttonPressStartMs = millis();
      buttonLongHandled = false;
      addLogLine("Przycisk: press");
    } else {
      const unsigned long heldMs = millis() - buttonPressStartMs;
      addLogLine("Przycisk: release after " + String(heldMs) + " ms");
      if (!buttonLongHandled) {
        if (heldMs < BUTTON_MIN_CLICK_MS) {
          addLogLine("Przycisk: zbyt krotki impuls, ignoruje");
        } else {
          if (relayOn) setRelayState(false, 0, "button");
          else setRelayState(true, 0, "button");
          publishZones();
          publishStatus(true);
          publishPlugTelemetry(true);
        }
      }
    }
  }

  if (buttonStablePressed && !buttonLongHandled) {
    const unsigned long heldMs = millis() - buttonPressStartMs;

#if WMS_BWSHP6_ENABLE_BUTTON_FALLBACKS
    const bool assignedMode = settings.assignedToWms || pairState == PairState::ASSIGNED;
    if (assignedMode && !buttonPrimaryConfirmedLow && heldMs >= BUTTON_AUTO_TAP_RELEASE_MS) {
      buttonStablePressed = false;
      buttonLastReading = false;
      buttonLongHandled = true;
      buttonPrimaryPulseHoldUntilMs = 0;
      buttonPrimaryLowSinceMs = 0;
      addLogLine("Przycisk: auto-release tap after " + String(heldMs) + " ms");
      if (relayOn) setRelayState(false, 0, "button");
      else setRelayState(true, 0, "button");
      publishZones();
      publishStatus(true);
      publishPlugTelemetry(true);
      return;
    }
#endif

    if (heldMs >= BUTTON_FACTORY_RESET_MS) {
      buttonLongHandled = true;
      addLogLine("Przytrzymanie przycisku: factory reset konfiguracji");
      settings.ssid = "";
      settings.pass = "";
      settings.assignedToWms = false;
      settings.assignedAccount = "";
      saveSettings();
      saveLogs();
      delay(250);
      ESP.restart();
      return;
    }

    if (heldMs >= BUTTON_PAIR_PRESS_MS && pairState != PairState::PAIRING) {
      buttonLongHandled = true;
      addLogLine("Przytrzymanie przycisku: wejście w pairing");
      settings.assignedToWms = false;
      settings.assignedAccount = "";
      saveSettings();
      setPairState(PairState::PAIRING, "button 3s");
      ensureApMode();
      publishStatus(true);
      publishSettingsPublic();
    }
  }
}

static void sendJson(const JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  server.send(200, "application/json", payload);
}

static bool parseRequestJson(JsonDocument& doc) {
  if (!server.hasArg("plain")) {
    Serial.printf("[HTTP] %s missing plain body (args=%u)\r\n", server.uri().c_str(), static_cast<unsigned>(server.args()));
    return false;
  }
  const String body = server.arg("plain");
  if (body.length() == 0) {
    Serial.printf("[HTTP] %s empty plain body\r\n", server.uri().c_str());
    return false;
  }
  // For form submissions we may still get "plain", but it's not JSON.
  const char first = body.charAt(0);
  if (first != '{' && first != '[') {
    Serial.printf("[HTTP] %s plain body is non-json (len=%u)\r\n", server.uri().c_str(), static_cast<unsigned>(body.length()));
    return false;
  }
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[HTTP] %s json parse error: %s (len=%u)\r\n", server.uri().c_str(), err.c_str(), static_cast<unsigned>(body.length()));
    return false;
  }
  Serial.printf("[HTTP] %s json parsed ok (len=%u)\r\n", server.uri().c_str(), static_cast<unsigned>(body.length()));
  return true;
}

static bool readJsonStringAlias(JsonVariantConst src, String& out, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr, const char* k4 = nullptr) {
  if (k1 && src[k1].is<const char*>()) {
    out = String(src[k1].as<const char*>());
    return true;
  }
  if (k2 && src[k2].is<const char*>()) {
    out = String(src[k2].as<const char*>());
    return true;
  }
  if (k3 && src[k3].is<const char*>()) {
    out = String(src[k3].as<const char*>());
    return true;
  }
  if (k4 && src[k4].is<const char*>()) {
    out = String(src[k4].as<const char*>());
    return true;
  }
  return false;
}

static bool readRequestArgAlias(String& out, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr, const char* k4 = nullptr) {
  if (k1 && server.hasArg(k1)) {
    out = server.arg(k1);
    return true;
  }
  if (k2 && server.hasArg(k2)) {
    out = server.arg(k2);
    return true;
  }
  if (k3 && server.hasArg(k3)) {
    out = server.arg(k3);
    return true;
  }
  if (k4 && server.hasArg(k4)) {
    out = server.arg(k4);
    return true;
  }
  return false;
}

static bool buildSettingsPatchFromRequestArgs(JsonDocument& outPatch) {
  bool hasAny = false;
  String v;

  if (readRequestArgAlias(v, "ssid", "wifiSsid", "wifi_ssid", "network_ssid")) {
    outPatch["ssid"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "pass", "password", "wifiPass", "wifi_password")) {
    outPatch["pass"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttServer", "mqtt_server")) {
    outPatch["mqttServer"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttPort", "mqtt_port")) {
    outPatch["mqttPort"] = v.toInt();
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttUser", "mqtt_user")) {
    outPatch["mqttUser"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttPass", "mqtt_password")) {
    outPatch["mqttPass"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttClientId", "mqtt_client_id")) {
    outPatch["mqttClientId"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "mqttTopicBase", "mqtt_topic")) {
    outPatch["mqttTopicBase"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "timezone", "tz")) {
    outPatch["timezone"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "zoneName", "zone_name")) {
    outPatch["zoneName"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "assignedToWms", "assigned_to_wms")) {
    outPatch["assignedToWms"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "assignedAccount", "assigned_account", "account_id", "owner_user_id")) {
    outPatch["assignedAccount"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "relayInverted", "relay_inverted")) {
    outPatch["relayInverted"] = v;
    hasAny = true;
  }
  if (readRequestArgAlias(v, "telemetryIntervalSec", "telemetry_interval_sec")) {
    outPatch["telemetryIntervalSec"] = v.toInt();
    hasAny = true;
  }
  if (readRequestArgAlias(v, "cf1Pin", "cf1_pin")) {
    outPatch["cf1Pin"] = v.toInt();
    hasAny = true;
  }
  if (readRequestArgAlias(v, "hlwVoltageMultiplier", "hlw_voltage_multiplier")) {
    outPatch["hlwVoltageMultiplier"] = v.toFloat();
    hasAny = true;
  }
  if (readRequestArgAlias(v, "hlwCurrentMultiplier", "hlw_current_multiplier")) {
    outPatch["hlwCurrentMultiplier"] = v.toFloat();
    hasAny = true;
  }
  if (readRequestArgAlias(v, "hlwPowerMultiplier", "hlw_power_multiplier")) {
    outPatch["hlwPowerMultiplier"] = v.toFloat();
    hasAny = true;
  }

  return hasAny;
}

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="pl">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>WMS Smart Plug</title>
  <style>
    :root { --bg:#0b1220; --card:#131c31; --text:#e7edf9; --muted:#9eb0d2; --accent:#2dd4bf; --danger:#fb7185; }
    body { margin:0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: radial-gradient(1000px 600px at 80% -20%, #1f335b 0%, var(--bg) 60%); color:var(--text); }
    .wrap { max-width: 900px; margin: 24px auto; padding: 0 14px; }
    .grid { display:grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap:12px; }
    .card { background:var(--card); border:1px solid rgba(255,255,255,.08); border-radius:14px; padding:14px; }
    .label { color:var(--muted); font-size:12px; }
    .val { font-size:24px; margin-top:6px; font-weight:700; }
    button { border:0; border-radius:10px; padding:11px 14px; color:#08111f; background:var(--accent); cursor:pointer; font-weight:700; }
    button.secondary { background:#334155; color:#e2e8f0; }
    button.danger { background:var(--danger); color:#fff; }
    input, select { width:100%; border-radius:10px; border:1px solid #314267; background:#0c1527; color:#e2e8f0; padding:9px; }
    .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
    .row > * { flex:1; min-width:120px; }
    .prog { border:1px solid rgba(255,255,255,.08); border-radius:10px; padding:8px; margin-top:8px; display:flex; justify-content:space-between; align-items:center; gap:8px; }
    .small { color:var(--muted); font-size:12px; }
  </style>
</head>
<body>
  <div class="wrap">
    <h2>WMS Smart Plug BW-SHP6</h2>
    <div class="small" id="device"></div>

    <div class="grid" style="margin-top:12px;">
      <div class="card"><div class="label">Stan</div><div class="val" id="relay">-</div></div>
      <div class="card"><div class="label">Moc</div><div class="val" id="power">-</div></div>
      <div class="card"><div class="label">Napięcie</div><div class="val" id="voltage">-</div></div>
      <div class="card"><div class="label">Prąd</div><div class="val" id="current">-</div></div>
      <div class="card"><div class="label">Energia dzisiaj</div><div class="val" id="eday">-</div></div>
      <div class="card"><div class="label">Energia całkowita</div><div class="val" id="etotal">-</div></div>
    </div>

    <div class="card" style="margin-top:12px;">
      <div class="row">
        <button id="toggleBtn">Przełącz</button>
        <button class="secondary" id="refreshBtn">Odśwież</button>
        <button class="danger" id="resetEnergyBtn">Reset energii</button>
      </div>
    </div>

    <div class="card" style="margin-top:12px;">
      <h3>Dodaj harmonogram</h3>
      <div class="row">
        <div><label>Godzina<input id="time" type="time" value="06:00"></label></div>
        <div><label>Czas (min)<input id="duration" type="number" min="1" max="1440" value="10"></label></div>
      </div>
      <div class="row" style="margin-top:8px;">
        <label><input type="checkbox" class="day" value="1" checked>Pon</label>
        <label><input type="checkbox" class="day" value="2" checked>Wt</label>
        <label><input type="checkbox" class="day" value="3" checked>Śr</label>
        <label><input type="checkbox" class="day" value="4" checked>Czw</label>
        <label><input type="checkbox" class="day" value="5" checked>Pt</label>
        <label><input type="checkbox" class="day" value="6" checked>Sob</label>
        <label><input type="checkbox" class="day" value="0" checked>Nd</label>
      </div>
      <div class="row" style="margin-top:8px;">
        <button id="addProgramBtn">Dodaj</button>
      </div>
      <div id="programs"></div>
    </div>

    <div class="card" style="margin-top:12px;">
      <h3>WiFi i MQTT</h3>
      <div class="row">
        <div><label>SSID<input id="ssid" /></label></div>
        <div><label>Hasło WiFi<input id="pass" type="password" /></label></div>
      </div>
      <div class="row" style="margin-top:8px;">
        <div><label>MQTT host<input id="mqttServer" /></label></div>
        <div><label>MQTT port<input id="mqttPort" type="number" min="1" max="65535" /></label></div>
      </div>
      <div class="row" style="margin-top:8px;">
        <div><label>MQTT user<input id="mqttUser" /></label></div>
        <div><label>MQTT pass<input id="mqttPass" type="password" placeholder="(bez zmian: zostaw puste)"/></label></div>
      </div>
      <div class="row" style="margin-top:8px;">
        <button id="saveSettingsBtn">Zapisz ustawienia</button>
      </div>
    </div>
  </div>

  <script>
    async function j(url, opt) {
      const r = await fetch(url, opt);
      if (!r.ok) throw new Error(await r.text());
      return r.json();
    }

    function fmt(v, unit, d = 2) {
      const n = Number(v || 0);
      return `${n.toFixed(d)} ${unit}`;
    }

    function readDays() {
      return [...document.querySelectorAll('.day:checked')].map(x => Number(x.value));
    }

    async function renderPrograms() {
      const list = await j('/api/programs');
      const root = document.getElementById('programs');
      root.innerHTML = '';
      (list || []).forEach((p) => {
        const row = document.createElement('div');
        row.className = 'prog';
        const days = (p.days || []).join(',');
        row.innerHTML = `<div>#${p.id} ${p.time} / ${p.duration} min / dni: ${days}</div><button class="danger">Usuń</button>`;
        row.querySelector('button').onclick = async () => {
          await fetch(`/api/programs/${p.id}`, { method: 'DELETE' });
          await renderPrograms();
        };
        root.appendChild(row);
      });
    }

    async function refresh() {
      const [status, telem, settings] = await Promise.all([
        j('/api/status'),
        j('/api/plug/telemetry'),
        j('/api/settings')
      ]);

      document.getElementById('device').textContent = `${status.device_id} | ${status.ip} | FW ${status.fw_version}`;
      document.getElementById('relay').textContent = telem.relay_on ? 'ON' : 'OFF';
      document.getElementById('power').textContent = fmt(telem.power_w, 'W');
      document.getElementById('voltage').textContent = fmt(telem.voltage_v, 'V');
      document.getElementById('current').textContent = fmt(telem.current_a, 'A');
      document.getElementById('eday').textContent = fmt(telem.energy_today_kwh, 'kWh', 3);
      document.getElementById('etotal').textContent = fmt(telem.energy_total_kwh, 'kWh', 3);

      document.getElementById('ssid').value = settings.ssid || '';
      document.getElementById('mqttServer').value = settings.mqttServer || '';
      document.getElementById('mqttPort').value = settings.mqttPort || 8883;
      document.getElementById('mqttUser').value = settings.mqttUser || '';
    }

    document.getElementById('toggleBtn').onclick = async () => {
      await j('/api/relay/toggle', { method: 'POST' });
      await refresh();
    };

    document.getElementById('refreshBtn').onclick = async () => {
      await refresh();
      await renderPrograms();
    };

    document.getElementById('resetEnergyBtn').onclick = async () => {
      if (!confirm('Wyzerować liczniki energii?')) return;
      await j('/api/energy/reset', { method: 'POST' });
      await refresh();
    };

    document.getElementById('addProgramBtn').onclick = async () => {
      const body = {
        time: document.getElementById('time').value,
        duration: Number(document.getElementById('duration').value || 10),
        zone: 0,
        active: true,
        days: readDays()
      };
      await j('/api/programs', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      });
      await renderPrograms();
    };

    document.getElementById('saveSettingsBtn').onclick = async () => {
      const body = {
        ssid: document.getElementById('ssid').value,
        pass: document.getElementById('pass').value,
        mqttServer: document.getElementById('mqttServer').value,
        mqttPort: Number(document.getElementById('mqttPort').value || 8883),
        mqttUser: document.getElementById('mqttUser').value,
        mqttPass: document.getElementById('mqttPass').value
      };
      try {
        const wifiIntent = String(body.ssid || '').trim().length > 0;
        if (wifiIntent) {
          const params = new URLSearchParams();
          params.set('ssid', body.ssid || '');
          params.set('pass', body.pass || '');
          params.set('mqttServer', body.mqttServer || '');
          params.set('mqttPort', String(body.mqttPort || 8883));
          params.set('mqttUser', body.mqttUser || '');
          params.set('mqttPass', body.mqttPass || '');
          const r = await fetch('/api/wifi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
            body: params.toString()
          });
          if (!r.ok) throw new Error(await r.text());
          alert('Provisioning zapisany. Urządzenie restartuje się.');
        } else {
          await j('/api/settings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body)
          });
          alert('Ustawienia zapisane');
        }
      } catch (e) {
        alert('Błąd zapisu ustawień: ' + (e && e.message ? e.message : 'unknown'));
      }
    };

    refresh().catch(console.error);
    renderPrograms().catch(console.error);
    setInterval(() => refresh().catch(console.error), 3000);
  </script>
</body>
</html>
)HTML";

static void handleRoot() {
  serialHttpRequest("root");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static const char WIFI_FORM_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="pl">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>WMS Plug WiFi Setup</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; margin: 0; background: #f3f4f6; color: #111827; }
    .wrap { max-width: 520px; margin: 18px auto; background: #fff; border-radius: 12px; padding: 16px; box-shadow: 0 10px 24px rgba(0,0,0,.08); }
    h2 { margin-top: 0; }
    label { display:block; font-size: 14px; margin-top: 10px; }
    input { width: 100%; box-sizing: border-box; padding: 10px; border: 1px solid #c7ced9; border-radius: 8px; margin-top: 4px; }
    button { margin-top: 14px; width: 100%; padding: 11px; border: 0; border-radius: 8px; background: #0ea5e9; color: #fff; font-weight: 700; }
    .small { margin-top: 10px; font-size: 12px; color: #4b5563; }
  </style>
</head>
<body>
  <div class="wrap">
    <h2>Konfiguracja WiFi (tryb awaryjny)</h2>
    <form method="POST" action="/api/wifi">
      <label>SSID
        <input name="ssid" maxlength="32" required />
      </label>
      <label>Hasło
        <input name="pass" type="password" maxlength="64" />
      </label>
      <label>MQTT host
        <input name="mqttServer" value="wmsprinkler.pl" />
      </label>
      <label>MQTT port
        <input name="mqttPort" value="8883" />
      </label>
      <button type="submit">Zapisz i restartuj</button>
    </form>
    <div class="small">Po wysłaniu urządzenie zapisze ustawienia i uruchomi się ponownie.</div>
  </div>
</body>
</html>
)HTML";

static void handleWifiForm() {
  serialHttpRequest("wifi form");
  server.send_P(200, "text/html; charset=utf-8", WIFI_FORM_HTML);
}

static void handleApiStatus() {
  serialHttpRequest("status");
  JsonDocument doc;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
  doc["ip"] = wifiIpOrDash();
  doc["time"] = nowTimestamp();
  doc["device_id"] = identity.deviceId;
  doc["fw_version"] = FW_VERSION;
  doc["online"] = (WiFi.status() == WL_CONNECTED);
  doc["relay_on"] = relayOn;
  doc["remaining_sec"] = relayRemainingSeconds();
  doc["mqtt_connected"] = mqttConnectedFlag;
  doc["mqtt_state"] = mqttClient.state();
  doc["hardware"] = "bwshp6";
  doc["device_type"] = "smart_plug";
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assigned_to_wms"] = settings.assignedToWms;
  doc["assigned_account"] = settings.assignedAccount;
  doc["ble_supported"] = false;
  doc["provisioning_method"] = "wifi_ap";
  if (WiFi.status() == WL_CONNECTED) {
    doc["ssid"] = WiFi.SSID();
    doc["bssid"] = WiFi.BSSIDstr();
  }
  sendJson(doc);
}

static void handleApiDevice() {
  JsonDocument doc;
  doc["device_id"] = identity.deviceId;
  doc["claim_code"] = identity.claimCode;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assigned_to_wms"] = settings.assignedToWms;
  sendJson(doc);
}

static void handleApiTelemetry() {
  JsonDocument doc;
  doc["relay_on"] = relayOn;
  doc["remaining_sec"] = relayRemainingSeconds();
  doc["power_w"] = telemetry.activePower;
  doc["voltage_v"] = telemetry.voltage;
  doc["current_a"] = telemetry.current;
  doc["apparent_power_va"] = telemetry.apparentPower;
  doc["reactive_power_var"] = telemetry.reactivePower;
  doc["power_factor"] = telemetry.powerFactor;
  doc["energy_total_kwh"] = telemetry.energyTotalWh / 1000.0f;
  doc["energy_today_kwh"] = telemetry.energyTodayWh / 1000.0f;
  doc["energy_session_kwh"] = telemetry.energySessionWh / 1000.0f;
  doc["sampled_at"] = nowTimestamp();
  sendJson(doc);
}

static void handleApiZones() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  fillZonesJson(arr);
  sendJson(doc);
}

static void handleApiProgramsGet() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  fillProgramsJson(arr, false);
  sendJson(doc);
}

static void handleApiLogs() {
  JsonDocument doc;
  JsonArray arr = doc["logs"].to<JsonArray>();
  for (int i = 0; i < logsCount; ++i) arr.add(logsBuffer[i]);
  sendJson(doc);
}

static void handleApiSettingsGet() {
  JsonDocument doc;
  doc["ssid"] = settings.ssid;
  doc["timezone"] = settings.timezone;
  doc["enableMqtt"] = settings.enableMqtt;
  doc["mqttServer"] = settings.mqttServer;
  doc["mqttPort"] = settings.mqttPort;
  doc["mqttUser"] = settings.mqttUser;
  doc["mqttClientId"] = settings.mqttClientId;
  doc["mqttTopicBase"] = baseTopic();
  doc["mqttPassConfigured"] = settings.mqttPass.length() > 0;
  doc["autoMode"] = settings.autoMode;
  doc["zoneName"] = settings.zoneName;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assignedToWms"] = settings.assignedToWms;
  doc["assignedAccount"] = settings.assignedAccount;
  doc["bleSupported"] = false;
  doc["provisioningMethod"] = "wifi_ap";
  doc["telemetryIntervalSec"] = settings.telemetryIntervalSec;
  doc["relayInverted"] = settings.relayInverted;
  doc["cf1Pin"] = settings.cf1Pin;
  doc["hlwVoltageMultiplier"] = settings.hlwVoltageMultiplier;
  doc["hlwCurrentMultiplier"] = settings.hlwCurrentMultiplier;
  doc["hlwPowerMultiplier"] = settings.hlwPowerMultiplier;
  sendJson(doc);
}

static void handleApiRelayToggle() {
  addLogLine("Local API /api/relay/toggle");
  if (relayOn) setRelayState(false, 0, "local api");
  else setRelayState(true, 0, "local api");
  publishZones();
  publishStatus(true);
  publishPlugTelemetry(true);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiRelaySet() {
  JsonDocument doc;
  if (!parseRequestJson(doc)) {
    addLogLine("Local API /api/relay/set: invalid json");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  const bool on = toBoolFlexible(doc["on"], relayOn);
  uint32_t secs = 0;
  if (doc["seconds"].is<int>()) secs = static_cast<uint32_t>(doc["seconds"].as<int>());
  addLogLine("Local API /api/relay/set: on=" + String(on ? 1 : 0) + " seconds=" + String(secs));

  if (on) setRelayState(true, secs, "local api");
  else setRelayState(false, 0, "local api");

  publishZones();
  publishStatus(true);
  publishPlugTelemetry(true);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiEnergyReset() {
  serialHttpRequest("energy/reset");
  telemetry.energyTodayWh = 0.0f;
  telemetry.energySessionWh = 0.0f;
  telemetry.energyTotalWh = 0.0f;
  telemetry.sessionStartWh = relayOn ? telemetry.energyTotalWh : 0.0f;
  saveEnergy();
  addLogLine("Local API /api/energy/reset: liczniki wyzerowane");
  publishPlugTelemetry(true);
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiSettingsPost() {
  serialHttpRequest("settings");
  JsonDocument doc;
  const bool hasJson = parseRequestJson(doc);

  JsonDocument argsPatch;
  const bool hasArgsPatch = buildSettingsPatchFromRequestArgs(argsPatch);
  if (!hasJson && !hasArgsPatch) {
    addLogLine("Local API /api/settings: invalid payload (no json/no args)");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid payload\"}");
    return;
  }

  bool mqttReconnect = false;
  bool wifiReconnect = false;
  bool rebootRequired = false;
  const JsonVariantConst src = hasJson ? doc.as<JsonVariantConst>() : argsPatch.as<JsonVariantConst>();
  const bool changed = applySettingsPatch(src, false, mqttReconnect, wifiReconnect, rebootRequired);
  addLogLine(
    "Local API /api/settings: src=" + String(hasJson ? "json" : "args")
    + " changed=" + String(changed ? 1 : 0)
    + " wifiReconnect=" + String(wifiReconnect ? 1 : 0)
    + " mqttReconnect=" + String(mqttReconnect ? 1 : 0)
    + " reboot=" + String(rebootRequired ? 1 : 0)
    + " ssid_len=" + String(settings.ssid.length())
    + " pass_len=" + String(settings.pass.length())
  );

  File verify = LittleFS.open("/settings.json", "r");
  if (verify) {
    JsonDocument verifyDoc;
    const DeserializationError verifyErr = deserializeJson(verifyDoc, verify);
    verify.close();
    if (!verifyErr) {
      addLogLine(
        "Local API /api/settings: verify ssid_len=" + String(String(verifyDoc["ssid"] | "").length())
        + " pass_len=" + String(String(verifyDoc["pass"] | "").length())
      );
    } else {
      addLogLine("Local API /api/settings: verify parse error " + String(verifyErr.c_str()));
    }
  } else {
    addLogLine("Local API /api/settings: verify open failed");
  }

  publishSettingsPublic();
  publishStatus(true);
  publishZones();

  server.send(200, "application/json", "{\"ok\":true}");

  if (wifiReconnect) {
    WiFi.disconnect();
    delay(200);
    connectWifiSta(WIFI_CONNECT_TIMEOUT_MS);
  }

  if (mqttReconnect) {
    mqttClient.disconnect();
  }

  if (rebootRequired) {
    addLogLine("Restart po zmianie kalibracji/pinu CF1");
    saveLogs();
    delay(250);
    ESP.restart();
  }
}

static void handleApiPairState() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["pair_state"] = pairStateLabelForExternal();
  doc["assigned_to_wms"] = settings.assignedToWms;
  doc["assigned_account"] = settings.assignedAccount;
  doc["ap_enabled"] = apEnabled;
  doc["ap_ssid"] = apEnabled ? apSsid : "";
  doc["ble_supported"] = false;
  doc["provisioning_method"] = "wifi_ap";
  sendJson(doc);
}

static void handleApiPairStart() {
  settings.assignedToWms = false;
  settings.assignedAccount = "";
  saveSettings();
  setPairState(PairState::PAIRING, "local api pair/start");
  ensureApMode();
  publishStatus(true);
  publishSettingsPublic();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiPairProvision() {
  serialHttpRequest("pair/provision");
  addLogLine("Local API /api/pair/provision: request");
  JsonDocument doc;
  const bool hasJson = parseRequestJson(doc);

  String argNames;
  for (uint8_t i = 0; i < server.args(); ++i) {
    if (argNames.length() > 0) argNames += ",";
    argNames += server.argName(i);
  }
  if (argNames.length() == 0) argNames = "-";
  addLogLine(
    "Local API /api/pair/provision: hasJson=" + String(hasJson ? 1 : 0)
    + " args=" + String(server.args())
    + " [" + argNames + "]"
  );

  if (!hasJson && server.args() == 0) {
    addLogLine("Local API /api/pair/provision: brak JSON i brak args");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid payload\"}");
    return;
  }

  String ssid;
  String wifiPass;
  String mqttServer;
  String mqttUser;
  String mqttPass;
  String mqttTopicBase;
  String assignedAccount;
  bool passProvided = false;
  bool mqttPortProvided = false;
  int mqttPort = settings.mqttPort;

  if (hasJson) {
    JsonVariantConst src = doc.as<JsonVariantConst>();
    readJsonStringAlias(src, ssid, "ssid", "wifiSsid", "wifi_ssid", "network_ssid");
    passProvided = readJsonStringAlias(src, wifiPass, "pass", "password", "wifiPass", "wifi_password");
    readJsonStringAlias(src, mqttServer, "mqttServer", "mqtt_server");
    readJsonStringAlias(src, mqttUser, "mqttUser", "mqtt_user");
    readJsonStringAlias(src, mqttPass, "mqttPass", "mqtt_password");
    readJsonStringAlias(src, mqttTopicBase, "mqttTopicBase", "mqtt_topic");
    readJsonStringAlias(src, assignedAccount, "assignedAccount", "account_id", "owner_user_id");
    if (src["mqttPort"].is<int>()) {
      mqttPort = src["mqttPort"].as<int>();
      mqttPortProvided = true;
    } else if (src["mqtt_port"].is<int>()) {
      mqttPort = src["mqtt_port"].as<int>();
      mqttPortProvided = true;
    }
  }

  if (ssid.length() == 0) {
    readRequestArgAlias(ssid, "ssid", "wifiSsid", "wifi_ssid", "network_ssid");
  }
  if (!passProvided) {
    passProvided = readRequestArgAlias(wifiPass, "pass", "password", "wifiPass", "wifi_password");
  }
  if (mqttServer.length() == 0) readRequestArgAlias(mqttServer, "mqttServer", "mqtt_server");
  if (mqttUser.length() == 0) readRequestArgAlias(mqttUser, "mqttUser", "mqtt_user");
  if (mqttPass.length() == 0) readRequestArgAlias(mqttPass, "mqttPass", "mqtt_password");
  if (mqttTopicBase.length() == 0) readRequestArgAlias(mqttTopicBase, "mqttTopicBase", "mqtt_topic");
  if (assignedAccount.length() == 0) readRequestArgAlias(assignedAccount, "assignedAccount", "account_id", "owner_user_id");
  if (!mqttPortProvided) {
    String mqttPortText;
    if (readRequestArgAlias(mqttPortText, "mqttPort", "mqtt_port")) {
      mqttPort = mqttPortText.toInt();
      mqttPortProvided = true;
    }
  }

  ssid.trim();
  if (ssid.length() == 0) {
    addLogLine("Local API /api/pair/provision: ssid required");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }

  settings.ssid = ssid;
  if (passProvided) settings.pass = wifiPass;
  if (mqttServer.length() > 0) settings.mqttServer = mqttServer;
  if (mqttPortProvided && mqttPort > 0) settings.mqttPort = mqttPort;
  if (mqttUser.length() > 0) settings.mqttUser = mqttUser;
  if (mqttPass.length() > 0) settings.mqttPass = mqttPass;
  if (mqttTopicBase.length() > 0) {
    const String topic = sanitizeTopicBase(mqttTopicBase);
    if (topic.length() > 0) settings.mqttTopicBase = topic;
  }
  settings.assignedAccount = assignedAccount;

  settings.assignedToWms = false;
  saveSettings();

  File verify = LittleFS.open("/settings.json", "r");
  if (verify) {
    JsonDocument verifyDoc;
    const DeserializationError verifyErr = deserializeJson(verifyDoc, verify);
    verify.close();
    if (!verifyErr) {
      const String verifySsid = String(verifyDoc["ssid"] | "");
      const String verifyPass = String(verifyDoc["pass"] | "");
      addLogLine(
        "Local API /api/pair/provision: verify file ssid_len=" + String(verifySsid.length())
        + " pass_len=" + String(verifyPass.length())
      );
    } else {
      addLogLine("Local API /api/pair/provision: verify parse error " + String(verifyErr.c_str()));
    }
  } else {
    addLogLine("Local API /api/pair/provision: verify open failed");
  }

  setPairState(PairState::PAIRING, "local api pair/provision");
  publishStatus(true);
  publishSettingsPublic();

  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  addLogLine(
    "Provisioning zapisany. Restart. ssid_len=" + String(settings.ssid.length())
    + " pass_len=" + String(settings.pass.length())
    + " mqtt=" + settings.mqttServer + ":" + String(settings.mqttPort)
  );
  delay(250);
  ESP.restart();
}

static void handleApiWifiLegacy() {
  serialHttpRequest("wifi legacy");
  addLogLine("Local API /api/wifi: alias -> /api/pair/provision");
  handleApiPairProvision();
}

static void handleApiProgramsPost() {
  JsonDocument doc;
  if (!parseRequestJson(doc)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  if (doc.is<JsonArrayConst>()) {
    int count = 0;
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
      if (count >= MAX_PROGRAMS) break;
      ProgramEntry p;
      if (!parseProgramFromVariant(v, p)) continue;
      programs[count++] = p;
    }
    programCount = count;
    savePrograms();
    addLogLine("Local API: import programow");
    publishPrograms();
    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  if (programCount >= MAX_PROGRAMS) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"limit\"}");
    return;
  }

  ProgramEntry p;
  if (!parseProgramFromVariant(doc.as<JsonVariantConst>(), p)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid program\"}");
    return;
  }

  programs[programCount++] = p;
  savePrograms();
  addLogLine("Local API: dodano program");
  publishPrograms();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleApiProgramDelete() {
  String uri = server.uri();
  const String prefix = "/api/programs/";
  if (!uri.startsWith(prefix)) {
    server.send(404, "application/json", "{\"ok\":false}");
    return;
  }
  const String tail = uri.substring(prefix.length());
  if (tail.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  for (size_t i = 0; i < tail.length(); ++i) {
    if (!isDigit(tail[i])) {
      server.send(400, "application/json", "{\"ok\":false}");
      return;
    }
  }

  const int id = tail.toInt();
  if (id < 0 || id >= programCount) {
    server.send(404, "application/json", "{\"ok\":false}");
    return;
  }

  for (int i = id; i < programCount - 1; ++i) programs[i] = programs[i + 1];
  --programCount;
  savePrograms();
  addLogLine("Local API: usunieto program " + String(id));
  publishPrograms();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void setupWeb() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifiForm);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/device", HTTP_GET, handleApiDevice);
  server.on("/api/plug/telemetry", HTTP_GET, handleApiTelemetry);
  server.on("/api/zones", HTTP_GET, handleApiZones);
  server.on("/api/programs", HTTP_GET, handleApiProgramsGet);
  server.on("/api/programs", HTTP_POST, handleApiProgramsPost);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.on("/api/settings", HTTP_GET, handleApiSettingsGet);
  server.on("/api/settings", HTTP_POST, handleApiSettingsPost);
  server.on("/api/wifi", HTTP_POST, handleApiWifiLegacy);
  server.on("/api/pair/state", HTTP_GET, handleApiPairState);
  server.on("/api/pair/start", HTTP_POST, handleApiPairStart);
  server.on("/api/pair/provision", HTTP_POST, handleApiPairProvision);
  server.on("/api/relay/toggle", HTTP_POST, handleApiRelayToggle);
  server.on("/api/relay/set", HTTP_POST, handleApiRelaySet);
  server.on("/api/energy/reset", HTTP_POST, handleApiEnergyReset);

  server.onNotFound([]() {
    serialHttpRequest("notFound");
    if (server.method() == HTTP_DELETE && server.uri().startsWith("/api/programs/")) {
      handleApiProgramDelete();
      return;
    }
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  server.begin();
  addLogLine("HTTP server started");
}

void setup() {
  Serial.begin(115200);
  delay(120);
  serialBoot("setup start");
  serialBoot(String("fw ") + FW_VERSION);
  serialBoot("reset reason: " + ESP.getResetReason());
  serialBoot("free heap at boot: " + String(ESP.getFreeHeap()));

  pinMode(PIN_RELAY, OUTPUT);
  if (PIN_RELAY_ALT >= 0) pinMode(static_cast<uint8_t>(PIN_RELAY_ALT), OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  if (auxLedEnabled()) pinMode(static_cast<uint8_t>(PIN_LED_AUX), OUTPUT);
  if (auxLed2Enabled()) pinMode(static_cast<uint8_t>(PIN_LED_AUX2), OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), onPrimaryButtonEdgeISR, FALLING);
  pinMode(PIN_BUTTON_ALT, INPUT_PULLUP);
  if (!pinReservedForAuxLed(PIN_BUTTON_ALT2)) {
    pinMode(PIN_BUTTON_ALT2, INPUT_PULLUP);
  } else {
    serialBoot("GPIO2 reserved for AUX LED; alt button on GPIO2 disabled");
  }
  if (!pinReservedForAuxLed(PIN_BUTTON_ALT3)) {
    pinMode(PIN_BUTTON_ALT3, INPUT_PULLUP);
  } else {
    serialBoot("GPIO16 reserved for AUX LED; alt button on GPIO16 disabled");
  }
  serialBoot(
    "LED map: primary GPIO" + String(PIN_LED)
    + ", aux GPIO" + String(PIN_LED_AUX)
    + ", aux2 GPIO" + String(PIN_LED_AUX2)
  );
  serialBoot(
    "BTN map: primary GPIO" + String(PIN_BUTTON)
    + ", alt GPIO" + String(PIN_BUTTON_ALT)
    + ", alt2 GPIO" + String(PIN_BUTTON_ALT2)
    + ", alt3 GPIO" + String(PIN_BUTTON_ALT3)
    + ", shared GPIO" + String(PIN_BUTTON_ALT4)
  );
  buttonPrimaryLastRaw = readButtonPinRaw(PIN_BUTTON);
  buttonPrimarySeenActivity = false;
  buttonPrimaryConfirmedLow = false;
  buttonPrimaryLowSinceMs = 0;
  buttonPrimaryPulseHoldUntilMs = 0;
  buttonPrimaryIrqLowSeen = false;
  buttonPrimaryIrqLastUs = 0;
  serialBoot("pins configured");

  setRelayOutput(false);
  serialBoot("relay forced OFF");

  const bool fsOk = LittleFS.begin();
  if (!fsOk) {
    Serial.println("[FS] mount failed");
  } else {
    serialBoot("LittleFS mounted");
  }

  serialBoot("loading identity/settings/programs/energy/logs");
  loadIdentity();
  loadSettings();
  loadPrograms();
  loadEnergy();
  loadLogs();
  applyBoardProfileMigrations();
  serialBoot("data loaded: " + identity.deviceId);

  pairState = settings.assignedToWms ? PairState::ASSIGNED : PairState::UNASSIGNED;
  serialBoot("pairState init: " + String(pairStateLabel(pairState)));
  addLogLine(
    "Boot config: ssid_len=" + String(settings.ssid.length())
    + " pass_len=" + String(settings.pass.length())
    + " assigned=" + String(settings.assignedToWms ? 1 : 0)
    + " relayInv=" + String(settings.relayInverted ? 1 : 0)
    + " cf1=" + String(settings.cf1Pin)
  );

  // Fallback for hardware revisions where pair button pin differs:
  // if the device is unassigned and has no WiFi credentials, force pairing/AP on boot.
  if (pairState == PairState::UNASSIGNED && settings.ssid.length() == 0) {
    setPairState(PairState::PAIRING, "auto no wifi creds");
  }

  applyTimezone();
  startNtp();
  serialBoot("time services started");

  configureHlw();
  mqttConfigure();
  serialBoot("power meter + mqtt configured");

  setupWeb();
  serialBoot("http started");

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  serialBoot("wifi stack tuned: autoReconnect=1 sleep=none");

  if (settings.ssid.length() > 0) {
    serialBoot("wifi connect attempt");
    if (!connectWifiSta(WIFI_CONNECT_TIMEOUT_MS)) {
      if (settings.assignedToWms || pairState == PairState::ASSIGNED) {
        addLogLine("Boot: WiFi connect fail, ale urządzenie przypisane -> zostaje ASSIGNED i będzie retry");
      } else {
        addLogLine("Boot: nieudane połączenie WiFi po starcie, przejście do pairingu/AP");
        if (pairState != PairState::PAIRING) {
          setPairState(PairState::PAIRING, "boot wifi connect failed");
        }
        ensureApMode();
      }
    }
  } else if (pairState == PairState::PAIRING) {
    serialBoot("pairing without ssid -> AP");
    ensureApMode();
  }

  addLogLine("Device ready: " + identity.deviceId);
  serialBoot("setup done");
}

void loop() {
  updateLedPattern();
  server.handleClient();

  wifiEnsureConnected();

  mqttEnsureConnected();
  if (mqttConnectedFlag) {
    const unsigned long now = millis();
    if (now - mqttLastLoopMs >= MQTT_LOOP_INTERVAL_MS) {
      mqttLastLoopMs = now;
      if (!mqttClient.loop()) {
        mqttConnectedFlag = false;
        mqttPostConnectSyncPending = false;
        addLogLine("MQTT loop rozłączony: state=" + String(mqttClient.state()));
      }
    }
    if (mqttConnectedFlag && mqttPostConnectSyncPending && (now - mqttPostConnectAtMs >= MQTT_POST_CONNECT_SYNC_DELAY_MS)) {
      mqttPostConnectSyncPending = false;
      publishAllSnapshots();
    }
    processPendingOta();
    publishStatus(false);
    publishPlugTelemetry(false);
  }

  loopRelayTimer();
  loopProgramScheduler();
  loopTelemetry();
  loopPairingState();
  handleButton();
  maybeFlushLogs();
  serialHeartbeat();
}
