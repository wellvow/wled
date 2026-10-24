# WELLVOW STRIP — ESP32-MINI-1-N4

## Build target

The default LED driver is two independent 800 kHz WS2811-compatible RGB
outputs on GPIO2 and GPIO15. Both outputs default to 60 controllable WS2811 ICs
and RGB color order. Many commercial WS2811 strips are physically GRB; if the
red/green test is reversed, change only the color order in WLED LED settings.
Use the pixel count for independently controllable WS2811 IC groups, not the
number of individual LED packages printed on a 3-LED-per-IC 12 V strip.

Use PlatformIO environment `esp32_mini_1_n4_wellvow`.

Hardware defaults inherited from this product source package:

- module: ESP32-MINI-1-N4 (classic ESP32, 4 MB flash)
- LED outputs: GPIO2 and GPIO15
- LED protocol: SM16825 RGB+CCT, 60 pixels per output
- buttons: GPIO0 and GPIO25
- PDM microphone: data GPIO35, word-select GPIO27
- flash: DIO, 40 MHz
- partition: two 0x1F0000 OTA slots plus 64 KiB LittleFS

Verify these GPIO assignments against the final PCB schematic before production.
GPIO0, GPIO2 and GPIO15 are boot strapping pins; external pull resistors and the
LED input circuit must not force an invalid level during reset.

## Local setup and web control

On an unconfigured device, connect to the `WELLVOW-STRIP` access point and open
`http://4.3.2.1/`. The WELLVOW web UI supports power, brightness, RGB/CCT,
effects, presets, Wi-Fi scanning and Wi-Fi credential submission. Advanced WLED
LED, sync and OTA settings remain linked from the Setup tab.

The UI uses the standard WLED JSON endpoints (`/json/si`, `/json/state`,
`/json/cfg`, `/json/net`), so web, LAN integrations and BLE update one WLED
state rather than maintaining separate states.

## BLE compatibility

Advertising name: `WVOW_STRIP_N4`

| Item | UUID |
|---|---|
| Service | `FFF0` |
| APP write (write/write-no-response) | `FFF3` |
| Device notify | `FFF4` |

Frames use the APP's `AA 55 CMD LEN DATA... CHECKSUM` format. Implemented paths:

- XTEA challenge/response authentication (`CMD 0x40`, legacy brand-key path)
- DP get/set and group control (`0x01`–`0x04`)
- power, brightness, speed, direction, WLED effect, RGB, sync groups, pixel
  count, intensity, palette, transition, nightlight, segment and preset DPs
- BLE Wi-Fi scan/provision/query (`CMD 0x42`)
- ledmap BLOB upload (`CMD 0x30`, kind `0x02`) to `/ledmap.json`, with immediate reload
- WSEQ BLOB upload (`CMD 0x30`, kind `0x03`) and local playback through DP `0x1B`
- OPEN-BLE compatible 2.4G remote advertisement reception (`0D .. 55 AA`, key byte 12)

Per-device seed/claim, BLE OTA and unbind are not enabled in this STRIP firmware
target. Firmware upgrades use WLED web OTA.

## Studio video and controller group

WELLVOW Studio imports MP4/MOV/WebM/GIF locally, samples the video at the wiring
coordinates and exports WSEQ v1 (`WLVQ`) with RGB run-length compression. Use a
numeric slot from 1 to 15. One IP uploads a complete layout; comma-separated IPs
upload the per-controller slices created by the Studio device assignment tool.
`Sync play group` starts the same slot on all listed controllers concurrently.

The N4 audio build keeps the PDM audio-reactive module and uses a 64 KiB
filesystem. Studio rejects a sequence larger than 48 KiB to leave room for
configuration, ledmap and filesystem overhead. Reduce FPS, duration or pixel
count if the compressed sequence exceeds that limit.

### PC live video streaming (no animation-size limit)

For long MP4/MOV/WebM/GIF material, run Studio as the Electron desktop app and
click `PC live play`. Studio keeps the source video on the computer, samples the
wiring coordinates for every frame, slices pixels by their assigned controller,
and sends RGB frames to UDP port `21325`. The controller buffers only one frame;
the video is not written to the 64 KiB filesystem, so the 48 KiB WSEQ limit does
not apply.

Enter controller addresses in layout order, separated by commas, for example
`192.168.1.50,192.168.1.51`. Pixels assigned to device 1 go to the first address,
device 2 to the second address, and so on. A shared frame ID and commit packet
give every controller the same presentation deadline. Start at 15-20 FPS for
large layouts; use 25-30 FPS only after checking Wi-Fi packet loss and visual
jitter. `Stop live play` releases WLED realtime mode and returns the controllers
to normal WLED, BLE and audio-reactive control.

This mode requires the desktop app because ordinary browser pages cannot send
raw UDP packets. All controllers and the computer must be on the same 2.4 GHz
LAN, and AP/client isolation must be disabled.

### Desktop live-stream test

1. Import the wiring file in Studio and assign the selected pixels to device 1,
   device 2, etc. Keep the comma-separated IP list in exactly that order.
2. Import a video, choose 15 FPS, then click `PC live play`. Verify that each
   controller renders only its own pixels and that motion crosses controller
   boundaries without a visible frame jump.
3. Increase to 20, 25 and 30 FPS while watching for packet loss or jitter. If it
   appears, reduce FPS or improve Wi-Fi signal rather than uploading the video.
4. Click `Stop live play`, then verify normal web/BLE effects and an
   audio-reactive effect still work.
5. Power-cycle one controller and repeat the test to confirm no video data was
   required in flash storage.

## Two-device LAN sync test

1. Flash the same firmware to two controllers and provision both onto the same
   2.4 GHz Wi-Fi network. Client isolation/guest network must be disabled.
2. In WLED `Config > Sync interfaces`, keep UDP port `21324`, send group `1`
   and receive group `1` on both devices. The WELLVOW build enables direct-change
   UDP transmission by default.
3. Open device A in the APP. Change power, brightness, color, speed and three
   different effects. Device B should follow each change without being selected
   in the APP. Repeat once from device A's web page.
4. Temporarily set device B receive group to `2`; it must stop following group
   `1`. Restore it to `1` and verify synchronization resumes.

## Ledmap upload/read test

Use a ledmap containing `name`, `n`, `map`, `coords` and optionally `segs`.
Import it in the APP for device A. A successful test has all three results:

- opening `http://DEVICE_A_IP/ledmap.json` returns the uploaded JSON, including
  its `coords` and `segs` metadata;
- reopening the STRIP page draws the preview in the uploaded shape;
- selecting multiple effects changes the real effect ID and the preview remains
  arranged in the uploaded shape.

Then reboot device A and repeat the last two checks to verify persistence.

## Build

```powershell
platformio run -e esp32_mini_1_n4_wellvow
```

The generated web header is committed. After editing `wled00/data/index.htm`,
run `npm run build` before compiling the firmware.
