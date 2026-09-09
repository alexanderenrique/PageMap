# Bring-up checklist (Milestone 1 / 6)

Use this after flashing firmware to the Waveshare ESP32-S3-Touch-LCD-7.

## Boot

- [ ] Display shows library screen within a few seconds
- [ ] Serial log shows CH422G, RGB panel, GT911, and (if present) SD mount
- [ ] `log_memory` lines print internal heap and PSRAM free sizes
- [ ] Tile cache budget is chosen from free PSRAM

## Touch

- [ ] Single-finger drag pans in the reader
- [ ] Center tap toggles chrome
- [ ] Edge taps change page / scroll
- [ ] Double-tap toggles fit-width vs detail zoom
- [ ] `+` / `-` buttons zoom when multi-touch is awkward
- [ ] Confirm GT911 reports two contacts (serial / future overlay) for pinch readiness

## Storage + decode

- [ ] With packages under `/documents/`, library lists titles
- [ ] Opening a document shows overview tiles within ~300 ms when cached
- [ ] Debug overlay (quick settings) shows SD read and JPEG decode averages
- [ ] Missing tile paths log a warning and show gray placeholders (no reboot)

## Persistence

- [ ] Brightness slider updates dim overlay immediately and survives reboot
- [ ] Closing a document or waiting for debounce writes `/reader-state/progress.json`
- [ ] Re-open restores page index and approximate position
- [ ] Power-cycle mid-read loses at most the last debounced write

## Stress

- [ ] Rapid pan / zoom / page-change for 30 minutes: no watchdog, no runaway heap growth
- [ ] Malformed package directory is skipped; valid books remain readable
