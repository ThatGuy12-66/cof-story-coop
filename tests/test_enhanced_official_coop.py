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
