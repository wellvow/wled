# WELLVOW WLED-STRIP firmware (DIY edition)

This is the firmware that runs on WELLVOW WLED-STRIP controllers (W705 / H706, ESP32-MINI-1-N4, and the S3 variant).
It is [WLED](https://github.com/wled/WLED) **0.15.0-b6** (MIT, Copyright 2016-2025 Christian Schwinne) plus the
`usermods/wellvow_ble/` usermod that adds the Bluetooth LE control protocol used by the WELLVOW app, the simplified
web console (`/wellvow`), sunrise/sunset scenes, output-layout and strip-type settings and a few product defaults.

## Building a DIY firmware that still works with the app
The factory firmware carries a brand key that is not in this repository. Instead, every controller has a **DIY code**
(`WV1-…`, shown in the app under *Me → DIY code*) that is bound to the chip's MAC address. Pass it at build time:

```bash
PLATFORMIO_BUILD_FLAGS='-D WELLVOW_DIY_CODE=\"WV1-YOURCODE\"' pio run -e wellvow_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX
```

Full walkthrough (Chinese): [docs/DIY.md](docs/DIY.md). Building without `WELLVOW_DIY_CODE` stops with `#error` on purpose.

## What is not here
The HUB (signal capture / multi-controller distribution) features of the WELLVOW HUB product line are not part of this
repository. Host/follower sync over Wi‑Fi, groups, schedules, Studio uploads and the web console are included.

## Layout
- `usermods/wellvow_ble/wellvow_ble.h` – the usermod (BLE, HTTP API, scenes, defaults)
- `usermods/wellvow_ble/wellvow_board.h` – board pins (N4 / S3)
- `usermods/wellvow_ble/web/` – web console source; `gen_html.py` regenerates `wled00/html_wellvow.h`
- `platformio_override.ini` – the `wellvow_*` build environments
- `WELLVOW_BASE_0.15.0-b6.md` – list of changes against upstream WLED

License: MIT (see `LICENSE`).
