#pragma once

// Domyślna konfiguracja chmury dla pracy "od kopa".
// To konto MQTT jest kontem serwisowym urządzenia (cloud/device link).
// Dostępy użytkowników Home Assistant są generowane osobno per-device po stronie cloud.
static constexpr const char* WMS_DEFAULT_MQTT_SERVER = "wmsprinkler.pl";
static constexpr int         WMS_DEFAULT_MQTT_PORT   = 8883;
static constexpr const char* WMS_DEFAULT_MQTT_USER   = "wms_device";
static constexpr const char* WMS_DEFAULT_MQTT_PASS   = "9521mycode";
static constexpr const char* WMS_DEFAULT_MQTT_PREFIX = "wms";
