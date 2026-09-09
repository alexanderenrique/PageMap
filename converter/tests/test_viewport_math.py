"""Host-side viewport math mirrors (no ESP-IDF required)."""

from __future__ import annotations


def fit_page_scale(page_w: float, page_h: float, vw: float, vh: float) -> float:
    return min(vw / page_w, vh / page_h)


def fit_width_scale(content_w: float, vw: float) -> float:
    return vw / content_w


def clamp_pan(vx: float, vy: float, page_w: float, page_h: float, scale: float, vw: float, vh: float):
    scaled_w = page_w * scale
    scaled_h = page_h * scale
    if scaled_w <= vw:
        vx = (vw - scaled_w) * 0.5
    else:
        vx = min(0.0, max(vw - scaled_w, vx))
    if scaled_h <= vh:
        vy = (vh - scaled_h) * 0.5
    else:
        vy = min(0.0, max(vh - scaled_h, vy))
    return vx, vy


def zoom_around(scale_old: float, scale_new: float, doc_x: float, doc_y: float, sx: float, sy: float):
    vx = sx - doc_x * scale_new
    vy = sy - doc_y * scale_new
    return vx, vy


def test_fit_page_letter_on_800x480():
    # Letter @ 72 DPI ≈ 612x792
    s = fit_page_scale(612, 792, 800, 480)
    assert abs(s - 480 / 792) < 1e-6


def test_fit_width_content_box():
    s = fit_width_scale(500, 800)
    assert abs(s - 1.6) < 1e-6


def test_zoom_preserves_focal_point():
    scale_old = 0.5
    vx, vy = 10.0, 20.0
    sx, sy = 400.0, 240.0
    doc_x = (sx - vx) / scale_old
    doc_y = (sy - vy) / scale_old
    scale_new = 1.0
    nvx, nvy = zoom_around(scale_old, scale_new, doc_x, doc_y, sx, sy)
    assert abs(doc_x * scale_new + nvx - sx) < 1e-6
    assert abs(doc_y * scale_new + nvy - sy) < 1e-6


def test_clamp_centers_small_page():
    vx, vy = clamp_pan(-100, -100, 200, 200, 1.0, 800, 480)
    assert abs(vx - 300) < 1e-6
    assert abs(vy - 140) < 1e-6
