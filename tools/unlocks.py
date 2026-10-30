"""1.3.8: the unlocks of the player's own Cry of Fear, carried into the co-op copy each time the app starts the game.

Cry of Fear keeps its unlocks (the endings, the bonus room's weapons, the outfits, the developer commentary and the
rest) in cryoffear/scriptsettings.dat. The game splits the file with strtok( buf, "\\n" ), counts the non-empty pieces
from 1 and reads each unlock from a line of its own: the line holds a LOCKED text (locked) or an UNLOCKED one
(unlocked), which differ in one character (CBasePlayer::CheckingEndingOrUnlockables, hl.dll 0x100DD000;
player.cpp:15313-15935 in the decompilation). Its writer (SetEndingOrUnlockables) rewrites the file in the game folder
at an ending's credits, on the host. Research: evidence/tester-package-20261015/research/bonus-room-re.md and
evidence/decomp-survey-20261018/H-engine-menus-launcher.md finding 6 (the dev PC).

Setup copied the file once, so what a player unlocked later in their own game never reached co-op (a player on the
Discord, 2026-10-30: "the unlocks you already unlocked, would transfer to the simon's story with friends"). Now every
unlock the player's own game has and the copy lacks is written into the copy before the game starts. Nothing is ever
locked (the copy's own, from a co-op ending, stay) and the player's own game is only read. In a session the host's file
decides for everyone (the engine reads it: sv_cof_bonus.c, cof_bonus_room -1 when "Open the bonus room" is unticked).
"""
from __future__ import annotations

import os
from pathlib import Path

SETTINGS = "cryoffear/scriptsettings.dat"
MOST = 64 * 1024   # the file is about 2 KB; anything far larger is not the game's

# (name, the game's bit, line, LOCKED, UNLOCKED); the bytes are the game's own (0xA3 is the pound sign). Lines 47 and
# 48 are the two extra ending flags (CheckingEndingOrUnlockables' ending6 and ending7 arguments).
UNLOCKS = (
    ("ending 1", 0x1, 15, b"v9agjak+p)(,o3qb)aq4", b"v9agjak+p)(,03qb)aq4"),
    ("ending 2", 0x2, 7, b"ye&ole1^0(fcanb3v5t4", b"ye&0le1^0(fcanb3v5t4"),
    ("ending 3", 0x4, 45, b"1r6fd46ob7u2yj9q83yx", b"1r6fd46ob7u2yj9r83yx"),
    ("ending 4", 0x8, 22, b"2quwo6z3y4w\xa3ld)y!i35", b"2quvo6z3y4w\xa3ld)y!i35"),
    ("ending 5", 0x10, 3, b"z3,4lj)4b!9g7vgi4x5a", b"z3,4rj)4b!9g7vgi4x5a"),
    ("unlock 0x20", 0x20, 83, b"&fx!hd$bw^ayk%dfmcbg", b"&fx!hd$bw!ayk%dfmcbg"),
    ("the FAMAS", 0x40, 39, b"r1&,iji4cjtvf,44w919", b"r1&,iji4cjtvf.44w919"),
    ("David Leatherhoff suit", 0x80, 51, b"olt0\xa3s^\xa3rn(aki!9eqkh", b"olt0\xa3s^\xa3rn(aki!9eqkj"),
    ("ModDB hoodie", 0x100, 52, b"h*wchks\xa3n&%!\xa3bf%jtew", b"h*wchks\xa3n&%!\xa3bf%jtei"),
    ("Hello Kitty suit", 0x200, 53, b"%xn%i)jhxmfnbgjsr^if", b"\xa3xn%i)jhxmfnbgjsr^if"),
    ("Afraid of Monsters suit", 0x400, 54, b"fy^xix&&vkz)vcdn!wtv", b"fy^xix&&vkz)vcrn!wtv"),
    ("Camouflage hoodie", 0x800, 55, b"jwsyv**yb&(qtqlg)moj", b"jwtyv**yb&(qtqlg)moj"),
    ("Half-Life Creations hoodie", 0x1000, 56, b"henp(cm$at!n%\xa3f(c$jp", b"henp(cm$at!m%\xa3f(c$jp"),
    ("Black Metal suit", 0x2000, 57, b"yef!xkyzw*^vd*tj)zkc", b"yef!&kyzw*^vd*tj)zkc"),
    ("Team Psykskallar hoodie", 0x4000, 58, b"z(*mbwy(talqlrqf$lmw", b"z(*mbwy(tolqlrqf$lmw"),
    ("Awesome suit", 0x8000, 59, b"zgpwg\xa3vyqlzensuauwat", b"zwpwg\xa3vyqlzensuauwat"),
    ("Sick Simon suit", 0x10000, 60, b"dzpy)*iiy$oj!akyfbn%", b"dzpy)*iiy$0j!akyfbn%"),
    ("AoM Twitcher suit", 0x20000, 61, b"pe%%*\xa3\xa3rxa(%(bog)l^j", b"pe%%*\xa3\xa3rxr(%(bog)l^j"),
    ("Custom hoodie", 0x40000, 62, b"efq%)x)mk\xa3*odxsdocb)", b"efq%)u)mk\xa3*odxsdocb)"),
    ("the axe", 0x80000, 70, b"pzakmx&p^lbk^kw($(fo", b"pzakhx&p^lbk^kw($(fo"),
    ("the camera", 0x100000, 71, b"g*q$cceqhz&vcdp(au%!", b"g*q$cceqhz&vcdp(av%!"),
    ("the book laser", 0x200000, 72, b"aaa^$i)whenzprdh!vus", b"aba^$i)whenzprdh!vus"),
    ("the developer commentary", 0x400000, 73, b"mwu\xa3g$&ztmkfwej(bg^a", b"mwu\xa3g$&stmkfwej(bg^a"),
    ("the package key", 0x800000, 74, b"tzi&aoxfbkb^n&ikaxj%", b"tzi&a0xfbkb^n&ikaxj%"),
    ("the night vision", 0x1000000, 75, b"wph!uhhgrws)*erdb*rg", b"wph!uhmgrws)*erdb*rg"),
    ("unlock 0x2000000", 0x2000000, 76, b"&x&w%dvcr&$yoqs%c%ar", b"&x&v%dvcr&$yoqs%c%ar"),
    ("unlock 0x4000000", 0x4000000, 77, b"feyvvuxq^i)brfentm$&", b"foyvvuxq^i)brfentm$&"),
    ("unlock 0x8000000", 0x8000000, 78, b"*j%gb*^noyresqjihoez", b"*j%gb*^noyresqjihoes"),
    ("unlock 0x10000000", 0x10000000, 79, b"nz(xdxj*w(\xa3!&uh\xa3qn*j", b"nz(xdxj*w)\xa3!&uh\xa3qn*j"),
    ("unlock 0x20000000", 0x20000000, 80, b"lvdw&bhwjqsjm$pfwnp)", b"rvdw&bhwjqsjm$pfwnp)"),
    ("unlock 0x40000000", 0x40000000, 81, b"zqv%n(nqquvh)dlmtl!x", b"zqv%n(noquvh)dlmtl!x"),
    ("unlock 0x80000000", 0x80000000, 82, b")cfhc$vle$^hn\xa3padlvd", b")cfhc$vle$^hn\xa3podlvd"),
    ("ending 6", 0, 47, b"g_t\xa30j^zrqhli09jb5rc", b"c_t\xa30j^zrqhli09jb5rc"),
    ("ending 7", 0, 48, b"kk78kewvx9!%n218dkss", b"ck78kewvx9!%n218dkss"),
)


def lines(data: bytes) -> list[tuple[int, int]]:
    """Where each line the game counts starts and ends: the non-empty pieces strtok( buf, "\\n" ) gives (a lone
    "\\r" counts), up to the first zero byte (the end of the C string the game reads)."""
    end = data.find(b"\0")
    end = len(data) if end < 0 else end
    spans, start = [], 0
    while start <= end:
        stop = data.find(b"\n", start, end)
        stop = end if stop < 0 else stop
        if stop > start:
            spans.append((start, stop))
        start = stop + 1
    return spans


def state(data: bytes, line: int, locked: bytes, unlocked: bytes, spans=None) -> bool | None:
    """True unlocked, False locked, None neither (the line is missing or holds something else): the game's own
    order, LOCKED first."""
    spans = lines(data) if spans is None else spans
    if line > len(spans):
        return None
    start, stop = spans[line - 1]
    if data.find(locked, start, stop) >= 0:
        return False
    return True if data.find(unlocked, start, stop) >= 0 else None


def unlocked_names(data: bytes) -> list[str]:
    spans = lines(data)
    return [name for name, _, line, locked, unlocked in UNLOCKS if state(data, line, locked, unlocked, spans)]


def carry(game: bytes, copy: bytes) -> tuple[bytes, list[str]]:
    """The copy's file with every unlock of the player's own game it lacks (one character changed on that line each),
    and their names. Lines the copy has unlocked, and anything else in it, stay as they are."""
    out = bytearray(copy)
    game_spans, copy_spans = lines(game), lines(copy)
    carried = []
    for name, _, line, locked, unlocked in UNLOCKS:
        if state(game, line, locked, unlocked, game_spans) and state(copy, line, locked, unlocked, copy_spans) is False:
            start, stop = copy_spans[line - 1]
            at = copy.find(locked, start, stop)
            out[at:at + len(locked)] = unlocked
            carried.append(name)
    return bytes(out), carried


def carry_into_copy(root: Path, source_game: str | os.PathLike | None) -> list[str]:
    """Before the copy's game starts: the unlocks of the game the copy was made from (the installed record's
    source_game) written into the copy's scriptsettings.dat; their names. A copy with no file of its own gets the
    player's. Nothing happens when the player's game or its file is gone."""
    if not source_game:
        return []
    mine, target = Path(source_game) / SETTINGS, Path(root) / SETTINGS
    if not mine.is_file() or mine.stat().st_size > MOST:
        return []
    game = mine.read_bytes()
    if not target.exists():
        if not unlocked_names(game) or not target.parent.is_dir():
            return []
        merged, carried = game, unlocked_names(game)
    else:
        if target.is_symlink() or not target.is_file() or target.stat().st_size > MOST:
            return []
        merged, carried = carry(game, target.read_bytes())
        if not carried:
            return []
    temporary = target.with_name(target.name + ".cof-new")
    temporary.write_bytes(merged)
    os.replace(temporary, target)
    return carried
