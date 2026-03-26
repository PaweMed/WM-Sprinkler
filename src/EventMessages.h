#pragma once

#include <Arduino.h>

namespace EventMessages {

struct PushMessage {
  String title;
  String body;
};

inline String zoneNameOrDefault(String zoneName, int zoneNumber) {
  zoneName.trim();
  if (zoneName.length() == 0) zoneName = "Strefa " + String(zoneNumber);
  return zoneName;
}

inline String zoneObject(const String& zoneName, int zoneNumber) {
  return zoneNameOrDefault(zoneName, zoneNumber) + " (" + String(zoneNumber) + ")";
}

inline String minuteWordAccusative(int mins) {
  const int absMins = abs(mins);
  const int mod100 = absMins % 100;
  const int mod10 = absMins % 10;
  if (mod100 >= 12 && mod100 <= 14) return "minut";
  if (mod10 == 1) return "minutę";
  if (mod10 >= 2 && mod10 <= 4) return "minuty";
  return "minut";
}

inline String minutesPhrase(int mins) {
  if (mins < 1) mins = 1;
  return String(mins) + " " + minuteWordAccusative(mins);
}

inline String sourceCode(String raw, bool localFallback = false) {
  raw.trim();
  String key = raw;
  key.toLowerCase();

  if (key == "smart_climate" || key == "wmsc") return "WMSC";
  if (key == "home_assistant" || key == "ha") return "HA";
  if (key == "mqtt") return "MQTT";
  if (key == "schedule" || key == "scheduler" || key == "program") return "HARMONOGRAM";
  if (key == "cloud" || key == "manual_cloud") return "RECZNY_CLOUD";
  if (key == "local" || key == "manual_local") return "RECZNY_LOKALNIE";
  if (key == "system") return "SYSTEM";
  if (key.length() == 0 && localFallback) return "RECZNY_LOKALNIE";
  if (key.length() == 0) return "";

  raw.replace(" ", "_");
  raw.toUpperCase();
  return raw;
}

inline String logTitle(const char* category, const String& action) {
  return "[" + String(category) + "] " + action;
}

inline void appendLogField(String& msg, const char* key, const String& value) {
  if (value.length() == 0) return;
  msg += " | ";
  msg += key;
  msg += "=";
  msg += value;
}

inline String normalizeZoneObject(String zoneObj) {
  zoneObj.trim();
  zoneObj.replace(" (#", " (");
  zoneObj.replace("#", "");
  return zoneObj;
}

inline bool hasCloudTimestampPrefix(const String& text) {
  if (text.length() < 21) return false;
  auto isDigitAt = [&text](int idx) -> bool {
    return idx >= 0 && idx < (int)text.length() && isDigit((unsigned char)text[idx]);
  };
  return isDigitAt(0) && isDigitAt(1) && isDigitAt(2) && isDigitAt(3) &&
         text[4] == '-' &&
         isDigitAt(5) && isDigitAt(6) &&
         text[7] == '-' &&
         isDigitAt(8) && isDigitAt(9) &&
         text[10] == ' ' &&
         isDigitAt(11) && isDigitAt(12) &&
         text[13] == ':' &&
         isDigitAt(14) && isDigitAt(15) &&
         text[16] == ':' &&
         isDigitAt(17) && isDigitAt(18) &&
         text[19] == ':' &&
         text[20] == ' ';
}

inline String stripTimestampPrefix(const String& line, bool& hadIsoTimestamp) {
  hadIsoTimestamp = false;
  if (hasCloudTimestampPrefix(line)) {
    hadIsoTimestamp = true;
    return line.substring(21);
  }
  if (line.startsWith("uptime+")) {
    const int sep = line.indexOf(": ");
    if (sep > 0) return line.substring(sep + 2);
  }
  return line;
}

inline String stripCategoryPrefix(String text) {
  text.trim();
  if (text.startsWith("[")) {
    const int end = text.indexOf(']');
    if (end > 0) {
      text = text.substring(end + 1);
    }
  }
  text.trim();
  return text;
}

inline String ensureSentence(String text) {
  text.trim();
  if (!text.length()) return text;
  const char last = text[text.length() - 1];
  if (last == '.' || last == '!' || last == '?') return text;
  return text + ".";
}

inline String simplifyReason(String raw) {
  raw.trim();
  if (!raw.length()) return "brak powodu";

  String key = raw;
  key.toLowerCase();
  if (key.indexOf("przymrozk") >= 0 || key.indexOf("mroz") >= 0) return "ryzyko przymrozku";
  if (key.indexOf("niska temperatura") >= 0) return "niska temperatura";
  if (key.indexOf("wysoka temperatura") >= 0 || key.indexOf("upal") >= 0) return "upal";
  if (key.indexOf("deficyt ponizej progu") >= 0 || key.indexOf("deficyt poniżej progu") >= 0) return "deficyt ponizej progu";
  if (key.indexOf("okno czasowe") >= 0 || key.indexOf("okno podlewania") >= 0) return "czeka na okno podlewania";
  if (key.indexOf("prognoza") >= 0 && key.indexOf("deszcz") >= 0) return "prognoza deszczu";
  if (key.indexOf("prognoza") >= 0 && key.indexOf("opad") >= 0) return "prognoza deszczu";
  if (key.indexOf("ulew") >= 0 || key.indexOf("mocne opady") >= 0 || key.indexOf("mocny deszcz") >= 0) return "mocny deszcz";
  if (key.indexOf("deszcz") >= 0 || key.indexOf("opad") >= 0) return "opady";
  if (key.indexOf("wiatr") >= 0) return "silny wiatr";
  if (key.indexOf("za krotki odstep") >= 0 || key.indexOf("za krótki odstęp") >= 0 || key.indexOf("ostatniego cyklu") >= 0) {
    return "za krotka przerwa po poprzednim podlewaniu";
  }
  if (key.indexOf("offline") >= 0) return "urzadzenie offline";
  if (key.indexOf("inna strefa") >= 0) return "inna strefa juz pracuje";
  if (key.indexOf("wyslano start") >= 0 || key.indexOf("wysłano start") >= 0) return "oczekiwanie na potwierdzenie startu";
  if (key.indexOf("rozliczono") >= 0 || key.indexOf("realne podlewanie") >= 0) return "podlewanie zakonczone";
  if (key.indexOf("swiezych danych pogodowych") >= 0 || key.indexOf("świeżych danych pogodowych") >= 0) {
    return "brak swiezych danych pogodowych";
  }
  if (key.indexOf("historii opadow") >= 0 || key.indexOf("historii opadów") >= 0) {
    return "brak swiezej historii opadow";
  }

  const int semicolon = raw.indexOf(';');
  if (semicolon > 0) raw = raw.substring(0, semicolon);
  const int paren = raw.indexOf(" (");
  if (paren > 0) raw = raw.substring(0, paren);
  raw.trim();
  if (!raw.length()) return "brak powodu";
  raw[0] = (char)tolower((unsigned char)raw[0]);
  return raw;
}

inline String logSentence(const char* category, const String& sentence) {
  return "[" + String(category) + "] " + ensureSentence(sentence);
}

inline PushMessage pushSentence(const String& sentence) {
  PushMessage msg;
  msg.title = "WM Sprinkler";
  msg.body = ensureSentence(sentence);
  return msg;
}

inline String zoneStarted(const String& zoneObj, int mins) {
  String out = normalizeZoneObject(zoneObj) + " uruchomiono";
  if (mins > 0) out += " na " + minutesPhrase(mins);
  return out;
}

inline String zoneStopped(const String& zoneObj) {
  return normalizeZoneObject(zoneObj) + " wyłączono";
}

inline String zoneCancelled(const String& zoneObj, const String& reason) {
  return normalizeZoneObject(zoneObj) + " anulowano - " + simplifyReason(reason);
}

inline String zoneUpdated(const String& zoneObj, int mins) {
  String out = normalizeZoneObject(zoneObj) + " zaktualizowano";
  if (mins > 0) out += " do " + minutesPhrase(mins);
  return out;
}

inline String plugStarted(const String& plugLabel, int mins) {
  String out = String(plugLabel) + " włączono";
  if (mins > 0) out += " na " + minutesPhrase(mins);
  return out;
}

inline String plugStopped(const String& plugLabel) {
  return String(plugLabel) + " wyłączono";
}

inline String settingsSaved() {
  return "Ustawienia zapisano";
}

inline String zoneNamesSaved() {
  return "Nazwy stref zapisano";
}

inline String displayMessageText(const String& rawLine) {
  bool hadIsoTimestamp = false;
  String text = stripTimestampPrefix(rawLine, hadIsoTimestamp);
  text.trim();

  String prefix = "";
  if (hadIsoTimestamp) prefix = rawLine.substring(0, 19) + ": ";
  return prefix + stripCategoryPrefix(text);
}

}  // namespace EventMessages
