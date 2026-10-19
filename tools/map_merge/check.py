"""The check run after every pair merge: the merged file is the two maps, nothing lost or moved wrongly.

Structure (every index in range), hull walks as the engine does them, what is at sampled points in all four hulls,
what every leaf can see, and every face's texture, light styles and light data.
"""
from __future__ import annotations

import math
import random

from tools.map_merge.bsp import Bsp, bits_of, decompress_vis, ent_get, f32, relay
from tools.map_merge.merge import PairInfo, origin_models

LIMIT = 60


def lint(x: Bsp, name: str, bad) -> None:
    P, N, L, F, C = len(x.planes), len(x.nodes), len(x.leafs), len(x.faces), len(x.clipnodes)
    for i, n in enumerate(x.nodes):
        if not 0 <= n[0] < P: bad(f'{name} node {i} plane')
        for c in n[1:3]:
            if c >= N or (c < 0 and -c - 1 >= L): bad(f'{name} node {i} child {c}')
        if n[9] + n[10] > F: bad(f'{name} node {i} faces')
    for i, c in enumerate(x.clipnodes):
        if not 0 <= c[0] < P: bad(f'{name} clip {i} plane')
        for ch in c[1:3]:
            if ch >= C or ch < -15: bad(f'{name} clip {i} child {ch}')
    for i, f in enumerate(x.faces):
        if not 0 <= f[0] < P or f[2] + f[3] > len(x.surfedges) or not 0 <= f[4] < len(x.texinfo): bad(f'{name} face {i}')
        if f[9] >= 0 and f[9] > len(x.lighting): bad(f'{name} face {i} light')
    for i, l in enumerate(x.leafs):
        if l[8] + l[9] > len(x.marksurfaces): bad(f'{name} leaf {i} marks')
        if l[1] >= len(x.vis) and l[1] != -1: bad(f'{name} leaf {i} vis')
    for s in x.surfedges:
        if abs(s[0]) >= len(x.edges): bad(f'{name} surfedge')
    for e in x.edges:
        if max(e) >= len(x.vertexes): bad(f'{name} edge')
    for mk in x.marksurfaces:
        if mk[0] >= F: bad(f'{name} marksurface')
    for i, mo in enumerate(x.models):
        if mo[9] >= N or mo[14] + mo[15] > F: bad(f'{name} model {i}')
    for t in x.texinfo:
        if not 0 <= t[8] < len(x.textures): bad(f'{name} texinfo miptex')


def hull_ranges(x: Bsp, name: str, bad) -> list:
    """The engine walks each model's hull only over [head, last]: every node a tree reaches must not come before
    its head (pm_trace.c "bad node number"), and hull 0's nodes must be one block from the head
    (CountClipNodes16_r). Returns which hulls are empty per model."""
    empty = []
    for mi, mo in enumerate(x.models):
        flags = []
        for h in range(4):
            head = mo[9 + h]
            if h == 0:
                stack, kids = [head], lambda n: [c for c in x.nodes[n][1:3] if c >= 0]
            else:
                if head >= len(x.clipnodes) or head < 0:
                    flags.append(True)
                    continue
                stack, kids = [head], lambda n: [c for c in x.clipnodes[n][1:3] if c >= 0]
            flags.append(False)
            seen = set()
            while stack:
                n = stack.pop()
                if n in seen:
                    continue
                if (h == 0 and n >= len(x.nodes)) or (h and n >= len(x.clipnodes)):
                    bad(f'{name} model {mi} hull {h}: node {n} past the end')
                    break
                seen.add(n)
                if n < head:
                    bad(f'{name} model {mi} hull {h}: node {n} before head {head}')
                    break
                stack.extend(kids(n))
            if h == 0 and seen and max(seen) > head + len(seen):
                bad(f'{name} model {mi} hull 0: nodes {head}..{max(seen)} not one block of {len(seen)}')
        empty.append(flags)
    return empty


def leaf_at(x: Bsp, root: int, p) -> int:
    n = root
    while n >= 0:
        node = x.nodes[n]
        pl = x.planes[node[0]]
        n = node[1] if pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3] >= 0 else node[2]
    return -n - 1


def hull_at(x: Bsp, root: int, p) -> int:
    n = root
    while n >= 0:
        c = x.clipnodes[n]
        pl = x.planes[c[0]]
        n = c[1] if pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] - pl[3] >= 0 else c[2]
    return n


def extents(x: Bsp, f) -> tuple:
    t = x.texinfo[f[4]]
    s_vals, t_vals = [], []
    for i in range(f[3]):
        se = x.surfedges[f[2] + i][0]
        v = x.vertexes[x.edges[se][0] if se >= 0 else x.edges[-se][1]]
        s_vals.append(f32(v[0] * t[0] + v[1] * t[1] + v[2] * t[2] + t[3]))
        t_vals.append(f32(v[0] * t[4] + v[1] * t[5] + v[2] * t[6] + t[7]))
    return min(s_vals), max(s_vals), min(t_vals), max(t_vals)


def cells(e) -> list:
    return [math.floor(f32(e[0] / 16)), math.ceil(f32(e[1] / 16)), math.floor(f32(e[2] / 16)), math.ceil(f32(e[3] / 16))]


def tex(x: Bsp, f):
    mip = x.textures[x.texinfo[f[4]][8]]
    return mip[:16].split(b'\0')[0].lower() if mip else None


def face_vertexes(x: Bsp, f) -> list:
    out = []
    for i in range(f[3]):
        se = x.surfedges[f[2] + i][0]
        out.append(x.vertexes[x.edges[se][0] if se >= 0 else x.edges[-se][1]])
    return out


def submodels(b: Bsp, m: Bsp, info: PairInfo, rng, bad, points: int = 8) -> None:
    """Every brush model of b in m: moved by dz with its map, or, with an origin brush (merge.origin_models), in its
    own coordinates: its faces' vertexes and what is at points in its bounds in all four hulls."""
    fixed = origin_models(b)
    for k in range(1, len(b.models)):
        bm, mm = b.models[k], m.models[info.a_models - 1 + k]
        dz = 0.0 if k in fixed else info.dz
        if bm[15] != mm[15]:
            bad(f'B model {k}: {mm[15]} faces, {bm[15]} expected')
            continue
        for j in range(bm[15]):
            want = [[v[0], v[1], f32(v[2] + dz)] for v in face_vertexes(b, b.faces[bm[14] + j])]
            if face_vertexes(m, m.faces[mm[14] + j]) != want:
                bad(f'B model {k} face {j} geometry')
                break
        lo, hi = bm[0:3], bm[3:6]
        for _ in range(points):
            p = [rng.uniform(lo[i], hi[i]) for i in range(3)]
            q = [p[0], p[1], p[2] + dz]
            if b.leafs[leaf_at(b, bm[9], p)][0] != m.leafs[leaf_at(m, mm[9], q)][0]: bad(f'B model {k} contents at {p}')
            for h in (1, 2, 3):
                if 0 <= bm[9 + h] < len(b.clipnodes) and hull_at(b, bm[9 + h], p) != hull_at(m, mm[9 + h], q):
                    bad(f'B model {k} hull {h} at {p}')


def check_pair(a: Bsp, b: Bsp, m: Bsp, info: PairInfo, style_map: dict | None = None, points: int = 4000,
               seed: int = 1) -> list:
    """Problems found in m, the merge of a and b (b moved by info.dz, its face styles through style_map);
    an empty list when it is clean."""
    problems = []

    def bad(msg):
        problems.append(msg)
        if len(problems) > LIMIT:
            raise StopIteration

    style_map = style_map or {}
    Aw, Af, Bw, Av, Bv, An = info.Aw, info.Af, info.Bw, info.Av, info.Bv, info.An
    def faceA(f): return f if f < Aw else f + Bw
    def faceB(f): return Aw + f if f < Bw else Af + f
    def leafA(l): return l if l <= Av else l + Bv
    def leafB(l): return 0 if l == 0 else (Av + l if l <= Bv else An - 1 + l)
    try:
        for x, n in ((a, 'A'), (b, 'B'), (m, 'merged')):
            lint(x, n, bad)
        if problems:
            return problems
        ea, eb, em = hull_ranges(a, 'A', bad), hull_ranges(b, 'B', bad), hull_ranges(m, 'merged', bad)
        if em[1:] != ea[1:] + eb[1:]:
            bad('empty hulls of submodels differ from the originals')
        if problems:
            return problems

        rng = random.Random(seed)
        for src, leafmap, dz, tag in ((a, leafA, 0.0, 'A'), (b, leafB, info.dz, 'B')):
            lo, hi = src.models[0][0:3], src.models[0][3:6]
            pts = [[float(t) for t in ent_get(e, 'origin').split()] for e in src.ents
                   if ent_get(e, 'origin') and not ent_get(e, 'model')]
            pts += [[rng.uniform(lo[k], hi[k]) for k in range(3)] for _ in range(points)]
            for p in pts:
                q = [p[0], p[1], p[2] + dz]
                l0, l1 = leaf_at(src, src.models[0][9], p), leaf_at(m, m.models[0][9], q)
                if leafmap(l0) != l1: bad(f'{tag} leaf at {p}: {l0}->{leafmap(l0)} merged {l1}')
                if src.leafs[l0][0] != m.leafs[l1][0]: bad(f'{tag} contents at {p}')
                for h in (1, 2, 3):
                    if hull_at(src, src.models[0][9 + h], p) != hull_at(m, m.models[0][9 + h], q): bad(f'{tag} hull {h} at {p}')

        rowV = (Av + Bv + 7) >> 3
        for src, leafmap, count, tag in ((a, leafA, Av, 'A'), (b, leafB, Bv, 'B')):
            row = (count + 7) >> 3
            for l in range(1, count + 1):
                if src.leafs[l][1] < 0:
                    continue
                orig = {leafmap(k + 1) for k in bits_of(decompress_vis(src.vis, src.leafs[l][1], row), count)}
                ml = m.leafs[leafmap(l)]
                got = {k + 1 for k in bits_of(decompress_vis(m.vis, ml[1], rowV), Av + Bv)}
                if orig != got: bad(f'{tag} vis of leaf {l}')

        for src, facemap, styles, tag in ((a, faceA, {}, 'A'), (b, faceB, style_map, 'B')):
            for i, f in enumerate(src.faces):
                g = m.faces[facemap(i)]
                lm0, lm1 = cells(extents(src, f)), cells(extents(m, g))
                n = sum(1 for st in f[5:9] if st != 255)
                if [styles.get(s, s) for s in f[5:9]] != list(g[5:9]): bad(f'{tag} face {i} styles')
                if tex(src, f) != tex(m, g): bad(f'{tag} face {i} texture')
                if f[9] < 0:
                    continue
                size0 = (lm0[1] - lm0[0] + 1) * (lm0[3] - lm0[2] + 1) * 3 * n
                if lm0 != lm1:
                    box0, box1 = ((lm0[0], lm0[1]), (lm0[2], lm0[3])), ((lm1[0], lm1[1]), (lm1[2], lm1[3]))
                    size1 = (lm1[1] - lm1[0] + 1) * (lm1[3] - lm1[2] + 1) * 3 * n
                    if m.lighting[g[9]:g[9] + size1] != relay(src.lighting[f[9]:f[9] + size0], box0, box1, n):
                        bad(f'{tag} face {i} re-laid light data')
                elif src.lighting[f[9]:f[9] + size0] != m.lighting[g[9]:g[9] + size0]:
                    bad(f'{tag} face {i} light data')
        submodels(b, m, info, rng, bad)
    except StopIteration:
        pass
    return problems
