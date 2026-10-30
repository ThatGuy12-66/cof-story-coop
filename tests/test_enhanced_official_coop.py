"""1.3.3: Cry of Fear's own co-op mode on our engine (a player: "it would be a dream if the actual co-op version were
playable too, since the standard Cry of Fear no longer runs properly"). The story layer stays off on its maps; the
multiplayer rules' allocation is zeroed in every multiplayer game, not only with the story layer on."""
import json
import unittest

from tests.native import ENGINE_SERVER, ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of
from tools import coop_play as play


class OfficialMapTests(unittest.TestCase):
    def test_which_maps_are_the_games_own_co_op(self):
        source = (ENGINE_SERVER / 'sv_cof_story.c').read_text(encoding='latin-1')
        start = source.index('qboolean SV_CoF_StoryOfficialMap( const char *map )')
        function = source[start:source.index('\n}\n', start) + 3]
        out = run_native(self, 'cof_official_map_harness.c', {'official_under_test.c': function})
        self.assertIn('official map scenarios passed', out)

    def test_the_apps_choices_are_all_official_and_never_merged(self):
        maps = [name for _, name in play.OFFICIAL_CHOICES]
        self.assertEqual(maps[0], 'cof_campaign_01')
        self.assertTrue(all(name.startswith('cof_') for name in maps))
        groups = json.loads((ROOT / 'tools/map_groups.json').read_text(encoding='utf-8'))['groups']
        merged = [m for g in groups for m in [g['name'], *g['members']]]
        self.assertTrue(merged)
        self.assertFalse([m for m in merged if m.startswith(('cof_', 'c_cof_'))])


class OfficialWiringTests(unittest.TestCase):
    def test_the_story_layer_is_off_on_official_maps(self):
        body = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'qboolean SV_CoF_StoryCoopEnabled( void )')
        off = body[:body.index('CoF_StoryCloseGate();')]
        assert_has(self, 'SV_CoF_StoryOfficialMap( sv.name )', off)

    def test_the_rules_are_zeroed_before_every_maps_entities_whatever_the_story_layer(self):
        spawn = body_of(ENGINE_SERVER / 'sv_game.c', 'void SV_SpawnEntities( const char *mapname )')
        self.assertLess(spawn.index('SV_CoF_StoryRulesGuard();'), spawn.index('SV_LoadFromFile( mapname, sv.worldmodel->entities );'))
        guard = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'static qboolean CoF_StoryRulesGuard( void )')
        self.assertNotIn('cof_story_coop', guard)
        assert_has(self, 'svs.maxclients <= 1', guard)
        assert_has(self, 'CoF_StoryZeroRules( base )', guard)
        # closing the story gate (the story layer off) keeps the rules zeroed
        close = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'static void CoF_StoryCloseGate( void )')
        self.assertNotIn('CoF_StoryRestoreRules', close)
        # the story gate needs them zeroed too
        enabled = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'qboolean SV_CoF_StoryCoopEnabled( void )')
        self.assertEqual(enabled.count('CoF_StoryRulesGuard()'), 2)


class OfficialPickupsTests(unittest.TestCase):
    """1.3.5 (a player: "collectible items such as syringes or ammunition are not shared"): the game's own co-op hands
    each pickup to whoever takes it first; every player takes their own copy there too, as in the story."""

    def test_the_pickups_run_in_the_games_own_co_op_from_the_story_frame(self):
        frame = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame( void )')
        off = frame[:frame.index('CoF_StoryChapterStart();')]
        assert_has(self, 'if( SV_CoF_StoryOfficialCoop( ))\n\t\t{\n\t\t\tSV_CoF_PickupsFrame();', off)
        official = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'qboolean SV_CoF_StoryOfficialCoop( void )')
        for part in ('svs.maxclients > 1', '"cryoffear"', 'SV_CoF_StoryOfficialMap( sv.name )', 'SV_CoF_RetailDLLVerified()', '!sv.background'):
            assert_has(self, part, official)
        self.assertNotIn('cof_story_coop', official)
        on = body_of(ENGINE_SERVER / 'sv_cof_pickups.c', 'static qboolean CoF_PickupsOn( void )')
        assert_has(self, 'SV_CoF_StoryCoopEnabled() || SV_CoF_StoryOfficialCoop( )', on)

    def test_every_map_of_the_games_own_co_op_is_a_fresh_visit(self):
        # its level changes (doors, a team wipe's restart) are not the story's: they clear the ledger
        change = body_of(ENGINE_SERVER / 'sv_save.c', 'void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background )')
        assert_has(self, 'else SV_CoF_TransitionReset( true );', change)
        reset = body_of(ENGINE_SERVER / 'sv_save.c', 'void SV_CoF_TransitionReset( qboolean clear_history )')
        assert_has(self, 'SV_CoF_PickupsReset();', reset)
        assert_has(self, 'memset( &cof_pickups_ledger, 0, sizeof( cof_pickups_ledger ));', body_of(ENGINE_SERVER / 'sv_cof_pickups.c', 'void SV_CoF_PickupsReset( void )'))
        # not at the find: cof_coop_pickups 0 then 1 on a map finds it again, and the copies lying there would go to all
        self.assertNotIn('cof_pickups_ledger, 0', body_of(ENGINE_SERVER / 'sv_cof_pickups.c', 'static void CoF_PickupsFind( void )'))


class OfficialWeaponsTests(unittest.TestCase):
    """1.3.9 (a player, in the game's own co-op: "After a player is revived, they are sometimes unable to use their
    weapons if they drop them and pick them up again, Cry of Fear crashes"): retail leftovers put right before each
    PostThink (sv_cof_retail.c SV_CoF_RetailItemsRepair, its scenarios in tests/engine/cof_retail_harness.c), and the
    story's CPR, syringe and fall repairs run in the game's own co-op too."""

    def test_the_repair_runs_before_each_post_think_in_both_co_ops(self):
        run = (ENGINE_SERVER / 'sv_pmove.c').read_text(encoding='latin-1')
        before = run[run.index('cof_dead = SV_CoF_PartyDead( clent );'):run.index('svgame.dllFuncs.pfnPlayerPostThink( clent );')]
        assert_has(self, 'if( SV_CoF_StoryCoopEnabled() || SV_CoF_StoryOfficialCoop( ))\n'
                         '\t\tSV_CoF_RetailItemsRepair( cl, cof_dead && !SV_CoF_PartyDead( clent ));', before)
        self.assertLess(before.index('svgame.dllFuncs.pfnPlayerPreThink( clent );'), before.index('SV_CoF_RetailItemsRepair'))

    def test_the_cpr_syringe_and_fall_repairs_run_in_the_games_own_co_op(self):
        frame = body_of(ENGINE_SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame( void )')
        off = frame[:frame.index('CoF_StoryChapterStart();')]
        assert_has(self, 'SV_CoF_PickupsFrame();\n\t\t\tSV_CoF_RetailCprRepair();', off)

    def test_the_weapon_fields_are_the_games(self):
        # the save table's offsets (sv_cof_retail_inventory.c, read from the retail DLL) and the four-byte pointers
        fields = (ENGINE_SERVER / 'sv_cof_retail_inventory.c').read_text(encoding='latin-1')
        retail = (ENGINE_SERVER / 'sv_cof_retail.c').read_text(encoding='latin-1')
        for name, offset, define in (('m_pActiveItem', '0x252C', 'COF_RETAIL_ACTIVE_ITEM'), ('m_pLastItem', '0x2534', 'COF_RETAIL_LAST_ITEM'),
                                     ('m_pNextItem', '0x2538', 'COF_RETAIL_NEXT_ITEM'), ('m_pStoredActionItem', '0x253C', 'COF_RETAIL_STORED_ACTION'),
                                     ('m_pStoredNightVisionWeapon', '0x3F0', 'COF_RETAIL_STORED_NV')):
            assert_has(self, f'"{name}", {offset},', fields)
            self.assertRegex(retail, rf'#define {define} +0x0*{offset[2:]} ')
        assert_has(self, 'typedef uint32_t cof_retail_ptr_t;', retail)


class OfficialLevelChangeTests(unittest.TestCase):
    """1.3.5 (a player: "the player who joins gets disconnected during level transitions or when passing through doors,
    requiring them to rejoin"): reproduced live with two copies on one PC, the guest timed out 3 times in 3; with the
    connection kept, 3 times in 3 it went on to the next map."""

    def test_a_level_change_of_the_games_own_co_op_keeps_the_guests_connection(self):
        change = body_of(ENGINE_SERVER / 'sv_save.c', 'void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background )')
        # only to another of its maps: the notice is told again only while one runs (SV_CoF_StoryNotices)
        assert_has(self, 'qboolean keep = !story && !loadfromsavedgame && SV_CoF_StoryOfficialCoop() && SV_CoF_StoryOfficialMap( mapname );', change)
        told = change.index('SV_FinalMessage( "", true );')
        self.assertLess(change.index('sv_cof_keep_connections = keep;'), told)
        self.assertLess(told, change.index('sv_cof_keep_connections = false;'))
        self.assertLess(change.index('SV_InactivateClients ();'), change.index('sv_cof_keep_connections = keep;'))
        # the kept connection needs its frames back, as the story's does
        assert_has(self, 'for( int i = 0; ( story || keep ) && i < svs.maxclients; i++ )', change)
        final = body_of(ENGINE_SERVER / 'sv_main.c', 'void SV_FinalMessage( const char *message, qboolean reconnect )')
        assert_has(self, 'if( SV_CoF_TransitionActive() || sv_cof_keep_connections )', final)
        self.assertLess(final.index('cof_story_changelevel %d'), final.index('SV_BuildReconnect( &msg );'))
        # told again until the guest's "new" comes
        notices = body_of(ENGINE_SERVER / 'sv_client.c', 'void SV_CoF_StoryNotices( void )')
        assert_has(self, '!( SV_CoF_StoryCoopEnabled() || SV_CoF_StoryOfficialCoop( ))', notices)


class OfficialLaunchTests(unittest.TestCase):
    def test_the_host_runs_the_games_own_rules_with_the_story_layer_off(self):
        from pathlib import Path
        runtime = Path('copy')
        args, cfg = play.official_launch(runtime, name='Bob', map_name='cof_campaign_01', players=4, password='s3cret',
                                         port=27015, video=[])
        self.assertEqual(args[0], str(runtime / 'CoFLaunchApp.exe'))
        self.assertEqual(args[-2:], ['+map', 'cof_campaign_01'])
        self.assertLess(args.index(play.STORY_CFG), args.index(play.SESSION_CFG))   # the session cfg has the last word
        self.assertFalse(any('s3cret' in a for a in args))
        for line in ('cof_story_coop 0', 'coop 0', 'deathmatch 1', 'maxplayers 4', 'cof_companion 0', 'cof_merged_maps 0',
                     'cof_continue 0', 'sv_lan 0', 'sv_password "s3cret"', f'cof_build {play.VERSION}'):
            self.assertIn(line + '\n', cfg)
        self.assertNotIn('cof_chapter_start', cfg)
        self.assertIn('cof_story_coop 1', play.story_config())   # what the session cfg turns off
        for bad in (dict(map_name='c_start'), dict(map_name='cof_campaign_99'), dict(players=5), dict(players=1)):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                play.official_launch(runtime, **dict(dict(name='Bob', map_name='cof_campaign_01', players=2,
                                                          password='', port=27015, video=[]), **bad))


if __name__ == '__main__':
    unittest.main()
