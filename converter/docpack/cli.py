from __future__ import annotations

from pathlib import Path
from typing import Optional

import typer

from . import __version__
from .convert import convert_pdf
from .validation import validate_package

app = typer.Typer(add_completion=False, no_args_is_help=True, help="ESP document package tools")


@app.callback()
def main() -> None:
    """ESP document package tools."""


@app.command("convert")
def convert_cmd(
    input_pdf: Path = typer.Argument(..., exists=True, dir_okay=False, readable=True),
    output: Path = typer.Option(..., "--output", "-o", help="Output package directory"),
    dpi: int = typer.Option(300, "--dpi", help="Master rasterization DPI"),
    tile_size: int = typer.Option(256, "--tile-size", help="JPEG tile edge length"),
    quality: int = typer.Option(85, "--quality", min=40, max=95, help="JPEG quality"),
    crop: str = typer.Option("auto", "--crop", help="Content crop mode: auto|none"),
    title: Optional[str] = typer.Option(None, "--title", help="Override document title"),
) -> None:
    """Rasterize a PDF into an esp-docpack tile package."""
    if crop not in {"auto", "none"}:
        raise typer.BadParameter("crop must be 'auto' or 'none'")
    result = convert_pdf(
        input_pdf=input_pdf,
        output_dir=output,
        dpi=dpi,
        tile_size=tile_size,
        quality=quality,
        crop=crop,
        title_override=title,
    )
    typer.echo(f"Wrote package to {result}")


@app.command("validate")
def validate_cmd(
    package: Path = typer.Argument(..., exists=True, file_okay=False),
) -> None:
    """Validate an existing document package."""
    errors = validate_package(package)
    if errors:
        typer.echo(f"INVALID: {len(errors)} problem(s)")
        for error in errors:
            typer.echo(f"  - {error}")
        raise typer.Exit(code=1)
    typer.echo("OK")


@app.command("version")
def version_cmd() -> None:
    typer.echo(__version__)


if __name__ == "__main__":
    app()
