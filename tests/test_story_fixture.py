import copy
import hashlib
import json
import unittest

from cofcoop import core
from cofcoop.bsp import parse_entities, load_map_entities
from tools.story_fixture import build_fixture, make_override, inspect_spawn, SOURCES, SPAWNS, GUEST_POSITIONS, LANDMARK_ORIGINS
from tools.verify_story_fixture import verify, parse_status, PHASES, MAPS


def observations():
    result = []
    for i, (phase, map_name) in enumerate(zip(PHASES, MAPS)):
        players = [
            {"userid": 101, "slot": 0, "fakeclient": False, "ready": True, "door_flag": 0,
             "effects": 0, "frozen": False, "health": 83, "origin": [2428, -883, -728],
             "inventory": {"inventoryitems/weapons/weapon_mobile.txt": 1,
                           "inventoryitems/weapons/weapon_switchblade.txt": 1,
                           "inventoryitems/weapons/weapon_glock.txt": 1}},
            {"userid": 202, "slot": 1, "fakeclient": False, "ready": True, "door_flag": 0,
             "effects": 0, "frozen": False, "health": 67, "origin": [2458, -883, -728],
             "inventory": {"inventoryitems/weapons/weapon_mobile.txt": 1,
                           "inventoryitems/weapons/weapon_switchblade.txt": 1,
                           "inventoryitems/weapons/weapon_flare.txt": 1}},
        ]
        players[0]["origin"] = SPAWNS[map_name].copy()
        players[1]["origin"] = GUEST_POSITIONS[map_name].copy()
        result.append({"phase": phase, "map": map_name, "generation": (1, 1, 2, 2, 3)[i],
                       "landmark": {"name": "hahaland", "origin": LANDMARK_ORIGINS[map_name].copy()},
                       "players": players,
                       "world": {"sentinel_name": "cof_probe_once", "marker_name": "cof_probe_marker",
                                 "sentinel_present": i == 0, "marker_health": 17 if i == 0 else 73}})
    return result


def status_capture():
    return """timestamp cofstory phase=status enabled=1 map=c_start generation=7 paused=0
cofstory phase=player slot=1 userid=101 ready=1 origin=2428,-883,-728 door=0 hidden=0 flags=0 health=83 fakeclient=0 frozen=0
cofstory phase=inventory slot=1 userid=101 class=weapon_glock
cofstory phase=inventory-slot slot=1 userid=101 index=0 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=1 descriptor="inventoryitems/weapons/weapon_mobile.txt"
cofstory phase=inventory-slot slot=1 userid=101 index=2 descriptor="inventoryitems/weapons/weapon_switchblade.txt"
cofstory phase=inventory-slot slot=1 userid=101 index=3 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=4 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=5 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=6 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=7 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=8 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=9 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=10 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=11 descriptor=""
cofstory phase=inventory-slot slot=1 userid=101 index=12 descriptor=""
cofstory phase=fixture name=cof_probe_once class=trigger_once health=0
cofstory phase=fixture name=cof_probe_marker class=info_target health=17
cofstory phase=landmark name=hahaland origin=2424,-908,-694
cofstory phase=status-end
"""


class StoryEvidenceTests(unittest.TestCase):
    def test_status_parser_uses_private_descriptors_and_requires_complete_capture(self):
        text = status_capture()
        parsed = parse_status(text, "initial")
        self.assertEqual(parsed["players"][0]["inventory"], {
            "inventoryitems/weapons/weapon_mobile.txt": 1,
            "inventoryitems/weapons/weapon_switchblade.txt": 1,
        })
        self.assertTrue(parsed["world"]["sentinel_present"])
        self.assertEqual(parse_status(text + "cofstory phase=unrelated\n", "initial"), parsed)
        with self.assertRaises(ValueError):
            parse_status(text.replace("cofstory phase=status-end", ""), "initial")
        with self.assertRaises(ValueError):
            parse_status("\n".join(line for line in text.splitlines() if "inventory-slot" not in line), "initial")
        with self.assertRaises(ValueError):
            parse_status("\n".join(line for line in text.splitlines() if "index=6" not in line), "initial")
        with self.assertRaises(ValueError):
            parse_status(text.replace("index=6", "index=5"), "initial")
        with self.assertRaises(ValueError):
            parse_status("\n".join(line for line in text.splitlines() if "index=12" not in line), "initial")
        with self.assertRaises(ValueError):
            parse_status(text.replace('index=7 descriptor=""', 'index=13 descriptor=""'), "initial")
        with self.assertRaises(ValueError):
            parse_status(text + "cofstory phase=status enabled=1 map=c_start generation=8\n", "initial")

    def test_seven_slot_capture_still_parses(self):
        text = "\n".join(line for line in status_capture().splitlines()
                         if not any(f"index={i} " in line for i in range(7, 13)))
        self.assertEqual(parse_status(text, "initial")["players"][0]["inventory"],
                         parse_status(status_capture(), "initial")["players"][0]["inventory"])

    def test_unused_slot_zero_cannot_supply_inventory(self):
        text = status_capture().replace('index=0 descriptor=""',
                                        'index=0 descriptor="inventoryitems/weapons/weapon_glock.txt"')
        with self.assertRaises(ValueError):
            parse_status(text, "initial")

    def test_status_parser_preserves_all_descriptors_and_counts(self):
        text = status_capture().replace('index=3 descriptor=""',
                                        'index=3 descriptor="inventoryitems/keys/hall_key.txt"')
        text = text.replace('index=4 descriptor=""',
                            'index=4 descriptor="inventoryitems/keys/hall_key.txt"')
        text = text.replace('index=6 descriptor=""',
                            'index=6 descriptor="inventoryitems/notes/hall_key.txt"')
        self.assertEqual(parse_status(text, "initial")["players"][0]["inventory"], {
            "inventoryitems/weapons/weapon_mobile.txt": 1,
            "inventoryitems/weapons/weapon_switchblade.txt": 1,
            "inventoryitems/keys/hall_key.txt": 2,
            "inventoryitems/notes/hall_key.txt": 1,
        })

    def test_old_class_records_and_ambiguous_fields_are_rejected(self):
        text = status_capture()
        marker = 'index=1 descriptor="inventoryitems/weapons/weapon_mobile.txt"'
        for replacement in (
            "index=1 class=weapon_mobile",
            'index=1 class=weapon_mobile descriptor="inventoryitems/weapons/weapon_mobile.txt"',
            marker + ' descriptor="inventoryitems/weapons/weapon_mobile.txt"',
            marker + " userid=101",
            marker + " stray-text",
            "index=1",
        ):
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                parse_status(text.replace(marker, replacement), "initial")

    def test_malformed_descriptors_cannot_be_normalized_into_proof(self):
        for descriptor in (
            "weapon_mobile", "weapon_mobile.txt", "elsewhere/weapon_mobile.txt",
            "inventoryitems/weapons/../weapon_mobile.txt",
            "inventoryitems//weapons/weapon_mobile.txt",
            "inventoryitems/weapons/weapon_mobile.txt.fake",
            "inventoryitems/weapons/weapon_mobile.txt/",
            r"inventoryitems\weapons\weapon_mobile.txt",
            r"inventoryitems/weapons/weapon_mob\ile.txt",
            "inventoryitems/weapons/weapon mobile.txt", "empty", "none", "-",
        ):
            with self.subTest(descriptor=descriptor), self.assertRaises(ValueError):
                parse_status(status_capture().replace("inventoryitems/weapons/weapon_mobile.txt", descriptor), "initial")

    def test_only_exact_canonical_starter_descriptors_count(self):
        for descriptor in (
            "inventoryitems/keys/weapon_mobile.txt",
            "inventoryitems/weapons/weapon_mobile_fake.txt",
            "inventoryitems/weapons/WEAPON_MOBILE.txt",
        ):
            data = observations()
            for snapshot in data:
                inventory = snapshot["players"][0]["inventory"]
                inventory[descriptor] = inventory.pop("inventoryitems/weapons/weapon_mobile.txt")
            with self.subTest(descriptor=descriptor):
                result = verify(data)
                self.assertFalse(result["passed"])
                self.assertTrue(any("lacks phone or knife" in error for error in result["errors"]))

    def test_legacy_classname_json_is_not_inventory_proof(self):
        data = observations()
        for snapshot in data:
            for player in snapshot["players"]:
                player["inventory"] = {name.rsplit("/", 1)[-1][:-4]: count
                                       for name, count in player["inventory"].items()}
        self.assertFalse(verify(data)["passed"])

    def test_nonweapon_descriptor_changes_are_not_hidden_by_basename(self):
        data = observations()
        for snapshot in data:
            snapshot["players"][0]["inventory"]["inventoryitems/keys/hall_key.txt"] = 2
        self.assertTrue(verify(data)["passed"])
        data[2]["players"][0]["inventory"].pop("inventoryitems/keys/hall_key.txt")
        data[2]["players"][0]["inventory"]["inventoryitems/notes/hall_key.txt"] = 2
        result = verify(data)
        self.assertFalse(result["passed"])
        self.assertIn("after_outbound: userid 101 inventory changed", result["errors"])

    def test_complete_observed_roundtrip_passes(self):
        self.assertTrue(verify(observations())["passed"])

    def test_map_load_logs_alone_cannot_pass(self):
        self.assertFalse(verify([{"phase": p, "map": m} for p, m in zip(PHASES, MAPS)])["passed"])

    def test_fresh_hub_spawn_cannot_pass(self):
        for field, value in (("sentinel_present", True), ("marker_health", 17)):
            data = observations()
            data[-1]["world"][field] = value
            with self.subTest(field=field):
                self.assertFalse(verify(data)["passed"])

    def test_lost_swapped_and_identical_inventories_fail(self):
        data = observations()
        data[2]["players"][1]["inventory"].pop("inventoryitems/weapons/weapon_mobile.txt")
        self.assertFalse(verify(data)["passed"])
        data = observations()
        data[2]["players"][0]["inventory"], data[2]["players"][1]["inventory"] = data[2]["players"][1]["inventory"], data[2]["players"][0]["inventory"]
        self.assertFalse(verify(data)["passed"])
        data = observations()
        for snapshot in data:
            snapshot["players"][1]["inventory"] = copy.deepcopy(snapshot["players"][0]["inventory"])
        self.assertFalse(verify(data)["passed"])

    def test_new_identity_bot_hidden_and_incomplete_evidence_fail(self):
        for field, value in (("userid", 303), ("fakeclient", True), ("ready", False), ("door_flag", 1), ("effects", 128), ("frozen", True)):
            data = observations()
            data[2]["players"][1][field] = value
            with self.subTest(field=field):
                self.assertFalse(verify(data)["passed"])
        data = observations()
        del data[4]["world"]
        self.assertFalse(verify(data)["passed"])

    def test_nonzero_landmark_delta_and_spawn_reset_detection(self):
        data = observations()
        self.assertEqual([a - b for a, b in zip(data[2]["landmark"]["origin"], data[1]["landmark"]["origin"])], [1789, 2070, 0])
        self.assertTrue(verify(data)["passed"])
        data[2]["players"][1]["origin"] = [639, -2953, -728]
        self.assertFalse(verify(data)["passed"])

    def test_old_shared_landmark_fixture_is_rejected(self):
        data = observations()
        for snapshot in data:
            snapshot["landmark"]["origin"] = [2424, -908, -694]
        self.assertFalse(verify(data)["passed"])

    def test_slot_swaps_preserve_user_identity_when_state_follows_user(self):
        data = observations()
        for index in (2, 3, 4):
            data[index]["players"][0]["slot"], data[index]["players"][1]["slot"] = 1, 0
        self.assertTrue(verify(data)["passed"])


@unittest.skipUnless((core.DEFAULT_GAME_DIR / "cryoffear/maps/c_start.bsp").exists(), "Local private BSPs are optional")
class StoryFixtureGenerationTests(unittest.TestCase):
    def test_original_worldspawn_and_bidirectional_synthetic_landmarks(self):
        sources = {name: (core.DEFAULT_GAME_DIR / "cryoffear/maps" / (name + ".bsp")).read_bytes() for name in SOURCES}
        files = build_fixture(sources)
        for name, data in sources.items():
            parsed = parse_entities(files[name + ".ent"].decode("latin-1"))
            source_entities = load_map_entities(core.DEFAULT_GAME_DIR / "cryoffear/maps" / (name + ".bsp"))
            self.assertEqual(parsed[0], source_entities[0])
            self.assertIn("wad", parsed[0])
            exit_entity = next(entity for entity in parsed if entity.get("targetname") == "cof_probe_exit")
            self.assertEqual(exit_entity["map"], "c_start2" if name == "c_start" else "c_start")
            self.assertEqual(exit_entity["landmark"], "hahaland")
            self.assertEqual(exit_entity["spawnflags"], "2")
            spawn = next(entity for entity in parsed if entity.get("classname") == "info_player_start")
            landmark = next(entity for entity in parsed if entity.get("classname") == "info_landmark")
            self.assertEqual([int(x) for x in spawn["origin"].split()], SPAWNS[name])
            self.assertEqual([int(x) for x in landmark["origin"].split()], LANDMARK_ORIGINS[name])
            for position in (SPAWNS[name], GUEST_POSITIONS[name]):
                geometry = inspect_spawn(data, position)
                self.assertEqual(geometry["standing_hull_contents"], -1)
                self.assertTrue({texture.upper() for texture in geometry["leaf_textures"]} - {"BLACK"})
            starts = [entity for entity in parsed if entity.get("classname") == "info_player_start"]
            self.assertEqual(len(starts), 2)
            self.assertEqual([int(x) for x in starts[1]["origin"].split()], GUEST_POSITIONS[name])
            self.assertEqual(hashlib.sha256(data).hexdigest(), SOURCES[name])
        manifest = json.loads(files["fixture.json"])
        self.assertEqual(manifest["map_sequence"], ["c_start", "c_start2", "c_start"])
        self.assertEqual(manifest["schema"], 2)
        self.assertNotEqual(manifest["landmark"]["origins"]["c_start"], manifest["landmark"]["origins"]["c_start2"])

    def test_original_bad_shared_spawn_is_empty_but_all_black_in_start(self):
        source = (core.DEFAULT_GAME_DIR / "cryoffear/maps/c_start.bsp").read_bytes()
        observed = inspect_spawn(source, [2428, -883, -728])
        self.assertEqual(observed["leaf"], 1)
        self.assertEqual(observed["standing_hull_contents"], -1)
        self.assertEqual([texture.upper() for texture in observed["leaf_textures"]], ["BLACK"])

    def test_modified_source_is_rejected(self):
        with self.assertRaises(ValueError):
            make_override(b"invalid", "c_start")


if __name__ == "__main__":
    unittest.main()
