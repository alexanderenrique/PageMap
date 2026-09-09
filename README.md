# PageMap

Open-source **ESP32 PDF reader** that treats pages like a map. Original layout, pan and zoom into figures — no reflow.

Portable reader for the **Waveshare ESP32-S3-Touch-LCD-7** (800×480). PDFs are never parsed on the device. A desktop tool converts each PDF into a tiled multi-resolution package; the firmware streams only the tiles visible on screen.

Architecture: [docs/PageMap_Architecture.md](docs/PageMap_Architecture.md)

## Repository layout

| Path | Purpose |
|------|---------|
| `converter/` | Python CLI `docpack` — PDF → PageMap package (`esp-docpack`) |
| `schemas/` | Shared JSON Schema for `manifest.json` / `page.json` |
| `firmware/` | ESP-IDF + LVGL 9 reader firmware |
| `docs/` | Architecture specification |

## Hardware

- Waveshare ESP32-S3-Touch-LCD-7
- 800×480 RGB LCD, GT911 capacitive touch (5-point)
- CH422G I/O expander (backlight, resets, SD CS)
- SPI microSD (MOSI 11 / SCK 12 / MISO 13, CS via expander)
- 8 MB octal PSRAM

**Note:** Backlight is on/off only. The brightness slider uses a software dim overlay.

## Convert a PDF

```bash
cd converter
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"

docpack convert ../your.pdf --output ../packages/My_Paper --dpi 300 --tile-size 256 --quality 85
docpack validate ../packages/My_Paper
```

Copy the package to the SD card:

```text
/documents/My_Paper/manifest.json
/documents/My_Paper/cover.jpg
/documents/My_Paper/pages/...
```

Progress is stored at `/reader-state/progress.json`.

## SD card: format, partitions, and `/sdcard`

The reader does **not** load the whole card into RAM. The ESP32-S3 streams JPEG tiles from the filesystem into PSRAM as you pan and zoom. Card size is library capacity; the 8 MB of PSRAM is the working set.

### What `/sdcard` is

`/sdcard` is a **firmware mount point**, not the volume name of the card. When ESP-IDF attaches the FAT filesystem, C APIs (`fopen`, `opendir`) see it at `/sdcard`. On a computer the card can be named anything (`AED-BOOK`, `NO NAME`, …) and appears as `/Volumes/YOUR_SD/` on macOS.

So firmware `/sdcard/documents/My_Paper` is the same folder as `documents/My_Paper` at the root of the card.

### Partitions vs. the whole card

An SD card is a block device: the controller exposes the **entire** capacity as numbered sectors. A partition table (usually MBR) is only a map that says “this filesystem occupies sectors X through Y.” The firmware mounts the **first FAT partition** and ignores the rest.

That is why a 64 GB card can look like ~500 MB on the device. It is not an ESP32 limit. It happens when the card still has leftover partitions from another use (Raspberry Pi, Linux, cameras, etc.). Example of a card that would only give the reader 537 MB:

```text
disk4     64 GB card
  s1      FAT32     ~537 MB    ← mounted; this is all the ESP sees
  s2      Linux     ~9 GB      ← macOS will not mount this; it will not show up in /Volumes
          free      rest
```

Finder and `ls /Volumes` only list **mounted** volumes. Use `diskutil list` on macOS to see every partition, including ones the Mac cannot mount.

If you reformat the card as **one FAT32 volume covering the whole device**, the reader can use the full ~64 GB. There is a good reason to use a large card: tiled packages are bulky (multi-resolution JPEGs per page). A few papers fit in hundreds of megabytes; a real library wants tens of gigabytes. Speed class still matters more than capacity for page-turn latency.

### Required format

| Requirement | Why |
|-------------|-----|
| **Single partition**, whole card | Firmware mounts the first FAT volume only |
| **FAT32** (`MS-DOS FAT`) | This firmware’s FatFs build does not include exFAT |
| **MBR** partition map | ESP-IDF SD FAT mount expects MBR (or a superfloppy card with no table), not GPT |
| Volume name anything | Does not have to be `sdcard` |

Cards larger than 32 GB (SDXC) are often formatted **exFAT** by Disk Utility / Windows by default. The ESP will fail to mount those. Format explicitly as FAT32.

macOS (replace `diskN` with the identifier from `diskutil list`; this **erases** the card):

```bash
diskutil list                          # confirm the SD device, e.g. disk4
diskutil unmountDisk /dev/diskN
diskutil eraseDisk FAT32 AEDBOOK MBRFormat /dev/diskN
mkdir /Volumes/AEDBOOK/documents
cp -R processed-docs/My_Paper /Volumes/AEDBOOK/documents/
```

FAT32 volume names are limited to 11 characters and may drop hyphens (`AED-BOOK` → `AEDBOOK`). After the first boot the firmware creates `/reader-state/` itself.

## Build firmware (PlatformIO)

```bash
cd firmware
pio run                 # build
pio run -t upload       # flash
pio run -t upload -t monitor
```

See [firmware/README.md](firmware/README.md) for ports, SD layout, and board notes.
Native `idf.py` still works if you prefer a standalone ESP-IDF install.

## Milestone status

1. Hardware + LVGL shell + tile decode path
2. Desktop converter + schema validation
3. Static page viewer (fit page / fit width)
4. Tiled zoom/pan, cache, gesture controls
5. Multi-document library + reader chrome
6. Diagnostics, atomic progress writes, cache sizing hooks

## Acceptance smoke test

1. Place two packages under `/documents/` on SD
2. Open each from the library; confirm restored page after reboot
3. Fit-page and fit-width reading without reflow
4. Zoom into a figure; pan without allocating a full-page bitmap
5. Go-to-page, brightness dim overlay persists across restart
6. UI stays responsive while tiles load
