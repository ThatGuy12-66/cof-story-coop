"""Repaint the game's images that have English on them, for a language pack (1.5): the English taken out of the
image and the translation drawn in its place, in the image's own style.

    python make_art.py --art <gather_art.py output> --lang russian --out <folder> [--only substr] [--sheet out.png]

--art holds en/ (the game's originals) and pl/ (the Polish pack's repaints of the same files), as
tools/langpack/gather_art.py writes them. The output goes to <out>/overlay/cryoffear/..., <out>/textures/... and
<out>/models/..., the layout of a pack; build_packs.py --art copies it into languages/<code>/.

cards.json: the item and weapon cards (white caption with a soft drop shadow over the item's picture). The caption
is found by drawing the English lines and matching them against the image; under the shadow model the game's art
uses (black, offset 3,4, blur 2.2, 70%) the shadow is divided out and the letters inpainted, which gives the picture
back; the translation is drawn with the same shadow.

labels.json: everything else, block by block (written by labels_spec.py). A block names the box the English
occupies, how to take it out ("polish": per pixel, whichever of the English and the Polish image is not lettering
there, since their letters fall in different places; "inpaint", "inpaint_ink", "patch", "fill", "color", "clear"),
the translation for each language and a style (STYLES: font per language, colour, outline, shadow, the menus'
streak, emboss, carving, blood drips, pencil...).

Fonts: Liberation Sans/Serif/Mono, Noto Sans/Serif CJK SC, and from Google Fonts Rubik, Oswald, Neucha, Caveat,
Amatic SC, LongCang, Ma Shan Zheng (all SIL OFL); COF_ART_FONTS names the folders to look in.

Output formats are the game's: interface TGAs uncompressed 32-bit, bottom-up (VGUI1's loader reads the alpha of an
RLE TGA inverted against an uncompressed one; the Polish pack's RLE files store it inverted for that reason);
world textures 24-bit TGA; studio model textures 8-bit BMP in the model's own palette, index 255 kept for masked
textures, so the engine's CoF_Lang_StudioTextures can put them into the model. Needs Pillow, numpy and OpenCV.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
# name -> (file, collection index). Liberation Sans has Arial's metrics (the cards are Arial, squeezed horizontally);
# Noto Sans CJK SC is the Chinese the packs' own font (CoF Sans SC) is cut from.
FONTS = {
    "latin": ("LiberationSans-Regular.ttf", 0, None),
    "cjk": ("NotoSansCJK-Regular.ttc", 2, None),
    "sans": ("LiberationSans-Regular.ttf", 0, None),
    "sans_bold": ("LiberationSans-Bold.ttf", 0, None),
    "mono": ("LiberationMono-Regular.ttf", 0, None),
    "mono_bold": ("LiberationMono-Bold.ttf", 0, None),
    "serif": ("LiberationSerif-Regular.ttf", 0, None),
    "serif_bold": ("LiberationSerif-Bold.ttf", 0, None),
    "black": ("Rubik[wght].ttf", 0, 900),             # Arial Black's weight, with Cyrillic
    "condensed": ("Oswald[wght].ttf", 0, 700),
    "condensed_light": ("Oswald[wght].ttf", 0, 300),
    "hand": ("Neucha.ttf", 0, None),
    "hand2": ("Caveat[wght].ttf", 0, 500),
    "paint": ("RubikWetPaint-Regular.ttf", 0, None),
    "scrawl": ("AmaticSC-Bold.ttf", 0, None),         # thin hand-drawn capitals, with Cyrillic
    "cjk_light": ("NotoSansCJK-Light.ttc", 2, None),
    "cjk_medium": ("NotoSansCJK-Medium.ttc", 2, None),
    "cjk_bold": ("NotoSansCJK-Bold.ttc", 2, None),
    "cjk_black": ("NotoSansCJK-Black.ttc", 2, None),
    "cjk_serif": ("NotoSerifCJK-Regular.ttc", 2, None),
    "cjk_serif_bold": ("NotoSerifCJK-Bold.ttc", 2, None),
    "cjk_hand": ("LongCang-Regular.ttf", 0, None),
    "cjk_brush": ("MaShanZheng-Regular.ttf", 0, None),
    "cjk_scrawl": ("ZhiMangXing-Regular.ttf", 0, None),
}
FONT_DIRS = [Path(p) for p in os.environ.get("COF_ART_FONTS", "").split(os.pathsep) if p] + [
    Path("/usr/share/fonts/truetype/liberation"), Path("/usr/share/fonts/opentype/noto"), Path("C:/Windows/Fonts")]
LATIN, CJK = "latin", "cjk"

SHADOW = dict(dx=3, dy=4, sigma=2.2, strength=0.7)
CARD_SIZE, CARD_XSCALE = 18, 0.82
NEAR = 3


def font_path(name: str) -> Path:
    file = FONTS[name][0]
    for base in FONT_DIRS:
        if (base / file).exists():
            return base / file
        hits = [d / file for d in base.iterdir() if (d / file).exists()] if base.is_dir() else []
        if hits:
            return hits[0]
    raise FileNotFoundError(f"{file}: put it in a folder named by COF_ART_FONTS")


_fonts: dict = {}


def font(name: str, size: int) -> ImageFont.FreeTypeFont:
    key = (name, size)
    if key not in _fonts:
        f = ImageFont.truetype(str(font_path(name)), size, index=FONTS[name][1])
        if FONTS[name][2]:
            f.set_variation_by_axes([FONTS[name][2]])
        _fonts[key] = f
    return _fonts[key]


# ------------------------------------------------------------------------------------------------------- drawing text

def draw_line(text: str, name: str, size: int, xscale: float = 1.0) -> np.ndarray:
    """One line's coverage (0..1), on a canvas as tall as the font's ascent+descent, so lines share a baseline."""
    f = font(name, size)
    asc, desc = f.getmetrics()
    w = max(1, int(f.getlength(text)) + 4)
    im = Image.new("L", (w, asc + desc + 2), 0)
    ImageDraw.Draw(im).text((2, 1), text, font=f, fill=255)
    if abs(xscale - 1.0) > 1e-3:
        im = im.resize((max(1, round(w * xscale)), im.height), Image.LANCZOS)
    a = np.asarray(im).astype(np.float64) / 255
    cols = np.where(a.max(0) > 0.02)[0]
    return a[:, cols[0]:cols[-1] + 1] if len(cols) else a[:, :1]


def ink_bbox(a: np.ndarray, cut: float = 0.05):
    ys, xs = np.where(a > cut)
    if not len(ys):
        return 0, 0, 0, 0
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


# Russian prepositions and conjunctions that typesetting keeps on the line of the word after them
CLINGING = {"во", "ко", "со", "об", "от", "до", "на", "по", "за", "из", "не", "ни", "но", "да", "при", "для", "без",
            "над", "под", "про", "что", "как", "его", "её", "их", "ею", "им"}
CJK_RE = re.compile(r"[\u2e80-\u9fff\u3000-\u303f\uff00-\uffef\u2018-\u201f\u2026]")
NO_START = set("，。、：；！？）」』”’…》〉,.:;!?)")
NO_END = set("（「『“‘《〈(")


def tokens(text: str) -> list[str]:
    """Break points: between words (a one-letter word stays with the next, as Russian typesetting wants), and
    between any two Chinese characters (not before closing or after opening punctuation)."""
    out, cur = [], ""
    for ch in text:
        if CJK_RE.match(ch):
            if cur:
                out.append(cur)
                cur = ""
            if out and (ch in NO_START or out[-1][-1:] in NO_END):
                out[-1] += ch
            else:
                out.append(ch)
        elif ch == " ":
            cur += " "
            out.append(cur)
            cur = ""
        else:
            if not cur and out and out[-1][-1:] in NO_END:
                out[-1] += ch
            else:
                cur += ch
    if cur:
        out.append(cur)
    joined = []
    for t in out:
        prev = joined[-1].strip().lower() if joined else ""
        if joined and joined[-1].endswith(" ") and (len(prev) == 1 and prev.isalpha() or prev in CLINGING):
            joined[-1] += t
        else:
            joined.append(t)
    return joined


CLAUSE_END = set("，。；：！？…")


def clauses(toks: list[str], width: float, measure) -> list[str]:
    """Chinese: whole clauses (up to their punctuation) as single tokens, where one fits on a line."""
    out, cur = [], []
    for t in toks:
        cur.append(t)
        if t[-1:] in CLAUSE_END:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    merged = []
    for c in out:
        if measure("".join(c).strip()) <= width:
            merged.append("".join(c))
        else:
            merged += c
    return merged


def wrap(text: str, width: float, measure) -> list[str]:
    toks = tokens(text)
    if CJK_RE.search(text):
        toks = clauses(toks, width, measure)

    def greedy(limit):
        lines, cur = [], ""
        for t in toks:
            trial = cur + t
            if cur and measure(trial.rstrip()) > limit:
                lines.append(cur.rstrip())
                cur = t.lstrip()
            else:
                cur = trial
        if cur.strip():
            lines.append(cur.rstrip())
        return lines

    lines = greedy(width)
    if len(lines) > 1:   # balance: the narrowest width that still needs no more lines
        lo, hi = width * 0.4, width
        for _ in range(14):
            mid = (lo + hi) / 2
            if len(greedy(mid)) <= len(lines):
                hi = mid
            else:
                lo = mid
        lines = greedy(hi)
    return lines


# ------------------------------------------------------------------------------------------------------- the cards

def whiteness(a: np.ndarray) -> np.ndarray:
    """How much of a pixel the white caption covers: exact over the transparent background (white over nothing),
    estimated from lightness over the picture."""
    mn, al = a[..., :3].min(-1), a[..., 3]
    return np.where(al < 0.6, mn * al, np.clip((mn - 0.55) / 0.4, 0, 1))


def shadow_of(G: np.ndarray) -> np.ndarray:
    dx, dy = SHADOW["dx"], SHADOW["dy"]
    sh = np.zeros_like(G)
    sh[dy:, dx:] = G[:G.shape[0] - dy, :G.shape[1] - dx]
    return np.clip(SHADOW["strength"] * cv2.GaussianBlur(sh, (0, 0), SHADOW["sigma"]), 0, 0.95)


def locate(a: np.ndarray, lines: list[str]):
    """Where each English line is drawn: (score, x, y, size, xscale, coverage). Every size and squeeze is tried for
    every line; the lines are then placed together, one under the other (a title can also match inside the line
    under it)."""
    W = whiteness(a.astype(np.float64) / 255).astype(np.float32)
    H = W.shape[0]
    per_line = []
    for text in lines:
        best = [None] * H                      # by top row: the best match starting there
        for size in range(14, 23):
            for xs in np.arange(0.70, 1.001, 0.02):
                L = draw_line(text, LATIN, size, float(xs)).astype(np.float32)
                if L.shape[1] >= W.shape[1] or L.shape[0] >= H:
                    continue
                r = cv2.matchTemplate(W, L, cv2.TM_CCORR_NORMED)
                xs_best = r.argmax(1)
                for y in range(r.shape[0]):
                    score = float(r[y, xs_best[y]])
                    if best[y] is None or score > best[y][0]:
                        best[y] = (score, int(xs_best[y]), y, size, float(xs), L)
        per_line.append(best)
    # lines in order, each 12..34 rows under the one before
    n = len(lines)
    total = [[-1e9] * H for _ in range(n)]
    back = [[-1] * H for _ in range(n)]
    for y in range(H):
        if per_line[0][y]:
            total[0][y] = per_line[0][y][0]
    for i in range(1, n):
        for y in range(H):
            if not per_line[i][y]:
                continue
            for py in range(max(0, y - 34), y - 11):
                if total[i - 1][py] > -1e8 and total[i - 1][py] + per_line[i][y][0] > total[i][y]:
                    total[i][y] = total[i - 1][py] + per_line[i][y][0]
                    back[i][y] = py
    y = int(np.argmax(total[n - 1]))
    found = [None] * n
    for i in range(n - 1, -1, -1):
        found[i] = per_line[i][y]
        y = back[i][y]
    return found


def remove_caption(rgba8: np.ndarray, found) -> np.ndarray:
    a = rgba8.astype(np.float64) / 255
    H, W_ = a.shape[:2]
    near = np.zeros((H, W_), np.uint8)
    for _score, x, y, _s, _xs, L in found:
        h, w = L.shape
        near[y:y + h, x:x + w] |= (L[:H - y, :W_ - x] > 0.03).astype(np.uint8)
    near = cv2.dilate(near, np.ones((NEAR, NEAR), np.uint8)).astype(bool)
    G = np.where(near, whiteness(a), 0.0)
    S = shadow_of(G)
    A, C = a[..., 3], a[..., :3] * a[..., 3:]
    g = np.minimum(G, 0.95)
    A1 = (A - g) / (1 - g)
    C1 = (C - g[..., None]) / (1 - g[..., None])
    pa = np.clip((A1 - S) / (1 - S), 0, 1)
    pc = np.minimum(np.clip(C1 / (1 - S[..., None]), 0, 1), pa[..., None])
    zone = cv2.dilate((G > 0.02).astype(np.uint8), np.ones((15, 15), np.uint8)).astype(bool)   # letters + shadow
    pa = np.where(zone & (pa < 0.12), 0, pa)          # what is left of the shadow over nothing
    pc = np.minimum(pc, pa[..., None])
    hole = cv2.dilate((G > 0.3).astype(np.uint8), np.ones((3, 3), np.uint8)) * 255
    prem = np.concatenate([pc, pa[..., None]], -1)
    out = np.empty_like(prem)
    for c in range(4):
        out[..., c] = cv2.inpaint((prem[..., c] * 255).round().astype(np.uint8), hole, 2, cv2.INPAINT_TELEA) / 255
    return out   # premultiplied


def compose(prem: np.ndarray, G: np.ndarray) -> np.ndarray:
    """White text with the art's drop shadow over a premultiplied picture; straight RGBA uint8 out."""
    S = shadow_of(G)
    A1 = S + prem[..., 3] * (1 - S)
    C1 = prem[..., :3] * (1 - S[..., None])
    A = G + A1 * (1 - G)
    C = G[..., None] + C1 * (1 - G[..., None])
    rgb = np.where(A[..., None] > 1e-4, C / np.maximum(A[..., None], 1e-4), 1.0)
    rgb = np.where(A[..., None] < 1 / 255, 1.0, rgb)   # the game's own transparent pixels are white
    return (np.clip(np.concatenate([rgb, A[..., None]], -1), 0, 1) * 255).round().astype(np.uint8)


def layout(lines_in: list[str], lang: str, size: int, xscale: float, maxw: int):
    name = CJK if lang == "schinese" else LATIN
    if lang == "schinese":
        xscale = 1.0
    f = font(name, size)
    measure = lambda s: f.getlength(s) * xscale
    lines = []
    for i, part in enumerate(lines_in):
        lines += wrap(part, maxw, measure)
    return name, lines, xscale


def card(rgba8: np.ndarray, english: list[str], lines_in: list[str], lang: str):
    found = locate(rgba8, english)
    prem = remove_caption(rgba8, found)
    H, W_ = rgba8.shape[:2]
    size, xs = CARD_SIZE, CARD_XSCALE
    top = min(f[2] + ink_bbox(f[5])[1] for f in found)
    bottom = max(f[2] + ink_bbox(f[5])[3] for f in found)
    centre = (top + bottom) / 2
    if lang == "schinese":
        size = max(13, round(size * 0.86))
    maxw = W_ - 8
    while True:
        name, lines, x_used = layout(lines_in, lang, size, xs, maxw)
        f = font(name, size)
        pitch = round(size * (1.32 if lang == "schinese" else 1.19))
        asc, desc = f.getmetrics()
        height = pitch * (len(lines) - 1) + asc + desc
        if height <= H - 2 or size <= 12:
            break
        if lang != "schinese" and xs > 0.76:
            xs -= 0.02
        else:
            size -= 1
    G = np.zeros((H, W_))
    for i, s in enumerate(lines):
        L = draw_line(s, name, size, x_used)
        h, w = L.shape
        x0 = int(round((W_ - w) / 2))
        y0 = i * pitch
        G_line = np.zeros((H + 200, W_))
        G_line[y0 + 100:y0 + 100 + h, max(0, x0):max(0, x0) + min(w, W_)] = L[:, :min(w, W_)]
        G = np.maximum(G, G_line[100:100 + H]) if False else G
        G_big = G_line
        if i == 0:
            acc = G_big
        else:
            acc = np.maximum(acc, G_big)
    x0_, y0_, x1_, y1_ = ink_bbox(acc)
    shift = int(round(centre - (y0_ + y1_) / 2))
    shift = max(-y0_, min(shift, H + 200 - y1_))
    full = np.zeros((H + 400, W_))
    full[200 + shift:200 + shift + acc.shape[0]] = acc
    G = full[200:200 + H]
    lo, hi = np.where(G.max(1) > 0.05)[0][[0, -1]] if G.max() > 0.05 else (0, 0)
    if lo < 1:
        G = np.roll(G, 1 - lo, 0)
    elif hi > H - 6:
        G = np.roll(G, H - 6 - hi, 0)
    return compose(prem, G), dict(found=[(round(f[0], 2), f[1], f[2], f[3], round(f[4], 2)) for f in found],
                                  size=size, xscale=round(x_used, 2), lines=lines)



# ------------------------------------------------------------------------------------------------------- labels

# How a label looks. Each block of labels.json names one and can override any part of it.
#   font: per language (russian / schinese)       color: the letters
#   outline: (width, colour)                      shadow: (dx, dy, blur, colour, opacity)
#   streak: (half length, strength) - the menus' horizontal motion smear, in the letters' colour
#   glow: (blur, colour, strength)                emboss: (dx, dy, light colour, light opacity, dark opacity)
#   rough: 0..1 - patchy ink, for paint and pencil  min_xscale: how far Latin/Cyrillic may be squeezed to fit
STYLES = {
    "plain": dict(font={"russian": "sans", "schinese": "cjk"}, color=(255, 255, 255)),
    "plain_bold": dict(font={"russian": "sans_bold", "schinese": "cjk_bold"}, color=(255, 255, 255)),
    "streak": dict(font={"russian": "sans", "schinese": "cjk"}, color=(255, 255, 255), streak=(36, 0.8)),
    "dialog": dict(font={"russian": "black", "schinese": "cjk_black"}, color=(255, 255, 255),
                   outline=(3, (0, 0, 0)), shadow=(2, 2, 1.0, (0, 0, 0), 0.5)),
    "engraved": dict(font={"russian": "serif_bold", "schinese": "cjk_serif_bold"}, color=(30, 30, 30),
                     emboss=(1, 1, (190, 190, 190), 0.55, 0.0), opacity=0.85),
    "stencil": dict(font={"russian": "condensed", "schinese": "cjk_black"}, color=(255, 255, 255), rough=0.35),
    "pencil": dict(font={"russian": "hand", "schinese": "cjk_hand"}, color=(40, 40, 40), rough=0.3),
    "sketch": dict(font={"russian": "hand", "schinese": "cjk_hand"}, color=(55, 57, 62), rough=0.4, sketch=(3, 1.5),
                   hollow=1),
    "blood": dict(font={"russian": "scrawl", "schinese": "cjk_hand"}, color=(95, 12, 12), opacity=0.92, rough=0.25,
                  drips=(0.22, 0.45), wobble=(2.5, 3.0)),
    "carved": dict(font={"russian": "black", "schinese": "cjk_black"}, color=(0, 0, 0), multiply=0.4,
                   emboss=(1, 1, (150, 140, 120), 0.45, 0.35)),
}
CJK_SCALE = 1.12      # a Chinese character's height for a Latin capital's


def is_cjk(name: str) -> bool:
    return name.startswith("cjk")


def ref_extent(name: str, size: int):
    """(top, bottom) of a capital H (a Chinese character for Chinese fonts), from the baseline."""
    b = font(name, size).getbbox("国" if is_cjk(name) else "H", anchor="ls")
    return b[1], b[3]


def text_layer(lines, name, size, xscale, stroke=0, pitch=None, align="center"):
    """Coverage of the lines, centred on each other; returns (coverage, baseline of line 1, pitch)."""
    f = font(name, size)
    asc, desc = f.getmetrics()
    pitch = pitch or round(size * (1.3 if is_cjk(name) else 1.18))
    pad = 4 + stroke
    widths = [f.getlength(t) for t in lines]
    W = int(max(widths) + 2 * pad + 2)
    H = int(asc + desc + pitch * (len(lines) - 1) + 2 * pad)
    im = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(im)
    for i, t in enumerate(lines):
        x = pad + ((max(widths) - widths[i]) / 2 if align == "center" else 0)
        d.text((x, pad + asc + i * pitch), t, font=f, fill=255, anchor="ls",
               stroke_width=stroke, stroke_fill=255)
    if abs(xscale - 1) > 1e-3:
        im = im.resize((max(1, round(W * xscale)), H), Image.LANCZOS)
    return np.asarray(im).astype(np.float64) / 255, pad + asc, pitch


def measure_ink(rgba8: np.ndarray, box, color, tol):
    x, y, w, h = box
    sub = rgba8[y:y + h, x:x + w].astype(np.float64)
    m = (sub[..., 3] > 160) & (np.sqrt(((sub[..., :3] - np.array(color)) ** 2).sum(-1)) < tol)
    ys, xs = np.nonzero(m)
    if len(ys) < 5:
        return None
    return x + xs.min(), y + ys.min(), x + xs.max() + 1, y + ys.max() + 1


def premul(rgba8):
    a = rgba8.astype(np.float64) / 255
    return np.concatenate([a[..., :3] * a[..., 3:], a[..., 3:]], -1)


def unpremul(p):
    al = p[..., 3:]
    rgb = np.where(al > 1e-4, p[..., :3] / np.maximum(al, 1e-4), 1.0)
    return (np.clip(np.concatenate([rgb, al], -1), 0, 1) * 255).round().astype(np.uint8)


def over(dst, color, cov, opacity=1.0):
    c = np.clip(cov * opacity, 0, 1)[..., None]
    src = np.concatenate([np.array(color, float)[None, None, :] / 255 * np.ones_like(c), np.ones_like(c)], -1)
    return src * c + dst * (1 - c)


def remove_block(p, rgba8, block, polish=None):
    x, y, w, h = block["box"]
    how = block.get("remove", "clear")
    kind = how if isinstance(how, str) else how[0]
    if kind == "polish":
        # The Polish pack's repaint of the same image: where its letters are not, it has the clean background the
        # English letters cover. Per pixel, of the two the one nearer a smooth estimate of the background is the
        # background; where both are letters the gap is inpainted.
        if polish is None:
            kind = "inpaint"
        else:
            q = premul(polish)
            sl = (slice(y, y + h), slice(x, x + w))
            e, o = p[sl], q[sl]
            D = (np.abs(e - o).max(-1) * 255 > block.get("thr", 12)).astype(np.uint8)
            # where both have letters in the same place they agree, so also take what stands out from the surface
            # near the differences
            med = np.stack([cv2.medianBlur((e[..., c] * 255).round().astype(np.uint8), 15) for c in range(4)], -1) / 255
            loud = (np.abs(e - med).max(-1) * 255 > block.get("loud", 45)).astype(np.uint8)
            D = cv2.dilate(D | (loud & cv2.dilate(D, np.ones((9, 9), np.uint8))), np.ones((3, 3), np.uint8))
            B = np.empty_like(e)
            for c in range(4):
                B[..., c] = cv2.inpaint((e[..., c] * 255).round().astype(np.uint8), D * 255, 6, cv2.INPAINT_TELEA) / 255
            de, do = np.abs(e - B).max(-1), np.abs(o - B).max(-1)
            english = D.astype(bool) & (de > do) & (de * 255 > block.get("ink_thr", 25))
            ys, xs = np.nonzero(english)
            if len(ys) > 8:   # where the English letters are: their size and place, for the translation
                block["_ink"] = (x + np.percentile(xs, 0.5), y + np.percentile(ys, 0.5),
                                 x + np.percentile(xs, 99.5) + 1, y + np.percentile(ys, 99.5) + 1)
            pick = np.where((do < de)[..., None], o, e)
            out = np.where(D[..., None].astype(bool), pick, e)
            hole = (D.astype(bool) & (np.minimum(de, do) * 255 > block.get("hole", 40))).astype(np.uint8)
            hole = cv2.dilate(hole, np.ones((3, 3), np.uint8)) * 255
            for c in range(4):
                out[..., c] = cv2.inpaint((out[..., c] * 255).round().astype(np.uint8), hole, 3,
                                          cv2.INPAINT_TELEA) / 255
            p[sl] = out
            return p
    if kind == "clear":
        p[y:y + h, x:x + w] = 0
    elif kind == "inpaint_ink":   # only what stands out from the surface (text on a smooth screen or plate)
        sub = (p[y:y + h, x:x + w] * 255).round().astype(np.uint8)
        k = block.get("kernel", 15)
        med = np.stack([cv2.medianBlur(sub[..., c], k) for c in range(4)], -1).astype(np.int16)
        ink = (np.abs(sub.astype(np.int16) - med).max(-1) > block.get("thr", 18)).astype(np.uint8)
        m = np.zeros(p.shape[:2], np.uint8)
        m[y:y + h, x:x + w] = cv2.dilate(ink, np.ones((3, 3), np.uint8)) * 255
        for c in range(4):
            p[..., c] = cv2.inpaint((p[..., c] * 255).round().astype(np.uint8), m, block.get("radius", 4),
                                    cv2.INPAINT_TELEA) / 255
    elif kind == "inpaint":
        m = np.zeros(p.shape[:2], np.uint8)
        m[y:y + h, x:x + w] = 255
        for c in range(4):
            p[..., c] = cv2.inpaint((p[..., c] * 255).round().astype(np.uint8), m, block.get("radius", 4),
                                    cv2.INPAINT_TELEA) / 255
    elif kind == "patch":      # copy a clean stretch of the same background from (dx, dy) away, feathered in
        dx, dy = how[1], how[2]
        src = p[y + dy:y + dy + h, x + dx:x + dx + w].copy()
        f = block.get("feather", 3)
        wy = np.minimum(np.arange(h) + 1, np.arange(h)[::-1] + 1) / (f + 1)
        wx = np.minimum(np.arange(w) + 1, np.arange(w)[::-1] + 1) / (f + 1)
        wgt = np.clip(np.minimum(wy[:, None], wx[None, :]), 0, 1)[..., None]
        p[y:y + h, x:x + w] = src * wgt + p[y:y + h, x:x + w] * (1 - wgt)
    elif kind == "fill":       # a flat plate: each row from the colours just left and right of the box
        l = p[y:y + h, max(0, x - 2):x].mean(1)
        r = p[y:y + h, x + w:x + w + 2].mean(1)
        t = np.linspace(0, 1, w)[None, :, None]
        p[y:y + h, x:x + w] = l[:, None, :] * (1 - t) + r[:, None, :] * t
    elif kind == "color":
        p[y:y + h, x:x + w] = np.array(list(np.array(how[1]) / 255 * how[2]) + [how[2]])
    return p


def draw_block(p, rgba8, block, lang, rng):
    style = dict(STYLES[block.get("style", "plain")])
    style.update({k: v for k, v in block.items() if k in ("font", "color", "outline", "shadow", "streak", "glow",
                                                          "emboss", "rough", "opacity", "min_xscale", "multiply",
                                                          "sketch", "hollow", "drips", "wobble")})
    text = block[lang]
    lines = text if isinstance(text, list) else text.split("\n")
    fonts = style["font"]
    name = fonts.get(lang) if isinstance(fonts, dict) else fonts
    H, W = p.shape[:2]
    x, y, w, h = block["box"]
    area = block.get("area", block["box"])
    # the English letters' size and place, unless the block says
    ink = block.get("_ink")
    if ink is None and ("ink" in block or "size" not in block or "center" not in block):
        ink_color = block.get("ink", style["color"])
        ink = measure_ink(rgba8, block["box"], ink_color, block.get("tol", 90))
    if "size" in block:
        cap = block["size"]
    else:
        n_en = block.get("en_lines", 1)
        cap = (ink[3] - ink[1]) / (n_en * 1.45 - 0.45) if ink else h * 0.6
    target = cap * (CJK_SCALE * block.get("scale_cjk", 1.0) if is_cjk(name) else 1.0) * block.get("scale", 1.0)
    size = 6
    while size < 400:
        t, b = ref_extent(name, size + 1)
        if b - t > target:
            break
        size += 1
    if "center" in block:
        cx, cy = block["center"]
    elif ink:
        cx, cy = (ink[0] + ink[2]) / 2, (ink[1] + ink[3]) / 2
    else:
        cx, cy = x + w / 2, y + h / 2
    if block.get("align") == "left":
        left = block.get("left", ink[0] if ink else x)
    maxw = block.get("maxw", area[2])
    maxh = block.get("maxh", 10 ** 6)
    xscale = 1.0 if is_cjk(name) else block.get("xscale", 1.0)
    min_xs = style.get("min_xscale", 0.8)
    stroke = style["outline"][0] if style.get("outline") else 0
    size0 = size
    text_align = "left" if block.get("align") == "left" else "center"
    source_lines = lines
    while True:
        pitch_px = round(block["pitch"] * size / size0) if block.get("pitch") else None
        if block.get("wrap"):
            f = font(name, size)
            lines = []
            for part in source_lines:
                lines += wrap(part, maxw / xscale, f.getlength) if part else [""]
        cov, base, pitch = text_layer(lines, name, size, xscale, pitch=pitch_px, align=text_align)
        ink_cols = np.where(cov.max(0) > 0.03)[0]
        width = (ink_cols[-1] - ink_cols[0] + 1) if len(ink_cols) else 0
        height = pitch * len(lines)
        if (width <= maxw and height <= maxh) or size <= 7:
            break
        if width > maxw and not block.get("wrap") and not is_cjk(name) and xscale > min_xs + 1e-6:
            xscale = max(min_xs, xscale * maxw / width - 0.005)
        else:
            size -= 1
    t, b = ref_extent(name, size)
    # where the layer goes: the first line's reference box centred so that all lines centre on cy
    mid_first = base + (t + b) / 2
    if "first_center" in block:            # top-anchored: the first line's middle at this row
        top_of_layer = block["first_center"] - mid_first
    else:
        top_of_layer = cy - (len(lines) - 1) * pitch / 2 - mid_first
    if block.get("align") == "left":
        left_of_layer = left - ink_cols[0]
    elif block.get("align") == "right":
        left_of_layer = block["right"] - ink_cols[-1] - 1
    else:
        left_of_layer = cx - (ink_cols[0] + ink_cols[-1] + 1) / 2

    def place(layer):
        out = np.zeros((H, W))
        oy, ox = int(round(top_of_layer)), int(round(left_of_layer))
        ys, xs = max(0, oy), max(0, ox)
        ye, xe = min(H, oy + layer.shape[0]), min(W, ox + layer.shape[1])
        if ye > ys and xe > xs:
            out[ys:ye, xs:xe] = layer[ys - oy:ye - oy, xs - ox:xe - ox]
        return out

    G = place(cov)
    if block.get("mirror"):       # text seen from behind (the texture is mapped mirrored)
        Mm = np.float32([[-1, 0, 2 * cx], [0, 1, 0]])
        G = cv2.warpAffine(G, Mm, (W, H), flags=cv2.INTER_LINEAR)
    if block.get("rotate"):
        M = cv2.getRotationMatrix2D((cx, cy), block["rotate"], 1.0)
        G = cv2.warpAffine(G, M, (W, H), flags=cv2.INTER_CUBIC)
    if style.get("wobble"):       # a hand's unevenness: a slow random bend and a slight tilt
        amp, tilt = style["wobble"]
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        fx = cv2.GaussianBlur(rng.standard_normal((H, W)).astype(np.float32), (0, 0), 14)
        fy = cv2.GaussianBlur(rng.standard_normal((H, W)).astype(np.float32), (0, 0), 14)
        fx *= amp / max(1e-6, np.abs(fx).max())
        fy *= amp / max(1e-6, np.abs(fy).max())
        ang = np.deg2rad(rng.uniform(-tilt, tilt))
        cxp, cyp = W / 2, H / 2
        rx = cxp + (xx - cxp) * np.cos(ang) - (yy - cyp) * np.sin(ang) + fx
        ry = cyp + (xx - cxp) * np.sin(ang) + (yy - cyp) * np.cos(ang) + fy
        G = cv2.remap(G.astype(np.float32), rx.astype(np.float32), ry.astype(np.float32),
                      cv2.INTER_LINEAR).astype(np.float64)
    if style.get("hollow"):       # only the letters' edges, as a pencil outlines big letters
        k = style["hollow"]
        G = np.clip(G - cv2.erode(G, np.ones((2 * k + 1, 2 * k + 1), np.uint8)), 0, 1)
    if style.get("sketch"):       # pencil: the word gone over a few times, each a little off
        n, jit = style["sketch"]
        acc = np.zeros_like(G)
        for _ in range(n):
            dx, dy = rng.uniform(-jit, jit, 2)
            M2 = np.float32([[1, rng.uniform(-0.01, 0.01), dx], [rng.uniform(-0.01, 0.01), 1, dy]])
            acc = 1 - (1 - acc) * (1 - 0.6 * cv2.warpAffine(G, M2, (W, H), flags=cv2.INTER_LINEAR))
        G = acc
    if style.get("drips"):        # paint or blood running down from the letters' lower edges
        prob, frac = style["drips"]
        n = max(2, int(round(target * frac)))
        edge = (G > 0.5) & (np.roll(G, -1, 0) < 0.25)
        ys, xs = np.nonzero(edge)
        D = np.zeros_like(G)
        last = -10
        for yy, xx in sorted(zip(ys, xs), key=lambda t: t[1]):
            if xx - last < 3 or rng.random() > prob:
                continue
            last = xx
            length = int(rng.integers(max(2, n // 3), n + 1))
            wdt = 1 if target < 30 else 2
            for k in range(length):
                yk = yy + 1 + k
                if yk >= H:
                    break
                D[yk, xx:xx + wdt] = max(0.35, 1 - k / (length * 1.6))
            yk = min(H - 1, yy + 1 + length)
            D[max(0, yk - 1):yk + 1, max(0, xx - 1):xx + wdt + 1] = np.maximum(D[max(0, yk - 1):yk + 1, max(0, xx - 1):xx + wdt + 1], 0.8)
        G = np.maximum(G, cv2.GaussianBlur(D, (0, 0), 0.5))
    if style.get("rough"):
        noise = cv2.GaussianBlur(rng.random((H, W)), (0, 0), 0.8)
        noise = (noise - noise.min()) / max(1e-6, noise.max() - noise.min())
        G = G * np.clip(1 - style["rough"] + style["rough"] * 2 * noise, 0, 1)
    O = None
    if stroke:
        so, _, _ = text_layer(lines, name, size, xscale, stroke=stroke, pitch=pitch_px, align=text_align)
        # the stroked layer is padded by the stroke more than the plain one
        pad_diff = stroke
        so = so[pad_diff:, round(pad_diff * xscale):] if so.shape[0] > pad_diff else so
        O = np.maximum(place(so), G)
        if block.get("rotate"):
            O = cv2.warpAffine(O, M, (W, H), flags=cv2.INTER_CUBIC)
    body = O if O is not None else G
    opacity = style.get("opacity", 1.0)
    if style.get("shadow"):
        dx, dy, blur, col, op = style["shadow"]
        S = np.roll(np.roll(body, dy, 0), dx, 1)
        if blur:
            S = cv2.GaussianBlur(S, (0, 0), blur)
        p = over(p, col, S, op * opacity)
    if style.get("glow"):
        blur, col, k = style["glow"]
        p = over(p, col, np.clip(cv2.GaussianBlur(body, (0, 0), blur) * k, 0, 1), opacity)
    if style.get("streak"):
        L, k = style["streak"]
        k1 = cv2.blur(G, (2 * L + 1, 1))
        k2 = cv2.blur(G, (L + 1, 1))
        p = over(p, style["color"], np.clip((k1 * 0.5 + k2 * 0.5) * k, 0, 0.45), opacity)
    if O is not None:
        p = over(p, style["outline"][1], O, opacity)
    if style.get("emboss"):
        dx, dy, light, lop, dop = style["emboss"]
        Lh = np.roll(np.roll(G, dy, 0), dx, 1)
        p = over(p, light, np.clip(Lh - G, 0, 1), lop * opacity)
        if dop:
            Dk = np.roll(np.roll(G, -dy, 0), -dx, 1)
            p = over(p, (0, 0, 0), np.clip(Dk - G, 0, 1), dop * opacity)
    if style.get("multiply") is not None:     # letters cut into the surface: darker of the same material
        f = style["multiply"]
        k = (1 - G * (1 - f) * opacity)[..., None]
        p = np.concatenate([p[..., :3] * k, p[..., 3:]], -1)
    else:
        p = over(p, style["color"], G, opacity)
    return p, dict(size=size, xscale=round(xscale, 2), cap=round(cap, 1))


def label(rgba8: np.ndarray, spec: dict, lang: str, rng, polish=None):
    p = premul(rgba8)
    for block in spec["blocks"]:
        p = remove_block(p, rgba8, block, polish)
    info = []
    for block in spec["blocks"]:
        if block.get(lang) is None:
            continue
        p, i = draw_block(p, rgba8, block, lang, rng)
        info.append(i)
    return unpremul(p), info


# ------------------------------------------------------------------------------------------------------- files

def read_tga(path: Path) -> np.ndarray:
    """As the game shows it: an RLE TGA's alpha is read inverted (see the top)."""
    a = np.asarray(Image.open(path).convert("RGBA")).copy()
    head = path.read_bytes()[:18]
    if head[2] == 10 and head[16] == 32:
        a[..., 3] = 255 - a[..., 3]
    return a


def write_tga(path: Path, rgba8: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba8, "RGBA").save(path, "TGA", compression=None, orientation=-1)


def read_source(path: Path):
    """The game's image as RGBA the way the game shows it, and how to write it back."""
    if path.suffix.lower() == ".bmp":
        im = Image.open(path)
        idx = np.asarray(im).copy()
        pal = np.array(im.getpalette()[:768], np.uint8).reshape(-1, 3)
        flags = path.with_name(path.name + ".flags")
        masked = flags.exists() and int(flags.read_text()) & 0x40
        rgba = np.concatenate([pal[idx], np.full(idx.shape + (1,), 255, np.uint8)], -1)
        if masked:
            rgba[idx == 255, 3] = 0
        return rgba, dict(kind="bmp", palette=pal, indices=idx, masked=bool(masked))
    return read_tga(path), dict(kind="tga", alpha=Image.open(path).mode == "RGBA")


def write_out(path: Path, rgba8: np.ndarray, info: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if info["kind"] == "bmp":     # a studio model's texture: its own palette, so the model needs no other change
        pal = info["palette"].astype(np.float64)
        cand = pal[:255] if info["masked"] else pal
        rgb = rgba8[..., :3].astype(np.float64)
        idx = info["indices"].copy()
        changed = np.abs(rgb - pal[idx]).max(-1) > 3
        if info["masked"]:
            changed |= (rgba8[..., 3] < 128) != (idx == 255)
        px = rgb[changed]
        if len(px):
            near = np.empty(len(px), np.int64)
            for i in range(0, len(px), 4096):
                d = ((px[i:i + 4096, None, :] - cand[None, :, :]) ** 2).sum(-1)
                near[i:i + 4096] = d.argmin(1)
            idx[changed] = near
        if info["masked"]:
            idx[rgba8[..., 3] < 128] = 255
        out = Image.fromarray(idx.astype(np.uint8), "P")
        out.putpalette(info["palette"].reshape(-1).tolist())
        out.save(path, "BMP")
    elif info["alpha"]:
        write_tga(path, rgba8)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(rgba8[..., :3], "RGB").save(path, "TGA", compression=None, orientation=-1)


def out_path(root: Path, rel: str) -> Path:
    return root / "overlay/cryoffear" / rel[len("overlay/"):] if rel.startswith("overlay/") else root / rel


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--art", type=Path, required=True)
    ap.add_argument("--lang", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--sheet", type=Path)
    ap.add_argument("--log", type=Path)
    args = ap.parse_args(argv)
    rng = np.random.default_rng(7)
    review, log = [], {}
    cards = json.loads((HERE / "cards.json").read_text(encoding="utf-8"))
    for rel, texts in cards.items():
        if args.only and args.only not in rel:
            continue
        src = read_tga(args.art / "en/overlay" / rel)
        out, info = card(src, texts["english"], texts[args.lang], args.lang)
        write_tga(args.out / "overlay/cryoffear" / rel, out)
        log[rel] = info
        review.append((rel, src, out))
    labels = json.loads((HERE / "labels.json").read_text(encoding="utf-8"))
    for rel, spec in labels.items():
        if args.only and args.only not in rel:
            continue
        src, info = read_source(args.art / "en" / rel)
        pl = args.art / "pl" / rel
        polish = read_source(pl)[0] if pl.exists() else None
        out, log[rel] = label(src, spec, args.lang, rng, polish)
        write_out(out_path(args.out, rel), out, info)
        shown, _ = read_source(out_path(args.out, rel)) if info["kind"] == "bmp" else (out, None)
        review.append((rel, src, shown))
    if args.log:
        args.log.write_text(json.dumps(log, ensure_ascii=False, indent=1, default=str), encoding="utf-8")
    if args.sheet and review:
        sheet(review, args.sheet)
    print(f"{args.lang}: {len(review)} image(s)")
    return 0


def sheet(review, path: Path, scale: float = 2.0, width: int = 1200) -> None:
    tiles = []
    for rel, a, b in review:
        scale = min(2.0, (width / 2 - 4) / a.shape[1])
        pair = []
        for im in (a, b):
            al = im[..., 3:] / 255.0
            rgb = (im[..., :3] * al + np.array([60, 60, 80]) * (1 - al)).astype(np.uint8)
            p = Image.fromarray(rgb)
            pair.append(p.resize((int(p.width * scale), int(p.height * scale)), Image.LANCZOS))
        t = Image.new("RGB", (pair[0].width * 2 + 8, pair[0].height + 14), (255, 255, 255))
        t.paste(pair[0], (0, 14))
        t.paste(pair[1], (pair[0].width + 8, 14))
        ImageDraw.Draw(t).text((2, 1), rel[-70:], fill=(0, 0, 0))
        tiles.append(t)
    W_ = max(t.width for t in tiles)
    out = Image.new("RGB", (W_, sum(t.height + 4 for t in tiles)), (200, 200, 200))
    y = 0
    for t in tiles:
        out.paste(t, (0, y))
        y += t.height + 4
    out.save(path)


if __name__ == "__main__":
    sys.exit(main())
