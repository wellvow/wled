# Build your own WELLVOW firmware (DIY code)

Follow these steps to build this firmware yourself and keep using the WELLVOW app with your controller.

## 1. Get your DIY code
1. Install the WELLVOW app (search **WELLVOW** in the App Store or your Android app store).
2. Add the controller to the app while it still runs the factory firmware.
3. Open **Me → DIY Code**, find the controller and tap **Copy DIY Code**. You get a 36-character string like `WV1-842C…4AC5`.

The code belongs to that one controller. For another controller, copy its own code.

## 2. Get the source
```bash
git clone -b diy-0.15.0-b6 https://github.com/wellvow/wled.git
cd wled
```
Install PlatformIO (VS Code extension, or `pip install platformio`).

## 3. Build and flash
Pass the code as a build flag and flash over USB (replace the code and the serial port):
```bash
PLATFORMIO_BUILD_FLAGS='-D WELLVOW_DIY_CODE=\"WV1-YOURCODE\"' pio run -e wellvow_esp32 -t upload --upload-port /dev/cu.usbserial-XXXX
```
Pick the environment for your board:

| Environment | Board |
|---|---|
| `wellvow_esp32` | WLED-STRIP-W705 (ESP32-MINI-1-N4) |
| `wellvow_esp32_h706` | WLED-STRIP-H706 (same board, SM16825 default) |
| `wellvow_s3_n8r2` | ESP32-S3-WROOM-1-N8R2 version |

Alternatively add the flag to the environment's `build_flags` in `platformio_override.ini`:
```
  -D WELLVOW_DIY_CODE=\"WV1-YOURCODE\"
```
Building without the flag stops with `#error` on purpose.

## 4. Check
1. Open the serial monitor at 115200 baud. The boot log must contain `[WLV-DIY] DIY build, code WV1-…`.
2. Reconnect to the controller in the app (no need to delete it). When it connects, you are done.
3. If the app cannot connect: the code is not this controller's code, or it was copied wrong (`WV1-` plus 32 hex characters).

## Board note: N4 vs S3
- **WLED-STRIP-W705 / H706 (ESP32-MINI-1-N4, classic ESP32)**: limited RAM. Use this repository (WLED 0.15.0-b6). Do not build these boards on WLED 16.x: with Bluetooth on and a longer strip (tested with 288 LEDs) the device freezes and reboots in a loop and cannot be used.
- **S3 version (ESP32-S3-WROOM-1-N8R2, with PSRAM)**: supports 288 LEDs with Bluetooth on, also on WLED 16.x (tested 2026-10-25, 10 minutes without a single reset).

## Notes
- The web console (`http://<controller-ip>/wellvow`), Wi‑Fi setup, groups, schedules, Studio uploads and host/follower sync are all included.
- To regenerate the embedded web console after editing `usermods/wellvow_ble/web/wellvow_ctrl.html`, run `python3 usermods/wellvow_ble/web/gen_html.py`.
- Firmware base: WLED 0.15.0-b6, MIT license. Keep the `LICENSE` file when you redistribute.
