"""The bonus room switch: scriptsettings.dat as the game reads it, and the engine hook."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native

LOCKED = ROOT / 'baseline/Cry of Fear/cryoffear/scriptsettings.dat'      # the user's game: nothing unlocked
UNLOCKED = ROOT / 'runtimes/enhanced/cryoffear/scriptsettings.dat'      # a copy with nearly everything unlocked


def c_path(path):
    return '"' + path.as_posix() + '"' if path.is_file() else '"<missing>"'


class BonusTests(unittest.TestCase):
    def test_rewrite_against_the_game_reader(self):
        output = run_native(self, 'cof_bonus_test.c', {
            'bonus_under_test.h': (ENGINE_SERVER / 'sv_cof_bonus.h').read_text(encoding='latin-1'),
            'bonus_fixture_paths.h': f'static const char *locked_path = {c_path(LOCKED)};\n'
                                     f'static const char *unlocked_path = {c_path(UNLOCKED)};\n',
        })
        self.assertIn('bonus room scenarios passed', output)
        if LOCKED.is_file():
            self.assertIn('locked file checked', output)
        if UNLOCKED.is_file():
            self.assertIn('unlocked file checked', output)

    def test_the_server_loads_files_through_the_switch(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        table = game[game.index('CRC32_Final,'):game.index('pfnGetGameDir,')]
        self.assertIn('SV_CoF_BonusLoadFile,', table)
        self.assertNotIn('COM_LoadFileForMe,', table)
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_BonusInit();', story)


if __name__ == '__main__':
    unittest.main()
