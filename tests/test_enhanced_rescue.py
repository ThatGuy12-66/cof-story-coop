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

    def test_a_map_entity_is_spawned_from_the_maps_own_text(self):
        body = body_of(SERVER / 'sv_game.c', 'edict_t *SV_CoF_SpawnMapEntity(')
        assert_has(self, 'sv.worldmodel->entities', body)
        assert_has(self, 'SV_ParseEdict( &cursor, ent )', body)
        assert_has(self, 'svgame.dllFuncs.pfnSpawn( ent )', body)


if __name__ == '__main__':
    unittest.main()
