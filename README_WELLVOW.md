# WELLVOW WLED-STRIP firmware (DIY edition)

Firmware for WELLVOW WLED-STRIP controllers (W705 / H706 on ESP32-MINI-1-N4, and the ESP32-S3 version).
It is [WLED](https://github.com/wled/WLED) **0.15.0-b6** (MIT) plus the `usermods/wellvow_ble/` usermod:
Bluetooth LE control for the WELLVOW app, the web console at `/wellvow`, sunrise/sunset scenes, output layout and
strip-type settings, and the product defaults.

## Build it yourself
Every controller has a **DIY code** (`WV1-…`, shown in the app under *Me → DIY Code*). Pass it when building:

```bash
git clone -b diy-0.15.0-b6 https://github.com/wellvow/wled.git
cd wled
PLATFORMIO_BUILD_FLAGS='-D WELLVOW_DIY_CODE=\"WV1-YOURCODE\"' pio run -e wellvow_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX
```

Step-by-step guide: [docs/DIY.md](docs/DIY.md).

## What is included
- Bluetooth control, Wi‑Fi setup, web console, groups, schedules, Studio uploads, host/follower sync.

## Layout
- `usermods/wellvow_ble/wellvow_ble.h` – the usermod (BLE, HTTP API, scenes, defaults)
- `usermods/wellvow_ble/wellvow_board.h` – board pins (N4 / S3)
- `usermods/wellvow_ble/web/` – web console source; `gen_html.py` regenerates `wled00/html_wellvow.h`
- `platformio_override.ini` – the `wellvow_*` build environments
- `WELLVOW_BASE_0.15.0-b6.md` – changes against upstream WLED

License: MIT (see `LICENSE`).
