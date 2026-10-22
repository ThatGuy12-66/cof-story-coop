"""Draws tools/app.ico, the app's own icon (1.2.1): a dark tile with a red ring and a pale dot, sizes 16 to 256.
Original art (no game or PyInstaller artwork). Run once when the icon changes: python -m tools.make_app_icon"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SIZES = (16, 24, 32, 48, 64, 128, 256)


def draw(size: int):
    from PIL import Image, ImageDraw
    scale = 4   # drawn large, then shrunk: smooth edges
    big = size * scale
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    pen = ImageDraw.Draw(image)
    pen.rounded_rectangle((0, 0, big - 1, big - 1), radius=big * .22, fill=(13, 14, 16, 255))
    ring = big * .14
    box = (big * .2, big * .2, big * .8, big * .8)
    pen.arc(box, start=40, end=320, fill=(179, 38, 30, 255), width=round(ring))   # an open ring, like a C
    dot = big * .11
    centre = (big * .5, big * .5)
    pen.ellipse((centre[0] - dot, centre[1] - dot, centre[0] + dot, centre[1] + dot), fill=(235, 231, 225, 255))
    return image.resize((size, size), Image.LANCZOS)


def main() -> int:
    images = [draw(size) for size in SIZES]
    images[-1].save(ROOT / "tools/app.ico", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
