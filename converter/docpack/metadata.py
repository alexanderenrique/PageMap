from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from .pyramid import LevelInfo


def stable_document_id(pdf_path: Path, title: str, page_count: int) -> str:
    digest = hashlib.sha256()
    digest.update(pdf_path.name.encode("utf-8"))
    digest.update(str(pdf_path.stat().st_size).encode("utf-8"))
    digest.update(title.encode("utf-8"))
    digest.update(str(page_count).encode("utf-8"))
    # Include a few bytes of content so identical names with different bytes diverge.
    with pdf_path.open("rb") as handle:
        digest.update(handle.read(64 * 1024))
    return f"sha256:{digest.hexdigest()}"


def page_json(
    page_number: int,
    master_width: int,
    master_height: int,
    content_box: tuple[int, int, int, int],
    levels: list[LevelInfo],
) -> dict[str, Any]:
    return {
        "page_number": page_number,
        "master_width": master_width,
        "master_height": master_height,
        "content_box": list(content_box),
        "thumbnail": "thumb.jpg",
        "levels": [
            {
                "id": level.level_id,
                "width": level.width,
                "height": level.height,
                "columns": level.columns,
                "rows": level.rows,
                "scale_from_master": level.scale_from_master,
                "path": level.path_template,
            }
            for level in levels
        ],
    }


def manifest_json(
    document_id: str,
    title: str,
    authors: list[str],
    page_count: int,
    tile_size: int,
    source_filename: str,
    page_paths: list[str],
) -> dict[str, Any]:
    return {
        "format": "esp-docpack",
        "format_version": 1,
        "document_id": document_id,
        "title": title,
        "authors": authors,
        "page_count": page_count,
        "cover": "cover.jpg",
        "tile_size": tile_size,
        "tile_format": "jpeg",
        "source": {
            "type": "pdf",
            "filename": source_filename,
        },
        "pages": page_paths,
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
