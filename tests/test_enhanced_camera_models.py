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
            'the player left (no object): 1',
            'a player who left or is loading, whose object still holds it: 0',
            'an owner object we cannot read: 0',
            'placed by the map (no owner): 0',
            'owned by another entity: 0',
            "a ladder's hands, to its climber: 0",
            "a ladder's hands, to a partner: 1",
            'another camera model (the strangle, a cutscene), to a partner: 0',
            'a ladder model placed by the map: 0',
            'owned by another entity: 0',
            'climbing: 1',
            'the climb let go (unfrozen): 0',
            'a newer scene: 0',
            'not a ladder: 0',
        ])


class CameraModelWiringTests(unittest.TestCase):
    def test_removed_each_frame_in_story_co_op(self):
        module = (SERVER / 'sv_cof_camera_models.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_camera_models, "1",', module)
        frame = body_of(SERVER / 'sv_cof_camera_models.c', 'void SV_CoF_CameraModelsFrame(')
        assert_has(self, 'SetBits( ent->v.flags, FL_KILLME );', frame)
        assert_has(self, 'COF_CAMERA_MODEL_SCENE', frame)
        # 1.1.8 (E2-1): the owner's player object decides, not its client state (a player who left or is loading
        # keeps the object, and a running model cutscene walks its raw pDeathScene).
        self.assertNotIn('cs_spawned', frame)
        self.assertNotIn('svs.clients', frame)
        assert_has(self, 'owner->pvPrivateData', frame)
        assert_has(self, 'SV_CoF_CameraModelsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_CameraModelsFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))


if __name__ == '__main__':
    unittest.main()
