# WM Sprinkler TFT

Wariant `tft/` to firmware WM Sprinkler na `ESP32` rozszerzony o:

- panel lokalny na TFT `ST7789 320x240 (SPI)`
- sterowanie enkoderem `EC11` (obrot + klik)
- nowy interfejs ekranowy: `Panel Glowny`, `Podlewanie Reczne`, `Status Systemu`, `Ustawienia`

## Build (PlatformIO)

Srodowisko domyslne:

- `esp32dev_tft` (`flash 16MB`, OTA + LittleFS)

Budowanie:

```bash
pio run -e esp32dev_tft
pio run -e esp32dev_tft -t buildfs
```

Wgrywanie:

```bash
pio run -e esp32dev_tft -t upload
pio run -e esp32dev_tft -t uploadfs
```

Jesli `pio` nie jest w `PATH`:

```bash
~/.platformio/penv/bin/pio run -e esp32dev_tft
~/.platformio/penv/bin/pio run -e esp32dev_tft -t buildfs
~/.platformio/penv/bin/pio run -e esp32dev_tft -t upload
~/.platformio/penv/bin/pio run -e esp32dev_tft -t uploadfs
```

Artefakty po kompilacji:

- `.pio/build/esp32dev_tft/bootloader.bin`
- `.pio/build/esp32dev_tft/partitions.bin`
- `.pio/build/esp32dev_tft/firmware.bin`
- `.pio/build/esp32dev_tft/littlefs.bin`

## Piny TFT i enkodera

Domyslna mapa pinow (plik `src/TftPanelUI.cpp`):

- `TFT_SCK=18`
- `TFT_MOSI=19`
- `TFT_CS=21`
- `TFT_DC=22`
- `TFT_RST=4`
- `TFT_BL=15`
- `ENC_A=16`
- `ENC_B=17`
- `ENC_BTN=2`

Jesli Twoja plytka ma inny rozklad GPIO, zmien stale w `src/TftPanelUI.cpp`.

## Logo na pasku

Top bar laduje logo z LittleFS:

- `/wm-logo-24.bmp`
- tapeta wygaszenia: `/wm-logo-large-200.bmp`

W projekcie jest juz przygotowany plik:

- `data/wm-logo-24.bmp`
- `data/wm-logo-large-200.bmp`

Mozesz go podmienic wlasnym logo (24x24 BMP, preferowane 32-bit RGBA).

## Sterowanie enkoderem

- `Panel Glowny`: obrot wybiera sekcje, klik otwiera podlewanie reczne, dlugi klik otwiera status.
- `Podlewanie Reczne`: klik przelacza aktywne pole (sekcja/czas/start-stop), obrot zmienia wartosc, dlugi klik wraca.
- `Status Systemu`: klik otwiera `Ustawienia`, dlugi klik wraca do panelu glownego.
- `Ustawienia`: obrot zmienia wartosc aktywnego pola (jasnosc / timeout), klik przelacza pole, dlugi klik wraca.
- po bezczynnosci (1/3/5 min) ekran nie gasnie, tylko schodzi do 15% i pokazuje tapete; ruch lub klik enkodera wybudza podswietlenie.
