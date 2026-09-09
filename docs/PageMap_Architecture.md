# PageMap

## Architecture and Implementation Specification

**Status:** Initial implementation brief  
**Target:** 7-inch ESP32-S3 touch display with PSRAM and SD storage  
**Primary content:** Preprocessed PDF documents  
**UI framework:** LVGL on ESP-IDF  

## 1. Product goal

Build **PageMap**, a portable document reader that preserves the original layout and detail of PDFs while remaining practical on an ESP32-S3. The device must make full-page documents easy to navigate and allow the user to zoom into graphs, figures, tables, and small text.

The ESP32 does **not** parse or render PDFs. A desktop preprocessing tool converts each PDF into a device-friendly document package containing metadata, thumbnails, and a multi-resolution image tile pyramid. The device loads and decodes only the tiles visible in the current viewport.

This produces an interaction model closer to a maps application than a conventional reflowing e-reader.

## 2. Core principles

1. **Preserve PDF layout.** Do not reflow, reinterpret, or simplify page content.
2. **Precompute expensive work.** Rasterization, margin detection, thumbnails, and tiling occur on a computer.
3. **Bound device memory use.** Decode visible tiles into reusable buffers rather than loading whole high-resolution pages.
4. **Optimize the normal reading path.** Fit-width reading and vertical scrolling should be immediate and predictable.
5. **Make detail inspection first-class.** Pinch, double-tap, and drag should make figures pleasant to inspect.
6. **Degrade gracefully.** Missing or corrupt tiles should produce a visible placeholder, not crash the reader.
7. **Keep format and UI layers separate.** The reader UI consumes a generic packaged-document interface and does not know about PDF internals.

## 3. Scope

### Version 1 must support

- Multiple packaged documents on an SD card
- Library view with title, thumbnail, reading progress, and last-opened state
- Opening, closing, and switching documents
- Full-page and fit-width modes
- Manual zoom and pan
- Page navigation
- Portrait and landscape layouts, if the board supports rotation
- Persistent reading position per document
- Screen brightness control
- Basic reader and application menus
- Tile caching and background prefetch
- Clear loading, missing-file, and corrupt-package states

### Version 1 does not need

- Native PDF parsing on the ESP32
- PDF text selection or search
- Annotations or handwriting
- DRM-protected documents
- General-purpose EPUB rendering
- Cloud synchronization
- Arbitrary CSS or browser functionality
- Animated page turns

Bookmarks, recent-document sorting, figure detection, table-of-contents navigation, and Wi-Fi transfer are suitable follow-on features.

## 4. System architecture

```text
DESKTOP / LAPTOP

PDF
  -> PDF rasterizer
  -> content-bound detection
  -> thumbnails
  -> multi-resolution tile generator
  -> packaged document directory
  -> SD card or optional transfer utility

ESP32-S3 DEVICE

SD card
  -> document catalog
  -> package reader
  -> viewport and tile selector
  -> tile cache
  -> JPEG decoder
  -> page canvas
  -> LVGL interface
  -> RGB LCD and touch controller
```

### Layer responsibilities

| Layer | Responsibility |
|---|---|
| Desktop converter | Rasterize PDFs, detect content bounds, create zoom levels, tiles, thumbnails, and metadata |
| Storage adapter | Enumerate documents and provide bounded file reads from SD |
| Document catalog | Validate packages and expose title, page count, thumbnail, and progress |
| Document model | Provide page dimensions, zoom levels, content bounds, and tile paths |
| Viewport controller | Own current page, scale, viewport origin, fit mode, and gesture transforms |
| Tile manager | Determine visible tiles, schedule reads, prefetch neighbors, and enforce cache limits |
| Image decoder | Decode compressed tiles into RGB565 or the display-native pixel format |
| Page canvas | Composite available tiles and placeholders into the visible page area |
| Application UI | Library, reader chrome, menus, settings, dialogs, and status indicators |
| Persistence service | Save settings and per-document reading state |
| Hardware services | Display, touch, backlight PWM, SD card, battery, rotation, and power state |

## 5. Recommended hardware assumptions

Design against a capability profile instead of hard-coding one board:

- ESP32-S3
- 8 MB PSRAM minimum; 16 MB preferred
- SDMMC preferred, SPI SD acceptable
- 7-inch 800x480 or 1024x600 RGB LCD
- Capacitive touch with at least two touch points if pinch zoom is required
- PWM-controlled LCD backlight
- Battery measurement input when available
- Optional accelerometer or a manual orientation control

Board-specific pin assignments and display initialization must live behind a hardware abstraction module.

## 6. Software stack

- ESP-IDF as the firmware framework
- LVGL for screens, overlays, menus, controls, input dispatch, and the page canvas widget
- Espressif LVGL integration layer where it fits the selected board
- FAT filesystem on SD card
- Hardware JPEG decoder when available; otherwise an incremental software decoder
- NVS for global settings
- SD-side state file or NVS index for per-document progress
- FreeRTOS tasks, queues, and synchronization primitives supplied by ESP-IDF

Avoid performing SD reads or JPEG decoding in the LVGL event callback path.

## 7. Desktop preprocessing pipeline

The converter should be a separate command-line project so it can be tested independently from firmware.

Example command:

```bash
docpack convert input.pdf --output My_Paper --dpi 300 --tile-size 256 --quality 85 --crop auto
```

### Pipeline

1. Open the PDF using a mature desktop PDF engine.
2. Read document metadata and page count.
3. Rasterize each page at a high-detail master resolution, with 300 DPI as the initial default.
4. Detect the bounding rectangle containing meaningful page content.
5. Generate lower-resolution levels until the complete page fits roughly within one tile or the smallest configured overview level.
6. Split every level into fixed-size tiles, initially 256x256 pixels.
7. Encode tiles as baseline JPEG unless a different decoder proves better on the target hardware.
8. Generate a small cover image and optional per-page thumbnails.
9. Write deterministic metadata and a package version number.
10. Validate that every referenced file exists and can be decoded.

### Zoom pyramid

Each page has a master raster and successively downsampled levels. Level numbering should be explicit in metadata; firmware must not assume that `z0` is always the largest or smallest image without reading the documented convention.

Use this convention for version 1:

- `z0`: lowest-resolution overview
- Increasing `z`: increasing detail
- Highest level: approximately the configured source DPI
- Adjacent levels: nominally a factor of two in width and height

The converter may omit redundant levels on unusually small pages.

### Margin cropping

Keep the full rendered page, but record an automatically detected content box. Fit-width mode uses the content box by default. Full-page mode uses the physical page bounds.

Automatic cropping must be conservative. It must not permanently remove content because users need to be able to reveal the entire page.

## 8. Packaged document format

Suggested SD layout:

```text
/documents/
  my-paper/
    manifest.json
    cover.jpg
    pages/
      0001/
        page.json
        thumb.jpg
        z0/
          0_0.jpg
        z1/
          0_0.jpg
          1_0.jpg
        z2/
          0_0.jpg
          1_0.jpg
          0_1.jpg
          1_1.jpg
      0002/
        ...
/reader-state/
  progress.json
```

Use zero-padded page directories so lexical and numeric ordering agree. Tile filenames use `{column}_{row}.jpg`.

### Example `manifest.json`

```json
{
  "format": "esp-docpack",
  "format_version": 1,
  "document_id": "sha256:stable-content-id",
  "title": "Example Research Paper",
  "authors": ["A. Author", "B. Author"],
  "page_count": 12,
  "cover": "cover.jpg",
  "tile_size": 256,
  "tile_format": "jpeg",
  "source": {
    "type": "pdf",
    "filename": "example.pdf"
  },
  "pages": [
    "pages/0001/page.json",
    "pages/0002/page.json"
  ]
}
```

### Example `page.json`

Coordinates are expressed in pixels at the highest-resolution level. Rectangles use `[left, top, right, bottom]`.

```json
{
  "page_number": 1,
  "master_width": 2550,
  "master_height": 3300,
  "content_box": [180, 210, 2370, 3090],
  "thumbnail": "thumb.jpg",
  "levels": [
    {
      "id": "z0",
      "width": 319,
      "height": 413,
      "columns": 2,
      "rows": 2,
      "scale_from_master": 0.125,
      "path": "z0/{column}_{row}.jpg"
    },
    {
      "id": "z1",
      "width": 638,
      "height": 825,
      "columns": 3,
      "rows": 4,
      "scale_from_master": 0.25,
      "path": "z1/{column}_{row}.jpg"
    }
  ]
}
```

The converter and firmware must share a JSON Schema or equivalent validation fixture for these files.

## 9. Reader viewport model

The viewport controller owns:

```text
document_id
page_index
fit_mode: page | width | manual
display_scale
viewport_x
viewport_y
orientation
```

Keep these values independent from LVGL object coordinates. The page canvas converts document coordinates to screen coordinates during drawing.

### Fit page

Calculate the largest scale at which the entire physical page fits inside the usable viewport. Center the page in unused space.

### Fit width

Calculate scale using the content box width, then align its top edge with the top of the viewport. Allow vertical scrolling through the page. Provide an option to include physical margins instead.

### Manual zoom

Select a scale around a focal point. When zooming around touch coordinate `(sx, sy)`, preserve the document coordinate under that point so the content does not jump.

Clamp pan offsets so the page cannot be lost completely off-screen. When the page is smaller than the viewport on an axis, center it on that axis.

### Pyramid level selection

Choose the level whose source-pixel density most closely matches, or slightly exceeds, the requested display density. Prefer mild downscaling over upscaling. During an active pinch, temporarily reuse the current tiles and switch levels after the gesture settles to avoid excessive SD and decode churn.

## 10. Tile manager and memory strategy

For each redraw:

1. Transform the screen viewport into coordinates in the selected pyramid level.
2. Divide the visible bounds by tile size to obtain the inclusive row and column range.
3. Clamp the range to the level's declared grid.
4. Request visible tiles at high priority.
5. Request a one-tile border and likely next reading region at low priority.
6. Cancel or deprioritize stale requests after rapid pan, zoom, or page changes.

### Cache

Use a bounded LRU cache keyed by:

```text
(document_id, page_index, level_id, column, row)
```

Start with two reusable decode buffers plus a cache budget set according to detected PSRAM. A decoded 256x256 RGB565 tile consumes 128 KiB. The implementation should expose cache hits, misses, decode time, and SD read time in a developer overlay.

Prefer this visual loading sequence:

1. Existing tile from the previous scale or overview level
2. Neutral placeholder
3. Newly decoded correct-resolution tile

Never block the interface while waiting for all tiles in a viewport.

## 11. Task model

Suggested tasks:

| Task | Priority | Work |
|---|---:|---|
| UI task | High | LVGL timers, input dispatch, screen transitions, draw invalidation |
| Tile I/O task | Medium | SD reads and request ordering |
| Decode task | Medium | JPEG decode into reusable buffers |
| Persistence task | Low | Debounced settings and reading-progress writes |
| Device task | Low | Battery sampling, inactivity timeout, and power state |

Use queues to pass immutable tile requests and completion messages. Only the UI task should mutate LVGL objects.

## 12. Application screens and menus

### 12.1 Library screen

The default home screen displays available packaged documents.

Required elements:

- Grid or list toggle
- Cover thumbnail or generic document icon
- Title
- Reading progress and current page
- Last-opened indicator
- Sort menu: title, recently opened, date added
- Refresh/rescan action
- Settings entry
- Empty state explaining where document packages belong on the SD card

Selecting a document opens its last saved location. Long-press or overflow menu actions can include:

- Open
- Start from beginning
- View document information
- Remove from recent list

Do not delete document files from version 1 unless a confirmation and recovery strategy are deliberately implemented.

### 12.2 Reader screen

Keep the document content visually dominant. Reader chrome is hidden during normal reading and shown with a single tap in a safe central region.

Top bar:

- Back to library
- Document title, truncated safely
- Current page and page count
- Battery indicator when available
- Overflow menu

Bottom bar:

- Previous page
- Page scrubber or compact page picker
- Next page
- View mode control: fit page, fit width, manual
- Brightness shortcut

Bars should overlay or temporarily reduce the content viewport; choose one behavior consistently so content does not unexpectedly jump. Overlay is preferred for the first implementation.

### 12.3 Quick settings panel

Open from the brightness shortcut or an edge gesture if reliable on the target touch controller.

- Brightness slider
- Orientation: auto, portrait, landscape
- Theme: light, dark UI, system/default
- Keep screen awake toggle
- Touch/gesture sensitivity if the hardware needs tuning

Changing brightness should update the PWM duty cycle immediately and persist only after a short debounce.

`0%` in the UI should map to a safe, visible minimum unless the user explicitly chooses **Turn screen off**. Apply a perceptual curve or lookup table so the slider feels approximately linear to the eye.

Dark UI mode changes controls and surrounding background. It does not invert document tiles in version 1, because inversion can make colored figures and images misleading.

### 12.4 Reader overflow menu

- Go to page
- Fit page
- Fit content width
- Show full margins
- Rotate
- Document information
- Return to library

Future menu items may include bookmark page, view bookmarks, table of contents, and jump to detected figure.

### 12.5 Application settings

- Default opening mode: fit width or fit page
- Default orientation
- Remember zoom per document: on/off
- Brightness
- Sleep timeout
- Tile cache size: automatic by default
- Developer diagnostics: off by default
- About and package-format version

## 13. Touch and physical controls

### Required gestures

| Input | Result |
|---|---|
| Single tap center | Toggle reader chrome |
| Drag | Pan content; in fit-width mode, primarily scroll vertically |
| Pinch | Continuous zoom around gesture centroid |
| Double-tap | Toggle between fit mode and a useful detail zoom around tap point |
| Tap left/right edge | Previous/next page when the gesture is not a pan |

If the touch controller reports only one contact, provide `+` and `-` zoom buttons in the visible reader toolbar and retain double-tap zoom.

Gesture recognition must use movement and time thresholds so a page-turn tap is not triggered at the end of a pan. Cancel an edge tap as soon as movement exceeds the tap threshold.

If the board has physical buttons, map them to page back, page forward, wake, and menu. Physical controls are optional and should use the same command interface as touch actions.

## 14. Page navigation behavior

In fit-page mode, previous and next actions change pages directly.

In fit-width mode:

- Next advances downward by approximately 85% of the viewport height.
- At the bottom of the page, next opens the following page at its top.
- Previous behaves symmetrically.
- Edge taps may turn pages directly if a separate scroll gesture is already clear to users; make this configurable if testing shows accidental turns.

On page change, show an overview or thumbnail immediately while visible tiles load.

## 15. Persistence

Global settings belong in NVS. Per-document state may be stored in a compact SD-side file keyed by stable `document_id`.

Persist:

- Last opened document
- Last opened timestamp
- Current page
- Fit mode
- Normalized viewport position
- Manual zoom, if enabled
- Orientation override
- Reading progress

Store viewport position in normalized page coordinates rather than screen pixels so it survives orientation and display-resolution changes.

Debounce writes and use an atomic replace pattern to reduce SD corruption risk. Also save on document close and before sleep.

## 16. Error handling

Handle these states explicitly:

- SD card missing or mount failed
- No document packages found
- Unsupported package version
- Malformed manifest or page metadata
- Referenced tile missing
- Tile decode failure
- Insufficient memory for the configured cache
- Document removed while open

Package errors should identify the affected document and, when possible, the missing relative path. The device should return safely to the library instead of rebooting.

At startup, skip invalid packages and list them in a diagnostics panel rather than preventing access to valid books.

## 17. Performance targets

Initial targets for a release build on representative hardware:

- Library visible within 2 seconds after SD mount for a modest collection, using a cached index if necessary
- Reader chrome responds within 100 ms even while tiles are loading
- Low-resolution page overview appears within 300 ms after a page turn when cached or readily available
- Visible high-resolution tiles progressively settle within 1 second under normal SD conditions
- No full-page high-resolution allocation
- No crash or watchdog reset during repeated rapid pan, zoom, and page changes
- Brightness changes appear immediate
- Reading position survives restart and unexpected power loss with at most the most recent debounced change lost

Treat these as testable goals, then tune them after measuring the actual display bus, SD card, JPEG decoder, and PSRAM bandwidth.

## 18. Suggested firmware modules

```text
firmware/
  main/
    app_main.c
    app_controller.*
    config.*
  hardware/
    board_config.*
    display_port.*
    touch_port.*
    backlight.*
    battery.*
    storage_mount.*
  documents/
    catalog.*
    manifest.*
    document_model.*
    progress_store.*
  reader/
    viewport.*
    tile_key.*
    tile_manager.*
    tile_cache.*
    jpeg_decoder.*
    page_canvas.*
    gesture_controller.*
  ui/
    screen_library.*
    screen_reader.*
    panel_quick_settings.*
    dialog_go_to_page.*
    dialog_document_info.*
    theme.*
  diagnostics/
    metrics.*
    debug_overlay.*
```

```text
converter/
  docpack/
    cli.*
    pdf_source.*
    crop_detection.*
    pyramid.*
    tile_encoder.*
    metadata.*
    validation.*
  tests/
  schemas/
    manifest-v1.schema.json
    page-v1.schema.json
```

Language and exact file extensions can follow the chosen implementation, but module boundaries should remain recognizable.

## 19. Command interface inside firmware

Route UI actions through explicit application commands rather than directly coupling controls to the renderer:

```text
OPEN_DOCUMENT(document_id)
CLOSE_DOCUMENT
GO_TO_PAGE(page_index)
NEXT_VIEW
PREVIOUS_VIEW
SET_FIT_MODE(mode)
SET_ZOOM(scale, focal_point)
PAN_BY(delta)
SET_BRIGHTNESS(percent)
SET_ORIENTATION(mode)
SHOW_READER_CHROME(visible)
SAVE_READING_STATE
```

This makes touch controls, physical buttons, automated tests, and future remote control share the same behavior.

## 20. Implementation milestones

### Milestone 1: Hardware and UI shell

- Initialize display, touch, PSRAM, backlight, and SD card
- Run LVGL reliably
- Show placeholder library and reader screens
- Implement brightness slider and persistence
- Measure free memory and display refresh behavior

### Milestone 2: Converter and package validation

- Convert a PDF into a versioned package
- Generate overview and high-resolution tile levels
- Record content boxes
- Validate manifests, dimensions, grids, and tile decodability
- Test portrait, landscape, Letter, A4, and mixed-size documents

### Milestone 3: Static page viewer

- Load a package from SD
- Display one overview page
- Change pages
- Implement fit page and fit width
- Save and restore the current document and page

### Milestone 4: Tiled zoom and pan

- Select the correct pyramid level
- Load only visible tiles
- Add bounded cache and reusable decode buffers
- Implement drag, pinch, and double-tap
- Add cancellation/deprioritization for stale requests

### Milestone 5: Document-reader experience

- Scan and display multiple documents
- Add library sorting and document information
- Complete reader chrome, quick settings, page picker, and rotation
- Refine fit-width scrolling and page-boundary behavior
- Add loading placeholders and all error states

### Milestone 6: Hardening

- Profile SD, decode, render, and input latency
- Stress-test rapid navigation and malformed packages
- Test power loss during progress writes
- Tune cache size by detected PSRAM
- Validate touch behavior across the full screen
- Add release logging controls and user-facing diagnostics

## 21. Acceptance criteria for version 1

The build is complete when:

1. A user can place at least two valid packaged documents on an SD card, choose either from the library, and switch between them.
2. Each document opens at its previously saved page and position.
3. A portrait Letter or A4 PDF can be viewed as a full page and in content-fit-width mode without reflow.
4. The user can zoom into a graph until the converter's high-resolution detail is visible, then pan without loading the full page into memory.
5. The user can change pages by touch controls and can jump to a specific page.
6. The user can adjust brightness from a quick panel, and the setting survives restart.
7. Menus provide library navigation, view-mode selection, margin behavior, rotation, page jump, and document information.
8. The interface remains responsive while SD reads and tile decoding occur.
9. Missing or corrupt content displays a recoverable error rather than causing a reboot.
10. A stress test of repeated page changes, pans, and zooms runs for at least 30 minutes without a crash, runaway memory growth, or watchdog reset.

## 22. Early technical decisions to validate on the actual board

Before optimizing the full application, build small measurements for:

- Whether the touch controller reports reliable multi-touch coordinates
- JPEG decode throughput and whether hardware decoding supports the chosen tile encoding
- SDMMC versus SPI SD read latency
- Practical number of decoded tiles that fit alongside LVGL and display buffers
- Whether direct tile drawing, an LVGL canvas, or a custom draw widget performs best
- Display tearing behavior during partial redraws
- Backlight PWM frequency, minimum usable duty cycle, and perceptual mapping
- Rotation cost and whether runtime display rotation is acceptable

These measurements may change cache size, tile encoding, or tile dimensions, but they do not change the overall architecture.

## 23. Future extensions

- Bookmarks and recent locations
- Table-of-contents metadata extracted by the converter
- Detected figure regions with **Zoom to figure** actions
- Optional OCR text layer for search without changing visual rendering
- Highlights and annotations stored as normalized page coordinates
- Wi-Fi document transfer
- Phone or desktop companion app
- Multiple tile profiles for different device resolutions
- Reflowable EPUB renderer as an independent document backend
- Optional night-treatment tiles generated during preprocessing

## 24. Guidance for the coding assistant

Implement vertical slices and keep the system runnable after each milestone. Start with a single hard-coded document package and one page before building library management. Keep board-specific code, package parsing, viewport math, tile loading, and LVGL screens in separate modules.

Do not introduce a PDF engine into the firmware. Do not allocate a high-resolution full-page framebuffer. Do not let storage or decode work run synchronously inside LVGL callbacks. Add timing and memory instrumentation early, because the correct cache and buffer choices depend on the actual board.

The first proof should answer four questions:

1. Can the board decode and display one 256x256 tile quickly enough?
2. Can it composite a viewport while preserving UI responsiveness?
3. Does the touch hardware provide usable pinch data?
4. Can fit-width reading and zoomed figure inspection feel natural on the physical 7-inch display?

If those pass, the remaining work is primarily application engineering rather than document-rendering research.
