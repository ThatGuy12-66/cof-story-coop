"""Two maps' geometry stacked into one BSP30 file: map b moved by dz in height above or below map a.

Geometry only (planes, textures, vertexes, vis, nodes, clip hulls, leafs, faces, lighting, models); the entity
lump is a's worldspawn with b's wad files added and the larger view distance. The entities themselves are put
together by the engine at map load from each member's own entity source and the .sections rules (rules.py).
"""
from __future__ import annotations

from dataclasses import dataclass

from tools.map_merge.bsp import (WORLD_HALF, Bsp, bits_of, build_textures, compress_vis, decompress_vis, ent_get,
                                 f32, face_box, relay, serialize_ents, styles_of, tex_name)

GAP = 128.0
STEP = 512.0
DEFAULT_VIEW_DISTANCE = 4096.0      # the HLSDK worldspawn: sv_zmax = MaxRange, or 4096 (every story map sets one)


@dataclass
class PairInfo:
    dz: float
    above: bool
    split: float
    Aw: int     # a's world faces
    Af: int     # a's faces
    Bw: int
    Bf: int
    Av: int     # a's visible (world) leafs
    Bv: int
    An: int     # a's leafs
    Bn: int
    a_models: int
    lightmaps_relaid: int
    textures_added: int


def choose_dz(a_z: tuple, b_z: tuple) -> float:
    """The height to move b by: the smallest multiple of 512 that leaves a 128-unit gap to a and keeps b
    within the +-4096 world, trying above before below at each size."""
    (a_lo, a_hi), (b_lo, b_hi) = a_z, b_z
    for k in range(1, int(2 * WORLD_HALF / STEP) + 1):
        up = k * STEP
        if b_lo + up >= a_hi + GAP and b_hi + up <= WORLD_HALF - 16:
            return up
        if b_hi - up <= a_lo - GAP and b_lo - up >= -WORLD_HALF + 16:
            return -up
    raise ValueError('the second map does not fit above or below the first within +-4096')


def remap_styles(face: list, style_map: dict) -> list:
    out = list(face)
    for k in range(5, 9):
        out[k] = style_map.get(out[k], out[k])
    return out


def wad_names(value: str) -> set:
    """The wad files a worldspawn names, by file name (the engine looks each up by its base name)."""
    names = set()
    for item in value.split(';'):
        item = item.strip().replace('\\', '/')
        if item:
            names.add(item.rsplit('/', 1)[-1].lower())
    return names


def joined_wads(a_value: str, b_value: str) -> str:
    items = [x for x in a_value.split(';') if x.strip()]
    seen = wad_names(a_value)
    for item in b_value.split(';'):
        name = item.strip().replace('\\', '/').rsplit('/', 1)[-1].lower()
        if name and name not in seen:
            seen.add(name)
            items.append(item.strip())
    return ';'.join(items) + (';' if items else '')


def view_distance(world: list) -> float:
    """The view distance hl.dll takes from a worldspawn: MaxRange, 4096 when absent."""
    return float(ent_get(world, 'MaxRange', '') or DEFAULT_VIEW_DISTANCE)


def merged_worldspawn(worlds: list) -> list:
    """The merged file's worldspawn: the first member's, with every member's wad files joined and the largest view
    distance (MaxRange) of them all."""
    world = [list(kv) for kv in worlds[0]]
    joined = ent_get(world, 'wad', '')
    for other in worlds[1:]:
        joined = joined_wads(joined, ent_get(other, 'wad', ''))
    world = [[k, joined if k == 'wad' else v] for k, v in world]
    if not any(k == 'wad' for k, _ in world) and joined:
        world.append(['wad', joined])
    farthest = max(worlds, key=view_distance)
    if view_distance(farthest) > view_distance(world):
        value = ent_get(farthest, 'MaxRange')
        world = [[k, value if k == 'MaxRange' else v] for k, v in world]
        if not any(k == 'MaxRange' for k, _ in world):
            world.append(['MaxRange', value])
    return world


def origin_models(x: Bsp) -> set:
    """The submodels whose entity has an origin key. An origin-brush entity's geometry lies around that origin (a
    rotating door's hinge, most trains) and the engine's composer moves the origin key with the member
    (cof_sections.h CoF_SectMovedOrigin): the merger leaves that geometry unmoved."""
    with_origin, without = set(), set()
    for e in x.ents:
        model = ent_get(e, 'model', '')
        if model.startswith('*') and model[1:].isdigit() and 1 <= int(model[1:]) < len(x.models):
            (with_origin if ent_get(e, 'origin') is not None else without).add(int(model[1:]))
    if with_origin & without:
        raise ValueError(f'model *{min(with_origin & without)} is used with and without an origin')
    return with_origin


def model_parts(x: Bsp, k: int) -> tuple:
    """(nodes, clipnodes, leafs) model k's hull trees reach; leaf 0, the solid leaf every tree shares, left out."""
    m = x.models[k]
    nodes, leafs, stack = set(), set(), [m[9]]
    while stack:
        n = stack.pop()
        if n < 0:
            if n != -1:
                leafs.add(-n - 1)
        elif n not in nodes:
            nodes.add(n)
            stack.extend(x.nodes[n][1:3])
    clips, stack = set(), [m[h] for h in (10, 11, 12) if 0 <= m[h] < len(x.clipnodes)]
    while stack:
        c = stack.pop()
        if c >= 0 and c not in clips:
            clips.add(c)
            stack.extend(x.clipnodes[c][1:3])
    return nodes, clips, leafs


def fixed_parts(b: Bsp, fixed: set) -> tuple:
    """(faces, nodes, clipnodes, leafs) of b's models in fixed; refused when the moved geometry shares any of them."""
    parts = {k: model_parts(b, k) for k in range(len(b.models))}
    mine = [set().union(*(parts[k][i] for k in fixed)) for i in range(3)]
    others = [set().union(*(parts[k][i] for k in parts if k not in fixed)) for i in range(3)]
    if any(m & o for m, o in zip(mine, others)):
        raise ValueError('an origin-brush model shares its hull tree with moved geometry')
    faces = {f for k in fixed for f in range(b.models[k][14], b.models[k][14] + b.models[k][15])}
    return (faces, *mine)


def world_nodes(x: Bsp) -> int:
    """How many nodes the world's tree has; they must be nodes 0..count-1."""
    seen, stack = set(), [x.models[0][9]]
    while stack:
        n = stack.pop()
        if n >= 0 and n not in seen:
            seen.add(n)
            stack.extend(x.nodes[n][1:3])
    if seen != set(range(len(seen))):
        raise ValueError('world nodes are not 0..count-1')
    return len(seen)


def fix_lightmaps(b: Bsp, out: dict, face_index) -> int:
    """Moving a map can tip a face's lightmap box across a 16-unit line by float rounding: the engine would
    then read the face's light with the wrong width. Lay such faces' light out again for the box the engine
    will compute (appended to the lighting lump). Returns the number of faces re-laid."""
    vertexes, edges, surfedges = out['vertexes'], out['edges'], out['surfedges']
    extra = bytearray()
    base = len(out['lighting'])
    fixed = 0
    for i, f in enumerate(b.faces):
        if f[9] < 0:
            continue
        g = out['faces'][face_index(i)]
        old = face_box(b.vertexes, b.edges, b.surfedges, f, b.texinfo[f[4]])
        new = face_box(vertexes, edges, surfedges, g, out['texinfo'][g[4]])
        if old == new:
            continue
        n = styles_of(f)
        size = (old[0][1] - old[0][0] + 1) * (old[1][1] - old[1][0] + 1) * 3 * n
        g[9] = base + len(extra)
        extra += relay(b.lighting[f[9]:f[9] + size], old, new, n)
        fixed += 1
    out['lighting'] = out['lighting'] + bytes(extra)
    return fixed


def merge_pair(a: Bsp, b: Bsp, dz: float, style_map: dict) -> tuple[dict, PairInfo]:
    """a and b (moved by dz) as one file's lumps. b's face light styles go through style_map."""
    amin, amax = a.models[0][0:3], a.models[0][3:6]
    bmin, bmax = b.models[0][0:3], b.models[0][3:6]
    above = dz > 0
    d = (0.0, 0.0, float(dz))
    split = (amax[2] + bmin[2] + d[2]) / 2 if above else (amin[2] + bmax[2] + d[2]) / 2

    Aw, Af = a.models[0][15], len(a.faces)
    Bw, Bf = b.models[0][15], len(b.faces)
    if a.models[0][14] != 0 or b.models[0][14] != 0:
        raise ValueError('world faces must start at 0')
    for x, w in ((a, Aw), (b, Bw)):
        if any(m[14] < w and m[15] for m in x.models[1:]):
            raise ValueError('a submodel shares the world faces')
    Av, Bv = a.models[0][13], b.models[0][13]
    An, Bn = len(a.leafs), len(b.leafs)
    ANodes, AClip, AP = len(a.nodes), len(a.clipnodes), len(a.planes)

    def faceA(f): return f if f < Aw else f + Bw
    def faceB(f): return Aw + f if f < Bw else Af + f
    def leafA(l): return l if l <= Av else l + Bv
    def leafB(l): return 0 if l == 0 else (Av + l if l <= Bv else An - 1 + l)
    # The engine takes a model's hull 0 as [head, head + nodes reached] (CountClipNodes16_r), so the merged
    # world's nodes must be one block: new root, a's world nodes, b's world nodes, then the submodels' nodes.
    Awn, Bwn = world_nodes(a), world_nodes(b)
    def nodeA(n): return 1 + n if n < Awn else 1 + n + Bwn
    def nodeB(n): return 1 + Awn + n if n < Bwn else 1 + ANodes + n
    def childA(c): return nodeA(c) if c >= 0 else -(leafA(-c - 1) + 1)
    def childB(c): return nodeB(c) if c >= 0 else -(leafB(-c - 1) + 1)

    # b's origin-brush models stay in their own coordinates: copies of their planes, vertexes, edges and texture
    # alignment, unmoved, after everything else (a merge without such models comes out as before)
    fixed = origin_models(b)
    fixed_faces, fixed_nodes, fixed_clips, fixed_leafs = fixed_parts(b, fixed) if fixed else (set(), set(), set(), set())

    out = {}
    planes = [list(p) for p in a.planes]
    for p in b.planes:
        q = list(p)
        q[3] = f32(p[3] + p[0] * d[0] + p[1] * d[1] + p[2] * d[2])
        planes.append(q)
    split_plane = len(planes)
    planes.append([0.0, 0.0, 1.0, split, 2])
    out['planes'] = planes
    plane_copy = {}

    def unmoved_plane(p):
        if p not in plane_copy:
            plane_copy[p] = len(planes)
            planes.append(list(b.planes[p]))
        return plane_copy[p]

    # textures: a's list, plus b's that differ by name or bytes
    mips = list(a.textures)
    by_key = {}
    for i, m in enumerate(mips):
        if m is not None:
            by_key.setdefault((tex_name(m), m), i)
    texmap = {}
    for i, m in enumerate(b.textures):
        key = (tex_name(m), m) if m is not None else None
        if key is not None and key in by_key:
            texmap[i] = by_key[key]
        else:
            texmap[i] = len(mips)
            mips.append(m)
    out['textures'] = build_textures(mips)

    out['vertexes'] = [list(v) for v in a.vertexes] + [[f32(v[0] + d[0]), f32(v[1] + d[1]), f32(v[2] + d[2])] for v in b.vertexes]
    if len(out['vertexes']) > 65535:
        raise ValueError('too many vertexes')
    AV = len(a.vertexes)
    out['edges'] = [list(e) for e in a.edges] + [[e[0] + AV, e[1] + AV] for e in b.edges]
    AE = len(a.edges)
    out['surfedges'] = [list(s) for s in a.surfedges] + [[s[0] + AE if s[0] >= 0 else s[0] - AE] for s in b.surfedges]

    texinfo = [list(t) for t in a.texinfo]
    for t in b.texinfo:
        q = list(t)
        for r in (0, 4):
            q[r + 3] = f32(t[r + 3] - (t[r] * d[0] + t[r + 1] * d[1] + t[r + 2] * d[2]))
        q[8] = texmap[t[8]]
        texinfo.append(q)
    out['texinfo'] = texinfo
    AT, AS, AL = len(a.texinfo), len(a.surfedges), len(a.lighting)
    tex_copy, vertex_copy, edge_copy = {}, {}, {}

    def unmoved_texinfo(t):
        if t not in tex_copy:
            q = list(b.texinfo[t])
            q[8] = texmap[q[8]]
            tex_copy[t] = len(texinfo)
            texinfo.append(q)
        return tex_copy[t]

    def unmoved_vertex(v):
        if v not in vertex_copy:
            vertex_copy[v] = len(out['vertexes'])
            out['vertexes'].append(list(b.vertexes[v]))
        return vertex_copy[v]

    def unmoved_surfedge(se):
        # the face's edge from s to t (a negative surfedge walks its edge backwards), between unmoved vertexes
        e = b.edges[abs(se)]
        s, t = (unmoved_vertex(e[0]), unmoved_vertex(e[1])) if se >= 0 else (unmoved_vertex(e[1]), unmoved_vertex(e[0]))
        if (t, s) in edge_copy:
            return [-edge_copy[(t, s)]]
        if (s, t) not in edge_copy:
            edge_copy[(s, t)] = len(out['edges'])
            out['edges'].append([s, t])
        return [edge_copy[(s, t)]]

    faces = [None] * (Af + Bf)
    for i, f in enumerate(a.faces):
        faces[faceA(i)] = list(f)
    for i, f in enumerate(b.faces):
        q = remap_styles(f, style_map)
        if i in fixed_faces:
            q[0] = unmoved_plane(f[0])
            q[2] = len(out['surfedges'])
            out['surfedges'] += [unmoved_surfedge(b.surfedges[s][0]) for s in range(f[2], f[2] + f[3])]
            q[4] = unmoved_texinfo(f[4])
        else:
            q[0] += AP
            q[2] += AS
            q[4] += AT
        if q[9] >= 0:
            q[9] += AL
        faces[faceB(i)] = q
    out['faces'] = faces
    if len(out['vertexes']) > 65535:
        raise ValueError('too many vertexes')
    out['lighting'] = a.lighting + b.lighting

    out['marksurfaces'] = [[faceA(m[0])] for m in a.marksurfaces] + [[faceB(m[0])] for m in b.marksurfaces]
    if len(out['marksurfaces']) > 65535:
        raise ValueError('too many marksurfaces')
    AM = len(a.marksurfaces)

    # visibility: block-diagonal, rows deduplicated
    V = Av + Bv
    rowA, rowB, rowV = (Av + 7) >> 3, (Bv + 7) >> 3, (V + 7) >> 3
    vis = bytearray()
    cache = {}

    def put(row):
        c = compress_vis(row)
        if c not in cache:
            cache[c] = len(vis)
            vis.extend(c)
        return cache[c]

    leafs = [None] * (An + Bn - 1)
    for i, l in enumerate(a.leafs):
        q = list(l)
        if 1 <= i <= Av and l[1] >= 0:
            row = bytearray(rowV)
            for k in bits_of(decompress_vis(a.vis, l[1], rowA), Av):
                row[k >> 3] |= 1 << (k & 7)
            q[1] = put(bytes(row))
        leafs[leafA(i)] = q
    for i, l in enumerate(b.leafs):
        if i == 0:
            continue
        q = list(l)
        if i not in fixed_leafs:
            q[4] = int(l[4] + d[2])
            q[7] = int(l[7] + d[2])
        q[8] += AM
        if 1 <= i <= Bv and l[1] >= 0:
            row = bytearray(rowV)
            for k in bits_of(decompress_vis(b.vis, l[1], rowB), Bv):
                k2 = Av + k
                row[k2 >> 3] |= 1 << (k2 & 7)
            q[1] = put(bytes(row))
        leafs[leafB(i)] = q
    out['leafs'] = leafs
    out['visibility'] = bytes(vis)

    # nodes: the new root first (renderers start at node 0)
    wmin = [min(amin[k], bmin[k] + d[k]) for k in range(3)]
    wmax = [max(amax[k], bmax[k] + d[k]) for k in range(3)]
    broot, aroot = nodeB(b.models[0][9]), nodeA(a.models[0][9])
    front, back = (broot, aroot) if above else (aroot, broot)
    nodes = [None] * (1 + ANodes + len(b.nodes))
    nodes[0] = [split_plane, front, back, *[int(x) - 1 for x in wmin], *[int(x) + 1 for x in wmax], 0, 0]
    for i, n in enumerate(a.nodes):
        q = list(n)
        q[1], q[2] = childA(n[1]), childA(n[2])
        q[9] = faceA(n[9]) if n[10] else 0
        nodes[nodeA(i)] = q
    for i, n in enumerate(b.nodes):
        q = list(n)
        q[1], q[2] = childB(n[1]), childB(n[2])
        if i in fixed_nodes:
            q[0] = unmoved_plane(n[0])
        else:
            q[0] += AP
            for k in (5, 8):
                q[k] = int(n[k] + d[2])
        q[9] = faceB(n[9]) if n[10] else 0
        nodes[nodeB(i)] = q
    if len(nodes) > 32767:
        raise ValueError('too many nodes')
    out['nodes'] = nodes

    # The engine walks a hull only over [its head, last clipnode] (pm_trace.c "bad node number"): every head
    # must come before its branches, so the three new heads go first. A head at or past the end of a map's
    # clipnodes marks an empty hull (ZHLT) and stays past the end.
    BClip = len(b.clipnodes)
    total = 3 + AClip + BClip
    def clipA(c): return (c + 3 if c < AClip else total) if c >= 0 else c
    def clipB(c): return (c + 3 + AClip if c < BClip else total) if c >= 0 else c
    hull_roots, clip = [], []
    for h in (1, 2, 3):
        broot_h, aroot_h = clipB(b.models[0][9 + h]), clipA(a.models[0][9 + h])
        if broot_h >= total or aroot_h >= total:
            raise ValueError('a world hull is empty')
        f, bk = (broot_h, aroot_h) if above else (aroot_h, broot_h)
        hull_roots.append(len(clip))
        clip.append([split_plane, f, bk])
    for c in a.clipnodes:
        clip.append([c[0], clipA(c[1]), clipA(c[2])])
    for i, c in enumerate(b.clipnodes):
        clip.append([unmoved_plane(c[0]) if i in fixed_clips else c[0] + AP, clipB(c[1]), clipB(c[2])])
    if total > 32767:
        raise ValueError('too many clipnodes')
    out['clipnodes'] = clip

    models = [[*wmin, *wmax, 0.0, 0.0, 0.0, 0, *hull_roots, V, 0, Aw + Bw]]
    for m in a.models[1:]:
        q = list(m)
        q[9] = nodeA(m[9])
        for h in (10, 11, 12):
            q[h] = clipA(m[h])
        q[14] = faceA(m[14]) if m[15] else 0
        models.append(q)
    for k_model, m in enumerate(b.models[1:], 1):
        q = list(m)
        if k_model not in fixed:
            for k in (2, 5):
                q[k] = m[k] + d[2]
        q[9] = nodeB(m[9])
        for h in (10, 11, 12):
            q[h] = clipB(m[h])
        q[14] = faceB(m[14]) if m[15] else 0
        models.append(q)
    out['models'] = models

    out['entities'] = serialize_ents([merged_worldspawn([a.ents[0], b.ents[0]])])
    relaid = fix_lightmaps(b, out, faceB)
    info = PairInfo(dz=float(dz), above=above, split=split, Aw=Aw, Af=Af, Bw=Bw, Bf=Bf, Av=Av, Bv=Bv, An=An, Bn=Bn,
                    a_models=len(a.models), lightmaps_relaid=relaid,
                    textures_added=sum(1 for i in texmap.values() if i >= len(a.textures)))
    return out, info
