# WMS Cloud (MVP)

Minimalny projekt chmurowy do sterowania ESP32 przez internet:
- logowanie/rejestracja użytkownika,
- przypisanie urządzenia (`device_id` + `claim_code`),
- panel web o tym samym wyglądzie co lokalny (ten sam bundle z `data/`),
- dwukierunkowa komunikacja przez MQTT,
- ACK komend MQTT (accepted/failed + timestamp),
- healthcheck cloud + mqtt.

## 1) Start lokalny

```bash
cd cloud
cp .env.example .env
npm install
npm start
```

Panel: `http://localhost:8080/login`

## 2) Konfiguracja ESP32 do chmury

W ustawieniach MQTT na urządzeniu ustaw:
- `mqttServer`: adres brokera (publiczny),
- `mqttPort`: `8883` (TLS) lub `1883` (test),
- `mqttUser` / `mqttPass`: wg brokera,
- `mqttTopic`: `wms/WMS_12345` (gdzie `WMS_12345` to kod urządzenia).

To musi odpowiadać `MQTT_TOPIC_PREFIX` w backendzie (`wms`).

## 3) Claim urządzenia

Po zalogowaniu:
1. Wejdź w `/devices`.
2. Wpisz `device_id` i `claim_code` (obecnie ten sam kod, np. `WMS_12345`).
3. Wybierz urządzenie jako aktywne.
4. Otwórz panel (`/`) i steruj przez internet.

Ważne:
- Claim działa tylko, gdy urządzenie jest online (serwer dostaje live status przez MQTT).
- Jedno urządzenie może być przypisane tylko do jednego konta.
- Gdy urządzenie jest offline, endpointy danych zwracają błąd `503` (brak danych live).
- Konto bez przypisanego urządzenia jest automatycznie usuwane po 30 dniach braku logowania (konfigurowalne przez `STALE_UNASSIGNED_USER_RETENTION_DAYS`).
- Housekeeping bazy (w tym auto-cleanup kont) uruchamia się cyklicznie; interwał ustawisz przez `DB_HOUSEKEEPING_INTERVAL_MS` (domyślnie 6h).

## 3a) Panel administratora

W `.env` ustaw:
- `ADMIN_EMAILS=twoj@email.pl`

Po zalogowaniu kontem z tej listy otwiera się panel `/admin`, gdzie widzisz:
- login użytkownika,
- `device_id`,
- status urządzenia (`online/podlewa/bezczynny/offline`),
- czas ostatniego połączenia.

Panel ma też zakładkę `Aktualizacje` (MVP):
- upload pliku `.bin` z metadanymi wersji/kanału/opisu,
- wybór hardware firmware (`ESP32` albo `ESP32-C6`),
- tworzenie kampanii OTA (`wszystkie online` albo `wybrane urządzenia`),
- podgląd statusów kampanii i anulowanie kampanii.

Dla OTA ustaw też:
- `PUBLIC_BASE_URL=https://wmsprinkler.pl`
- `OTA_SIGN_PRIVATE_KEY_PATH=/var/www/wmsprinkler/secrets/ota_ed25519_private.pem`
  (alternatywnie `OTA_SIGN_PRIVATE_KEY_PEM_B64`)

Cloud generuje URL firmware w formie:
- `/fw/<firmware_id>.bin`

Podpis OTA (Ed25519) jest teraz wykonywany automatycznie przez backend przy uploadzie firmware.
Podczas tworzenia kampanii backend wysyła do urządzenia komplet pól:
- `target` (`firmware`/`fs`)
- `hardware` (`esp32`/`esp32c6`)
- `sha256`
- `signature`
- `signature_alg=ed25519`

Nie trzeba ręcznie doklejać JSON z podpisem.
Cloud blokuje kampanię OTA, jeśli hardware firmware nie pasuje do hardware urządzenia.

Urządzenie odbiera komendę MQTT:
- `<base>/cmd/ota/start`
i publikuje postęp:
- `<base>/ota/status`

## 4) Co działa w MVP

- Odczyt statusu/pogody/stref/harmonogramów/logów z retained MQTT.
- Komendy:
  - ręczne przełączanie stref,
  - import/edycja/usuwanie programów,
  - czyszczenie logów,
  - zapis ustawień.
- Każda komenda z panelu czeka na ACK urządzenia (`cmd -> ack`).
- Przejściowo możesz wyłączyć twardy wymóg ACK (`REQUIRE_CMD_ACK=false`) na czas migracji firmware.

## 5) Monitoring i alerty

1. Rotacja logów PM2:
```bash
cd /var/www/wmsprinkler/cloud
sudo -u deploy ./scripts/setup-pm2-logrotate.sh
```

2. Health endpoint:
- `GET /health` (HTTP 200 = cloud+mqtt ok, HTTP 503 = problem)

3. Cron healthcheck co 1 min:
```bash
cd /var/www/wmsprinkler/cloud
sudo ALERT_WEBHOOK_URL="https://twoj-webhook" ./scripts/setup-healthcheck-cron.sh
```

## 6) Polityka fail-safe

- Gdy internet/MQTT padnie, ESP32 nadal działa lokalnie:
  - lokalny harmonogram i logika podlewania pracują dalej,
  - po powrocie MQTT następuje ponowna synchronizacja cloud.
- W logach firmware pojawiają się wpisy `[FAILSAFE]`.

## 7) Tryb testowy bez ESP32

Możesz uruchomić symulator urządzenia po MQTT:

```bash
cd /var/www/wmsprinkler/cloud
MQTT_URL=mqtt://127.0.0.1:1883 MQTT_USERNAME=wms_device MQTT_PASSWORD=TU_HASLO npm run sim
```

Domyślnie symulator używa:
- `SIM_DEVICE_ID=WMS_99999`
- `SIM_CLAIM_CODE=WMS_99999`

W panelu `/devices` przypisz ten `device_id` i `claim_code`, a potem możesz testować pełne UI i komendy z ACK.

## 8) Ważne uwagi

- To jest MVP pod szybkie uruchomienie. Przed produkcją:
  - ustaw silny `JWT_SECRET`,
  - włącz HTTPS na domenie,
  - skonfiguruj bezpieczny broker MQTT (TLS + auth),
  - rozważ osobny, losowy `claim_code` zamiast równego `device_id`.

## 9) Home Assistant: izolacja per urządzenie (bez podglądu obcych danych)

Cloud generuje oddzielne konto MQTT dla każdego urządzenia (`ha_<device_id>`), a ACL ogranicza dostęp tylko do:
- `wms/<device_id>/#` (odczyt stanu),
- `wms/<device_id>/cmd/#` + `wms/<device_id>/global/refresh` (sterowanie),
- discovery tylko dla `node_id=<device_id>`.

Uwaga: ta izolacja discovery wymaga firmware, który publikuje discovery w formacie:
`homeassistant/<component>/<device_id>/<object_id>/config`.

### 9.1 Wygeneruj pliki auth/ACL dla Mosquitto

Na serwerze:

```bash
cd /var/www/wmsprinkler/cloud
sudo WMS_DB_PATH=/var/www/wmsprinkler/cloud/storage/db.json \
  WMS_HA_PASSWD_FILE=/etc/mosquitto/passwd \
  WMS_HA_ACL_FILE=/etc/mosquitto/wms-ha.acl \
  node scripts/sync-ha-mqtt-auth.js
sudo systemctl reload mosquitto
```

### 9.2 Podłącz ACL do brokera

W `/etc/mosquitto/conf.d/wms.conf` upewnij się, że masz:

```conf
acl_file /etc/mosquitto/wms-ha.acl
password_file /etc/mosquitto/passwd
```

Po zmianie:

```bash
sudo systemctl restart mosquitto
```

### 9.3 Auto-sync po claim/rotacji hasła (opcjonalnie)

W `cloud/.env`:

```env
MQTT_AUTH_SYNC_HOOK=sudo /var/www/wmsprinkler/cloud/scripts/sync-ha-mqtt-auth.sh
```

Następnie:

```bash
cd /var/www/wmsprinkler/cloud
sudo -u deploy pm2 restart wms-cloud --update-env
```

Po stronie użytkownika dane HA są dostępne po zalogowaniu do panelu urządzenia (`/`) w zakładce **Ustawienia** w sekcji `Home Assistant (MQTT)`.
W tej sekcji są też akcje:
- `Test MQTT/ACL` - sprawdza logowanie i uprawnienia konta `ha_<device_id>`,
- `Wymuś rediscovery` - publikuje `homeassistant/status=online` oraz `wms/<device_id>/global/refresh`.
