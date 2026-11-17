"""The 12-slot inventory's left-half art (tools/make_inventory12_art.py)."""
import re
import struct
import tempfile
import io
from contextlib import redirect_stderr
import unittest
from pathlib import Path

from tools import make_inventory12_art as art

ROOT = Path(__file__).resolve().parents[1]
LOGIC = (ROOT / 'engine'
         / '3rdparty/freevgui/platform/xash3d-fwgs/cofinventory_logic.h')
RETAIL = ROOT / 'runtimes/enhanced/cryoffear/gfx/vgui/640_inventory.tga'

PANEL = bytes((10, 10, 10, 141))   # BGRA, as the retail panel texture
BORDER = bytes((100, 100, 100, 198))
INSIDE = bytes((3, 3, 3, 198))
TEXT = bytes((230, 230, 230, 255))


def lum(pixel):
    return (pixel[0] + pixel[1] + pixel[2]) // 3


def synthetic(three=False):
    """A 748x480 stand-in for the retail background: panel texture, labels, six old boxes, the description box."""
    image = art.Image(748, 480, bytearray(PANEL * (748 * 480)))

    def fill(x0, y0, x1, y1, pixel):
        for y in range(y0, y1):
            o = (y * 748 + x0) * 4
            image.pixels[o:o + (x1 - x0) * 4] = pixel * (x1 - x0)

    def box(x, y, w, h):
        fill(x, y, x + w, y + h, INSIDE)
        fill(x, y, x + w, y + 1, BORDER)
        fill(x, y + h - 1, x + w, y + h, BORDER)
        fill(x, y, x + 1, y + h, BORDER)
        fill(x + w - 1, y, x + w, y + h, BORDER)

    fill(36, 35, 86, 57, TEXT)            # BAG (POCKETS in the three-slot art)
    if not three:
        fill(36, 195, 152, 217, TEXT)     # POCKETS
    fill(37, 337, 127, 346, TEXT)         # ITEM DESCRIPTION
    for top in (73, 232):
        for left in (36, 138, 240):
            box(left, top, 99, 99)
    box(36, 353, 303, 99)                 # the description box
    return image


POLISH = ROOT / 'runtimes/enhanced/cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/640_inventory.tga'


def rle_tga(image, bpp, top_first=False):
    """image as an RLE (type 10) TGA: mixed raw and run packets, packets crossing row boundaries."""
    step = bpp // 8
    flat = [bytes(image.pixels[i:i + 4])[:step] for i in range(0, len(image.pixels), 4)]
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 10, 0, 0, 0, 0, 0, image.wide, image.tall, bpp,
                         0x20 if top_first else 0)
    if not top_first:   # rows are stored bottom first
        rows = [flat[y * image.wide:(y + 1) * image.wide] for y in range(image.tall)][::-1]
        flat = [p for row in rows for p in row]
    return rle_tga_flat(header, flat)


def rle_tga_flat(header, flat):
    packets, i, n = bytearray(), 0, len(flat)
    while i < n:
        run = 1
        while i + run < n and run < 128 and flat[i + run] == flat[i]:
            run += 1
        if run >= 2:
            packets += bytes((0x80 | (run - 1),)) + flat[i]
            i += run
            continue
        j = i + 1
        while j < n and j - i < 128 and not (j + 1 < n and flat[j] == flat[j + 1]):
            j += 1
        packets += bytes((j - i - 1,)) + b''.join(flat[i:j])
        i = j
    return header + bytes(packets)


class ArtTests(unittest.TestCase):
    def test_layout_matches_the_grid(self):
        text = LOGIC.read_text()

        def const(name):
            return int(re.search(rf'\b{name}\s*=\s*(\d+)', text).group(1))

        self.assertEqual((art.BOX, art.STRIDE, art.LEFT, art.WIDE, art.TALL),
                         (const('kBox'), const('kStride'), const('kLeft'), const('kGridWide'), const('kGridTall')))
        rows = re.search(r'kRowTop\[3\]\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+)', text).groups()
        self.assertEqual(art.ROW_TOP, tuple(int(v) for v in rows))
        # the four columns end where the description box (36, 303 px wide) ends: 5 px gaps
        self.assertEqual(art.LEFT + 3 * art.STRIDE + art.BOX, 36 + 303)
        # the examine picture covers the moved description box (old top frame 353), 1 px
        # above its top frame line: there its drawn-in text is centred (measured live)
        self.assertEqual((36, 353 + art.DESC_SHIFT - 1), (const('kCheckX'), const('kCheckY')))

    def test_compose(self):
        out = art.compose(synthetic(), three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        self.assertEqual(lum(out.get(40, 40)), 230)          # BAG stays
        self.assertLess(lum(out.get(134, 120)), 25)          # old box 1's right border is gone (inside new box 2)
        self.assertEqual(lum(out.get(40, 227)), 230)         # POCKETS moved: text top 227
        self.assertLess(lum(out.get(40, 226)), 25)
        self.assertEqual(lum(out.get(40, 342)), 230)         # ITEM DESCRIPTION moved: text top 339
        self.assertEqual(lum(out.get(100, 357)), 100)        # the description box frame, 4 px lower
        self.assertLess(lum(out.get(100, 353)), 25)          # its old top border is gone
        for slot in range(1, 13):
            x, y = art.slot_origin(slot)
            self.assertEqual(lum(out.get(x, y + 30)), 100, slot)        # left border
            self.assertEqual(lum(out.get(x + 71, y + 30)), 100, slot)   # right border
            self.assertEqual(out.get(x + 30, y + 30)[3], 198, slot)     # inside: the old inside
        self.assertEqual(out.get(370, 240), synthetic().get(370, 240))  # the gap column is untouched

    def test_compose_three(self):
        out = art.compose(synthetic(three=True), three=True)
        x, y = art.slot_origin(1)
        self.assertEqual(lum(out.get(x, y + 30)), 100)
        for slot in (4, 5, 12):                             # only boxes 1..3
            x, y = art.slot_origin(slot)
            self.assertLess(lum(out.get(x, y + 30)), 25, slot)
        self.assertLess(lum(out.get(40, 232)), 25)          # no POCKETS in the middle
        self.assertEqual(lum(out.get(40, 342)), 230)

    def test_write_read_and_generate(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            vgui = game / 'cryoffear/gfx/vgui'
            polish = game / 'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui'
            art.write_tga(vgui / '640_inventory.tga', synthetic())
            art.write_tga(vgui / '640_inventory_three.tga', synthetic(three=True))
            art.write_tga(polish / '640_inventory.tga', synthetic())
            chinese = game / 'cryoffear/languages/schinese/overlay/cryoffear/gfx/vgui'
            art.write_tga(chinese / '640_inventory_three.tga', synthetic(three=True))
            (game / 'cryoffear/languages/german/strings').mkdir(parents=True)   # a pack without art
            written = art.generate(game)
            self.assertEqual(sorted(p.relative_to(game).as_posix() for p in written), sorted([
                'cryoffear/gfx/vgui/inventory12_left.tga',
                'cryoffear/gfx/vgui/inventory12_full.tga',
                'cryoffear/gfx/vgui/inventory12_left_three.tga',
                'cryoffear/gfx/vgui/inventory12_full_three.tga',
                'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/inventory12_left.tga',
                'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/inventory12_full.tga',
                'cryoffear/languages/schinese/overlay/cryoffear/gfx/vgui/inventory12_left_three.tga',
                'cryoffear/languages/schinese/overlay/cryoffear/gfx/vgui/inventory12_full_three.tga']))
            back = art.read_tga(vgui / 'inventory12_left.tga')
            left = art.compose(synthetic(), three=False)
            self.assertEqual(back.pixels, left.pixels)
            # the whole background for the grid to show instead of the retail one: our left half
            # (no retail POCKETS under the grid any more), the retail right half untouched
            whole = art.read_tga(vgui / 'inventory12_full.tga')
            src = synthetic()
            self.assertEqual((whole.wide, whole.tall), (src.wide, src.tall))
            for y in (0, 100, 232, 479):
                for x in (0, 40, 372):
                    self.assertEqual(whole.get(x, y), left.get(x, y), (x, y))
                for x in (373, 500, src.wide - 1):
                    self.assertEqual(whole.get(x, y), src.get(x, y), (x, y))
            self.assertLess(lum(whole.get(40, 200)), lum(src.get(40, 200)))   # the old POCKETS label is gone
            raw = (vgui / 'inventory12_left.tga').read_bytes()
            self.assertEqual((raw[2], raw[16], raw[17]), (2, 32, 8))  # uncompressed, 32-bit, bottom row first
            self.assertEqual(art.read_tga(vgui / '640_inventory.tga').pixels, synthetic().pixels)  # retail untouched

    def test_no_retail_art_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                art.generate(Path(tmp))

    @unittest.skipUnless(RETAIL.is_file(), 'the retail inventory art is not installed locally')
    def test_retail_art(self):
        out = art.compose(art.read_tga(RETAIL), three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        self.assertLess(lum(out.get(134, 120)), 25)
        x, y = art.slot_origin(12)
        self.assertGreater(lum(out.get(x, y + 30)), 60)

    def test_read_rle_tga(self):
        src = synthetic()
        for bpp in (32, 24):
            expect = bytearray(src.pixels)
            if bpp == 24:
                expect[3::4] = bytes([255]) * (len(expect) // 4)
            for top_first in (False, True):
                with tempfile.TemporaryDirectory() as tmp:
                    path = Path(tmp) / 'rle.tga'
                    path.write_bytes(rle_tga(src, bpp, top_first))
                    self.assertEqual(art.read_tga(path).pixels, expect, (bpp, top_first))

    def test_truncated_rle_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'rle.tga'
            data = rle_tga(synthetic(), 32)
            path.write_bytes(data[:len(data) // 2])
            with self.assertRaises(ValueError):
                art.read_tga(path)

    def test_unreadable_polish_overlay_is_skipped(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            vgui = game / 'cryoffear/gfx/vgui'
            polish = game / 'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui'
            art.write_tga(vgui / '640_inventory.tga', synthetic())
            polish.mkdir(parents=True)
            (polish / '640_inventory.tga').write_bytes(b'\x00' * 40)
            err = io.StringIO()
            with redirect_stderr(err):
                written = art.generate(game)
            self.assertEqual([p.relative_to(game).as_posix() for p in written],
                             ['cryoffear/gfx/vgui/inventory12_left.tga', 'cryoffear/gfx/vgui/inventory12_full.tga'])
            self.assertIn('640_inventory.tga', err.getvalue())
            self.assertEqual(len(err.getvalue().strip().splitlines()), 1)
            (vgui / '640_inventory.tga').write_bytes(b'\x00' * 40)   # the main folder still fails hard
            with self.assertRaises(Exception):
                art.generate(game)

    @unittest.skipUnless(POLISH.is_file(), 'the Polish overlay inventory art is not installed locally')
    def test_polish_overlay_art(self):
        src = art.read_tga(POLISH)
        out = art.compose(src, three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        art.bright_box(src, art.POCKETS_SEARCH)
        art.bright_box(src, art.ITEMDESC_SEARCH)


if __name__ == '__main__':
    unittest.main()
