#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Aktualna wersja firmware pokazywana w UI i publikowana do cloud.
// Zmieniaj ten numer przy każdym oficjalnym wydaniu.
static constexpr const char* WM_FW_VERSION = "1.4.10";

class FirmwareVersionStore {
public:
  static constexpr const char* kStatePath = "/fw-version.json";

  static String buildVersion() {
    return String(WM_FW_VERSION);
  }

  static String reportedVersion() {
    String current;
    String pending;
    if (readState(current, pending)) {
      current.trim();
      if (current.length() > 0) return current;
    }
    return buildVersion();
  }

  static bool stagePendingVersion(const String& version, String& errOut) {
    String current;
    String pending;
    if (!readState(current, pending)) {
      errOut = "cannot read firmware version state";
      return false;
    }

    pending = normalizeVersion(version);
    if (pending.length() == 0) {
      errOut = "missing ota version";
      return false;
    }

    return writeState(current, pending, errOut);
  }

  static bool setCurrentVersion(const String& version, String& errOut) {
    String current;
    String pending;
    if (!readState(current, pending)) {
      errOut = "cannot read firmware version state";
      return false;
    }

    current = normalizeVersion(version);
    if (current.length() == 0) {
      errOut = "missing ota version";
      return false;
    }

    pending = "";
    return writeState(current, pending, errOut);
  }

  static bool activatePendingVersion(String& activatedVersion, String& errOut) {
    activatedVersion = "";

    String current;
    String pending;
    if (!readState(current, pending)) {
      errOut = "cannot read firmware version state";
      return false;
    }

    pending = normalizeVersion(pending);
    if (pending.length() == 0) return true;

    current = pending;
    pending = "";
    if (!writeState(current, pending, errOut)) return false;

    activatedVersion = current;
    return true;
  }

private:
  static String normalizeVersion(String value) {
    value.trim();
    return value;
  }

  static bool readState(String& current, String& pending) {
    current = "";
    pending = "";

    if (!LittleFS.exists(kStatePath)) return true;

    File f = LittleFS.open(kStatePath, "r");
    if (!f) return false;

    JsonDocument doc;
    const auto err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonObject>()) {
      return true;
    }

    current = normalizeVersion(String(doc["current"] | ""));
    pending = normalizeVersion(String(doc["pending"] | ""));
    return true;
  }

  static bool writeState(const String& current, const String& pending, String& errOut) {
    JsonDocument doc;
    doc["current"] = normalizeVersion(current);
    doc["pending"] = normalizeVersion(pending);

    File f = LittleFS.open(kStatePath, "w");
    if (!f) {
      errOut = "cannot open firmware version state";
      return false;
    }
    if (serializeJson(doc, f) == 0) {
      f.close();
      errOut = "cannot write firmware version state";
      return false;
    }
    f.close();
    return true;
  }
};
