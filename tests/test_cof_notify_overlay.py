"""The console's notify lines over the game (engine/client/console.c Con_DrawNotify). Every co-op game has more than
one slot, and a game with more than one slot unlocks the console on its own (cl_parse.c, "allow console in
multiplayer games"), so every engine message (missing retail sounds, "Error: ...", "Setting up renderer...") flashed
in the top-left corner of each player's screen (the user, 2026-10-21). 1.2.3: the lines are drawn only for a player
who has the console themselves (con_enable, -console, -dev) or developer mode, as GoldSrc shows none outside
developer mode."""
import unittest

from tests.native import ROOT, assert_has
from tests.test_enhanced_sections import body_of

CLIENT = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/client'


class NotifyOverlayTests(unittest.TestCase):
    def test_the_lines_need_the_players_own_console_or_developer_mode(self):
        draw = body_of(CLIENT / 'console.c', 'static void Con_DrawNotify( void )')
        gate = 'if( host.allow_console && ( host.allow_console_init || host_developer.value > 0.0f ) && ' \
               '!Con_BackgroundMapActive( ) && cof_notify.value != 0.0f )'
        assert_has(self, gate, draw)
        # the chat input line is drawn whatever the gate says
        self.assertLess(draw.index(gate), draw.index('if( cls.key_dest == key_message )'))

    def test_a_multiplayer_game_unlocks_the_console_but_not_the_players_choice(self):
        parse = (CLIENT / 'parse/cl_parse.c').read_text(encoding='latin-1')
        rule = parse[parse.index('// allow console in multiplayer games'):]
        rule = rule[:rule.index('}')]
        assert_has(self, 'host.allow_console = true;', rule)
        self.assertNotIn('allow_console_init', rule)
        # what the player turns on (Options' console setting) sets both
        enable = body_of(CLIENT / 'console.c', 'static void Con_ApplyEnable( void )')
        assert_has(self, 'host.allow_console = host.allow_console_init = true;', enable)


if __name__ == '__main__':
    unittest.main()
