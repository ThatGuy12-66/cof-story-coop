"""Generate the 12-slot inventory's left-half art from the player's own retail inventory art.

Reads cryoffear/gfx/vgui/640_inventory.tga and 640_inventory_three.tga (uncompressed 32-bit, as
Cry of Fear 1.6 ships them) and writes inventory12_left.tga / inventory12_left_three.tga next to
them: the left 373x480 of the background with the six old boxes erased, the POCKETS and ITEM
DESCRIPTION labels and the description box moved, and 12 (or 3) boxes of 72 px drawn for the grid
in 3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h. Also inventory12_full.tga /
inventory12_full_three.tga: the whole 748x480 background with that left half, which the grid puts
in place of the retail picture (the inventory is drawn see-through, so retail art left under the
grid showed through it). Every language pack's overlay (Polish, and since 1.5 Russian and Chinese)
gets the same when it repaints 640_inventory.tga. The
retail files are only read; nothing copied from the game is stored in the repository.

    python -m tools.make_inventory12_art <game folder>     (the folder that holds cryoffear/)
"""
import argparse
import struct
import sys
from pathlib import Path

WIDE, TALL = 373, 480                 # the grid panel: the left panel and the gap after it
BOX, STRIDE, LEFT = 72, 77, 36        # cofinventory_logic.h kBox, kStride, kLeft
ROW_TOP = (68, 145, 259)              # kRowTop: BAG rows (slots 1..8), POCKETS row (9..12)
DONOR_X, DONOR_W = 342, 22            # clean panel texture on every row, right of the old boxes
ERASE = (14, 64, 366, 466)            # x0, y0, x1, y1 (end exclusive): everything below the top label
OLD_FRAME, OLD_BOX = (36, 73), 99     # the first old box, 1 px border included
LABEL_THRESHOLD = 150                 # label text is bright; the panel texture stays below 25
POCKETS_SEARCH, POCKETS_TOP = (16, 176, 360, 229), 227   # 10 px clear above and below (live 2026-10-15)
ITEMDESC_SEARCH, ITEMDESC_TOP = (30, 331, 360, 351), 339
DESC_BOX, DESC_SHIFT = (33, 350, 342, 455), 4   # the description box with its frame


class Image:
    """BGRA pixels, top row first."""

    def __init__(self, wide, tall, pixels=None):
        self.wide, self.tall = wide, tall
        self.pixels = pixels if pixels is not None else bytearray(wide * tall * 4)

    def get(self, x, y):
        o = (y * self.wide + x) * 4
        return bytes(self.pixels[o:o + 4])

    def put(self, x, y, bgra):
        o = (y * self.wide + x) * 4
        self.pixels[o:o + 4] = bgra


def rle_decode(data, pos, count, step, path):
    """count pixels of RLE packets as BGRA bytes (24-bit gets alpha 255); packets may cross rows."""
    out = bytearray()
    done = 0
    while done < count:
        if pos >= len(data):
            raise ValueError(f'{path}: truncated')
        head = data[pos]
        pos += 1
        n = (head & 0x7f) + 1
        take = pos + (step if head & 0x80 else n * step)
        if take > len(data):
            raise ValueError(f'{path}: truncated')
        chunk = data[pos:take]
        pos = take
        if head & 0x80:
            chunk = chunk * n
        if step == 3:
            chunk = b''.join(chunk[i:i + 3] + b'\xff' for i in range(0, len(chunk), 3))
        out += chunk
        done += n
    return bytes(out[:count * 4])


def read_tga(path):
    data = Path(path).read_bytes()
    idlen, cmtype, itype = data[0], data[1], data[2]
    wide, tall, bpp, desc = struct.unpack_from('<HHBB', data, 12)
    if cmtype or itype not in (2, 10) or bpp not in ((32,) if itype == 2 else (24, 32)):
        raise ValueError(f'{path}: expected an uncompressed 32-bit or run-length-encoded 24/32-bit TGA')
    start = 18 + idlen
    if itype == 2:
        rows = data[start:start + wide * tall * 4]
    else:
        rows = rle_decode(data, start, wide * tall, bpp // 8, path)
    if len(rows) != wide * tall * 4:
        raise ValueError(f'{path}: truncated')
    image, stride = Image(wide, tall), wide * 4
    for y in range(tall):
        source = y if desc & 0x20 else tall - 1 - y
        image.pixels[y * stride:(y + 1) * stride] = rows[source * stride:(source + 1) * stride]
    return image


def write_tga(path, image):
    stride = image.wide * 4
    out = bytearray(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, image.wide, image.tall, 32, 8))
    for y in range(image.tall - 1, -1, -1):   # bottom row first, as the retail files
        out += image.pixels[y * stride:(y + 1) * stride]
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(bytes(out))


def bright_box(image, box):
    """The bounding box (end exclusive) of the label text inside box."""
    x0, y0, x1, y1 = box
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            b, g, r, _ = image.get(x, y)
            if (r + g + b) // 3 >= LABEL_THRESHOLD:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise ValueError(f'no label text in {box}')
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def copy(src, dst, box, dx, dy):
    """The pixels of box (end exclusive) from src to dst, moved by dx, dy and clipped."""
    x0, y0, x1, y1 = box
    x0, y0 = max(x0, 0, -dx), max(y0, 0, -dy)
    x1, y1 = min(x1, src.wide, dst.wide - dx), min(y1, src.tall, dst.tall - dy)
    if x1 <= x0:
        return
    for y in range(y0, y1):
        s, d = (y * src.wide + x0) * 4, ((y + dy) * dst.wide + x0 + dx) * 4
        dst.pixels[d:d + (x1 - x0) * 4] = src.pixels[s:s + (x1 - x0) * 4]


def mirror(k, n):
    k %= 2 * n
    return k if k < n else 2 * n - 1 - k


def erase(src, dst, box):
    """Panel texture over box: the clean strip, mirrored sideways, on the same rows (keeps the vertical shading)."""
    x0, y0, x1, y1 = box
    for y in range(y0, y1):
        for x in range(x0, x1):
            dst.put(x, y, src.get(DONOR_X + mirror(x - x0, DONOR_W), y))


def frame_tile(src):
    """The old 99 px box as a 72 px one: the 1 px border kept crisp, the inside resampled."""
    fx, fy = OLD_FRAME
    border = src.get(fx, fy + OLD_BOX // 2)
    tile = Image(BOX, BOX)
    for y in range(BOX):
        for x in range(BOX):
            if x in (0, BOX - 1) or y in (0, BOX - 1):
                tile.put(x, y, border)
            else:
                tile.put(x, y, src.get(fx + 1 + (x - 1) * (OLD_BOX - 2) // (BOX - 2),
                                       fy + 1 + (y - 1) * (OLD_BOX - 2) // (BOX - 2)))
    return tile


def slot_origin(slot):
    i = slot - 1
    return LEFT + (i % 4) * STRIDE, ROW_TOP[i // 4]


def move_label(src, out, search, top, margin_x, margin_top, margin_bottom):
    x0, y0, x1, y1 = bright_box(src, search)
    copy(src, out, (max(0, x0 - margin_x), y0 - margin_top, min(DONOR_X, x1 + margin_x), y1 + margin_bottom), 0, top - y0)


def compose(src, three):
    if src.wide < WIDE or src.tall != TALL:
        raise ValueError('expected the 748x480 retail inventory background')
    out = Image(WIDE, TALL)
    copy(src, out, (0, 0, WIDE, TALL), 0, 0)
    erase(src, out, ERASE)
    if not three:
        move_label(src, out, POCKETS_SEARCH, POCKETS_TOP, 10, 9, 9)
    move_label(src, out, ITEMDESC_SEARCH, ITEMDESC_TOP, 8, 4, 4)   # 4 px: the old box border is 6 px above
    copy(src, out, DESC_BOX, 0, DESC_SHIFT)
    tile = frame_tile(src)
    for slot in range(1, (3 if three else 12) + 1):
        x, y = slot_origin(slot)
        copy(tile, out, (0, 0, BOX, BOX), x, y)
    return out


def full(src, left):
    """The whole background with the grid's left half in place of the retail one."""
    out = Image(src.wide, src.tall, bytearray(src.pixels))
    copy(left, out, (0, 0, left.wide, left.tall), 0, 0)
    return out


def generate(game):
    """The art for one game folder (the folder that holds cryoffear/); the files written."""
    game = Path(game)
    # the game's own art, then every language pack that repaints the inventory (Polish, Russian, Chinese)
    folders = (game / 'cryoffear/gfx/vgui',) + tuple(sorted(
        p / 'overlay/cryoffear/gfx/vgui' for p in (game / 'cryoffear/languages').glob('*')
        if (p / 'overlay/cryoffear/gfx/vgui').is_dir()))
    if not (folders[0] / '640_inventory.tga').is_file():
        raise SystemExit(f'{folders[0] / "640_inventory.tga"} is missing: is {game} a Cry of Fear folder?')
    written = []
    for folder in folders:
        for source, suffix, three in (('640_inventory.tga', '', False), ('640_inventory_three.tga', '_three', True)):
            if (folder / source).is_file():
                try:
                    src = read_tga(folder / source)
                    image = compose(src, three)
                except (ValueError, struct.error, IndexError) as error:
                    if folder is folders[0]:
                        raise
                    print(f'skipped {folder / source}: {error}', file=sys.stderr)   # a pack's overlay is optional
                    continue
                for target, picture in ((f'inventory12_left{suffix}.tga', image), (f'inventory12_full{suffix}.tga', full(src, image))):
                    write_tga(folder / target, picture)
                    written.append(folder / target)
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('game', help='the folder that holds cryoffear/')
    args = parser.parse_args(argv)
    for path in generate(args.game):
        print(path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
