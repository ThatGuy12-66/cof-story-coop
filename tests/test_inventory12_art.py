"""The 12-slot inventory's left-half art (tools/make_inventory12_art.py)."""
import re
import tempfile
import unittest
from pathlib import Path

from tools import make_inventory12_art as art

ROOT = Path(__file__).resolve().parents[1]
LOGIC = (ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a'
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


class ArtTests(unittest.TestCase):
    def test_layout_matches_the_grid(self):
        text = LOGIC.read_text()

        def const(name):
            return int(re.search(rf'\b{name}\s*=\s*(\d+)', text).group(1))

        self.assertEqual((art.BOX, art.STRIDE, art.LEFT, art.WIDE, art.TALL),
                         (const('kBox'), const('kStride'), const('kLeft'), const('kGridWide'), const('kGridTall')))
        rows = re.search(r'kRowTop\[3\]\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+)', text).groups()
        self.assertEqual(art.ROW_TOP, tuple(int(v) for v in rows))
        # the examine picture sits 5 px up-left of the moved description box frame (old top 353)
        self.assertEqual(353 + art.DESC_SHIFT - 5, const('kCheckY'))

    def test_compose(self):
        out = art.compose(synthetic(), three=False)
        self.assertEqual((out.wide, out.tall), (373, 480))
        self.assertEqual(lum(out.get(40, 40)), 230)          # BAG stays
        self.assertLess(lum(out.get(134, 120)), 25)          # old box 1's right border is gone (inside new box 2)
        self.assertEqual(lum(out.get(40, 232)), 230)         # POCKETS moved: text top 227
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
            written = art.generate(game)
            self.assertEqual(sorted(p.relative_to(game).as_posix() for p in written), sorted([
                'cryoffear/gfx/vgui/inventory12_left.tga',
                'cryoffear/gfx/vgui/inventory12_left_three.tga',
                'cryoffear/languages/polish/overlay/cryoffear/gfx/vgui/inventory12_left.tga']))
            back = art.read_tga(vgui / 'inventory12_left.tga')
            self.assertEqual(back.pixels, art.compose(synthetic(), three=False).pixels)
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


if __name__ == '__main__':
    unittest.main()
