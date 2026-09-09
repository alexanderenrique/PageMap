#include "page_canvas.hpp"
#include "diagnostics/metrics.hpp"

#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace reader {

static int32_t floor_to_i32(float v)
{
    return static_cast<int32_t>(std::floor(v));
}

// Map a pixel on the selected pyramid level onto an absolute LVGL screen coordinate.
// Adjacent tiles must share this mapping so their edges abut instead of rounding apart.
static int32_t level_px_to_screen(int level_px, float k, float origin)
{
    return floor_to_i32(static_cast<float>(level_px) * k + origin);
}

struct CanvasCtx {
    Viewport *viewport = nullptr;
    docs::DocumentModel *model = nullptr;
    TileCache *cache = nullptr;
    TileManager *manager = nullptr;
    docs::PageInfo page{};
    uint32_t doc_hash = 0;
    int tile_size = 256;
    float dim_alpha = 0.0f;
    lv_timer_t *timer = nullptr;
    std::vector<std::shared_ptr<DecodedTile>> pinned;
};

static void canvas_draw_event(lv_event_t *e)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *ctx = static_cast<CanvasCtx *>(lv_obj_get_user_data(obj));
    if (!ctx || !ctx->viewport) {
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const float scale = ctx->viewport->doc_to_screen_scale();
    const reader::Rect vis = ctx->viewport->visible_doc_rect();

    if (ctx->page.levels.empty()) {
        lv_draw_rect_dsc_t dsc{};
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(0xB0B0B0);
        lv_draw_rect(layer, &dsc, &coords);
        return;
    }

    const int level = ctx->model->select_level_for_scale(scale, ctx->page);
    const docs::PageLevel &lv_info = ctx->page.levels[level];
    const int tile_size = std::max(1, ctx->tile_size);
    const float k = scale / std::max(lv_info.scale_from_master, 0.0001f);
    const float origin_x = ctx->viewport->state().viewport_x + static_cast<float>(coords.x1);
    const float origin_y = ctx->viewport->state().viewport_y + static_cast<float>(coords.y1);

    const float vis_lx0 = vis.x * lv_info.scale_from_master;
    const float vis_ly0 = vis.y * lv_info.scale_from_master;
    const float vis_lx1 = (vis.x + vis.w) * lv_info.scale_from_master;
    const float vis_ly1 = (vis.y + vis.h) * lv_info.scale_from_master;
    const int col0 = static_cast<int>(std::floor(vis_lx0 / static_cast<float>(tile_size)));
    const int col1 = static_cast<int>(std::ceil(vis_lx1 / static_cast<float>(tile_size))) - 1;
    const int row0 = static_cast<int>(std::floor(vis_ly0 / static_cast<float>(tile_size)));
    const int row1 = static_cast<int>(std::ceil(vis_ly1 / static_cast<float>(tile_size))) - 1;

    ctx->pinned.clear();
    for (int r = row0; r <= row1; ++r) {
        for (int c = col0; c <= col1; ++c) {
            if (c < 0 || r < 0 || c >= lv_info.columns || r >= lv_info.rows) {
                continue;
            }

            TileKey key{
                .doc_hash = ctx->doc_hash,
                .page_index = ctx->viewport->state().page_index,
                .level_index = level,
                .col = c,
                .row = r,
            };

            const int32_t x1 = level_px_to_screen(c * tile_size, k, origin_x);
            const int32_t y1 = level_px_to_screen(r * tile_size, k, origin_y);
            const int32_t x2e = level_px_to_screen((c + 1) * tile_size, k, origin_x);
            const int32_t y2e = level_px_to_screen((r + 1) * tile_size, k, origin_y);
            int32_t dest_w = x2e - x1;
            int32_t dest_h = y2e - y1;
            if (dest_w < 1 || dest_h < 1) {
                continue;
            }

            lv_area_t tile_area = {
                .x1 = x1,
                .y1 = y1,
                .x2 = x1 + dest_w - 1,
                .y2 = y1 + dest_h - 1,
            };

            auto tile = ctx->cache ? ctx->cache->find(key) : nullptr;
            if (tile && tile->img_dsc.data && tile->width > 0 && tile->height > 0) {
                diag::metrics().cache_hits++;
                ctx->pinned.push_back(tile);

                const int src_w = tile->width;
                const int src_h = tile->height;
                lv_draw_image_dsc_t img_dsc{};
                lv_draw_image_dsc_init(&img_dsc);
                img_dsc.src = &tile->img_dsc;
                img_dsc.pivot.x = 0;
                img_dsc.pivot.y = 0;

                // LVGL 9 treats `coords` as the unscaled bitmap. Stretch with scale_x/y
                // instead of hoping the dest rect will resample the JPEG.
                lv_area_t img_coords = tile_area;
                if (dest_w != src_w || dest_h != src_h) {
                    dest_w += 1;
                    dest_h += 1;
                    img_dsc.scale_x = (dest_w * LV_SCALE_NONE) / src_w;
                    img_dsc.scale_y = (dest_h * LV_SCALE_NONE) / src_h;
                    img_coords.x2 = x1 + src_w - 1;
                    img_coords.y2 = y1 + src_h - 1;
                }
                lv_draw_image(layer, &img_dsc, &img_coords);
            } else {
                diag::metrics().cache_misses++;
                lv_draw_rect_dsc_t dsc{};
                lv_draw_rect_dsc_init(&dsc);
                dsc.bg_color = lv_color_hex(0xC8C8C8);
                lv_draw_rect(layer, &dsc, &tile_area);
            }
        }
    }

    if (ctx->dim_alpha > 0.01f) {
        lv_draw_rect_dsc_t dim{};
        lv_draw_rect_dsc_init(&dim);
        dim.bg_color = lv_color_black();
        dim.bg_opa = static_cast<lv_opa_t>(ctx->dim_alpha * static_cast<float>(LV_OPA_COVER));
        lv_draw_rect(layer, &dim, &coords);
    }
}

static void canvas_refresh_timer(lv_timer_t *timer)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    page_canvas_refresh(obj);
}

static void canvas_deleted(lv_event_t *e)
{
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *ctx = static_cast<CanvasCtx *>(lv_obj_get_user_data(obj));
    if (!ctx) {
        return;
    }
    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
        ctx->timer = nullptr;
    }
    ctx->pinned.clear();
    lv_obj_set_user_data(obj, nullptr);
    delete ctx;
}

lv_obj_t *page_canvas_create(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    auto *ctx = new CanvasCtx();
    lv_obj_set_user_data(obj, ctx);
    lv_obj_add_event_cb(obj, canvas_draw_event, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(obj, canvas_deleted, LV_EVENT_DELETE, nullptr);

    ctx->timer = lv_timer_create(canvas_refresh_timer, 200, obj);
    return obj;
}

void page_canvas_bind(lv_obj_t *canvas, Viewport *viewport, docs::DocumentModel *model,
                      TileCache *cache, TileManager *manager, uint32_t doc_hash, int tile_size)
{
    auto *ctx = static_cast<CanvasCtx *>(lv_obj_get_user_data(canvas));
    if (!ctx) {
        return;
    }
    ctx->viewport = viewport;
    ctx->model = model;
    ctx->cache = cache;
    ctx->manager = manager;
    ctx->doc_hash = doc_hash;
    ctx->tile_size = tile_size;
    if (model && viewport) {
        model->load_page(viewport->state().page_index, &ctx->page);
    }
}

void page_canvas_set_dim_overlay(lv_obj_t *canvas, float alpha)
{
    auto *ctx = static_cast<CanvasCtx *>(lv_obj_get_user_data(canvas));
    if (ctx) {
        ctx->dim_alpha = alpha;
        lv_obj_invalidate(canvas);
    }
}

void page_canvas_refresh(lv_obj_t *canvas)
{
    auto *ctx = static_cast<CanvasCtx *>(lv_obj_get_user_data(canvas));
    if (!ctx || !ctx->manager || !ctx->viewport || !ctx->model) {
        lv_obj_invalidate(canvas);
        return;
    }

    ctx->model->load_page(ctx->viewport->state().page_index, &ctx->page);

    std::vector<TileRequest> visible;
    ctx->manager->set_page_info(ctx->page);
    ctx->manager->update_visible_requests(&visible);

    std::vector<TileRequest> all = visible;
    ctx->manager->add_prefetch_border(visible, &all);

    for (const auto &req : all) {
        tile_manager_submit_io(req);
    }

    lv_obj_invalidate(canvas);
}

}  // namespace reader
