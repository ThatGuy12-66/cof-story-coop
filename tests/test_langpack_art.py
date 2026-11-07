"""1.5: the repainted art of the Russian and Chinese packs (tools/langpack/art/make_art.py and its specs)."""
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "tools/langpack/art"

try:
    import cv2  # noqa: F401
    import numpy as np
    from PIL import Image
    from tools.langpack.art import make_art
except ImportError:   # the art tools need Pillow, numpy and OpenCV; the release machine may not have them
    make_art = None


class SpecTests(unittest.TestCase):
    def test_every_card_has_both_languages(self):
        cards = json.loads((ART / "cards.json").read_text(encoding="utf-8"))
        self.assertGreater(len(cards), 60)
        for rel, card in cards.items():
            self.assertTrue(rel.startswith("gfx/vgui/") and "_check" in rel.lower(), rel)
            for lang in ("english", "russian", "schinese"):
                self.assertTrue(card[lang] and all(isinstance(line, str) for line in card[lang]), (rel, lang))

    def test_every_label_block_has_both_languages_or_neither(self):
        labels = json.loads((ART / "labels.json").read_text(encoding="utf-8"))
        self.assertGreater(len(labels), 100)
        for rel, spec in labels.items():
            self.assertTrue(rel.split("/")[0] in ("overlay", "textures", "models"), rel)
            for block in spec["blocks"]:
                self.assertEqual(len(block["box"]), 4, rel)
                self.assertEqual(block.get("russian") is None, block.get("schinese") is None, rel)

    def test_the_specs_write_the_json(self):
        # cards.json and labels.json are generated: the specs and the JSON must agree
        import runpy
        for spec, data in (("cards_spec.py", "cards.json"), ("labels_spec.py", "labels.json")):
            ns = runpy.run_path(str(ART / spec), run_name="spec")
            table = ns.get("CARDS") or ns.get("L")
            written = json.loads((ART / data).read_text(encoding="utf-8"))
            self.assertEqual(sorted(table), sorted(written), spec)


@unittest.skipIf(make_art is None, "Pillow, numpy and OpenCV are not installed")
class MakeArtTests(unittest.TestCase):
    def test_chinese_breaks_keep_punctuation(self):
        toks = make_art.tokens("你在哪儿？尽快回家，天快黑了。")
        self.assertNotIn("？", toks)                  # a closing mark never starts a piece
        self.assertTrue(any(t.endswith("？") for t in toks))
        self.assertIn("Forum ", make_art.tokens("开往 Forum Necka"))

    def test_russian_one_letter_words_cling(self):
        toks = make_art.tokens("Ключ с биркой и надписью")
        self.assertIn("с биркой ", toks)
        self.assertIn("и надписью", toks)

    def test_wrap_balances_lines(self):
        lines = make_art.wrap("один два три четыре пять шесть", 120, lambda s: len(s) * 6)
        self.assertEqual(len(lines), 2)
        self.assertLess(abs(len(lines[0]) - len(lines[1])), 8)

    def test_rle_alpha_is_read_inverted_only_with_alpha(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = np.zeros((2, 2, 4), np.uint8)
            a[..., 3] = 200
            p = Path(tmp) / "a.tga"
            Image.fromarray(a, "RGBA").save(p, "TGA", compression="tga_rle")
            self.assertEqual(int(make_art.read_tga(p)[0, 0, 3]), 55)
            Image.fromarray(a[..., :3], "RGB").save(p, "TGA", compression="tga_rle")
            self.assertEqual(int(make_art.read_tga(p)[0, 0, 3]), 255)
            make_art.write_tga(p, a)
            head = p.read_bytes()[:18]
            self.assertEqual((head[2], head[16], head[17] & 0x20), (2, 32, 0))   # uncompressed, 32-bit, bottom-up

    def test_a_model_texture_keeps_its_palette_and_mask(self):
        pal = np.zeros((256, 3), np.uint8)
        pal[1] = (200, 0, 0)
        pal[2] = (0, 0, 200)
        pal[255] = (0, 0, 255)
        idx = np.array([[1, 1, 255], [2, 1, 255]], np.uint8)
        info = dict(kind="bmp", palette=pal, indices=idx, masked=True)
        rgba = np.concatenate([pal[idx], np.where(idx[..., None] == 255, 0, 255).astype(np.uint8)], -1)
        rgba[0, 1, :3] = (10, 10, 190)      # repainted: nearest palette colour is 2
        rgba[1, 2, 3] = 255                 # a masked pixel made solid
        rgba[1, 2, :3] = (190, 5, 5)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "t.bmp"
            make_art.write_out(p, rgba, info)
            back = np.asarray(Image.open(p))
            self.assertEqual(back.tolist(), [[1, 2, 255], [2, 1, 1]])
            self.assertEqual(Image.open(p).getpalette()[:9], [0, 0, 0, 200, 0, 0, 0, 0, 200])

    def test_the_polish_repaint_gives_the_background_back(self):
        rng = np.random.default_rng(3)
        bg = (rng.normal(120, 6, (40, 120, 3))).clip(0, 255).astype(np.uint8)
        en, pl = bg.copy(), bg.copy()
        for x in range(10, 60, 6):              # the English letters: thin strokes
            en[10:30, x:x + 3] = 20
        for y in range(12, 28, 5):              # the Polish ones, elsewhere, crossing the English a little
            pl[y:y + 2, 50:100] = 20
        to_rgba = lambda a: np.concatenate([a, np.full(a.shape[:2] + (1,), 255, np.uint8)], -1)
        p = make_art.premul(to_rgba(en))
        block = {"box": [0, 0, 120, 40], "remove": "polish"}
        out = make_art.remove_block(p, to_rgba(en), block, to_rgba(pl))
        rgb = (out[..., :3] * 255).round()
        self.assertLess(np.abs(rgb[10:30, 10:48] - bg[10:30, 10:48]).max(), 1)   # only English there: exact
        self.assertLess(np.abs(rgb - bg).mean(), 2)
        self.assertEqual(block["_ink"][:2], (10, 10))


if __name__ == "__main__":
    unittest.main()
