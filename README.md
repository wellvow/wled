# H705D / SM6812 firmware package

This branch contains the H705D controller firmware package and macOS flashing, WLED Installer, and pin-assignment instructions.

For the complete procedure, file layout, safety notes, and SHA-256 checksums, see the [H705D macOS flashing guide](docs/H705D-MACOS-FLASHING-GUIDE.md).

## Package contents

- `firmware/SM6812/SK6812.bin` — the H705D/SM6812 application firmware.
- `firmware/SM6812/` — the firmware plus supporting boot, partition, and offset files.
- `docs/H705D-MACOS-FLASHING-GUIDE.md` — macOS USB, esptool, WLED Installer, and GPIO configuration guide.
- `docs/images/wled-led-preferences-gpio.png` — WLED LED Preferences reference image with Data GPIO set to 2.

> Only use this firmware with the intended H705D/SM6812 controller hardware. Flashing an incompatible image may prevent the controller from booting.
