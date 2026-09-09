#include "viewport.hpp"

#include <algorithm>
#include <cmath>

namespace reader {

void Viewport::set_viewport_size(int w, int h)
{
    viewport_w_ = w;
    viewport_h_ = h;
    clamp_pan();
}

void Viewport::set_page(int page_index, float page_w, float page_h, const float content_box[4])
{
    state_.page_index = page_index;
    page_w_ = page_w;
    page_h_ = page_h;
    if (content_box) {
        for (int i = 0; i < 4; ++i) {
            content_box_[i] = content_box[i];
        }
    }
    if (state_.fit_mode == FitMode::Page) {
        apply_fit_page();
    } else if (state_.fit_mode == FitMode::Width) {
        apply_fit_width(true);
    } else {
        clamp_pan();
    }
}

void Viewport::set_fit_mode(FitMode mode)
{
    state_.fit_mode = mode;
    if (mode == FitMode::Page) {
        apply_fit_page();
    } else if (mode == FitMode::Width) {
        apply_fit_width(true);
    }
}

float Viewport::fit_page_scale() const
{
    if (page_w_ <= 0 || page_h_ <= 0) {
        return 1.0f;
    }
    const float sx = static_cast<float>(viewport_w_) / page_w_;
    const float sy = static_cast<float>(viewport_h_) / page_h_;
    return std::min(sx, sy);
}

float Viewport::fit_width_scale(bool use_content_box) const
{
    float src_w = page_w_;
    float src_h = page_h_;
    if (use_content_box) {
        src_w = content_box_[2] - content_box_[0];
        src_h = content_box_[3] - content_box_[1];
    }
    if (src_w <= 0) {
        return 1.0f;
    }
    return static_cast<float>(viewport_w_) / src_w;
}

void Viewport::apply_fit_page()
{
    state_.display_scale = fit_page_scale();
    const float scaled_w = page_w_ * state_.display_scale;
    const float scaled_h = page_h_ * state_.display_scale;
    state_.viewport_x = (static_cast<float>(viewport_w_) - scaled_w) * 0.5f;
    state_.viewport_y = (static_cast<float>(viewport_h_) - scaled_h) * 0.5f;
    clamp_pan();
}

void Viewport::apply_fit_width(bool use_content_box)
{
    state_.display_scale = fit_width_scale(use_content_box);
    const float top = use_content_box ? content_box_[1] : 0.0f;
    state_.viewport_x = use_content_box
                            ? -content_box_[0] * state_.display_scale
                            : 0.0f;
    state_.viewport_y = -top * state_.display_scale;
    clamp_pan();
}

void Viewport::set_manual_scale(float scale, float focal_doc_x, float focal_doc_y,
                                int focal_screen_x, int focal_screen_y)
{
    state_.fit_mode = FitMode::Manual;
    const float old_scale = state_.display_scale;
    scale = std::clamp(scale, 0.05f, 8.0f);

    const float screen_x_before = focal_doc_x * old_scale + state_.viewport_x;
    const float screen_y_before = focal_doc_y * old_scale + state_.viewport_y;

    state_.display_scale = scale;
    state_.viewport_x = static_cast<float>(focal_screen_x) - focal_doc_x * scale;
    state_.viewport_y = static_cast<float>(focal_screen_y) - focal_doc_y * scale;

    (void)screen_x_before;
    (void)screen_y_before;
    clamp_pan();
}

void Viewport::pan_by(float dx, float dy)
{
    if (state_.fit_mode != FitMode::Manual) {
        state_.fit_mode = FitMode::Manual;
    }
    state_.viewport_x += dx;
    state_.viewport_y += dy;
    clamp_pan();
}

void Viewport::next_view(float step_fraction)
{
    const float scaled_h = page_h_ * state_.display_scale;
    const float step = static_cast<float>(viewport_h_) * step_fraction;
    const float bottom_visible = (-state_.viewport_y + viewport_h_) / state_.display_scale;

    if (bottom_visible >= page_h_ - 1.0f) {
        if (state_.page_index + 1 < page_count_) {
            state_.page_index++;
            if (state_.fit_mode == FitMode::Width) {
                apply_fit_width(true);
            } else {
                state_.viewport_y = -0.0f;
                clamp_pan();
            }
        }
        return;
    }

    state_.fit_mode = FitMode::Manual;
    state_.viewport_y -= step;
    clamp_pan();

    if (scaled_h <= viewport_h_) {
        if (state_.page_index + 1 < page_count_) {
            state_.page_index++;
            apply_fit_width(true);
        }
    }
}

void Viewport::previous_view(float step_fraction)
{
    const float top_visible = (-state_.viewport_y) / state_.display_scale;
    const float step = static_cast<float>(viewport_h_) * step_fraction;

    if (top_visible <= 0.5f) {
        if (state_.page_index > 0) {
            state_.page_index--;
            if (state_.fit_mode == FitMode::Width) {
                apply_fit_width(true);
                state_.viewport_y = -(page_h_ * state_.display_scale - viewport_h_);
                clamp_pan();
            }
        }
        return;
    }

    state_.fit_mode = FitMode::Manual;
    state_.viewport_y += step;
    clamp_pan();
}

void Viewport::clamp_pan()
{
    const float scaled_w = page_w_ * state_.display_scale;
    const float scaled_h = page_h_ * state_.display_scale;

    if (scaled_w <= viewport_w_) {
        state_.viewport_x = (static_cast<float>(viewport_w_) - scaled_w) * 0.5f;
    } else {
        const float min_x = static_cast<float>(viewport_w_) - scaled_w;
        state_.viewport_x = std::clamp(state_.viewport_x, min_x, 0.0f);
    }

    if (scaled_h <= viewport_h_) {
        state_.viewport_y = (static_cast<float>(viewport_h_) - scaled_h) * 0.5f;
    } else {
        const float min_y = static_cast<float>(viewport_h_) - scaled_h;
        state_.viewport_y = std::clamp(state_.viewport_y, min_y, 0.0f);
    }
}

Rect Viewport::visible_doc_rect() const
{
    const float inv = 1.0f / state_.display_scale;
    return Rect{
        .x = (-state_.viewport_x) * inv,
        .y = (-state_.viewport_y) * inv,
        .w = static_cast<float>(viewport_w_) * inv,
        .h = static_cast<float>(viewport_h_) * inv,
    };
}

void Viewport::doc_to_screen(float doc_x, float doc_y, float *sx, float *sy) const
{
    if (sx) {
        *sx = doc_x * state_.display_scale + state_.viewport_x;
    }
    if (sy) {
        *sy = doc_y * state_.display_scale + state_.viewport_y;
    }
}

void Viewport::screen_to_doc(float sx, float sy, float *doc_x, float *doc_y) const
{
    const float inv = 1.0f / state_.display_scale;
    if (doc_x) {
        *doc_x = (sx - state_.viewport_x) * inv;
    }
    if (doc_y) {
        *doc_y = (sy - state_.viewport_y) * inv;
    }
}

void Viewport::restore_state(const ViewportState &s)
{
    state_ = s;
    clamp_pan();
}

}  // namespace reader
