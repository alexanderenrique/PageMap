from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from PIL import Image


def _schema_dir() -> Path:
    # Prefer sibling schemas/ at repo root; fall back to bundled path.
    here = Path(__file__).resolve()
    candidates = [
        here.parents[2] / "schemas",
        here.parents[1] / "schemas",
    ]
    for candidate in candidates:
        if candidate.is_dir():
            return candidate
    raise FileNotFoundError("Could not locate schemas directory")


def load_schema(name: str) -> dict[str, Any]:
    path = _schema_dir() / name
    return json.loads(path.read_text(encoding="utf-8"))


def validate_against_schema(payload: dict[str, Any], schema_name: str) -> list[str]:
    schema = load_schema(schema_name)
    validator = Draft202012Validator(schema)
    return sorted(error.message for error in validator.iter_errors(payload))


def validate_package(package_dir: Path) -> list[str]:
    errors: list[str] = []
    manifest_path = package_dir / "manifest.json"
    if not manifest_path.is_file():
        return [f"missing manifest.json in {package_dir}"]

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"manifest.json is not valid JSON: {exc}"]

    errors.extend(validate_against_schema(manifest, "manifest-v1.schema.json"))

    cover = package_dir / manifest.get("cover", "cover.jpg")
    if not cover.is_file():
        errors.append(f"missing cover image: {cover.name}")

    pages = manifest.get("pages", [])
    if isinstance(pages, list) and manifest.get("page_count") != len(pages):
        errors.append(
            f"page_count ({manifest.get('page_count')}) does not match pages list ({len(pages)})"
        )

    tile_size = int(manifest.get("tile_size", 256))
    for rel in pages if isinstance(pages, list) else []:
        page_path = package_dir / rel
        if not page_path.is_file():
            errors.append(f"missing page metadata: {rel}")
            continue
        try:
            page = json.loads(page_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            errors.append(f"{rel} is not valid JSON: {exc}")
            continue

        errors.extend(f"{rel}: {msg}" for msg in validate_against_schema(page, "page-v1.schema.json"))

        page_dir = page_path.parent
        thumb = page_dir / page.get("thumbnail", "thumb.jpg")
        if not thumb.is_file():
            errors.append(f"missing thumbnail for {rel}")

        box = page.get("content_box", [])
        if isinstance(box, list) and len(box) == 4:
            left, top, right, bottom = box
            if not (0 <= left < right <= page.get("master_width", 0)):
                errors.append(f"{rel}: invalid content_box width bounds")
            if not (0 <= top < bottom <= page.get("master_height", 0)):
                errors.append(f"{rel}: invalid content_box height bounds")

        for level in page.get("levels", []):
            level_id = level.get("id", "?")
            columns = int(level.get("columns", 0))
            rows = int(level.get("rows", 0))
            width = int(level.get("width", 0))
            height = int(level.get("height", 0))
            expected_cols = max(1, (width + tile_size - 1) // tile_size)
            expected_rows = max(1, (height + tile_size - 1) // tile_size)
            if columns != expected_cols or rows != expected_rows:
                errors.append(
                    f"{rel}/{level_id}: grid {columns}x{rows} does not match size {width}x{height}"
                )
            for row in range(rows):
                for col in range(columns):
                    tile_path = page_dir / level_id / f"{col}_{row}.jpg"
                    if not tile_path.is_file():
                        errors.append(f"missing tile: {tile_path.relative_to(package_dir)}")
                        continue
                    try:
                        with Image.open(tile_path) as img:
                            img.verify()
                    except Exception as exc:  # noqa: BLE001 - collect all decode failures
                        errors.append(f"tile decode failed {tile_path.name}: {exc}")

    return errors
