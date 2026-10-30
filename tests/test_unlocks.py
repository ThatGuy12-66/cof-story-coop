"""1.3.8: the unlocks of the player's own Cry of Fear carried into the co-op copy (tools/unlocks.py)."""
import ast
import os
import re
import tempfile
import unittest
from pathlib import Path

from tests.native import ENGINE_SERVER, ROOT
from tools import unlocks

LOCKED = ROOT / 'baseline/Cry of Fear/cryoffear/scriptsettings.dat'      # the user's game: nothing unlocked
UNLOCKED = ROOT / 'runtimes/enhanced/cryoffear/scriptsettings.dat'      # nearly everything unlocked


def settings(open_names=(), *, newline=b'\r\n', lines=101):
    """A file laid out as the game's: one 20-byte token a line, each unlock line locked unless named."""
    rows = [b'%020d' % n for n in range(1, lines + 1)]
    for name, _, line, locked, unlocked in unlocks.UNLOCKS:
        rows[line - 1] = unlocked if name in open_names else locked
    return newline.join(rows) + newline


class TableTests(unittest.TestCase):
    def test_every_unlock_is_one_character_on_a_line_of_its_own(self):
        lines = [line for _, _, line, _, _ in unlocks.UNLOCKS]
        self.assertEqual(len(lines), len(set(lines)))
        self.assertEqual(len(unlocks.UNLOCKS), 34)                     # the game's 32 bits and the two ending flags
        bits = [bit for _, bit, _, _, _ in unlocks.UNLOCKS if bit]
        self.assertEqual(sorted(bits), [1 << n for n in range(32)])
        for name, _, line, locked, unlocked in unlocks.UNLOCKS:
            self.assertTrue(3 <= line <= 83, name)
            self.assertEqual(len(locked), len(unlocked), name)
            self.assertEqual(sum(a != b for a, b in zip(locked, unlocked)), 1, name)
            self.assertNotIn(b'\n', locked + unlocked)

    def test_the_engine_reads_the_same_lines(self):
        # sv_cof_bonus.h's bonus room and wardrobe tables (the engine's rewrite) against this one
        header = (ENGINE_SERVER / 'sv_cof_bonus.h').read_text(encoding='latin-1')
        table = {line: (locked, unlocked) for _, _, line, locked, unlocked in unlocks.UNLOCKS}
        rows = re.findall(r'\{ (\d+), ((?:"[^"]*"\s*)+), ((?:"[^"]*"\s*)+)\}', header)
        self.assertEqual(len(rows), 23)

        def c_string(text):
            return b''.join(ast.literal_eval('b' + part).decode('unicode_escape').encode('latin-1')
                            for part in re.findall(r'"[^"]*"', text))
        for line, locked, unlocked in rows:
            self.assertEqual(table[int(line)], (c_string(locked), c_string(unlocked)), line)


class ReadTests(unittest.TestCase):
    def test_lines_are_counted_as_the_game_counts_them(self):
        data = b'a\r\n\n\nb\n\r\nc\0d\n'
        self.assertEqual([data[s:e] for s, e in unlocks.lines(data)], [b'a\r', b'b', b'\r', b'c'])
        self.assertEqual(unlocks.lines(b''), [])

    def test_a_line_reads_locked_first(self):
        name, _, line, locked, unlocked = unlocks.UNLOCKS[0]
        self.assertIs(unlocks.state(settings(), line, locked, unlocked), False)
        self.assertIs(unlocks.state(settings({name}), line, locked, unlocked), True)
        self.assertIsNone(unlocks.state(b'x\n' * 5, line, locked, unlocked))      # too short
        self.assertEqual(unlocks.unlocked_names(settings({'ending 2', 'the axe'})), ['ending 2', 'the axe'])

    @unittest.skipUnless(LOCKED.is_file() and UNLOCKED.is_file(), 'the dev PC\'s two game files')
    def test_the_real_files(self):
        locked, unlocked = LOCKED.read_bytes(), UNLOCKED.read_bytes()
        self.assertEqual(unlocks.unlocked_names(locked), [])
        names = {name for name, *_ in unlocks.UNLOCKS}
        self.assertEqual(set(unlocks.unlocked_names(unlocked)), names - {'unlock 0x40000000', 'ending 6'})
        merged, carried = unlocks.carry(unlocked, locked)
        self.assertEqual(merged, unlocked)                              # every other byte was the same already
        self.assertEqual(len(carried), 32)


class CarryTests(unittest.TestCase):
    def test_only_what_the_copy_lacks_is_added(self):
        game = settings({'ending 1', 'the FAMAS', 'Awesome suit'})
        copy = settings({'the FAMAS', 'the night vision'}, newline=b'\n')
        merged, carried = unlocks.carry(game, copy)
        self.assertEqual(carried, ['ending 1', 'Awesome suit'])
        self.assertEqual(set(unlocks.unlocked_names(merged)),
                         {'ending 1', 'the FAMAS', 'Awesome suit', 'the night vision'})   # nothing locked again
        self.assertEqual(len(merged), len(copy))
        self.assertEqual(merged.count(b'\n'), copy.count(b'\n'))
        self.assertNotIn(b'\r', merged)                                 # the copy's own line ends
        self.assertEqual(unlocks.carry(copy, merged), (merged, []))

    def test_a_line_the_copy_does_not_hold_is_left(self):
        game = settings({'ending 3'})
        copy = settings().replace(unlocks.UNLOCKS[2][3], b'something else......')
        self.assertEqual(unlocks.carry(game, copy), (copy, []))


class CopyTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.game, self.root = Path(temp.name) / 'Cry of Fear', Path(temp.name) / 'copy'
        for folder in (self.game, self.root):
            (folder / 'cryoffear').mkdir(parents=True)
        self.mine, self.target = self.game / unlocks.SETTINGS, self.root / unlocks.SETTINGS

    def test_each_start_brings_the_players_new_unlocks(self):
        self.mine.write_bytes(settings({'ending 4'}))
        self.target.write_bytes(settings({'the camera'}))
        self.assertEqual(unlocks.carry_into_copy(self.root, str(self.game)), ['ending 4'])
        self.assertEqual(unlocks.unlocked_names(self.target.read_bytes()), ['ending 4', 'the camera'])
        self.assertEqual(self.mine.read_bytes(), settings({'ending 4'}))                 # the player's game: read only
        self.assertFalse((self.root / 'cryoffear/scriptsettings.dat.cof-new').exists())
        before = self.target.stat().st_mtime_ns
        os.utime(self.target, ns=(before - 10 ** 9, before - 10 ** 9))
        self.assertEqual(unlocks.carry_into_copy(self.root, str(self.game)), [])
        self.assertEqual(self.target.stat().st_mtime_ns, before - 10 ** 9)               # nothing new: not written

    def test_a_copy_without_the_file_gets_the_players(self):
        self.mine.write_bytes(settings({'ending 5'}))
        self.assertEqual(unlocks.carry_into_copy(self.root, str(self.game)), ['ending 5'])
        self.assertEqual(self.target.read_bytes(), settings({'ending 5'}))
        self.target.unlink()
        self.mine.write_bytes(settings())
        self.assertEqual(unlocks.carry_into_copy(self.root, str(self.game)), [])
        self.assertFalse(self.target.exists())                         # nothing unlocked: no file needed

    def test_nothing_happens_without_the_players_game(self):
        self.target.write_bytes(settings())
        for source in ('', None, str(self.game), str(self.game.parent / 'moved')):
            self.assertEqual(unlocks.carry_into_copy(self.root, source), [])
        self.assertEqual(self.target.read_bytes(), settings())
        self.mine.write_bytes(b'x' * (unlocks.MOST + 1))
        self.assertEqual(unlocks.carry_into_copy(self.root, str(self.game)), [])


if __name__ == '__main__':
    unittest.main()
