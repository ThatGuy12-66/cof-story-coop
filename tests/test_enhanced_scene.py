"""Shared cutscenes: partners watch the camera the game shows one player."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


class SceneStateTests(unittest.TestCase):
    def test_scene_rules(self):
        out = run_native(self, 'cof_scene_state_test.c', {
            'sv_cof_scene_state.h': (ENGINE_SERVER / 'sv_cof_scene_state.h').read_text(),
        })
        self.assertIn('scene state scenarios passed', out)

    def test_scene_glue(self):
        out = run_native(self, 'cof_scene_harness.c', {
            'sv_cof_scene_state.h': (ENGINE_SERVER / 'sv_cof_scene_state.h').read_text(),
            'scene_under_test.c': '#include "sv_cof_scene_state.h"\n' + strip_includes((ENGINE_SERVER / 'sv_cof_scene.c').read_text()),
        })
        self.assertIn('scene glue scenarios passed', out)


if __name__ == '__main__':
    unittest.main()
