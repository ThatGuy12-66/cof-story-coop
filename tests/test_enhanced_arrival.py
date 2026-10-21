"""What a story level change leaves behind (engine/server/sv_cof_arrival.h): hands gone after a door to another map
(the door's HideWeapon 9 never cleared on the client), the co-op torch off after a level change and F pressed twice
(the client's scoreboard copy wiped, the game writing its light flags over frags), and the party bounced back through
the exit it just came through (arriving inside it, or a step from it). Found in a host log of 2026-10-20."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

SERVER = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'


class ArrivalTests(unittest.TestCase):
    def test_the_rules(self):
        header = strip_includes((SERVER / 'sv_cof_arrival.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_arrival_harness.c', {'arrival_under_test.h': header})
        self.assertEqual(program_output(out), [
            'F sees on=32 off=0',
            'after on->off 0, off->on 1, refused -1',
            'keep on frags=2 -> 34, on frags=34 -> -1, off frags=2 -> -1, on frags=0 -> 32',
            'scoreinfo 02 22 00 ff ff 00 00 2c 01 fields 34 -1 0 300',
            'restored in the exit back: refused',
            'dodged into the exit back: refused',
            'through the door back: counts',
            'walked back later: counts',
            'another exit at once: counts',
            'stood in another exit since arriving: refused',
            'door long ago: refused',
            'no story arrival: counts',
        ])

    def test_a_carried_player_gets_the_hud_state_at_sign_on(self):
        begin = body_of(SERVER / 'sv_client.c', 'static qboolean SV_Begin_f( sv_client_t *cl )')
        assert_has(self, 'cof_carried = SV_CoF_TransitionRestoredClient( cl );', begin)
        assert_has(self, 'if( cof_carried ) SV_CoF_ArrivalSignon( cl );', begin)
        self.assertLess(begin.index('cof_carried = SV_CoF_TransitionRestoredClient'), begin.index('SV_CoF_TransitionClientReady( cl );'))
        arrival = (SERVER / 'sv_cof_arrival.c').read_text(encoding='latin-1')
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "HideWeapon", &hide, 1, true );', arrival)
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "ScoreInfo", payload, sizeof( payload ), true )', arrival)

    def test_f_switches_the_torch_the_player_has(self):
        source = (SERVER / 'sv_client.c').read_text(encoding='latin-1')
        call = source.index('svgame.dllFuncs.pfnClientCommand( cl->edict );')
        begin = source.index('SV_CoF_ArrivalCommandBegin( cl );')
        self.assertLess(call - begin, 400)
        self.assertGreater(call, begin)
        assert_has(self, 'svgame.dllFuncs.pfnClientCommand( cl->edict );\n\t\tSV_CoF_ArrivalCommandEnd( cl );', source)

    def test_the_exit_check_comes_before_the_hold_and_the_guard(self):
        source = (SERVER / 'sv_game.c').read_text(encoding='latin-1')
        check = source.index('if( SV_CoF_ArrivalRefuses( level, landmark )) return;')
        self.assertLess(source.index('if( SV_CoF_SectionChangeLevel( level, landmark )) return;'), check)
        self.assertLess(check, source.index('if( svs.spawncount == last_spawncount )'))
        self.assertLess(check, source.index('if( !SV_CoF_ExitRequest( level, landname ))'))

    def test_a_player_gone_while_loading_keeps_what_they_carried(self):
        # a joiner who crashed loading c_sewer3 came back with only the shared starting weapons
        drop = body_of(SERVER / 'sv_client.c', 'void SV_DropClient( sv_client_t *cl, qboolean crash )')
        carried = drop.index('if( SV_CoF_ArrivalLoading( cl )) SV_CoF_RecoveryDropCarried( cl );')
        self.assertLess(drop.index('SV_CoF_RecoveryDrop( cl );'), carried)
        self.assertLess(carried, drop.index('SV_CoF_RecoveryDropDone( cl );'))
        save = (SERVER / 'sv_save.c').read_text(encoding='latin-1')
        assert_has(self, 'SV_CoF_ArrivalRestored( &svs.clients[slot] );', save)
        recovery = body_of(SERVER / 'sv_cof_recovery.c', 'void SV_CoF_RecoveryDropCarried( sv_client_t *cl )')
        assert_has(self, 'cl->state == cs_spawned', recovery)
        assert_has(self, 'CoF_RecoveryCapture( cl, record )', recovery)

    def test_ready_and_frame_and_reset_are_wired(self):
        save = (SERVER / 'sv_save.c').read_text(encoding='latin-1')
        assert_has(self, 'SV_CoF_ArrivalReady( cof_transition.destination_origin );', save)
        story = (SERVER / 'sv_cof_story.c').read_text(encoding='latin-1')
        self.assertLess(story.index('SV_CoF_LightFrame();'), story.index('SV_CoF_ArrivalFrame();'))
        assert_has(self, 'SV_CoF_ArrivalReset();', story)


if __name__ == '__main__':
    unittest.main()
