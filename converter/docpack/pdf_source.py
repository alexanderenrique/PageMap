from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

import pymupdf as fitz  # PyMuPDF
from PIL import Image


@dataclass(frozen=True)
class RasterPage:
    page_number: int
    image: Image.Image
    content_box: tuple[int, int, int, int]


def open_pdf(path: Path) -> fitz.Document:
    return fitz.open(path)


def extract_metadata(doc: fitz.Document) -> tuple[str, list[str]]:
    meta = doc.metadata or {}
    title = (meta.get("title") or "").strip()
    author = (meta.get("author") or "").strip()
    authors = [a.strip() for a in author.split(",") if a.strip()] if author else []
    return title, authors


def rasterize_page(page: fitz.Page, page_number: int, dpi: int) -> Image.Image:
    scale = dpi / 72.0
    matrix = fitz.Matrix(scale, scale)
    pix = page.get_pixmap(matrix=matrix, alpha=False)
    mode = "RGB" if pix.n < 4 else "RGBA"
    image = Image.frombytes(mode, (pix.width, pix.height), pix.samples)
    if image.mode != "RGB":
        image = image.convert("RGB")
    return image


def detect_content_box(image: Image.Image, threshold: int = 245, margin: int = 8) -> tuple[int, int, int, int]:
    """Conservative content bounds. Never permanently removes page content."""
    gray = image.convert("L")
    width, height = gray.size
    pixels = gray.load()
    left, top, right, bottom = width, height, 0, 0
    found = False

    for y in range(height):
        for x in range(width):
            if pixels[x, y] < threshold:
                left = min(left, x)
                top = min(top, y)
                right = max(right, x)
                bottom = max(bottom, y)
                found = True

    if not found:
        return (0, 0, width, height)

    left = max(0, left - margin)
    top = max(0, top - margin)
    right = min(width, right + 1 + margin)
    bottom = min(height, bottom + 1 + margin)

    # Keep at least 90% of the page so crop stays conservative.
    min_w = int(width * 0.9)
    min_h = int(height * 0.9)
    if (right - left) < min_w:
        pad = (min_w - (right - left)) // 2
        left = max(0, left - pad)
        right = min(width, right + pad)
    if (bottom - top) < min_h:
        pad = (min_h - (bottom - top)) // 2
        top = max(0, top - pad)
        bottom = min(height, bottom + pad)

    return (left, top, right, bottom)


def rasterize_document(path: Path, dpi: int, crop: str = "auto") -> list[RasterPage]:
    doc = open_pdf(path)
    pages: list[RasterPage] = []
    try:
        for index in range(doc.page_count):
            page = doc.load_page(index)
            image = rasterize_page(page, index + 1, dpi)
            if crop == "auto":
                box = detect_content_box(image)
            else:
                box = (0, 0, image.width, image.height)
            pages.append(RasterPage(page_number=index + 1, image=image, content_box=box))
    finally:
        doc.close()
    return pages


def dyadic_scales(master_w: int, master_h: int, tile_size: int) -> list[float]:
    """Return scale_from_master values from overview (z0) up to 1.0."""
    # Overview should roughly fit in one tile.
    overview = min(tile_size / max(master_w, 1), tile_size / max(master_h, 1))
    overview = min(overview, 1.0)
    if overview <= 0:
        overview = 1.0

    # Round overview down to nearest power-of-two fraction for clean pyramid.
    if overview < 1.0:
        exp = math.floor(math.log2(overview))
        overview = 2.0**exp
        overview = max(overview, 1.0 / 64.0)

    scales: list[float] = []
    scale = overview
    while scale < 1.0 - 1e-9:
        scales.append(scale)
        scale *= 2.0
    scales.append(1.0)
    # Deduplicate while preserving order.
    unique: list[float] = []
    for s in scales:
        if not unique or abs(unique[-1] - s) > 1e-9:
            unique.append(s)
    return unique
