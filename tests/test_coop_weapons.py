import json
import math
import struct
import tempfile
import unittest
from pathlib import Path

from cofcoop import core
from tools import prepare_coop_weapons as weapons
from tools import prepare_simon_models as simon

GAME_ROOT = core.DEFAULT_GAME_DIR
SOURCES_PRESENT = all((GAME_ROOT / "cryoffear" / path).exists() for path in weapons.SOURCES)
# (1.1.9: the gas mask's fit with the real models is checked in tests/test_coop_gasmask.py)


def angle(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, weapons.dot(weapons.normalize(a), weapons.normalize(b))))))


class StudioMathsTests(unittest.TestCase):
    def test_bone_angles_round_trip_through_the_hlsdk_quaternion(self):
        for value in ((1.0, 2.0, 3.0, 0.3, -1.1, 2.5), (0, 0, 0, -1.302, -0.028, 1.667), (0, 0, 0, 0.2, 1.2, -0.4)):
            matrix = weapons.bone_matrix(value)
            again = weapons.bone_matrix(weapons.bone_value(matrix))
            for row, other in zip(matrix, again):
                for a, b in zip(row, other):
                    self.assertAlmostEqual(a, b, places=6)

    def test_angle_quaternion_matches_hlsdk_axis_order(self):
        # yaw 90 degrees turns +X into +Y; pitch 90 turns +X into -Z; roll 90 turns +Y into +Z
        self.assertTrue(all(abs(a - b) < 1e-9 for a, b in zip(weapons.rotate(weapons.bone_matrix((0, 0, 0, 0, 0, math.pi / 2)), (1, 0, 0)), (0, 1, 0))))
        self.assertTrue(all(abs(a - b) < 1e-9 for a, b in zip(weapons.rotate(weapons.bone_matrix((0, 0, 0, 0, math.pi / 2, 0)), (1, 0, 0)), (0, 0, -1))))
        self.assertTrue(all(abs(a - b) < 1e-9 for a, b in zip(weapons.rotate(weapons.bone_matrix((0, 0, 0, math.pi / 2, 0, 0)), (0, 1, 0)), (0, 0, 1))))

    def test_grip_solver_maps_item_axes_and_point_into_the_fist(self):
        item = ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (5.0, -2.0, 1.0))
        grip = weapons.solve_grip(item, {"sequence": None, "primary": ("hand", (0, 0, 1)), "secondary": ("hand", (-1, 0, 0))}, None)
        self.assertLess(max(abs(a - b) for a, b in zip(weapons.transform(grip, item[2]), weapons.FIST)), 1e-9)
        self.assertLess(angle(weapons.rotate(grip, item[0]), (0, 0, 1)), 1e-6)
        self.assertLess(angle(weapons.rotate(grip, item[1]), (-1, 0, 0)), 1e-6)

    def test_mirror_is_a_proper_half_turn_that_keeps_the_fist(self):
        m = weapons.MIRROR
        det = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
               + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
        self.assertAlmostEqual(det, 1.0)
        self.assertEqual(weapons.transform(m, weapons.FIST), weapons.FIST)

    def test_written_model_reads_back_and_validates(self):
        part = {"name": "studio", "model": "box", "verts": [(0, 0, 0), (4, 0, 0), (0, 4, 0)], "norms": [(0, 0, 1)],
                "meshes": [{"skinref": 0, "numnorms": 1, "numtris": 1, "commands": [(3, [(0, 0, 0, 0), (1, 0, 3, 0), (2, 0, 0, 3)])]}]}
        texture = {"name": "t.bmp", "flags": 0, "width": 8, "height": 4, "pixels": bytes(range(32)), "palette": bytes(768)}
        bones = [("Bip01", -1, (0,) * 6, (1,) * 6), ("SHOTGUN", 0, (1, 2, 3, 0.1, 0.2, 0.3), (1,) * 6)]
        data = weapons.write_studio("t.mdl", bones, 1, [part], [texture], [0], (("", (1, 0, 0)),))
        report = simon.validate_model(data)
        self.assertEqual((report["bones"], report["sequences"], report["textures"]), (2, 1, 1))
        model = weapons.Studio(data)
        self.assertEqual([b["name"] for b in model.bones], ["Bip01", "SHOTGUN"])
        texture_data = struct.unpack_from("<i", data, 188)[0]
        self.assertEqual(texture_data + 32 + 768, len(data))  # texture data is last
        parts = weapons.read_mesh(model, root_bone="SHOTGUN")
        self.assertEqual(parts[0]["meshes"][0]["commands"], part["meshes"][0]["commands"])
        for got, want in zip(parts[0]["verts"], part["verts"]):
            self.assertLess(max(abs(a - b) for a, b in zip(got, want)), 1e-5)

    def test_shrink_texture_halves_with_block_majority(self):
        texture = {"name": "t", "flags": 0, "width": 4, "height": 4, "palette": bytes(768),
                   "pixels": bytes([1, 1, 2, 3, 1, 9, 3, 3, 5, 5, 6, 6, 5, 7, 6, 8])}
        small = weapons.shrink_texture(texture, limit=2)
        self.assertEqual((small["width"], small["height"], small["pixels"]), (2, 2, bytes([1, 3, 5, 6])))


@unittest.skipUnless(SOURCES_PRESENT, "User-owned model inputs are optional")
class CoopWeaponModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = weapons.read_sources(GAME_ROOT)
        cls.outputs = weapons.build_models(cls.files)
        cls.manifest = json.loads(cls.outputs["coop-weapons.json"])
        simon_bytes, _ = simon.adapt_sequences(cls.files["models/cutscene/player.mdl"], cls.files["models/player/police1/police1.mdl"])
        cls.simon = weapons.Studio(simon_bytes)
        cls.police = weapons.Studio(cls.files["models/player/police1/police1.mdl"])
        cls.models = {path: weapons.Studio(data) for path, data in cls.outputs.items() if path.endswith(".mdl")}

    def merged_item(self, path, sequence, player=None):
        """World matrix of the output's item bone merged onto a posed player (frame 0, pitch 0)."""
        player = player or self.simon
        model = self.models[path]
        world = weapons.merge_bones(player, weapons.player_pose(player, player.sequence(sequence)), model)
        return world[len(model.bones) - 1], world

    def test_expected_outputs_with_short_paths_and_matching_manifest(self):
        names = set(weapons.STORY_ITEMS) | set(weapons.LONG_GUNS) | set(weapons.DUAL_ITEMS) | {weapons.GASMASK}
        self.assertEqual(set(self.models), {f"models/coop/{name}.mdl" for name in names})
        self.assertEqual(set(self.models), set(weapons.output_paths()))
        for name in ("p_axe", "p_famas", "p_lantern", "p_flare", "p_mobile", "p_camera", "p_booklaser", "p_browning",
                     "p_rifle", "p_shotgun", "l_flashlight", "l_mobile", "l_lantern", "l_flare", "l_glock"):
            self.assertIn(f"models/coop/{name}.mdl", self.models)
        for path, data in self.outputs.items():
            if not path.endswith(".mdl"):
                continue
            with self.subTest(path=path):
                self.assertLess(len(path), 64)
                record = self.manifest["outputs"][path]
                self.assertEqual(record["sha256"], weapons.sha(data))
                self.assertEqual(simon.validate_model(data)["sequences"], 1)
                self.assertTrue(all(source["path"] in {**weapons.SOURCES, **weapons.OPTIONAL_SOURCES}
                                    for source in record["sources"]))
                self.assertLess(len(data), 600_000)

    def test_bones_merge_onto_every_simon_model_and_police1(self):
        simons = [weapons.Studio(simon.adapt_sequences(self.files["models/cutscene/player.mdl"],
                                                       self.files["models/player/police1/police1.mdl"], name)[0])
                  for name in simon.MODEL_NAMES]
        for other in simons:  # the costumes differ only in textures: same skeleton and poses
            for sequence in ("pistol_idle", "rifle_idle", "branch_idle"):
                self.assertEqual(weapons.hand_matrix(other, sequence), weapons.hand_matrix(self.simon, sequence))
        players = [{b["name"] for b in model.bones} for model in simons + [self.police]]
        for path, model in self.models.items():
            with self.subTest(path=path):
                names = [b["name"] for b in model.bones]
                side = "L" if path.split("/")[-1].startswith("l_") else "R"
                chain = weapons.HEAD_CHAIN if path.split("/")[-1].startswith("h_") else \
                    [pattern.format(side=side) for pattern in weapons.ARM_CHAIN]
                self.assertEqual(names[:-1], list(chain))
                for player in players:
                    self.assertTrue(set(names[:-1]) <= player)
                    self.assertNotIn(names[-1], player)
                data = model.data
                count, offset = struct.unpack_from("<ii", data, 204)
                for part in range(count):
                    _n, _b, record = struct.unpack_from("<iii", data, offset + 76 * part + 64)
                    numverts, vertinfo = struct.unpack_from("<ii", data, record + 80)
                    self.assertEqual(set(data[vertinfo:vertinfo + numverts]), {len(names) - 1})

    def test_mesh_sits_in_the_hand_in_the_bind_pose(self):
        for path, model in self.models.items():
            if path.split("/")[-1].startswith("h_"):
                continue  # the gas mask is on the head: test_gasmask_covers_simons_face
            with self.subTest(path=path):
                world = weapons.world_bones(model, weapons.local_pose(model, 0))
                hand = world[len(model.bones) - 2]
                fist = weapons.transform(hand, weapons.FIST)
                verts = [v for part in weapons.read_mesh(model) for v in part["verts"]]
                self.assertLess(min(math.dist(v, weapons.transform(hand, (0, 0, 0))) for v in verts), 7.0)
                # the fist centre is on the item: inside its box (item bone space) with 1.5 units to spare
                item = [v for part in weapons.read_mesh(model, root_bone=model.bones[-1]["name"]) for v in part["verts"]]
                local = weapons.transform(weapons.invert(world[-1]), fist)
                for axis in range(3):
                    self.assertGreater(local[axis], min(v[axis] for v in item) - 1.5)
                    self.assertLess(local[axis], max(v[axis] for v in item) + 1.5)

    def test_stock_rifle_reproduces_the_research_numbers(self):
        """Cross-check of the maths against thirdperson-weapons-re.md (pose.py, numpy): the stock
        p_rifle points ~40 degrees up on Simon and level on police1."""
        rifle = weapons.Studio(self.files["police/models/weapons/rifle/p_rifle.mdl"])
        for player, expected in ((self.simon, (0.77, 0.02, 0.64)), (self.police, (1.0, 0.01, 0.01))):
            world = weapons.merge_bones(player, weapons.player_pose(player, player.sequence("rifle_idle")), rifle)
            shotgun = world[rifle.bone("SHOTGUN")]
            verts = [weapons.transform(shotgun, v) for part in weapons.read_mesh(rifle, root_bone="SHOTGUN") for v in part["verts"]]
            axis = weapons.principal_axis(verts)
            self.assertLess(max(abs(a - b) for a, b in zip(axis, expected)), 0.02)

    def test_long_guns_point_along_the_view_on_simon(self):
        for name, spec in weapons.LONG_GUNS.items():
            path = f"models/coop/{name}.mdl"
            with self.subTest(gun=name):
                item, _world = self.merged_item(path, spec["game"])
                model = self.models[path]
                verts = [weapons.transform(item, v) for part in weapons.read_mesh(model, root_bone="SHOTGUN") for v in part["verts"]]
                barrel = weapons.principal_axis(verts)
                self.assertLess(angle(barrel, (1, 0, 0)), 2.0)
                # the gun's top: hand +Z (the thumb side) square to the barrel as the police grip holds it
                stock = self.files[spec["source"]]
                stock_model = weapons.Studio(stock)
                index = model.bone("SHOTGUN")
                turn = weapons.concat(weapons.bone_matrix(model.bones[index]["value"]),
                                      weapons.invert(weapons.bone_matrix(stock_model.bones[index]["value"])))
                held = weapons.principal_axis([v for part in weapons.read_mesh(stock_model, root_bone="Bip01 R Hand") for v in part["verts"]])
                top = weapons.normalize(tuple(z - held[2] * b for z, b in zip((0, 0, 1), held)))
                up = weapons.rotate(weapons.hand_matrix(self.simon, spec["game"]), weapons.rotate(turn, top))
                self.assertLess(angle(up, (0, 0, 1)), 3.0)
                changed = {i for i, (a, b) in enumerate(zip(stock, self.outputs[path])) if a != b}
                offset = model.bones[model.bone("SHOTGUN")]["offset"] + 64
                self.assertTrue(changed)
                self.assertLessEqual(changed, set(range(offset, offset + 24)))

    def test_items_aligned_in_their_design_pose(self):
        """World directions in Simon's pose for the sequence each grip was designed for."""
        cases = [("p_famas", "pistol_idle", "R", (1, 0, 0), 2.0), ("p_browning", "pistol_idle", "R", (1, 0, 0), 2.0),
                 ("p_lantern", "branch_idle", "R", (0, 0, 1), 2.0)]
        for name in ("l_flashlight", "l_flare", "l_glock", "l_p345", "l_vp70", "l_revolver"):
            cases.append((name, weapons.DUAL_SEQUENCE, "L", (1, 0, 0), 2.0))
        cases += [("l_mobile", weapons.DUAL_SEQUENCE, "L", (0, 0, 1), 2.0), ("l_lantern", weapons.DUAL_SEQUENCE, "L", (0, 0, 1), 2.0),
                  ("r_flashlight", weapons.DUAL_SEQUENCE, "R", (1, 0, 0), 2.0), ("r_lantern", weapons.DUAL_SEQUENCE, "R", (0, 0, 1), 2.0)]
        for name, sequence, side, expected, tolerance in cases:
            path = f"models/coop/{name}.mdl"
            with self.subTest(item=name):
                item_world, _ = self.merged_item(path, sequence)
                model = self.models[path]
                if name in weapons.STORY_ITEMS:
                    primary, point = weapons.STORY_ITEMS[name]["item"][0], weapons.STORY_ITEMS[name]["item"][2]
                elif weapons.DUAL_ITEMS[name]["source"] in weapons.STORY_ITEMS:
                    primary, point = weapons.STORY_ITEMS[weapons.DUAL_ITEMS[name]["source"]]["item"][0::2]
                elif name in ("l_flashlight", "r_flashlight"):
                    primary, point = (0, 0, 1), weapons.FIST
                else:
                    held = weapons.read_mesh(weapons.Studio(self.files[weapons.DUAL_ITEMS[name]["source"]]), root_bone="Bip01 R Hand")
                    primary, point = weapons.principal_axis([v for part in held for v in part["verts"]]), weapons.FIST
                self.assertLess(angle(weapons.rotate(item_world, primary), expected), tolerance)
                hand = weapons.hand_matrix(self.simon, sequence, side)
                self.assertLess(math.dist(weapons.transform(item_world, point), weapons.transform(hand, weapons.FIST)), 0.05)
                self.assertEqual(model.bones[-2]["name"], f"Bip01 {side} Hand")

    def test_hand_held_items_read_right_in_the_game_and_suggested_poses(self):
        def world_axis(name, sequence, index):
            item_world, _ = self.merged_item(f"models/coop/{name}.mdl", sequence)
            return weapons.rotate(item_world, weapons.STORY_ITEMS[name]["item"][index])
        self.assertLess(angle(world_axis("p_mobile", "radio_idle", 0), (0, 0, 1)), 25)    # phone upright
        self.assertGreater(-world_axis("p_mobile", "radio_idle", 1)[0], 0.8)               # screen to the player
        self.assertLess(angle(world_axis("p_camera", "radio_idle", 0), (1, 0, 0)), 25)    # lens forward
        self.assertLess(angle(world_axis("p_booklaser", "radio_idle", 0), (0, 0, 1)), 25)  # book upright
        self.assertGreater(world_axis("p_axe", "sledgehammer_idle", 0)[2], 0.4)           # head raised
        self.assertGreater(world_axis("p_flare", "idle", 0)[0], 0.5)                      # lit end forward
        self.assertLess(angle(world_axis("p_famas", "pistol_idle", 1), (0, 0, 1)), 3)     # no cant

    def test_mirrored_melee_keeps_the_police_grip_in_the_left_fist(self):
        for name in ("l_switchblade", "l_nightstick", "l_branch"):
            with self.subTest(item=name):
                model = self.models[f"models/coop/{name}.mdl"]
                police = weapons.Studio(self.files[weapons.DUAL_ITEMS[name]["source"]])
                right = [v for part in weapons.read_mesh(police, root_bone="Bip01 R Hand") for v in part["verts"]]
                left = [v for part in weapons.read_mesh(model, root_bone="Bip01 L Hand") for v in part["verts"]]
                mirrored = [weapons.transform(weapons.MIRROR, v) for v in right]
                self.assertLess(max(math.dist(a, b) for a, b in zip(left, mirrored)), 1e-3)

    def test_unknown_or_tampered_sources_are_refused(self):
        tampered = dict(self.files)
        tampered["models/weapons/axe/w_axe.mdl"] = tampered["models/weapons/axe/w_axe.mdl"][:-1]
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            weapons.build_models(tampered)
        missing = dict(self.files)
        del missing["models/weapons/famas/w_famas.mdl"]
        with self.assertRaisesRegex(ValueError, "missing"):
            weapons.build_models(missing)
        # 1.1.9: the gas mask's source is optional: modded or missing, only h_gasmask is left out
        for files in ({**self.files, weapons.GASMASK_SOURCE: b"modded"},
                      {k: v for k, v in self.files.items() if k != weapons.GASMASK_SOURCE}):
            outputs = weapons.build_models(files)
            self.assertNotIn("models/coop/h_gasmask.mdl", outputs)
            self.assertEqual(set(outputs) | {"models/coop/h_gasmask.mdl"}, set(self.outputs))
            self.assertEqual([s["path"] for s in json.loads(outputs["coop-weapons.json"])["skipped"]], ["models/coop/h_gasmask.mdl"])

    def test_cli_refuses_an_output_outside_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(weapons.main(["--game-dir", str(GAME_ROOT), "--out", str(Path(temporary) / "out")]), 1)


if __name__ == "__main__":
    unittest.main()
