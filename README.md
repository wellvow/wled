# H705D / SM6812 firmware package

This branch contains the H705D controller firmware package, the Windows flashing utility, the CP210x USB-to-UART driver, and flashing/pin-assignment instructions.

For the complete procedure, file layout, safety notes, and SHA-256 checksums, see [H705D flashing guide](docs/H705D-FLASHING-GUIDE.md).

## Package contents

- `firmware/SM6812/SK6812.bin` — combined image to select in ESP-Flasher.
- `firmware/SM6812/` — original SM6812 firmware binaries and offset reference.
- `tools/ESP-Flasher.exe` — Windows ESP-Flasher utility.
- `drivers/CP210x_Universal_Windows_Driver_WIN7_8.zip` — CP210x serial driver package.
- `docs/images/esp-flasher-load-bin.png` — annotated ESP-Flasher reference image.

> Only use this firmware with the intended H705D/SM6812 controller hardware. Flashing an incompatible image may prevent the controller from booting.
