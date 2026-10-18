"""Story co-op rescues from soft-locks the retail game makes with more than one player
(docs/research/2026-10-18-cof-decompilation-survey.md section 2, engine/server/sv_cof_rescue.c)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of


class RescueTests(unittest.TestCase):
    def test_rescue_scenarios(self):
        out = run_native(self, 'cof_rescue_harness.c', {
            'rescue_under_test.c': strip_includes((SERVER / 'sv_cof_rescue.c').read_text(encoding='latin-1')),
        })
        self.assertIn('rescue scenarios passed', out)


class RescueWiringTests(unittest.TestCase):
    def test_runs_with_the_story_layer(self):
        assert_has(self, 'SV_CoF_RescueFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))
        assert_has(self, 'SV_CoF_RescueReset();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryShutdown('))

    def test_a_think_can_have_its_own_first_player_and_edict_one(self):
        game = body_of(SERVER / 'sv_game.c', 'static qboolean CoF_SenderFind(')
        assert_has(self, 'cof_command_sender ? cof_command_sender : cof_think_first ? cof_think_first : cof_first_preference', game)
        for wrapper in ('static edict_t *pfnPEntityOfEntIndexBroken(', 'static edict_t *GAME_EXPORT pfnPEntityOfEntIndexAllEntities('):
            assert_has(self, 'if( iEntIndex == 1 && cof_think_slot_one ) return cof_think_slot_one;', body_of(SERVER / 'sv_game.c', wrapper))
        physics = (SERVER / 'sv_phys.c').read_text(encoding='latin-1')
        self.assertEqual(physics.count('svgame.dllFuncs.pfnThink( ent );'), 2)
        self.assertEqual(physics.count('SV_CoF_RescueThinkBegin( ent );\n'), 2)
        self.assertEqual(physics.count('SV_CoF_RescueThinkEnd();'), 2)
        begin = body_of(SERVER / 'sv_cof_rescue.c', 'void SV_CoF_RescueThinkBegin( edict_t *thinker )')
        assert_has(self, 'SV_CoF_ThinkFirst( SV_CoF_RescueThinkFirst( thinker ));', begin)
        assert_has(self, 'SV_CoF_ThinkSlotOne( SV_CoF_RescueThinkSlotOne( thinker ));', begin)

    def test_a_leaver_is_taken_out_of_the_bosses_pointers(self):
        body = body_of(SERVER / 'sv_client.c', 'void SV_DropClient( sv_client_t *cl, qboolean crash )')
        self.assertLess(body.index('SV_CoF_RescueDrop( cl );'), body.index('svgame.dllFuncs.pfnClientDisconnect( cl->edict );'))

    def test_padlock_turns_are_seen_before_the_game(self):
        body = body_of(SERVER / 'sv_client.c', 'static void SV_ExecuteClientCommand( sv_client_t *cl, const char *s )\n{')
        self.assertLess(body.index('SV_CoF_RescueClientCommand( cl )'), body.index('svgame.dllFuncs.pfnClientCommand( cl->edict )'))

    def test_a_map_entity_is_spawned_from_the_maps_own_text(self):
        body = body_of(SERVER / 'sv_game.c', 'edict_t *SV_CoF_SpawnMapEntity(')
        assert_has(self, 'sv.worldmodel->entities', body)
        assert_has(self, 'SV_ParseEdict( &cursor, ent )', body)
        assert_has(self, 'svgame.dllFuncs.pfnSpawn( ent )', body)


if __name__ == '__main__':
    unittest.main()
