"""1.1.9 (open items I8, I9): every human takes their own copy of each weapon, ammo box and medicine a story map
placed; the retail multiplayer rules gave the only one to the first player (engine/server/sv_cof_pickups.c, the rules
in sv_cof_pickups_state.h)."""
import re
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of

PREAMBLE = '#include <stdlib.h>\n#include <string.h>\n'


def state_header():
    return PREAMBLE + strip_includes((SERVER / 'sv_cof_pickups_state.h').read_text(encoding='latin-1'))


def module():
    return (SERVER / 'sv_cof_pickups.c').read_text(encoding='latin-1')


class PickupRuleTests(unittest.TestCase):
    def test_who_sees_and_takes_which_copy(self):
        out = run_native(self, 'cof_pickups_state_test.c', {'pickups_state_under_test.h': state_header()})
        self.assertIn('pickups state scenarios passed', out)

    def test_the_module_against_engine_doubles(self):
        out = run_native(self, 'cof_pickups_harness.c', {
            'pickups_under_test.c': state_header() + '\n' + strip_includes(module()),
        })
        self.assertIn('pickups scenarios passed', out)

    def test_every_class_the_story_maps_place_is_decided(self):
        # what the 81 story maps place (ents/<map>.ent, 2026-10-21): the per-player classes with their counts, and the
        # ones that stay single. None of these blocks has a targetname, target, killtarget or iuser4.
        per_player = {
            'ammo_glock': 34, 'ammo_m16': 10, 'ammo_p345': 7, 'ammo_revolver': 7, 'ammo_rifle': 6, 'ammo_shells': 7,
            'ammo_tmp': 10, 'ammo_vp70': 8, 'weapon_syringe': 50, 'aom_pills': 8, 'weapon_flare': 17,
            'weapon_lantern': 28, 'weapon_branch': 8, 'weapon_nightstick': 4, 'weapon_m16': 5, 'weapon_tmp': 4,
            'weapon_glock': 2, 'weapon_shotgun': 2, 'weapon_rifle': 1, 'weapon_vp70': 1, 'weapon_switchblade': 1,
            'weapon_p345': 1, 'weapon_sledgehammer': 2, 'weapon_axe': 3, 'weapon_famas': 2, 'weapon_booklaser': 2,
            'weapon_camera': 2, 'weapon_mp5': 2,
        }
        single = ['item_key', 'item_phonebattery', 'item_nightvision', 'item_padlock']
        self.assertEqual(sum(per_player.values()), 234)
        table = re.findall(r'\{ "(\w+)", "(\w+)", COF_PICKUP_(\w+) \}', state_header())
        rows = {name: kind for name, _, kind in table}
        for name in per_player:
            self.assertIn(name, rows)
        for name in single:
            self.assertNotIn(name, rows)
        # every alias is spawned as a class of the table
        self.assertTrue(all(spawns in rows for _, spawns, _ in table))
        self.assertEqual({rows[n] for n in per_player if n.startswith('ammo_')}, {'AMMO'})
        self.assertEqual(rows['aom_pills'], 'PILLS')


class PickupModuleWiringTests(unittest.TestCase):
    def test_on_by_default_and_registered(self):
        text = module()
        self.assertIn('CVAR_DEFINE_AUTO( cof_coop_pickups, "1",', text)
        assert_has(self, 'Cvar_RegisterVariable( &cof_coop_pickups );', body_of(SERVER / 'sv_cof_pickups.c', 'void SV_CoF_PickupsInit('))

    def test_a_copy_is_made_from_the_maps_own_text_with_the_copy_mark(self):
        body = body_of(SERVER / 'sv_cof_pickups.c', 'static edict_t *CoF_PickupsSpawn(')
        assert_has(self, 'sv.worldmodel->entities + slot->offset', body)
        assert_has(self, 'svgame.dllFuncs.pfnKeyValue( ent, &kvd );', body)
        self.assertLess(body.index('SetBits( ent->v.spawnflags, COF_PICKUP_COPY_FLAG );'), body.index('svgame.dllFuncs.pfnSpawn( ent )'))

    def test_the_frame_finds_a_maps_pickups_once_per_map(self):
        body = body_of(SERVER / 'sv_cof_pickups.c', 'void SV_CoF_PickupsFrame(')
        assert_has(self, 'if( cof_pickups.generation != svs.spawncount ) CoF_PickupsFind();', body)

    def test_the_hooks_called_for_every_entity_are_cheap(self):
        # sent entities and sphere searches only look the edict up in the index: no scan, no cvar lookup
        for signature in ('qboolean SV_CoF_PickupsVisible(', 'qboolean SV_CoF_PickupsBlocked(', 'static cof_pickups_slot_t *CoF_PickupsAt('):
            body = body_of(SERVER / 'sv_cof_pickups.c', signature)
            self.assertNotIn('for(', body)
            self.assertNotIn('Cvar_', body)
            self.assertNotIn('SV_CoF_StoryCoopEnabled', body)


def lead_hooks_wired():
    return 'SV_CoF_PickupsFrame();' in (SERVER / 'sv_cof_story.c').read_text(encoding='latin-1')


@unittest.skipUnless(lead_hooks_wired(), 'NEEDS LEAD: the hooks in other agents\' files are not in yet (fxw report)')
class PickupHookTests(unittest.TestCase):
    """The one-line hooks in the files other slices own. All of them once the story layer runs the module."""

    def test_the_story_layer_runs_it(self):
        assert_has(self, 'SV_CoF_PickupsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        frame = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame(')
        # after the world moved (the story frame runs after SV_Physics), before the AI companions' commands
        self.assertLess(frame.index('SV_CoF_PickupsFrame();'), frame.index('SV_CoF_CompanionFrame();'))
        assert_has(self, 'SV_CoF_PickupsReset();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryShutdown('))

    def test_a_new_session_forgets_who_took_what(self):
        body = body_of(SERVER / 'sv_save.c', 'void SV_CoF_TransitionReset( qboolean clear_history )')
        clear = body[body.index('if( clear_history )'):]
        assert_has(self, 'SV_CoF_PickupsReset();', clear)

    def test_every_command_is_looked_at_after_the_games_postthink(self):
        body = body_of(SERVER / 'sv_pmove.c', 'void SV_RunCmd( sv_client_t *cl, usercmd_t *ucmd, int random_seed )')
        self.assertLess(body.index('svgame.dllFuncs.pfnPlayerPostThink( clent );'), body.index('SV_CoF_PickupsCommand( cl );'))

    def test_a_copy_is_not_sent_to_who_took_theirs(self):
        body = body_of(SERVER / 'sv_frame.c', 'static void SV_AddEntitiesToPacket(')
        self.assertLess(body.index('SV_CoF_PickupsVisible( pClient, ent )'), body.index('svgame.dllFuncs.pfnAddToFullPack('))

    def test_their_use_does_not_find_it(self):
        body = body_of(SERVER / 'sv_game.c', 'static edict_t *GAME_EXPORT pfnFindEntityInSphere(')
        assert_has(self, 'SV_CoF_PickupsBlocked( sv.current_client->edict, ent )', body)

    def test_checkpoints_carry_the_ledger(self):
        save = body_of(SERVER / 'sv_cof_session.inc', 'qboolean SV_CoF_SessionSave( const char *slot )')
        # only a checkpoint actually written: after its file and its log line
        self.assertLess(save.index('phase=session-saved'), save.index('SV_CoF_PickupsCheckpointSaved( slot );'))
        self.assertLess(save.index('SV_CoF_PickupsCheckpointSaved( slot );'), save.index('done:'))
        load = body_of(SERVER / 'sv_cof_session.inc', 'static qboolean CoF_SessionRunLoad( void )')
        self.assertLess(load.index('LoadGameState( cof_session_data->map, false )'), load.index('SV_CoF_PickupsCheckpointLoaded( cof_session_pending_slot );'))


if __name__ == '__main__':
    unittest.main()
