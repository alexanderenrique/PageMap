# docpack

Desktop preprocessor for **PageMap**, the ESP32 portable document reader.

Converts a PDF into a versioned tile package (`esp-docpack` format) with:

- Multi-resolution JPEG tile pyramids
- Content-box metadata for fit-width reading
- Cover and per-page thumbnails
- Schema-validated manifests

## Install

```bash
cd converter
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

## Convert

```bash
docpack convert ../lec17.pdf --output My_Paper --dpi 300 --tile-size 256 --quality 85 --crop auto
```

Copy the resulting directory to the SD card under `/documents/` (card must be a single FAT32 volume; see the root [README](../README.md#sd-card-format-partitions-and-sdcard)):

```text
/documents/My_Paper/manifest.json
/documents/My_Paper/cover.jpg
/documents/My_Paper/pages/...
```

## Validate

```bash
docpack validate /path/to/My_Paper
```
