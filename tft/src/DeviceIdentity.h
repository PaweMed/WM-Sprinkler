#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <esp_mac.h>

class DeviceIdentity {
private:
  String deviceId;
  String claimCode;
  bool loaded = false;

  static bool isDigitString(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
      if (!isDigit(s[i])) return false;
    }
    return true;
  }

  static bool isValidDeviceCode(const String& code) {
    if (!code.startsWith("WMS_")) return false;
    if (code.length() != 9) return false; // WMS_ + 5 cyfr
    String num = code.substring(4);
    return isDigitString(num);
  }

  static String formatDeviceIdFromMac() {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);

    // 48-bit MAC -> stabilne 5 cyfr (00000-99999), format: WMS_12345
    uint64_t macNum =
      ((uint64_t)mac[0] << 40) |
      ((uint64_t)mac[1] << 32) |
      ((uint64_t)mac[2] << 24) |
      ((uint64_t)mac[3] << 16) |
      ((uint64_t)mac[4] << 8)  |
      (uint64_t)mac[5];

    uint32_t suffix = (uint32_t)(macNum % 100000ULL);
    char out[16];
    snprintf(out, sizeof(out), "WMS_%05u", (unsigned)suffix);
    return String(out);
  }

  static String generateClaimCode() {
    // Claim code i nazwa urządzenia są tym samym stabilnym kodem.
    return formatDeviceIdFromMac();
  }

  bool loadFromFS() {
    if (!LittleFS.exists("/device-identity.json")) return false;

    File f = LittleFS.open("/device-identity.json", "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    deviceId = doc["device_id"] | "";
    claimCode = doc["claim_code"] | "";
    return !(deviceId.isEmpty() || claimCode.isEmpty());
  }

  bool saveToFS() const {
    File f = LittleFS.open("/device-identity.json", "w");
    if (!f) return false;

    JsonDocument doc;
    doc["device_id"] = deviceId;
    doc["claim_code"] = claimCode;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
  }

public:
  void begin() {
    if (loaded) return;

    if (!loadFromFS()) {
      deviceId = formatDeviceIdFromMac();
      claimCode = generateClaimCode();
      saveToFS();
    } else {
      // Migracja starszego formatu do docelowego: WMS_12345
      String expected = formatDeviceIdFromMac();
      if (!isValidDeviceCode(deviceId) || deviceId != expected) {
        deviceId = expected;
      }
      if (!isValidDeviceCode(claimCode) || claimCode != expected) {
        claimCode = expected;
      }
      saveToFS();
    }
    loaded = true;
  }

  String getDeviceId() const { return deviceId; }
  String getClaimCode() const { return claimCode; }

  bool rotateClaimCode() {
    // Kod jest stabilny i deterministyczny względem MAC.
    claimCode = formatDeviceIdFromMac();
    deviceId = claimCode;
    return saveToFS();
  }
};
