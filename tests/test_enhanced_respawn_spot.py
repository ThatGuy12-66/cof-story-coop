"""Where a story respawn lands (engine/server/sv_pmove.c SV_RunCmd, sv_cof_party.c SV_CoF_PartyRespawned). hl.dll
respawns a player in PreThink, on the entity gpGlobals->startspot names (the landmark the map was entered by), else on
the first info_player_start (player.cpp:7115-7127 in the decompilation). 180 of the 195 landmarks a story map is
entered by stand in its exit back (evidence/map-merge-wave3-20261019/spawn_in_exit.txt), and 59 of the 81 maps' first
info_player_start stand in a map-start script trigger (spawn_triggers.txt), so neither is a safe spot. Live
2026-10-19: a jump respawn in cm_sewer3 landed on kalalale, in c_sewer2's trigger to c_sewer, and the level change it
fired crashed the game. The engine moves the respawned player, after that PreThink and before their move touches
anything, back where they came into the map or section (tests/engine/cof_party_harness.c has the behaviour)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has
from tests.test_enhanced_sections import body_of


class RespawnSpotTests(unittest.TestCase):
    def test_a_respawn_is_moved_before_the_move_touches_anything(self):
        body = body_of(SERVER / 'sv_pmove.c', 'void SV_RunCmd(')
        dead = body.index('cof_dead = SV_CoF_PartyDead( clent );')
        think = body.index('svgame.dllFuncs.pfnPlayerPreThink( clent );')
        moved = body.index('if( cof_dead ) SV_CoF_PartyRespawned( cl );')
        self.assertLess(dead, think)
        self.assertLess(think, moved)
        self.assertLess(moved, body.index('SV_PlayerRunThink( clent, frametime, cl->timebase );'))
        self.assertLess(moved, body.index('svgame.dllFuncs.pfnPM_Move( svgame.pmove, true );'))

    def test_a_solid_way_in_falls_back_to_another_then_beside_a_partner(self):
        # 1.1.8 (E1 F9): the recorded spot can be solid by now; the start landmark stands in the exit back, so try
        # every recorded way in, then beside a living partner, and still mark the player as set down in an exit
        # (tests/engine/cof_party_harness.c has the behaviour).
        body = body_of(SERVER / 'sv_cof_party.c', 'void SV_CoF_PartyRespawned(')
        entries = body.index('SV_CoF_PartySectionMove( cl, cof_party_entry[j].origin, NULL, NULL )')
        partner = body.index('SV_CoF_PartySectionMove( cl, partner->edict->v.origin, partner, NULL )')
        self.assertLess(entries, partner)
        self.assertLess(partner, body.index('SV_CoF_ArrivalLanded( cl );'))

    def test_the_way_in_is_recorded_at_arrivals_and_section_moves(self):
        arrive = body_of(SERVER / 'sv_cof_party.c', 'void SV_CoF_PartyTransitionArrive(')
        assert_has(self, 'CoF_PartyEntered( &svs.clients[i] );', arrive)
        move = body_of(SERVER / 'sv_cof_party.c', 'qboolean SV_CoF_PartySectionMove(')
        assert_has(self, 'CoF_PartyEntered( cl );', move)


if __name__ == '__main__':
    unittest.main()
