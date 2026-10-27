"""1.3.4: everyone rides a wheelchair in the fight at the end of Ending 4 (c_ending4b): the owner's "would be cool if in
the good ending both players would be on wheelchairs". engine/server/sv_cof_chairs.c, sv_cof_chairs_state.h."""
import hashlib
import unittest

from tests.native import ENGINE_SERVER as SERVER, ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of

HL_DLL = ROOT / 'runtimes/enhanced/cryoffear/cl_dlls/hl.dll'
HL_SHA = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'
CHAIRS = SERVER / 'sv_cof_chairs.c'


def text(path):
    return path.read_text(encoding='latin-1')


class ChairDecisionTests(unittest.TestCase):
    def test_who_is_seated_when_the_ride_ends_and_which_rider_a_simon_fights(self):
        out = run_native(self, 'cof_chairs_harness.c', {'chairs_state_under_test.h': text(SERVER / 'sv_cof_chairs_state.h')})
        self.assertIn('wheelchair scenarios passed', out)


class ChairWiringTests(unittest.TestCase):
    def test_it_runs_after_the_rescue_and_before_the_party(self):
        frame = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame(')
        self.assertLess(frame.index('SV_CoF_RescueFrame();'), frame.index('SV_CoF_ChairsFrame();'))
        self.assertLess(frame.index('SV_CoF_ChairsFrame();'), frame.index('SV_CoF_PartyFrame();'))
        assert_has(self, 'SV_CoF_ChairsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_ChairsReset();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryShutdown('))
        own = body_of(CHAIRS, 'void SV_CoF_ChairsFrame( void )')
        # last frame's riders are compared before anyone is seated or let out this frame, and remembered after
        self.assertLess(own.index('CoF_ChairsWatch();'), own.index('CoF_ChairsSeatAll();'))
        self.assertLess(own.index('CoF_ChairsSeatAll();'), own.index('CoF_ChairsRemember();'))
        assert_has(self, 'cof_coop_wheelchairs.value != 0.0f && !cof_chairs_over', own)
        assert_has(self, 'static CVAR_DEFINE_AUTO( cof_coop_wheelchairs, "1", 0,', text(CHAIRS))

    def test_a_seat_is_the_games_own_beside_the_first_rider_and_the_body_parked_on_theirs(self):
        seat = body_of(CHAIRS, 'static void CoF_ChairsSeatOne( sv_client_t *cl, sv_client_t *anchor, edict_t *anchor_chair, int slot )')
        order = ['VectorCopy( anchor->edict->v.origin, park );', 'SV_CoF_PartyMoveBeside( cl, anchor )',
                 'facing[YAW] = anchor_chair->v.angles[YAW];', 'SV_CoF_StoryCallMember0( COF_CHAIRS_START, pv )',
                 'SV_CoF_PartyPlace( cl, park );']
        self.assertEqual([seat.index(step) for step in order], sorted(seat.index(step) for step in order))
        # the chair's Spawn traces to the floor along -gpGlobals->v_up: straight down from the seat, not a pitched view
        self.assertLess(seat.index('AngleVectors( facing, svgame.globals->v_forward, svgame.globals->v_right, svgame.globals->v_up );'),
                        seat.index('SV_CoF_StoryCallMember0( COF_CHAIRS_START, pv )'))
        # nobody is moved when the game's member cannot run, and a refused seat is not tried again (each try moves them)
        self.assertLess(seat.index('!SV_CoF_StoryBase() || cof_chairs_people[slot].refused'), seat.index('SV_CoF_PartyMoveBeside( cl, anchor )'))
        assert_has(self, 'cof_chairs_people[slot].refused = true;', seat)
        assert_has(self, '#define COF_CHAIRS_START 0xF5B30', text(CHAIRS))
        assert_has(self, '#define COF_CHAIRS_STOP 0xF5D50', text(CHAIRS))
        # the beside spot is the one a J takes beside a rider: beside their chair
        party = text(SERVER / 'sv_cof_party.c')
        assert_has(self, 'return CoF_PartyMoveBeside( cl, partner );', body_of(SERVER / 'sv_cof_party.c', 'qboolean SV_CoF_PartyMoveBeside('))
        self.assertLess(party.index('static qboolean CoF_PartyMoveBeside('), party.index('qboolean SV_CoF_PartyMoveBeside('))
        # an AI companion is never seated, and one the map seated is let out once a person rides
        seat_all = body_of(CHAIRS, 'static void CoF_ChairsSeatAll( void )')
        assert_has(self, 'if( p.fake && person && p.riding && !dead ) CoF_ChairsLetOut( cl, "companion" );', seat_all)

    def test_a_member_with_no_arguments_is_called_with_nothing_on_the_stack(self):
        call = body_of(SERVER / 'sv_cof_story.c', 'qboolean SV_CoF_StoryCallMember0( unsigned int rva, void *self )')
        assert_has(self, 'typedef void ( __fastcall *cof_member0_t )( void *self, void *edx );', call)
        assert_has(self, '((cof_member0_t)((byte *)lib->hInstance + rva ))( self, NULL );', call)

    def test_the_ride_ends_for_everyone_with_the_maps(self):
        watch = body_of(CHAIRS, 'static void CoF_ChairsWatch( void )')
        assert_has(self, 'cof_chairs_over = true;', watch)
        assert_has(self, 'CoF_ChairsLetOut( cl, "ride-over" );', watch)
        let_out = body_of(CHAIRS, 'static qboolean CoF_ChairsLetOut( sv_client_t *cl, const char *reason )')
        assert_has(self, 'SV_CoF_StoryCallMember0( COF_CHAIRS_STOP, pv )', let_out)

    def test_a_team_wipe_lets_every_rider_out_first_and_simons_fight_the_nearest_rider(self):
        rescue = text(SERVER / 'sv_cof_rescue.c')
        wipe = rescue[rescue.index('static void CoF_RescueWheelchair( void )'):]
        self.assertLess(wipe.index('SV_CoF_ChairsReleaseAll();'), wipe.index('svgame.dllFuncs.pfnClientKill( player );'))
        first = body_of(SERVER / 'sv_cof_rescue.c', 'edict_t *SV_CoF_RescueThinkFirst( edict_t *thinker )')
        assert_has(self, 'if( CoF_RescueClass( thinker, "monster_booksimon" )) return SV_CoF_ChairsSimonTarget( thinker );', first)
        # the same field the rescue clears for a leaver (+0x38C, CBookSimon::pPlayer)
        assert_has(self, '#define COF_SIMON_PLAYER 0x38C', rescue)
        assert_has(self, '#define COF_CHAIRS_SIMON_PLAYER 0x38C', text(CHAIRS))
        simon = body_of(CHAIRS, 'edict_t *SV_CoF_ChairsSimonTarget( edict_t *simon )')
        assert_has(self, "if( fights ) return NULL; // it keeps the one it took", simon)
        assert_has(self, 'simon->v.frags == 0.0f', simon)

    def test_checkpoints_wait_out_the_ride(self):
        session = SERVER / 'sv_cof_session.inc'
        assert_has(self, 'if( SV_CoF_ChairsRiding()) return 5;', body_of(session, 'static int CoF_SessionWait( void )'))
        assert_has(self, '"boat", "chapter-kit", "wheelchair"', body_of(session, 'qboolean SV_CoF_SessionSave( const char *slot )'))
        assert_has(self, 'else if( wait == 5 ) Con_Printf( "Co-op save waits: someone is riding a wheelchair.', text(session))
        assert_has(self, '#include "sv_cof_chairs.h"', text(SERVER / 'sv_save.c'))

    def test_a_riders_parked_body_is_not_drawn_for_the_others(self):
        hidden = body_of(SERVER / 'sv_cof_weaponmodel.c', 'qboolean SV_CoF_WeaponModelHidden( const edict_t *viewer, int e )')
        assert_has(self, '( cof_weaponmodel_players[slot].boat || cof_weaponmodel_players[slot].chair ) && viewer != svs.clients[slot].edict', hidden)
        frame = body_of(SERVER / 'sv_cof_weaponmodel.c', 'void SV_CoF_WeaponModelFrame( void )')
        assert_has(self, 'cof_weaponmodel_players[i].chair = SV_CoF_RetailInWheelchair( cl );', frame)

    def test_the_chair_offsets_agree_with_the_other_modules(self):
        for path in (SERVER / 'sv_cof_moments.c', SERVER / 'sv_cof_rescue.c'):
            source = text(path)
            for define in ('COF_CHAIR_SIZE 0x388', 'COF_CHAIR_DEAD 0x368', 'COF_CHAIR_RIDER 0x384'):
                assert_has(self, '#define ' + define, source)
        own = text(CHAIRS)
        for define in ('COF_CHAIRS_SIZE 0x388', 'COF_CHAIRS_DEAD 0x368', 'COF_CHAIRS_RIDER 0x384', 'COF_CHAIRS_PL_SIZE 0x281C'):
            assert_has(self, '#define ' + define, own)

    def test_the_retail_members_take_nothing_and_return_with_a_plain_ret(self):
        if not HL_DLL.is_file() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail Cry of Fear 1.6 hl.dll is not installed locally')
        from tests.test_coop_engine import file_bytes
        # StartWheelchairMode: push esi; mov esi, ecx; lea ecx, [esi+0x390] (m_pWheelchair); ... pop edi; pop esi; ret
        self.assertEqual(file_bytes(HL_DLL, 0xF5B30, 9).hex(), '568bf18d8e90030000')
        self.assertEqual(file_bytes(HL_DLL, 0xF5C82, 3).hex(), '5f5ec3')
        # StopWheelchairMode: push esi; mov esi, ecx; push edi; lea ecx, [esi+0x390]; ... pop edi; pop esi; ret
        self.assertEqual(file_bytes(HL_DLL, 0xF5D50, 10).hex(), '568bf1578d8e90030000')
        self.assertEqual(file_bytes(HL_DLL, 0xF5D9D, 3).hex(), '5f5ec3')


if __name__ == '__main__':
    unittest.main()
