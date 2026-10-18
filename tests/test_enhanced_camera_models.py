"""The camera models a finished model cutscene leaves (engine/server/sv_cof_camera_models.h, sv_cof_camera_models.c):
cof_mdlcutscene's StartNextAnimation clears each player's pDeathScene and then removes it, i.e. removes NULL (hl.dll
1009F56E/1009F5E9, effects.cpp:9882 in the decompilation), so every finished model cutscene left one camera_model per
player, a sky entity sent to everyone, until the map changed (docs/research/2026-10-18-cof-decompilation-survey.md 3)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class CameraModelTests(unittest.TestCase):
    def test_which_are_left_behind(self):
        header = strip_includes((SERVER / 'sv_cof_camera_models.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_camera_models_harness.c', {'camera_models_under_test.h': '#include <stddef.h>\n' + header})
        self.assertEqual(program_output(out), [
            "the player's scene: 0",
            'the scene ended (pDeathScene cleared): 1',
            'the player has a newer scene: 1',
            'the player left: 1',
            'placed by the map (no owner): 0',
            'owned by another entity: 0',
        ])


class CameraModelWiringTests(unittest.TestCase):
    def test_removed_each_frame_in_story_co_op(self):
        module = (SERVER / 'sv_cof_camera_models.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_camera_models, "1",', module)
        frame = body_of(SERVER / 'sv_cof_camera_models.c', 'void SV_CoF_CameraModelsFrame(')
        assert_has(self, 'SetBits( ent->v.flags, FL_KILLME );', frame)
        assert_has(self, 'COF_CAMERA_MODEL_SCENE', frame)
        assert_has(self, 'SV_CoF_CameraModelsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_CameraModelsFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))


if __name__ == '__main__':
    unittest.main()
