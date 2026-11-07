"""The app's language (1.5): the window's own text in the language the player picks, the same one the game uses.

T(text, **fields) is the only call the window makes. text is the English, as written in the code; fields fill its
{name} placeholders after the lookup. A message another module made (an error, a setup step, the router's answer)
is passed through T() as it is: the tables also hold patterns, English with {name} placeholders, that match it and
put its pieces into the translation; a piece that is itself a row is translated too ("Your name" inside "Your name:
use up to 24 plain letters..."). Anything without a row is shown in English.

The tables are tools/app_lang/<code>.json, packed with the app (build_tester_package). The codes are the game's
language pack codes; "english" is the game without a pack. The choice lives in CoF-Story-Coop-language.txt beside
the copy pointer, and is kept in step with the game's own Options > Game > Language through the copy's config.cfg
(cof_language and the client's cof_subtitlelanguage): the window takes the game's choice when it shows the main screen,
and writes its own into config.cfg before the game starts.
"""
from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

# code, the name the list shows (in its own language), the game's cof_language, the client's subtitle slot
LANGUAGES = (
    ("english", "English", "", 1),
    ("german", "Deutsch", "german", 4),
    ("spanish", "Español", "spanish", 6),
    ("french", "Français", "french", 3),
    ("dutch", "Nederlands", "dutch", 2),
    ("norwegian", "Norsk", "norwegian", 5),
    ("polish", "Polski", "polish", 1),
    ("russian", "Русский", "russian", 1),
    ("swedish", "Svenska", "swedish", 7),
    ("schinese", "简体中文", "schinese", 1),
)
CODES = tuple(code for code, *_ in LANGUAGES)
NAMES = {code: name for code, name, *_ in LANGUAGES}
SUBTITLE_SLOT = {code: slot for code, _name, _pack, slot in LANGUAGES}
# the game's own six subtitle languages without a pack (the client's slot, cof_language ""), as older copies set them
SLOT_LANGUAGE = {slot: code for code, _name, pack, slot in LANGUAGES if slot > 1}

# Windows UI language (the primary language of GetUserDefaultUILanguage) -> code
WINDOWS_PRIMARY = {0x07: "german", 0x0A: "spanish", 0x0C: "french", 0x13: "dutch", 0x14: "norwegian",
                   0x15: "polish", 0x19: "russian", 0x1D: "swedish"}
# Chinese: Simplified for the PRC and Singapore; Traditional (Taiwan, Hong Kong, Macao) is closer to it than to English
WINDOWS_CHINESE = 0x04

CHOICE_FILE = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "CoF-Story-Coop-language.txt"

_current = "english"
_tables: dict[str, dict] = {}


def table_dir() -> Path:
    base = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
    return base / "tools" / "app_lang"


def _compile(table: dict) -> list:
    patterns = []
    for key, value in table.items():
        if "{" not in key:
            continue
        names, parts, pos = [], [], 0
        for m in re.finditer(r"\{(\w+)\}", key):
            parts.append(re.escape(key[pos:m.start()]))
            name = m.group(1)
            parts.append(f"(?P={name})" if name in names else f"(?P<{name}>.+?)")
            names.append(name)
            pos = m.end()
        parts.append(re.escape(key[pos:]))
        literal = len(re.sub(r"\{\w+\}", "", key))
        patterns.append((literal, re.compile("".join(parts) + r"\Z", re.S), value))
    patterns.sort(key=lambda p: -p[0])   # the most literal text first: "Step 1 of 6: X, Y" before "Step 1 of 6: X"
    return patterns


def load(code: str) -> dict:
    if code not in _tables:
        table = {}
        if code != "english":
            try:
                table = json.loads((table_dir() / f"{code}.json").read_text(encoding="utf-8"))
            except (OSError, ValueError):
                table = {}
        _tables[code] = {"exact": table, "patterns": _compile(table)}
    return _tables[code]


def current() -> str:
    return _current


def set_current(code: str) -> str:
    global _current
    _current = code if code in CODES else "english"
    return _current


def _piece(table: dict, text: str, depth: int = 0) -> str:
    """A piece a pattern captured or a field: translated when it is itself a row, exactly or (one level) by pattern."""
    if text in table["exact"]:
        return table["exact"][text]
    if depth == 0:
        for _literal, pattern, value in table["patterns"]:
            m = pattern.match(text)
            if m:
                try:
                    return value.format(**{k: _piece(table, v, 1) for k, v in m.groupdict().items()})
                except (KeyError, IndexError, ValueError):
                    break
    return text


def T(text: str, **fields) -> str:
    """text in the current language; fields fill its {placeholders} (after the lookup)."""
    if not isinstance(text, str):
        text = str(text)
    table = load(_current)
    if fields:
        template = table["exact"].get(text, text)
        try:
            return template.format(**{k: _piece(table, str(v), 1) for k, v in fields.items()})
        except (KeyError, IndexError, ValueError):
            return text.format(**fields)
    if text in table["exact"]:
        return table["exact"][text]
    for _literal, pattern, value in table["patterns"]:
        m = pattern.match(text)
        if m:
            try:
                return value.format(**{k: _piece(table, v) for k, v in m.groupdict().items()})
            except (KeyError, IndexError, ValueError):
                return text
    return text


def system_language() -> str:
    """The Windows display language, when the app has a table for it; English otherwise."""
    if os.name != "nt":
        return "english"
    try:
        import ctypes
        lang = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    except (AttributeError, OSError):
        return "english"
    primary = lang & 0x3FF
    if primary == WINDOWS_CHINESE:
        return "schinese"
    return WINDOWS_PRIMARY.get(primary, "english")


def saved_choice(path: Path = CHOICE_FILE) -> str | None:
    try:
        code = path.read_text(encoding="utf-8").strip()
    except OSError:
        return None
    return code if code in CODES else None


def save_choice(code: str, path: Path = CHOICE_FILE) -> None:
    try:
        path.write_text(code + "\n", encoding="utf-8")
    except OSError:
        pass   # the choice is a convenience; the game keeps its own


def init() -> str:
    """The language the window opens in: the player's choice, else the Windows display language."""
    return set_current(saved_choice() or system_language())


# ------------------------------------------------------------------------------------------- the game's config.cfg

_CVAR = re.compile(r'^\s*(cof_language|cof_subtitlelanguage)\s+"?([^"\r\n]*)"?\s*$', re.M)


def game_language(config_text: str) -> str | None:
    """The language a copy's config.cfg names: cof_language, or the game's own subtitle slot without a pack. None
    when it names neither (a copy that has not run the game since setup)."""
    found = dict((m.group(1), m.group(2).strip()) for m in _CVAR.finditer(config_text))
    if "cof_language" not in found:
        return None
    pack = found["cof_language"]
    if pack:
        return pack if pack in CODES else None
    try:
        slot = int(float(found.get("cof_subtitlelanguage", "1") or 1))
    except ValueError:
        slot = 1
    return SLOT_LANGUAGE.get(slot, "english")


def config_with_language(config_text: str, code: str) -> str:
    """config.cfg with the language set: cof_language and cof_subtitlelanguage replaced or added."""
    pack = dict((c, p) for c, _n, p, _s in LANGUAGES).get(code, "")
    values = {"cof_language": pack, "cof_subtitlelanguage": str(SUBTITLE_SLOT.get(code, 1))}
    newline = "\r\n" if "\r\n" in config_text else "\n"
    lines = config_text.splitlines()
    seen = set()
    for i, line in enumerate(lines):
        m = _CVAR.match(line)
        if m:
            lines[i] = f'{m.group(1)} "{values[m.group(1)]}"'
            seen.add(m.group(1))
    lines += [f'{name} "{value}"' for name, value in values.items() if name not in seen]
    return newline.join(lines) + newline


def read_game_language(root: Path) -> str | None:
    try:
        return game_language((Path(root) / "cryoffear/config.cfg").read_text(encoding="latin-1"))
    except OSError:
        return None


def write_game_language(root: Path, code: str) -> None:
    """Into the copy's config.cfg, which the game reads at start (and writes back, with the player's in-game choice,
    when it quits). Nothing when the copy has none."""
    path = Path(root) / "cryoffear/config.cfg"
    try:
        text = path.read_text(encoding="latin-1")
    except FileNotFoundError:
        return   # setup writes one; a copy without it gets the game's defaults, as before
    new = config_with_language(text, code)
    if new != text:
        temp = path.with_suffix(".cfg.tmp")
        temp.write_text(new, encoding="latin-1", newline="")
        os.replace(temp, path)
