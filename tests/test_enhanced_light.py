"""The co-op flashlight (the game's `flashlight` command in multiplayer: a free torch every client draws) is the
host's choice, off by default: the original story has only Simon's phone and the lights it hands out
(engine/server/sv_cof_light.h, sv_cof_light.c)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class LightRuleTests(unittest.TestCase):
    def test_the_command_is_refused_and_a_lit_torch_switched_off_only_when_not_allowed(self):
        header = strip_includes((SERVER / 'sv_cof_light.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_light_harness.c', {'light_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'refused flashlight story=1 allowed=0: 1',
            'refused flashlight story=1 allowed=1: 0',
            'refused flashlight story=0 allowed=0: 0',
            'refused flashlights story=1 allowed=0: 0',
            'refused weapontoggle story=1 allowed=0: 0',
            'off frags=32 allowed=0 now=5 next=4: 1',
            'off frags=32 allowed=1 now=5 next=4: 0',
            'off frags=32 allowed=0 now=3 next=4: 0',
            'off frags=1 allowed=0 now=5 next=4: 0',
            'off frags=8 allowed=0 now=5 next=4: 0',
            'off frags=0 allowed=0 now=5 next=4: 0',
        ])


class LightWiringTests(unittest.TestCase):
    def test_off_unless_the_host_allows_it(self):
        module = (SERVER / 'sv_cof_light.c').read_text(encoding='latin-1')
        self.assertIn('CVAR_DEFINE_AUTO( cof_coop_flashlight, "0",', module)

    def test_a_players_command_is_refused_before_the_game_sees_it(self):
        # the definition, not the forward declaration near the top of the file
        body = body_of(SERVER / 'sv_client.c', 'static void SV_ExecuteClientCommand( sv_client_t *cl, const char *s )\n{')
        self.assertLess(body.index('SV_CoF_LightClientCommand( cl )'), body.index('svgame.dllFuncs.pfnClientCommand( cl->edict )'))

    def test_the_companion_lights_up_only_when_allowed(self):
        module = (SERVER / 'sv_cof_companion.c').read_text(encoding='latin-1')
        light = module[module.index('if( cof_companion_light && cof_companion_light->value'):]
        self.assertIn('SV_CoF_CoopFlashlightAllowed()', light[:light.index('\n')])

    def test_the_story_layer_switches_a_lit_torch_off(self):
        init = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit(')
        self.assertIn('SV_CoF_LightInit();', init)
        frame = body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame(')
        self.assertIn('SV_CoF_LightFrame();', frame)


if __name__ == '__main__':
    unittest.main()
