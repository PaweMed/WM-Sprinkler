# WM Sprinkler

WM Sprinkler to autorski sterownik nawadniania na `ESP32`. Firmware udostępnia lokalny panel WWW, zapisuje harmonogramy w urządzeniu i pozwala sterować strefami ręcznie albo automatycznie.

## Co jest w repo

- `src/` - firmware dla `ESP32`
- `smartplug_bwshp6/` - firmware dla smart gniazdka `BW-SHP6` (`ESP8266`)
- `data/` - pliki lokalnego panelu WWW (`LittleFS`)
- `cloud/` - opcjonalny backend i panel cloud
- `scripts/` - skrypty pomocnicze
- `dist/` - przykładowe gotowe pliki `.bin`
- `platformio.ini` - konfiguracja PlatformIO

## Co wgrać na czyste ESP32

Na pierwsze uruchomienie potrzebujesz tych plików:

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `littlefs.bin`

Jeśli budujesz projekt lokalnie, znajdziesz je po kompilacji w:

- `.pio/build/esp32dev/bootloader.bin`
- `.pio/build/esp32dev/partitions.bin`
- `.pio/build/esp32dev/firmware.bin`
- `.pio/build/esp32dev/littlefs.bin`

## Najprostsze wgranie (PlatformIO)

Zbuduj projekt:

```bash
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

Wgraj na płytkę po USB:

```bash
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs
```

To jest najprostsza i zalecana metoda. `upload` wgra bootloader, partycje i firmware, a `uploadfs` wgra lokalny panel WWW do `LittleFS`.

Jeśli `pio` nie jest w `PATH`, użyj:

```bash
~/.platformio/penv/bin/pio run -e esp32dev
~/.platformio/penv/bin/pio run -e esp32dev -t buildfs
~/.platformio/penv/bin/pio run -e esp32dev -t upload
~/.platformio/penv/bin/pio run -e esp32dev -t uploadfs
```

## Ręczne wgranie plików

Jeśli chcesz flashować ręcznie, wgraj te pliki pod takie offsety:

- `0x1000` -> `bootloader.bin`
- `0x8000` -> `partitions.bin`
- `0x10000` -> `firmware.bin`
- `0x2d0000` -> `littlefs.bin`

Przykład:

```bash
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 921600 write_flash -z \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin \
  0x2d0000 littlefs.bin
```

## Pierwsze uruchomienie

Po pierwszym starcie, jeśli urządzenie nie ma jeszcze zapisanej sieci Wi-Fi, przejdzie w tryb AP i pokaże lokalny panel konfiguracji Wi-Fi. Po podłączeniu do sieci możesz wejść na panel urządzenia z przeglądarki i ustawić:

- nazwy stref
- harmonogramy
- ustawienia pracy

## Ważne

- zawsze używaj plików z tego samego buildu
- przy aktualizacji OTA wgrywaj najpierw `firmware.bin`, potem `littlefs.bin`
- repo nie zawiera sekretów ani plików produkcyjnych (`.env`, hasła, tokeny)
