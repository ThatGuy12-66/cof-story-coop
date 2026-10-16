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
        cls.donors = {rel: (MODEL_ROOT / "costumes" / rel).read_bytes() for rel in models.costume_files()}
        cls.files = models.build_models(cls.simon, cls.police, cls.donors)
        cls.standard = cls.files["models/player/cof_s1/cof_s1.mdl"]
        cls.alternate = cls.files["models/player/cof_s2/cof_s2.mdl"]
        cls.third = cls.files["models/player/cof_s3/cof_s3.mdl"]
        cls.fourth = cls.files["models/player/cof_s4/cof_s4.mdl"]

    def test_six_character_network_names_change_only_embedded_name(self):
        self.assertEqual(models.MODEL_NAMES, ("cof_s1", "cof_s2", "cof_s3", "cof_s4"))
        for name, data in zip(models.MODEL_NAMES, (self.standard, self.alternate, self.third, self.fourth)):
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

    def test_aim_pitch_bends_the_spine_like_police(self):
        # The client writes the aim pitch into controller 3. On Simon's Half-Life skeleton that
        # twisted Bip01 Spine3 sideways; police1 bends Bip01 Spine forward and back instead.
        police_names = models.bone_names(self.police)
        police = [c for c in models.bone_controllers(self.police) if c["index"] == 3][0]
        for data in (self.standard, self.alternate):
            names = models.bone_names(data)
            controller = [c for c in models.bone_controllers(data) if c["index"] == 3]
            self.assertEqual(len(controller), 1)
            self.assertEqual(names[controller[0]["bone"]], police_names[police["bone"]])
            self.assertEqual({k: v for k, v in controller[0].items() if k != "bone"},
                             {k: v for k, v in police.items() if k != "bone"})
            self.assertEqual((police["type"], police["start"], police["end"]), (0x20, -45.0, 45.0))
            links = {(names[bone], dof) for bone, dof in models.controller_links(data, 3)}
            self.assertEqual(links, {("Bip01 Spine", 5)})
            # The yaw controllers 0-2 stay Simon's own.
            for index in range(3):
                self.assertEqual([c for c in models.bone_controllers(data) if c["index"] == index],
                                 [c for c in models.bone_controllers(self.simon) if c["index"] == index])

    def test_original_skeleton_meshes_and_animation_payload_are_byte_exact(self):
        allowed = set(range(8, 76)) | set(range(164, 172)) | set(range(236, 244))
        bones, bone_offset = struct.unpack_from("<ii", self.simon, 140)
        controllers, controller_offset = struct.unpack_from("<ii", self.simon, 148)
        names = models.bone_names(self.simon)
        for bone in (names.index("Bip01 Spine"), names.index("Bip01 Spine3")):
            allowed.update(range(bone_offset + 112 * bone + 40, bone_offset + 112 * bone + 64))
        allowed.update(range(controller_offset, controller_offset + 24 * controllers))
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
        self.assertEqual(len(report["costume"]), 5)
        self.assertIn("fallback", report["sequence_mapping"][0])

    def test_players_three_and_four_wear_their_own_hoodies(self):
        # Everyone after the host wore cof_s2: players 3 and 4 get the ModDB (red) and HLC (light) hoodies.
        info = models.texture_info(self.standard)
        hoodie = info["hoodie_grey.bmp"]
        span = range(hoodie["offset"], hoodie["offset"] + hoodie["width"] * hoodie["height"] + 768)
        seen = set()
        for data in (self.third, self.fourth):
            changed = {i for i, (a, b) in enumerate(zip(self.standard, data)) if a != b}
            self.assertTrue(changed - set(range(8, 72)))
            self.assertLessEqual(changed, set(range(8, 72)) | set(span))
            self.assertEqual(models.sequences(data), models.sequences(self.standard))
            seen.add(data[span.start:span.stop])
        self.assertEqual(len(seen), 2)

    def test_tampered_sources_and_costumes_refused(self):
        with self.assertRaises(ValueError):
            models.adapt_sequences(self.simon[:-1], self.police)
        bad = dict(self.donors)
        name = next(iter(bad))
        bad[name] = bad[name][:-1]
        with self.assertRaises(ValueError):
            models.apply_costume(self.standard, bad, models.COSTUMES["cof_s2"])

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
