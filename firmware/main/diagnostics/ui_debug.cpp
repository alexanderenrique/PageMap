#include "ui_debug.hpp"
#include "hardware/touch_port.hpp"

#include "esp_heap_caps.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <cstdarg>
#include <cstdio>

static const char *TAG = "ui_dbg";

namespace diag {
namespace {

lv_indev_t *s_indev = nullptr;

constexpr int kMaxNames = 48;

struct NamedObj {
    lv_obj_t *obj = nullptr;
    const char *name = nullptr;
};
NamedObj s_names[kMaxNames]{};

void remember_name(lv_obj_t *obj, const char *name)
{
    if (!obj || !name) {
        return;
    }
    for (auto &slot : s_names) {
        if (slot.obj == obj) {
            slot.name = name;
            return;
        }
    }
    for (auto &slot : s_names) {
        if (!slot.obj) {
            slot.obj = obj;
            slot.name = name;
            return;
        }
    }
}

void forget_name(lv_obj_t *obj)
{
    for (auto &slot : s_names) {
        if (slot.obj == obj) {
            slot.obj = nullptr;
            slot.name = nullptr;
            return;
        }
    }
}

const char *lookup_name(const lv_obj_t *obj)
{
    if (!obj) {
        return nullptr;
    }
    for (const auto &slot : s_names) {
        if (slot.obj == obj) {
            return slot.name;
        }
    }
    return nullptr;
}

void describe_obj(lv_obj_t *obj, char *buf, size_t n)
{
    if (!obj) {
        snprintf(buf, n, "null");
        return;
    }
    const char *name = lookup_name(obj);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    snprintf(buf, n, "%s [%d,%d %dx%d]",
             name ? name : "obj",
             (int)a.x1, (int)a.y1,
             (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
}

void dump_parent(lv_obj_t *parent, const char *label, uint32_t max_n)
{
    if (!parent) {
        ESP_LOGI(TAG, "%s: <null>", label);
        return;
    }
    char self[96];
    describe_obj(parent, self, sizeof(self));
    ESP_LOGI(TAG, "%s: %s children=%u",
             label, self, (unsigned)lv_obj_get_child_count(parent));
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n && i < max_n; ++i) {
        char d[96];
        describe_obj(lv_obj_get_child(parent, i), d, sizeof(d));
        ESP_LOGI(TAG, "  [%u] %s", (unsigned)i, d);
    }
}

void log_heap(const char *where)
{
    ESP_LOGI(TAG, "heap[%s] int=%u psram=%u",
             where,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

lv_obj_t *hit_at(lv_point_t pt)
{
    lv_point_t p = pt;
    lv_obj_t *top = lv_indev_search_obj(lv_layer_top(), &p);
    if (top) {
        return top;
    }
    return lv_indev_search_obj(lv_screen_active(), &p);
}

void log_touch(const char *evt)
{
    if (!s_indev) {
        return;
    }
    lv_point_t pt{};
    lv_indev_get_point(s_indev, &pt);

    char raw[72] = "raw=none";
    auto *tp = hw::touch_handle();
    if (tp) {
        esp_lcd_touch_point_data_t pts[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {};
        uint8_t cnt = 0;
        if (esp_lcd_touch_get_data(tp, pts, &cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) == ESP_OK &&
            cnt > 0) {
            snprintf(raw, sizeof(raw), "raw=%u,%u",
                     (unsigned)pts[0].x, (unsigned)pts[0].y);
        }
    }

    char hit_d[96];
    describe_obj(hit_at(pt), hit_d, sizeof(hit_d));
    const char *scr_name = lookup_name(lv_screen_active());
    ESP_LOGI(TAG, "%s %d,%d %s scr=%s hit=%s",
             evt, (int)pt.x, (int)pt.y, raw,
             scr_name ? scr_name : "?", hit_d);
}

void on_indev_event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        log_touch("PRESSED");
        break;
    case LV_EVENT_RELEASED:
        log_touch("RELEASED");
        break;
    case LV_EVENT_CLICKED:
        log_touch("CLICKED");
        break;
    default:
        break;
    }
}

void on_widget_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_CLICKED && code != LV_EVENT_DELETE) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target_obj(e);
    char d[96];
    describe_obj(obj, d, sizeof(d));
    ESP_LOGI(TAG, "widget %s %s", lv_event_code_get_name(code), d);
    if (code == LV_EVENT_DELETE) {
        forget_name(obj);
    }
}

}  // namespace

void ui_debug_install(lv_display_t *disp, lv_indev_t *indev)
{
    (void)disp;
    s_indev = indev;
    if (s_indev) {
        lv_indev_add_event_cb(s_indev, on_indev_event, LV_EVENT_PRESSED, nullptr);
        lv_indev_add_event_cb(s_indev, on_indev_event, LV_EVENT_RELEASED, nullptr);
        lv_indev_add_event_cb(s_indev, on_indev_event, LV_EVENT_CLICKED, nullptr);
    }
    ESP_LOGI(TAG, "serial touch trace on (no HUD)");
}

void ui_debug_watch(lv_obj_t *obj, const char *name)
{
    if (!obj) {
        return;
    }
    if (name && name[0]) {
        remember_name(obj, name);
    }
    lv_obj_add_event_cb(obj, on_widget_event, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, on_widget_event, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, on_widget_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, on_widget_event, LV_EVENT_DELETE, nullptr);
    lv_obj_update_layout(obj);
    char d[96];
    describe_obj(obj, d, sizeof(d));
    ESP_LOGI(TAG, "watch %s", d);
}

void ui_debug_dump_layers(const char *why)
{
    ESP_LOGI(TAG, "---- layers (%s) ----", why ? why : "?");
    dump_parent(lv_screen_active(), "screen", 8);
    dump_parent(lv_layer_top(), "layer_top", 8);
    log_heap(why ? why : "layers");
}

void ui_debug_note(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ESP_LOGI(TAG, "NOTE %s", buf);
}

}  // namespace diag
