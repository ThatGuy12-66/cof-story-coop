"""Build third-person weapon models (p_/l_ models) for story co-op from the user's own stock files.

Retail Cry of Fear only made p_ models for the police campaign. Story-only items deploy
models/null.mdl, and the police long guns are held badly by the generated Simon player models
(cof_s1/cof_s2, see prepare_simon_models.py). This generator writes, under models/coop/:

* p_<item>.mdl  story items on Bip01 R Hand (axe, famas, lantern, flare, mobile, camera,
  booklaser, browning), built from the item's w_ model mesh and textures;
* p_<gun>.mdl   grip-corrected copies of police long guns for Simon (only the item bone changes);
* l_<item>.mdl  left-hand variants on Bip01 L Hand for the dual-wield "other hand";
* r_<item>.mdl  right-hand lights re-aimed for the dual-wield body pose.

Every model uses the stock bone names, so the retail client bone-merges it onto the player
(HLSDK StudioMergeBones). Grips are solved with the HLSDK studio maths re-implemented here: the
item is placed so that, in Simon's pose for the sequence the game (or the suggested engine
override) plays, its axes point along the wanted world directions and its grip point sits in
the palm. Default is inspection; --write creates a fresh evidence directory only. Generated
models are stock-derived private assets and stay local, like the Simon models.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import core
from tools import prepare_simon_models as simon

OUTPUT_DIR = "models/coop"
MAX_TEXTURE = 256  # third-person textures are halved until they fit (police p_ models use 128-384)

# Every file the generator reads, relative to the game's cryoffear folder, pinned to the stock bytes.
SOURCES = {
    "models/cutscene/player.mdl": simon.SIMON_SHA,
    "models/player/police1/police1.mdl": simon.POLICE_SHA,
    "police/models/weapons/glock/p_glock.mdl": "ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49",
    "police/models/weapons/p345/p_p345.mdl": "c090798e8754084506c6859c268c4db08961c3b0d609afba6a14e50457626bb0",
    "police/models/weapons/vp70/p_vp70.mdl": "a4412beb27cbd51478651b87290bb1bff66133478229c56c5879136c5cab93e8",
    "police/models/weapons/revolver/p_revolver.mdl": "2a54b7b911ed2e5145947eed39779db5ec83b6707701eeaf9826cb73f8f69312",
    "police/models/weapons/switchblade/p_switchblade.mdl": "b0d352084ef7c81ac47709440d3d32e38b790ee91da5756d0c3b57c320de476a",
    "police/models/weapons/nightstick/p_nightstick.mdl": "1376ed2f949281199100d92d0c36153d30314bab13cbec7960f41b421ae7f3e1",
    "police/models/weapons/branch/p_branch.mdl": "396abd8db19c905aec92a8a8c1941b58d96b67d2d76ff3b539517f33a7f19f69",
    "police/models/weapons/rifle/p_rifle.mdl": "d4beeebb7d8548171165776c359e1fab21f874b1b96d9521203332a216eb9386",
    "police/models/weapons/shotgun/p_shotgun.mdl": "60911831719dc57cca1bcb34801c8b74e24443372afe13d5230e336316a5a5bc",
    "police/models/weapons/tmp/p_tmp.mdl": "ad599b80f9e9d4e37cbb0f981d218a7741f414bfa7d3ddfd4be86368273da87f",
    "models/weapons/flashlight/p_flashlight.mdl": "2d379075479fe5b3320db46bda75578b56d082c932cd9a9bb9990ae6b3846d66",
    "models/weapons/axe/w_axe.mdl": "ae3601eb25cbf200dc23179b327588fc897e55370372242b308311fd423e7be3",
    "models/weapons/famas/w_famas.mdl": "dd9ce3c9127d9d3aedb8c282c1154cf92a3eba28f66728fa8694250e6edcdfeb",
    "models/weapons/lantern/w_lantern.mdl": "60096eb47adcfa57c6da9792457cd45bab9e8b48d9a418e5ddc5d59eca24b572",
    "models/weapons/flare/w_flare.mdl": "9a897d6587a19a7405202dfc09c616cdeb9efab31520ec52591e1dde62747639",
    "models/weapons/mobile/w_mobile.mdl": "5e3f2093af68a3e25fbb404066f71599092db918f8c4fc76788139906de30005",
    "models/weapons/camera/w_camera.mdl": "01e54840151049260b605d26350e64fb257173ee345530914a78c27d9a1b7d99",
    "models/weapons/booklaser/w_booklaser.mdl": "a9f9ebe24e0c30f3df7bee10ffb90676b95d114203fa3fcafe36d718d02b2284",
    "models/weapons/browning/w_browning.mdl": "002540d8473bb6d7d0838f65a0795ef2fb34c4e928e0548c448b96ad0ee0037b",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


# ---------------------------------------------------------------------------------------------
# Studio model reading (GoldSrc IDST version 10)
# ---------------------------------------------------------------------------------------------

class Studio:
    """Read-only view of the parts of a studio model the generator needs."""

    def __init__(self, data: bytes):
        if len(data) < 244 or data[:4] != b"IDST" or struct.unpack_from("<i", data, 4)[0] != 10:
            raise ValueError("Expected GoldSrc IDST version 10 model")
        self.data = data
        count, offset = struct.unpack_from("<ii", data, 140)
        simon._range(data, offset, count * 112, "bones")
        self.bones = []
        for index in range(count):
            start = offset + index * 112
            self.bones.append({
                "name": _cstr(data[start:start + 32]),
                "parent": struct.unpack_from("<i", data, start + 32)[0],
                "value": struct.unpack_from("<6f", data, start + 64),
                "scale": struct.unpack_from("<6f", data, start + 88),
                "offset": start,
            })
        count, offset = struct.unpack_from("<ii", data, 164)
        simon._range(data, offset, count * 176, "sequence table")
        self.sequences = []
        for index in range(count):
            start = offset + index * 176
            self.sequences.append({
                "label": _cstr(data[start:start + 32]),
                "numframes": struct.unpack_from("<i", data, start + 56)[0],
                "motion": struct.unpack_from("<ii", data, start + 68),
                "numblends": struct.unpack_from("<i", data, start + 120)[0],
                "animindex": struct.unpack_from("<i", data, start + 124)[0],
                "blendstart": struct.unpack_from("<f", data, start + 136)[0],
                "blendend": struct.unpack_from("<f", data, start + 144)[0],
                "seqgroup": struct.unpack_from("<i", data, start + 156)[0],
            })

    def bone(self, name):
        for index, bone in enumerate(self.bones):
            if bone["name"].lower() == name.lower():
                return index
        raise KeyError(f"No bone {name}")

    def sequence(self, name):
        for index, sequence in enumerate(self.sequences):
            if sequence["label"] == name:
                return index
        raise KeyError(f"No sequence {name}")


def _anim_value(data, cursor, frame):
    """HLSDK StudioCalcBone* run decoding: the value of a compressed channel at an integer frame."""
    k = frame
    while True:
        simon._range(data, cursor, 2, "animation run")
        valid, total = data[cursor], data[cursor + 1]
        if not total:
            raise ValueError("Invalid compressed animation run")
        if total > k:
            break
        k -= total
        cursor += 2 * (valid + 1)
    index = k + 1 if valid > k else valid
    simon._range(data, cursor + 2 * index, 2, "animation value")
    return struct.unpack_from("<h", data, cursor + 2 * index)[0]


# ---------------------------------------------------------------------------------------------
# HLSDK studio maths (mathlib.c / StudioModelRenderer.cpp), 3x4 row matrices
# ---------------------------------------------------------------------------------------------

def angle_quaternion(angles):
    """AngleQuaternion: (roll x, pitch y, yaw z) in radians -> (x, y, z, w)."""
    sr, cr = math.sin(angles[0] * 0.5), math.cos(angles[0] * 0.5)
    sp, cp = math.sin(angles[1] * 0.5), math.cos(angles[1] * 0.5)
    sy, cy = math.sin(angles[2] * 0.5), math.cos(angles[2] * 0.5)
    return (sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy)


def quaternion_slerp(p, q, t):
    """QuaternionSlerp as in HLSDK (aligns q to p first)."""
    a = sum((p[i] - q[i]) ** 2 for i in range(4))
    b = sum((p[i] + q[i]) ** 2 for i in range(4))
    if a > b:
        q = tuple(-value for value in q)
    cosom = sum(p[i] * q[i] for i in range(4))
    if 1.0 + cosom > 1e-6:
        if 1.0 - cosom > 1e-6:
            omega = math.acos(cosom)
            sinom = math.sin(omega)
            sclp, sclq = math.sin((1.0 - t) * omega) / sinom, math.sin(t * omega) / sinom
        else:
            sclp, sclq = 1.0 - t, t
        return tuple(sclp * p[i] + sclq * q[i] for i in range(4))
    turned = (-q[1], q[0], -q[3], q[2])
    sclp, sclq = math.sin((1.0 - t) * 0.5 * math.pi), math.sin(t * 0.5 * math.pi)
    return tuple(sclp * p[i] + sclq * turned[i] for i in range(3)) + (turned[3],)


def quaternion_matrix(q, position=(0.0, 0.0, 0.0)):
    x, y, z, w = q
    return [[1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * w * z, 2 * x * z + 2 * w * y, position[0]],
            [2 * x * y + 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * w * x, position[1]],
            [2 * x * z - 2 * w * y, 2 * y * z + 2 * w * x, 1 - 2 * x * x - 2 * y * y, position[2]]]


def concat(a, b):
    """ConcatTransforms(a, b): a * b for 3x4 matrices."""
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0) for j in range(4)]
            for i in range(3)]


def invert(m):
    """Inverse of a rigid 3x4 transform."""
    r = [[m[j][i] for j in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[i] + [t[i]] for i in range(3)]


def transform(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) + m[i][3] for i in range(3))


def rotate(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


IDENTITY = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def matrix_angles(m):
    """Inverse of AngleQuaternion+QuaternionMatrix: rotation part -> (roll, pitch, yaw) radians.
    The rotation is Rz(yaw) * Ry(pitch) * Rx(roll)."""
    pitch = math.asin(max(-1.0, min(1.0, -m[2][0])))
    if abs(m[2][0]) < 0.999999:
        roll = math.atan2(m[2][1], m[2][2])
        yaw = math.atan2(m[1][0], m[0][0])
    else:  # gimbal lock: fold roll into yaw
        roll = 0.0
        yaw = math.atan2(-m[0][1], m[1][1])
    return roll, pitch, yaw


def bone_matrix(value6):
    """A bone's local transform from (x, y, z, roll, pitch, yaw)."""
    return quaternion_matrix(angle_quaternion(value6[3:6]), value6[:3])


def normalize(v):
    length = math.sqrt(sum(c * c for c in v))
    if length < 1e-9:
        raise ValueError("Zero-length axis")
    return tuple(c / length for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def frame_from_axes(forward, up, origin=(0.0, 0.0, 0.0)):
    """Rigid frame whose X axis is `forward` and whose Z axis is `up` made orthogonal to it."""
    x = normalize(forward)
    y = normalize(cross(up, x))
    z = cross(x, y)
    return [[x[i], y[i], z[i], origin[i]] for i in range(3)]


def local_pose(model: Studio, sequence, frame=0, pitch=0.0):
    """StudioCalcRotations at an integer frame, blended like StudioPlayerBlend for `pitch`
    (Cry of Fear's client zeroes the entity pitch before blending, so 0 is what others see).
    Returns one (position, quaternion) per bone."""
    seq = model.sequences[sequence]
    if seq["seqgroup"] != 0:
        raise ValueError("Only in-file sequence groups are supported")
    frame = min(frame, seq["numframes"] - 1)
    poses = []
    for blend in range(min(seq["numblends"], 2)):
        anim = seq["animindex"] + blend * len(model.bones) * 12
        simon._range(model.data, anim, len(model.bones) * 12, "animation records")
        pose = []
        for index, bone in enumerate(model.bones):
            start = anim + index * 12
            offsets = struct.unpack_from("<6H", model.data, start)
            value = list(bone["value"])
            for channel in range(6):
                if offsets[channel]:
                    value[channel] += _anim_value(model.data, start + offsets[channel], frame) * bone["scale"][channel]
            motiontype, motionbone = seq["motion"]
            if index == motionbone:
                for channel, flag in enumerate((1, 2, 4)):  # STUDIO_X/Y/Z zero the root motion
                    if motiontype & flag:
                        value[channel] = 0.0
            pose.append((tuple(value[:3]), angle_quaternion(value[3:6])))
        poses.append(pose)
    if len(poses) == 1:
        return poses[0]
    start, end = seq["blendstart"], seq["blendend"]
    blend = pitch * 3
    if blend < start:
        weight = 0
    elif blend > end:
        weight = 255
    else:
        weight = 127 if end - start < 0.1 else int(255 * (blend - start) / (end - start))
    t = weight / 255.0
    return [(tuple(a[0][i] * (1 - t) + b[0][i] * t for i in range(3)), quaternion_slerp(a[1], b[1], t))
            for a, b in zip(*poses)]


def world_bones(model: Studio, local, root=IDENTITY):
    world = []
    for index, bone in enumerate(model.bones):
        matrix = quaternion_matrix(local[index][1], local[index][0])
        parent = bone["parent"]
        world.append(concat(world[parent] if parent >= 0 else root, matrix))
    return world


def player_pose(model: Studio, sequence, gait="idle", frame=0, pitch=0.0):
    """StudioSetupBones for a player: bones before "Bip01 Spine" come from the gait sequence."""
    local = local_pose(model, sequence, frame, pitch)
    if gait is not None:
        legs = local_pose(model, model.sequence(gait) if isinstance(gait, str) else gait, frame, pitch)
        for index, bone in enumerate(model.bones):
            if bone["name"] == "Bip01 Spine":
                break
            local[index] = legs[index]
    return world_bones(model, local)


def merge_bones(player: Studio, player_world, weapon: Studio):
    """StudioMergeBones: bones named like a player bone copy its transform, the rest use the
    weapon's own sequence 0 frame 0 under their parent."""
    names = {bone["name"].lower(): index for index, bone in enumerate(player.bones)}
    local = local_pose(weapon, 0)
    world = []
    for index, bone in enumerate(weapon.bones):
        match = names.get(bone["name"].lower())
        if match is not None:
            world.append(player_world[match])
        else:
            matrix = quaternion_matrix(local[index][1], local[index][0])
            parent = bone["parent"]
            world.append(concat(world[parent] if parent >= 0 else IDENTITY, matrix))
    return world


# ---------------------------------------------------------------------------------------------
# Meshes and textures
# ---------------------------------------------------------------------------------------------

def read_textures(data):
    count, offset = struct.unpack_from("<ii", data, 180)
    textures = []
    for index, (name, info) in enumerate(simon.texture_info(data).items()):
        start = info["offset"]
        size = info["width"] * info["height"]
        textures.append({"name": _cstr(data[offset + 80 * index:offset + 80 * index + 64]),
                         "flags": info["flags"], "width": info["width"], "height": info["height"],
                         "pixels": data[start:start + size], "palette": data[start + size:start + size + 768]})
    if len(textures) != count:
        raise ValueError("Duplicate texture names")
    return textures


def read_skins(data):
    refs, families, offset = struct.unpack_from("<iii", data, 192)
    simon._range(data, offset, refs * families * 2, "skin families")
    return [list(struct.unpack_from("<" + "h" * refs, data, offset + family * refs * 2)) for family in range(families)]


def _tricmds(data, offset):
    commands = []
    while True:
        simon._range(data, offset, 2, "triangle commands")
        count = struct.unpack_from("<h", data, offset)[0]
        offset += 2
        if not count:
            return commands
        simon._range(data, offset, abs(count) * 8, "triangle commands")
        commands.append((count, [struct.unpack_from("<4h", data, offset + 8 * i) for i in range(abs(count))]))
        offset += abs(count) * 8


def read_mesh(model: Studio, bodyparts=None, submodel=0, root_bone=None):
    """The chosen submodel of each bodypart, with vertices and normals in model space at
    sequence 0 frame 0 (the pose the stock model is seen in), or relative to `root_bone`
    (for a p_ model: the item as the hand holds it, in that hand bone's local space)."""
    data = model.data
    world = world_bones(model, local_pose(model, 0))
    if root_bone is not None:
        base = invert(world[model.bone(root_bone)])
        world = [concat(base, matrix) for matrix in world]
    count, offset = struct.unpack_from("<ii", data, 204)
    parts = []
    for part in range(count):
        if bodyparts is not None and part not in bodyparts:
            continue
        start = offset + part * 76
        nummodels, _base, modelindex = struct.unpack_from("<iii", data, start + 64)
        if submodel >= nummodels:
            raise ValueError("Missing submodel")
        m = modelindex + submodel * 112
        nummesh, meshindex, numverts, vinfo, vindex, numnorms, ninfo, nindex = struct.unpack_from("<8i", data, m + 72)
        simon._range(data, vindex, numverts * 12, "vertices")
        simon._range(data, nindex, numnorms * 12, "normals")
        simon._range(data, meshindex, nummesh * 20, "meshes")
        verts = [transform(world[data[vinfo + i]], struct.unpack_from("<3f", data, vindex + 12 * i)) for i in range(numverts)]
        norms = [rotate(world[data[ninfo + i]], struct.unpack_from("<3f", data, nindex + 12 * i)) for i in range(numnorms)]
        meshes = []
        for mesh in range(nummesh):
            numtris, triindex, skinref, mnorms, _ = struct.unpack_from("<5i", data, meshindex + 20 * mesh)
            meshes.append({"skinref": skinref, "numnorms": mnorms, "numtris": numtris, "commands": _tricmds(data, triindex)})
        for mesh in meshes:
            for _count, points in mesh["commands"]:
                for vertex, normal, _s, _t in points:
                    if not (0 <= vertex < numverts and 0 <= normal < numnorms):
                        raise ValueError("Triangle command index out of range")
        parts.append({"name": _cstr(data[start:start + 64]), "model": _cstr(data[m:m + 64]),
                      "verts": verts, "norms": norms, "meshes": meshes})
    return parts


def transform_mesh(parts, matrix):
    return [{**part, "verts": [transform(matrix, v) for v in part["verts"]],
             "norms": [rotate(matrix, n) for n in part["norms"]]} for part in parts]


def shrink_texture(texture, limit=MAX_TEXTURE):
    """Halve a texture until it fits `limit`, keeping the most common index of each 2x2 block."""
    width, height, pixels = texture["width"], texture["height"], texture["pixels"]
    while (width > limit or height > limit) and width % 2 == 0 and height % 2 == 0:
        half = bytearray()
        for y in range(0, height, 2):
            row, below = pixels[y * width:(y + 1) * width], pixels[(y + 1) * width:(y + 2) * width]
            for x in range(0, width, 2):
                block = (row[x], row[x + 1], below[x], below[x + 1])
                half.append(Counter(block).most_common(1)[0][0] if len(set(block)) > 1 else block[0])
        width, height, pixels = width // 2, height // 2, bytes(half)
    return {**texture, "width": width, "height": height, "pixels": bytes(pixels)}


# ---------------------------------------------------------------------------------------------
# Writing a merge model (one in-file sequence, no animation: every bone sits at its default)
# ---------------------------------------------------------------------------------------------

def _name(text, size):
    encoded = text.encode("ascii")
    if len(encoded) >= size:
        raise ValueError(f"Name too long: {text}")
    return encoded + b"\0" * (size - len(encoded))


def write_studio(name, bones, item_bone, parts, textures, skin, attachments=()):
    """Serialise a GoldSrc studio model: `bones` = [(name, parent, value6, scale6)], the meshes of
    `parts` all on `item_bone` (vertices and normals already in that bone's space), `textures`
    with pixels and palette, `skin` = one skin family (skinref -> texture index). Texture data
    goes last, as studiomdl writes it (Xash may cut a model's memory at texturedataindex)."""
    out = bytearray(244)

    def align():
        out.extend(b"\0" * (-len(out) % 4))

    bone_index = len(out)
    for bone_name, parent, value, scale in bones:
        out += _name(bone_name, 32) + struct.pack("<ii6i6f6f", parent, 0, *([-1] * 6), *value, *scale)
    attachment_index = len(out)
    for attachment_name, origin in attachments:
        out += _name(attachment_name, 32) + struct.pack("<ii3f9f", 0, item_bone, *origin, *([0.0] * 9))
    verts = [v for part in parts for v in part["verts"]]
    if not verts:
        raise ValueError("No vertices")
    mins = tuple(min(v[i] for v in verts) for i in range(3))
    maxs = tuple(max(v[i] for v in verts) for i in range(3))
    hitbox_index = len(out)
    out += struct.pack("<ii6f", item_bone, 0, *mins, *maxs)
    sequence_index = len(out)
    out += bytearray(176)
    group_index = len(out)
    out += _name("default", 32) + bytearray(72)
    anim_index = len(out)
    out += bytearray(12 * len(bones))
    sequence = bytearray(_name("idle", 32))
    sequence += struct.pack("<fiiiiiiiiii3fii3f3fii2i2f2fiiiiii", 30.0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
                            0.0, 0.0, 0.0, 0, 0, *mins, *maxs, 1, anim_index, 0, 0, 0.0, 0.0, 1.0, 0.0,
                            0, 0, 0, 0, 0, 0)
    out[sequence_index:sequence_index + 176] = sequence
    bodypart_index = len(out)
    out += bytearray(76 * len(parts))
    model_index = len(out)
    out += bytearray(112 * len(parts))
    for number, part in enumerate(parts):
        align()
        vertinfo = len(out)
        out += bytes([item_bone]) * len(part["verts"])
        align()
        norminfo = len(out)
        out += bytes([item_bone]) * len(part["norms"])
        align()
        vertindex = len(out)
        for v in part["verts"]:
            out += struct.pack("<3f", *v)
        normindex = len(out)
        for n in part["norms"]:
            out += struct.pack("<3f", *n)
        mesh_index = len(out)
        out += bytearray(20 * len(part["meshes"]))
        first_norm = 0
        for mesh_number, mesh in enumerate(part["meshes"]):
            align()
            tri_index = len(out)
            for count, points in mesh["commands"]:
                out += struct.pack("<h", count)
                for point in points:
                    out += struct.pack("<4h", *point)
            out += struct.pack("<h", 0)
            struct.pack_into("<5i", out, mesh_index + 20 * mesh_number, mesh["numtris"], tri_index,
                             mesh["skinref"], mesh["numnorms"], first_norm)
            first_norm += mesh["numnorms"]
        radius = max(math.sqrt(sum(c * c for c in v)) for v in part["verts"])
        record = model_index + 112 * number
        out[record:record + 64] = _name(part["model"], 64)
        struct.pack_into("<if10i", out, record + 64, 0, radius, len(part["meshes"]), mesh_index,
                         len(part["verts"]), vertinfo, vertindex, len(part["norms"]), norminfo, normindex, 0, 0)
        out[bodypart_index + 76 * number:bodypart_index + 76 * number + 64] = _name(part["name"], 64)
        struct.pack_into("<iii", out, bodypart_index + 76 * number + 64, 1, 1, record)
    align()
    texture_index = len(out)
    out += bytearray(80 * len(textures))
    skin_index = len(out)
    out += struct.pack("<" + "h" * len(skin), *skin)
    align()
    texture_data = len(out)
    for number, texture in enumerate(textures):
        record = texture_index + 80 * number
        out[record:record + 64] = _name(texture["name"], 64)
        struct.pack_into("<4i", out, record + 64, texture["flags"], texture["width"], texture["height"], len(out))
        out += texture["pixels"] + texture["palette"]
        align()
    out[0:8] = b"IDST" + struct.pack("<i", 10)
    out[8:72] = _name(name, 64)
    struct.pack_into("<i", out, 72, len(out))
    struct.pack_into("<26i", out, 140, len(bones), bone_index, 0, attachment_index, 1, hitbox_index,
                     1, sequence_index, 1, group_index, len(textures), texture_index, texture_data,
                     len(skin), 1, skin_index, len(parts), bodypart_index, len(attachments), attachment_index,
                     0, 0, 0, 0, 0, 0)
    result = bytes(out)
    simon.validate_model(result)
    return result


# ---------------------------------------------------------------------------------------------
# Grips
# ---------------------------------------------------------------------------------------------

# Centre of the closed fist in Bip01 R/L Hand local space (+X toward the fingers, +Z toward the
# right thumb). Measured from the police handles (sledgehammer, branch, radio, syringe,
# p_flashlight): their axes run along hand Z through (2.6..3.3, 0.3..0.9, -0.3..0.3).
FIST = (3.0, 0.6, 0.0)
# The left hand mirrors the right one with its local Z flipped (Biped). An item that is symmetric
# about its own mid-plane sits in the left fist like in the right fist after a half turn about the
# hand X axis through the fist centre.
MIRROR = [[1.0, 0.0, 0.0, 0.0], [0.0, -1.0, 0.0, 2 * FIST[1]], [0.0, 0.0, -1.0, 2 * FIST[2]]]
LEVEL = ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0))  # forward along the view at pitch 0, upright


def principal_axis(points):
    """Longest principal axis (power iteration on the covariance), signed toward +X."""
    count = len(points)
    mean = tuple(sum(p[i] for p in points) / count for i in range(3))
    cov = [[sum((p[i] - mean[i]) * (p[j] - mean[j]) for p in points) / count for j in range(3)] for i in range(3)]
    axis = (1.0, 0.0, 0.0)
    for _ in range(200):
        axis = normalize(tuple(sum(cov[i][j] * axis[j] for j in range(3)) for i in range(3)))
    return axis if axis[0] >= 0 else tuple(-c for c in axis)


def hand_matrix(player: Studio, sequence, side="R"):
    """Bip01 <side> Hand in Simon's pose: frame 0, gait idle, pitch 0 (what other players see at rest)."""
    return player_pose(player, player.sequence(sequence))[player.bone(f"Bip01 {side} Hand")]


def solve_grip(item, target, player: Studio | None, side="R"):
    """Item space -> hand local transform. `item` = (primary, secondary, point) in item space.
    `target` = {"sequence": name or None, "primary": (space, vec), "secondary": (space, vec)}
    with space "hand" (hand local) or "world" (Simon's pose for that sequence). The primary axis
    is matched exactly, the secondary as far as it is perpendicular, and the point lands in FIST."""
    hand = None
    if any(target[key][0] == "world" for key in ("primary", "secondary")):
        hand = invert(hand_matrix(player, target["sequence"], side))

    def local(spec):
        space, vector = spec
        return tuple(vector) if space == "hand" else rotate(hand, vector)

    goal = frame_from_axes(local(target["primary"]), local(target["secondary"]), FIST)
    return concat(goal, invert(frame_from_axes(item[0], item[1], item[2])))


def bone_value(matrix):
    """(x, y, z, roll, pitch, yaw) of a bone whose local transform is `matrix`."""
    return tuple(matrix[i][3] for i in range(3)) + matrix_angles(matrix)


# ---------------------------------------------------------------------------------------------
# What to build
# ---------------------------------------------------------------------------------------------

# Police-order sequence indices (cof_s1/cof_s2/police1 share the order) the report refers to.
SEQUENCES = {"idle": 1, "shotgun_idle": 22, "m16_idle": 28, "tmp_idle": 32, "pistol_idle": 36,
             "branch_idle": 48, "sledgehammer_idle": 50, "radio_idle": 55, "rifle_idle": 79}
DUAL_SEQUENCE = "pistol_idle"

# Story items on the right hand, from the w_ model mesh (item axes in w_ model space).
# game: what SetAnimation plays (hl.dll 0x100E03A0, by weapon id); suggest: a better body
# sequence the engine may send instead (None: the game's choice is fine).
STORY_ITEMS = {
    "p_axe": {"id": 24, "classname": "weapon_axe", "source": "models/weapons/axe/w_axe.mdl",
              "item": ((1.0, 0.03, 0.0), (0.0, -1.0, 0.0), (-7.5, 0.45, 0.9)),  # handle toward head, edge, grip
              "target": {"sequence": None, "primary": ("hand", (0, 0, 1)), "secondary": ("hand", (1, 0, 0))},
              "game": "idle", "suggest": "sledgehammer_idle"},
    "p_famas": {"id": 23, "classname": "weapon_famas", "source": "models/weapons/famas/w_famas.mdl",
                "item": ((0.8425, 0.5387, 0.0), (-0.5387, 0.8425, 0.0), (1.40, -4.0, 1.64)),  # bore, top, pistol grip
                # pistol_idle (arms forward) shoulders the bullpup; in m16_idle (hand at the hip) its
                # stock would stick out behind Simon's back
                "target": {"sequence": "pistol_idle", "primary": ("world", LEVEL[0]), "secondary": ("world", LEVEL[1])},
                "attachments": (("", (20.0, 12.2, 1.64)),), "game": "idle", "suggest": "pistol_idle"},
    "p_lantern": {"id": 10, "classname": "weapon_lantern", "source": "models/weapons/lantern/w_lantern.mdl",
                  "item": ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 0.0, 17.6)),  # up, bail bar, bail top
                  "target": {"sequence": "branch_idle", "primary": ("world", (0, 0, 1)), "secondary": ("hand", (0, 0, 1))},
                  "game": "branch_idle", "suggest": None},
    "p_flare": {"id": 13, "classname": "weapon_flare", "source": "models/weapons/flare/w_flare.mdl",
                "item": ((0.307, -0.952, 0.0), (0.0, 0.0, 1.0), (-0.16, 2.1, 0.56)),  # toward the burning end, grip near the cap
                "target": {"sequence": None, "primary": ("hand", (0, 0, 1)), "secondary": ("hand", (0, 1, 0))},
                "game": "idle", "suggest": None},
    "p_mobile": {"id": 2, "classname": "weapon_mobile", "source": "models/weapons/mobile/w_mobile.mdl",
                 "item": ((-1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (2.0, 0.0, 0.6)),  # top end, screen, lower half
                 "target": {"sequence": None, "primary": ("hand", (0, 0, 1)), "secondary": ("hand", (-1, 0, 0))},
                 "game": "radio_idle", "suggest": None},
    "p_camera": {"id": 5, "classname": "weapon_camera", "source": "models/weapons/camera/w_camera.mdl",
                 "item": ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (-0.8, -2.6, 2.4)),  # lens, top, right-hand grip
                 "target": {"sequence": None, "primary": ("hand", (1, 0, 0)), "secondary": ("hand", (0, 0, 1))},
                 "game": "idle", "suggest": "radio_idle"},
    "p_booklaser": {"id": 25, "classname": "weapon_booklaser", "source": "models/weapons/booklaser/w_booklaser.mdl",
                    "item": ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (8.0, 0.8, 0.8)),  # spine, page side, outer edge
                    "target": {"sequence": None, "primary": ("hand", (0, 0, 1)), "secondary": ("hand", (-1, 0, 0))},
                    "game": "idle", "suggest": "radio_idle"},
    "p_browning": {"id": 20, "classname": "weapon_browning_wheelchair", "source": "models/weapons/browning/w_browning.mdl",
                   "item": ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (-1.7, -1.8, 0.6)),  # bore, top, grip
                   "target": {"sequence": "pistol_idle", "primary": ("world", LEVEL[0]), "secondary": ("world", LEVEL[1])},
                   "attachments": (("", (4.7, 0.4, 0.6)),), "game": "idle", "suggest": "pistol_idle"},
}

# Police long guns re-gripped for Simon: barrel level and forward, top up, in the gun's own sequence.
# Only guns more than 5 degrees off the view on Simon are copied (stock errors: rifle 39.6,
# shotgun 15.6, tmp 7.4; m16 4.0 and g43 3.8 keep the police models).
LONG_GUNS = {
    "p_rifle": {"id": 11, "classname": "weapon_rifle", "source": "police/models/weapons/rifle/p_rifle.mdl", "game": "rifle_idle"},
    "p_shotgun": {"id": 4, "classname": "weapon_shotgun", "source": "police/models/weapons/shotgun/p_shotgun.mdl", "game": "shotgun_idle"},
    "p_tmp": {"id": 14, "classname": "weapon_tmp", "source": "police/models/weapons/tmp/p_tmp.mdl", "game": "tmp_idle"},
}

# Dual-wield models. First person (client.dll, see the report) shows the light (P+0x1E74) in the
# right hand and the weapon (P+0x1E70) mirrored into the left hand, so third person does the same:
# weaponmodel = the right-hand model of P+0x1E74, a follow entity = the l_ model of P+0x1E70, body
# sequence DUAL_SEQUENCE. l_ models: on Bip01 L Hand. r_ models: right-hand lights re-aimed for the
# dual pose where the single-wield model would point up or lie flat there (flashlight, lantern).
# "source" = a stock p_ model as the right hand holds it, or one of the story items above. Lights,
# the phone, the lantern and pistols are placed for Simon's dual pose; melee weapons keep their
# police grip mirrored into the left fist, which looks natural in any pose.
UPRIGHT = {"primary": ("world", (0, 0, 1)), "secondary": ("world", (-1, 0, 0))}  # phone: top up, screen to the player
HANG = {"primary": ("world", (0, 0, 1)), "secondary": ("hand", (0, 0, 1))}  # lantern: upright, bail across the fist
AIM = {"primary": ("world", LEVEL[0]), "secondary": ("world", LEVEL[1])}
DUAL_ITEMS = {
    "r_flashlight": {"id": 1, "side": "R", "source": "models/weapons/flashlight/p_flashlight.mdl", "target": AIM},
    "r_lantern": {"id": 10, "side": "R", "source": "p_lantern", "target": HANG},
    "l_flashlight": {"id": 1, "source": "models/weapons/flashlight/p_flashlight.mdl", "target": AIM},
    "l_mobile": {"id": 2, "source": "p_mobile", "target": UPRIGHT},
    "l_lantern": {"id": 10, "source": "p_lantern", "target": HANG},
    "l_flare": {"id": 13, "source": "p_flare", "target": AIM},
    "l_glock": {"id": 3, "source": "police/models/weapons/glock/p_glock.mdl", "target": AIM},
    "l_p345": {"id": 7, "source": "police/models/weapons/p345/p_p345.mdl", "target": AIM},
    "l_vp70": {"id": 19, "source": "police/models/weapons/vp70/p_vp70.mdl", "target": AIM},
    "l_revolver": {"id": 21, "source": "police/models/weapons/revolver/p_revolver.mdl", "target": AIM},
    "l_switchblade": {"id": 6, "source": "police/models/weapons/switchblade/p_switchblade.mdl", "target": "mirror"},
    "l_nightstick": {"id": 12, "source": "police/models/weapons/nightstick/p_nightstick.mdl", "target": "mirror"},
    "l_branch": {"id": 16, "source": "police/models/weapons/branch/p_branch.mdl", "target": "mirror"},
}

ARM_CHAIN = ("Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Spine2", "Bip01 Spine3",
             "Bip01 Neck", "Bip01 {side} Arm", "Bip01 {side} Arm1", "Bip01 {side} Arm2", "Bip01 {side} Hand")
ITEM_BONE = {"R": "SHOTGUN", "L": "LEFTITEM"}  # never a player bone name, so the client keeps it ours
ITEM_SCALE = (0.001, 0.001, 0.001, 0.0001, 0.0001, 0.0001)  # unused: the models have no animation


def skeleton(police: Studio, side, grip):
    """The 12-bone merge skeleton of the police p_ models for one hand, defaults from police1,
    plus the item bone whose local transform is the grip."""
    bones = []
    for index, pattern in enumerate(ARM_CHAIN):
        name = pattern.format(side=side)
        bone = police.bones[police.bone(name)]
        bones.append((name, index - 1, bone["value"], bone["scale"]))
    bones.append((ITEM_BONE[side], len(ARM_CHAIN) - 1, bone_value(grip), ITEM_SCALE))
    return bones


def item_assets(model: Studio, root_bone=None):
    """Mesh parts, third-person sized textures and skin family 0 of a stock model."""
    parts = read_mesh(model, root_bone=root_bone)
    textures = read_textures(model.data)
    skin = read_skins(model.data)[0]
    small = [shrink_texture(texture) for texture in textures]
    for part in parts:
        for mesh in part["meshes"]:
            if not 0 <= mesh["skinref"] < len(skin) or not 0 <= skin[mesh["skinref"]] < len(textures):
                raise ValueError("Invalid skin reference")
            old, new = textures[skin[mesh["skinref"]]], small[skin[mesh["skinref"]]]
            sx, sy = new["width"] / old["width"], new["height"] / old["height"]
            if (sx, sy) != (1.0, 1.0):
                mesh["commands"] = [(count, [(v, n, int(round(s * sx)), int(round(t * sy))) for v, n, s, t in points])
                                    for count, points in mesh["commands"]]
    return parts, small, skin


def regrip_gun(data, player: Studio, sequence):
    """A police long gun with only its SHOTGUN bone turned (about the fist centre) so that, in
    Simon's pose for `sequence`, the barrel points along the view and the gun stays upright."""
    model = Studio(data)
    index = model.bone("SHOTGUN")
    anim = model.sequences[0]["animindex"] + 12 * index
    if len(model.sequences) != 1 or any(struct.unpack_from("<6H", data, anim)):
        raise ValueError("Expected an unanimated SHOTGUN bone in a one-sequence p_ model")
    held = read_mesh(model, root_bone="Bip01 R Hand")
    barrel = principal_axis([v for part in held for v in part["verts"]])
    turn = solve_grip((barrel, (0.0, 0.0, 1.0), FIST),
                      {"sequence": sequence, "primary": ("world", LEVEL[0]), "secondary": ("world", LEVEL[1])}, player)
    grip = concat(turn, bone_matrix(model.bones[index]["value"]))
    out = bytearray(data)
    struct.pack_into("<6f", out, model.bones[index]["offset"] + 64, *bone_value(grip))
    simon.validate_model(bytes(out))
    return bytes(out), barrel


def angle_between(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, dot(normalize(a), normalize(b))))))


def build_models(files: dict[str, bytes]):
    """files: SOURCES path -> stock bytes. Returns {cryoffear-relative path: bytes} including
    the coop-weapons.json manifest. Refuses any source whose hash is not the pinned one."""
    for path, expected in SOURCES.items():
        if path not in files or sha(files[path]) != expected:
            raise ValueError(f"Stock source missing or hash mismatch: {path}")
    simon_bytes, _mapping = simon.adapt_sequences(files["models/cutscene/player.mdl"],
                                                  files["models/player/police1/police1.mdl"])
    player = Studio(simon_bytes)
    police = Studio(files["models/player/police1/police1.mdl"])
    outputs, records, right_grips = {}, {}, {}

    def add(name, data, sources, **info):
        path = f"{OUTPUT_DIR}/{name}.mdl"
        if len(path) >= 64:
            raise ValueError("Model path too long for the engine")
        outputs[path] = data
        records[path] = {"sha256": sha(data), "bytes": len(data), **simon.validate_model(data),
                         "sources": [{"path": source, "sha256": SOURCES[source]} for source in sources], **info}

    for name, spec in STORY_ITEMS.items():
        model = Studio(files[spec["source"]])
        parts, textures, skin = item_assets(model)
        grip = solve_grip(spec["item"], spec["target"], player)
        right_grips[name] = (spec, grip, parts, textures, skin)
        data = write_studio(name + ".mdl", skeleton(police, "R", grip), len(ARM_CHAIN), parts, textures, skin,
                            spec.get("attachments", ()))
        add(name, data, [spec["source"], "models/player/police1/police1.mdl", "models/cutscene/player.mdl"],
            hand="R", weapon_id=spec["id"], classname=spec["classname"],
            game_sequence={"name": spec["game"], "index": SEQUENCES[spec["game"]]},
            design_sequence=spec["target"]["sequence"],
            suggested_sequence=({"name": spec["suggest"], "index": SEQUENCES[spec["suggest"]]} if spec["suggest"] else None))

    for name, spec in LONG_GUNS.items():
        data, barrel = regrip_gun(files[spec["source"]], player, spec["game"])
        before = rotate(hand_matrix(player, spec["game"]), barrel)
        add(name, data, [spec["source"], "models/player/police1/police1.mdl", "models/cutscene/player.mdl"],
            hand="R", weapon_id=spec["id"], classname=spec["classname"],
            game_sequence={"name": spec["game"], "index": SEQUENCES[spec["game"]]},
            stock_barrel_error_degrees=round(angle_between(before, LEVEL[0]), 1))

    for name, spec in DUAL_ITEMS.items():
        side = spec.get("side", "L")
        if spec["source"] in right_grips:
            item_spec, right, parts, textures, skin = right_grips[spec["source"]]
            item, sources = item_spec["item"], [item_spec["source"]]
        else:  # a stock p_ model: its mesh as the right hand holds it
            model = Studio(files[spec["source"]])
            parts, textures, skin = item_assets(model, root_bone="Bip01 R Hand")
            right, sources = IDENTITY, [spec["source"]]
            if spec["source"].endswith("p_flashlight.mdl"):  # lens end toward the right thumb (+Z)
                item = ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), FIST)
            else:  # pistols: barrel along the principal axis, top toward the thumb
                item = (principal_axis([v for part in parts for v in part["verts"]]), (0.0, 0.0, 1.0), FIST)
        if spec["target"] == "mirror":
            grip = concat(MIRROR, right)
        else:
            grip = solve_grip(item, {**spec["target"], "sequence": DUAL_SEQUENCE}, player, side)
        data = write_studio(name + ".mdl", skeleton(police, side, grip), len(ARM_CHAIN), parts, textures, skin)
        add(name, data, sources + ["models/player/police1/police1.mdl", "models/cutscene/player.mdl"],
            hand=side, weapon_id=spec["id"], grip="mirrored right-hand grip" if spec["target"] == "mirror" else "dual pose",
            design_sequence={"name": DUAL_SEQUENCE, "index": SEQUENCES[DUAL_SEQUENCE]})

    manifest = {
        "generator": "tools/prepare_coop_weapons.py",
        "relative_to": "cryoffear",
        "mapping": engine_mapping(),
        "outputs": records,
        "limitations": [
            "Grips are solved for frame 0 of Simon's (cof_s1/cof_s2) pose at pitch 0 with the idle gait.",
            "Generated models are private stock-derived assets: build them on the player's machine, never ship them.",
        ],
    }
    outputs["coop-weapons.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    return outputs


# Stock right-hand p_ models the multiplayer Deploys already use (precached in MP), by weapon id.
STOCK_RIGHT = {3: "police/models/weapons/glock/p_glock.mdl", 7: "police/models/weapons/p345/p_p345.mdl",
               19: "police/models/weapons/vp70/p_vp70.mdl", 21: "police/models/weapons/revolver/p_revolver.mdl",
               6: "police/models/weapons/switchblade/p_switchblade.mdl", 12: "police/models/weapons/nightstick/p_nightstick.mdl",
               16: "police/models/weapons/branch/p_branch.mdl"}


def engine_mapping():
    """What the engine needs, by weapon id (m_iId, hl.dll GetItemInfo): the weaponmodel to send
    in story co-op and the body sequence to send instead of the game's, if any."""
    single = {}
    for name, spec in {**STORY_ITEMS, **LONG_GUNS}.items():
        suggest = spec.get("suggest")
        single[str(spec["id"])] = {"classname": spec["classname"], "model": f"{OUTPUT_DIR}/{name}.mdl",
                                   "sequence": SEQUENCES[suggest] if suggest else None}
    single["27"] = {"classname": "weapon_mp5", "model": f"{OUTPUT_DIR}/p_tmp.mdl", "sequence": SEQUENCES["tmp_idle"],
                    "note": "retail sends p_tmp and the missing mp5_idle (sequence -1)"}
    right = {str(spec["id"]): f"{OUTPUT_DIR}/{name}.mdl" for name, spec in DUAL_ITEMS.items() if spec.get("side") == "R"}
    right.update({str(STORY_ITEMS[name]["id"]): f"{OUTPUT_DIR}/{name}.mdl" for name in ("p_mobile", "p_flare")})
    right.update({str(number): path for number, path in STOCK_RIGHT.items()})
    left = {str(spec["id"]): f"{OUTPUT_DIR}/{name}.mdl" for name, spec in DUAL_ITEMS.items() if spec.get("side", "L") == "L"}
    return {"single": single,
            "dual": {"weapon_id": 15, "sequence": SEQUENCES[DUAL_SEQUENCE], "sequence_name": DUAL_SEQUENCE,
                     "right_hand_from": "P+0x1E74 (m_iRightWeaponID, the light or the second pistol)",
                     "left_hand_from": "P+0x1E70 (m_iLeftWeaponID, the weapon)",
                     "right": dict(sorted(right.items(), key=lambda item: int(item[0]))),
                     "left": dict(sorted(left.items(), key=lambda item: int(item[0])))}}


def read_sources(game_dir: Path):
    root = game_dir / "cryoffear"
    return {path: (root / path).read_bytes() for path in SOURCES}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--game-dir", type=Path, default=core.DEFAULT_GAME_DIR,
                        help="Existing game root containing cryoffear/models, read only")
    parser.add_argument("--out", type=Path, default=core.PROJECT_ROOT / "evidence/coop-weapons")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args(argv)
    try:
        out = core._local_path(args.out)
        evidence = (core.PROJECT_ROOT / "evidence").resolve()
        if out == evidence or not out.is_relative_to(evidence) or out.exists():
            raise ValueError("Choose a fresh subdirectory of project evidence")
        files = build_models(read_sources(core._local_path(args.game_dir)))
        if args.write:
            out.mkdir(parents=True, exist_ok=False)
            for name, data in files.items():
                path = out / name
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as stream:
                    stream.write(data)
        manifest = json.loads(files["coop-weapons.json"])
        print(json.dumps({"output": str(out), "written": args.write,
                          "models": {path: {"sha256": record["sha256"], "bytes": record["bytes"]}
                                     for path, record in manifest["outputs"].items()}}, indent=2))
    except (ValueError, OSError, KeyError, struct.error) as exc:
        print(f"Weapon model preparation refused: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
