from __future__ import annotations

from pathlib import Path

from .metadata import manifest_json, page_json, stable_document_id, write_json
from .pdf_source import dyadic_scales, extract_metadata, open_pdf, rasterize_document
from .pyramid import build_levels, write_thumbnail
from .validation import validate_package


def convert_pdf(
    input_pdf: Path,
    output_dir: Path,
    dpi: int = 300,
    tile_size: int = 256,
    quality: int = 85,
    crop: str = "auto",
    title_override: str | None = None,
) -> Path:
    input_pdf = input_pdf.resolve()
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    with open_pdf(input_pdf) as doc:
        title, authors = extract_metadata(doc)
        page_count = doc.page_count

    if title_override:
        title = title_override
    elif not title:
        title = input_pdf.stem.replace("_", " ")

    pages = rasterize_document(input_pdf, dpi=dpi, crop=crop)
    if not pages:
        raise ValueError("PDF has no pages")

    document_id = stable_document_id(input_pdf, title, page_count)
    page_paths: list[str] = []

    for page in pages:
        page_dir = output_dir / "pages" / f"{page.page_number:04d}"
        page_dir.mkdir(parents=True, exist_ok=True)
        scales = dyadic_scales(page.image.width, page.image.height, tile_size)
        levels = build_levels(page.image, page_dir, scales, tile_size, quality)
        write_thumbnail(page.image, page_dir / "thumb.jpg")
        rel = f"pages/{page.page_number:04d}/page.json"
        write_json(
            output_dir / rel,
            page_json(
                page.page_number,
                page.image.width,
                page.image.height,
                page.content_box,
                levels,
            ),
        )
        page_paths.append(rel)

    write_thumbnail(pages[0].image, output_dir / "cover.jpg", max_edge=320, quality=quality)
    write_json(
        output_dir / "manifest.json",
        manifest_json(
            document_id=document_id,
            title=title,
            authors=authors,
            page_count=len(pages),
            tile_size=tile_size,
            source_filename=input_pdf.name,
            page_paths=page_paths,
        ),
    )

    errors = validate_package(output_dir)
    if errors:
        raise RuntimeError("Package validation failed:\n- " + "\n- ".join(errors))

    return output_dir
