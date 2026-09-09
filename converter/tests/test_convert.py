from __future__ import annotations

from pathlib import Path

import pymupdf as fitz
from PIL import Image, ImageDraw

from docpack.convert import convert_pdf
from docpack.pdf_source import detect_content_box, dyadic_scales
from docpack.validation import validate_package


FIXTURES = Path(__file__).parent / "fixtures"


def make_fixture_pdf(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    doc = fitz.open()
    page = doc.new_page(width=612, height=792)
    page.insert_text((72, 72), "PageMap Fixture", fontsize=24)
    page.insert_text((72, 110), "Page 1 — figure and small text for zoom tests.", fontsize=12)
    page.draw_rect(fitz.Rect(72, 160, 272, 320), color=(0, 0, 0), width=1)
    page.insert_text((80, 180), "Graph placeholder", fontsize=11)

    page2 = doc.new_page(width=612, height=792)
    page2.insert_text((72, 72), "Page 2", fontsize=14)
    for i in range(8):
        page2.insert_text((72, 110 + i * 18), f"Row {i + 1}: value={i * 3.14:.2f}", fontsize=12)

    doc.save(path)
    doc.close()


def test_content_box_detects_ink() -> None:
    image = Image.new("RGB", (200, 200), (255, 255, 255))
    draw = ImageDraw.Draw(image)
    draw.rectangle((40, 50, 150, 140), fill=(0, 0, 0))
    box = detect_content_box(image, threshold=245, margin=2)
    left, top, right, bottom = box
    assert left <= 40
    assert top <= 50
    assert right >= 150
    assert bottom >= 140


def test_dyadic_scales_include_master() -> None:
    scales = dyadic_scales(2550, 3300, 256)
    assert scales[0] < 1.0
    assert abs(scales[-1] - 1.0) < 1e-9


def test_convert_and_validate(tmp_path: Path) -> None:
    pdf = FIXTURES / "sample.pdf"
    make_fixture_pdf(pdf)
    out = tmp_path / "SampleDoc"
    convert_pdf(pdf, out, dpi=72, tile_size=128, quality=70, crop="auto", title_override="Sample")
    errors = validate_package(out)
    assert errors == []
    assert (out / "manifest.json").is_file()
    assert (out / "cover.jpg").is_file()
    assert (out / "pages" / "0001" / "page.json").is_file()
    assert (out / "pages" / "0001" / "z0" / "0_0.jpg").is_file()
