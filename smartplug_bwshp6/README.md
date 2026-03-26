# WMS Smart Plug FW for BW-SHP6

Firmware for BlitzWolf BW-SHP6 (ESP8266) integrated with WM Sprinkler cloud/MQTT:
- relay control as `zone 0`
- schedule support (`programs`)
- energy telemetry (`power/voltage/current/energy`)
- pairing state machine with LED patterns

## Hardware assumptions (WYS-01-033-WIFI_V1.4)
- MCU: ESP8266
- Relay: `GPIO15`
- LED (active-low): `GPIO0`
- Button (active-low): `GPIO13`
- HLW8012/BL0937: `CF=GPIO5`, `CF1=GPIO4`, `SEL=GPIO12`

You can change `CF1` by build flag if needed:

```ini
build_flags =
  -D WMS_BWSHP6_CF1_GPIO=4
```

## Build

```bash
"$HOME/.platformio/penv/bin/pio" run -d "/Users/pawelwitkowski/Documents/New project/smartplug_bwshp6" -e bw_shp6
```

Output binary:
`/Users/pawelwitkowski/Documents/New project/smartplug_bwshp6/.pio/build/bw_shp6/firmware.bin`

## LED states
- `fast blink` -> unassigned to WMS
- `slow pulse` -> pairing mode
- `relay mirror` (assigned): ON when relay ON, OFF when relay OFF

## Button behavior
- short press: relay toggle
- hold `3s`: enter pairing mode (Wi-Fi provisioning)
- hold `10s`: factory reset Wi-Fi + assignment

## Pairing flow (Wi-Fi provisioning)

Important: BW-SHP6 uses ESP8266 and does **not** support BLE.
Provisioning is done through Wi-Fi.

1. Plug device into socket: LED fast blinks (`unassigned`).
2. Hold button for `3s`: LED changes to slow pulse (`pairing mode`).
3. Device opens AP:
   - SSID: `WMS_PLUG_XXXXX`
   - Password: `12345678`
4. Provision Wi-Fi credentials to plug (from local network agent/app/controller) via:
   - `POST /api/pair/provision` on device AP IP `192.168.4.1`
   - JSON body example:

```json
{
  "ssid": "TwojaSiecWiFi",
  "pass": "TwojeHasloWiFi",
  "mqttServer": "wmsprinkler.pl",
  "mqttPort": 8883,
  "mqttUser": "wms_device",
  "mqttPass": "9521mycode"
}
```

5. Device restarts, joins home Wi-Fi, publishes `global/status` with `device_id` and `claim_code`.
6. In cloud `/devices`, claim device to user account.
7. Cloud sends `cmd/pair/claim`; plug switches to `assigned` state.
8. In panel, device can be controlled and linked to schedules (`programs`).

## Main cloud MQTT topics used
- `wms/<device_id>/global/status`
- `wms/<device_id>/zones`
- `wms/<device_id>/programs`
- `wms/<device_id>/plug/telemetry`
- commands: `wms/<device_id>/cmd/#`
- ack: `wms/<device_id>/ack`

## Local HTTP API (device)
- `GET /api/status`
- `GET /api/device`
- `GET /api/plug/telemetry`
- `GET /api/zones`
- `GET /api/programs`
- `POST /api/programs`
- `DELETE /api/programs/<id>`
- `GET /api/settings`
- `POST /api/settings`
- `POST /api/relay/toggle`
- `POST /api/relay/set`
- `GET /api/pair/state`
- `POST /api/pair/start`
- `POST /api/pair/provision`
