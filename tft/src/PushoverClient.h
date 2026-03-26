#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "Settings.h"

class PushoverClient {
  Settings* settings;

  String urlEncode(const String& in) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < in.length(); i++) {
      const unsigned char c = (unsigned char)in[i];
      const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
      if (safe) {
        out += (char)c;
      } else if (c == ' ') {
        out += '+';
      } else {
        out += '%';
        out += hex[(c >> 4) & 0x0F];
        out += hex[c & 0x0F];
      }
    }
    return out;
  }

public:
  PushoverClient(Settings* s) : settings(s) {}
  void begin() {}
  void send(const String& msg) {
    send("WMS", msg);
  }

  void send(const String& title, const String& msg) {
    if (!settings) return;
    if (!settings->getEnablePushover()) return;
    if (settings->getPushoverUser() == "" || settings->getPushoverToken() == "") return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.pushover.net/1/messages.json");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = "token=" + urlEncode(settings->getPushoverToken()) +
                  "&user=" + urlEncode(settings->getPushoverUser()) +
                  "&title=" + urlEncode(title) +
                  "&message=" + urlEncode(msg);
    const int code = http.POST(body);
    if (code != 200) {
      Serial.printf("[Pushover] POST failed, HTTP=%d\n", code);
    } else {
      Serial.println("[Pushover] Message sent");
    }
    http.end();
  }
};
