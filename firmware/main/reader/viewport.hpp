#pragma once

#include <stdint.h>

namespace reader {

enum class FitMode : uint8_t {
    Page = 0,
    Width = 1,
    Manual = 2,
};

struct Point {
    float x;
    float y;
};

struct Size {
    float w;
    float h;
};

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

struct ViewportState {
    int page_index = 0;
    FitMode fit_mode = FitMode::Width;
    float display_scale = 1.0f;
    float viewport_x = 0.0f;
    float viewport_y = 0.0f;
};

class Viewport {
public:
    void set_viewport_size(int w, int h);
    void set_page(int page_index, float page_w, float page_h, const float content_box[4]);

    void set_fit_mode(FitMode mode);
    FitMode fit_mode() const { return state_.fit_mode; }

    void apply_fit_page();
    void apply_fit_width(bool use_content_box = true);
    float compute_fit_page_scale() const { return fit_page_scale(); }
    float compute_fit_width_scale(bool use_content_box = true) const
    {
        return fit_width_scale(use_content_box);
    }

    void set_manual_scale(float scale, float focal_doc_x, float focal_doc_y, int focal_screen_x, int focal_screen_y);
    void pan_by(float dx, float dy);
    void next_view(float step_fraction = 0.85f);
    void previous_view(float step_fraction = 0.85f);

    const ViewportState &state() const { return state_; }
    int width() const { return viewport_w_; }
    int height() const { return viewport_h_; }
    float page_width() const { return page_w_; }
    float page_height() const { return page_h_; }
    int page_count_hint() const { return page_count_; }
    void set_page_count(int count) { page_count_ = count; }

    Rect visible_doc_rect() const;
    float doc_to_screen_scale() const { return state_.display_scale; }
    void doc_to_screen(float doc_x, float doc_y, float *sx, float *sy) const;
    void screen_to_doc(float sx, float sy, float *doc_x, float *doc_y) const;

    void restore_state(const ViewportState &s);
    ViewportState capture_state() const { return state_; }

private:
    void clamp_pan();
    float fit_page_scale() const;
    float fit_width_scale(bool use_content_box) const;

    ViewportState state_;
    int viewport_w_ = 800;
    int viewport_h_ = 480;
    float page_w_ = 1.0f;
    float page_h_ = 1.0f;
    float content_box_[4] = {0, 0, 1, 1};
    int page_count_ = 1;
};

}  // namespace reader
