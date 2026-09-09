#pragma once

#include "lvgl.h"

namespace diag {

/** Install serial-only touch tracing. Call with the LVGL lock held. */
void ui_debug_install(lv_display_t *disp, lv_indev_t *indev);

/** Name a widget and log press/release/click. */
void ui_debug_watch(lv_obj_t *obj, const char *name);

/** Dump active screen / layer_top / layer_sys children. */
void ui_debug_dump_layers(const char *why);

/** Timestamped note. */
void ui_debug_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace diag
