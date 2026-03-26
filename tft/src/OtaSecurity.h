#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include "FirmwareVersion.h"

#if __has_include(<sodium.h>)
extern "C" {
#include <sodium.h>
}
#define OTA_SODIUM_AVAILABLE 1
#else
#define OTA_SODIUM_AVAILABLE 0
#endif

#ifndef OTA_REQUIRE_SIGNATURE
#define OTA_REQUIRE_SIGNATURE 1
#endif

#ifndef OTA_ED25519_PUBLIC_KEY_HEX
#define OTA_ED25519_PUBLIC_KEY_HEX ""
#endif

#ifndef OTA_BOOT_MAX_ATTEMPTS
#define OTA_BOOT_MAX_ATTEMPTS 2
#endif

class OtaSecurity {
public:
  static constexpr const char* kPublicKeyPath = "/ota-ed25519-pubkey.hex";
  static constexpr const char* kBootMarkerPath = "/ota-boot-verify.json";

  static String normalizeHex(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
      char c = in[i];
      if (c >= 'A' && c <= 'F') c = char(c - 'A' + 'a');
      if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) out += c;
    }
    return out;
  }

  static bool requireSignature() {
    return OTA_REQUIRE_SIGNATURE != 0;
  }

  static bool verifyHashEd25519(const uint8_t hash[32], const String& signature, String& errOut) {
    const String sigTrimmed = trimCopy(signature);
    if (sigTrimmed.length() == 0) {
      if (requireSignature()) {
        errOut = "missing signature";
        return false;
      }
      return true;
    }

    uint8_t pubKey[32];
    if (!loadPublicKey(pubKey, errOut)) return false;

#if OTA_SODIUM_AVAILABLE
    uint8_t sig[64];
    if (!hexToBytes(sigTrimmed, sig, sizeof(sig), errOut)) {
      errOut = "invalid signature format (expected 128 hex chars)";
      return false;
    }

    const int rc = crypto_sign_ed25519_verify_detached(sig, hash, 32, pubKey);
    if (rc != 0) {
      errOut = "invalid Ed25519 signature";
      return false;
    }
    return true;
#else
    (void)hash;
    (void)pubKey;
    errOut = "Ed25519 unavailable (libsodium missing in build)";
    return false;
#endif
  }

  static bool markBootPending(const String& version, const String& target, String& errOut) {
    JsonDocument doc;
    doc["version"] = version;
    doc["target"] = target;
    doc["attempts"] = 0;
    doc["created_ms"] = millis();

    File f = LittleFS.open(kBootMarkerPath, "w");
    if (!f) {
      errOut = "cannot write ota boot marker";
      return false;
    }
    if (serializeJson(doc, f) == 0) {
      f.close();
      errOut = "cannot serialize ota boot marker";
      return false;
    }
    f.close();
    return true;
  }

  static void handleBootStart() {
    if (!LittleFS.exists(kBootMarkerPath)) return;

    JsonDocument doc;
    int attempts = 0;
    String version;
    File in = LittleFS.open(kBootMarkerPath, "r");
    if (in) {
      if (deserializeJson(doc, in) == DeserializationError::Ok && doc.is<JsonObject>()) {
        attempts = doc["attempts"].is<int>() ? doc["attempts"].as<int>() : 0;
        version = String(doc["version"] | "");
      }
      in.close();
    }

    attempts += 1;
    doc["attempts"] = attempts;
    doc["last_boot_ms"] = millis();
    File out = LittleFS.open(kBootMarkerPath, "w");
    if (out) {
      serializeJson(doc, out);
      out.close();
    }

    const bool pendingVerify = isRunningPartitionPendingVerify();
    if (pendingVerify) {
      Serial.printf("[OTA-VERIFY] Pending verify boot #%d (version=%s)\n", attempts, version.c_str());
    }

    if (attempts > OTA_BOOT_MAX_ATTEMPTS) {
      Serial.println("[OTA-VERIFY] Too many failed boots, requesting rollback.");
      if (pendingVerify) {
        esp_err_t rc = esp_ota_mark_app_invalid_rollback_and_reboot();
        Serial.printf("[OTA-VERIFY] rollback rc=%d\n", (int)rc);
      } else if (Update.canRollBack() && Update.rollBack()) {
        Serial.println("[OTA-VERIFY] Rollback scheduled via Update.rollBack().");
        delay(100);
        esp_restart();
      } else {
        Serial.println("[OTA-VERIFY] Rollback unavailable on this bootloader/config.");
      }
    }
  }

  static void confirmBootIfPending() {
    String bootVersion;
    if (LittleFS.exists(kBootMarkerPath)) {
      JsonDocument doc;
      File in = LittleFS.open(kBootMarkerPath, "r");
      if (in) {
        if (deserializeJson(doc, in) == DeserializationError::Ok && doc.is<JsonObject>()) {
          bootVersion = String(doc["version"] | "");
          bootVersion.trim();
        }
        in.close();
      }
    }

    if (isRunningPartitionPendingVerify()) {
      const esp_err_t rc = esp_ota_mark_app_valid_cancel_rollback();
      if (rc == ESP_OK) {
        Serial.println("[OTA-VERIFY] App marked valid, rollback cancelled.");
      } else {
        Serial.printf("[OTA-VERIFY] mark valid failed rc=%d\n", (int)rc);
      }
    }
    if (bootVersion.length() > 0) {
      String versionErr;
      if (!FirmwareVersionStore::setCurrentVersion(bootVersion, versionErr)) {
        Serial.println("[FWVER] WARN commit OTA version: " + versionErr);
      } else {
        Serial.println("[FWVER] Committed OTA version: " + bootVersion);
      }
    }
    if (LittleFS.exists(kBootMarkerPath)) {
      LittleFS.remove(kBootMarkerPath);
    }
  }

private:
  static bool isRunningPartitionPendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return false;

    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) return false;
    return otaState == ESP_OTA_IMG_PENDING_VERIFY;
  }

  static String trimCopy(String value) {
    value.trim();
    return value;
  }

  static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  }

  static bool hexToBytes(const String& in, uint8_t* out, size_t outLen, String& errOut) {
    String hex = normalizeHex(in);
    if (hex.length() != outLen * 2) {
      errOut = "invalid hex length";
      return false;
    }
    for (size_t i = 0; i < outLen; ++i) {
      int hi = hexNibble(hex[i * 2]);
      int lo = hexNibble(hex[i * 2 + 1]);
      if (hi < 0 || lo < 0) {
        errOut = "invalid hex char";
        return false;
      }
      out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
  }

  static bool loadPublicKey(uint8_t out[32], String& errOut) {
    String keyHex;
    if (LittleFS.exists(kPublicKeyPath)) {
      File f = LittleFS.open(kPublicKeyPath, "r");
      if (f) {
        keyHex = f.readString();
        f.close();
      }
    }
    if (keyHex.length() == 0) {
      keyHex = OTA_ED25519_PUBLIC_KEY_HEX;
    }
    keyHex = normalizeHex(keyHex);

    if (keyHex.length() == 0) {
      errOut = "missing OTA Ed25519 public key";
      return false;
    }
    if (keyHex.length() != 64) {
      errOut = "invalid OTA Ed25519 public key length";
      return false;
    }
    return hexToBytes(keyHex, out, 32, errOut);
  }
};
