#pragma once
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <StreamString.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "Zones.h"
#include "Weather.h"
#include "PushoverClient.h"
#include "Programs.h"
#include "Logs.h"
#include "MQTTClient.h"
#include "DeviceIdentity.h"
#include "FirmwareVersion.h"
#include "OtaStateBackup.h"
#include "EventMessages.h"

// z main.cpp
extern "C" void setTimezoneFromWeb();

extern MQTTClient mqtt; // użyjemy do updateConfig po zapisaniu ustawień

static const char* wifiAuthModeToText(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa-psk";
    case WIFI_AUTH_WPA2_PSK: return "wpa2-psk";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa-wpa2-psk";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK: return "wpa3-psk";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2-wpa3-psk";
#endif
    default: return "unknown";
  }
}

struct WiFiScanEntry {
  String ssid;
  int rssi = -200;
  wifi_auth_mode_t enc = WIFI_AUTH_OPEN;
  bool secure = false;
};

static WiFiScanEntry gWifiScanCache[32];
static int gWifiScanCacheCount = 0;
static bool gWifiScanRunning = false;
static unsigned long gWifiScanLastUpdateMs = 0;
static bool gWifiScanNeedsApRestore = false;

static void fillWifiScanCacheFromDriver(int n) {
  gWifiScanCacheCount = 0;
  for (int i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    const int rssi = WiFi.RSSI(i);
    const wifi_auth_mode_t enc = WiFi.encryptionType(i);
    const bool secure = (enc != WIFI_AUTH_OPEN);

    int existing = -1;
    for (int j = 0; j < gWifiScanCacheCount; ++j) {
      if (gWifiScanCache[j].ssid == ssid) {
        existing = j;
        break;
      }
    }
    if (existing >= 0) {
      if (rssi > gWifiScanCache[existing].rssi) {
        gWifiScanCache[existing].rssi = rssi;
        gWifiScanCache[existing].enc = enc;
        gWifiScanCache[existing].secure = secure;
      }
    } else if (gWifiScanCacheCount < (int)(sizeof(gWifiScanCache) / sizeof(gWifiScanCache[0]))) {
      gWifiScanCache[gWifiScanCacheCount].ssid = ssid;
      gWifiScanCache[gWifiScanCacheCount].rssi = rssi;
      gWifiScanCache[gWifiScanCacheCount].enc = enc;
      gWifiScanCache[gWifiScanCacheCount].secure = secure;
      gWifiScanCacheCount++;
    }
  }
}

static void restoreApModeIfNeeded() {
  if (!gWifiScanNeedsApRestore) return;
  if (gWifiScanRunning) return;
  if (WiFi.getMode() == WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_AP);
  }
  gWifiScanNeedsApRestore = false;
}

static void startWifiScanAsyncIfNeeded() {
  if (gWifiScanRunning) return;
  const bool wasPureAp = (WiFi.getMode() == WIFI_MODE_AP);
  gWifiScanNeedsApRestore = false;
  if (wasPureAp) {
    WiFi.mode(WIFI_AP_STA);
    gWifiScanNeedsApRestore = true;
  }
  WiFi.scanDelete();
  // async=true -> szybka odpowiedź API; wyniki odbieramy w kolejnych requestach.
  int rc = WiFi.scanNetworks(true, false);
  if (rc == WIFI_SCAN_RUNNING) {
    gWifiScanRunning = true;
  } else if (rc >= 0) {
    fillWifiScanCacheFromDriver(rc);
    WiFi.scanDelete();
    gWifiScanLastUpdateMs = millis();
    gWifiScanRunning = false;
    restoreApModeIfNeeded();
  } else {
    gWifiScanRunning = false;
    restoreApModeIfNeeded();
  }
}

static void runWifiScanSyncFallback() {
  const bool wasPureAp = (WiFi.getMode() == WIFI_MODE_AP);
  if (wasPureAp) {
    WiFi.mode(WIFI_AP_STA);
  }
  WiFi.scanDelete();

  int n = WiFi.scanNetworks(false, false);
  if (n <= 0) {
    // Część AP/routerów pokazuje się tylko przy show_hidden=true.
    n = WiFi.scanNetworks(false, true);
  }

  if (n > 0) {
    fillWifiScanCacheFromDriver(n);
  } else {
    gWifiScanCacheCount = 0;
  }
  WiFi.scanDelete();
  gWifiScanRunning = false;
  gWifiScanLastUpdateMs = millis();
  if (wasPureAp && WiFi.getMode() == WIFI_MODE_APSTA) {
    WiFi.mode(WIFI_AP);
  }
}

static void collectWifiScanResultsIfReady() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    gWifiScanRunning = true;
    return;
  }
  if (n < 0) {
    gWifiScanRunning = false;
    restoreApModeIfNeeded();
    return;
  }

  fillWifiScanCacheFromDriver(n);

  WiFi.scanDelete();
  gWifiScanRunning = false;
  gWifiScanLastUpdateMs = millis();
  restoreApModeIfNeeded();
}

// ========== AWARYJNA STRONA GŁÓWNA ==========
const char MAIN_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>WeatherMap Sprinkler – Panel awaryjny</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #f3f3f3 !important; margin:0; }
    #header { background: #3685c9; color:#fff; padding:20px 0; text-align:center; box-shadow:0 2px 6px #bbb;}
    .card { max-width: 500px; margin: 40px auto; background: #fff; border-radius: 12px; box-shadow: 0 2px 10px #bbb; padding: 30px 24px;}
    h2 { color: #3685c9;}
    .warn { color: #c93c36; font-weight: bold; }
    .info { color: #555; }
    .zone { margin: 12px 0; padding: 10px; border-radius: 6px; background: #f7fafc; display: flex; justify-content: space-between; align-items: center;}
    .zone.on { background: #d1edc1; }
    .zone.off { background: #fae3e3; }
    .zone button { min-width:90px; padding: 7px 0; border-radius: 6px; border: none; font-size:1em; cursor:pointer; }
    .zone.on button { background: #c93c36; color: #fff; }
    .zone.off button { background: #3685c9; color: #fff; }
    .weather-box { margin: 24px 0 12px 0; background: #e7f2fa; border-radius: 8px; padding: 14px;}
    .weather-row { margin: 5px 0;}
    .status-row { margin: 6px 0;}
    a.btn { display:inline-block; margin:6px 0; padding:8px 12px; background:#3685c9; color:#fff; border-radius:6px; text-decoration:none;}
    @media (max-width:550px) {
      .card { max-width: 98vw; padding: 10vw 2vw;}
    }
  </style>
</head>
<body>
  <div id="header">
    <h1>WeatherMap Sprinkler</h1>
  </div>
  <div class="card">
    <h2>Panel awaryjny</h2>
    <div class="warn" style="margin-bottom:12px;">Brak pliku <b>index.html</b> w pamięci LittleFS.<br>Wyświetlana jest wersja zapasowa panelu!</div>
    <div class="status-row"><b>Status WiFi:</b> <span id="wifi"></span></div>
    <div class="status-row"><b>Adres IP:</b> <span id="ip"></span></div>
    <div class="status-row"><b>Czas:</b> <span id="czas"></span></div>
    <hr>
    <div id="weather" class="weather-box">
      <b>Dane pogodowe:</b>
      <div class="weather-row">Temperatura: <span id="temp">?</span> &deg;C</div>
      <div class="weather-row">Wilgotność: <span id="humidity">?</span> %</div>
      <div class="weather-row">Deszcz (1h): <span id="rain">?</span> mm</div>
      <div class="weather-row">Wiatr: <span id="wind">?</span> m/s</div>
    </div>
    <hr>
    <b>Sterowanie strefami:</b>
    <div id="zones"></div>
    <hr>
    <div class="info">
      <a class="btn" href="/wifi">Konfiguracja WiFi</a><br>
      <a class="btn" href="/ota">Aktualizacja OTA (FW)</a><br>
      <a class="btn" href="/fs">Pliki LittleFS</a><br><br>
      <button onclick="location.reload()">Odśwież</button>
    </div>
  </div>
  <script>
    function loadStatus() {
      fetch('/api/status').then(r=>r.json()).then(d=>{
        document.getElementById('wifi').textContent = d.wifi;
        document.getElementById('ip').textContent = d.ip;
        document.getElementById('czas').textContent = d.time;
      });
    }
    function loadWeather() {
      fetch('/api/weather').then(r=>r.json()).then(d=>{
        document.getElementById('temp').textContent = d.temp ?? '?';
        document.getElementById('humidity').textContent = d.humidity ?? '?';
        document.getElementById('rain').textContent = d.rain ?? '?';
        document.getElementById('wind').textContent = d.wind ?? '?';
      });
    }
    function loadZones() {
      fetch('/api/zones').then(r=>r.json()).then(zs=>{
        let html = '';
        for (let z of zs) {
          html += `<div class="zone ${z.active?'on':'off'}">
            <span>Strefa #${z.id+1} ${z.active?'(WŁ)':'(WYŁ)'} ${z.name?(' - ' + z.name):''}</span>
            <button onclick="toggleZone(${z.id}, this)">${z.active?'Wyłącz':'Włącz'}</button>
          </div>`;
        }
        document.getElementById('zones').innerHTML = html;
      });
    }
    function toggleZone(id, btn) {
      btn.disabled = true;
      fetch('/api/zones', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({id, toggle:true}) })
        .then(r=>r.json()).then(()=>{ setTimeout(loadZones, 400); btn.disabled = false; });
    }
    loadStatus(); loadWeather(); loadZones();
  </script>
</body>
</html>
)rawliteral";

// ========== STRONA KONFIGU ACJI WIFI ==========
const char WIFI_CONFIG_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Konfiguracja WiFi – WeatherMap Sprinkler</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { height: 100%; margin: 0; padding: 0; font-family: Arial, sans-serif; background: #f3f3f3 !important; }
    body { min-height: 100vh; display: flex; flex-direction: column; background: #f3f3f3 !important; }
    #header { background: #3685c9; color: #fff; padding: 20px 0; text-align: center; box-shadow: 0 2px 6px #bbb; flex-shrink: 0; width: 100%; }
    .centerbox-outer { flex: 1 1 auto; display: flex; justify-content: center; align-items: center; min-height: 0; min-width: 0; }
    .card { width: 100%; max-width: 390px; background: #fff; border-radius: 12px; box-shadow: 0 2px 10px #bbb; padding: 28px 24px 24px 24px; margin: 0 auto; display: flex; flex-direction: column; justify-content: center; align-items: stretch; }
    label { font-size: 1.1em; display:block; margin-top: 1em; }
    input[type=text], input[type=password], select { width: 100%; box-sizing: border-box; padding: 10px; margin-top:6px; margin-bottom:12px; border: 1px solid #bbb; border-radius: 6px; font-size: 1em; }
    button { width: 100%; background: #3685c9; color:#fff; font-size:1.1em; padding:10px 0; border:none; border-radius:6px; cursor:pointer; margin-top: 16px; }
    button.secondary { margin-top: 8px; background: #f2f5f9; color: #2b4b68; border: 1px solid #c8d5e1; font-size: 0.98em; }
    .msg { color: #3685c9; margin-bottom: 16px; min-height: 1.3em; }
    .hint { color:#6b7280; font-size:.92em; margin:-6px 0 10px 0; min-height: 1.2em; }
  </style>
</head>
<body>
  <div id="header">
    <h1>Konfiguracja WiFi – WeatherMap Sprinkler</h1>
  </div>
  <div class="centerbox-outer">
    <div class="card">
      <form id="wifiForm" autocomplete="off">
        <div class="msg" id="msg"></div>
        <label for="ssidSelect">Wybierz sieć WiFi:</label>
        <select id="ssidSelect" name="ssidSelect">
          <option value="">Skanowanie sieci...</option>
        </select>
        <button type="button" id="refreshNets" class="secondary">Odśwież listę sieci</button>
        <div class="hint" id="netHint"></div>
        <label for="ssid">Nazwa sieci WiFi (SSID):</label>
        <input type="text" id="ssid" name="ssid" required maxlength="32" autocomplete="off">
        <label for="pass">Hasło WiFi:</label>
        <input type="password" id="pass" name="pass" maxlength="64" autocomplete="off">
        <div class="hint">Jeśli sieć jest otwarta, hasło zostaw puste.</div>
        <button type="submit">Zapisz i połącz</button>
      </form>
    </div>
  </div>
  <script>
    const msg = document.getElementById('msg');
    const ssidSelect = document.getElementById('ssidSelect');
    const ssidInput = document.getElementById('ssid');
    const passInput = document.getElementById('pass');
    const refreshBtn = document.getElementById('refreshNets');
    const netHint = document.getElementById('netHint');

    function setSelectOptions(networks) {
      ssidSelect.innerHTML = '';

      const first = document.createElement('option');
      first.value = '';
      first.textContent = networks.length ? 'Wybierz sieć z listy' : 'Brak sieci - wpisz SSID ręcznie';
      ssidSelect.appendChild(first);

      networks.forEach(net => {
        const opt = document.createElement('option');
        opt.value = net.ssid || '';
        const enc = net.enc || (net.secure ? 'secured' : 'open');
        opt.textContent = `${net.ssid} (${net.rssi} dBm, ${enc})`;
        opt.dataset.secure = net.secure ? '1' : '0';
        opt.dataset.enc = enc;
        ssidSelect.appendChild(opt);
      });

      const manual = document.createElement('option');
      manual.value = '__manual__';
      manual.textContent = 'Inna sieć (wpisz ręcznie)';
      ssidSelect.appendChild(manual);
    }

    async function loadNetworks() {
      refreshBtn.disabled = true;
      msg.textContent = 'Skanowanie sieci WiFi...';
      netHint.textContent = '';
      try {
        const res = await fetch('/api/wifi/networks', { cache: 'no-store' });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        const networks = Array.isArray(data.networks) ? data.networks : [];
        setSelectOptions(networks);
        if (data.scanning) {
          msg.textContent = networks.length
            ? ('Skanowanie trwa... znalezione dotąd: ' + networks.length)
            : 'Skanowanie trwa...';
          setTimeout(loadNetworks, 1200);
        } else {
          msg.textContent = networks.length
            ? ('Znaleziono sieci: ' + networks.length)
            : 'Nie znaleziono sieci. Wpisz SSID ręcznie.';
        }
      } catch (err) {
        msg.textContent = 'Nie udało się pobrać listy sieci: ' + (err && err.message ? err.message : err);
        // iOS potrafi chwilowo przerwać request podczas skanu - próbujemy ponownie.
        setTimeout(loadNetworks, 1500);
      } finally {
        refreshBtn.disabled = false;
      }
    }

    ssidSelect.addEventListener('change', function() {
      const selected = ssidSelect.options[ssidSelect.selectedIndex];
      const val = selected ? selected.value : '';

      if (val === '__manual__') {
        ssidInput.focus();
        ssidInput.select();
        netHint.textContent = 'Wpisz SSID ręcznie.';
        return;
      }
      if (!val) {
        netHint.textContent = '';
        return;
      }

      ssidInput.value = val;
      const isSecure = selected && selected.dataset && selected.dataset.secure === '1';
      const enc = selected && selected.dataset ? (selected.dataset.enc || '') : '';
      if (!isSecure) {
        netHint.textContent = 'Sieć otwarta - hasło może pozostać puste.';
      } else if (enc.indexOf('wpa3') >= 0) {
        netHint.textContent = 'Sieć WPA3/WPA2-WPA3. Jeśli ESP nie łączy się, ustaw w routerze WPA2-PSK (AES).';
      } else {
        netHint.textContent = 'Sieć zabezpieczona - wpisz hasło.';
      }
      if (!isSecure) passInput.value = '';
    });

    refreshBtn.addEventListener('click', loadNetworks);

    document.getElementById('wifiForm').addEventListener('submit', async function(e){
      e.preventDefault();
      msg.textContent = '';
      const ssid = ssidInput.value.trim();
      const pass = passInput.value;
      if (!ssid) { msg.textContent = 'Wpisz nazwę sieci!'; return; }
      msg.textContent = 'Zapisywanie...';
      const payload = JSON.stringify({ ssid: ssid, pass: pass });
      // Safari/iOS bywa wrażliwy na restart AP w trakcie odpowiedzi.
      // Używamy XHR i traktujemy status=0 po wysłaniu jako "prawdopodobny sukces + restart".
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/wifi', true);
      xhr.setRequestHeader('Content-Type', 'application/json');
      xhr.timeout = 12000;

      let sent = false;
      xhr.onload = function() {
        let data = {};
        try { data = JSON.parse(xhr.responseText || '{}'); } catch (_e) {}
        if (xhr.status >= 200 && xhr.status < 300 && data.ok) {
          msg.textContent = 'Dane zapisane! Urządzenie restartuje się i spróbuje połączyć z nową siecią WiFi.';
          setTimeout(function(){ location.reload(); }, 5000);
        } else {
          msg.textContent = 'Błąd: ' + (data.error || ('HTTP ' + xhr.status));
        }
      };
      xhr.onerror = function() {
        if (sent) {
          msg.textContent = 'Ustawienia wysłane. Urządzenie mogło już przejść do restartu. Połącz się ponownie z siecią/AP za chwilę.';
        } else {
          msg.textContent = 'Błąd połączenia (nie wysłano danych).';
        }
      };
      xhr.ontimeout = function() {
        msg.textContent = 'Przekroczono czas oczekiwania. Jeśli ESP się restartuje, odczekaj chwilę i sprawdź połączenie WiFi.';
      };
      try {
        sent = true;
        xhr.send(payload);
      } catch (err) {
        msg.textContent = 'Błąd połączenia: ' + err;
      }
    });

    loadNetworks();
  </script>
</body>
</html>
)rawliteral";

// ========== STRONA OTA (FIRMWARE) ==========
const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Aktualizacja OTA – WeatherMap Sprinkler</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { font-family: Arial, sans-serif; background: #f3f3f3; margin: 0; }
    #header { background: #3685c9; color: #fff; padding: 20px 0; text-align: center; box-shadow: 0 2px 10px #bbb; }
    .card { max-width: 420px; margin: 40px auto; background: #fff; border-radius: 12px; box-shadow: 0 2px 10px #bbb; padding: 32px 24px 24px 24px; }
    h2 { color: #3685c9; }
    .msg { color: #c93c36; margin-bottom: 16px; min-height: 1.3em; }
    input[type=file] { width: 100%; margin: 14px 0; }
    button { width: 100%; background: #3685c9; color:#fff; font-size:1.1em; padding:10px 0; border:none; border-radius:6px; cursor:pointer; margin-top: 16px; }
    .progress { width: 100%; background: #eee; height: 18px; border-radius: 8px; margin-top: 10px; }
    .bar { height: 18px; border-radius: 8px; width: 0%; background: #3685c9; }
    .hint { font-size: .95em; color:#444; }
  </style>
</head>
<body>
  <div id="header"><h1>Aktualizacja OTA</h1></div>
  <div class="card">
    <h2>Firmware (.bin)</h2>
    <div class="msg" id="msg"></div>
    <form id="otaForm">
      <input type="file" id="firmware" name="firmware" accept=".bin" required>
      <div class="progress"><div id="bar" class="bar"></div></div>
      <button type="submit">Wyślij i zaktualizuj</button>
    </form>
    <p class="hint">Szukasz aktualizacji plików WWW? Użyj <b><a href="/fs">/fs</a></b> (pojedyncze pliki) lub <b>OTA FS</b> niżej.</p>
    <h2>LittleFS image (.bin)</h2>
    <form id="fsForm">
      <input type="file" id="fsbin" name="fsbin" accept=".bin" required>
      <div class="progress"><div id="bar2" class="bar"></div></div>
      <button type="submit">Wyślij i wgraj obraz LittleFS</button>
    </form>
  </div>
  <script>
    const msg = document.getElementById('msg');
    const bar = document.getElementById('bar');
    const bar2 = document.getElementById('bar2');

    document.getElementById('otaForm').addEventListener('submit', function(e){
      e.preventDefault();
      msg.style.color = "#c93c36"; msg.textContent = 'Wgrywanie firmware...';
      const fw = document.getElementById('firmware').files[0];
      if (!fw) { msg.textContent = 'Wybierz plik .bin'; return; }
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/ota');
      xhr.upload.onprogress = (e)=>{ if(e.lengthComputable){ bar.style.width = (Math.round(e.loaded/e.total*100)) + '%'; } };
      xhr.onload = ()=>{ if (xhr.status==200){ msg.style.color="#3685c9"; msg.textContent='Sukces! Restart...'; setTimeout(()=>location.reload(), 3500);} else { msg.textContent='Błąd: '+xhr.responseText; bar.style.width='0%'; } };
      xhr.onerror = ()=>{ msg.textContent='Błąd połączenia'; bar.style.width='0%'; };
      const fd = new FormData(); fd.append('firmware', fw); xhr.send(fd);
    });

    document.getElementById('fsForm').addEventListener('submit', function(e){
      e.preventDefault();
      msg.style.color = "#c93c36"; msg.textContent = 'Wgrywanie obrazu LittleFS...';
      const f = document.getElementById('fsbin').files[0];
      if (!f) { msg.textContent = 'Wybierz obraz LittleFS .bin'; return; }
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/ota-fs');
      xhr.upload.onprogress = (e)=>{ if(e.lengthComputable){ bar2.style.width = (Math.round(e.loaded/e.total*100)) + '%'; } };
      xhr.onload = ()=>{ if (xhr.status==200){ msg.style.color="#3685c9"; msg.textContent='LittleFS wgrany! Restart...'; setTimeout(()=>location.reload(), 3500);} else { msg.textContent='Błąd: '+xhr.responseText; bar2.style.width='0%'; } };
      xhr.onerror = ()=>{ msg.textContent='Błąd połączenia'; bar2.style.width='0%'; };
      const fd = new FormData(); fd.append('fsbin', f); xhr.send(fd);
    });
  </script>
</body>
</html>
)rawliteral";

// ========== STRONA /fs (menedżer plików) ==========
const char FS_MANAGER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Pliki LittleFS – WeatherMap Sprinkler</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #f3f3f3; margin:0; }
    #header { background: #3685c9; color:#fff; padding: 16px 0; text-align: center; box-shadow: 0 2px 10px #bbb; }
    .wrap { max-width: 900px; margin: 20px auto; background:#fff; border-radius:12px; box-shadow:0 2px 10px #bbb; padding: 16px; }
    table { width:100%; border-collapse: collapse; }
    th, td { border-bottom:1px solid #eee; text-align:left; padding:8px; }
    .row { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:10px 0; }
    input[type=text], input[type=file]{ padding:6px; border:1px solid #bbb; border-radius:6px; }
    button { padding:8px 12px; border:none; border-radius:6px; background:#3685c9; color:#fff; cursor:pointer; }
    .danger { background:#c93c36; }
    code { background:#f3f3f3; padding:2px 6px; border-radius:4px; }
  </style>
</head>
<body>
  <div id="header"><h1>Pliki LittleFS</h1></div>
  <div class="wrap">
    <div class="row">
      <form id="uploadForm">
        <input type="file" id="file" required>
        <input type="text" id="path" placeholder="/index.html lub /assets/app.js" size="40" required>
        <button type="submit">Wyślij plik</button>
      </form>
      <button onclick="refresh()">Odśwież</button>
      <a href="/ota" style="margin-left:auto"><button>Przejdź do OTA</button></a>
    </div>
    <table id="tbl">
      <thead><tr><th>Ścieżka</th><th>Rozmiar (B)</th><th>Akcje</th></tr></thead>
      <tbody></tbody>
    </table>
    <p>Uwaga: lista jest z katalogu głównego. Dla podkatalogów podaj pełną ścieżkę w polu <code>/path</code>.</p>
  </div>
  <script>
    function refresh(){
      fetch('/api/fs/list').then(r=>r.json()).then(d=>{
        const tb = document.querySelector('#tbl tbody'); tb.innerHTML='';
        (d.files||[]).forEach(f=>{
          const tr = document.createElement('tr');
          tr.innerHTML = `<td>${f.name}</td><td>${f.size}</td>
          <td><button class="danger" onclick="del('${f.name}')">Usuń</button></td>`;
          tb.appendChild(tr);
        });
      });
    }
    function del(path){
      if(!confirm('Usunąć '+path+' ?')) return;
      fetch('/api/fs/delete', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({path})})
        .then(r=>r.json()).then(()=>refresh());
    }
    document.getElementById('uploadForm').addEventListener('submit', function(e){
      e.preventDefault();
      const f = document.getElementById('file').files[0];
      const p = document.getElementById('path').value.trim();
      if(!f || !p){ alert('Wskaż plik i ścieżkę!'); return; }
      const fd = new FormData(); fd.append('file', f); fd.append('path', p);
      fetch('/api/fs/upload', { method:'POST', body:fd })
        .then(r=>r.json()).then(_=>{ alert('Wgrano: '+p); refresh(); })
        .catch(_=> alert('Błąd uploadu'));
    });
    refresh();
  </script>
</body>
</html>
)rawliteral";

// --- Auth
inline bool checkAuth(AsyncWebServerRequest *request) {
  if (!request->authenticate("admin", "admin")) {
    request->requestAuthentication();
    return false;
  }
  return true;
}

namespace WebServerUI {
  static AsyncWebServer* server = nullptr;
  static File _uploadFile; // do /api/fs/upload
  static bool _otaFwPrepared = false;
  static bool _otaFsPrepared = false;
  static String _otaFwError = "";
  static String _otaFsError = "";

  static Weather::SmartIrrigationConfig buildSmartIrrigationConfig(Config* cfg) {
    Weather::SmartIrrigationConfig sc;
    if (!cfg) return sc;
    sc.tempSkipC = cfg->getIrrigationTempSkipC();
    sc.tempLowMaxC = cfg->getIrrigationTempLowMaxC();
    sc.tempMidMaxC = cfg->getIrrigationTempMidMaxC();
    sc.tempHighMaxC = cfg->getIrrigationTempHighMaxC();
    sc.tempFactorLow = cfg->getIrrigationTempFactorLow();
    sc.tempFactorMid = cfg->getIrrigationTempFactorMid();
    sc.tempFactorHigh = cfg->getIrrigationTempFactorHigh();
    sc.tempFactorVeryHigh = cfg->getIrrigationTempFactorVeryHigh();
    sc.rainSkipMm = cfg->getIrrigationRainSkipMm();
    sc.rainHighMinMm = cfg->getIrrigationRainHighMinMm();
    sc.rainMidMinMm = cfg->getIrrigationRainMidMinMm();
    sc.rainFactorHigh = cfg->getIrrigationRainFactorHigh();
    sc.rainFactorMid = cfg->getIrrigationRainFactorMid();
    sc.rainFactorLow = cfg->getIrrigationRainFactorLow();
    sc.humidityHighPercent = cfg->getIrrigationHumidityHighPercent();
    sc.humidityFactorHigh = cfg->getIrrigationHumidityFactorHigh();
    sc.windSkipKmh = cfg->getIrrigationWindSkipKmh();
    sc.windFactor = cfg->getIrrigationWindFactor();
    sc.percentMin = cfg->getIrrigationPercentMin();
    sc.percentMax = cfg->getIrrigationPercentMax();
    return sc;
  }

  void begin(
      Config* config,
      void* /*Scheduler* scheduler,*/,
      Zones* relays,
      Weather* weather,
      PushoverClient* pushover,
      Programs* programs,
      Logs* logs,
      DeviceIdentity* deviceIdentity
  ) {
    server = new AsyncWebServer(80);

    // --- Strona główna
    server->on("/", HTTP_GET, [config](AsyncWebServerRequest *req) {
      Serial.println("[HTTP] GET /");
      if (config->isInAPMode()) { req->redirect("/wifi"); return; }
      if (LittleFS.exists("/index.html")) req->send(LittleFS, "/index.html", "text/html");
      else req->send_P(200, "text/html", MAIN_PAGE_HTML);
    });

    // favicon.ico -> spróbuj z favicon.png, inaczej 204
    server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req){
      if (LittleFS.exists("/favicon.png")) {
        req->send(LittleFS, "/favicon.png", "image/png");
      } else {
        req->send(204); // bez treści, bez błędu
      }
    });

    // --- Strona konfiguracji WiFi
    server->on("/wifi", HTTP_GET, [config](AsyncWebServerRequest *req) {
      if (config->isInAPMode()) req->send_P(200, "text/html", WIFI_CONFIG_PAGE_HTML);
      else req->redirect("/");
    });

    // --- Strona OTA (FW) + alias
    server->on("/ota", HTTP_GET, [](AsyncWebServerRequest *req){
      if (!checkAuth(req)) return;
      req->send_P(200, "text/html", OTA_PAGE_HTML);
    });
    server->on("/ota.html", HTTP_GET, [](AsyncWebServerRequest *req){
      if (!checkAuth(req)) return;
      req->send_P(200, "text/html", OTA_PAGE_HTML);
    });

    // --- Strona /fs (menedżer plików)
    server->on("/fs", HTTP_GET, [](AsyncWebServerRequest *req){
      if (!checkAuth(req)) return;
      req->send_P(200, "text/html", FS_MANAGER_HTML);
    });

    // --- API: LISTA PLIKÓW (root)
    server->on("/api/fs/list", HTTP_GET, [](AsyncWebServerRequest *req){
      if (!checkAuth(req)) return;
      JsonDocument doc;
      JsonArray arr = doc["files"].to<JsonArray>();
      File root = LittleFS.open("/");
      if (root) {
        File f = root.openNextFile();
        while (f) {
          JsonObject o = arr.add<JsonObject>();
          o["name"] = String("/") + f.name();
          o["size"] = (uint32_t)f.size();
          f = root.openNextFile();
        }
      }
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- API: UPLOAD POJEDYNCZEGO PLIKU (multipart)
    server->on(
      "/api/fs/upload",
      HTTP_POST,
      [](AsyncWebServerRequest* request){
        if (!checkAuth(request)) return;
        if (_uploadFile) _uploadFile.close();
        request->send(200, "application/json", "{\"ok\":true}");
      },
      [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!checkAuth(request)) return;
        // Ścieżka z form-data "path" (jeśli brak – użyj nazwy pliku)
        String path = "/";
        if (request->hasParam("path", true)) {
          path = request->getParam("path", true)->value();
          if (!path.startsWith("/")) path = "/" + path;
        } else {
          if (!filename.startsWith("/")) path = "/" + filename;
          else path = filename;
        }
        if (index == 0) {
          Serial.printf("[FS] Upload start: %s\n", path.c_str());
          if (LittleFS.exists(path)) LittleFS.remove(path);
          _uploadFile = LittleFS.open(path, "w");
          if (!_uploadFile) { Serial.println("[FS] Nie można otworzyć pliku do zapisu!"); return; }
        }
        if (_uploadFile && len) {
          _uploadFile.write(data, len);
        }
        if (final) {
          if (_uploadFile) _uploadFile.close();
          Serial.println("[FS] Upload koniec");
        }
      }
    );

    // --- API: DELETE (JSON body: {"path":"/index.html"})
    server->on("/api/fs/delete", HTTP_POST, [](AsyncWebServerRequest *req){
      if (!checkAuth(req)) return;

      if (req->contentType() != "application/json") {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Content-Type != application/json\"}");
        return;
      }
      if (!req->hasParam("body", true)) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak body\"}");
        return;
      }

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, req->getParam("body", true)->value());
      if (err) {
        req->send(400, "application/json", String("{\"ok\":false,\"error\":\"Błąd JSON: ") + err.c_str() + "\"}");
        return;
      }

      String path = doc["path"] | "";
      if (path == "") {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak path\"}");
        return;
      }
      if (!path.startsWith("/")) path = "/" + path;

      bool ok = LittleFS.remove(path);
      String resp = String("{\"ok\":") + (ok ? "true" : "false") + "}";
      req->send(ok ? 200 : 404, "application/json", resp);
    });

    // --- API: OTA FIRMWARE (flash) – multipart Update U_FLASH
    server->on(
      "/api/ota",
      HTTP_POST,
      [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (!_otaFwPrepared) {
          const String msg = _otaFwError.length() ? _otaFwError : "Nie przygotowano backupu OTA";
          OtaStateBackup::clear();
          _otaFwError = "";
          request->send(500, "text/plain", "Błąd: " + msg);
          return;
        }
        bool ok = !Update.hasError();
        _otaFwPrepared = false;
        if (ok) {
          _otaFwError = "";
          request->send(200, "text/plain", "OK");
          Serial.println("[OTA] FW OK. Restart...");
          request->client()->close();
          delay(1500);
          ESP.restart();
        } else {
          OtaStateBackup::clear();
          StreamString ss; Update.printError(ss);
          _otaFwError = "";
          request->send(500, "text/plain", "Błąd: " + String(ss.c_str()));
        }
      },
      [config](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!checkAuth(request)) return;
        if (index == 0) {
          _otaFwPrepared = false;
          _otaFwError = "";
          Serial.printf("[OTA] FW start: %s\n", filename.c_str());
          if (!OtaStateBackup::stage(config, "firmware", "", _otaFwError)) {
            Serial.printf("[OTA] backup err: %s\n", _otaFwError.c_str());
            return;
          }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            StreamString ss; Update.printError(ss);
            _otaFwError = String("Update.begin: ") + ss.c_str();
            OtaStateBackup::clear();
            Serial.printf("[OTA] begin err: %s\n", ss.c_str());
            return;
          }
          _otaFwPrepared = true;
        }
        if (!_otaFwPrepared) return;
        if (len) {
          size_t w = Update.write(data, len);
          if (w != len) { StreamString ss; Update.printError(ss); Serial.printf("[OTA] write err: %s\n", ss.c_str()); }
        }
        if (final) {
          if (!Update.end(true)) { StreamString ss; Update.printError(ss); Serial.printf("[OTA] end err: %s\n", ss.c_str()); }
          else Serial.printf("[OTA] FW ok, %u bajtów\n", (unsigned)(index + len));
        }
      }
    );

    // --- API: OTA LITTLEFS IMAGE (FS) – multipart Update U_SPIFFS (działa dla LittleFS w Arduino-ESP32)
    server->on(
      "/api/ota-fs",
      HTTP_POST,
      [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (!_otaFsPrepared) {
          const String msg = _otaFsError.length() ? _otaFsError : "Nie przygotowano backupu OTA";
          OtaStateBackup::clear();
          _otaFsError = "";
          request->send(500, "text/plain", "Błąd: " + msg);
          return;
        }
        bool ok = !Update.hasError();
        _otaFsPrepared = false;
        if (ok) {
          _otaFsError = "";
          request->send(200, "text/plain", "OK");
          Serial.println("[OTA] FS OK. Restart...");
          request->client()->close();
          delay(1500);
          ESP.restart();
        } else {
          OtaStateBackup::clear();
          StreamString ss; Update.printError(ss);
          _otaFsError = "";
          request->send(500, "text/plain", "Błąd: " + String(ss.c_str()));
        }
      },
      [config](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if (!checkAuth(request)) return;
        if (index == 0) {
          _otaFsPrepared = false;
          _otaFsError = "";
          Serial.printf("[OTA] FS start: %s\n", filename.c_str());
          if (!OtaStateBackup::stage(config, "fs", "", _otaFsError)) {
            Serial.printf("[OTA] FS backup err: %s\n", _otaFsError.c_str());
            return;
          }
          // Uwaga: dla LittleFS w Arduino-ESP32 nadal używa się U_SPIFFS do partycji FS
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
            StreamString ss; Update.printError(ss);
            _otaFsError = String("Update.begin: ") + ss.c_str();
            OtaStateBackup::clear();
            Serial.printf("[OTA] FS begin err: %s\n", ss.c_str());
            return;
          }
          _otaFsPrepared = true;
        }
        if (!_otaFsPrepared) return;
        if (len) {
          size_t w = Update.write(data, len);
          if (w != len) { StreamString ss; Update.printError(ss); Serial.printf("[OTA] FS write err: %s\n", ss.c_str()); }
        }
        if (final) {
          if (!Update.end(true)) { StreamString ss; Update.printError(ss); Serial.printf("[OTA] FS end err: %s\n", ss.c_str()); }
          else Serial.printf("[OTA] FS ok, %u bajtów\n", (unsigned)(index + len));
        }
      }
    );

    // --- Rain history (bez i z ukośnikiem)
    server->on("/api/rain-history", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/rain-history");
      JsonDocument doc;
      weather->rainHistoryToJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });
    server->on("/api/rain-history/", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/rain-history/ (trailing slash)");
      JsonDocument doc;
      weather->rainHistoryToJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- Watering percent (bez i z ukośnikiem)
    server->on("/api/watering-percent", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/watering-percent");
      JsonDocument doc;
      weather->irrigationDecisionToJson(doc);
      // wyjaśnienie po polsku:
      doc["explain"] = weather->getWateringDecisionExplain();
      // zostawiamy też prognozy dzienne, jeśli front to gdzieś pokazuje:
      doc["daily_max_temp"] = weather->getDailyMaxTemp();
      doc["daily_humidity_forecast"] = weather->getDailyHumidityForecast();
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });
    server->on("/api/watering-percent/", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/watering-percent/ (trailing slash)");
      JsonDocument doc;
      weather->irrigationDecisionToJson(doc);
      doc["explain"] = weather->getWateringDecisionExplain();
      doc["daily_max_temp"] = weather->getDailyMaxTemp();
      doc["daily_humidity_forecast"] = weather->getDailyHumidityForecast();
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- Skan sieci WiFi (AP config UI)
    server->on("/api/wifi/networks", HTTP_GET, [](AsyncWebServerRequest *req) {
      collectWifiScanResultsIfReady();
      // Uruchom nowy scan jeśli nie mamy jeszcze wyników lub cache jest starszy niż 30s.
      if (!gWifiScanRunning && (gWifiScanCacheCount == 0 || (millis() - gWifiScanLastUpdateMs) > 30000UL)) {
        startWifiScanAsyncIfNeeded();
      }
      // Fallback: jeśli async nie wystartował lub nic nie znalazł, spróbuj raz synchronicznie.
      if (!gWifiScanRunning && gWifiScanCacheCount == 0) {
        runWifiScanSyncFallback();
      }

      JsonDocument doc;
      JsonArray arr = doc["networks"].to<JsonArray>();
      for (int i = 0; i < gWifiScanCacheCount; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = gWifiScanCache[i].ssid;
        o["rssi"] = gWifiScanCache[i].rssi;
        o["secure"] = gWifiScanCache[i].secure;
        o["enc"] = wifiAuthModeToText(gWifiScanCache[i].enc);
      }
      doc["scanning"] = gWifiScanRunning;
      doc["count"] = arr.size();
      doc["apMode"] = WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA;
      doc["scan_age_ms"] = (gWifiScanLastUpdateMs > 0) ? (millis() - gWifiScanLastUpdateMs) : 0;
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- onRequestBody do obsługi JSON POST/PUT (wifi/settings/zones/nazwy/programy)
    server->onRequestBody([config, relays, programs, logs, weather, pushover](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
      String url = request->url();
      auto method = request->method();

      // --- /api/wifi
      if (url == "/api/wifi" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return; }
        String ssid = doc["ssid"] | "";
        String pass = doc["pass"] | "";
        if (ssid == "") { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak SSID\"}"); return; }
        JsonDocument cfg;
        cfg["ssid"] = ssid; cfg["pass"] = pass;
        config->saveFromJson(cfg);
        if (logs) {
          String logMsg = EventMessages::logTitle("WIFI", "zapisano ustawienia");
          EventMessages::appendLogField(logMsg, "ssid", ssid);
          logs->add(logMsg);
        }
        request->send(200, "application/json", "{\"ok\":true}");
        // Dłuższa zwłoka poprawia niezawodność odpowiedzi na iOS/Safari
        // zanim AP zniknie po restarcie.
        delay(2500);
        ESP.restart(); return;
      }

      // --- /api/settings
      if (url == "/api/settings" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return; }
        config->saveFromJson(doc);
        relays->setZoneCount(config->getZoneCount());
        setTimezoneFromWeb();
        weather->applySettings(config->getOwmApiKey(), config->getOwmLocation(), config->getEnableWeatherApi(), config->getWeatherUpdateIntervalMin());
        weather->applySmartIrrigationConfig(buildSmartIrrigationConfig(config));
        mqtt.updateConfig();
        if (logs) logs->add(EventMessages::logSentence("SYSTEM", EventMessages::settingsSaved()));
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/zones (toggle)
      if (url == "/api/zones" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        int id = doc["id"] | -1;
        bool toggle = doc["toggle"] | false;
        if (id < 0 || id >= relays->getZoneCount()) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        if (toggle) {
          bool wasActive = relays->getZoneState(id);
          relays->toggleZone(id);
          bool isActive = relays->getZoneState(id);
          const String zoneObj = EventMessages::zoneObject(relays->getZoneName(id), id + 1);
          if (logs) {
            if (!wasActive && isActive) {
              int secs = relays->getRemainingSeconds(id);
              int mins = (secs + 59) / 60;
              logs->add(EventMessages::logSentence("STREFA", EventMessages::zoneStarted(zoneObj, mins)));
            } else if (wasActive && !isActive) {
              logs->add(EventMessages::logSentence("STREFA", EventMessages::zoneStopped(zoneObj)));
            }
          }
          if (pushover && config && config->getEnablePushover()) {
            if (!wasActive && isActive) {
              int secs = relays->getRemainingSeconds(id);
              int mins = (secs + 59) / 60;
              if (mins < 1) mins = 1;
              EventMessages::PushMessage pushMsg = EventMessages::pushSentence(EventMessages::zoneStarted(zoneObj, mins));
              pushover->send(pushMsg.title, pushMsg.body);
            } else if (wasActive && !isActive) {
              EventMessages::PushMessage pushMsg = EventMessages::pushSentence(EventMessages::zoneStopped(zoneObj));
              pushover->send(pushMsg.title, pushMsg.body);
            }
          }
        }
        JsonDocument resp; relays->toJson(resp);
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
        return;
      }

      // --- /api/zones-names
      if (url == "/api/zones-names" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len) || !doc["names"].is<JsonArray>()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON lub brak tablicy 'names'\"}"); return;
        }
        relays->setAllZoneNames(doc["names"].as<JsonArray>());
        if (logs) logs->add(EventMessages::logSentence("SYSTEM", EventMessages::zoneNamesSaved()));
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/programs (add/edit/import)
      if (url == "/api/programs" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->addFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
      if (url == "/api/programs/import" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->importFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
      // --- PUT /api/programs/<id>
      String prog_prefix = "/api/programs/";
      if (url.startsWith(prog_prefix) && method == HTTP_PUT) {
        int idx = url.substring(prog_prefix.length()).toInt();
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->edit(idx, doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    });

    // --- Status
    server->on("/api/status", HTTP_GET, [config, deviceIdentity](AsyncWebServerRequest *req) {
      JsonDocument doc;
      doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
      doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
      if (deviceIdentity) doc["device_id"] = deviceIdentity->getDeviceId();
      doc["fw_version"] = FirmwareVersionStore::reportedVersion();
      time_t now = time(nullptr);
      struct tm t; localtime_r(&now, &t);
      char buf[32];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min);
      doc["time"] = buf;
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- Device identity (do rejestracji urządzenia na serwerze)
    server->on("/api/device", HTTP_GET, [deviceIdentity](AsyncWebServerRequest *req) {
      if (!deviceIdentity) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Brak modułu identity\"}");
        return;
      }
      JsonDocument doc;
      doc["ok"] = true;
      doc["device_id"] = deviceIdentity->getDeviceId();
      doc["claim_code"] = deviceIdentity->getClaimCode();
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    server->on("/api/device/rotate-claim", HTTP_POST, [deviceIdentity](AsyncWebServerRequest *req) {
      if (!checkAuth(req)) return;
      if (!deviceIdentity) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"Brak modułu identity\"}");
        return;
      }
      bool ok = deviceIdentity->rotateClaimCode();
      JsonDocument doc;
      doc["ok"] = ok;
      doc["device_id"] = deviceIdentity->getDeviceId();
      doc["claim_code"] = deviceIdentity->getClaimCode();
      String out;
      serializeJson(doc, out);
      req->send(ok ? 200 : 500, "application/json", out);
    });

    // --- Weather
    server->on("/api/weather", HTTP_GET, [weather](AsyncWebServerRequest *req) {
      JsonDocument doc; weather->toJson(doc);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Zones
    server->on("/api/zones", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      JsonDocument doc; relays->toJson(doc);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Zones names
    server->on("/api/zones-names", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      JsonDocument doc; JsonArray names = doc["names"].to<JsonArray>();
      relays->toJsonNames(names);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Programs GET/EXPORT/DELETE
    server->on("/api/programs", HTTP_GET, [programs](AsyncWebServerRequest *req){
      JsonDocument doc; programs->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });
    server->on("/api/programs/export", HTTP_GET, [programs](AsyncWebServerRequest *req){
      JsonDocument doc; programs->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });
    server->on("/api/programs", HTTP_DELETE, [programs](AsyncWebServerRequest *req) {
      if (!req->hasParam("id")) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak parametru id\"}"); return; }
      int idx = req->getParam("id")->value().toInt();
      programs->remove(idx);
      req->send(200, "application/json", "{\"ok\":true}");
    });

    // --- LOGS
    if (logs) {
      server->on("/api/logs", HTTP_GET, [logs](AsyncWebServerRequest *req){
        JsonDocument doc; logs->toJson(doc);
        String json; serializeJson(doc, json);
        req->send(200, "application/json", json);
      });
      server->on("/api/logs", HTTP_DELETE, [logs](AsyncWebServerRequest *req){
        logs->clear();
        req->send(200, "application/json", "{\"ok\":true}");
      });
    }

    // --- USTAWIENIA GET
    server->on("/api/settings", HTTP_GET, [config](AsyncWebServerRequest *req){
      JsonDocument doc; config->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // Serwowanie plików statycznych (LittleFS).
    // Wyłączamy automatyczne szukanie *.gz, żeby nie spamować logów błędami open().
    auto& staticHandler = server->serveStatic("/", LittleFS, "/");
    staticHandler.setTryGzipFirst(false);
    server->begin();
    Serial.println("[HTTP] Serwer wystartował na porcie 80");
  }

  void loop() { }
}
