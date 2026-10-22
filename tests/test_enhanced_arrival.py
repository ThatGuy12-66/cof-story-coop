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
            'quick: glock 3 1, slot 12 1, slot 0 0, slot 13 0, cleared 0, key 0, none 0',
        ])

    def test_every_story_sign_on_gets_its_quick_slot_pictures_back(self):
        # client.dll's VidInit empties the QUICK SELECT boxes; the game sends them again only from the first PostThink
        # after a restore, on one global flag the first player's PostThink clears (100EB4CA..100EB4FF): guests' boxes
        # stayed empty after every level change or load
        begin = body_of(SERVER / 'sv_client.c', 'static qboolean SV_Begin_f( sv_client_t *cl )')
        self.assertLess(begin.index('cl->state = cs_spawned;'), begin.index('SV_CoF_ArrivalBegin( cl );'))
        arrival = (SERVER / 'sv_cof_arrival.c').read_text(encoding='latin-1')
        assert_has(self, '#define COF_ARRIVAL_QUICK_SLOT  0x03D8', arrival)
        assert_has(self, '#define COF_ARRIVAL_QUICK_NAME  0x204C', arrival)
        quick = body_of(SERVER / 'sv_cof_arrival.c', 'static void CoF_ArrivalQuickSlots( sv_client_t *cl )')
        assert_has(self, 'pv + COF_ARRIVAL_QUICK_SLOT + k * (int)sizeof( int )', quick)
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "QSlotUpdate", payload, sizeof( payload ), true )', quick)
        frame = body_of(SERVER / 'sv_cof_arrival.c', 'void SV_CoF_ArrivalFrame( void )')
        assert_has(self, 'CoF_ArrivalQuickSlots( cl );', frame)
        story = body_of(SERVER / 'sv_cof_arrival.c', 'void SV_CoF_ArrivalBegin( sv_client_t *cl )')
        assert_has(self, 'p->quick = true;', story)

    def test_the_quick_slot_fields_are_where_the_retail_code_reads_them(self):
        import hashlib
        from tests.test_coop_engine import HL_DLL, HL_SHA, file_bytes
        if not HL_DLL.is_file() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        # the restore's resend (PostThink): cmp [m_bRestored], 0, then cmp [edi+0x3DC], 0 for quick slot 1
        self.assertEqual(file_bytes(HL_DLL, 0xEB4CA, 7).hex(), '803d7664221000')
        self.assertEqual(file_bytes(HL_DLL, 0xEB5B6, 7).hex(), '83bfdc03000000')
        # QuickSelectItem: lea eax, [esi+0x204C] (m_sQuickSel1), the names 0x80 apart
        self.assertEqual(file_bytes(HL_DLL, 0xF0A11, 6).hex(), '8dbe4c200000')

    def test_a_refusal_is_logged_once_per_player_exit_and_map(self):
        # 1.1.9 (X4, E1 F10): an arrival idling in the exit back logged exit-arrival-refused every 10 s
        helper = body_of(SERVER / 'sv_cof_arrival.c', 'static qboolean CoF_ArrivalRefusalNew(') + '\n}\n'
        out = run_native(self, 'cof_arrival_refusal_harness.c', {'refusal_under_test.c': helper})
        self.assertIn('arrival refusal scenarios passed', out)
        refuses = body_of(SERVER / 'sv_cof_arrival.c', 'qboolean SV_CoF_ArrivalRefuses(')
        self.assertLess(refuses.index('if( CoF_ArrivalRefusalNew( refused, level, landmark ))'), refuses.index('phase=exit-arrival-refused'))
        self.assertNotIn('refused_logged', (SERVER / 'sv_cof_arrival.c').read_text(encoding='latin-1'))

    def test_a_carried_player_gets_the_hud_state_at_sign_on(self):
        begin = body_of(SERVER / 'sv_client.c', 'static qboolean SV_Begin_f( sv_client_t *cl )')
        assert_has(self, 'cof_carried = SV_CoF_TransitionRestoredClient( cl );', begin)
        assert_has(self, 'if( cof_carried ) SV_CoF_ArrivalSignon( cl );', begin)
        self.assertLess(begin.index('cof_carried = SV_CoF_TransitionRestoredClient'), begin.index('SV_CoF_TransitionClientReady( cl );'))
        arrival = (SERVER / 'sv_cof_arrival.c').read_text(encoding='latin-1')
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "HideWeapon", &hide, 1, true );', arrival)
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "ScoreInfo", payload, sizeof( payload ), true )', arrival)

    def test_an_arrival_in_the_exit_back_does_not_count_from_its_own_sign_on(self):
        # 1.2.2 (host log 2026-10-21): the host signs on first, set down at the landmark inside the exit back; while the
        # guest still loaded that touch held the exit, and the guest's arrival took the party straight back, eight
        # times in two minutes (c_city <-> c_buscity). The stayed rule began only once everyone had signed on.
        signon = body_of(SERVER / 'sv_cof_arrival.c', 'void SV_CoF_ArrivalSignon( sv_client_t *cl )')
        assert_has(self, 'p->stayed = SV_CoF_PartyInExit( cl->edict, NULL, 0 );', signon)
        assert_has(self, 'p->stay_generation = svs.spawncount;', signon)
        self.assertLess(signon.index('p->signon = true;'), signon.index('p->stayed ='))
        # the refusal reads it before the exit can be held, and SV_Begin_f sets cs_spawned before the sign-on call
        refuses = body_of(SERVER / 'sv_cof_arrival.c', 'qboolean SV_CoF_ArrivalRefuses(')
        assert_has(self, 'p->stayed && p->stay_generation == svs.spawncount', refuses)
        begin = body_of(SERVER / 'sv_client.c', 'static qboolean SV_Begin_f( sv_client_t *cl )')
        self.assertLess(begin.index('cl->state = cs_spawned;'), begin.index('if( cof_carried ) SV_CoF_ArrivalSignon( cl );'))

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
