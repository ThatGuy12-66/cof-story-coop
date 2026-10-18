"""A merged map's 3D skies, each section its own (engine/server/sv_cof_sky.h, sv_cof_sky.c): every envpos_sky /
envpos_world sends its marker (skymark_sky, skymark_w) to a joining player and client.dll keeps only the last, drawing
every sky model (renderfx 70/137/184, sent without the PVS test) against it (effects.cpp:5448-5513, gl_sky.cpp:88-124,
client.cpp:3113 in the decompilation; docs/research/2026-10-18-cof-decompilation-survey.md section 5.2)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class SkyRuleTests(unittest.TestCase):
    def test_which_sky_a_player_gets(self):
        header = strip_includes((SERVER / 'sv_cof_sky.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_sky_harness.c', {'sky_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'hidden fx 70 viewer 0 model 1: 1',
            'hidden fx 137 viewer 2 model 1: 1',
            'hidden fx 184 viewer 1 model 0: 1',
            'hidden fx 70 viewer 1 model 1: 0',
            'hidden fx 70 viewer -1 model 1: 0',
            'hidden fx 70 viewer 0 model -1: 0',
            'hidden fx 0 viewer 0 model 1: 0',
            'marker skymark_sky 0 skymark_w 1 SetFog -1',
            'eighths: 1 z -200',
            'large coords: 1 z -200',
            'too short: 0 z 0',
        ])


class SkyWiringTests(unittest.TestCase):
    def test_on_by_default_with_the_story_layer(self):
        module = (SERVER / 'sv_cof_sky.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_section_sky, "1",', module)
        assert_has(self, 'SV_CoF_SkyInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_SkyFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))
        assert_has(self, 'SV_CoF_SkyReset();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryShutdown('))

    def test_markers_held_and_other_sections_sky_models_not_sent(self):
        end = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        assert_has(self, 'SV_CoF_SkyMessage( svgame.msg_ent, svgame.msg_dest, name, sv.multicast.pData + cof_msg_payload, svgame.msg_realsize )', end)
        frame = (SERVER / 'sv_frame.c').read_text(encoding='latin-1')
        assert_has(self, 'svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient, sv.hostflags, player, pset ) && !SV_CoF_SkyHiddenFor( pClient, ent, state->renderfx )', frame)


if __name__ == '__main__':
    unittest.main()
