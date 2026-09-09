from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


@dataclass(frozen=True)
class LevelInfo:
    level_id: str
    width: int
    height: int
    columns: int
    rows: int
    scale_from_master: float
    path_template: str


def resize_level(master: Image.Image, scale: float) -> Image.Image:
    if abs(scale - 1.0) < 1e-9:
        return master.copy()
    width = max(1, int(round(master.width * scale)))
    height = max(1, int(round(master.height * scale)))
    return master.resize((width, height), Image.Resampling.LANCZOS)


def tile_grid(width: int, height: int, tile_size: int) -> tuple[int, int]:
    columns = max(1, math.ceil(width / tile_size))
    rows = max(1, math.ceil(height / tile_size))
    return columns, rows


def write_tiles(
    image: Image.Image,
    out_dir: Path,
    tile_size: int,
    quality: int,
) -> tuple[int, int]:
    out_dir.mkdir(parents=True, exist_ok=True)
    columns, rows = tile_grid(image.width, image.height, tile_size)
    for row in range(rows):
        for col in range(columns):
            left = col * tile_size
            top = row * tile_size
            right = min(left + tile_size, image.width)
            bottom = min(top + tile_size, image.height)
            tile = image.crop((left, top, right, bottom))
            # Pad edge tiles to a consistent size for the hardware decoder.
            if tile.width != tile_size or tile.height != tile_size:
                padded = Image.new("RGB", (tile_size, tile_size), (255, 255, 255))
                padded.paste(tile, (0, 0))
                tile = padded
            tile.save(out_dir / f"{col}_{row}.jpg", format="JPEG", quality=quality, optimize=True)
    return columns, rows


def build_levels(
    master: Image.Image,
    page_dir: Path,
    scales: list[float],
    tile_size: int,
    quality: int,
) -> list[LevelInfo]:
    levels: list[LevelInfo] = []
    for index, scale in enumerate(scales):
        level_id = f"z{index}"
        level_image = resize_level(master, scale)
        level_dir = page_dir / level_id
        columns, rows = write_tiles(level_image, level_dir, tile_size, quality)
        levels.append(
            LevelInfo(
                level_id=level_id,
                width=level_image.width,
                height=level_image.height,
                columns=columns,
                rows=rows,
                scale_from_master=scale,
                path_template=f"{level_id}/{{column}}_{{row}}.jpg",
            )
        )
    return levels


def write_thumbnail(image: Image.Image, path: Path, max_edge: int = 240, quality: int = 80) -> None:
    thumb = image.copy()
    thumb.thumbnail((max_edge, max_edge), Image.Resampling.LANCZOS)
    path.parent.mkdir(parents=True, exist_ok=True)
    thumb.save(path, format="JPEG", quality=quality, optimize=True)
