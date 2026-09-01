# H705D / SM6812 Firmware Flashing Guide

This branch is intended for the H705D controller. It includes the SM6812 firmware, the ESP-Flasher utility, and the CP210x Windows serial driver.

## Package Contents

| Path | Purpose |
| --- | --- |
| `firmware/SM6812/SK6812.bin` | H705D/SM6812 firmware file to load in ESP-Flasher |
| `firmware/SM6812/bootloader_dout_40m.bin` | Bootloader image |
| `firmware/SM6812/partitions.bin` | Partition table |
| `firmware/SM6812/boot_app0.bin` | Boot app image |
| `firmware/SM6812/FlashOffset.txt` | Flash offsets for the supporting image files |
| `tools/ESP-Flasher.exe` | ESP-Flasher utility for Windows |
| `drivers/CP210x_Universal_Windows_Driver_WIN7_8.zip` | CP210x USB-to-UART driver |

## Before You Start

1. Connect the controller to your Windows computer with a USB data cable.
2. If no COM port appears, extract and install `drivers/CP210x_Universal_Windows_Driver_WIN7_8.zip`, then disconnect and reconnect the controller.
3. Close any software that may be using the serial port, such as a serial monitor, another flashing utility, or a WLED logging window.
4. Confirm that the controller model is H705D. `SK6812.bin` is the firmware file for this controller and is the only `.bin` file that should be selected in ESP-Flasher.

## Method B – Flash with ESP-Flasher

This procedure follows **Method B – Flash with ESP-Flasher** from the Wellvow firmware tutorial and uses the paths in this package:

1. Run `tools/ESP-Flasher.exe`.
2. Select the controller's COM port from the **Serial port** list. If you have just installed the driver, click the refresh button on the right.
3. Click **Browse** beside the **Firmware** field.
4. Select `firmware/SM6812/SK6812.bin`. This is the firmware file required by this procedure. Do not select the bootloader, partition, or boot app files in ESP-Flasher.
5. Click **Flash ESP** to begin flashing.
6. Keep the USB connection in place and wait until progress reaches 100% and the tool reports success.
7. Power-cycle the controller, or disconnect and reconnect its USB cable.

![Example of loading SK6812.bin in ESP-Flasher](images/esp-flasher-load-bin.png)

The numbered labels in the image indicate:

1. Select the correct COM port.
2. Click **Browse** and load `firmware/SM6812/SK6812.bin` from this repository.
3. Click **Flash ESP**.

### Important Notes and Troubleshooting

- Do not disconnect USB or power, and do not close ESP-Flasher while flashing is in progress.
- Use only firmware intended for the H705D/SM6812 controller. An incompatible image may prevent the controller from booting.
- If no COM port is available, install the CP210x driver, try a USB cable that supports data transfer, and check the port in Windows Device Manager.
- If flashing fails, reconnect the controller and try again. If the utility provides a baud-rate option, reduce it to `115200`.
- If the port is reported as busy, close other serial applications before restarting ESP-Flasher.

## IO Pin Assignment (Our Controller Platform)

### 2. Buttons

| Function | GPIO Pin | Description |
| --- | --- | --- |
| Button 0 | GPIO0 | User button 0 |
| Button 1 | GPIO25 | User button 1 |

These buttons can be mapped in WLED to actions such as power toggle, preset cycling, and playlist control. GPIO0 may also affect the ESP32 boot mode, so do not hold Button 0 while powering on or resetting the controller unless you intend to enter download/flashing mode.

### Microphone Interface (Generic I2S PDM)

| Signal | GPIO Pin | Description |
| --- | --- | --- |
| I2S SD / Data In | GPIO35 | PDM microphone data input |
| I2S WS / LRCLK | GPIO27 | PDM clock/word-select signal |
| I2S SCK / Clock | Not used | Not connected in this design |

The microphone uses a PDM I2S input: SD is connected to GPIO35, WS is connected to GPIO27, and SCK is not used by this controller design. GPIO35 is an input-only pin and is suitable for the microphone data input.

## Sources

- Wellvow tutorial: [Firmware Downloads & Controller Updates](https://wellvow.com/forums/discussion/firmware-downloads-controller-updates/)
- The ESP-Flasher operation screenshot was supplied by the firmware package maintainer.

## File Integrity (SHA-256)

After copying or downloading the package, files can be verified in Windows PowerShell with `Get-FileHash -Algorithm SHA256 <file-path>`. The expected values are listed in `SHA256SUMS.txt`.
