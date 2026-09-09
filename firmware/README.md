# PageMap Firmware (PlatformIO + ESP-IDF)

Firmware for **PageMap** on the **Waveshare ESP32-S3-Touch-LCD-7**.

## Requirements

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) (`pio`)
- USB cable to the board's UART Type-C port
- microSD formatted as a **single FAT32 partition** (whole card), with packages under `/documents/` — see [SD layout](#document-layout-on-sd) and the root [README SD section](../README.md#sd-card-format-partitions-and-sdcard)

## Build

```bash
cd firmware
pio run
```

## Flash

```bash
pio run -t upload
```

## Serial monitor

```bash
pio device monitor
# or combined:
pio run -t upload -t monitor
```

List serial ports if upload fails:

```bash
pio device list
```

Then set `upload_port` / `monitor_port` in `platformio.ini`.

## Document layout on SD

`/sdcard` is the firmware VFS mount point for the **first FAT partition**, not the card’s volume label. On a computer, create these folders at the card root:

```text
/documents/<package-name>/manifest.json
/documents/<package-name>/pages/...
/reader-state/progress.json   (auto-created)
```

```bash
mkdir -p /Volumes/YOUR_SD/documents
cp -R ../processed-docs/My_Paper /Volumes/YOUR_SD/documents/
```

Format the card as **one FAT32 / MBR volume that fills the whole device**. Extra partitions (Linux, leftover Raspberry Pi layouts, exFAT) are invisible to this firmware. A 64 GB card with a 512 MB FAT32 slice will only give the ESP ~512 MB. Details and a `diskutil` recipe: [../README.md](../README.md#sd-card-format-partitions-and-sdcard).

## Configuration

- Board options: `platformio.ini` + `boards/waveshare_esp32s3_touch_lcd_7.json`
- ESP-IDF Kconfig defaults: `sdkconfig.defaults` (octal PSRAM 80 MHz, 8 MB flash, LVGL fonts)
- App options: `pio run -t menuconfig` → **PageMap**

## First flash notes

1. Keep Flash/PSRAM at **80 MHz** unless your module is proven stable at 120 MHz.
2. Backlight is expander on/off; brightness is a software dim overlay.
3. Use `waveshare_lcd7_debug` env for a debug-oriented build:
   ```bash
   pio run -e waveshare_lcd7_debug -t upload
   ```
4. Bring-up steps: [../docs/BRINGUP_CHECKLIST.md](../docs/BRINGUP_CHECKLIST.md)

## Architecture notes

- **Tiles**: JPEG decode and SD I/O run in FreeRTOS tasks, not LVGL callbacks.
- **LVGL**: `esp_lvgl_adapter` (via `idf_component.yml`), with `esp_lvgl_port` fallback.
- **Cache**: Sized from free PSRAM at boot.

See [../docs/PageMap_Architecture.md](../docs/PageMap_Architecture.md).
