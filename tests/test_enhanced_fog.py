"""Fog in story co-op (engine/server/sv_cof_fog.h, sv_cof_fog.c; docs/research/2026-10-18-cof-decompilation-survey.md
section 3): env_fog sends SetFog to every player only when it changes, and the client clears its fog on every ResetHUD
and InitHUD, so a respawn or a late join lost the fog. In merged maps one section's fog reached every section."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class FogTests(unittest.TestCase):
    def test_which_fog(self):
        header = strip_includes((SERVER / 'sv_cof_fog.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_fog_harness.c', {'fog_under_test.h': '#include <string.h>\ntypedef unsigned char byte;\n' + header})
        self.assertEqual(program_output(out), [
            'payload: 1 [40 50 60 5 100 900]',
            'fits 0 own: 1',
            "fits 2 other's: 0",
            'fits 1 its fade out: 1',
            'fits 1 off: 1',
            'fits 0 off: 0',
            'fits all off: 1',
            'wanted 0: 1 [40 50 60 0 100 900]',
            'wanted 1: 0 [0 0 0 0 0 0]',
            'wanted all: 1 [40 50 60 0 100 900]',
            'wanted 0 fading out: 0 [0 0 0 0 0 0]',
            'send reset on: 1, off: 0',
            'send section on: 1, off: 1',
            'send arrival on: 1, off: 1',
            'send nothing new: 0',
        ])


class FogWiringTests(unittest.TestCase):
    def test_the_games_fog_messages_are_checked_before_they_go(self):
        body = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        fog = body.index('SV_CoF_FogMessage( svgame.msg_ent, svgame.msg_dest, name, sv.multicast.pData + cof_msg_payload, svgame.msg_realsize )')
        self.assertLess(fog, body.index('SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );'))
        begin = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageBegin( int msg_dest, int msg_num, const float *pOrigin, edict_t *ed )')
        assert_has(self, 'cof_msg_payload = MSG_GetNumBytesWritten( &sv.multicast );', begin)

    def test_runs_with_the_story_layer(self):
        assert_has(self, 'SV_CoF_FogFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))

    def test_a_new_maps_first_frame_gets_the_fog(self):
        """1.1.8 (E2-6): a story level change sends no ResetHUD or InitHUD, and a START_ON env_fog went only to
        the players already in (c_subwaysick1, c_trainscene): each player's first frame of a new map is replayed."""
        frame = body_of(SERVER / 'sv_cof_fog.c', 'void SV_CoF_FogFrame(')
        assert_has(self, 'arrived = cof_fog_players[slot].generation != svs.spawncount;', frame)
        assert_has(self, 'CoF_FogSend( cof_fog_players[slot].replay, moved, arrived, wanted )', frame)
        assert_has(self, 'cof_fog_players[slot].generation = svs.spawncount;', frame)


if __name__ == '__main__':
    unittest.main()
