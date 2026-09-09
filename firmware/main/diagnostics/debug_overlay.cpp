#include "debug_overlay.hpp"
#include "metrics.hpp"
#include "reader/tile_cache.hpp"

using reader::g_tile_cache_for_debug;

#include <cstdio>

namespace diag {

lv_obj_t *debug_overlay_create(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_bg_color(lbl, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_70, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_all(lbl, 6, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 4, 4);
    return lbl;
}

void debug_overlay_update(lv_obj_t *label)
{
    if (!label) {
        return;
    }
    const Metrics &m = metrics();
    char buf[256];
    uint64_t avg_sd = m.sd_reads ? m.sd_read_us / m.sd_reads : 0;
    uint64_t avg_dec = m.decodes ? m.decode_us / m.decodes : 0;
    snprintf(buf, sizeof(buf),
             "SD:%llu avg %lluus | Dec:%llu avg %lluus fail:%llu\nCache hits:%llu miss:%llu tiles:%zu",
             (unsigned long long)m.sd_reads, (unsigned long long)avg_sd,
             (unsigned long long)m.decodes, (unsigned long long)avg_dec,
             (unsigned long long)m.decode_fail,
             (unsigned long long)m.cache_hits, (unsigned long long)m.cache_misses,
             g_tile_cache_for_debug ? g_tile_cache_for_debug->size() : 0);
    lv_label_set_text(label, buf);
}

}  // namespace diag
