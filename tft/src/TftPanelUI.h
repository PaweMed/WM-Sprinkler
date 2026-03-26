#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "Config.h"
#include "Logs.h"
#include "MQTTClient.h"
#include "RotaryInput.h"
#include "Weather.h"
#include "Zones.h"

class TftPanelUI {
public:
  TftPanelUI();

  void begin(Zones* zones, Weather* weather, Config* config, MQTTClient* mqtt, Logs* logs);
  void loop();

private:
  enum class Screen : uint8_t {
    Home,
    Weather,
    Manual,
    Status,
    Logs,
    Settings
  };

  enum class ManualField : uint8_t {
    Duration,
    Action
  };

  enum class StatusField : uint8_t {
    WiFi,
    Cloud,
    Temp,
    Watering
  };

  enum class SettingsField : uint8_t {
    Brightness,
    WakeTimeout
  };

  enum class UiMode : uint8_t {
    CardSelect,
    FieldSelect,
    ValueEdit,
    Modal
  };

  enum class AccentMark : uint8_t {
    None,
    Acute,
    Dot,
    Ogonek,
    Stroke
  };

  Adafruit_ST7789 tft_;
  RotaryInput encoder_;

  Zones* zones_ = nullptr;
  Weather* weather_ = nullptr;
  Config* config_ = nullptr;
  MQTTClient* mqtt_ = nullptr;
  Logs* logs_ = nullptr;

  Screen screen_ = Screen::Home;
  UiMode uiMode_ = UiMode::CardSelect;
  ManualField manualField_ = ManualField::Duration;
  StatusField statusField_ = StatusField::WiFi;
  SettingsField settingsField_ = SettingsField::Brightness;
  int selectedZone_ = 0;
  int manualZone_ = 0;
  int manualDurationMin_ = 15;
  int focusX_ = 12;
  int focusY_ = 76;
  int focusTargetX_ = 12;
  int focusTargetY_ = 76;
  bool focusInitialized_ = false;
  int manualFocusY_ = 120;
  int manualFocusTargetY_ = 120;
  int statusFocusY_ = 72;
  int statusFocusTargetY_ = 72;
  int settingsFocusY_ = 76;
  int settingsFocusTargetY_ = 76;
  int statusWifiAction_ = 0;  // 0=Polacz, 1=Rozlacz
  Screen previousScreen_ = Screen::Home;
  Screen lastRenderedScreen_ = Screen::Home;
  bool hasRenderedScreen_ = false;
  bool transitionActive_ = false;
  bool dimmed_ = false;
  unsigned long lastUserActivityMs_ = 0;
  int brightnessPercent_ = 85;
  int dimTimeoutIndex_ = 1;
  int backlightCurrentPercent_ = 85;
  bool backlightReady_ = false;
  bool settingsDirty_ = false;
  unsigned long settingsChangedMs_ = 0;
  bool pendingShortPress_ = false;
  unsigned long pendingShortPressMs_ = 0;
  int lastManualRemainingSec_ = -1;
  bool lastManualZoneActive_ = false;
  bool logoLoaded_ = false;
  bool logoLoadAttempted_ = false;
  bool topBarDirty_ = true;
  String lastTopBarClockText_;
  unsigned long lastTopBarClockCheckMs_ = 0;
  uint32_t lastHomeStateSignature_ = 0;
  uint32_t lastHomeActiveMask_ = 0;
  int lastHomeZoneCount_ = 0;
  bool homeSnapshotValid_ = false;
  uint32_t lastStatusStateSignature_ = 0;
  unsigned long lastStatusCheckMs_ = 0;
  bool statusSnapshotValid_ = false;
  String lastStatusWifiText_;
  String lastStatusCloudText_;
  String lastStatusTempText_;
  String lastStatusWateringText_;
  bool lastStatusWiFiEditMode_ = false;
  bool lastStatusFocusVisible_ = false;
  StatusField lastStatusFocusField_ = StatusField::WiFi;
  int lastStatusWifiActionSel_ = 0;
  uint32_t lastWeatherStateSignature_ = 0;
  unsigned long lastWeatherCheckMs_ = 0;
  bool logsSnapshotValid_ = false;
  uint32_t lastLogsRevision_ = 0;
  int lastLogsVisibleCount_ = 0;
  bool lastLogsFocusVisible_ = false;
  unsigned long lastLogsCheckMs_ = 0;
  uint32_t lastLogsMarqueeSignature_ = 0;
  static constexpr int kLogsSlotCacheSize = 6;
  int lastLogsSlotIndices_[kLogsSlotCacheSize] = {-1, -1, -1, -1, -1, -1};
  int lastLogsSlotShiftPx_[kLogsSlotCacheSize] = {0, 0, 0, 0, 0, 0};
  uint32_t lastLogsSlotTimeSig_[kLogsSlotCacheSize] = {0, 0, 0, 0, 0, 0};
  int logsScrollPx_ = 0;
  int logsMaxScrollPx_ = 0;
  int logsQueuedTurns_ = 0;
  uint8_t logsScrollbarAlpha_ = 0;
  unsigned long logsLastScrollMs_ = 0;
  unsigned long logsLastFadeMs_ = 0;
  unsigned long logsLastInteractionMs_ = 0;
  unsigned long logsLastStepMs_ = 0;
  uint32_t lastSettingsStateSignature_ = 0;
  unsigned long lastSettingsCheckMs_ = 0;
  bool settingsSnapshotValid_ = false;
  int lastSettingsBrightnessPct_ = 85;
  int lastSettingsDimTimeoutIdx_ = 1;
  bool lastSettingsFocusVisible_ = false;
  SettingsField lastSettingsFocusField_ = SettingsField::Brightness;
  static constexpr int kModalMaxLines = 48;
  String modalLines_[kModalMaxLines];
  int modalLineCount_ = 0;
  int modalScrollPx_ = 0;
  int modalMaxScrollPx_ = 0;
  uint8_t modalScrollbarAlpha_ = 0;
  unsigned long modalLastScrollMs_ = 0;
  unsigned long modalLastFadeMs_ = 0;
  bool weatherSnapshotValid_ = false;
  String lastWeatherTempText_;
  String lastWeatherHumText_;
  String lastWeatherWindText_;
  String lastWeatherRainText_;
  String lastWeatherIconCode_;
  static constexpr int kLogoW = 24;
  static constexpr int kLogoH = 24;
  uint16_t logo565_[kLogoW * kLogoH] = {};
  uint8_t logoAlpha_[kLogoW * kLogoH] = {};
  bool dirty_ = true;
  unsigned long lastFrameMs_ = 0;
  unsigned long bootScreensaverUntilMs_ = 0;
  bool dimmedWateringSnapshotActive_ = false;
  int dimmedWateringSnapshotZone_ = -1;
  int dimmedWateringSnapshotRemainingSec_ = -1;
  int dimmedWateringSnapshotTotalSec_ = 0;
  unsigned long lastDimmedWateringCheckMs_ = 0;

  void requestScreen(Screen next);
  void updateAnimations();
  void syncFocusTargets();
  void handleInput(int turns, bool shortPress, bool longPress, bool doublePress);
  int screenIndex(Screen s) const;
  Screen screenFromIndex(int idx) const;
  void ensureBacklightPwm();
  void setBacklightPercent(int percent);
  int selectedDimTimeoutMinutes() const;
  bool shouldKeepAwakeForEmergency() const;
  void loadUiSettings();
  void persistUiSettings(bool force);
  void enterDim();
  void wakeFromDim();
  void executeManualAction();
  void executeStatusWiFiAction();
  void syncDefaultStatusWifiAction();
  void openWateringInfoModal();
  void closeModal();
  void appendModalWrapped(const String& text, int maxChars = 34);
  String toFontSafeText(const String& text, AccentMark* accents, int maxAccents, int& outCount) const;
  int glyphAdvance(char c, const GFXfont* font) const;
  int textWidthFontSafe(const String& text, const GFXfont* font);
  void drawAccents(const String& safeText, const AccentMark* accents, int count, int x, int baseline, const GFXfont* font, uint16_t color);
  void drawUtf8PlText(const String& text, int x, int y, const GFXfont* font, uint16_t color);
  int measureTextWidth(const String& text, const GFXfont* font);
  void drawStatusFieldRow(int xOffset, StatusField field);
  void drawStatusFocusBox(int xOffset, StatusField field);
  void drawLogsFocusBox(int xOffset);
  bool applyLogsTurnStep(int xOffset, int step);
  bool processPendingLogsScroll(int xOffset, bool forceImmediate);
  void renderLogsRowToCanvas(int slotIndex, int logIndex, int msgShiftPx);
  void pushLogsCanvas(int xOffset, int slotIndex = -1);
  void drawLogsRowSlot(int xOffset, int slotIndex, int logIndex, int msgShiftPx);
  int logsMessageClipWidth(int xOffset) const;

  int zoneCount() const;
  int wrapZone(int idx) const;
  int statusFieldTopY(StatusField field) const;
  int statusFieldHeight(StatusField field) const;
  int settingsFieldTopY(SettingsField field) const;
  int settingsFieldHeight(SettingsField field) const;
  void draw();
  void drawScreen(Screen screen, int xOffset);
  void drawTopBar();
  bool drawTopBarClock(bool force);
  String buildTopBarClockText() const;
  void drawDimmedWallpaper();
  void drawDimmedWateringGauge(bool fullScreenClear, int zoneIdx, int remainingSec, int totalSec);
  void drawRingArc(int cx, int cy, int innerR, int outerR, int startDeg, int sweepDeg, uint16_t color);
  void getDimmedWateringState(bool& active, int& zoneIdx, int& remainingSec, int& totalSec) const;
  bool refreshDimmedWateringLive();
  void drawHome(int xOffset);
  void drawWeather(int xOffset);
  void drawManual(int xOffset);
  void drawStatus(int xOffset);
  void drawLogs(int xOffset);
  void drawHomeStatusBar(int xOffset);
  void drawHomeTileWithFocusMargin(int xOffset, int zoneIdx, bool focused);
  bool refreshHomeLive(int xOffset);
  void drawWateringInfoModal(int xOffset);
  void drawWateringInfoModalFrame(int xOffset);
  void drawWateringInfoModalText(int xOffset);
  void drawWateringInfoModalScrollbar(int xOffset, bool clearTrack);
  void drawSettingsBrightnessRow(int xOffset);
  void drawSettingsBrightnessDynamic(int xOffset);
  void drawSettingsWakeRow(int xOffset);
  void drawSettingsFocusBox(int xOffset, SettingsField field);
  void drawLogsPanelFrame(int xOffset);
  void drawLogsPanelText(int xOffset);
  void drawLogsScrollbar(int xOffset, bool clearTrack);
  void normalizeLogsScroll(bool preserveAnchor);
  String ellipsizeTextToWidth(const String& text, int maxWidth, const GFXfont* font);
  int logsVisibleRowCount() const;
  int logsRowMarqueeOffsetPx(const String& text, unsigned long nowMs, int maxWidth);
  void drawSettings(int xOffset);
  uint32_t buildHomeActiveMask() const;
  uint32_t buildHomeStateSignature() const;
  uint32_t buildStatusStateSignature() const;
  uint32_t buildWeatherStateSignature() const;
  uint32_t buildSettingsStateSignature() const;
  void buildStatusDisplayValues(String& wifiState, String& cloudState, String& tempText, String& wateringText) const;
  bool refreshStatusLive(int xOffset);
  bool refreshLogsLive(int xOffset);
  bool refreshSettingsLive(int xOffset);
  void buildWeatherDisplayValues(String& temp, String& hum, String& wind, String& rain24, String& iconCode) const;
  bool refreshWeatherLive(int xOffset);
  void drawManualLiveSection(int xOffset);
  void drawManualRemainingValue(int xOffset, int remainingSec);
  void drawWeatherIcon(int x, int y, int size, const String& iconCode);

  bool loadLogoFromBmp(const char* path);
  bool drawBmpFromFs(const char* path, int x, int y, bool blendWithDimBg, uint16_t fallbackBg);
  bool drawBmpFromFsScaled(const char* path, int x, int y, int outW, int outH, bool blendWithDimBg, uint16_t fallbackBg);
  uint16_t topBarColorAtY(int y) const;
  uint16_t dimBgColorAtY(int y) const;
  void drawLogo(int x, int y, int outSize);
  void drawHomeTile(int x, int y, int zoneIdx, bool focused, uint16_t focusColor);
  void drawStatusRow(int xOffset, int y, const String& label, const String& value, uint16_t valueColor);

  void drawCenteredText(const String& text, int cx, int cy, const GFXfont* font, uint16_t color);
  void drawLeftText(const String& text, int x, int y, const GFXfont* font, uint16_t color);
  void drawRightText(const String& text, int rightX, int y, const GFXfont* font, uint16_t color);
};
