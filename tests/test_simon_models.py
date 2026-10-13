import hashlib
import json
import struct
import unittest

from cofcoop import core
from tools import prepare_simon_models as models

MODEL_ROOT = core.DEFAULT_GAME_DIR / "cryoffear/models"


class TextureConversionTests(unittest.TestCase):
    def test_tga_bottom_origin_and_bgr_are_normalized(self):
        header = bytearray(18)
        header[2] = 2
        struct.pack_into("<HH", header, 12, 2, 2)
        header[16] = 24
        # Bottom row blue/white, top row red/green, in BGR format.
        pixels = bytes((255, 0, 0, 255, 255, 255, 0, 0, 255, 0, 255, 0))
        width, height, rgb = models.decode_tga(bytes(header) + pixels)
        self.assertEqual((width, height), (2, 2))
        self.assertEqual(rgb, [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)])
        indexed, palette = models.quantize(rgb)
        restored = [tuple(palette[index * 3:index * 3 + 3]) for index in indexed]
        self.assertEqual(restored, rgb)

    def test_invalid_tga_and_truncated_pixels_are_refused(self):
        with self.assertRaises(ValueError):
            models.decode_tga(b"not a TGA")
        header = bytearray(18)
        header[2], header[16] = 2, 24
        struct.pack_into("<HH", header, 12, 512, 512)
        with self.assertRaises(ValueError):
            models.decode_tga(header)


@unittest.skipUnless((MODEL_ROOT / "cutscene/player.mdl").exists(), "User-owned model inputs are optional")
class SimonModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.simon = (MODEL_ROOT / "cutscene/player.mdl").read_bytes()
        cls.police = (MODEL_ROOT / "player/police1/police1.mdl").read_bytes()
        cls.donors = {name: (MODEL_ROOT / "costumes/Sick_Simon" / name).read_bytes() for name, _sha in models.TEXTURES.values()}
        cls.files = models.build_models(cls.simon, cls.police, cls.donors)
        cls.standard = cls.files["models/player/cof_s1/cof_s1.mdl"]
        cls.alternate = cls.files["models/player/cof_s2/cof_s2.mdl"]

    def test_six_character_network_names_change_only_embedded_name(self):
        self.assertEqual(models.MODEL_NAMES, ("cof_s1", "cof_s2"))
        for name, data in zip(models.MODEL_NAMES, (self.standard, self.alternate)):
            self.assertEqual(len(name), 6)
            self.assertEqual(data[8:72].split(b"\0", 1)[0].decode(), name + ".mdl")
        old, _ = models.adapt_sequences(self.simon, self.police, "cof_simon")
        self.assertEqual(self.standard[:8], old[:8])
        self.assertEqual(self.standard[72:], old[72:])

    def test_complete_native_sequence_order_and_all_animation_channels_validate(self):
        expected = [models.label(record) for record in models.sequences(self.police)]
        self.assertEqual(len(expected), 85)
        self.assertEqual([models.label(record) for record in models.sequences(self.standard)], expected)
        for data in (self.standard, self.alternate):
            report = models.validate_model(data)
            self.assertEqual(report["sequences"], 85)
            self.assertEqual(report["bones"], 36)
            self.assertGreater(report["validated_animation_channels"], 8000)
            self.assertEqual(struct.unpack_from("<ii", data, 236), (0, 0))

    def test_aliases_preserve_donor_animation_event_and_blend_offsets(self):
        source = {models.label(record): record for record in models.sequences(self.simon)}
        for record in models.sequences(self.standard):
            native = models.label(record)
            donor = source[models.mapped_animation(native)]
            with self.subTest(sequence=native):
                self.assertEqual(record[32:], donor[32:])

    def test_original_skeleton_meshes_and_animation_payload_are_byte_exact(self):
        allowed = set(range(8, 76)) | set(range(164, 172)) | set(range(236, 244))
        changed = {i for i, (a, b) in enumerate(zip(self.simon, self.standard)) if a != b}
        self.assertLessEqual(changed, allowed)
        self.assertEqual(hashlib.sha256(self.simon).hexdigest(), models.SIMON_SHA)
        self.assertEqual(len(self.standard), len(self.simon) + 85 * 176)

    def test_alternate_changes_only_name_and_compatible_costume_pixels(self):
        allowed = set(range(8, 72))
        info = models.texture_info(self.standard)
        for texture in models.TEXTURES:
            item = info[texture]
            allowed.update(range(item["offset"], item["offset"] + item["width"] * item["height"] + 768))
        changed = {i for i, (a, b) in enumerate(zip(self.standard, self.alternate)) if a != b}
        self.assertTrue(changed)
        self.assertLessEqual(changed, allowed)
        self.assertEqual(models.sequences(self.standard), models.sequences(self.alternate))
        report = json.loads(self.files["model-report.json"])
        self.assertEqual(len(report["costume"]), 3)
        self.assertIn("fallback", report["sequence_mapping"][0])

    def test_tampered_sources_and_costumes_refused(self):
        with self.assertRaises(ValueError):
            models.adapt_sequences(self.simon[:-1], self.police)
        bad = dict(self.donors)
        name = next(iter(bad))
        bad[name] = bad[name][:-1]
        with self.assertRaises(ValueError):
            models.apply_costume(self.standard, bad)

    def test_bad_animation_reference_and_zero_length_run_refused(self):
        corrupt = bytearray(self.standard)
        offset = struct.unpack_from("<i", corrupt, 168)[0]
        struct.pack_into("<i", corrupt, offset + 124, len(corrupt) - 2)
        with self.assertRaisesRegex(ValueError, "animation records"):
            models.validate_model(corrupt)
        corrupt = bytearray(self.standard)
        record = models.sequences(corrupt)[0]
        anim = struct.unpack_from("<i", record, 124)[0]
        for index in range(36):
            position = anim + index * 12
            offsets = struct.unpack_from("<6H", corrupt, position)
            found = next((value for value in offsets if value), None)
            if found:
                corrupt[position + found + 1] = 0
                break
        with self.assertRaisesRegex(ValueError, "animation run"):
            models.validate_model(corrupt)


if __name__ == "__main__":
    unittest.main()
