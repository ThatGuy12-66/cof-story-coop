"""The first-player lookup during a client command, run natively offline."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class SenderTests(unittest.TestCase):
    def test_sender_is_the_first_player_during_its_command(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        start = game.index('// Cry of Fear story co-op: the player whose client command')
        end = game.index('/*\n=========\nSV_FindEntityByString', start)
        out = run_native(self, 'cof_sender_test.c', {
            'sv_cof_sender.h': (ENGINE_SERVER / 'sv_cof_sender.h').read_text(),
            'sender_under_test.c': game[start:end],
        })
        self.assertIn('sender scenarios passed', out)

    def test_scope_is_opened_and_closed_around_the_game_command(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        find = game[game.index('static edict_t *GAME_EXPORT SV_FindEntityByString('):]
        find = find[:find.index('\n}\n')]
        self.assertIn('CoF_SenderFind( pszField, pszValue, &e )', find)
        client = (ENGINE_SERVER / 'sv_client.c').read_text()
        call = client.index('svgame.dllFuncs.pfnClientCommand( cl->edict );')
        self.assertIn('SV_CoF_CommandSender( cl->state == cs_spawned', client[call - 200:call])
        self.assertIn('SV_CoF_CommandSender( NULL );', client[call:call + 120])
        # an engine error inside a command must not leave the scope open
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        shutdown = story[story.index('void SV_CoF_StoryShutdown('):]
        self.assertIn('SV_CoF_CommandSender( NULL );', shutdown)
        self.assertIn('SV_CoF_UsercmdSender( NULL );', shutdown)
        # a player's movement command: opened before the game's CmdStart, closed after CmdEnd
        pmove = (ENGINE_SERVER / 'sv_pmove.c').read_text()
        run = pmove[pmove.index('void SV_RunCmd('):]
        run = run[:run.index('\n}\n')]
        self.assertLess(run.index('SV_CoF_UsercmdSender( cl->edict );'), run.index('pfnCmdStart'))
        self.assertGreater(run.index('if( cof_scoped ) SV_CoF_UsercmdSender( NULL );'), run.index('pfnCmdEnd'))


if __name__ == '__main__':
    unittest.main()
