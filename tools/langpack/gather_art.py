"""Gather the art a language pack repaints: for every image the Polish pack carries (overlay/, textures/, models/), the
game's English original and the Polish repaint, side by side, for tools/langpack/make_art.py.

    python gather_art.py --game "<Cry of Fear>/cryoffear" --repo "<cof-enhanced repo>" --out <folder>

<out>/en/... and <out>/pl/... mirror the pack's own paths: overlay/<game path> (the game's TGA), textures/<name>.tga
(the miptex of that name from the game's BSPs or WADs, as a 24-bit TGA), models/<model>/<texture>.bmp (the texture
embedded in the studio model, 8-bit BMP with its palette). Reads the game, writes only into <out>.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path


def find_ci(root: Path, rel: str) -> Path | None:
    """rel under root, matching each part's case the way Windows does."""
    here = root
    for part in rel.split("/"):
        if not here.is_dir():
            return None
        exact = here / part
        if exact.exists():
            here = exact
            continue
        low = part.lower()
        hit = next((c for c in here.iterdir() if c.name.lower() == low), None)
        if hit is None:
            return None
        here = hit
    return here


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", type=Path, required=True)
    ap.add_argument("--repo", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)
    sys.path.insert(0, str(args.repo / "scripts/polish"))
    import extract_sign_textures as est   # the Polish generators' readers (cof-enhanced scripts/polish)
    import build_model_textures as bmt

    pl = args.repo / "languages/polish"
    out_en, out_pl = args.out / "en", args.out / "pl"
    missing = []

    for f in sorted((pl / "overlay/cryoffear").rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(pl / "overlay/cryoffear").as_posix()
        src = find_ci(args.game, rel)
        if src is None:
            missing.append("overlay/" + rel)
            continue
        for base, data in ((out_en, src.read_bytes()), (out_pl, f.read_bytes())):
            dst = base / "overlay" / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)

    wanted = {f.stem.lower(): f for f in (pl / "textures").glob("*.tga")}
    found: dict[str, bytes] = {}
    sizes: dict[str, tuple] = {}
    sources = sorted((args.game / "maps").glob("*.bsp")) + sorted(args.game.glob("*.wad"))
    for src in sources:
        if len(found) == len(wanted):
            break
        try:
            if src.suffix.lower() == ".bsp":
                lump = est.read_texture_lump(src)
                table = est.embedded_miptex(lump)
            else:
                table = est.wad_miptex(src)
        except (ValueError, OSError, struct.error):
            continue
        for name in wanted:
            if name in found or name not in table:
                continue
            blob = table[name]
            px = blob[:-768]
            # the miptex's own size: the Polish TGA has it (its header), the indices are w*h
            hdr = est.read_tga_header(wanted[name])
            w, h = hdr["width"], hdr["height"]
            if w * h != len(px):
                continue
            found[name] = blob
            sizes[name] = (w, h)
    for name, f in wanted.items():
        if name not in found:
            missing.append("textures/" + f.name)
            continue
        w, h = sizes[name]
        blob = found[name]
        px, pal = blob[:-768], blob[-768:]
        dst = out_en / "textures" / f.name
        dst.parent.mkdir(parents=True, exist_ok=True)
        est.write_tga(dst, w, h, est.indices_to_bgr(px, pal), has_alpha=False)
        (out_pl / "textures").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, out_pl / "textures" / f.name)

    for f in sorted((pl / "models").rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(pl / "models")
        model_rel = "models/" + rel.parent.as_posix()
        tex = rel.name
        hit = None
        for cand in (model_rel + ".mdl", model_rel + "T.mdl", model_rel + "t.mdl"):
            mdl = find_ci(args.game, cand)
            if mdl is None:
                continue
            data = mdl.read_bytes()
            try:
                for t in bmt.texture_table(data):
                    tname = t["name"] if "." in t["name"] else t["name"] + ".bmp"
                    if tname.lower() == tex.lower():
                        n = t["w"] * t["h"]
                        hit = (t["w"], t["h"], data[t["idx"]:t["idx"] + n], data[t["idx"] + n:t["idx"] + n + 768], t["flags"])
                        break
            except (ValueError, struct.error):
                continue
            if hit:
                break
        if not hit:
            missing.append("models/" + rel.as_posix())
            continue
        w, h, px, pal, flags = hit
        dst = out_en / "models" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(bmt.bmp8(w, h, px, pal))
        (dst.parent / (dst.name + ".flags")).write_text(str(flags))
        pdst = out_pl / "models" / rel
        pdst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(f, pdst)

    print("missing", len(missing))
    for m in missing:
        print("  ", m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
