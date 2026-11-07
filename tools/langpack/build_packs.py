"""Build the Cry of Fear: Enhanced language packs from the co-op launcher's translations.

    python build_packs.py --game "<game>/cryoffear" --repo "<cof-enhanced repo>" [--text <dir>] [--langs a,b]

Reads the English game text (txtfiles/, notes/, inventoryitems/) from --game, the translations from
tools/langpack/text/<lang>/ and the packs already in <repo>/languages/, and writes:

  russian, schinese   whole packs: the game's text (subtitles, hints, conclusions, phone messages, notes,
                      item names), the DLL strings, the map text (strings/entity-strings.tsv), the menu, the
                      co-op messages, strings/glyphs.txt, manifest/README/LICENSE-NOTE/MANIFEST.tsv
  dutch ... swedish   the six packs for the game's own subtitle languages: item names, DLL strings and map
                      text added (their subtitles, hints and notes ship with the game); menu rows added
  polish              the co-op menu rows and the co-op messages added (the rest is the fan translation)

Text is written in each pack's code page (txt/, inventoryitems/: cp1251, UTF-8, cp1252; the TSVs are UTF-8
for every pack, the engine converts them). Nothing is ever byte-identical to a game file. Python 3.10+.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TODAY = "2026-11-06"
DRAFT = "machine-drafted by the Cry of Fear Co-op project " + TODAY + ", please review"

FULL = {
    "russian": {"display": "Русский", "codepage": 1251, "enc": "cp1251", "slot": 1, "conclusion": "#Заключение"},
    "schinese": {"display": "简体中文", "codepage": 65001, "enc": "utf-8", "slot": 1, "conclusion": "#尾声"},
}
SIX = {
    "dutch": ("Nederlands", 2), "french": ("Français", 3), "german": ("Deutsch", 4),
    "norwegian": ("Norsk", 5), "spanish": ("Español", 6), "swedish": ("Svenska", 7),
}
ALL = list(FULL) + list(SIX) + ["polish"]
ENC = {**{k: v["enc"] for k, v in FULL.items()}, **{k: "cp1252" for k in SIX}, "polish": "cp1250"}

CONCLUSIONS = ["conclusion1", "conclusion2", "conclusion3", "conclusion4", "coconc", "docconc"]


class BuildError(Exception):
    pass


# ---------------------------------------------------------------------------------------------- helpers

def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}


def encode(text: str, enc: str, where: str) -> bytes:
    try:
        return text.encode(enc)
    except UnicodeEncodeError as e:
        raise BuildError(f"{where}: {text[e.start:e.end]!r} is not in {enc}") from None


def trailing_crlf(raw: bytes) -> int:
    n = 0
    while raw.endswith(b"\r\n" * (n + 1)):
        n += 1
    return n


def tsv_cell(text: str) -> str:
    if "\t" in text or "\n" in text or "\r" in text:
        raise BuildError(f"a TSV cell may not hold a tab or a new line: {text!r}")
    return text


def leading_controls(english: str, translation: str) -> str:
    """A DLL string's leading control bytes (the HUD's colour codes, "\\x10Inventory full") stay."""
    m = re.match(r"[\x00-\x1f]+", english)
    if m and not translation.startswith(m.group(0)):
        return m.group(0) + translation
    return translation


FMT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?[sdiufxc]")


def check_formats(english: str, translation: str, where: str, allow_fewer: bool = False) -> None:
    a, b = FMT.findall(english), FMT.findall(translation)
    if a == b or (allow_fewer and b == a[: len(b)]):
        return
    raise BuildError(f"{where}: {english!r} -> {translation!r}: conversions {a} != {b}")


class Pack:
    """The files a build writes into one languages/<code>/ folder (relative path -> bytes)."""

    def __init__(self, code: str, root: Path):
        self.code = code
        self.root = root
        self.files: dict[str, bytes] = {}

    def put(self, rel: str, data: bytes) -> None:
        self.files[rel] = data

    def existing(self, rel: str) -> bytes | None:
        p = self.root / rel
        return p.read_bytes() if p.exists() else None


# ---------------------------------------------------------------------------------------------- game text

def build_subtitles(game: Path, text: Path, enc: str) -> bytes:
    en = (game / "txtfiles/subtitles.txt").read_bytes().decode("cp1252").split("\r\n")
    tr = (text / "txtfiles/subtitles.txt").read_text(encoding="utf-8").split("\n")
    if len(en) != len(tr):
        raise BuildError(f"subtitles: {len(tr)} lines, the game has {len(en)}")
    for i, (a, b) in enumerate(zip(en, tr)):
        if (a.strip() == "") != (b.strip() == "") or a.rstrip().endswith("*") != b.rstrip().endswith("*"):
            raise BuildError(f"subtitles line {i + 1}: {a!r} / {b!r} do not line up")
    return encode("\r\n".join(tr), enc, "subtitles.txt")


def build_hints(game: Path, text: Path, enc: str) -> bytes:
    src = (game / "txtfiles/hints.txt").read_bytes().decode("cp1252")
    table = load_json(text / "hints.json")
    missing = []

    def sub(m: re.Match) -> str:
        value = m.group(3)
        if value not in table:
            missing.append(value)
            return m.group(0)
        if value.count("*") != table[value].count("*") or '"' in table[value]:
            raise BuildError(f"hints: {value!r} -> {table[value]!r}: '*' or a quote")
        return m.group(1) + m.group(2) + table[value] + '"'

    out = re.sub(r'(\t(?:line\d+|nobutton)\t+)(")([^"]*)"', sub, src)
    if missing:
        raise BuildError(f"hints: no translation for {missing}")
    return encode(out, enc, "hints.txt")


def build_conclusion(game: Path, name: str, lines: list, title: str, enc: str) -> bytes:
    raw = (game / f"txtfiles/{name}.txt").read_bytes()
    en = raw.decode("cp1252").split("\r\n")
    # the title line and the blank lines after it, as the game has them
    head = 1
    while head < len(en) and en[head].strip() == "":
        head += 1
    body, para = [], []
    for item in lines:
        if item is None:
            if para:
                body.append(para)
            para = []
        else:
            para.append(item)
    if para:
        body.append(para)
    text = title + "\r\n" * head + "\r\n\r\n\r\n\r\n".join("\r\n\r\n".join(p) for p in body)
    return encode(text + "\r\n" * trailing_crlf(raw), enc, f"{name}.txt")


def build_lines(src_raw: bytes, path: Path, enc: str) -> bytes:
    lines = path.read_text(encoding="utf-8").rstrip("\n").split("\n")
    return encode("\r\n".join(lines) + "\r\n" * trailing_crlf(src_raw), enc, path.name)


def build_inventory(game: Path, items: dict, enc: str, pack: Pack) -> dict:
    """inventoryitems/**: nice_name translated, every other byte the game's. Returns English -> translation used."""
    used = {}
    pat = re.compile(rb'("nice_name"\r?\n\{\r?\n\tdata\t")([^"\r\n]*)(")')
    for f in sorted((game / "inventoryitems").rglob("*.txt")):
        raw = f.read_bytes()
        m = pat.search(raw)
        if not m:
            continue
        english = m.group(2).decode("cp1252")
        if english not in items or items[english] == english:
            continue
        new = raw[: m.start(2)] + encode(items[english], enc, f.name) + raw[m.end(2):]
        if new != raw:
            pack.put("inventoryitems/" + f.relative_to(game / "inventoryitems").as_posix(), new)
            used[english] = items[english]
    return used


# ---------------------------------------------------------------------------------------------- string tables

def read_tsv_rows(data: bytes | None) -> list[list[str]]:
    if not data:
        return []
    return [r.split("\t") for r in data.decode("utf-8").split("\n") if r.strip("\r")]


def polish_dll_rows(repo: Path) -> list[list[str]]:
    rows = read_tsv_rows((repo / "languages/polish/strings/dll-strings.tsv").read_bytes())
    return [[c.rstrip("\r") for c in r] for r in rows[1:] if len(r) >= 4]


def dll_rows(code: str, repo: Path, text: Path, existing: list[list[str]], items: dict) -> bytes:
    """english <TAB> translation <TAB> source_dll <TAB> file_offset <TAB> flagged"""
    dll = load_json(text / "dll.json")
    engine = load_json(text / "engine.json")
    out: list[list[str]] = []
    seen: set[str] = set()

    def add(english, translation, source, offset, flagged):
        if english in seen:
            return
        seen.add(english)
        out.append([tsv_cell(english), tsv_cell(translation), source, offset, flagged])

    # rows the pack already has stay first and as they are (Polish: the fan translation; the six: quick saves)
    for r in existing:
        if r[0] == "english":
            continue
        add(r[0], r[1], r[2] if len(r) > 2 else "engine", r[3] if len(r) > 3 else "-", r[4] if len(r) > 4 else "")
    if dll:
        for r in polish_dll_rows(repo):
            english = r[0]
            if english in seen:
                continue
            if english not in dll:
                if english in ("1.6a",) or r[2] in ("engine", "item"):
                    continue
                raise BuildError(f"{code}: no DLL translation for {english!r}")
            t = leading_controls(english, dll[english])
            check_formats(english.replace("\\n", "\n"), t.replace("\\n", "\n"), f"{code} dll")
            flag = "kept English: identifier-like name" if t == english else DRAFT
            add(english, t, r[2], r[3], flag)
        for english, t in dll.items():   # engine rows of dll.json (the quick saves) a pack does not have yet
            if english not in seen:
                check_formats(english, t, f"{code} dll")
                add(english, t, "engine", "-", "engine text (quick saves), " + DRAFT)
    for english, t in engine.items():
        check_formats(english, t, f"{code} engine", allow_fewer=english.startswith("You can leave without"))
        add(english, t, "engine", "-", "engine text (co-op launcher), " + DRAFT)
    # item names: a co-op message names the item in the host's words ("%s found the %s"); the engine translates
    # a captured piece that is itself a row
    for english, t in items.items():
        if english != t:
            add(english, t, "item", "-", "inventory item name, for the co-op messages that quote one")
    header = ["english", code, "source_dll", "file_offset", "flagged"]
    return ("\n".join("\t".join(r) for r in [header] + out) + "\n").encode("utf-8")


MENU_HEADER = """# Cry of Fear: Enhanced - {name} strings of the menu (MainUI with the Cry of Fear theme), and of the co-op pages.
# MACHINE-DRAFTED FOR REVIEW: written {date} by an AI worker of the Cry of Fear Co-op project, not by a
# translator; please review.
# Format: english <TAB> translation <TAB> where the English comes from; UTF-8; \\n is a new line;
# printf conversions (%s %d %i ...) must stay, in the same order. A row whose translation
# equals the English changes nothing (kept so the check sees it as covered).
english\ttranslation\twhere
"""


def menu_rows(code: str, name: str, text: Path, existing: bytes | None, where: dict) -> bytes:
    menu = load_json(text / "menu.json")
    if existing:
        lines = existing.decode("utf-8").rstrip("\n").split("\n")
        have = {r.split("\t")[0] for r in lines if r and not r.startswith("#") and "\t" in r}
        added = []
        for english, t in menu.items():
            if english in have:
                continue
            check_formats(english, t, f"{code} menu")
            added.append("\t".join([tsv_cell(english), tsv_cell(t), where.get(english, "co-op launcher")]))
        if not added:
            return existing
        marker = f"# Co-op launcher rows ({DRAFT})"
        return ("\n".join(lines + [marker] + added) + "\n").encode("utf-8")
    rows = []
    for english, t in menu.items():
        check_formats(english, t, f"{code} menu")
        rows.append("\t".join([tsv_cell(english), tsv_cell(t), where.get(english, "co-op launcher")]))
    return (MENU_HEADER.format(name=name, date=TODAY) + "\n".join(rows) + "\n").encode("utf-8")


def entity_rows(code: str, text: Path) -> bytes | None:
    ents = load_json(text / "entities.json")
    if not ents:
        return None
    rows = ["english\t" + code]
    for english, t in ents.items():
        if '"' in t:
            raise BuildError(f"{code} map text: a quote in {t!r} would end the entity value")
        if len(t.encode(ENC[code] if ENC[code] != "utf-8" else "utf-8")) >= 120:
            raise BuildError(f"{code} map text: {t!r} is too long for the HUD's message lines")
        rows.append(tsv_cell(english) + "\t" + tsv_cell(t))
    return ("\n".join(rows) + "\n").encode("utf-8")


# ---------------------------------------------------------------------------------------------- pack files

def manifest_full(code: str) -> bytes:
    f = FULL[code]
    lines = [
        f"language_code={code}", f"display_name={f['display']}", f"codepage={f['codepage']}",
        f"codepage_name={f['enc']}", f"subtitle_language={f['slot']}", "pack_version=1.0.0",
        "authors=Cry of Fear Co-op project (machine-drafted translation, waits for review)",
        "notes=Translated by the Cry of Fear Co-op project from Team Psykskallar's English text; the game's own art "
        "stays English unless the pack repaints it. See README.md.",
    ]
    return ("\n".join(lines) + "\n").encode("utf-8")


def bump_manifest(data: bytes, version: str, notes: str | None) -> bytes:
    out = []
    for line in data.decode("utf-8").rstrip("\n").split("\n"):
        if line.startswith("pack_version="):
            line = "pack_version=" + version
        elif notes is not None and line.startswith("notes="):
            line = "notes=" + notes
        out.append(line)
    return ("\n".join(out) + "\n").encode("utf-8")


def glyphs(pack: Pack, enc: str) -> bytes:
    chars = set()
    for rel, data in pack.files.items():
        if rel.endswith((".tsv", ".txt")) and not rel.endswith("MANIFEST.tsv"):
            utf8 = rel.endswith(".tsv") or rel.startswith("strings/") or rel == "manifest.txt" or enc == "utf-8"
            s = data.decode("utf-8" if utf8 else enc)
            chars.update(c for c in s if ord(c) > 0x7F and c != "﻿")
    ordered = "".join(sorted(chars))
    return ("\n".join(ordered[i:i + 64] for i in range(0, len(ordered), 64)) + "\n").encode("utf-8")


def manifest_tsv(pack: Pack, previous: bytes | None, keep_existing: bool) -> bytes:
    rows: dict[str, tuple[int, str]] = {}
    if keep_existing and previous:
        for r in previous.decode("utf-8").split("\n")[1:]:
            c = r.rstrip("\r").split("\t")
            if len(c) == 3:
                rows[c[0]] = (int(c[1]), c[2])
    for rel, data in pack.files.items():
        if rel in ("MANIFEST.tsv", "README.md"):
            continue
        rows[rel] = (len(data), sha256(data))
    if keep_existing:
        for rel in list(rows):
            if rel not in pack.files and not (pack.root / rel).exists():
                del rows[rel]
    lines = ["path\tbytes\tsha256"] + [f"{p}\t{b}\t{h}" for p, (b, h) in sorted(rows.items())]
    return ("\n".join(lines) + "\n").encode("utf-8")


README_FULL = """# Cry of Fear: Enhanced - {display} ({code})

A **whole language pack** for Cry of Fear: Enhanced, made by the Cry of Fear Co-op project ({date}).
Code page {cp}. **The translation is machine-drafted** (written by an AI worker of the project, not by a
translator) **and waits for review** by a native speaker.

| Path | Files | What |
|---|---|---|
| `manifest.txt` | 1 | display name, code page, client subtitle slot 1 (the pack's text replaces the English files) |
| `txt/` | {txt} | subtitles, hints, conclusions, phone messages and notes, in the game's `txtfiles/` and `notes/` layout |
| `inventoryitems/` | {inv} | item and weapon names (`nice_name`), every other byte the game's |
| `strings/dll-strings.tsv` | 1 | the strings compiled into client.dll / hl.dll ({dll} rows), the engine's quick save and co-op messages, and the item names the co-op messages quote |
| `strings/entity-strings.tsv` | 1 | map text ({ent} lines: door lines, game_text messages, chapter titles, save locations), applied by the engine to every map that uses the line, the co-op launcher's merged maps included |
| `strings/menu-strings.tsv` | 1 | the menu and the co-op pages ({menu} rows) |
| `strings/glyphs.txt` | 1 | every character beyond ASCII the pack uses: the engine and VGUI fonts prepare them in one go |
| `LICENSE-NOTE.md`, `README.md`, `MANIFEST.tsv` | 3 | terms, this file, file list with hashes |

The game's art (signs, posters, item cards, menu pictures) stays English unless a later version repaints it.
{cjk}Generated by `tools/langpack/build_packs.py` in the co-op launcher from `tools/langpack/text/{code}/`; corrections
go there. `MANIFEST.tsv` lists every file but itself and this README (path, bytes, SHA-256).
"""

CJK_NOTE = ("Chinese is drawn with **CoF Sans SC** (`gfx/fonts/CoFSansSC-*.otf`, a GB2312 subset of Noto Sans SC, "
            "SIL Open Font License 1.1): the engine, VGUI and menu fonts take every character their own fonts lack "
            "from it.\n")

LICENSE_FULL = """# Licence note: {display} language pack

**Most of this folder is not covered by the project's GNU GPL.** The repository's `LICENSE` applies to the
project's code and to the project's own strings; it does not apply to the game-derived text here.

## What this pack is

- `txt/`, `inventoryitems/`, `strings/entity-strings.tsv` and the rows of `strings/dll-strings.tsv` whose
  `source_dll` is `client` or `hl` are a **translation of Team Psykskallar's Cry of Fear text** (subtitles, notes,
  hints, conclusions, phone messages, item names, map text and the game DLLs' interface strings). The
  underlying game content belongs to Team Psykskallar.
- `strings/menu-strings.tsv`, the `engine` rows of `strings/dll-strings.tsv` and `strings/glyphs.txt` translate
  or list **the project's own text** (the menu, the co-op pages and messages) and are covered by the project's
  licence, GPL-3.0-or-later.
- The translation was **machine-drafted** on {date} by an AI worker of the Cry of Fear Co-op project, not by a
  translator, and waits for review by a native speaker.

## Terms for the game-derived text

- It is distributed free of charge, as part of Cry of Fear: Enhanced, **only for use with a legally owned copy of
  Cry of Fear**. It is useless without the game and does not replace it.
- It is game-derived data, not software, and it is not offered under the GPL or any other open licence.
- Do not sell it. If Team Psykskallar ask for any part of it to be removed, the project will remove it.

Keep this note and `README.md` with every copy of the pack.
"""

LICENSE_SIX_ADD = """
## Added by the Cry of Fear Co-op project ({date})

The pack now also holds **a translation of Team Psykskallar's Cry of Fear text** that the game does not ship in
this language: item names (`inventoryitems/`), map text (`strings/entity-strings.tsv`) and the game DLLs'
interface strings (the rows of `strings/dll-strings.tsv` whose `source_dll` is `client` or `hl`). That text is
**not** covered by the GPL: it is game-derived data, distributed free of charge as part of Cry of Fear: Enhanced
only for use with a legally owned copy of Cry of Fear; do not sell it, and the project will remove any part Team
Psykskallar ask it to. The co-op menu rows and the engine's co-op messages are the project's own text, under the
GPL like the rest of the menu. All of it was machine-drafted and waits for review.
"""

README_SIX_ADD = """
## Added by the Cry of Fear Co-op project ({date})

The game's own {name} text covers the subtitles, hints, conclusions, phone messages and notes; the rest is added
here, machine-drafted (an AI worker of the project, not a translator) and waiting for review:

* `inventoryitems/` - the item and weapon names ({inv} files; every other byte the game's), code page 1252.
* `strings/dll-strings.tsv` - now also the strings compiled into client.dll / hl.dll ({dll} rows in all), the
  engine's co-op messages and the item names those messages quote.
* `strings/entity-strings.tsv` - map text ({ent} lines: door lines, game_text messages, chapter titles, save
  locations), applied by the engine to every map that uses the line.
* `strings/menu-strings.tsv` - the co-op pages' rows added at the end.

The engine now mounts `inventoryitems/` for this pack. The game's art (signs, item cards) stays English.
"""


# ---------------------------------------------------------------------------------------------- languages

def menu_where(repo: Path) -> dict:
    where = {}
    for p in sorted((repo / "languages").glob("*/strings/menu-strings.tsv")):
        for r in p.read_text(encoding="utf-8").split("\n"):
            c = r.split("\t")
            if len(c) >= 3 and not r.startswith("#") and c[0] != "english":
                where.setdefault(c[0], c[2].rstrip("\r"))
    return where


def build_full(code: str, game: Path, repo: Path, text: Path, where: dict) -> Pack:
    f = FULL[code]
    enc = f["enc"]
    pack = Pack(code, repo / "languages" / code)
    base = f"txt/txtfiles/languages/{code}/"
    pack.put(base + "subtitles.txt", build_subtitles(game, text, enc))
    pack.put(base + "hints.txt", build_hints(game, text, enc))
    conc = load_json(text / "conclusions.json")
    for name in CONCLUSIONS:
        pack.put(base + f"{name}.txt", build_conclusion(game, name, conc[name], conc["_title"], enc))
    pack.put(base + "phone_messages.txt",
             build_lines((game / "txtfiles/phone_messages.txt").read_bytes(), text / "txtfiles/phone_messages.txt", enc))
    for note in sorted((text / "notes").glob("*.txt")):
        pack.put(f"txt/notes/languages/{code}/{note.name}",
                 build_lines((game / "notes" / note.name).read_bytes(), note, enc))
    build_inventory(game, load_json(text / "items.json"), enc, pack)
    pack.put("strings/dll-strings.tsv", dll_rows(code, repo, text, [], load_json(text / "items.json")))
    pack.put("strings/entity-strings.tsv", entity_rows(code, text))
    pack.put("strings/menu-strings.tsv", menu_rows(code, f["display"], text, None, where))
    pack.put("manifest.txt", manifest_full(code))
    pack.put("LICENSE-NOTE.md", LICENSE_FULL.format(display=f["display"], date=TODAY).encode("utf-8"))
    pack.put("strings/glyphs.txt", glyphs(pack, enc))
    counts = dict(
        txt=sum(1 for r in pack.files if r.startswith("txt/")),
        inv=sum(1 for r in pack.files if r.startswith("inventoryitems/")),
        dll=pack.files["strings/dll-strings.tsv"].count(b"\n") - 1,
        ent=pack.files["strings/entity-strings.tsv"].count(b"\n") - 1,
        menu=len(load_json(text / "menu.json")),
    )
    pack.put("README.md", README_FULL.format(display=f["display"], code=code, date=TODAY, cp=f["enc"],
                                             cjk=CJK_NOTE if code == "schinese" else "", **counts).encode("utf-8"))
    pack.put("MANIFEST.tsv", manifest_tsv(pack, None, False))
    return pack


def build_six(code: str, game: Path, repo: Path, text: Path, where: dict) -> Pack:
    name, _slot = SIX[code]
    pack = Pack(code, repo / "languages" / code)
    items = load_json(text / "items.json")
    build_inventory(game, items, "cp1252", pack)
    pack.put("strings/dll-strings.tsv",
             dll_rows(code, repo, text, read_tsv_rows(pack.existing("strings/dll-strings.tsv")), items))
    pack.put("strings/entity-strings.tsv", entity_rows(code, text))
    pack.put("strings/menu-strings.tsv",
             menu_rows(code, name, text, pack.existing("strings/menu-strings.tsv"), where))
    pack.put("manifest.txt", bump_manifest(
        pack.existing("manifest.txt"), "1.1.0",
        f"The game's own {code.capitalize()} text (txtfiles/languages/{code}, notes/languages/{code}, client subtitle slot) plus, "
        "from the Cry of Fear Co-op project (machine-drafted, please review): item names (inventoryitems/), the DLL "
        "strings, the map text (strings/entity-strings.tsv), the menu and the co-op messages."))
    lic = pack.existing("LICENSE-NOTE.md").decode("utf-8")
    if "Added by the Cry of Fear Co-op project" not in lic:
        lic = lic.rstrip("\n") + "\n" + LICENSE_SIX_ADD.format(date=TODAY)
    pack.put("LICENSE-NOTE.md", lic.encode("utf-8"))
    readme = pack.existing("README.md").decode("utf-8")
    if "Added by the Cry of Fear Co-op project" not in readme:
        readme = readme.rstrip("\n") + "\n" + README_SIX_ADD.format(
            date=TODAY, name=code.capitalize(), inv=sum(1 for r in pack.files if r.startswith("inventoryitems/")),
            dll=pack.files["strings/dll-strings.tsv"].count(b"\n") - 1,
            ent=pack.files["strings/entity-strings.tsv"].count(b"\n") - 1)
    pack.put("README.md", readme.encode("utf-8"))
    pack.put("MANIFEST.tsv", manifest_tsv(pack, pack.existing("MANIFEST.tsv"), True))
    return pack


def polish_items(game: Path, repo: Path) -> dict:
    """English -> Polish item names, from the fan translation's inventoryitems (for the co-op messages)."""
    out = {}
    pat = re.compile(rb'"nice_name"\r?\n\{\r?\n\tdata\t"([^"\r\n]*)"')
    for f in sorted((repo / "languages/polish/inventoryitems").rglob("*.txt")):
        src = game / "inventoryitems" / f.relative_to(repo / "languages/polish/inventoryitems")
        if not src.exists():
            continue
        a, b = pat.search(src.read_bytes()), pat.search(f.read_bytes())
        if a and b and a.group(1) != b.group(1):
            out.setdefault(a.group(1).decode("cp1252"), b.group(1).decode("cp1250"))
    return out


def build_polish(game: Path, repo: Path, text: Path, where: dict) -> Pack:
    pack = Pack("polish", repo / "languages/polish")
    items = polish_items(game, repo)
    pack.put("strings/dll-strings.tsv",
             dll_rows("polish", repo, text, read_tsv_rows(pack.existing("strings/dll-strings.tsv")), items))
    pack.put("strings/menu-strings.tsv",
             menu_rows("polish", "Polish", text, pack.existing("strings/menu-strings.tsv"), where))
    pack.put("manifest.txt", bump_manifest(pack.existing("manifest.txt"), "1.3.0", None))
    pack.put("MANIFEST.tsv", manifest_tsv(pack, pack.existing("MANIFEST.tsv"), True))
    return pack


def check_pack(pack: Pack, game: Path) -> None:
    for rel, data in pack.files.items():
        for sub in ("txt/txtfiles/languages/" + pack.code + "/", "txt/notes/languages/" + pack.code + "/", "inventoryitems/"):
            if rel.startswith(sub):
                kind = {"txt/t": "txtfiles/", "txt/n": "notes/", "inven": "inventoryitems/"}[sub[:5]]
                src = game / kind / rel[len(sub):]
                if src.exists() and src.read_bytes() == data:
                    raise BuildError(f"{pack.code}/{rel} is identical to the game's file")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", required=True, type=Path, help="the English game folder, <Cry of Fear>/cryoffear")
    ap.add_argument("--repo", required=True, type=Path, help="the cof-enhanced repository (languages/ is written)")
    ap.add_argument("--text", type=Path, default=HERE / "text")
    ap.add_argument("--langs", default=",".join(ALL))
    ap.add_argument("--dry-run", action="store_true", help="build and check, write nothing")
    args = ap.parse_args(argv)
    where = menu_where(args.repo)
    total = 0
    for code in [c for c in args.langs.split(",") if c]:
        text = args.text / code
        if code in FULL:
            pack = build_full(code, args.game, args.repo, text, where)
        elif code in SIX:
            pack = build_six(code, args.game, args.repo, text, where)
        elif code == "polish":
            pack = build_polish(args.game, args.repo, text, where)
        else:
            raise BuildError(f"unknown language {code}")
        check_pack(pack, args.game)
        changed = 0
        for rel, data in sorted(pack.files.items()):
            dst = pack.root / rel
            if dst.exists() and dst.read_bytes() == data:
                continue
            changed += 1
            if not args.dry_run:
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(data)
        total += changed
        print(f"{code:10} {len(pack.files):4} files built, {changed:4} {'would change' if args.dry_run else 'written'}")
    print(f"done: {total} file(s) {'would change' if args.dry_run else 'written'}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BuildError as e:
        print("error:", e, file=sys.stderr)
        sys.exit(1)
