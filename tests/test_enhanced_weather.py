"""Scripted weather in story co-op (engine/server/sv_cof_weather.h, sv_cof_weather.c; the rain-modify gate in
sv_cof_gates.h): CRainModify::Use (100A99A0) returns at once in deathmatch ("Rain error: only static rain in
multiplayer") and in single player changes only player 1 (effects.cpp:5195-5231 in the decompilation). The gate lets it
run; the engine gives every other player player 1's rain, and the game's own PostThink sends each one RainData."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class WeatherRuleTests(unittest.TestCase):
    def test_player_ones_rain_reaches_the_others(self):
        header = strip_includes((SERVER / 'sv_cof_weather.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_weather_harness.c', {'weather_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'block 27DC size 48 update 280C',
            'same rain: copied 0 drips 300 ideal 0 end 0 update 0',
            'scripted change: copied 1 drips 900 ideal 0 end 0 update 1',
            'scripted fade: copied 1 drips 400 ideal 900 end 60 update 1',
            # each player's own PostThink fades its rain; a step apart is no difference
            'both fading, a step apart: copied 0 drips 450 ideal 900 end 60 update 0',
            "host's fade over a frame early: copied 0 drips 800 ideal 900 end 60 update 0",
            'respawned (Spawn zeroes the rain): copied 1 drips 900 ideal 0 end 0 update 1',
        ])


class WeatherWiringTests(unittest.TestCase):
    def test_on_by_default_with_the_story_layer(self):
        module = (SERVER / 'sv_cof_weather.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_weather, "1",', module)
        assert_has(self, 'SV_CoF_WeatherInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_WeatherFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))

    def test_the_gate_follows_the_cvar(self):
        wanted = body_of(SERVER / 'sv_cof_gates.c', 'static qboolean CoF_GateWanted(')
        assert_has(self, 'case COF_GATE_RAIN_MODIFY: return SV_CoF_WeatherOn();', wanted)


if __name__ == '__main__':
    unittest.main()
