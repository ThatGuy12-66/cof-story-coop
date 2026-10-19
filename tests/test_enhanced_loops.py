"""Ambient loops in merged story maps (engine/server/sv_cof_loops.h, sv_cof_loops.c): the engine sends a looping
ambient_generic to every player, from the sign-on when it starts with the map, so in a merged map every section heard
every section's loops, and a loop started in one section played on after its players moved to another. hl.dll replays
every active loop when a level is restored (CAmbientGeneric::Precache, sound.cpp): a section's loops now start when a
player enters it and stop when they leave, as across the original level changes."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class LoopTests(unittest.TestCase):
    def test_the_loops_and_a_players_move(self):
        header = strip_includes((SERVER / 'sv_cof_loops.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_loops_harness.c', {'loops_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'start: 1',
            'started: [40 ambience/rain45.wav s0 v100 p100] [41 boss/sawer/chainsaw_loop.wav s1 v200 p100]'
            ' [40 ambience/wind.wav s0 v90 p100]',
            'find 41: 1, find 41 other: -1',
            'change vol: 1',
            'change pitch: 1',
            'change unknown: 0',
            'changed: [40 ambience/rain45.wav s0 v100 p100] [41 boss/sawer/chainsaw_loop.wav s1 v50 p100]'
            ' [40 ambience/wind.wav s0 v90 p120]',
            'stopped: [41 boss/sawer/chainsaw_loop.wav s1 v50 p100] [40 ambience/wind.wav s0 v90 p120]',
            'too long: 0',
            'no sample: 0',
            'full: 0 (256)',
            'full, again: 1',
            'first frame in 0: own 2',
            "first frame in 1: other's 0",
            "0 to 1: left's 1, new's 2, new's removed 0",
            "1 to 2: left's removed 1, another's 0",
            'stays: 0',
        ])


class LoopWiringTests(unittest.TestCase):
    def test_the_games_ambient_sounds_go_through_the_loops(self):
        body = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnEmitAmbientSound(')
        loop = body.index('section = SV_CoF_LoopSound( ent, pos, sample, vol, attn, flags, pitch );')
        self.assertLess(body.index('SetBits( flags, SND_SPAWNING );'), loop)
        assert_has(self, 'if( section == COF_LOOP_HELD ) return;', body)
        filtered = body.index('cof_multicast_section = section >= 0 ? section : SV_CoF_SoundSection( attn, flags, pos );')
        self.assertLess(loop, filtered)
        self.assertLess(filtered, body.index('SV_Multicast( msg_dest, pos, NULL, false, false );'))

    def test_a_saves_loops_are_left_to_the_game(self):
        # the restored ambient_generic replays its own loop (Precache), which the loops hold back from the sign-on
        body = body_of(SERVER / 'sv_save.c', 'static void RestoreSound(')
        held = body.index('if( SV_CoF_LoopRestored( ent, snd->channel )) return;')
        self.assertLess(held, body.index('SV_BuildSoundMsg( &sv.signon'))

    def test_a_removed_ambients_loop_can_still_be_stopped(self):
        # SV_BuildSoundMsg sends a freed edict's sound as the world's (0): the client would not find it to stop it
        assert_has(self, 'return SV_BuildSoundMsgNum( msg, SV_IsValidEdict( ent ) ? NUM_FOR_EDICT( ent ) : 0, chan, '
                         'sample, vol, attn, flags, pitch, pos );',
                   body_of(SERVER / 'sv_game.c', 'int SV_BuildSoundMsg( sizebuf_t *msg, edict_t *ent,'))
        send = body_of(SERVER / 'sv_cof_loops.c', 'static qboolean CoF_LoopSend(')
        assert_has(self, 'SV_BuildSoundMsgNum( &cl->netchan.message, loop->entnum, CHAN_STATIC, loop->sample', send)

    def test_runs_with_the_story_layer_after_the_section_moves(self):
        body = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame(')
        self.assertLess(body.index('SV_CoF_SectionsFrame();'), body.index('SV_CoF_LoopsFrame();'))

    def test_a_loops_entity_is_an_ambient_generic_that_loops(self):
        body = body_of(SERVER / 'sv_cof_loops.c', 'static qboolean CoF_LoopEntity(')
        assert_has(self, 'Q_strcmp( SV_GetString( ent->v.classname ), "ambient_generic" )', body)
        assert_has(self, 'FBitSet( ent->v.spawnflags, COF_AMBIENT_NOT_LOOPING )', body)

    def test_only_in_a_merged_story_map(self):
        body = body_of(SERVER / 'sv_cof_loops.c', 'static qboolean CoF_LoopsOn(')
        assert_has(self, 'SV_CoF_StoryCoopEnabled()', body)
        assert_has(self, 'SV_CoF_PointSection( origin ) >= 0', body)


if __name__ == '__main__':
    unittest.main()
