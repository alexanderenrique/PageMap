#include "gesture_controller.hpp"
#include "page_canvas.hpp"

#include "esp_timer.h"

#include <cmath>

namespace reader {

struct GestureCtx {
    Viewport *viewport = nullptr;
    app::AppController *controller = nullptr;
    lv_obj_t *canvas = nullptr;
    int start_x = 0;
    int start_y = 0;
    int last_x = 0;
    int last_y = 0;
    int64_t press_time_us = 0;
    int64_t last_tap_us = 0;
    int last_tap_x = 0;
    int last_tap_y = 0;
    bool dragging = false;
};

static void on_press(lv_event_t *e)
{
    auto *ctx = static_cast<GestureCtx *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p{};
    lv_indev_get_point(indev, &p);
    ctx->start_x = ctx->last_x = p.x;
    ctx->start_y = ctx->last_y = p.y;
    ctx->press_time_us = esp_timer_get_time();
    ctx->dragging = false;
}

static void on_pressing(lv_event_t *e)
{
    auto *ctx = static_cast<GestureCtx *>(lv_event_get_user_data(e));
    lv_point_t p{};
    lv_indev_get_point(lv_indev_active(), &p);

    const int dx = p.x - ctx->last_x;
    const int dy = p.y - ctx->last_y;
    const int total_dx = p.x - ctx->start_x;
    const int total_dy = p.y - ctx->start_y;

    if (!ctx->dragging && (std::abs(total_dx) > 12 || std::abs(total_dy) > 12)) {
        ctx->dragging = true;
        page_canvas_set_dragging(ctx->canvas, true);
    }

    // Apply pan on the LVGL thread: invalidate only the canvas (no chrome rebuild,
    // no tile IO submit) so drag frames stay cheap.
    if (ctx->dragging && ctx->viewport) {
        ctx->viewport->pan_by(static_cast<float>(dx), static_cast<float>(dy));
        page_canvas_invalidate(ctx->canvas);
    }

    ctx->last_x = p.x;
    ctx->last_y = p.y;
}

static void apply_double_tap_zoom(GestureCtx *ctx, int sx, int sy)
{
    if (!ctx->viewport || !ctx->controller) {
        return;
    }

    float doc_x = 0, doc_y = 0;
    ctx->viewport->screen_to_doc(static_cast<float>(sx), static_cast<float>(sy), &doc_x, &doc_y);

    const float fit = ctx->viewport->fit_mode() == FitMode::Page
                          ? ctx->viewport->state().display_scale
                          : ctx->viewport->state().display_scale;
    const bool currently_fit =
        ctx->viewport->fit_mode() == FitMode::Page || ctx->viewport->fit_mode() == FitMode::Width;

    if (currently_fit) {
        // Zoom into detail around tap (~2.5x fit-width density or clamp).
        const float target = std::min(fit * 2.5f, 4.0f);
        ctx->controller->post_set_zoom(target, doc_x, doc_y, sx, sy);
    } else {
        ctx->controller->post_set_fit_mode(ctx->controller->config().default_fit_mode);
    }
}

static void on_release(lv_event_t *e)
{
    auto *ctx = static_cast<GestureCtx *>(lv_event_get_user_data(e));
    lv_point_t p{};
    lv_indev_get_point(lv_indev_active(), &p);

    const int64_t now = esp_timer_get_time();
    const int64_t dur_ms = (now - ctx->press_time_us) / 1000;
    const int total_dx = p.x - ctx->start_x;
    const int total_dy = p.y - ctx->start_y;

    if (ctx->dragging) {
        page_canvas_set_dragging(ctx->canvas, false);
        // One full refresh after the gesture: submit visible/prefetch tile IO and
        // redraw with filtered scaling restored.
        page_canvas_refresh(ctx->canvas);
        return;
    }

    if (dur_ms > 450) {
        return;
    }

    if (std::abs(total_dx) > 20 || std::abs(total_dy) > 20) {
        return;
    }

    // Double-tap detection
    if (ctx->last_tap_us > 0 && (now - ctx->last_tap_us) < 350000 &&
        std::abs(p.x - ctx->last_tap_x) < 40 && std::abs(p.y - ctx->last_tap_y) < 40) {
        apply_double_tap_zoom(ctx, p.x, p.y);
        ctx->last_tap_us = 0;
        return;
    }
    ctx->last_tap_us = now;
    ctx->last_tap_x = p.x;
    ctx->last_tap_y = p.y;

    const int edge = ctx->viewport->width() / 8;
    if (p.x < edge) {
        ctx->controller->post_previous_view();
        return;
    }
    if (p.x > ctx->viewport->width() - edge) {
        ctx->controller->post_next_view();
        return;
    }

    const int cx = ctx->viewport->width() / 2;
    const int cy = ctx->viewport->height() / 2;
    const int safe_r = 120;
    if (std::abs(p.x - cx) < safe_r && std::abs(p.y - cy) < safe_r) {
        ctx->controller->post_toggle_chrome();
    }
}

void gesture_controller_attach(lv_obj_t *target, Viewport *viewport, app::AppController *controller)
{
    auto *ctx = new GestureCtx{.viewport = viewport, .controller = controller, .canvas = target};
    lv_obj_add_event_cb(target, on_press, LV_EVENT_PRESSED, ctx);
    lv_obj_add_event_cb(target, on_pressing, LV_EVENT_PRESSING, ctx);
    lv_obj_add_event_cb(target, on_release, LV_EVENT_RELEASED, ctx);
}

}  // namespace reader
