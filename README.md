# WM Sprinkler

WM Sprinkler is a firmware for a proprietary ESP32-based irrigation controller. The firmware provides a local web panel, saves schedules in the transmission, and allows for shared or automatic zone control.

## What to flash to a clean ESP32

First run these files:

- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `littlefs.bin`

## Simplest flash (IO Platform)

Build Project:

"beating"
pio run -e esp32dev
pio run -e esp32dev -t buildfs
```

Flash to the board via USB:

"beating"
pio run -e esp32dev -t upload
pio run -e esp32dev -t uploadfs
```

This is the simplest and recommended method. "upload" will flash the bootloader, partitions, and firmware, and "uploadfs" will flash the local web panel to "LittleFS".

If `pio` is not in the `PATH`, it applies to:

,,beating
~/.platformio/penv/bin/pio run -e esp32dev
~/.platformio/penv/bin/pio run -e esp32dev -t buildfs
~/.platformio/penv/bin/pio run -e esp32dev -t upload
~/.platformio/penv/bin/pio run -e esp32dev -t uploadfs
```

## Manually Uploading Files

If you want to flash the firmware, upload these files to the following offsets:

- `0x1000` -> `bootloader.bin`
- `0x8000` -> `partitions.bin`
- `0x10000` -> `firmware.bin`
- `0x2d0000` -> `littlefs.bin`

Example:

,,beating
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 921600 write_flash -z \
0x1000 bootloader.bin \
0x8000 partition.bin \
Firmware 0x10000.bin \
0x2d0000 Littlefs.bin
```

## First boot

After the first boot, the device does not yet have a Wi-Fi network available, it switches to AP mode and an additional local Wi-Fi configuration panel. The AP password is 12345678.

After connecting to the network, access the device panel, allowing configuration and installation of the IP device:

- zone name
- schedule
- operating settings

## Important

- always use files from the same build
- for OTA updates, upload `firmware.bin` first, then `littlefs.bin`

Cloud access: https://wmsprinkler.pl
