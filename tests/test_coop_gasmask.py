"""The gas mask others see on a player whose night vision is on (1.1.9): models/coop/h_gasmask.mdl, made by
tools/prepare_coop_weapons.py from the stock models/items/w_nightvision.mdl and merged by the engine onto Bip01 Head.

GasMaskFitTests run anywhere: a synthetic item and the generator's own maths. RealMaskTests need the real models (a
folder like cryoffear holding models/items/w_nightvision.mdl, models/player/police1/police1.mdl and either
models/player/cof_s1/cof_s1.mdl or models/cutscene/player.mdl): COF_GASMASK_FILES names one, else the game in
core.DEFAULT_GAME_DIR is used; they skip without them. They check what a player sees: lenses on the eyes looking
forward, the filter in front below the mouth, the harness behind the head; the first 1.1.9 fit fails them."""
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from cofcoop import core
from tools import prepare_coop_weapons as weapons
from tools import prepare_simon_models as simon

JOINT1 = (0.0538, -0.0875, 0.0, 1.5708, 0.0, 0.0)  # the stock item's only bone (probe)
STOCK_BOX = ((-6.72, -3.33, -0.06), (7.72, 3.84, 7.41))  # the stock item's mesh (probe)
POLICE_CHAIN = {  # police1's values for the head chain (probe; cof_s1 has the same)
    "Bip01": (0.2338, 0.3682, 39.5261, 0.0, 0.0, -1.5708), "Bip01 Pelvis": (0.0, 0.0, 0.0, -1.5708, -1.5708, 0.0),
    "Bip01 Spine": (1.2937, 0.6845, 0.0, 0.0, 0.0, 0.1142), "Bip01 Spine1": (5.3097, -0.0038, 0.0, 0.0, 0.0, -0.0698),
    "Bip01 Spine2": (4.8268, -0.0046, 0.0, 0.0, 0.0, -0.0959), "Bip01 Spine3": (5.7928, -0.0053, 0.0, 0.0, 0.0, 0.0174),
    "Bip01 Neck": (6.6623, -0.0015, 0.0, 0.0, 0.0, 0.471), "Bip01 Head": (2.047, 0.0, 0.0, 0.0, 0.0, -0.1578)}
SCALE = (0.001,) * 6
# The first 1.1.9 fit, from boxes and a wrong guess at the item's axes (lenses up, filter inside the head): the real
# file checks must reject it.
FIRST_FIT = [[0.0, 0.0, 1.0, -0.534], [-1.0, 0.0, 0.0, 2.352], [0.0, -1.0, 0.0, 0.2755]]
FIRST_SCALE = 1.1


def close(a, b, places=3):
    return all(abs(x - y) < 10 ** -places for x, y in zip(a, b))


def mask_item():
    """A synthetic w_nightvision.mdl: a closed box filling the stock mask's box, on joint1 (vertices in its space)."""
    (x0, y0, z0), (x1, y1, z1) = STOCK_BOX
    corners = [(x, y, z) for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    faces = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4),
             (1, 5, 7), (1, 7, 3)]
    local = weapons.invert(weapons.bone_matrix(JOINT1))
    part = {"name": "body", "model": "doctor", "verts": [weapons.transform(local, v) for v in corners],
            "norms": [(0.0, 0.0, 1.0)],
            "meshes": [{"skinref": 0, "numnorms": 1, "numtris": len(faces),
                        "commands": [(3, [(i, 0, 0, 0) for i in face]) for face in faces]}]}
    texture = {"name": "head1_256.bmp", "flags": 0, "width": 512, "height": 512, "pixels": bytes(512 * 512),
               "palette": bytes(768)}
    return weapons.write_studio("w_nightvision.mdl", [("joint1", -1, JOINT1, SCALE)], 0, [part], [texture], [0])


def police_chain():
    """A stand-in police1 with the head chain's stock values (all build_gasmask reads from it)."""
    bones = [(name, index - 1, POLICE_CHAIN[name], SCALE) for index, name in enumerate(weapons.HEAD_CHAIN)]
    part = {"name": "body", "model": "ref", "verts": [(0, 0, 0), (1, 0, 0), (0, 1, 0)], "norms": [(0, 0, 1)],
            "meshes": [{"skinref": 0, "numnorms": 1, "numtris": 1, "commands": [(3, [(0, 0, 0, 0), (1, 0, 0, 0), (2, 0, 0, 0)])]}]}
    texture = {"name": "t.bmp", "flags": 0, "width": 8, "height": 8, "pixels": bytes(64), "palette": bytes(768)}
    return weapons.Studio(weapons.write_studio("police1.mdl", bones, 0, [part], [texture], [0]))


class FakePlayer:
    """Just what merge_bones reads of a player: its bone names (Simon's head chain)."""
    bones = [{"name": name} for name in weapons.HEAD_CHAIN]


def merged_item_bone(model: weapons.Studio, head_world):
    """The GASMASK bone of the generated model merged onto a player whose Bip01 Head is head_world (StudioMergeBones,
    as the client draws a follow entity)."""
    world = [weapons.IDENTITY] * (len(weapons.HEAD_CHAIN) - 1) + [head_world]
    return weapons.merge_bones(FakePlayer, world, model)[model.bone(weapons.HEAD_ITEM_BONE)]


class GasMaskFitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.item = mask_item()
        cls.data, cls.fit = weapons.build_gasmask(cls.item, police_chain())
        cls.model = weapons.Studio(cls.data)

    def test_the_placement_is_the_documented_derivation(self):
        forward, left, up = weapons.gasmask_axes()
        self.assertTrue(close(forward, (math.cos(math.radians(54)), 0.0, math.sin(math.radians(54))), 6))
        self.assertEqual(left, (0.0, 1.0, 0.0))
        self.assertTrue(close(up, (-math.sin(math.radians(54)), 0.0, math.cos(math.radians(54))), 6))  # to the forehead
        place = weapons.gasmask_placement()
        lenses = tuple(weapons.GASMASK_SCALE * c for c in weapons.GASMASK_LENSES)
        self.assertTrue(close(weapons.transform(place, lenses), (9.44 + 1.2, 0.085, 30.54), 6))
        self.assertTrue(close(weapons.rotate(place, forward), (1, 0, 0), 6))  # the lenses look ahead
        self.assertTrue(close(weapons.rotate(place, left), (0, 1, 0), 6))
        self.assertTrue(close(weapons.rotate(place, up), (0, 0, 1), 6))
        self.assertEqual(weapons.GASMASK_SCALE, 1.0)
        rigid = weapons.rigid(weapons.HEAD_WORLD)
        for row, want in zip(rigid, weapons.HEAD_WORLD):
            self.assertTrue(close(row, want, 3))
        # the head bone is tilted in the idle pose: up the head is mostly +Z, its Y mostly forward
        self.assertGreater(weapons.HEAD_WORLD[2][0], 0.9)
        self.assertGreater(weapons.HEAD_WORLD[0][1], 0.9)

    def test_the_model_merges_on_the_head(self):
        self.assertEqual(simon.validate_model(self.data)["sequences"], 1)
        names = [bone["name"] for bone in self.model.bones]
        self.assertEqual(names, list(weapons.HEAD_CHAIN) + [weapons.HEAD_ITEM_BONE])
        self.assertEqual([bone["parent"] for bone in self.model.bones], list(range(-1, len(names) - 1)))
        self.assertNotIn(weapons.HEAD_ITEM_BONE, weapons.ARM_CHAIN)
        for name in weapons.HEAD_CHAIN:  # the chain keeps police1's (and cof_s1's) defaults
            self.assertTrue(close(self.model.bones[self.model.bone(name)]["value"], POLICE_CHAIN[name], 4))
        for row, want in zip(weapons.bone_matrix(self.model.bones[-1]["value"]), weapons.gasmask_fit()):
            self.assertTrue(close(row, want, 5))
        count, offset = struct.unpack_from("<ii", self.data, 204)
        for part in range(count):
            _n, _b, record = struct.unpack_from("<iii", self.data, offset + 76 * part + 64)
            numverts, vertinfo = struct.unpack_from("<ii", self.data, record + 80)
            self.assertEqual(set(self.data[vertinfo:vertinfo + numverts]), {len(names) - 1})
        textures = weapons.read_textures(self.data)
        self.assertEqual([(t["name"], t["width"], t["height"]) for t in textures], [("head1_256.bmp", 256, 256)])
        self.assertLess(len(self.data), 600_000)

    def test_merged_on_simons_idle_head_the_lenses_sit_before_his_eyes(self):
        # the client's merge hands over the head's matrix: the lens midpoint lands 1.2 before the eyes, looking +X
        item = merged_item_bone(self.model, weapons.rigid(weapons.HEAD_WORLD))
        lenses = tuple(weapons.GASMASK_SCALE * c for c in weapons.GASMASK_LENSES)
        self.assertTrue(close(weapons.transform(item, lenses), (10.64, 0.085, 30.54), 3))
        forward, left, up = weapons.gasmask_axes()
        self.assertTrue(close(weapons.rotate(item, forward), (1, 0, 0), 3))
        self.assertTrue(close(weapons.rotate(item, up), (0, 0, 1), 3))
        self.assertEqual(self.fit["lenses_in_idle"], [10.64, 0.085, 30.54])


class OptionalSourceTests(unittest.TestCase):
    def test_the_mask_source_is_optional_and_pinned(self):
        self.assertNotIn(weapons.GASMASK_SOURCE, weapons.SOURCES)  # setup never refuses a game over it
        self.assertEqual(weapons.OPTIONAL_SOURCES[weapons.GASMASK_SOURCE],
                         "d18f78bc1e56c03c9b899220b9a154157b78de462af76a70084c58850fa318eb")
        paths = weapons.output_paths()
        self.assertIn("models/coop/h_gasmask.mdl", paths)
        self.assertEqual(len(paths), len(set(paths)))
        self.assertTrue(all(len(path) < 64 and path.startswith("models/coop/") for path in paths))
        self.assertEqual(weapons.engine_mapping()["gasmask"]["model"], "models/coop/h_gasmask.mdl")
        self.assertEqual(weapons.engine_mapping()["gasmask"]["flash_flag"], 64)

    def test_read_optional_takes_what_is_there(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertEqual(weapons.read_optional(root), {})
            path = root / weapons.GASMASK_SOURCE
            path.parent.mkdir(parents=True)
            path.write_bytes(b"mask")
            self.assertEqual(weapons.read_optional(root), {weapons.GASMASK_SOURCE: b"mask"})

    def test_a_missing_or_modded_mask_source_leaves_only_the_mask_out(self):
        stock = b"stock mask"
        with mock.patch.dict(weapons.OPTIONAL_SOURCES, {weapons.GASMASK_SOURCE: weapons.sha(stock)}):
            self.assertEqual(weapons.optional_source({weapons.GASMASK_SOURCE: stock}, weapons.GASMASK_SOURCE), (stock, ""))
            self.assertEqual(weapons.optional_source({weapons.GASMASK_SOURCE: b"modded"}, weapons.GASMASK_SOURCE),
                             (None, "models/items/w_nightvision.mdl is not the stock file"))
            self.assertEqual(weapons.optional_source({}, weapons.GASMASK_SOURCE),
                             (None, "models/items/w_nightvision.mdl is missing"))
        # build_models makes the rest as usual and records why the mask was left out (setup logs it)
        source = Path(weapons.__file__).read_text()
        body = source[source.index("def build_models("):source.index("# Stock right-hand p_ models")]
        self.assertIn("mask, why = optional_source(files, GASMASK_SOURCE)", body)
        self.assertIn('skipped.append({"path": f"{OUTPUT_DIR}/{GASMASK}.mdl", "reason": why})', body)
        self.assertIn('"skipped": skipped,', body)
        self.assertLess(body.index("for name, spec in DUAL_ITEMS.items():"), body.index("optional_source("))


# ---------------------------------------------------------------------------------------------
# The real models
# ---------------------------------------------------------------------------------------------

def real_root():
    named = os.environ.get("COF_GASMASK_FILES")
    for root in ([Path(named)] if named else []) + [core.DEFAULT_GAME_DIR / "cryoffear"]:
        mask = root / weapons.GASMASK_SOURCE
        if (mask.is_file() and (root / "models/player/police1/police1.mdl").is_file() and
                ((root / "models/player/cof_s1/cof_s1.mdl").is_file() or (root / "models/cutscene/player.mdl").is_file())):
            return root
    return None


def textured_triangles(data, world):
    """(three posed points, texel rgb 0-1, three (s, t), texture name) of every triangle of a studio model."""
    textures = weapons.read_textures(data)
    skin = weapons.read_skins(data)[0]
    count, offset = struct.unpack_from("<ii", data, 204)
    out = []
    for part in range(count):
        _n, _b, record = struct.unpack_from("<iii", data, offset + 76 * part + 64)
        nummesh, meshindex, numverts, vinfo, vindex = struct.unpack_from("<5i", data, record + 72)
        verts = [weapons.transform(world[data[vinfo + i]], struct.unpack_from("<3f", data, vindex + 12 * i)) for i in range(numverts)]
        for mesh in range(nummesh):
            _tris, triindex, skinref = struct.unpack_from("<3i", data, meshindex + 20 * mesh)
            texture = textures[skin[skinref]]
            for count2, points in weapons._tricmds(data, triindex):
                for i in range(len(points) - 2):
                    corners = (points[0], points[i + 1], points[i + 2]) if count2 < 0 else points[i:i + 3]
                    s = int(sum(c[2] for c in corners) / 3) % texture["width"]
                    t = int(sum(c[3] for c in corners) / 3) % texture["height"]
                    index = texture["pixels"][t * texture["width"] + s]
                    rgb = tuple(c / 255 for c in texture["palette"][index * 3:index * 3 + 3])
                    out.append(([verts[c[0]] for c in corners], rgb, [(c[2], c[3]) for c in corners], texture["name"]))
    return out


def olive(rgb):
    """The lenses' and the filter's olive green (the rest of the mask is near black)."""
    return rgb[0] > 0.25 and rgb[1] > 0.25 and rgb[2] < rgb[0] * 0.95


def centre(points):
    return tuple(sum(p[k] for p in points) / len(points) for k in range(3))


def outward(triangles, inside):
    """Area-weighted normal of triangles, each turned away from the point `inside`; and their area."""
    total, area = [0.0, 0.0, 0.0], 0.0
    for a, b, c in triangles:
        n = weapons.cross(tuple(q - p for p, q in zip(a, b)), tuple(q - p for p, q in zip(a, c)))
        if weapons.dot(n, tuple(m - i for m, i in zip(centre((a, b, c)), inside))) < 0:
            n = tuple(-x for x in n)
        total = [t + x / 2 for t, x in zip(total, n)]
        area += math.sqrt(weapons.dot(n, n)) / 2
    return weapons.normalize(total), area


def ray_hit(origin, direction, triangles):
    """Distance along direction to the nearest triangle (Moller-Trumbore), or None."""
    best = None
    for a, b, c in triangles:
        e1, e2 = tuple(q - p for p, q in zip(a, b)), tuple(q - p for p, q in zip(a, c))
        h = weapons.cross(direction, e2)
        det = weapons.dot(e1, h)
        if abs(det) < 1e-9:
            continue
        s = tuple(o - p for o, p in zip(origin, a))
        u = weapons.dot(s, h) / det
        q = weapons.cross(s, e1)
        v = weapons.dot(direction, q) / det
        t = weapons.dot(e2, q) / det
        if u >= 0 and v >= 0 and u + v <= 1 and t > 1e-4 and (best is None or t < best):
            best = t
    return best


def simon_landmarks(data, world):
    """Simon's eyes, nose tip, mouth and brow on the posed face: points of head_face.bmp (texels) found on the mesh."""
    face = [(points, uv) for points, _rgb, uv, name in textured_triangles(data, world) if name == "head_face.bmp"]

    def at(u, v):
        best = None
        for points, ((x0, y0), (x1, y1), (x2, y2)) in face:
            den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
            if not den:
                continue
            a = ((y1 - y2) * (u - x2) + (x2 - x1) * (v - y2)) / den
            b = ((y2 - y0) * (u - x2) + (x0 - x2) * (v - y2)) / den
            c = 1 - a - b
            if min(a, b, c) >= -1e-6:
                p = tuple(a * points[0][k] + b * points[1][k] + c * points[2][k] for k in range(3))
                if best is None or p[0] > best[0]:  # the face, not a mirrored ear
                    best = p
        return best

    return {name: at(u, v) for name, (u, v) in {"eye_r": (222, 205), "eye_l": (293, 205), "nose": (258, 268),
                                                 "mouth": (258, 302), "brow": (258, 170)}.items()}


def wear_problems(mask_data, simon_model, world, marks, scale=None):
    """What is wrong with the mask as others see it on Simon's idle pose (an empty list: nothing). scale: the one
    the model was made with (its mesh is the item's, scaled, on the GASMASK bone)."""
    scale = weapons.GASMASK_SCALE if scale is None else scale
    model = weapons.Studio(mask_data)
    merged = weapons.merge_bones(simon_model, world, model)
    posed = textured_triangles(mask_data, merged)
    stock = textured_triangles(mask_data, [weapons.IDENTITY] * len(model.bones))  # the item's space, scaled
    problems = []
    # the item's lenses are its olive parts on top of the face piece, its filter the olive part past the hose
    lens = [p for (p, rgb, _uv, _n), (q, _r, _u, _m) in zip(posed, stock)
            if olive(rgb) and centre(q)[0] < 0.5 * scale and centre(q)[2] > 4.5 * scale]
    filt = [p for (p, rgb, _uv, _n), (q, _r, _u, _m) in zip(posed, stock) if olive(rgb) and centre(q)[0] > 3.8 * scale]
    eyes = [marks["eye_l"], marks["eye_r"]]
    middle = centre([v for p, *_ in posed for v in p])
    for eye in eyes:
        near = [p for p in lens if abs(centre(p)[1] - eye[1]) < 1.6]
        if not near:
            problems.append(f"no lens beside the eye at {eye}")
            continue
        c = centre([v for p in near for v in p])
        if abs(c[2] - eye[2]) > 0.5 or not 0.5 < c[0] - eye[0] < 2.5:
            problems.append(f"the lens at {tuple(round(x, 2) for x in c)} is not just before the eye {eye}")
    if lens:
        look, _area = outward(lens, middle)
        if weapons.angle_between(look, (1, 0, 0)) > 12:
            problems.append(f"the lenses look along {tuple(round(x, 2) for x in look)}, not forward")
    if not filt:
        problems.append("no filter")
    else:
        c = centre([v for p in filt for v in p])
        if c[0] < marks["nose"][0] + 1.0 or c[2] > marks["mouth"][2]:
            problems.append(f"the filter at {tuple(round(x, 2) for x in c)} is not in front below the mouth")
    harness = [v for p, *_ in posed for v in p]
    if min(v[0] for v in harness) > min(e[0] for e in eyes) - 3.0:
        problems.append("nothing goes back round the head")
    for name in ("eye_l", "eye_r", "nose", "mouth", "brow"):
        hit = ray_hit(marks[name], (1.0, 0.0, 0.0), [p for p, *_ in posed])
        if hit is None or hit > 4.0:
            problems.append(f"the {name} is not covered ({hit})")
    return problems


ROOT = real_root()


@unittest.skipUnless(ROOT, "the real models (COF_GASMASK_FILES or the game) are optional")
class RealMaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.item = (ROOT / weapons.GASMASK_SOURCE).read_bytes()
        police = (ROOT / "models/player/police1/police1.mdl").read_bytes()
        cofs1 = ROOT / "models/player/cof_s1/cof_s1.mdl"
        player = cofs1.read_bytes() if cofs1.is_file() else \
            simon.adapt_sequences((ROOT / "models/cutscene/player.mdl").read_bytes(), police)[0]
        cls.simon = weapons.Studio(player)
        cls.world = weapons.player_pose(cls.simon, cls.simon.sequence("idle"))
        cls.marks = simon_landmarks(player, cls.world)
        cls.police = weapons.Studio(police)

    def test_the_files_are_the_ones_the_fit_was_made_from(self):
        self.assertEqual(weapons.sha(self.item), weapons.OPTIONAL_SOURCES[weapons.GASMASK_SOURCE])
        head = self.world[self.simon.bone("Bip01 Head")]
        for row, want in zip(head, weapons.HEAD_WORLD):
            self.assertTrue(close(row, want, 3))
        eyes = centre([self.marks["eye_l"], self.marks["eye_r"]])
        self.assertTrue(close(eyes, weapons.SIMON_EYES, 1), eyes)
        # the item's lenses: where the constants say, looking where they say
        model = weapons.Studio(self.item)
        stock = textured_triangles(self.item, weapons.world_bones(model, weapons.local_pose(model, 0)))
        lens = [p for p, rgb, _uv, _n in stock if olive(rgb) and centre(p)[0] < 0.5 and centre(p)[2] > 4.5]
        self.assertTrue(close(centre([v for p in lens for v in p]), weapons.GASMASK_LENSES, 1))
        look, _ = outward(lens, centre([v for p, *_ in stock for v in p]))
        self.assertLess(weapons.angle_between(look, weapons.gasmask_axes()[0]), 2.0)

    def test_others_see_the_mask_on_simons_face(self):
        data, _fit = weapons.build_gasmask(self.item, self.police)
        self.assertEqual(wear_problems(data, self.simon, self.world, self.marks), [])

    def test_the_first_fit_is_rejected(self):
        with mock.patch.object(weapons, "gasmask_fit", return_value=FIRST_FIT), \
             mock.patch.object(weapons, "GASMASK_SCALE", FIRST_SCALE):
            data, _fit = weapons.build_gasmask(self.item, self.police)
        problems = wear_problems(data, self.simon, self.world, self.marks, FIRST_SCALE)
        self.assertTrue(any("look along" in p for p in problems), problems)
        self.assertTrue(any("filter" in p for p in problems), problems)


if __name__ == "__main__":
    unittest.main()
