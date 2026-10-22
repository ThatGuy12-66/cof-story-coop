"""The black rooms behind the doors between maps (engine/server/sv_cof_doorroom.h, 1.2.3). Every door to another map
ends in a small sealed black room; the other map has the same room with the level change back on one side of the
landmark and a walk-in trigger_multiple to a cof_teleport on the other. In the host log of 2026-10-21 the party sat in
c_buscity's room (landbc) with no way out and stepped back into c_city again and again: the room's teleportbc2 had
crossed into c_city with them (the game marks a cof_teleport in a landmark's view to cross a level change) and a change
into a merged map never brought it back. Teleports now stay home, missing ones are put back from the map's own
entity script, and an arrival in a black room is set down in its walk-in, as the retail arrival stands."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of

SERVER = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/server'


class DoorRoomTests(unittest.TestCase):
    def test_the_rules_on_the_logged_room(self):
        header = strip_includes((SERVER / 'sv_cof_doorroom.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_doorroom_harness.c', {'doorroom_under_test.h': header})
        self.assertIn('doorroom scenarios passed', out)

    def test_a_story_level_change_leaves_a_black_rooms_teleport_at_home(self):
        save = body_of(SERVER / 'sv_save.c', 'static SAVERESTOREDATA *SaveGameState( int changelevel )')
        rule = 'if( SV_CoF_TransitionActive() && SV_CoF_TeleportStaysHome( pTable->pent ))\n' \
               '\t\t\tClearBits( pTable->flags, FENTTABLE_MOVEABLE | (( 1 << MAX_LEVEL_CONNECTIONS ) - 1 ));'
        assert_has(self, rule, save)
        # after the game marked what crosses (ParmsChangeLevel) and the entity was saved, before the table is written
        self.assertLess(save.index('svgame.dllFuncs.pfnParmsChangeLevel();'), save.index(rule))
        self.assertLess(save.index('svgame.dllFuncs.pfnSave( pTable->pent, pSaveData );'), save.index(rule))
        self.assertLess(save.index(rule), save.index('// Write entity table'))
        home = body_of(SERVER / 'sv_cof_party.c', 'qboolean SV_CoF_TeleportStaysHome( edict_t *ent )')
        assert_has(self, 'Q_strcmp( SV_GetString( ent->v.classname ), "cof_teleport" )) return false;', home)
        assert_has(self, 'CoF_PartyNamed( target, ent )', home)
        assert_has(self, 'qboolean SV_CoF_TeleportStaysHome( edict_t *ent );',
                   (SERVER / 'sv_cof_party.h').read_text(encoding='latin-1'))

    def test_a_missing_teleport_is_put_back_before_the_arrival_is_placed(self):
        arrive = body_of(SERVER / 'sv_cof_party.c', 'void SV_CoF_PartyTransitionArrive(')
        repair = arrive.index('CoF_PartyRepairTeleports();')
        self.assertLess(arrive.index('CoF_PartyCheckGeneration();'), repair)
        self.assertLess(repair, arrive.index('if( !arrival.leader || arrival.epoch != epoch ) return;'))
        walk = arrive.index('CoF_PartyDoorRoomArrive( &arrival );')
        self.assertLess(arrive.index('phase=arrival-stranded'), walk)
        self.assertLess(walk, arrive.index('CoF_PartyEntered( &svs.clients[i] );'))
        frame = body_of(SERVER / 'sv_cof_party.c', 'void SV_CoF_PartyFrame( void )')
        self.assertLess(frame.index('CoF_PartyCheckGeneration();'), frame.index('CoF_PartyRepairTeleports();'))
        fix = body_of(SERVER / 'sv_cof_party.c', 'static void CoF_PartyRepairTeleports( void )')
        assert_has(self, 'if( SV_CoF_SpawnMapBlock( block ) == NULL ) continue;', fix)
        assert_has(self, 'cof_party_bounces.generation = -1;', fix)
        spawn = body_of(SERVER / 'sv_game.c', 'edict_t *SV_CoF_SpawnMapBlock( char *block )')
        assert_has(self, 'if( !SV_ParseEdict( &cursor, ent ))', spawn)
        assert_has(self, 'svgame.dllFuncs.pfnSpawn( ent ) == -1', spawn)
        assert_has(self, 'edict_t *SV_CoF_SpawnMapBlock( char *block );', (SERVER / 'server.h').read_text(encoding='latin-1'))

    def test_the_walk_in_is_not_avoided_like_a_bounce(self):
        # 1.1.9 (P3) keeps partners out of a trigger_multiple that fires a cof_teleport; in a black room that trigger
        # is the way out, so the arrival's own landing must not use that rule
        landing = body_of(SERVER / 'sv_cof_party.c', 'static qboolean CoF_PartyDoorRoomLanding(')
        self.assertNotIn('CoF_PartyInBounce', landing)
        self.assertNotIn('CoF_PartyLandingPoint', landing)
        assert_has(self, 'CoF_DoorRoomInside( point, ent->v.mins, ent->v.maxs, walkin->v.absmin, walkin->v.absmax )', landing)
        near = body_of(SERVER / 'sv_cof_party.c', 'static edict_t *CoF_PartyDoorRoomWalkIn(')
        assert_has(self, 'CoF_PartyWalkInTeleport( target )', near)
        assert_has(self, 'CoF_PartyLiveTeleport( target )', near)


if __name__ == '__main__':
    unittest.main()
