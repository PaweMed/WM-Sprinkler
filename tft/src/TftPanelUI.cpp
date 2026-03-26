#include "TftPanelUI.h"

#include <ctype.h>
#include <FS.h>
#include <LittleFS.h>
#include <math.h>
#include <Preferences.h>
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include "fonts/SFNS9pt7b.h"
#include "fonts/SFNS12pt7b.h"
#include "fonts/SFNS18pt7b.h"

namespace {
constexpr int kDisplayWidth = 320;
constexpr int kDisplayHeight = 240;
constexpr int kTopBarHeight = 44;
constexpr int kBodyHeight = kDisplayHeight - kTopBarHeight;
constexpr unsigned long kSettingsPersistDelayMs = 1200;
constexpr int kDimmedBacklightPercent = 15;

constexpr int kBacklightPwmCh = 3;
constexpr int kBacklightPwmFreqHz = 5000;
constexpr int kBacklightPwmBits = 10;

constexpr int kDimTimeoutOptionsMin[3] = {1, 3, 5};
constexpr int kMinUserBrightnessPercent = 1;
// Krótsze okno dwukliku, żeby pojedynczy klik był bardziej naturalny.
constexpr unsigned long kDoubleClickWindowMs = 260;
constexpr unsigned long kLogsDoubleClickWindowMs = 420;
constexpr int kModalLinePx = 16;
constexpr int kModalVisibleTextPx = 114;
constexpr unsigned long kModalScrollbarHoldMs = 420;
constexpr unsigned long kModalScrollbarFadeStepMs = 28;
constexpr uint8_t kModalScrollbarFadeStep = 24;
constexpr int kModalX = 18;
constexpr int kModalY = 52;
constexpr int kModalW = 284;
constexpr int kModalH = 176;
constexpr int kModalTextX = kModalX + 10;
constexpr int kModalTextY = kModalY + 34;
constexpr int kModalTextW = kModalW - 24;
constexpr int kModalTextH = kModalH - 48;
constexpr int kModalTrackW = 4;
constexpr int kModalTrackX = kModalTextX + kModalTextW - 8;
constexpr int kModalTrackY = kModalTextY + 6;
constexpr int kModalTrackH = kModalTextH - 12;
constexpr int kLogsPanelX = 4;
constexpr int kLogsPanelY = 72;
constexpr int kLogsPanelW = 312;
constexpr int kLogsPanelH = 160;
constexpr int kLogsListX = kLogsPanelX + 8;
constexpr int kLogsListY = kLogsPanelY + 34;
constexpr int kLogsListW = kLogsPanelW - 18;
constexpr int kLogsListH = kLogsPanelH - 44;
constexpr int kLogsTrackW = 3;
constexpr int kLogsTrackX = kLogsListX + kLogsListW + 3;
constexpr int kLogsTrackY = kLogsListY + 4;
constexpr int kLogsTrackH = kLogsListH - 8;
constexpr int kLogsRowH = 22;
constexpr int kLogsVisibleRows = kLogsListH / kLogsRowH;
constexpr int kLogsCanvasX = kLogsListX + 4;
constexpr int kLogsCanvasY = kLogsListY + 4;
constexpr int kLogsCanvasW = (kLogsListX + kLogsListW - 4) - kLogsCanvasX;
constexpr int kLogsCanvasH = kLogsVisibleRows * kLogsRowH;
constexpr int kLogsTimeAreaW = 40;
constexpr int kLogsMsgClipX = 12 + kLogsTimeAreaW + 10;
constexpr int kLogsMsgClipPadRight = 2;
constexpr int kLogsMsgClipW = kLogsCanvasW - kLogsMsgClipX - kLogsMsgClipPadRight;
constexpr int kLogsMsgTextInset = 0;
constexpr int kLogsDividerGap = 2;
constexpr unsigned long kLogsMarqueeDelayMs = 1000;
constexpr unsigned long kLogsMarqueeTickMs = 80;
constexpr int kLogsMarqueeStepPx = 2;
constexpr int kLogsMarqueeHoldSteps = 7;
constexpr unsigned long kLogsScrollStepIntervalMs = 18;
constexpr unsigned long kTopBarClockCheckIntervalMs = 1000;
constexpr int kTopBarClockRegionX = 232;
constexpr int kTopBarClockRegionY = 5;
constexpr int kTopBarClockRegionW = 84;
constexpr int kTopBarClockRegionH = 34;
constexpr int kTopBarClockRightX = 312;
constexpr int kTopBarClockBaselineY = 30;
constexpr int kSettingsSliderX = 34;
constexpr int kSettingsSliderY = 109;
constexpr int kSettingsSliderW = 214;
constexpr int kSettingsSliderH = 8;
constexpr int kSettingsSliderKnobR = 7;
constexpr int kDimDialCx = 160;
constexpr int kDimDialCy = 120;
constexpr int kDimDialOuterR = 88;
constexpr int kDimDialInnerR = 68;

// TFT + encoder pin map.
#if defined(WMS_BOARD_ESP32C6_RELAY_X1_V11)
// ESP32-C6 Relay X1 V1.1:
// - GPIO19 = relay, GPIO2 = status LED, GPIO9 = button (nie używamy do TFT/ENC).
constexpr int kPinTftSck = 6;
constexpr int kPinTftMosi = 7;
constexpr int kPinTftCs = 18;
constexpr int kPinTftDc = 21;
constexpr int kPinTftRst = 20;
constexpr int kPinTftBl = 15;

// Na tej płytce wygodnie dostępne są GPIO22/GPIO23/GPIO10.
constexpr int kPinEncA = 22;
constexpr int kPinEncB = 23;
constexpr int kPinEncBtn = 10;
#else
constexpr int kPinTftSck = 18;
constexpr int kPinTftMosi = 19;
constexpr int kPinTftCs = 21;
constexpr int kPinTftDc = 22;
constexpr int kPinTftRst = 4;
constexpr int kPinTftBl = 15;

// Encoder EC11 pins.
constexpr int kPinEncA = 16;
constexpr int kPinEncB = 17;
constexpr int kPinEncBtn = 2;  // Change if GPIO2 causes boot-mode issues.
#endif

constexpr int kTileW = 68;
constexpr int kTileH = 48;
constexpr int kTileGapX = 8;
constexpr int kTileGapY = 10;
constexpr int kTileStartX = 12;
constexpr int kTileStartY = 76;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t blend565(uint16_t c1, uint16_t c2, uint8_t t) {
  const uint8_t r1 = (c1 >> 11) & 0x1F;
  const uint8_t g1 = (c1 >> 5) & 0x3F;
  const uint8_t b1 = c1 & 0x1F;
  const uint8_t r2 = (c2 >> 11) & 0x1F;
  const uint8_t g2 = (c2 >> 5) & 0x3F;
  const uint8_t b2 = c2 & 0x1F;

  const uint8_t r = (uint8_t)(r1 + ((int)(r2 - r1) * t) / 255);
  const uint8_t g = (uint8_t)(g1 + ((int)(g2 - g1) * t) / 255);
  const uint8_t b = (uint8_t)(b1 + ((int)(b2 - b1) * t) / 255);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t le16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t le32(const uint8_t* p) {
  return (uint32_t)(p[0] | (uint32_t)(p[1] << 8) | (uint32_t)(p[2] << 16) | (uint32_t)(p[3] << 24));
}

int32_t sle32(const uint8_t* p) {
  return (int32_t)le32(p);
}

constexpr uint16_t kBg = rgb565(3, 8, 18);
constexpr uint16_t kCard = rgb565(8, 22, 40);
constexpr uint16_t kCardAlt = rgb565(12, 34, 56);
constexpr uint16_t kStroke = rgb565(20, 96, 132);
constexpr uint16_t kStrokeStrong = rgb565(16, 205, 217);
constexpr uint16_t kTextMain = rgb565(235, 244, 255);
constexpr uint16_t kTextMuted = rgb565(124, 168, 190);
constexpr uint16_t kGood = rgb565(66, 216, 144);
constexpr uint16_t kWarn = rgb565(243, 168, 92);

constexpr uint16_t kTopBarFrom = rgb565(6, 22, 46);
constexpr uint16_t kTopBarTo = rgb565(2, 10, 26);

constexpr uint16_t kDimBg = rgb565(2, 8, 18);

struct LogTimestampParts {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
};

struct AnimatedLogLabel {
  String primaryText;
  String secondaryText;
  uint8_t primaryAlpha = 0;
  uint8_t secondaryAlpha = 0;
};

bool hasCloudTimestampPrefix(const String& text) {
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

int parseDigitsAt(const String& text, int pos, int len) {
  if (pos < 0 || len <= 0 || pos + len > (int)text.length()) return -1;
  int value = 0;
  for (int i = 0; i < len; ++i) {
    const char c = text[pos + i];
    if (!isDigit((unsigned char)c)) return -1;
    value = value * 10 + (c - '0');
  }
  return value;
}

int logTimestampMessageOffset(const String& text) {
  String s = text;
  s.trim();
  if (!hasCloudTimestampPrefix(s)) return 0;
  if (s.length() >= 21 && s[19] == ':' && s[20] == ' ') return 21;
  if (s.length() >= 20 && s[19] == ' ') return 20;
  return 19;
}

bool parseLogTimestampParts(const String& text, LogTimestampParts& out) {
  String s = text;
  s.trim();
  if (!hasCloudTimestampPrefix(s)) return false;
  out.year = parseDigitsAt(s, 0, 4);
  out.month = parseDigitsAt(s, 5, 2);
  out.day = parseDigitsAt(s, 8, 2);
  out.hour = parseDigitsAt(s, 11, 2);
  out.minute = parseDigitsAt(s, 14, 2);
  out.second = parseDigitsAt(s, 17, 2);
  if (out.year < 2000 || out.month < 1 || out.month > 12 || out.day < 1 || out.day > 31 ||
      out.hour < 0 || out.hour > 23 || out.minute < 0 || out.minute > 59 ||
      out.second < 0 || out.second > 59) {
    return false;
  }
  return true;
}

String extractLogTimeLabel(const String& line) {
  String s = line;
  s.trim();
  if (!hasCloudTimestampPrefix(s)) return "";
  return s.substring(11, 16);
}

String extractLogDateLabel(const String& line) {
  LogTimestampParts parts;
  if (!parseLogTimestampParts(line, parts)) return "";

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d.%02d", parts.day, parts.month);
  return String(buf);
}

String extractLogMessageText(const String& line) {
  String s = line;
  s.trim();
  const int offset = logTimestampMessageOffset(s);
  String out = offset > 0 ? s.substring(offset) : s;
  out.trim();
  return out;
}

String pickLogTimestampSource(const String& primary, const String& fallback) {
  if (hasCloudTimestampPrefix(primary)) return primary;
  if (hasCloudTimestampPrefix(fallback)) return fallback;
  return "";
}

String resolveLogTimestampSource(const String& storedTimestamp, const String& rawLine, const String& line) {
  if (storedTimestamp.length()) return storedTimestamp + ":";
  const String parsed = pickLogTimestampSource(rawLine, line);
  return parsed;
}

AnimatedLogLabel buildAnimatedLogLabel(const String& line, unsigned long nowMs) {
  AnimatedLogLabel out;
  out.primaryText = extractLogTimeLabel(line);
  if (!out.primaryText.length()) return out;

  out.secondaryText = extractLogDateLabel(line);
  if (!out.secondaryText.length()) {
    out.primaryAlpha = 255;
    return out;
  }

  constexpr unsigned long kHoldMs = 3000;
  constexpr unsigned long kFadeMs = 320;
  const unsigned long cycleMs = (2 * kHoldMs) + (2 * kFadeMs);
  const unsigned long phaseMs = cycleMs > 0 ? (nowMs % cycleMs) : 0;

  if (phaseMs < kHoldMs) {
    out.primaryAlpha = 255;
    return out;
  }

  if (phaseMs < kHoldMs + kFadeMs) {
    const uint8_t t = (uint8_t)(((phaseMs - kHoldMs) * 255UL) / kFadeMs);
    out.primaryAlpha = (uint8_t)(255 - t);
    out.secondaryAlpha = t;
    return out;
  }

  if (phaseMs < (2 * kHoldMs) + kFadeMs) {
    out.primaryAlpha = 0;
    out.secondaryAlpha = 255;
    return out;
  }

  const uint8_t t = (uint8_t)(((phaseMs - ((2 * kHoldMs) + kFadeMs)) * 255UL) / kFadeMs);
  out.primaryAlpha = t;
  out.secondaryAlpha = (uint8_t)(255 - t);
  return out;
}

uint32_t animatedLogLabelSignature(const String& line, unsigned long nowMs) {
  const AnimatedLogLabel state = buildAnimatedLogLabel(line, nowMs);
  uint32_t sig = 2166136261u;
  const String a = state.primaryText;
  const String b = state.secondaryText;
  for (size_t i = 0; i < a.length(); ++i) {
    sig ^= (uint8_t)a[i];
    sig *= 16777619u;
  }
  sig ^= state.primaryAlpha;
  sig *= 16777619u;
  for (size_t i = 0; i < b.length(); ++i) {
    sig ^= (uint8_t)b[i];
    sig *= 16777619u;
  }
  sig ^= state.secondaryAlpha;
  sig *= 16777619u;
  return sig;
}

uint16_t logAccentColor(const String& message) {
  String s = message;
  s.toLowerCase();
  if (s.startsWith("[strefa]") || s.indexOf("włączono") >= 0 || s.indexOf("uruchomiono") >= 0 || s.indexOf("start") >= 0) {
    return kGood;
  }
  if (s.startsWith("[failsafe]") || s.indexOf("offline") >= 0 || s.indexOf("błąd") >= 0 || s.indexOf("blad") >= 0) {
    return kWarn;
  }
  if (s.startsWith("[mqtt]") || s.startsWith("mqtt cmd") || s.startsWith("mqtt ack")) {
    return kStrokeStrong;
  }
  if (s.startsWith("[system]") || s.startsWith("[ota]")) {
    return rgb565(109, 214, 255);
  }
  return kStroke;
}

GFXcanvas16& logsCanvas() {
  static GFXcanvas16 canvas(kLogsCanvasW, kLogsCanvasH);
  return canvas;
}

GFXcanvas16& logsMessageCanvas() {
  static GFXcanvas16 canvas(kLogsMsgClipW, kLogsRowH);
  return canvas;
}
}  // namespace

TftPanelUI::TftPanelUI()
  : tft_(kPinTftCs, kPinTftDc, kPinTftRst),
    encoder_(kPinEncA, kPinEncB, kPinEncBtn) {}

void TftPanelUI::begin(Zones* zones, Weather* weather, Config* config, MQTTClient* mqtt, Logs* logs) {
  zones_ = zones;
  weather_ = weather;
  config_ = config;
  mqtt_ = mqtt;
  logs_ = logs;

  SPI.begin(kPinTftSck, -1, kPinTftMosi, kPinTftCs);
  tft_.init(240, 320);
  tft_.setRotation(1);
  // Stabilny kompromis: szybkie odswiezanie bez artefaktow na dluzszych przewodach.
  tft_.setSPISpeed(27000000);
  tft_.setTextWrap(false);
  tft_.fillScreen(kBg);

  encoder_.begin();
  ensureBacklightPwm();
  loadUiSettings();
  // Start od wygaszacza, ale nie "na czarno" - używamy normalnej jasności użytkownika.
  setBacklightPercent(brightnessPercent_);

  selectedZone_ = 0;
  manualZone_ = 0;
  manualDurationMin_ = 15;
  screen_ = Screen::Home;
  uiMode_ = UiMode::CardSelect;
  previousScreen_ = Screen::Home;
  manualField_ = ManualField::Duration;
  statusField_ = StatusField::WiFi;
  settingsField_ = SettingsField::Brightness;
  syncDefaultStatusWifiAction();
  transitionActive_ = false;
  logoLoaded_ = false;
  logoLoadAttempted_ = false;
  focusInitialized_ = false;
  dimmed_ = true;
  bootScreensaverUntilMs_ = millis() + 2200UL;
  lastUserActivityMs_ = millis();
  lastManualRemainingSec_ = -1;
  lastManualZoneActive_ = false;
  lastStatusStateSignature_ = 0;
  lastStatusCheckMs_ = 0;
  statusSnapshotValid_ = false;
  lastStatusWifiText_ = "";
  lastStatusCloudText_ = "";
  lastStatusTempText_ = "";
  lastStatusWateringText_ = "";
  lastStatusWiFiEditMode_ = false;
  lastStatusFocusVisible_ = false;
  lastStatusFocusField_ = StatusField::WiFi;
  lastStatusWifiActionSel_ = statusWifiAction_;
  lastHomeStateSignature_ = 0;
  lastHomeActiveMask_ = 0;
  lastHomeZoneCount_ = 0;
  homeSnapshotValid_ = false;
  lastWeatherStateSignature_ = 0;
  lastWeatherCheckMs_ = 0;
  logsSnapshotValid_ = false;
  lastLogsRevision_ = logs_ ? logs_->revision() : 0;
  lastLogsVisibleCount_ = logs_ ? logs_->visibleCount() : 0;
  lastLogsFocusVisible_ = false;
  lastLogsCheckMs_ = 0;
  lastLogsMarqueeSignature_ = 0;
  logsScrollPx_ = 0;
  logsMaxScrollPx_ = 0;
  logsScrollbarAlpha_ = 0;
  logsLastScrollMs_ = 0;
  logsLastFadeMs_ = 0;
  logsLastInteractionMs_ = millis();
  lastSettingsStateSignature_ = 0;
  lastSettingsCheckMs_ = 0;
  settingsSnapshotValid_ = false;
  lastSettingsBrightnessPct_ = brightnessPercent_;
  lastSettingsDimTimeoutIdx_ = dimTimeoutIndex_;
  lastSettingsFocusVisible_ = false;
  lastSettingsFocusField_ = SettingsField::Brightness;
  weatherSnapshotValid_ = false;
  lastWeatherTempText_ = "";
  lastWeatherHumText_ = "";
  lastWeatherWindText_ = "";
  lastWeatherRainText_ = "";
  lastWeatherIconCode_ = "";

  syncFocusTargets();
  manualFocusY_ = manualFocusTargetY_;
  statusFocusY_ = statusFocusTargetY_;
  settingsFocusY_ = settingsFocusTargetY_;

  dirty_ = true;
  dimmedWateringSnapshotActive_ = false;
  dimmedWateringSnapshotZone_ = -1;
  dimmedWateringSnapshotRemainingSec_ = -1;
  dimmedWateringSnapshotTotalSec_ = 0;
  lastDimmedWateringCheckMs_ = 0;
  draw();
}

void TftPanelUI::loop() {
  encoder_.loop();

  const bool forceAwakeForEmergency = shouldKeepAwakeForEmergency();
  const bool keepBootScreensaver = dimmed_ && (millis() < bootScreensaverUntilMs_);
  if (forceAwakeForEmergency && dimmed_ && !keepBootScreensaver) {
    // Zamiast "gołego" przełączenia flagi wybudzamy pełnym torem,
    // aby zawsze odtworzyć poprawnie top bar i warstwę treści.
    wakeFromDim();
  }

  int turns = encoder_.consumeTurnSteps();
  bool rawShortPress = encoder_.consumeShortPress();
  bool longPress = encoder_.consumeLongPress();
  bool shortPress = false;
  bool doublePress = false;
  const bool hadInput = (turns != 0) || rawShortPress || longPress;

  if (hadInput) {
    if (dimmed_) {
      wakeFromDim();
      // Pierwsze poruszenie wybudza tylko ekran.
      turns = 0;
      rawShortPress = false;
      longPress = false;
      shortPress = false;
      doublePress = false;
      pendingShortPress_ = false;
    } else {
      lastUserActivityMs_ = millis();
    }
  }

  if (!dimmed_ && !forceAwakeForEmergency) {
    const unsigned long idleMs = millis() - lastUserActivityMs_;
    const unsigned long timeoutMs = (unsigned long)selectedDimTimeoutMinutes() * 60000UL;
    if (idleMs >= timeoutMs) enterDim();
  }

  if (!dimmed_) {
    const unsigned long now = millis();
    const unsigned long doubleClickWindowMs =
        (screen_ == Screen::Logs && uiMode_ == UiMode::FieldSelect) ? kLogsDoubleClickWindowMs : kDoubleClickWindowMs;
    // Nie kasuj oczekującego pierwszego kliku przy drobnym ruchu enkodera,
    // bo to często jest szum mechaniczny podczas wciskania (i psuje dwuklik).
    if (longPress) pendingShortPress_ = false;

    if (rawShortPress) {
      if (pendingShortPress_ && (now - pendingShortPressMs_ <= doubleClickWindowMs)) {
        doublePress = true;
        pendingShortPress_ = false;
      } else {
        // Odkładamy pojedynczy klik do końca okna dwukliku.
        pendingShortPress_ = true;
        pendingShortPressMs_ = now;
      }
    } else if (pendingShortPress_ && (now - pendingShortPressMs_ > doubleClickWindowMs)) {
      shortPress = true;
      pendingShortPress_ = false;
    }

    handleInput(turns, shortPress, longPress, doublePress);
    updateAnimations();
    if (settingsDirty_) persistUiSettings(false);

    if (screen_ == Screen::Logs && uiMode_ == UiMode::FieldSelect) {
      if (processPendingLogsScroll(0, false)) return;
    }
  }

  if (!dimmed_ && uiMode_ == UiMode::Modal && modalScrollbarAlpha_ > 0) {
    const unsigned long nowFade = millis();
    if (nowFade - modalLastScrollMs_ > kModalScrollbarHoldMs &&
        nowFade - modalLastFadeMs_ >= kModalScrollbarFadeStepMs) {
      modalLastFadeMs_ = nowFade;
      if (modalScrollbarAlpha_ <= kModalScrollbarFadeStep) modalScrollbarAlpha_ = 0;
      else modalScrollbarAlpha_ = (uint8_t)(modalScrollbarAlpha_ - kModalScrollbarFadeStep);
      drawWateringInfoModalScrollbar(0, true);
      lastFrameMs_ = nowFade;
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Logs && uiMode_ != UiMode::CardSelect && logsScrollbarAlpha_ > 0) {
    const unsigned long nowFade = millis();
    if (nowFade - logsLastScrollMs_ > kModalScrollbarHoldMs &&
        nowFade - logsLastFadeMs_ >= kModalScrollbarFadeStepMs) {
      logsLastFadeMs_ = nowFade;
      if (logsScrollbarAlpha_ <= kModalScrollbarFadeStep) logsScrollbarAlpha_ = 0;
      else logsScrollbarAlpha_ = (uint8_t)(logsScrollbarAlpha_ - kModalScrollbarFadeStep);
      drawLogsScrollbar(0, true);
      lastFrameMs_ = nowFade;
    }
  }

  const unsigned long now = millis();
  if (!dimmed_ && !topBarDirty_) {
    if (now - lastTopBarClockCheckMs_ >= kTopBarClockCheckIntervalMs) {
      lastTopBarClockCheckMs_ = now;
      if (drawTopBarClock(false)) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  const bool periodicDue = false;
  if (!dirty_ && dimmed_) {
    // W wygaszaczu odświeżamy wyłącznie element koła (bez pełnego redraw ekranu).
    constexpr unsigned long kDimmedGaugeCheckIntervalMs = 220;
    if (now - lastDimmedWateringCheckMs_ >= kDimmedGaugeCheckIntervalMs) {
      lastDimmedWateringCheckMs_ = now;
      if (refreshDimmedWateringLive()) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Manual && zones_) {
    const bool active = zones_->getZoneState(manualZone_);
    const int rem = active ? zones_->getRemainingSeconds(manualZone_) : -1;
    if (active != lastManualZoneActive_) {
      drawManualLiveSection(0);
      lastManualZoneActive_ = active;
      lastManualRemainingSec_ = rem;
      lastFrameMs_ = now;
      return;
    }
    if (active && rem != lastManualRemainingSec_) {
      drawManualRemainingValue(0, rem);
      lastManualRemainingSec_ = rem;
      lastFrameMs_ = now;
      return;
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Home && zones_) {
    const uint32_t sig = buildHomeStateSignature();
    if (sig != lastHomeStateSignature_) {
      if (refreshHomeLive(0)) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Status) {
    // Status odświeżamy punktowo: tylko gdy realnie zmieniła się treść pól.
    constexpr unsigned long kStatusCheckIntervalMs = 500;
    if (now - lastStatusCheckMs_ >= kStatusCheckIntervalMs) {
      lastStatusCheckMs_ = now;
      if (refreshStatusLive(0)) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Weather) {
    // Pogoda ma być stabilna: odświeżamy tylko zmienione elementy.
    constexpr unsigned long kWeatherCheckIntervalMs = 1000;
    if (now - lastWeatherCheckMs_ >= kWeatherCheckIntervalMs) {
      lastWeatherCheckMs_ = now;
      if (refreshWeatherLive(0)) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Logs) {
    if (uiMode_ == UiMode::CardSelect) {
      // W trybie wyboru kart nie animujemy logów ani marquee.
      // Dzięki temu pętla zostaje lekka i enkoder nie gubi ząbków przy przejściu do kolejnej karty.
      constexpr unsigned long kLogsIdleCheckIntervalMs = 600;
      if (now - lastLogsCheckMs_ >= kLogsIdleCheckIntervalMs) {
        lastLogsCheckMs_ = now;
        const uint32_t revision = logs_ ? logs_->revision() : 0;
        const int visibleCount = logs_ ? logs_->visibleCount() : 0;
        if (!logsSnapshotValid_ ||
            revision != lastLogsRevision_ ||
            visibleCount != lastLogsVisibleCount_ ||
            lastLogsFocusVisible_) {
          drawLogs(0);
          lastFrameMs_ = now;
          return;
        }
      }
    } else {
      constexpr unsigned long kLogsCheckIntervalMs = 80;
      if (now - lastLogsCheckMs_ >= kLogsCheckIntervalMs) {
        lastLogsCheckMs_ = now;
        if (refreshLogsLive(0)) {
          lastFrameMs_ = now;
          return;
        }
      }
    }
  }

  if (!dirty_ && !dimmed_ && screen_ == Screen::Settings) {
    // Ustawienia odświeżamy punktowo tylko przy realnej zmianie danych.
    constexpr unsigned long kSettingsCheckIntervalMs = 700;
    if (now - lastSettingsCheckMs_ >= kSettingsCheckIntervalMs) {
      lastSettingsCheckMs_ = now;
      if (refreshSettingsLive(0)) {
        lastFrameMs_ = now;
        return;
      }
    }
  }

  if (dirty_ || periodicDue) draw();
}

void TftPanelUI::requestScreen(Screen next) {
  if (next == screen_) return;
  previousScreen_ = screen_;
  screen_ = next;
  transitionActive_ = false;
  lastManualRemainingSec_ = -1;
  lastManualZoneActive_ = false;
  lastStatusCheckMs_ = 0;
  lastWeatherCheckMs_ = 0;
  lastLogsCheckMs_ = 0;
  lastSettingsCheckMs_ = 0;
  homeSnapshotValid_ = false;
  weatherSnapshotValid_ = false;
  statusSnapshotValid_ = false;
  logsSnapshotValid_ = false;
  logsQueuedTurns_ = 0;
  settingsSnapshotValid_ = false;
  dirty_ = true;
}

int TftPanelUI::screenIndex(Screen s) const {
  switch (s) {
    case Screen::Home: return 0;
    // "Manual" nie jest kartą główną - dostęp tylko z Panelu Głównego.
    case Screen::Manual: return 0;
    case Screen::Weather: return 1;
    case Screen::Status: return 2;
    case Screen::Logs: return 3;
    case Screen::Settings: return 4;
  }
  return 0;
}

TftPanelUI::Screen TftPanelUI::screenFromIndex(int idx) const {
  // Karty główne: Home -> Weather -> Status -> Logi -> Settings.
  idx %= 5;
  if (idx < 0) idx += 5;
  switch (idx) {
    case 0: return Screen::Home;
    case 1: return Screen::Weather;
    case 2: return Screen::Status;
    case 3: return Screen::Logs;
    case 4: return Screen::Settings;
  }
  return Screen::Home;
}

int TftPanelUI::settingsFieldTopY(SettingsField field) const {
  switch (field) {
    case SettingsField::Brightness: return 74;
    case SettingsField::WakeTimeout: return 134;
  }
  return 74;
}

int TftPanelUI::settingsFieldHeight(SettingsField field) const {
  switch (field) {
    case SettingsField::Brightness: return 54;
    case SettingsField::WakeTimeout: return 56;
  }
  return 54;
}

int TftPanelUI::statusFieldTopY(StatusField field) const {
  switch (field) {
    case StatusField::WiFi: return 72;
    case StatusField::Cloud: return 116;
    case StatusField::Temp: return 160;
    case StatusField::Watering: return 204;
  }
  return 72;
}

int TftPanelUI::statusFieldHeight(StatusField field) const {
  (void)field;
  return 36;
}

void TftPanelUI::updateAnimations() {
  syncFocusTargets();
  bool visibleChanged = false;

  if (screen_ == Screen::Home) {
    if (focusX_ != focusTargetX_ || focusY_ != focusTargetY_) visibleChanged = true;
  } else {
    focusX_ = focusTargetX_;
    focusY_ = focusTargetY_;
  }

  if (screen_ == Screen::Manual) {
    if (manualFocusY_ != manualFocusTargetY_) visibleChanged = true;
  } else {
    manualFocusY_ = manualFocusTargetY_;
  }

  if (screen_ == Screen::Status) {
    if (statusFocusY_ != statusFocusTargetY_) visibleChanged = true;
  } else {
    statusFocusY_ = statusFocusTargetY_;
  }

  if (screen_ == Screen::Settings) {
    if (settingsFocusY_ != settingsFocusTargetY_) visibleChanged = true;
  } else {
    settingsFocusY_ = settingsFocusTargetY_;
  }

  if (visibleChanged) {
    focusX_ = focusTargetX_;
    focusY_ = focusTargetY_;
    manualFocusY_ = manualFocusTargetY_;
    statusFocusY_ = statusFocusTargetY_;
    settingsFocusY_ = settingsFocusTargetY_;
    dirty_ = true;
  }
}

void TftPanelUI::syncFocusTargets() {
  const int n = zoneCount();
  if (selectedZone_ >= n) selectedZone_ = n - 1;
  if (selectedZone_ < 0) selectedZone_ = 0;
  if (manualZone_ >= n) manualZone_ = n - 1;
  if (manualZone_ < 0) manualZone_ = 0;

  const int col = selectedZone_ % 4;
  const int row = selectedZone_ / 4;
  focusTargetX_ = kTileStartX + col * (kTileW + kTileGapX);
  focusTargetY_ = kTileStartY + row * (kTileH + kTileGapY);
  if (!focusInitialized_) {
    focusX_ = focusTargetX_;
    focusY_ = focusTargetY_;
    focusInitialized_ = true;
  }

  manualFocusTargetY_ = (manualField_ == ManualField::Duration) ? 120 : 172;

  statusFocusTargetY_ = statusFieldTopY(statusField_);
  settingsFocusTargetY_ = settingsFieldTopY(settingsField_);
}

void TftPanelUI::handleInput(int turns, bool shortPress, bool longPress, bool doublePress) {
  auto resetLogsCardState = [&]() {
    logsScrollbarAlpha_ = 0;
    logsLastScrollMs_ = 0;
    logsLastFadeMs_ = 0;
    logsLastInteractionMs_ = millis();
    lastLogsMarqueeSignature_ = 0;
    logsSnapshotValid_ = false;
    for (int i = 0; i < kLogsSlotCacheSize; ++i) {
      lastLogsSlotIndices_[i] = -1;
      lastLogsSlotShiftPx_[i] = -1;
      lastLogsSlotTimeSig_[i] = 0;
    }
  };

  if (uiMode_ == UiMode::Modal) {
    if (doublePress || longPress) {
      closeModal();
      dirty_ = true;
      return;
    }
    if (turns != 0) {
      constexpr int kLineStepPx = 16;
      const int prevScrollPx = modalScrollPx_;
      modalScrollPx_ += turns * kLineStepPx;
      if (modalScrollPx_ < 0) modalScrollPx_ = 0;
      if (modalScrollPx_ > modalMaxScrollPx_) modalScrollPx_ = modalMaxScrollPx_;
      modalScrollbarAlpha_ = 255;
      modalLastScrollMs_ = millis();
      modalLastFadeMs_ = modalLastScrollMs_;
      if (modalScrollPx_ != prevScrollPx) drawWateringInfoModalText(0);
      drawWateringInfoModalScrollbar(0, true);
      lastFrameMs_ = millis();
      dirty_ = false;
    }
    return;
  }

  if (longPress) {
    bool switchedScreen = false;
    if (screen_ == Screen::Manual) {
      requestScreen(Screen::Home);
      switchedScreen = true;
    }
    uiMode_ = UiMode::CardSelect;
    if (switchedScreen) {
      dirty_ = true;
      return;
    }
    if (screen_ == Screen::Home) {
      drawHomeTileWithFocusMargin(0, selectedZone_, false);
      drawHomeStatusBar(0);
      lastFrameMs_ = millis();
      dirty_ = false;
    } else if (screen_ == Screen::Status) {
      drawStatusFieldRow(0, statusField_);
      lastStatusStateSignature_ = buildStatusStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    } else if (screen_ == Screen::Logs) {
      resetLogsCardState();
      dirty_ = true;
    } else if (screen_ == Screen::Settings) {
      if (settingsField_ == SettingsField::Brightness) drawSettingsBrightnessRow(0);
      else drawSettingsWakeRow(0);
      lastSettingsStateSignature_ = buildSettingsStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    } else {
      dirty_ = true;
    }
    return;
  }

  if (doublePress) {
    // Dwuklik zawsze wraca do globalnego wyboru kart.
    // Jeżeli jesteśmy w ekranie manualnym (pod-ekran), wracamy na kartę Home.
    bool switchedScreen = false;
    if (screen_ == Screen::Manual) {
      requestScreen(Screen::Home);
      manualField_ = ManualField::Duration;
      switchedScreen = true;
    }
    uiMode_ = UiMode::CardSelect;
    if (switchedScreen) {
      dirty_ = true;
      return;
    }
    if (screen_ == Screen::Home) {
      drawHomeTileWithFocusMargin(0, selectedZone_, false);
      drawHomeStatusBar(0);
      lastFrameMs_ = millis();
      dirty_ = false;
    } else if (screen_ == Screen::Status) {
      drawStatusFieldRow(0, statusField_);
      lastStatusStateSignature_ = buildStatusStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    } else if (screen_ == Screen::Logs) {
      resetLogsCardState();
      dirty_ = true;
    } else if (screen_ == Screen::Settings) {
      if (settingsField_ == SettingsField::Brightness) drawSettingsBrightnessRow(0);
      else drawSettingsWakeRow(0);
      lastSettingsStateSignature_ = buildSettingsStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    } else {
      dirty_ = true;
    }
    return;
  }

  // Tryb globalny: obrót zmienia kartę, klik wchodzi do karty.
  if (uiMode_ == UiMode::CardSelect) {
    if (turns != 0) {
      const int idx = screenIndex(screen_) + turns;
      requestScreen(screenFromIndex(idx));
    }
    if (shortPress) {
      if (screen_ == Screen::Manual) {
        // "Manual" nie powinien być osiągalny jako karta, ale zostawiamy bezpiecznik.
        requestScreen(Screen::Home);
      }
      if (screen_ == Screen::Weather) {
        // Karta pogody nie ma pól do edycji z enkodera.
        return;
      }
      uiMode_ = UiMode::FieldSelect;
      if (screen_ == Screen::Settings) {
        settingsField_ = SettingsField::Brightness;
        syncFocusTargets();
        settingsFocusY_ = settingsFocusTargetY_;
        drawSettingsFocusBox(0, settingsField_);
        lastSettingsStateSignature_ = buildSettingsStateSignature();
        lastFrameMs_ = millis();
        dirty_ = false;
      }
      if (screen_ == Screen::Status) {
        statusField_ = StatusField::WiFi;
        syncDefaultStatusWifiAction();
        syncFocusTargets();
        statusFocusY_ = statusFocusTargetY_;
        drawStatusFocusBox(0, statusField_);
        lastStatusStateSignature_ = buildStatusStateSignature();
        lastFrameMs_ = millis();
        dirty_ = false;
      }
      if (screen_ == Screen::Logs) {
        logsScrollPx_ = 0;
        logsLastInteractionMs_ = millis();
        normalizeLogsScroll(false);
        drawLogs(0);
        lastFrameMs_ = millis();
        dirty_ = false;
      }
      if (screen_ == Screen::Home) {
        syncFocusTargets();
        focusX_ = focusTargetX_;
        focusY_ = focusTargetY_;
        drawHomeTileWithFocusMargin(0, selectedZone_, true);
        drawHomeStatusBar(0);
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    }
    return;
  }

  if (screen_ == Screen::Home) {
    if (uiMode_ != UiMode::FieldSelect) {
      uiMode_ = UiMode::FieldSelect;
      syncFocusTargets();
      focusX_ = focusTargetX_;
      focusY_ = focusTargetY_;
      drawHomeTileWithFocusMargin(0, selectedZone_, true);
      drawHomeStatusBar(0);
      lastFrameMs_ = millis();
      dirty_ = false;
    }
    if (turns != 0) {
      const int prev = selectedZone_;
      selectedZone_ = wrapZone(selectedZone_ + turns);
      manualZone_ = selectedZone_;
      if (selectedZone_ != prev) {
        syncFocusTargets();
        focusX_ = focusTargetX_;
        focusY_ = focusTargetY_;
        drawHomeTileWithFocusMargin(0, prev, false);
        drawHomeTileWithFocusMargin(0, selectedZone_, true);
        drawHomeStatusBar(0);
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    }
    if (shortPress) {
      // Wejście z karty wyboru sekcji do karty ręcznego sterowania wybraną sekcją.
      manualZone_ = selectedZone_;
      manualField_ = ManualField::Duration;
      requestScreen(Screen::Manual);
      uiMode_ = UiMode::FieldSelect;
      dirty_ = true;
    }
    return;
  }

  if (screen_ == Screen::Weather) return;

  if (screen_ == Screen::Logs) {
    if (uiMode_ != UiMode::FieldSelect) {
      uiMode_ = UiMode::FieldSelect;
      logsLastInteractionMs_ = millis();
      logsQueuedTurns_ = 0;
      normalizeLogsScroll(false);
      drawLogs(0);
      lastFrameMs_ = millis();
      dirty_ = false;
      return;
    }

    if (turns != 0) {
      logsQueuedTurns_ += turns;
      if (logsQueuedTurns_ > 64) logsQueuedTurns_ = 64;
      if (logsQueuedTurns_ < -64) logsQueuedTurns_ = -64;
      processPendingLogsScroll(0, true);
    }
    return;
  }

  if (screen_ == Screen::Status) {
    if (uiMode_ == UiMode::FieldSelect) {
      if (turns != 0) {
        const StatusField prev = statusField_;
        int idx = (int)statusField_ + turns;
        idx %= 4;
        if (idx < 0) idx += 4;
        statusField_ = (StatusField)idx;
        if (prev != statusField_) {
          drawStatusFieldRow(0, prev);
          drawStatusFieldRow(0, statusField_);
          drawStatusFocusBox(0, statusField_);
          lastFrameMs_ = millis();
          dirty_ = false;
        }
      }
      if (shortPress) {
        if (statusField_ == StatusField::WiFi) {
          uiMode_ = UiMode::ValueEdit;
          syncDefaultStatusWifiAction();
          drawStatusFieldRow(0, StatusField::WiFi);
          drawStatusFocusBox(0, StatusField::WiFi);
          lastFrameMs_ = millis();
          dirty_ = false;
        } else if (statusField_ == StatusField::Watering) {
          openWateringInfoModal();
          uiMode_ = UiMode::Modal;
          dirty_ = true;
        }
      }
      return;
    }

    if (statusField_ == StatusField::WiFi && turns != 0) {
      const int prevAction = statusWifiAction_;
      statusWifiAction_ += turns;
      statusWifiAction_ %= 2;
      if (statusWifiAction_ < 0) statusWifiAction_ += 2;
      if (prevAction != statusWifiAction_) {
        drawStatusFieldRow(0, StatusField::WiFi);
        drawStatusFocusBox(0, StatusField::WiFi);
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    }

    if (shortPress) {
      if (statusField_ == StatusField::WiFi) {
        executeStatusWiFiAction();
      }
      uiMode_ = UiMode::FieldSelect;
      drawStatusFieldRow(0, statusField_);
      drawStatusFocusBox(0, statusField_);
      lastStatusStateSignature_ = buildStatusStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    }
    return;
  }

  if (screen_ == Screen::Manual) {
    if (uiMode_ == UiMode::FieldSelect) {
      if (turns != 0) {
        const ManualField prevField = manualField_;
        int idx = (int)manualField_ + turns;
        idx %= 2;
        if (idx < 0) idx += 2;
        idx %= 2;
        manualField_ = (ManualField)idx;
        if (prevField != manualField_) {
          syncFocusTargets();
          manualFocusY_ = manualFocusTargetY_;
          drawManualLiveSection(0);
          lastFrameMs_ = millis();
          dirty_ = false;
        }
      }
      if (shortPress) {
        // START/STOP ma działać "na jeden klik" bez dodatkowego wejścia w edycję.
        if (manualField_ == ManualField::Action) {
          executeManualAction();
          uiMode_ = UiMode::FieldSelect;
          drawManualLiveSection(0);
          lastFrameMs_ = millis();
          dirty_ = false;
        } else {
          uiMode_ = UiMode::ValueEdit;
          lastFrameMs_ = millis();
          dirty_ = false;
        }
      }
      return;
    }

    if (manualField_ == ManualField::Duration && turns != 0) {
      const int prev = manualDurationMin_;
      manualDurationMin_ += turns;
      if (manualDurationMin_ < 1) manualDurationMin_ = 1;
      if (manualDurationMin_ > 180) manualDurationMin_ = 180;
      if (manualDurationMin_ != prev) {
        drawManualLiveSection(0);
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    }

    if (shortPress) {
      if (manualField_ == ManualField::Action) executeManualAction();
      uiMode_ = UiMode::FieldSelect;
      lastFrameMs_ = millis();
      dirty_ = false;
    }
    return;
  }

  if (screen_ == Screen::Settings) {
    if (uiMode_ == UiMode::FieldSelect) {
      if (turns != 0) {
        const SettingsField prevField = settingsField_;
        int idx = (int)settingsField_ + turns;
        idx %= 2;
        if (idx < 0) idx += 2;
        settingsField_ = (SettingsField)idx;
        if (prevField != settingsField_) {
          syncFocusTargets();
          settingsFocusY_ = settingsFocusTargetY_;
          if (prevField == SettingsField::Brightness) drawSettingsBrightnessRow(0);
          else drawSettingsWakeRow(0);
          if (settingsField_ == SettingsField::Brightness) drawSettingsBrightnessRow(0);
          else drawSettingsWakeRow(0);
          drawSettingsFocusBox(0, settingsField_);
          lastSettingsStateSignature_ = buildSettingsStateSignature();
          lastFrameMs_ = millis();
          dirty_ = false;
        }
      }
      if (shortPress) {
        uiMode_ = UiMode::ValueEdit;
        lastSettingsStateSignature_ = buildSettingsStateSignature();
        lastFrameMs_ = millis();
        dirty_ = false;
      }
      return;
    }

    if (settingsField_ == SettingsField::Brightness && turns != 0) {
      const int prev = brightnessPercent_;
      brightnessPercent_ += turns * 5;
      if (brightnessPercent_ < kMinUserBrightnessPercent) brightnessPercent_ = kMinUserBrightnessPercent;
      if (brightnessPercent_ > 100) brightnessPercent_ = 100;
      if (brightnessPercent_ != prev) {
        setBacklightPercent(brightnessPercent_);
        settingsDirty_ = true;
        settingsChangedMs_ = millis();
        drawSettingsBrightnessDynamic(0);
        lastSettingsStateSignature_ = buildSettingsStateSignature();
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    } else if (settingsField_ == SettingsField::WakeTimeout && turns != 0) {
      const int prev = dimTimeoutIndex_;
      dimTimeoutIndex_ += turns;
      if (dimTimeoutIndex_ < 0) dimTimeoutIndex_ = 0;
      if (dimTimeoutIndex_ > 2) dimTimeoutIndex_ = 2;
      if (dimTimeoutIndex_ != prev) {
        settingsDirty_ = true;
        settingsChangedMs_ = millis();
        drawSettingsWakeRow(0);
        drawSettingsFocusBox(0, settingsField_);
        lastSettingsStateSignature_ = buildSettingsStateSignature();
        lastFrameMs_ = millis();
        dirty_ = false;
      }
    }

    if (shortPress) {
      uiMode_ = UiMode::FieldSelect;
      lastSettingsStateSignature_ = buildSettingsStateSignature();
      lastFrameMs_ = millis();
      dirty_ = false;
    }
  }
}

void TftPanelUI::ensureBacklightPwm() {
  if (backlightReady_) return;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttachChannel(kPinTftBl, kBacklightPwmFreqHz, kBacklightPwmBits, kBacklightPwmCh);
#else
  ledcSetup(kBacklightPwmCh, kBacklightPwmFreqHz, kBacklightPwmBits);
  ledcAttachPin(kPinTftBl, kBacklightPwmCh);
#endif
  backlightReady_ = true;
}

void TftPanelUI::setBacklightPercent(int percent) {
  ensureBacklightPwm();
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  backlightCurrentPercent_ = percent;

  const int maxDuty = (1 << kBacklightPwmBits) - 1;
  const int duty = (maxDuty * percent) / 100;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(kPinTftBl, duty);
#else
  ledcWrite(kBacklightPwmCh, duty);
#endif
}

int TftPanelUI::selectedDimTimeoutMinutes() const {
  if (dimTimeoutIndex_ < 0) return kDimTimeoutOptionsMin[0];
  if (dimTimeoutIndex_ > 2) return kDimTimeoutOptionsMin[2];
  return kDimTimeoutOptionsMin[dimTimeoutIndex_];
}

bool TftPanelUI::shouldKeepAwakeForEmergency() const {
  const bool wifiOk = (WiFi.status() == WL_CONNECTED);
  // Brak WiFi nie jest już stanem awaryjnym samym w sobie - wygaszacz ma działać normalnie.
  // Wyjątek: tryb AP (konfiguracja), gdzie utrzymujemy ekran aktywny.
  if (!wifiOk) return (config_ && config_->isInAPMode());

  // Gdy cloud jest skonfigurowany, ale aktualnie offline, nie przyciemniamy ekranu.
  if (mqtt_ && mqtt_->isConfigured() && !mqtt_->isConnected()) return true;
  return false;
}

void TftPanelUI::loadUiSettings() {
  Preferences pref;
  if (!pref.begin("tft_ui", true)) return;
  brightnessPercent_ = (int)pref.getUChar("bl_pct", (uint8_t)brightnessPercent_);
  dimTimeoutIndex_ = (int)pref.getUChar("dim_idx", (uint8_t)dimTimeoutIndex_);
  pref.end();

  if (brightnessPercent_ < kMinUserBrightnessPercent) brightnessPercent_ = kMinUserBrightnessPercent;
  if (brightnessPercent_ > 100) brightnessPercent_ = 100;
  if (dimTimeoutIndex_ < 0) dimTimeoutIndex_ = 0;
  if (dimTimeoutIndex_ > 2) dimTimeoutIndex_ = 2;
}

void TftPanelUI::persistUiSettings(bool force) {
  if (!settingsDirty_) return;
  if (!force && (millis() - settingsChangedMs_ < kSettingsPersistDelayMs)) return;

  Preferences pref;
  if (!pref.begin("tft_ui", false)) return;
  pref.putUChar("bl_pct", (uint8_t)brightnessPercent_);
  pref.putUChar("dim_idx", (uint8_t)dimTimeoutIndex_);
  pref.end();
  settingsDirty_ = false;
}

void TftPanelUI::enterDim() {
  persistUiSettings(true);
  dimmed_ = true;
  uiMode_ = UiMode::CardSelect;
  transitionActive_ = false;
  dimmedWateringSnapshotActive_ = false;
  dimmedWateringSnapshotZone_ = -1;
  dimmedWateringSnapshotRemainingSec_ = -1;
  dimmedWateringSnapshotTotalSec_ = 0;
  lastDimmedWateringCheckMs_ = 0;
  setBacklightPercent(kDimmedBacklightPercent);
  dirty_ = true;
}

void TftPanelUI::wakeFromDim() {
  dimmed_ = false;
  screen_ = Screen::Home;
  // Po wybudzeniu zawsze globalny wybór kart.
  uiMode_ = UiMode::CardSelect;
  manualField_ = ManualField::Duration;
  statusField_ = StatusField::WiFi;
  settingsField_ = SettingsField::Brightness;
  syncDefaultStatusWifiAction();
  modalLineCount_ = 0;
  modalScrollPx_ = 0;
  modalMaxScrollPx_ = 0;
  modalScrollbarAlpha_ = 0;
  modalLastScrollMs_ = 0;
  modalLastFadeMs_ = 0;
  pendingShortPress_ = false;
  lastManualRemainingSec_ = -1;
  lastManualZoneActive_ = false;
  weatherSnapshotValid_ = false;
  homeSnapshotValid_ = false;
  statusSnapshotValid_ = false;
  logsSnapshotValid_ = false;
  settingsSnapshotValid_ = false;
  topBarDirty_ = true;
  logsScrollPx_ = 0;
  logsMaxScrollPx_ = 0;
  logsScrollbarAlpha_ = 0;
  logsLastScrollMs_ = 0;
  logsLastFadeMs_ = 0;
  lastLogsMarqueeSignature_ = 0;
  logsLastInteractionMs_ = millis();
  lastTopBarClockText_ = "";
  lastTopBarClockCheckMs_ = 0;
  dimmedWateringSnapshotActive_ = false;
  dimmedWateringSnapshotZone_ = -1;
  dimmedWateringSnapshotRemainingSec_ = -1;
  dimmedWateringSnapshotTotalSec_ = 0;
  lastDimmedWateringCheckMs_ = 0;
  setBacklightPercent(brightnessPercent_);
  lastUserActivityMs_ = millis();
  dirty_ = true;
}

void TftPanelUI::executeManualAction() {
  if (!zones_) return;
  const bool active = zones_->getZoneState(manualZone_);
  if (active) zones_->stopZone(manualZone_);
  else zones_->startZone(manualZone_, manualDurationMin_ * 60);
  manualField_ = ManualField::Duration;
}

void TftPanelUI::syncDefaultStatusWifiAction() {
  const bool apMode = config_ && config_->isInAPMode();
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED) && !apMode;
  statusWifiAction_ = wifiConnected ? 1 : 0;  // 0=Połącz, 1=Rozłącz
}

void TftPanelUI::executeStatusWiFiAction() {
  if (!config_) return;

  if (statusWifiAction_ == 0) {
    // Połącz
    config_->uiConnectWiFi();
    // Po uruchomieniu łączenia podpowiadamy kolejną akcję "Rozłącz".
    statusWifiAction_ = 1;
  } else {
    // Rozłącz
    config_->uiDisconnectWiFi();
    // Po rozłączeniu domyślnie proponuj "Połącz".
    statusWifiAction_ = 0;
  }

  lastSettingsCheckMs_ = 0;
  lastStatusCheckMs_ = 0;
  dirty_ = true;
}

void TftPanelUI::appendModalWrapped(const String& text, int maxChars) {
  if (modalLineCount_ >= kModalMaxLines) return;

  String rest = text;
  rest.trim();
  if (rest.length() == 0) {
    modalLines_[modalLineCount_++] = "";
    return;
  }

  if (maxChars < 10) maxChars = 10;
  constexpr int kModalTextMaxWidthPx = 232;

  while (rest.length() > 0 && modalLineCount_ < kModalMaxLines) {
    if (measureTextWidth(rest, &SFNS9pt7b) <= kModalTextMaxWidthPx) {
      modalLines_[modalLineCount_++] = rest;
      return;
    }

    int cut = min((int)rest.length(), maxChars);
    while (cut > 1 && measureTextWidth(rest.substring(0, cut), &SFNS9pt7b) > kModalTextMaxWidthPx) --cut;

    int bestSpace = -1;
    for (int i = cut; i > 0; --i) {
      if (rest[i - 1] == ' ') {
        const String candidate = rest.substring(0, i - 1);
        if (candidate.length() > 0 && measureTextWidth(candidate, &SFNS9pt7b) <= kModalTextMaxWidthPx) {
          bestSpace = i - 1;
          break;
        }
      }
    }
    if (bestSpace > 0) cut = bestSpace;

    String line = rest.substring(0, cut);
    line.trim();
    if (line.length() == 0) {
      // Brak spacji / bardzo długi token: tnij twardo po szerokości pikseli.
      cut = 1;
      while (cut < (int)rest.length() &&
             measureTextWidth(rest.substring(0, cut + 1), &SFNS9pt7b) <= kModalTextMaxWidthPx) {
        ++cut;
      }
      line = rest.substring(0, cut);
      line.trim();
      if (line.length() == 0) break;
    }
    modalLines_[modalLineCount_++] = line;

    rest = rest.substring(cut);
    rest.trim();
  }
}

void TftPanelUI::openWateringInfoModal() {
  modalLineCount_ = 0;
  modalScrollPx_ = 0;
  modalMaxScrollPx_ = 0;
  modalScrollbarAlpha_ = 0;
  modalLastScrollMs_ = 0;
  modalLastFadeMs_ = 0;

  const int pct = weather_ ? weather_->getWateringPercent() : 100;
  int exampleMin = (10 * pct + 50) / 100;
  if (exampleMin < 0) exampleMin = 0;

  appendModalWrapped("Wskaźnik Nawadnianie to mnożnik czasu programu.");
  appendModalWrapped("100% oznacza dokładnie czasy z harmonogramu.");
  appendModalWrapped("Wartość > 100% wydłuża czas podlewania.");
  appendModalWrapped("Wartość < 100% skraca czas podlewania.");
  appendModalWrapped("Przy " + String(pct) + "% program 10 min trwa około " + String(exampleMin) + " min.");

  if (pct >= 130) {
    appendModalWrapped("System mocno podnosi dawkę, bo warunki są suche i zapotrzebowanie rośnie.");
  } else if (pct >= 110) {
    appendModalWrapped("System lekko podnosi dawkę, bo jest cieplej lub bardziej sucho.");
  } else if (pct >= 90) {
    appendModalWrapped("Wynik blisko 100% oznacza warunki zbliżone do neutralnych.");
  } else if (pct >= 70) {
    appendModalWrapped("System ogranicza podlewanie, bo jest chłodniej lub wilgotniej.");
  } else {
    appendModalWrapped("System mocno ogranicza podlewanie, bo warunki sprzyjają zatrzymaniu wilgoci.");
  }

  String temp, hum, wind, rain24, iconCode;
  buildWeatherDisplayValues(temp, hum, wind, rain24, iconCode);
  appendModalWrapped("Dane wejściowe: temp " + temp + ", wilgotność " + hum + ", wiatr " + wind + ", deszcz 24h " + rain24 + ".");
  appendModalWrapped("Cel: oszczędność wody i stabilna wilgotność gleby bez przelania.");

  const int contentH = modalLineCount_ * kModalLinePx;
  modalMaxScrollPx_ = contentH - kModalVisibleTextPx;
  if (modalMaxScrollPx_ < 0) modalMaxScrollPx_ = 0;
}

void TftPanelUI::closeModal() {
  modalScrollPx_ = 0;
  modalMaxScrollPx_ = 0;
  modalLineCount_ = 0;
  modalScrollbarAlpha_ = 0;
  modalLastScrollMs_ = 0;
  modalLastFadeMs_ = 0;
  hasRenderedScreen_ = false;
  uiMode_ = UiMode::FieldSelect;
}

String TftPanelUI::toFontSafeText(const String& text, AccentMark* accents, int maxAccents, int& outCount) const {
  outCount = 0;
  String safe;
  safe.reserve(text.length());

  auto pushChar = [&](char base, AccentMark accent) {
    safe += base;
    if (accents && outCount < maxAccents) accents[outCount] = accent;
    ++outCount;
  };

  const uint8_t* data = (const uint8_t*)text.c_str();
  const int len = text.length();
  int i = 0;
  while (i < len) {
    uint32_t cp = '?';
    const uint8_t b0 = data[i];
    if ((b0 & 0x80) == 0) {
      cp = b0;
      ++i;
    } else if ((b0 & 0xE0) == 0xC0 && i + 1 < len) {
      cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(data[i + 1] & 0x3F);
      i += 2;
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < len) {
      cp = ((uint32_t)(b0 & 0x0F) << 12) |
           ((uint32_t)(data[i + 1] & 0x3F) << 6) |
           (uint32_t)(data[i + 2] & 0x3F);
      i += 3;
    } else {
      ++i;
    }

    switch (cp) {
      case 0x0105: pushChar('a', AccentMark::Ogonek); break;  // ą
      case 0x0107: pushChar('c', AccentMark::Acute); break;   // ć
      case 0x0119: pushChar('e', AccentMark::Ogonek); break;  // ę
      case 0x0142: pushChar('l', AccentMark::Stroke); break;  // ł
      case 0x0144: pushChar('n', AccentMark::Acute); break;   // ń
      case 0x00F3: pushChar('o', AccentMark::Acute); break;   // ó
      case 0x015B: pushChar('s', AccentMark::Acute); break;   // ś
      case 0x017A: pushChar('z', AccentMark::Acute); break;   // ź
      case 0x017C: pushChar('z', AccentMark::Dot); break;     // ż
      case 0x0104: pushChar('A', AccentMark::Ogonek); break;  // Ą
      case 0x0106: pushChar('C', AccentMark::Acute); break;   // Ć
      case 0x0118: pushChar('E', AccentMark::Ogonek); break;  // Ę
      case 0x0141: pushChar('L', AccentMark::Stroke); break;  // Ł
      case 0x0143: pushChar('N', AccentMark::Acute); break;   // Ń
      case 0x00D3: pushChar('O', AccentMark::Acute); break;   // Ó
      case 0x015A: pushChar('S', AccentMark::Acute); break;   // Ś
      case 0x0179: pushChar('Z', AccentMark::Acute); break;   // Ź
      case 0x017B: pushChar('Z', AccentMark::Dot); break;     // Ż
      default:
        if (cp >= 32 && cp <= 126) pushChar((char)cp, AccentMark::None);
        else pushChar('?', AccentMark::None);
        break;
    }
  }
  return safe;
}

int TftPanelUI::glyphAdvance(char c, const GFXfont* font) const {
  if (!font) return 6;
  const uint8_t uc = (uint8_t)c;
  if (uc < font->first || uc > font->last) return 6;
  const GFXglyph* glyph = &font->glyph[uc - font->first];
  return glyph->xAdvance > 0 ? glyph->xAdvance : 6;
}

int TftPanelUI::textWidthFontSafe(const String& text, const GFXfont* font) {
  AccentMark accents[256];
  int count = 0;
  const String safe = toFontSafeText(text, accents, 256, count);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  tft_.setFont(font);
  tft_.getTextBounds(safe, 0, 0, &x1, &y1, &w, &h);
  tft_.setFont(nullptr);
  return (int)w;
}

void TftPanelUI::drawAccents(const String& safeText, const AccentMark* accents, int count, int x, int baseline, const GFXfont* font, uint16_t color) {
  int penX = x;
  const int n = min((int)safeText.length(), count);
  for (int i = 0; i < n; ++i) {
    const char c = safeText[i];
    const AccentMark accent = accents ? accents[i] : AccentMark::None;
    const int adv = glyphAdvance(c, font);
    if (accent == AccentMark::None) {
      penX += adv;
      continue;
    }

    int gx = penX;
    int gy = baseline - 8;
    int gw = 6;
    int gh = 8;
    if (font) {
      const uint8_t uc = (uint8_t)c;
      if (uc >= font->first && uc <= font->last) {
        const GFXglyph* glyph = &font->glyph[uc - font->first];
        gx = penX + glyph->xOffset;
        gy = baseline + glyph->yOffset;
        gw = max(1, (int)glyph->width);
        gh = max(1, (int)glyph->height);
      }
    }

    if (accent == AccentMark::Acute) {
      const int ax = gx + gw - 2;
      const int ay = gy - 2;
      tft_.drawLine(ax, ay, ax + 3, ay - 3, color);
    } else if (accent == AccentMark::Dot) {
      tft_.fillCircle(gx + gw / 2, gy - 3, 1, color);
    } else if (accent == AccentMark::Ogonek) {
      const int ox = gx + gw - 1;
      const int oy = baseline + 1;
      tft_.drawLine(ox, oy, ox - 2, oy + 3, color);
      tft_.drawPixel(ox - 3, oy + 3, color);
    } else if (accent == AccentMark::Stroke) {
      const int sy = gy + gh / 2;
      tft_.drawLine(gx - 1, sy, gx + gw, sy - 1, color);
    }

    penX += adv;
  }
}

void TftPanelUI::drawUtf8PlText(const String& text, int x, int y, const GFXfont* font, uint16_t color) {
  AccentMark accents[256];
  int count = 0;
  const String safe = toFontSafeText(text, accents, 256, count);
  tft_.setFont(font);
  tft_.setCursor(x, y);
  tft_.setTextColor(color);
  tft_.print(safe);
  tft_.setFont(nullptr);
  drawAccents(safe, accents, count, x, y, font, color);
}

int TftPanelUI::measureTextWidth(const String& text, const GFXfont* font) {
  return textWidthFontSafe(text, font);
}

void TftPanelUI::drawStatusFieldRow(int xOffset, StatusField field) {
  switch (field) {
    case StatusField::WiFi: {
      const bool wifiOk = (WiFi.status() == WL_CONNECTED) && !(config_ && config_->isInAPMode());
      const bool wifiManualOff = config_ && config_->isWiFiManuallyDisconnected();
      String wifiState = wifiOk ? "Połączono" : (config_ && config_->isInAPMode() ? "Tryb AP" : "Offline");
      if (!wifiOk && config_ && !config_->isWiFiConfigured()) wifiState = "Brak danych";
      if (wifiManualOff) wifiState = "Rozłączono";
      const uint16_t wifiStateColor = wifiOk ? kGood : (wifiManualOff ? kTextMuted : kWarn);

      const int x = 16 + xOffset;
      const int w = 288;
      const int wifiY = 72;
      const int wifiH = 36;
      if (uiMode_ == UiMode::ValueEdit && statusField_ == StatusField::WiFi) {
        tft_.fillRoundRect(x, wifiY, w, wifiH, 10, kCard);
        tft_.drawRoundRect(x, wifiY, w, wifiH, 10, kStroke);
        drawLeftText("WiFi:", 30 + xOffset, wifiY + 24, &SFNS9pt7b, kTextMuted);

        const int btnW = 92;
        const int btnH = 22;
        const int btnGap = 8;
        const int btnY = wifiY + 7;
        const int btn2X = x + w - 12 - btnW;
        const int btn1X = btn2X - btnGap - btnW;
        for (int i = 0; i < 2; ++i) {
          const int bx = (i == 0) ? btn1X : btn2X;
          const bool sel = (statusWifiAction_ == i);
          const uint16_t fill = sel ? blend565(kStrokeStrong, kCard, 80) : rgb565(10, 28, 44);
          const uint16_t stroke = sel ? kStrokeStrong : kStroke;
          tft_.fillRoundRect(bx, btnY, btnW, btnH, 6, fill);
          tft_.drawRoundRect(bx, btnY, btnW, btnH, 6, stroke);
          drawCenteredText(i == 0 ? "Połącz" : "Rozłącz", bx + (btnW / 2), btnY + (btnH / 2), &SFNS9pt7b, kTextMain);
        }
      } else {
        drawStatusRow(xOffset, 72, "WiFi", wifiState, wifiStateColor);
      }
      return;
    }
    case StatusField::Cloud: {
      String mqttState = "Wyłączone";
      uint16_t mqttColor = kTextMuted;
      if (mqtt_ && mqtt_->isConfigured()) {
        const bool ok = mqtt_->isConnected();
        mqttState = ok ? "Połączono" : "Offline";
        mqttColor = ok ? kGood : kWarn;
      }
      drawStatusRow(xOffset, 116, "Cloud", mqttState, mqttColor);
      return;
    }
    case StatusField::Temp: {
      const float chipTemp = temperatureRead();
      const String chipTempText = isfinite(chipTemp) ? (String(chipTemp, 1) + " C") : String("-");
      drawStatusRow(xOffset, 160, "Temp", chipTempText, rgb565(109, 214, 255));
      return;
    }
    case StatusField::Watering: {
      const int wateringPct = weather_ ? weather_->getWateringPercent() : 0;
      drawStatusRow(xOffset, 204, "Nawadnianie", String(wateringPct) + " %", kTextMain);
      return;
    }
  }
}

void TftPanelUI::drawStatusFocusBox(int xOffset, StatusField field) {
  if (uiMode_ == UiMode::CardSelect) return;
  const int x = 16 + xOffset;
  const int w = 288;
  const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);
  const int focusY = statusFieldTopY(field);
  const int focusH = statusFieldHeight(field);
  tft_.drawRoundRect(x, focusY, w, focusH, 10, focusColor);
  tft_.drawRoundRect(x + 1, focusY + 1, w - 2, focusH - 2, 9, blend565(kStrokeStrong, kTextMain, 72));
}

bool TftPanelUI::applyLogsTurnStep(int xOffset, int step) {
  if (step == 0) return false;
  const int prevScroll = logsScrollPx_;
  logsScrollPx_ += (step > 0 ? 1 : -1) * kLogsRowH;
  normalizeLogsScroll(false);
  if (logsScrollPx_ == prevScroll) return false;

  const unsigned long now = millis();
  logsLastInteractionMs_ = now;
  lastLogsMarqueeSignature_ = 0;
  logsScrollbarAlpha_ = 255;
  logsLastScrollMs_ = now;
  logsLastFadeMs_ = now;
  drawLogsPanelText(xOffset);
  drawLogsScrollbar(xOffset, true);
  lastFrameMs_ = now;
  dirty_ = false;
  return true;
}

bool TftPanelUI::processPendingLogsScroll(int xOffset, bool forceImmediate) {
  if (logsQueuedTurns_ == 0) return false;
  const unsigned long now = millis();
  if (!forceImmediate && (now - logsLastStepMs_) < kLogsScrollStepIntervalMs) return false;

  const int step = (logsQueuedTurns_ > 0) ? 1 : -1;
  const bool moved = applyLogsTurnStep(xOffset, step);
  if (moved) {
    logsQueuedTurns_ -= step;
  } else {
    logsQueuedTurns_ = 0;
  }
  logsLastStepMs_ = now;
  return moved;
}

void TftPanelUI::normalizeLogsScroll(bool preserveAnchor) {
  const int visibleCount = logs_ ? logs_->visibleCount() : 0;
  if (preserveAnchor && logsScrollPx_ > 0 && visibleCount > lastLogsVisibleCount_) {
    logsScrollPx_ += (visibleCount - lastLogsVisibleCount_) * kLogsRowH;
  }

  const int contentH = visibleCount * kLogsRowH;
  logsMaxScrollPx_ = contentH - (kLogsListH - 6);
  if (logsMaxScrollPx_ < 0) logsMaxScrollPx_ = 0;
  if (logsScrollPx_ < 0) logsScrollPx_ = 0;
  if (logsScrollPx_ > logsMaxScrollPx_) logsScrollPx_ = logsMaxScrollPx_;
  if (kLogsRowH > 0) logsScrollPx_ = (logsScrollPx_ / kLogsRowH) * kLogsRowH;
}

int TftPanelUI::logsVisibleRowCount() const {
  return kLogsVisibleRows;
}

int TftPanelUI::logsMessageClipWidth(int xOffset) const {
  (void)xOffset;
  return max(0, kLogsMsgClipW);
}

String TftPanelUI::ellipsizeTextToWidth(const String& text, int maxWidth, const GFXfont* font) {
  if (maxWidth <= 0) return "";
  if (measureTextWidth(text, font) <= maxWidth) return text;

  const String suffix = "...";
  const int suffixW = measureTextWidth(suffix, font);
  if (suffixW >= maxWidth) return suffix;

  String out;
  const int len = text.length();
  int i = 0;
  while (i < len) {
    int charLen = 1;
    const uint8_t b0 = (uint8_t)text[i];
    if ((b0 & 0x80) == 0x00) charLen = 1;
    else if ((b0 & 0xE0) == 0xC0 && i + 1 < len) charLen = 2;
    else if ((b0 & 0xF0) == 0xE0 && i + 2 < len) charLen = 3;
    else if ((b0 & 0xF8) == 0xF0 && i + 3 < len) charLen = 4;

    const String glyph = text.substring(i, i + charLen);
    if (measureTextWidth(out + glyph + suffix, font) > maxWidth) break;
    out += glyph;
    i += charLen;
  }

  out.trim();
  if (!out.length()) return suffix;
  return out + suffix;
}

int TftPanelUI::logsRowMarqueeOffsetPx(const String& text, unsigned long nowMs, int maxWidth) {
  if (nowMs <= logsLastInteractionMs_ || nowMs - logsLastInteractionMs_ < kLogsMarqueeDelayMs) return 0;
  const int fullWidth = measureTextWidth(text, &SFNS9pt7b);
  const int overflow = fullWidth - maxWidth;
  if (overflow <= 0) return 0;

  const unsigned long phaseMs = nowMs - logsLastInteractionMs_ - kLogsMarqueeDelayMs;
  const uint32_t phaseSteps = (uint32_t)(phaseMs / kLogsMarqueeTickMs);
  const uint32_t cycleSteps = (uint32_t)kLogsMarqueeHoldSteps + (uint32_t)((overflow + kLogsMarqueeStepPx - 1) / kLogsMarqueeStepPx) + (uint32_t)kLogsMarqueeHoldSteps;
  if (cycleSteps == 0) return 0;
  uint32_t step = phaseSteps % cycleSteps;
  if (step < kLogsMarqueeHoldSteps) return 0;
  step -= kLogsMarqueeHoldSteps;
  const int offset = (int)step * kLogsMarqueeStepPx;
  if (offset > overflow) return overflow;
  return offset;
}

void TftPanelUI::drawLogsPanelFrame(int xOffset) {
  const uint16_t panelBg = kCard;
  const uint16_t listBg = rgb565(5, 18, 34);
  const int panelX = kLogsPanelX + xOffset;
  const int listX = kLogsListX + xOffset;
  normalizeLogsScroll(false);

  tft_.fillRoundRect(panelX, kLogsPanelY, kLogsPanelW, kLogsPanelH, 12, panelBg);
  tft_.drawRoundRect(panelX, kLogsPanelY, kLogsPanelW, kLogsPanelH, 12, kStroke);
  drawLeftText("Na żywo", panelX + 14, kLogsPanelY + 22, &SFNS9pt7b, kTextMuted);
  drawRightText(String(logs_ ? logs_->visibleCount() : 0) + " wpisów", panelX + kLogsPanelW - 14, kLogsPanelY + 22, &SFNS9pt7b, kStrokeStrong);
  tft_.drawFastHLine(panelX + 10, kLogsPanelY + 28, kLogsPanelW - 20, blend565(kStroke, panelBg, 92));
  tft_.fillRoundRect(listX, kLogsListY, kLogsListW, kLogsListH, 8, listBg);
  tft_.drawRoundRect(listX, kLogsListY, kLogsListW, kLogsListH, 8, kStroke);
}

void TftPanelUI::renderLogsRowToCanvas(int slotIndex, int logIndex, int msgShiftPx) {
  const uint16_t rowBgA = rgb565(7, 23, 42);
  const uint16_t rowBgB = rgb565(9, 27, 47);
  const uint16_t listBg = rgb565(5, 18, 34);
  auto& canvas = logsCanvas();
  const int rowX = 0;
  const int rowY = slotIndex * kLogsRowH;
  const int rowW = kLogsCanvasW;
  const int rowH = kLogsRowH - 2;
  const int timeY = rowY + 13;
  const int msgClipX = kLogsMsgClipX;
  const int msgClipW = logsMessageClipWidth(0);
  const int baselineY = rowY + 16;
  const uint16_t rowBg = (slotIndex % 2 == 0) ? rowBgA : rowBgB;
  const uint16_t separator = blend565(kStroke, rowBg, 96);

  auto drawTextOn = [&](Adafruit_GFX& gfx, const String& text, int x, int y, const GFXfont* font, uint16_t color) {
    AccentMark accents[256];
    int count = 0;
    const String safe = toFontSafeText(text, accents, 256, count);
    gfx.setFont(font);
    gfx.setCursor(x, y);
    gfx.setTextColor(color);
    gfx.print(safe);
    gfx.setFont(nullptr);

    int penX = x;
    const int n = min((int)safe.length(), count);
    for (int i = 0; i < n; ++i) {
      const char c = safe[i];
      const AccentMark accent = accents[i];
      const int adv = glyphAdvance(c, font);
      if (accent == AccentMark::None) {
        penX += adv;
        continue;
      }

      int gx = penX;
      int gy = y - 8;
      int gw = 6;
      int gh = 8;
      if (font) {
        const uint8_t uc = (uint8_t)c;
        if (uc >= font->first && uc <= font->last) {
          const GFXglyph* glyph = &font->glyph[uc - font->first];
          gx = penX + glyph->xOffset;
          gy = y + glyph->yOffset;
          gw = max(1, (int)glyph->width);
          gh = max(1, (int)glyph->height);
        }
      }

      if (accent == AccentMark::Acute) {
        const int ax = gx + gw - 2;
        const int ay = gy - 2;
        gfx.drawLine(ax, ay, ax + 3, ay - 3, color);
      } else if (accent == AccentMark::Dot) {
        gfx.fillCircle(gx + gw / 2, gy - 3, 1, color);
      } else if (accent == AccentMark::Ogonek) {
        const int ox = gx + gw - 1;
        const int oy = y + 1;
        gfx.drawLine(ox, oy, ox - 2, oy + 3, color);
        gfx.drawPixel(ox - 3, oy + 3, color);
      } else if (accent == AccentMark::Stroke) {
        const int sy = gy + gh / 2;
        gfx.drawLine(gx - 1, sy, gx + gw, sy - 1, color);
      }

      penX += adv;
    }
  };

  auto drawDefaultCentered = [&](Adafruit_GFX& gfx, const String& text, int x, int y, int w, uint16_t color) {
    if (!text.length() || w <= 0) return;
    const int textW = (int)text.length() * 6;
    const int drawX = x + max(0, (w - textW) / 2);
    gfx.setFont(nullptr);
    gfx.setCursor(drawX, y);
    gfx.setTextColor(color);
    gfx.print(text);
  };

  canvas.fillRect(0, rowY, kLogsCanvasW, kLogsRowH, listBg);
  canvas.fillRoundRect(rowX, rowY, rowW, rowH, 6, rowBg);

  if (logIndex < 0 || !logs_ || logIndex >= logs_->visibleCount()) return;

  const String rawLine = logs_->visibleRawAt(logIndex);
  const String line = logs_->visibleAt(logIndex);
  const String storedTimestamp = logs_->visibleTimestampAt(logIndex);
  const String timeSource = resolveLogTimestampSource(storedTimestamp, rawLine, line);
  const String msgText = extractLogMessageText(line);
  const String renderText = msgText.length() ? msgText : line;
  const uint16_t accent = logAccentColor(renderText);
  canvas.fillRoundRect(rowX, rowY, 4, rowH, 3, accent);

  const int timeAreaX = rowX + 6;
  const int timeAreaH = rowH - 2;
  canvas.fillRect(timeAreaX, rowY + 1, msgClipX - timeAreaX, timeAreaH, rowBg);
  canvas.fillRect(msgClipX, rowY + 1, msgClipW, timeAreaH, rowBg);

  auto& msgCanvas = logsMessageCanvas();
  msgCanvas.fillScreen(rowBg);
  drawTextOn(msgCanvas, renderText, kLogsMsgTextInset - max(0, msgShiftPx), 16, &SFNS9pt7b, kTextMain);

  uint16_t* dstBase = canvas.getBuffer() + (rowY * kLogsCanvasW) + msgClipX;
  uint16_t* srcBase = msgCanvas.getBuffer();
  for (int y = 0; y < kLogsRowH; ++y) {
    memcpy(dstBase + (y * kLogsCanvasW),
           srcBase + (y * kLogsMsgClipW),
           (size_t)kLogsMsgClipW * sizeof(uint16_t));
  }

  // Dodatkowe, twarde odciecie obu stref po bokach chroni kolumne czasu
  // i prawa krawedz listy przed jakimkolwiek "przeciekaniem" marquee.
  canvas.fillRect(timeAreaX, rowY + 1, msgClipX - timeAreaX, timeAreaH, rowBg);
  const int rightGuardX = msgClipX + msgClipW;
  if (rightGuardX < rowW - 1) {
    canvas.fillRect(rightGuardX, rowY + 1, rowW - rightGuardX, timeAreaH, rowBg);
  }

  const int separatorX = msgClipX - kLogsDividerGap;
  canvas.drawFastVLine(separatorX, rowY + 4, rowH - 8, separator);
  const AnimatedLogLabel labelState = buildAnimatedLogLabel(timeSource, millis());
  const int timeDrawW = max(0, msgClipX - timeAreaX - kLogsDividerGap);
  if (labelState.primaryAlpha > 0 && labelState.primaryText.length()) {
    drawDefaultCentered(canvas, labelState.primaryText, timeAreaX, timeY, timeDrawW,
                        blend565(rowBg, kTextMuted, labelState.primaryAlpha));
  }
  if (labelState.secondaryAlpha > 0 && labelState.secondaryText.length()) {
    drawDefaultCentered(canvas, labelState.secondaryText, timeAreaX, timeY, timeDrawW,
                        blend565(rowBg, kStrokeStrong, labelState.secondaryAlpha));
  }
  canvas.fillRoundRect(rowX, rowY, 4, rowH, 3, accent);
}

void TftPanelUI::pushLogsCanvas(int xOffset, int slotIndex) {
  auto& canvas = logsCanvas();
  const int screenX = kLogsCanvasX + xOffset;
  const int screenY = kLogsCanvasY;
  if (slotIndex < 0) {
    tft_.drawRGBBitmap(screenX, screenY, canvas.getBuffer(), kLogsCanvasW, kLogsCanvasH);
    return;
  }

  if (slotIndex >= kLogsVisibleRows) return;
  uint16_t* rowPtr = canvas.getBuffer() + (slotIndex * kLogsRowH * kLogsCanvasW);
  tft_.drawRGBBitmap(screenX, screenY + slotIndex * kLogsRowH, rowPtr, kLogsCanvasW, kLogsRowH);
}

void TftPanelUI::drawLogsRowSlot(int xOffset, int slotIndex, int logIndex, int msgShiftPx) {
  renderLogsRowToCanvas(slotIndex, logIndex, msgShiftPx);
  pushLogsCanvas(xOffset, slotIndex);
}

void TftPanelUI::drawLogsPanelText(int xOffset) {
  const uint16_t listBg = rgb565(5, 18, 34);
  auto& canvas = logsCanvas();

  const int visibleCount = logs_ ? logs_->visibleCount() : 0;
  if (visibleCount <= 0) {
    canvas.fillScreen(listBg);
    pushLogsCanvas(xOffset);
    drawCenteredText("Brak logów", kLogsListX + xOffset + (kLogsListW / 2), kLogsListY + (kLogsListH / 2), &SFNS9pt7b, kTextMuted);
    return;
  }

  canvas.fillScreen(listBg);
  const int startIdx = (kLogsRowH > 0) ? (logsScrollPx_ / kLogsRowH) : 0;
  const int rowCount = logsVisibleRowCount();
  const unsigned long nowMs = millis();
  for (int row = 0; row < rowCount; ++row) {
    const int logIdx = startIdx + row;
    int shiftPx = 0;
    if (logIdx < visibleCount) {
      const String line = logs_->visibleAt(logIdx);
      const String msgText = extractLogMessageText(line);
      const String renderText = msgText.length() ? msgText : line;
      const int msgAreaW = logsMessageClipWidth(xOffset) - 4;
      shiftPx = logsRowMarqueeOffsetPx(renderText, nowMs, msgAreaW);
    }
    renderLogsRowToCanvas(row, (logIdx < visibleCount) ? logIdx : -1, shiftPx);
  }
  pushLogsCanvas(xOffset);
  const int extraY = kLogsCanvasY + kLogsCanvasH;
  const int extraH = (kLogsListY + kLogsListH - 4) - extraY;
  if (extraH > 0) tft_.fillRect(kLogsCanvasX + xOffset, extraY, kLogsCanvasW, extraH, listBg);
}

void TftPanelUI::drawLogsScrollbar(int xOffset, bool clearTrack) {
  const uint16_t panelBg = kCard;
  const int trackX = kLogsTrackX + xOffset;
  if (clearTrack) {
    tft_.fillRect(trackX - 1, kLogsTrackY, kLogsTrackW + 2, kLogsTrackH, panelBg);
  }
  if (logsMaxScrollPx_ <= 0 || logsScrollbarAlpha_ == 0) return;

  const uint16_t trackColor = blend565(panelBg, rgb565(10, 32, 54), logsScrollbarAlpha_);
  const uint16_t thumbColor = blend565(panelBg, kStrokeStrong, logsScrollbarAlpha_);
  tft_.fillRoundRect(trackX, kLogsTrackY, kLogsTrackW, kLogsTrackH, 2, trackColor);

  const int contentH = max((logs_ ? logs_->visibleCount() : 0) * kLogsRowH, 1);
  int thumbH = (kLogsTrackH * (kLogsListH - 6)) / contentH;
  if (thumbH < 18) thumbH = 18;
  if (thumbH > kLogsTrackH) thumbH = kLogsTrackH;
  const int travel = kLogsTrackH - thumbH;
  const int thumbY = kLogsTrackY + ((travel * logsScrollPx_) / max(logsMaxScrollPx_, 1));
  tft_.fillRoundRect(trackX, thumbY, kLogsTrackW, thumbH, 2, thumbColor);

  // Scrollbar siedzi w kanale pomiędzy ramką listy i zewnętrzną ramką panelu.
  // Po rysowaniu odtwarzamy obie ramki, żeby całość była wizualnie domknięta.
  tft_.drawRoundRect(kLogsListX + xOffset, kLogsListY, kLogsListW, kLogsListH, 8, kStroke);
  tft_.drawRoundRect(kLogsPanelX + xOffset, kLogsPanelY, kLogsPanelW, kLogsPanelH, 12, kStroke);
}

void TftPanelUI::drawLogsFocusBox(int xOffset) {
  if (uiMode_ == UiMode::CardSelect) return;
  const int panelX = kLogsPanelX + xOffset;
  const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);
  tft_.drawRoundRect(panelX, kLogsPanelY, kLogsPanelW, kLogsPanelH, 12, focusColor);
  tft_.drawRoundRect(panelX + 1, kLogsPanelY + 1, kLogsPanelW - 2, kLogsPanelH - 2, 11, blend565(kStrokeStrong, kTextMain, 72));
}

void TftPanelUI::drawLogs(int xOffset) {
  drawCenteredText("Logi", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);
  drawLogsPanelFrame(xOffset);
  drawLogsPanelText(xOffset);
  drawLogsScrollbar(xOffset, true);
  if (uiMode_ != UiMode::CardSelect) drawLogsFocusBox(xOffset);

  logsSnapshotValid_ = true;
  lastLogsRevision_ = logs_ ? logs_->revision() : 0;
  lastLogsVisibleCount_ = logs_ ? logs_->visibleCount() : 0;
  lastLogsFocusVisible_ = (uiMode_ != UiMode::CardSelect);
  lastLogsMarqueeSignature_ = 0;
  for (int i = 0; i < kLogsSlotCacheSize; ++i) {
    lastLogsSlotIndices_[i] = -1;
    lastLogsSlotShiftPx_[i] = -1;
    lastLogsSlotTimeSig_[i] = 0;
  }
}

bool TftPanelUI::refreshLogsLive(int xOffset) {
  const uint32_t revision = logs_ ? logs_->revision() : 0;
  const int visibleCount = logs_ ? logs_->visibleCount() : 0;
  const bool focusVisible = (uiMode_ != UiMode::CardSelect);
  const int startIdx = (kLogsRowH > 0) ? (logsScrollPx_ / kLogsRowH) : 0;
  const int rowCount = logsVisibleRowCount();
  uint32_t marqueeSig = 2166136261u;
  const int msgAreaW = logsMessageClipWidth(xOffset) - 4;
  const unsigned long nowMs = millis();
  int rowIndices[kLogsSlotCacheSize];
  int rowShifts[kLogsSlotCacheSize];
  uint32_t rowTimeSigs[kLogsSlotCacheSize];
  for (int i = 0; i < kLogsSlotCacheSize; ++i) {
    rowIndices[i] = -1;
    rowShifts[i] = 0;
    rowTimeSigs[i] = 0;
  }
  for (int row = 0; row < rowCount; ++row) {
    const int logIdx = startIdx + row;
    if (logIdx >= visibleCount || !logs_) {
      marqueeSig ^= 0xFFu;
      marqueeSig *= 16777619u;
      if (row < kLogsSlotCacheSize) rowIndices[row] = -1;
      continue;
    }
    const String rawLine = logs_->visibleRawAt(logIdx);
    const String line = logs_->visibleAt(logIdx);
    const String storedTimestamp = logs_->visibleTimestampAt(logIdx);
    const String timeSource = resolveLogTimestampSource(storedTimestamp, rawLine, line);
    const String msgText = extractLogMessageText(line);
    const String renderText = msgText.length() ? msgText : line;
    const int shift = logsRowMarqueeOffsetPx(renderText, nowMs, msgAreaW);
    const uint32_t timeSig = animatedLogLabelSignature(timeSource, nowMs);
    marqueeSig ^= (uint32_t)shift;
    marqueeSig *= 16777619u;
    marqueeSig ^= timeSig;
    marqueeSig *= 16777619u;
    if (row < kLogsSlotCacheSize) {
      rowIndices[row] = logIdx;
      rowShifts[row] = shift;
      rowTimeSigs[row] = timeSig;
    }
  }

  if (!logsSnapshotValid_) {
    drawLogs(xOffset);
    return true;
  }

  bool changed = false;
  if (revision != lastLogsRevision_ || visibleCount != lastLogsVisibleCount_) {
    normalizeLogsScroll(true);
    drawLogsPanelFrame(xOffset);
    drawLogsPanelText(xOffset);
    drawLogsScrollbar(xOffset, true);
    if (focusVisible) drawLogsFocusBox(xOffset);
    changed = true;
  } else if (focusVisible != lastLogsFocusVisible_) {
    drawLogs(xOffset);
    changed = true;
  } else if (marqueeSig != lastLogsMarqueeSignature_) {
    for (int row = 0; row < rowCount && row < kLogsSlotCacheSize; ++row) {
      if (rowIndices[row] != lastLogsSlotIndices_[row] ||
          rowShifts[row] != lastLogsSlotShiftPx_[row] ||
          rowTimeSigs[row] != lastLogsSlotTimeSig_[row]) {
        drawLogsRowSlot(xOffset, row, rowIndices[row], rowShifts[row]);
        changed = true;
      }
    }
    if (changed && logsScrollbarAlpha_ > 0) {
      drawLogsScrollbar(xOffset, true);
    }
  }

  lastLogsRevision_ = revision;
  lastLogsVisibleCount_ = visibleCount;
  lastLogsFocusVisible_ = focusVisible;
  lastLogsMarqueeSignature_ = marqueeSig;
  for (int row = 0; row < kLogsSlotCacheSize; ++row) {
    lastLogsSlotIndices_[row] = rowIndices[row];
    lastLogsSlotShiftPx_[row] = rowShifts[row];
    lastLogsSlotTimeSig_[row] = rowTimeSigs[row];
  }
  logsSnapshotValid_ = true;
  return changed;
}

int TftPanelUI::zoneCount() const {
  if (!zones_) return 1;
  const int n = zones_->getZoneCount();
  return n > 0 ? n : 1;
}

int TftPanelUI::wrapZone(int idx) const {
  const int n = zoneCount();
  while (idx < 0) idx += n;
  while (idx >= n) idx -= n;
  return idx;
}

void TftPanelUI::draw() {
  if (dimmed_) {
    drawDimmedWallpaper();
    dirty_ = false;
    lastFrameMs_ = millis();
    return;
  }

  if (topBarDirty_) {
    tft_.fillScreen(kBg);
    drawTopBar();
    topBarDirty_ = false;
    hasRenderedScreen_ = false;
  }

  // Czyścimy tło sekcji treści tylko przy pierwszym renderze lub zmianie karty.
  if (!hasRenderedScreen_ || lastRenderedScreen_ != screen_) {
    tft_.fillRect(0, kTopBarHeight + 1, kDisplayWidth, kDisplayHeight - (kTopBarHeight + 1), kBg);
  }
  drawScreen(screen_, 0);
  hasRenderedScreen_ = true;
  lastRenderedScreen_ = screen_;

  dirty_ = false;
  lastFrameMs_ = millis();
}

void TftPanelUI::drawScreen(Screen screen, int xOffset) {
  if (screen == Screen::Home) drawHome(xOffset);
  else if (screen == Screen::Weather) drawWeather(xOffset);
  else if (screen == Screen::Manual) drawManual(xOffset);
  else if (screen == Screen::Status) drawStatus(xOffset);
  else if (screen == Screen::Logs) drawLogs(xOffset);
  else drawSettings(xOffset);
}

void TftPanelUI::drawTopBar() {
  for (int y = 0; y < kTopBarHeight; ++y) {
    const uint8_t t = (uint8_t)((y * 255) / (kTopBarHeight - 1));
    const uint16_t c = blend565(kTopBarFrom, kTopBarTo, t);
    tft_.drawFastHLine(0, y, kDisplayWidth, c);
  }

  const uint16_t accent = blend565(kStrokeStrong, kTextMain, 96);
  tft_.drawFastHLine(0, kTopBarHeight, kDisplayWidth, accent);

  constexpr int kTopBarLogoSize = 30;
  const int logoX = 7;
  const int logoY = (kTopBarHeight - kTopBarLogoSize) / 2;
  drawLogo(logoX, logoY, kTopBarLogoSize);
  drawLeftText("WM Sprinkler", logoX + kTopBarLogoSize + 9, 30, &SFNS12pt7b, kTextMain);
  drawTopBarClock(true);
}

String TftPanelUI::buildTopBarClockText() const {
  const time_t nowTs = time(nullptr);
  if (nowTs < 100000) return "--:--";

  struct tm localTm;
  if (!localtime_r(&nowTs, &localTm)) return "--:--";

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", localTm.tm_hour, localTm.tm_min);
  return String(buf);
}

bool TftPanelUI::drawTopBarClock(bool force) {
  const String clockText = buildTopBarClockText();
  if (!force && clockText == lastTopBarClockText_) return false;

  for (int y = kTopBarClockRegionY; y < kTopBarClockRegionY + kTopBarClockRegionH; ++y) {
    tft_.drawFastHLine(kTopBarClockRegionX, y, kTopBarClockRegionW, topBarColorAtY(y));
  }
  drawRightText(clockText, kTopBarClockRightX, kTopBarClockBaselineY, &SFNS12pt7b, kTextMain);

  lastTopBarClockText_ = clockText;
  return true;
}

void TftPanelUI::getDimmedWateringState(bool& active, int& zoneIdx, int& remainingSec, int& totalSec) const {
  active = false;
  zoneIdx = -1;
  remainingSec = 0;
  totalSec = 0;
  if (!zones_) return;

  const int n = zoneCount();
  for (int i = 0; i < n; ++i) {
    if (!zones_->getZoneState(i)) continue;
    const int rem = zones_->getRemainingSeconds(i);
    int total = zones_->getZoneInitialDurationSeconds(i);
    if (total < rem) total = rem;
    if (total <= 0) total = max(rem, 1);
    active = true;
    zoneIdx = i;
    remainingSec = max(0, rem);
    totalSec = total;
    return;
  }
}

void TftPanelUI::drawRingArc(int cx, int cy, int innerR, int outerR, int startDeg, int sweepDeg, uint16_t color) {
  if (sweepDeg <= 0 || innerR <= 0 || outerR <= innerR) return;
  if (sweepDeg > 360) sweepDeg = 360;
  for (int a = 0; a <= sweepDeg; a += 2) {
    const float rad = (float)(startDeg + a) * DEG_TO_RAD;
    const int x0 = cx + (int)lroundf(cosf(rad) * innerR);
    const int y0 = cy + (int)lroundf(sinf(rad) * innerR);
    const int x1 = cx + (int)lroundf(cosf(rad) * outerR);
    const int y1 = cy + (int)lroundf(sinf(rad) * outerR);
    tft_.drawLine(x0, y0, x1, y1, color);
  }
}

void TftPanelUI::drawDimmedWateringGauge(bool fullScreenClear, int zoneIdx, int remainingSec, int totalSec) {
  if (zoneIdx < 0) return;
  if (remainingSec < 0) remainingSec = 0;
  if (totalSec <= 0) totalSec = max(remainingSec, 1);
  if (totalSec < remainingSec) totalSec = remainingSec;

  const uint16_t dialTrack = rgb565(60, 76, 100);
  const uint16_t dialInner = rgb565(8, 22, 42);
  const uint16_t dialBlue = rgb565(37, 99, 235);     // cloud-like blue
  const uint16_t dialBlueHi = rgb565(96, 165, 250);  // highlight
  const uint16_t dialGlow = blend565(kDimBg, dialBlue, 58);
  const auto sweepFrom = [](int rem, int total) -> int {
    if (rem < 0) rem = 0;
    if (total <= 0) total = max(rem, 1);
    if (total < rem) total = rem;
    const float ratio = (total > 0) ? ((float)rem / (float)total) : 0.0f;
    return (int)lroundf(max(0.0f, min(1.0f, ratio)) * 360.0f);
  };
  const int sweepDeg = sweepFrom(remainingSec, totalSec);

  auto drawZoneLabel = [&]() {
    String zoneLabel = zones_ ? zones_->getZoneName(zoneIdx) : "";
    zoneLabel.trim();
    if (!zoneLabel.length()) zoneLabel = "Strefa " + String(zoneIdx + 1);
    zoneLabel.replace("ą", "Ą");
    zoneLabel.replace("ć", "Ć");
    zoneLabel.replace("ę", "Ę");
    zoneLabel.replace("ł", "Ł");
    zoneLabel.replace("ń", "Ń");
    zoneLabel.replace("ó", "Ó");
    zoneLabel.replace("ś", "Ś");
    zoneLabel.replace("ż", "Ż");
    zoneLabel.replace("ź", "Ź");
    zoneLabel.toUpperCase();

    constexpr int kLabelAreaY = kDisplayHeight - 28;
    constexpr int kLabelAreaH = 26;
    const int labelY = kDisplayHeight - 10;
    tft_.fillRect(0, kLabelAreaY, kDisplayWidth, kLabelAreaH, kDimBg);

    const uint16_t zoneColor = blend565(kTextMain, kGood, 24);
    const uint16_t shadowColor = blend565(kDimBg, kTextMain, 44);
    const GFXfont* labelFont = &SFNS12pt7b;
    int w = measureTextWidth(zoneLabel, labelFont);
    if (w > (kDisplayWidth - 10)) {
      labelFont = &SFNS9pt7b;
      w = measureTextWidth(zoneLabel, labelFont);
    }
    const int textX = (kDisplayWidth - w) / 2;
    drawUtf8PlText(zoneLabel, textX + 1, labelY + 1, labelFont, shadowColor);
    drawUtf8PlText(zoneLabel, textX, labelY, labelFont, zoneColor);
    drawUtf8PlText(zoneLabel, textX + 1, labelY, labelFont, zoneColor);
  };

  auto drawTimeValue = [&]() {
    constexpr int kTimeBoxW = 138;
    constexpr int kTimeBoxH = 40;
    const int timeX = kDimDialCx - (kTimeBoxW / 2);
    const int timeY = kDimDialCy - (kTimeBoxH / 2);
    tft_.fillRoundRect(timeX, timeY, kTimeBoxW, kTimeBoxH, 8, dialInner);

    char timeBuf[16];
    const int mm = remainingSec / 60;
    const int ss = remainingSec % 60;
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mm, ss);
    drawCenteredText(String(timeBuf), kDimDialCx, kDimDialCy, &SFNS18pt7b, kTextMain);
  };

  if (fullScreenClear) {
    tft_.fillScreen(kDimBg);
    tft_.fillCircle(kDimDialCx, kDimDialCy, kDimDialOuterR + 5, dialGlow);
    tft_.fillCircle(kDimDialCx, kDimDialCy, kDimDialOuterR, dialTrack);
    tft_.fillCircle(kDimDialCx, kDimDialCy, kDimDialInnerR, dialInner);
    drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 1, kDimDialOuterR - 1, -90, sweepDeg, dialBlue);
    drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 4, kDimDialOuterR - 4, -90, sweepDeg, dialBlueHi);
    if (sweepDeg > 0) {
      const float endRad = (float)(-90 + sweepDeg) * DEG_TO_RAD;
      const int capR = (kDimDialInnerR + kDimDialOuterR) / 2;
      const int capX = kDimDialCx + (int)lroundf(cosf(endRad) * capR);
      const int capY = kDimDialCy + (int)lroundf(sinf(endRad) * capR);
      tft_.fillCircle(capX, capY, 4, dialBlueHi);
    }
    drawTimeValue();
    drawZoneLabel();
    return;
  }

  int prevRemaining = dimmedWateringSnapshotRemainingSec_;
  if (prevRemaining < 0) prevRemaining = remainingSec;
  int prevTotal = dimmedWateringSnapshotTotalSec_;
  if (prevTotal <= 0) prevTotal = max(prevRemaining, 1);
  if (prevTotal < prevRemaining) prevTotal = prevRemaining;
  const int prevSweep = sweepFrom(prevRemaining, prevTotal);

  if (prevSweep != sweepDeg) {
    if (sweepDeg < prevSweep) {
      const int eraseStart = -90 + sweepDeg;
      const int eraseSweep = prevSweep - sweepDeg;
      drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 1, kDimDialOuterR - 1, eraseStart, eraseSweep, dialTrack);
      drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 4, kDimDialOuterR - 4, eraseStart, eraseSweep, dialTrack);
    } else {
      const int addStart = -90 + prevSweep;
      const int addSweep = sweepDeg - prevSweep;
      drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 1, kDimDialOuterR - 1, addStart, addSweep, dialBlue);
      drawRingArc(kDimDialCx, kDimDialCy, kDimDialInnerR + 4, kDimDialOuterR - 4, addStart, addSweep, dialBlueHi);
    }

    const int capR = (kDimDialInnerR + kDimDialOuterR) / 2;
    if (prevSweep > 0) {
      const float prevRad = (float)(-90 + prevSweep) * DEG_TO_RAD;
      const int prevCapX = kDimDialCx + (int)lroundf(cosf(prevRad) * capR);
      const int prevCapY = kDimDialCy + (int)lroundf(sinf(prevRad) * capR);
      tft_.fillCircle(prevCapX, prevCapY, 5, dialTrack);
    }
    if (sweepDeg > 0) {
      const float newRad = (float)(-90 + sweepDeg) * DEG_TO_RAD;
      const int capX = kDimDialCx + (int)lroundf(cosf(newRad) * capR);
      const int capY = kDimDialCy + (int)lroundf(sinf(newRad) * capR);
      tft_.fillCircle(capX, capY, 4, dialBlueHi);
    }
  }

  if (remainingSec != prevRemaining) {
    drawTimeValue();
  }
  if (zoneIdx != dimmedWateringSnapshotZone_) {
    drawZoneLabel();
  }
}

bool TftPanelUI::refreshDimmedWateringLive() {
  bool active = false;
  int zoneIdx = -1;
  int remainingSec = 0;
  int totalSec = 0;
  getDimmedWateringState(active, zoneIdx, remainingSec, totalSec);

  if (!active) {
    if (!dimmedWateringSnapshotActive_) return false;
    drawDimmedWallpaper();
    return true;
  }

  if (!dimmedWateringSnapshotActive_) {
    drawDimmedWateringGauge(true, zoneIdx, remainingSec, totalSec);
    dimmedWateringSnapshotActive_ = true;
    dimmedWateringSnapshotZone_ = zoneIdx;
    dimmedWateringSnapshotRemainingSec_ = remainingSec;
    dimmedWateringSnapshotTotalSec_ = totalSec;
    return true;
  }

  const bool changed =
    (zoneIdx != dimmedWateringSnapshotZone_) ||
    (remainingSec != dimmedWateringSnapshotRemainingSec_) ||
    (totalSec != dimmedWateringSnapshotTotalSec_);
  if (!changed) return false;

  const bool majorChange =
    (zoneIdx != dimmedWateringSnapshotZone_) ||
    (totalSec != dimmedWateringSnapshotTotalSec_);
  drawDimmedWateringGauge(majorChange, zoneIdx, remainingSec, totalSec);
  dimmedWateringSnapshotActive_ = true;
  dimmedWateringSnapshotZone_ = zoneIdx;
  dimmedWateringSnapshotRemainingSec_ = remainingSec;
  dimmedWateringSnapshotTotalSec_ = totalSec;
  return true;
}

void TftPanelUI::drawDimmedWallpaper() {
  bool active = false;
  int zoneIdx = -1;
  int remainingSec = 0;
  int totalSec = 0;
  getDimmedWateringState(active, zoneIdx, remainingSec, totalSec);
  if (active) {
    drawDimmedWateringGauge(true, zoneIdx, remainingSec, totalSec);
    dimmedWateringSnapshotActive_ = true;
    dimmedWateringSnapshotZone_ = zoneIdx;
    dimmedWateringSnapshotRemainingSec_ = remainingSec;
    dimmedWateringSnapshotTotalSec_ = totalSec;
    return;
  }

  tft_.fillScreen(kDimBg);

  // Tapeta wygaszacza: logo z napisem ma zajmować ~95% wysokości ekranu.
  // Na prośbę użytkownika powiększamy dodatkowo o +30%.
  const int outH = ((kDisplayHeight * 95) / 100) * 130 / 100;
  const int outW = outH;  // źródło jest kwadratowe
  const int x = (kDisplayWidth - outW) / 2;
  const int y = (kDisplayHeight - outH) / 2;
  if (!drawBmpFromFsScaled("/wm-logo-large-200.bmp", x, y, outW, outH, false, kDimBg)) {
    const int fallbackX = (kDisplayWidth - 200) / 2;
    const int fallbackY = (kDisplayHeight - 200) / 2;
    (void)drawBmpFromFs("/wm-logo-large-200.bmp", fallbackX, fallbackY, false, kDimBg);
  }
  dimmedWateringSnapshotActive_ = false;
  dimmedWateringSnapshotZone_ = -1;
  dimmedWateringSnapshotRemainingSec_ = -1;
  dimmedWateringSnapshotTotalSec_ = 0;
}

void TftPanelUI::drawHome(int xOffset) {
  drawCenteredText("Panel Główny", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);

  const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);

  for (int i = 0; i < zoneCount(); ++i) {
    const int col = i % 4;
    const int row = i / 4;
    const int x = kTileStartX + xOffset + col * (kTileW + kTileGapX);
    const int y = kTileStartY + row * (kTileH + kTileGapY);
    const bool zoneFocused = (uiMode_ != UiMode::CardSelect) && (i == selectedZone_);
    drawHomeTile(x, y, i, zoneFocused, focusColor);
  }

  if (uiMode_ != UiMode::CardSelect) {
    const int focusX = focusX_ + xOffset;
    if (focusX > -kTileW && focusX < kDisplayWidth) {
      tft_.drawRoundRect(focusX - 2, focusY_ - 2, kTileW + 4, kTileH + 4, 11, focusColor);
      tft_.drawRoundRect(focusX - 3, focusY_ - 3, kTileW + 6, kTileH + 6, 12, blend565(kStrokeStrong, kTextMain, 72));
    }
  }

  drawHomeStatusBar(xOffset);
  lastHomeActiveMask_ = buildHomeActiveMask();
  lastHomeZoneCount_ = zoneCount();
  homeSnapshotValid_ = true;
  lastHomeStateSignature_ = buildHomeStateSignature();
}

void TftPanelUI::drawHomeStatusBar(int xOffset) {
  const bool zoneSelectMode = (screen_ == Screen::Home && uiMode_ == UiMode::FieldSelect);
  int activeCount = 0;
  int activeZone = -1;
  for (int i = 0; i < zoneCount(); ++i) {
    if (zones_ && zones_->getZoneState(i)) {
      activeZone = i;
      activeCount++;
    }
  }

  const int barX = 12 + xOffset;
  tft_.fillRoundRect(barX, 194, 296, 38, 10, kCard);
  tft_.drawRoundRect(barX, 194, 296, 38, 10, kStroke);

  if (zoneSelectMode) {
    String zoneLabel = zones_ ? zones_->getZoneName(selectedZone_) : "";
    zoneLabel.trim();
    if (!zoneLabel.length()) zoneLabel = "Strefa " + String(selectedZone_ + 1);
    // Nazwa strefy wyświetlana wycentrowana, drukowanymi literami.
    zoneLabel.replace("ą", "Ą");
    zoneLabel.replace("ć", "Ć");
    zoneLabel.replace("ę", "Ę");
    zoneLabel.replace("ł", "Ł");
    zoneLabel.replace("ń", "Ń");
    zoneLabel.replace("ó", "Ó");
    zoneLabel.replace("ś", "Ś");
    zoneLabel.replace("ż", "Ż");
    zoneLabel.replace("ź", "Ź");
    zoneLabel.toUpperCase();
    drawCenteredText(zoneLabel, 160 + xOffset, 214, &SFNS12pt7b, kWarn);
    return;
  }

  String right = "Gotowy";
  uint16_t rightColor = kWarn;
  if (activeCount == 1) {
    String activeZoneName = zones_ ? zones_->getZoneName(activeZone) : "";
    activeZoneName.trim();
    right = activeZoneName.length() ? activeZoneName : ("Strefa " + String(activeZone + 1));
    rightColor = kGood;
  } else if (activeCount > 1) {
    right = String(activeCount) + " sekcje";
    rightColor = kGood;
  }

  drawLeftText("Stan:", 28 + xOffset, 218, &SFNS12pt7b, kTextMain);
  drawLeftText(right, 108 + xOffset, 218, &SFNS12pt7b, rightColor);
}

void TftPanelUI::drawHomeTileWithFocusMargin(int xOffset, int zoneIdx, bool focused) {
  const int col = zoneIdx % 4;
  const int row = zoneIdx / 4;
  const int x = kTileStartX + xOffset + col * (kTileW + kTileGapX);
  const int y = kTileStartY + row * (kTileH + kTileGapY);
  tft_.fillRect(x - 4, y - 4, kTileW + 8, kTileH + 8, kBg);
  const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);
  drawHomeTile(x, y, zoneIdx, focused, focusColor);
  if (focused) {
    tft_.drawRoundRect(x - 2, y - 2, kTileW + 4, kTileH + 4, 11, focusColor);
    tft_.drawRoundRect(x - 3, y - 3, kTileW + 6, kTileH + 6, 12, blend565(kStrokeStrong, kTextMain, 72));
  }
}

bool TftPanelUI::refreshHomeLive(int xOffset) {
  const uint32_t sig = buildHomeStateSignature();
  const int n = zoneCount();
  if (n > 32) {
    drawHome(xOffset);
    return true;
  }
  const uint32_t mask = buildHomeActiveMask();
  if (!homeSnapshotValid_ || n != lastHomeZoneCount_) {
    drawHome(xOffset);
    return true;
  }
  if (mask == lastHomeActiveMask_) {
    lastHomeStateSignature_ = sig;
    return false;
  }

  bool changed = false;
  for (int i = 0; i < n; ++i) {
    const bool prevActive = (lastHomeActiveMask_ & (1UL << i)) != 0;
    const bool nowActive = (mask & (1UL << i)) != 0;
    if (prevActive == nowActive) continue;
    const bool focused = (uiMode_ != UiMode::CardSelect) && (i == selectedZone_);
    drawHomeTileWithFocusMargin(xOffset, i, focused);
    changed = true;
  }
  if (changed) {
    drawHomeStatusBar(xOffset);
  }
  lastHomeActiveMask_ = mask;
  lastHomeZoneCount_ = n;
  homeSnapshotValid_ = true;
  lastHomeStateSignature_ = sig;
  return changed;
}

void TftPanelUI::drawWeather(int xOffset) {
  drawCenteredText("Pogoda", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);

  String temp, hum, wind, rain24, iconCode;
  buildWeatherDisplayValues(temp, hum, wind, rain24, iconCode);

  drawStatusRow(xOffset, 72, "Temp", temp, rgb565(109, 214, 255));
  drawStatusRow(xOffset, 116, "Wilgotnosc", hum, kTextMain);
  drawStatusRow(xOffset, 160, "Wiatr", wind, kTextMain);
  drawStatusRow(xOffset, 204, "Deszcz 24h", rain24, kTextMain);
  // Ikona warunków pogodowych - prawa strona pierwszego wiersza (jak w cloud).
  drawWeatherIcon(262 + xOffset, 74, 30, iconCode);

  weatherSnapshotValid_ = true;
  lastWeatherTempText_ = temp;
  lastWeatherHumText_ = hum;
  lastWeatherWindText_ = wind;
  lastWeatherRainText_ = rain24;
  lastWeatherIconCode_ = iconCode;
  lastWeatherStateSignature_ = buildWeatherStateSignature();
}

void TftPanelUI::buildWeatherDisplayValues(String& temp, String& hum, String& wind, String& rain24, String& iconCode) const {
  const bool weatherConfigured =
    config_ &&
    config_->getEnableWeatherApi() &&
    config_->getOwmApiKey().length() > 0 &&
    config_->getOwmLocation().length() > 0;

  temp = "-";
  hum = "-";
  wind = "-";
  rain24 = "-";
  iconCode = "";
  if (!weatherConfigured || !weather_) return;

  temp = String(weather_->getCurrentTemp(), 1) + " C";
  hum = String((int)weather_->getCurrentHumidity()) + " %";
  wind = String(weather_->getCurrentWindKmh(), 1) + " km/h";
  rain24 = String(weather_->getLast24hRain(), 1) + " mm";
  iconCode = weather_->getCurrentIconCode();
  iconCode.trim();
  iconCode.toLowerCase();

  // Fallback: jeśli kod ikony jest pusty, ale mamy opad, pokazuj deszcz.
  if (iconCode.length() == 0 && weather_->getLast24hRain() > 0.1f) {
    iconCode = "10d";
  }
}

bool TftPanelUI::refreshWeatherLive(int xOffset) {
  const uint32_t sig = buildWeatherStateSignature();
  if (weatherSnapshotValid_ && sig == lastWeatherStateSignature_) return false;

  String temp, hum, wind, rain24, iconCode;
  buildWeatherDisplayValues(temp, hum, wind, rain24, iconCode);

  if (!weatherSnapshotValid_) {
    drawWeather(xOffset);
    return true;
  }

  bool changed = false;
  const bool tempRowChanged = (temp != lastWeatherTempText_) || (iconCode != lastWeatherIconCode_);
  if (tempRowChanged) {
    drawStatusRow(xOffset, 72, "Temp", temp, rgb565(109, 214, 255));
    drawWeatherIcon(262 + xOffset, 74, 30, iconCode);
    changed = true;
  }
  if (hum != lastWeatherHumText_) {
    drawStatusRow(xOffset, 116, "Wilgotnosc", hum, kTextMain);
    changed = true;
  }
  if (wind != lastWeatherWindText_) {
    drawStatusRow(xOffset, 160, "Wiatr", wind, kTextMain);
    changed = true;
  }
  if (rain24 != lastWeatherRainText_) {
    drawStatusRow(xOffset, 204, "Deszcz 24h", rain24, kTextMain);
    changed = true;
  }

  if (!changed) {
    lastWeatherStateSignature_ = sig;
    return false;
  }

  lastWeatherTempText_ = temp;
  lastWeatherHumText_ = hum;
  lastWeatherWindText_ = wind;
  lastWeatherRainText_ = rain24;
  lastWeatherIconCode_ = iconCode;
  lastWeatherStateSignature_ = sig;
  return true;
}

void TftPanelUI::drawManual(int xOffset) {
  drawCenteredText("Podlewanie Ręczne", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);

  const int x = 16 + xOffset;
  const int w = 288;
  const int sectionBoxY = 74;
  const int sectionBoxH = 44;

  tft_.fillRoundRect(x, sectionBoxY, w, sectionBoxH, 10, kCard);
  tft_.drawRoundRect(x, sectionBoxY, w, sectionBoxH, 10, kStroke);
  drawLeftText("Sekcja", 28 + xOffset, 102, &SFNS9pt7b, kTextMuted);
  drawRightText(String(manualZone_ + 1), x + w - 20, 106, &SFNS18pt7b, kTextMain);
  drawManualLiveSection(xOffset);
}

void TftPanelUI::drawManualLiveSection(int xOffset) {
  const int x = 16 + xOffset;
  const int w = 288;
  const bool active = zones_ && zones_->getZoneState(manualZone_);

  tft_.fillRoundRect(x, 120, w, 44, 10, kCard);
  tft_.drawRoundRect(x, 120, w, 44, 10, kStroke);
  if (active && zones_) {
    const int rem = zones_->getRemainingSeconds(manualZone_);
    drawLeftText("Pozostało", 28 + xOffset, 148, &SFNS9pt7b, kTextMuted);
    drawManualRemainingValue(xOffset, rem);
    lastManualRemainingSec_ = rem;
  } else {
    drawLeftText("Czas", 28 + xOffset, 148, &SFNS9pt7b, kTextMuted);
    drawLeftText(String(manualDurationMin_) + " min", 124 + xOffset, 152, &SFNS12pt7b, kTextMain);
    lastManualRemainingSec_ = -1;
  }

  uint16_t btnFill = active ? rgb565(116, 27, 41) : rgb565(34, 139, 34);  // START: #228B22
  if (uiMode_ != UiMode::CardSelect && manualField_ == ManualField::Action) btnFill = blend565(btnFill, kTextMain, 40);
  const int btnY = 172;
  const int btnH = 52;
  tft_.fillRoundRect(x, btnY, w, btnH, 10, btnFill);
  tft_.drawRoundRect(x, btnY, w, btnH, 10, kStroke);
  drawCenteredText(active ? "STOP" : "START", 160 + xOffset, btnY + (btnH / 2), &SFNS18pt7b, kTextMain);

  if (uiMode_ != UiMode::CardSelect) {
    const int focusY = manualFocusY_;
    const int focusH = (focusY > 160) ? 52 : 44;
    const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);
    tft_.drawRoundRect(x, focusY, w, focusH, 10, focusColor);
    tft_.drawRoundRect(x + 1, focusY + 1, w - 2, focusH - 2, 9, blend565(kStrokeStrong, kTextMain, 72));
  }
  lastManualZoneActive_ = active;
}

void TftPanelUI::drawManualRemainingValue(int xOffset, int remainingSec) {
  const int valueClearX = 116 + xOffset;
  const int valueClearY = 129;
  const int valueClearW = 176;
  const int valueClearH = 28;
  tft_.fillRect(valueClearX, valueClearY, valueClearW, valueClearH, kCard);

  const int mm = remainingSec / 60;
  const int ss = remainingSec % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);
  drawLeftText(String(buf), 124 + xOffset, 152, &SFNS12pt7b, kTextMain);
}

void TftPanelUI::drawStatus(int xOffset) {
  drawCenteredText("Status Systemu", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);

  drawStatusFieldRow(xOffset, StatusField::WiFi);
  drawStatusFieldRow(xOffset, StatusField::Cloud);
  drawStatusFieldRow(xOffset, StatusField::Temp);
  drawStatusFieldRow(xOffset, StatusField::Watering);
  if (uiMode_ != UiMode::CardSelect) drawStatusFocusBox(xOffset, statusField_);

  if (uiMode_ == UiMode::Modal) drawWateringInfoModal(xOffset);
  String wifiState, cloudState, tempText, wateringText;
  buildStatusDisplayValues(wifiState, cloudState, tempText, wateringText);
  statusSnapshotValid_ = true;
  lastStatusWifiText_ = wifiState;
  lastStatusCloudText_ = cloudState;
  lastStatusTempText_ = tempText;
  lastStatusWateringText_ = wateringText;
  lastStatusWiFiEditMode_ = (uiMode_ == UiMode::ValueEdit && statusField_ == StatusField::WiFi);
  lastStatusFocusVisible_ = (uiMode_ != UiMode::CardSelect);
  lastStatusFocusField_ = statusField_;
  lastStatusWifiActionSel_ = statusWifiAction_;
  lastStatusStateSignature_ = buildStatusStateSignature();
}

void TftPanelUI::buildStatusDisplayValues(String& wifiState, String& cloudState, String& tempText, String& wateringText) const {
  const bool wifiOk = (WiFi.status() == WL_CONNECTED) && !(config_ && config_->isInAPMode());
  const bool wifiManualOff = config_ && config_->isWiFiManuallyDisconnected();
  wifiState = wifiOk ? "Połączono" : (config_ && config_->isInAPMode() ? "Tryb AP" : "Offline");
  if (!wifiOk && config_ && !config_->isWiFiConfigured()) wifiState = "Brak danych";
  if (wifiManualOff) wifiState = "Rozłączono";

  cloudState = "Wyłączone";
  if (mqtt_ && mqtt_->isConfigured()) cloudState = mqtt_->isConnected() ? "Połączono" : "Offline";

  const float chipTemp = temperatureRead();
  tempText = isfinite(chipTemp) ? (String(chipTemp, 1) + " C") : String("-");

  const int wateringPct = weather_ ? weather_->getWateringPercent() : 0;
  wateringText = String(wateringPct) + " %";
}

bool TftPanelUI::refreshStatusLive(int xOffset) {
  const uint32_t sig = buildStatusStateSignature();
  if (statusSnapshotValid_ && sig == lastStatusStateSignature_) return false;

  String wifiState, cloudState, tempText, wateringText;
  buildStatusDisplayValues(wifiState, cloudState, tempText, wateringText);

  if (!statusSnapshotValid_) {
    drawStatus(xOffset);
    return true;
  }

  bool rowNeeds[4] = {false, false, false, false};
  const bool wifiEditMode = (uiMode_ == UiMode::ValueEdit && statusField_ == StatusField::WiFi);
  const bool focusVisible = (uiMode_ != UiMode::CardSelect);
  const StatusField focusField = statusField_;
  const bool focusTransition =
    (focusVisible != lastStatusFocusVisible_) ||
    (focusVisible && lastStatusFocusField_ != focusField);

  if (wifiState != lastStatusWifiText_) rowNeeds[(int)StatusField::WiFi] = true;
  if (cloudState != lastStatusCloudText_) rowNeeds[(int)StatusField::Cloud] = true;
  if (tempText != lastStatusTempText_) rowNeeds[(int)StatusField::Temp] = true;
  if (wateringText != lastStatusWateringText_) rowNeeds[(int)StatusField::Watering] = true;

  if (wifiEditMode != lastStatusWiFiEditMode_) rowNeeds[(int)StatusField::WiFi] = true;
  if (wifiEditMode && statusWifiAction_ != lastStatusWifiActionSel_) rowNeeds[(int)StatusField::WiFi] = true;

  if (lastStatusFocusVisible_ && (!focusVisible || lastStatusFocusField_ != focusField)) {
    rowNeeds[(int)lastStatusFocusField_] = true;
  }

  bool changed = false;
  for (int i = 0; i < 4; ++i) {
    if (!rowNeeds[i]) continue;
    drawStatusFieldRow(xOffset, (StatusField)i);
    changed = true;
  }

  const bool focusedRowRedrawn = focusVisible && rowNeeds[(int)focusField];
  if (focusVisible && (focusTransition || focusedRowRedrawn)) {
    drawStatusFocusBox(xOffset, focusField);
    changed = true;
  }

  lastStatusWifiText_ = wifiState;
  lastStatusCloudText_ = cloudState;
  lastStatusTempText_ = tempText;
  lastStatusWateringText_ = wateringText;
  lastStatusWiFiEditMode_ = wifiEditMode;
  lastStatusFocusVisible_ = focusVisible;
  lastStatusFocusField_ = focusField;
  lastStatusWifiActionSel_ = statusWifiAction_;
  lastStatusStateSignature_ = sig;
  return changed;
}

void TftPanelUI::drawWateringInfoModal(int xOffset) {
  drawWateringInfoModalFrame(xOffset);
  drawWateringInfoModalText(xOffset);
  drawWateringInfoModalScrollbar(xOffset, true);
}

void TftPanelUI::drawWateringInfoModalFrame(int xOffset) {
  const int modalX = kModalX + xOffset;
  const uint16_t modalBg = rgb565(4, 14, 30);
  const uint16_t modalStroke = blend565(kStrokeStrong, kTextMain, 72);
  tft_.fillRoundRect(modalX, kModalY, kModalW, kModalH, 12, modalBg);
  tft_.drawRoundRect(modalX, kModalY, kModalW, kModalH, 12, modalStroke);

  const int pct = weather_ ? weather_->getWateringPercent() : 100;
  drawLeftText("Wyjaśnienie Nawadniania", modalX + 12, kModalY + 22, &SFNS9pt7b, kTextMain);
  drawRightText(String(pct) + "%", modalX + kModalW - 12, kModalY + 22, &SFNS12pt7b, kStrokeStrong);
}

void TftPanelUI::drawWateringInfoModalText(int xOffset) {
  const int textX = kModalTextX + xOffset;
  const uint16_t modalInner = rgb565(6, 20, 38);
  tft_.fillRoundRect(textX, kModalTextY, kModalTextW, kModalTextH, 8, modalInner);
  tft_.drawRoundRect(textX, kModalTextY, kModalTextW, kModalTextH, 8, kStroke);

  for (int i = 0; i < modalLineCount_; ++i) {
    const int baselineY = kModalTextY + 16 + i * kModalLinePx - modalScrollPx_;
    if (baselineY < (kModalTextY + 12) || baselineY > (kModalTextY + kModalTextH - 2)) continue;
    drawLeftText(modalLines_[i], textX + 8, baselineY, &SFNS9pt7b, kTextMain);
  }
}

void TftPanelUI::drawWateringInfoModalScrollbar(int xOffset, bool clearTrack) {
  const uint16_t modalInner = rgb565(6, 20, 38);
  const int trackX = kModalTrackX + xOffset;
  if (clearTrack) {
    tft_.fillRect(trackX, kModalTrackY, kModalTrackW, kModalTrackH, modalInner);
  }
  if (modalMaxScrollPx_ <= 0 || modalScrollbarAlpha_ == 0) return;

  const uint16_t trackColor = blend565(modalInner, rgb565(10, 32, 54), modalScrollbarAlpha_);
  const uint16_t thumbColor = blend565(modalInner, kStrokeStrong, modalScrollbarAlpha_);
  tft_.fillRoundRect(trackX, kModalTrackY, kModalTrackW, kModalTrackH, 2, trackColor);

  const int contentH = max(kModalVisibleTextPx + modalMaxScrollPx_, 1);
  int thumbH = (kModalTrackH * kModalVisibleTextPx) / contentH;
  if (thumbH < 18) thumbH = 18;
  if (thumbH > kModalTrackH) thumbH = kModalTrackH;
  const int travel = kModalTrackH - thumbH;
  const int thumbY = kModalTrackY + ((travel * modalScrollPx_) / max(modalMaxScrollPx_, 1));
  tft_.fillRoundRect(trackX, thumbY, kModalTrackW, thumbH, 2, thumbColor);
}

void TftPanelUI::drawSettingsBrightnessDynamic(int xOffset) {
  // Czyścimy tylko pole z procentem i obszar suwaka, żeby uniknąć pełnego redraw karty.
  const int percentClearX = 212 + xOffset;
  const int percentClearY = 80;
  const int percentClearW = 88;
  const int percentClearH = 24;
  tft_.fillRect(percentClearX, percentClearY, percentClearW, percentClearH, kCard);
  drawLeftText(String(brightnessPercent_) + "%", 234 + xOffset, 98, &SFNS12pt7b, kTextMain);

  const int sliderX = kSettingsSliderX + xOffset;
  const int sliderClearX = sliderX - (kSettingsSliderKnobR + 1);
  const int sliderClearY = kSettingsSliderY - (kSettingsSliderKnobR + 1);
  const int sliderClearW = kSettingsSliderW + (kSettingsSliderKnobR + 1) * 2;
  const int sliderClearH = kSettingsSliderH + (kSettingsSliderKnobR + 1) * 2;
  tft_.fillRect(sliderClearX, sliderClearY, sliderClearW, sliderClearH, kCard);

  tft_.fillRoundRect(sliderX, kSettingsSliderY, kSettingsSliderW, kSettingsSliderH, 4, rgb565(13, 36, 58));
  const int fillW = (kSettingsSliderW * brightnessPercent_) / 100;
  if (fillW > 0) {
    const int drawW = (fillW < 4) ? 4 : fillW;
    tft_.fillRoundRect(sliderX, kSettingsSliderY, drawW, kSettingsSliderH, 4, kStrokeStrong);
  }
  int knobX = sliderX + fillW;
  if (knobX < sliderX) knobX = sliderX;
  if (knobX > sliderX + kSettingsSliderW) knobX = sliderX + kSettingsSliderW;
  tft_.fillCircle(knobX, kSettingsSliderY + kSettingsSliderH / 2, kSettingsSliderKnobR, blend565(kStrokeStrong, kTextMain, 90));
  tft_.drawCircle(knobX, kSettingsSliderY + kSettingsSliderH / 2, kSettingsSliderKnobR, kTextMain);
}

void TftPanelUI::drawSettingsBrightnessRow(int xOffset) {
  const int x = 16 + xOffset;
  const int w = 288;
  const int brightY = 74;
  const int brightH = 54;
  tft_.fillRoundRect(x, brightY, w, brightH, 10, kCard);
  tft_.drawRoundRect(x, brightY, w, brightH, 10, kStroke);
  drawLeftText("Jasność", 28 + xOffset, 98, &SFNS9pt7b, kTextMuted);
  drawSettingsBrightnessDynamic(xOffset);
}

void TftPanelUI::drawSettingsWakeRow(int xOffset) {
  const int x = 16 + xOffset;
  const int w = 288;
  const int wakeY = 134;
  const int wakeH = 56;
  tft_.fillRoundRect(x, wakeY, w, wakeH, 10, kCard);
  tft_.drawRoundRect(x, wakeY, w, wakeH, 10, kStroke);
  drawLeftText("Auto przygaszenie", 28 + xOffset, 157, &SFNS9pt7b, kTextMuted);

  const int optW = 72;
  const int optH = 22;
  const int optGap = 10;
  const int totalW = optW * 3 + optGap * 2;
  const int baseX = x + ((w - totalW) / 2);
  const int optY = 160;
  for (int i = 0; i < 3; ++i) {
    const int bx = baseX + i * (optW + optGap);
    const bool sel = (i == dimTimeoutIndex_);
    const uint16_t fill = sel ? blend565(kStrokeStrong, kCard, 80) : rgb565(10, 28, 44);
    const uint16_t stroke = sel ? kStrokeStrong : kStroke;
    tft_.fillRoundRect(bx, optY, optW, optH, 6, fill);
    tft_.drawRoundRect(bx, optY, optW, optH, 6, stroke);
    drawCenteredText(String(kDimTimeoutOptionsMin[i]) + " min", bx + (optW / 2), optY + (optH / 2), &SFNS9pt7b, kTextMain);
  }
}

void TftPanelUI::drawSettingsFocusBox(int xOffset, SettingsField field) {
  if (uiMode_ == UiMode::CardSelect) return;
  const int x = 16 + xOffset;
  const int w = 288;
  const int focusY = settingsFieldTopY(field);
  const int focusH = settingsFieldHeight(field);
  const uint16_t focusColor = blend565(kStrokeStrong, kTextMain, 96);
  tft_.drawRoundRect(x, focusY, w, focusH, 10, focusColor);
  tft_.drawRoundRect(x + 1, focusY + 1, w - 2, focusH - 2, 9, blend565(kStrokeStrong, kTextMain, 72));
}

void TftPanelUI::drawSettings(int xOffset) {
  drawCenteredText("Ustawienia", 160 + xOffset, 64, &SFNS9pt7b, kTextMuted);

  drawSettingsBrightnessRow(xOffset);
  drawSettingsWakeRow(xOffset);
  drawSettingsFocusBox(xOffset, settingsField_);
  settingsSnapshotValid_ = true;
  lastSettingsBrightnessPct_ = brightnessPercent_;
  lastSettingsDimTimeoutIdx_ = dimTimeoutIndex_;
  lastSettingsFocusVisible_ = (uiMode_ != UiMode::CardSelect);
  lastSettingsFocusField_ = settingsField_;
  lastSettingsStateSignature_ = buildSettingsStateSignature();
}

bool TftPanelUI::refreshSettingsLive(int xOffset) {
  const uint32_t sig = buildSettingsStateSignature();
  if (settingsSnapshotValid_ && sig == lastSettingsStateSignature_) return false;

  if (!settingsSnapshotValid_) {
    drawSettings(xOffset);
    return true;
  }

  const bool focusVisible = (uiMode_ != UiMode::CardSelect);
  const SettingsField focusField = settingsField_;
  const bool focusTransition =
    (focusVisible != lastSettingsFocusVisible_) ||
    (focusVisible && lastSettingsFocusField_ != focusField);

  bool redrawBrightnessRow = false;
  bool redrawWakeRow = false;
  bool redrawBrightnessDynamic = false;

  if (brightnessPercent_ != lastSettingsBrightnessPct_) redrawBrightnessDynamic = true;
  if (dimTimeoutIndex_ != lastSettingsDimTimeoutIdx_) redrawWakeRow = true;

  if (lastSettingsFocusVisible_ && (!focusVisible || lastSettingsFocusField_ != focusField)) {
    if (lastSettingsFocusField_ == SettingsField::Brightness) redrawBrightnessRow = true;
    else redrawWakeRow = true;
  }

  bool changed = false;
  if (redrawBrightnessRow) {
    drawSettingsBrightnessRow(xOffset);
    changed = true;
  } else if (redrawBrightnessDynamic) {
    drawSettingsBrightnessDynamic(xOffset);
    changed = true;
  }

  if (redrawWakeRow) {
    drawSettingsWakeRow(xOffset);
    changed = true;
  }

  const bool focusedRowRedrawn =
    focusVisible &&
    ((focusField == SettingsField::Brightness && redrawBrightnessRow) ||
     (focusField == SettingsField::WakeTimeout && redrawWakeRow));
  if (focusVisible && (focusTransition || focusedRowRedrawn)) {
    drawSettingsFocusBox(xOffset, focusField);
    changed = true;
  }

  lastSettingsBrightnessPct_ = brightnessPercent_;
  lastSettingsDimTimeoutIdx_ = dimTimeoutIndex_;
  lastSettingsFocusVisible_ = focusVisible;
  lastSettingsFocusField_ = focusField;
  lastSettingsStateSignature_ = sig;
  settingsSnapshotValid_ = true;
  return changed;
}

bool TftPanelUI::loadLogoFromBmp(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  uint8_t header[138];
  const size_t got = f.read(header, sizeof(header));
  if (got < 54) return false;
  if (header[0] != 'B' || header[1] != 'M') return false;

  const uint32_t dataOffset = le32(&header[10]);
  const uint32_t dibSize = le32(&header[14]);
  const int32_t width = sle32(&header[18]);
  const int32_t height = sle32(&header[22]);
  const uint16_t planes = le16(&header[26]);
  const uint16_t bpp = le16(&header[28]);
  const uint32_t compression = le32(&header[30]);

  if (dibSize < 40 || planes != 1 || width != kLogoW) return false;
  if (height != kLogoH && height != -kLogoH) return false;
  if (bpp != 24 && bpp != 32) return false;
  if (compression != 0 && compression != 3) return false;

  const bool topDown = (height < 0);
  const uint32_t rowBytes = ((uint32_t)(kLogoW * bpp + 31U) / 32U) * 4U;
  uint8_t row[128];
  if (rowBytes > sizeof(row)) return false;

  for (int y = 0; y < kLogoH; ++y) {
    const int srcY = topDown ? y : (kLogoH - 1 - y);
    const uint32_t rowPos = dataOffset + (uint32_t)srcY * rowBytes;
    if (!f.seek(rowPos, SeekSet)) return false;
    if (f.read(row, rowBytes) != (int)rowBytes) return false;

    for (int x = 0; x < kLogoW; ++x) {
      const int idx = y * kLogoW + x;
      const int px = x * (bpp / 8);
      const uint8_t b = row[px + 0];
      const uint8_t g = row[px + 1];
      const uint8_t r = row[px + 2];
      const uint8_t a = (bpp == 32) ? row[px + 3] : 255;
      logo565_[idx] = rgb565(r, g, b);
      logoAlpha_[idx] = a;
    }
  }
  return true;
}

bool TftPanelUI::drawBmpFromFs(const char* path, int x, int y, bool blendWithDimBg, uint16_t fallbackBg) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  uint8_t header[138];
  const size_t got = f.read(header, sizeof(header));
  if (got < 54) return false;
  if (header[0] != 'B' || header[1] != 'M') return false;

  const uint32_t dataOffset = le32(&header[10]);
  const uint32_t dibSize = le32(&header[14]);
  const int32_t width = sle32(&header[18]);
  const int32_t height = sle32(&header[22]);
  const uint16_t planes = le16(&header[26]);
  const uint16_t bpp = le16(&header[28]);
  const uint32_t compression = le32(&header[30]);
  if (dibSize < 40 || planes != 1) return false;
  if (width <= 0) return false;
  if (height == 0) return false;
  if (bpp != 24 && bpp != 32) return false;
  if (compression != 0 && compression != 3) return false;

  const int hAbs = (height < 0) ? -height : height;
  const bool topDown = (height < 0);
  const uint32_t rowBytes = ((uint32_t)(width * bpp + 31U) / 32U) * 4U;
  if (rowBytes > 2048) return false;

  uint8_t* row = (uint8_t*)malloc(rowBytes);
  if (!row) return false;

  bool ok = true;
  const int yyStart = max(0, -y);
  const int yyEnd = min(hAbs, kDisplayHeight - y);
  const int xxStart = max(0, -x);
  const int xxEnd = min((int)width, kDisplayWidth - x);
  if (yyStart >= yyEnd || xxStart >= xxEnd) {
    free(row);
    return true;
  }

  for (int yy = yyStart; yy < yyEnd && ok; ++yy) {
    const int srcY = topDown ? yy : (hAbs - 1 - yy);
    const uint32_t rowPos = dataOffset + (uint32_t)srcY * rowBytes;
    if (!f.seek(rowPos, SeekSet)) {
      ok = false;
      break;
    }
    if (f.read(row, rowBytes) != (int)rowBytes) {
      ok = false;
      break;
    }

    const int dstY = y + yy;
    for (int xx = xxStart; xx < xxEnd; ++xx) {
      const int dstX = x + xx;
      const int px = xx * (bpp / 8);
      const uint8_t b = row[px + 0];
      const uint8_t g = row[px + 1];
      const uint8_t r = row[px + 2];
      const uint8_t a = (bpp == 32) ? row[px + 3] : 255;
      if (a < 8) continue;

      uint16_t out = rgb565(r, g, b);
      if (a < 248) {
        const uint16_t bg = blendWithDimBg ? dimBgColorAtY(dstY) : fallbackBg;
        out = blend565(bg, out, a);
      }
      tft_.drawPixel(dstX, dstY, out);
    }
    delay(0);
  }

  free(row);
  return ok;
}

bool TftPanelUI::drawBmpFromFsScaled(const char* path, int x, int y, int outW, int outH, bool blendWithDimBg, uint16_t fallbackBg) {
  if (outW <= 0 || outH <= 0) return false;

  File f = LittleFS.open(path, "r");
  if (!f) return false;

  uint8_t header[138];
  const size_t got = f.read(header, sizeof(header));
  if (got < 54) return false;
  if (header[0] != 'B' || header[1] != 'M') return false;

  const uint32_t dataOffset = le32(&header[10]);
  const uint32_t dibSize = le32(&header[14]);
  const int32_t width = sle32(&header[18]);
  const int32_t height = sle32(&header[22]);
  const uint16_t planes = le16(&header[26]);
  const uint16_t bpp = le16(&header[28]);
  const uint32_t compression = le32(&header[30]);
  if (dibSize < 40 || planes != 1) return false;
  if (width <= 0) return false;
  if (height == 0) return false;
  if (bpp != 24 && bpp != 32) return false;
  if (compression != 0 && compression != 3) return false;

  const int hAbs = (height < 0) ? -height : height;
  const bool topDown = (height < 0);
  const uint32_t rowBytes = ((uint32_t)(width * bpp + 31U) / 32U) * 4U;
  if (rowBytes > 4096) return false;

  uint8_t* row = (uint8_t*)malloc(rowBytes);
  if (!row) return false;

  bool ok = true;
  int lastFileRow = -1;

  const int yyStart = max(0, -y);
  const int yyEnd = min(outH, kDisplayHeight - y);
  const int xxStart = max(0, -x);
  const int xxEnd = min(outW, kDisplayWidth - x);
  if (yyStart >= yyEnd || xxStart >= xxEnd) {
    free(row);
    return true;
  }

  for (int yy = yyStart; yy < yyEnd && ok; ++yy) {
    const int srcY = (yy * hAbs) / outH;
    const int fileRow = topDown ? srcY : (hAbs - 1 - srcY);

    if (fileRow != lastFileRow) {
      const uint32_t rowPos = dataOffset + (uint32_t)fileRow * rowBytes;
      if (!f.seek(rowPos, SeekSet)) {
        ok = false;
        break;
      }
      if (f.read(row, rowBytes) != (int)rowBytes) {
        ok = false;
        break;
      }
      lastFileRow = fileRow;
    }

    const int dstY = y + yy;
    for (int xx = xxStart; xx < xxEnd; ++xx) {
      const int dstX = x + xx;
      const int srcX = (xx * width) / outW;
      const int px = srcX * (bpp / 8);
      const uint8_t b = row[px + 0];
      const uint8_t g = row[px + 1];
      const uint8_t r = row[px + 2];
      const uint8_t a = (bpp == 32) ? row[px + 3] : 255;
      if (a < 8) continue;

      uint16_t out = rgb565(r, g, b);
      if (a < 248) {
        const uint16_t bg = blendWithDimBg ? dimBgColorAtY(dstY) : fallbackBg;
        out = blend565(bg, out, a);
      }
      tft_.drawPixel(dstX, dstY, out);
    }
    delay(0);
  }

  free(row);
  return ok;
}

uint16_t TftPanelUI::topBarColorAtY(int y) const {
  if (y < 0) y = 0;
  if (y >= kTopBarHeight) y = kTopBarHeight - 1;
  const uint8_t t = (uint8_t)((y * 255) / (kTopBarHeight - 1));
  return blend565(kTopBarFrom, kTopBarTo, t);
}

uint16_t TftPanelUI::dimBgColorAtY(int y) const {
  (void)y;
  return kDimBg;
}

void TftPanelUI::drawLogo(int x, int y, int outSize) {
  if (outSize <= 0) return;

  if (!logoLoadAttempted_) {
    logoLoadAttempted_ = true;
    logoLoaded_ = loadLogoFromBmp("/wm-logo-24.bmp");
  }

  if (logoLoaded_) {
    for (int yy = 0; yy < outSize; ++yy) {
      const int srcY = (yy * kLogoH) / outSize;
      const int dstY = y + yy;
      if (dstY < 0 || dstY >= kDisplayHeight) continue;
      for (int xx = 0; xx < outSize; ++xx) {
        const int dstX = x + xx;
        if (dstX < 0 || dstX >= kDisplayWidth) continue;
        const int srcX = (xx * kLogoW) / outSize;
        const int idx = srcY * kLogoW + srcX;
        const uint8_t a = logoAlpha_[idx];
        if (a < 8) continue;
        uint16_t out = logo565_[idx];
        if (a < 248) out = blend565(topBarColorAtY(dstY), out, a);
        tft_.drawPixel(dstX, dstY, out);
      }
    }
    return;
  }

  const uint16_t blue = rgb565(65, 187, 255);
  const uint16_t green = rgb565(72, 209, 137);
  auto sx = [outSize](int v) -> int { return (v * outSize) / 24; };
  const int rBlue = max(1, sx(7));
  const int rGreen = max(1, sx(6));
  const int rWhite = max(1, sx(2));
  tft_.fillCircle(x + sx(7), y + sx(15), rBlue, blue);
  tft_.fillTriangle(x + sx(7), y + sx(3), x + sx(1), y + sx(16), x + sx(13), y + sx(16), blue);
  tft_.fillCircle(x + sx(13), y + sx(18), rGreen, green);
  tft_.fillTriangle(x + sx(13), y + sx(8), x + sx(8), y + sx(19), x + sx(18), y + sx(19), green);
  tft_.fillCircle(x + sx(5), y + sx(12), rWhite, kTextMain);
}

uint32_t TftPanelUI::buildHomeStateSignature() const {
  uint32_t sig = 2166136261u;  // FNV-1a basis
  auto mix = [&sig](uint32_t v) {
    sig ^= v;
    sig *= 16777619u;
  };

  const int n = zoneCount();
  mix((uint32_t)n);
  for (int i = 0; i < n; ++i) {
    const bool active = zones_ && zones_->getZoneState(i);
    mix(active ? 1u : 0u);
  }
  return sig;
}

uint32_t TftPanelUI::buildHomeActiveMask() const {
  const int n = zoneCount();
  uint32_t mask = 0;
  for (int i = 0; i < n && i < 32; ++i) {
    if (zones_ && zones_->getZoneState(i)) mask |= (1UL << i);
  }
  return mask;
}

uint32_t TftPanelUI::buildStatusStateSignature() const {
  uint32_t sig = 2166136261u;  // FNV-1a basis
  auto mix = [&sig](uint32_t v) {
    sig ^= v;
    sig *= 16777619u;
  };

  mix((uint32_t)WiFi.status());
  mix((config_ && config_->isInAPMode()) ? 1u : 0u);
  mix((config_ && config_->isWiFiManuallyDisconnected()) ? 1u : 0u);
  mix((config_ && config_->isWiFiConfigured()) ? 1u : 0u);
  mix((mqtt_ && mqtt_->isConfigured()) ? 1u : 0u);
  mix((mqtt_ && mqtt_->isConnected()) ? 1u : 0u);
  mix((uint32_t)uiMode_);
  mix((uint32_t)statusField_);
  mix((uint32_t)statusWifiAction_);

  const float chipTemp = temperatureRead();
  if (isfinite(chipTemp)) {
    const int32_t t10 = (int32_t)lroundf(chipTemp * 10.0f);
    mix((uint32_t)(t10 + 2000));
  } else {
    mix(0xFFFFFFFFu);
  }

  const int wateringPct = weather_ ? weather_->getWateringPercent() : 0;
  mix((uint32_t)(wateringPct + 1000));

  return sig;
}

uint32_t TftPanelUI::buildWeatherStateSignature() const {
  uint32_t sig = 2166136261u;  // FNV-1a basis
  auto mix = [&sig](uint32_t v) {
    sig ^= v;
    sig *= 16777619u;
  };

  String temp, hum, wind, rain24, iconCode;
  buildWeatherDisplayValues(temp, hum, wind, rain24, iconCode);
  for (size_t i = 0; i < temp.length(); ++i) mix((uint8_t)temp[i]);
  for (size_t i = 0; i < hum.length(); ++i) mix((uint8_t)hum[i]);
  for (size_t i = 0; i < wind.length(); ++i) mix((uint8_t)wind[i]);
  for (size_t i = 0; i < rain24.length(); ++i) mix((uint8_t)rain24[i]);
  for (size_t i = 0; i < iconCode.length(); ++i) mix((uint8_t)iconCode[i]);

  return sig;
}

uint32_t TftPanelUI::buildSettingsStateSignature() const {
  uint32_t sig = 2166136261u;  // FNV-1a basis
  auto mix = [&sig](uint32_t v) {
    sig ^= v;
    sig *= 16777619u;
  };

  mix((uint32_t)brightnessPercent_);
  mix((uint32_t)dimTimeoutIndex_);
  mix((uint32_t)uiMode_);
  mix((uint32_t)settingsField_);
  return sig;
}

void TftPanelUI::drawWeatherIcon(int x, int y, int size, const String& iconCode) {
  if (size < 12) size = 12;
  const int cx = x + (size / 2);
  const int cy = y + (size / 2);

  String code = iconCode;
  code.trim();
  code.toLowerCase();
  int codeNum = -1;
  if (code.length() >= 2 && isdigit((unsigned char)code[0]) && isdigit((unsigned char)code[1])) {
    codeNum = (code[0] - '0') * 10 + (code[1] - '0');
  }
  const bool isNight = code.endsWith("n");

  auto drawCloudBase = [&]() {
    const uint16_t cloud = rgb565(178, 205, 226);
    const int baseY = y + (size * 2) / 3;
    const int cloudW = (size * 7) / 8;
    const int cloudX = x + (size - cloudW) / 2;
    tft_.fillRoundRect(cloudX, baseY - (size / 6), cloudW, size / 3, 5, cloud);
    tft_.fillCircle(cloudX + cloudW / 4, baseY - size / 6, size / 6, cloud);
    tft_.fillCircle(cloudX + cloudW / 2, baseY - size / 4, size / 5, cloud);
    tft_.fillCircle(cloudX + (cloudW * 3) / 4, baseY - size / 6, size / 6, cloud);
  };

  // 1: bezchmurnie
  if (codeNum == 1) {
    if (isNight) {
      const uint16_t moon = rgb565(213, 223, 240);
      const uint16_t cut = rgb565(12, 24, 44);
      const int r = size / 4;
      tft_.fillCircle(cx, cy, r, moon);
      tft_.fillCircle(cx + r / 2, cy - r / 3, r, cut);
      return;
    }
    const uint16_t sun = rgb565(252, 198, 61);
    const uint16_t sunCore = blend565(sun, kTextMain, 50);
    const int r = size / 4;
    tft_.fillCircle(cx, cy, r, sunCore);
    for (int i = 0; i < 8; ++i) {
      const float a = (float)i * (PI / 4.0f);
      const int x1 = cx + (int)((r + 2) * cosf(a));
      const int y1 = cy + (int)((r + 2) * sinf(a));
      const int x2 = cx + (int)((r + 7) * cosf(a));
      const int y2 = cy + (int)((r + 7) * sinf(a));
      tft_.drawLine(x1, y1, x2, y2, sun);
    }
    return;
  }

  // 2: małe zachmurzenie (słońce/księżyc + chmura)
  if (codeNum == 2) {
    if (isNight) {
      const uint16_t moon = rgb565(213, 223, 240);
      const uint16_t cut = rgb565(12, 24, 44);
      const int r = size / 5;
      tft_.fillCircle(x + size / 3, y + size / 3, r, moon);
      tft_.fillCircle(x + size / 3 + r / 2, y + size / 3 - r / 3, r, cut);
    } else {
      const uint16_t sun = rgb565(252, 198, 61);
      tft_.fillCircle(x + size / 3, y + size / 3, size / 6, sun);
    }
    drawCloudBase();
    return;
  }

  drawCloudBase();

  // 09/10: deszcz
  if (codeNum == 9 || codeNum == 10) {
    const uint16_t rain = rgb565(64, 180, 255);
    for (int i = 0; i < 3; ++i) {
      const int rx = x + (size / 4) + i * (size / 5);
      const int ry = y + (size * 3) / 4 + 1;
      tft_.drawLine(rx, ry - 4, rx - 1, ry + 2, rain);
      tft_.fillCircle(rx - 1, ry + 3, 2, rain);
    }
    return;
  }

  // 11: burza
  if (codeNum == 11) {
    const uint16_t bolt = rgb565(252, 198, 61);
    tft_.fillTriangle(cx - 2, y + size / 2, cx + 3, y + size / 2, cx - 1, y + size - 6, bolt);
    tft_.fillTriangle(cx + 1, y + size / 2 + 3, cx + 6, y + size / 2 + 3, cx + 2, y + size - 2, bolt);
    return;
  }

  // 13: śnieg
  if (codeNum == 13) {
    const uint16_t snow = rgb565(236, 246, 255);
    for (int i = 0; i < 3; ++i) {
      const int sx = x + (size / 4) + i * (size / 5);
      const int sy = y + (size * 4) / 5;
      tft_.drawLine(sx - 2, sy, sx + 2, sy, snow);
      tft_.drawLine(sx, sy - 2, sx, sy + 2, snow);
    }
    return;
  }

  // 50: mgła
  if (codeNum == 50) {
    const uint16_t mist = rgb565(160, 180, 196);
    tft_.drawFastHLine(x + 3, y + size / 2 + 1, size - 6, mist);
    tft_.drawFastHLine(x + 1, y + size / 2 + 5, size - 4, mist);
    tft_.drawFastHLine(x + 4, y + size / 2 + 9, size - 8, mist);
  }
}

void TftPanelUI::drawHomeTile(int x, int y, int zoneIdx, bool focused, uint16_t focusColor) {
  const bool active = zones_ && zones_->getZoneState(zoneIdx);
  uint16_t fill = active ? kCardAlt : kCard;
  uint16_t stroke = active ? kGood : kStroke;
  uint16_t text = active ? kGood : kTextMain;

  if (focused) {
    fill = blend565(fill, kCardAlt, 80);
    stroke = blend565(stroke, focusColor, 130);
  }

  tft_.fillRoundRect(x, y, kTileW, kTileH, 9, fill);
  tft_.drawRoundRect(x, y, kTileW, kTileH, 9, stroke);
  drawCenteredText(String(zoneIdx + 1), x + (kTileW / 2), y + (kTileH / 2), &SFNS18pt7b, text);
}

void TftPanelUI::drawStatusRow(int xOffset, int y, const String& label, const String& value, uint16_t valueColor) {
  const int x = 16 + xOffset;
  const int splitX = xOffset + (kDisplayWidth / 2);
  tft_.fillRoundRect(x, y, 288, 36, 10, kCard);
  tft_.drawRoundRect(x, y, 288, 36, 10, kStroke);
  drawLeftText(label + ":", 30 + xOffset, y + 24, &SFNS9pt7b, kTextMuted);
  drawLeftText(value, splitX + 8, y + 24, &SFNS9pt7b, valueColor);
}

void TftPanelUI::drawCenteredText(const String& text, int cx, int cy, const GFXfont* font, uint16_t color) {
  AccentMark accents[256];
  int count = 0;
  const String safe = toFontSafeText(text, accents, 256, count);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;

  tft_.setFont(font);
  tft_.getTextBounds(safe, 0, 0, &x1, &y1, &w, &h);
  const int x = cx - ((int)w / 2) - x1;
  const int y = cy - ((int)h / 2) - y1;
  tft_.setFont(nullptr);
  drawUtf8PlText(text, x, y, font, color);
}

void TftPanelUI::drawLeftText(const String& text, int x, int y, const GFXfont* font, uint16_t color) {
  drawUtf8PlText(text, x, y, font, color);
}

void TftPanelUI::drawRightText(const String& text, int rightX, int y, const GFXfont* font, uint16_t color) {
  AccentMark accents[256];
  int count = 0;
  const String safe = toFontSafeText(text, accents, 256, count);
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  (void)h;
  tft_.setFont(font);
  tft_.getTextBounds(safe, 0, 0, &x1, &y1, &w, &h);
  const int x = rightX - (int)w - x1;
  tft_.setFont(nullptr);
  drawUtf8PlText(text, x, y, font, color);
}
