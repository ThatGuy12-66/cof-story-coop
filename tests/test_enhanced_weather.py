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


class SectionRainTests(unittest.TestCase):
    # a merged map: the game takes every player's rain from the first rain_settings and the first constant
    # rain_modify of the whole map (player.cpp:9694-9765 in the decompilation; survey section 5.1)
    def test_each_section_its_own_rain(self):
        header = strip_includes((SERVER / 'sv_cof_weather.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_rain_harness.c', {'weather_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'section 0: drips 300 distance 600 wind 1 2 rand 3 4 mode 0 height 500',
            'section 1: drips 0 distance 0 wind 0 0 rand 0 0 mode 0 height 0',
            'section 2: drips 50 distance 800 wind 5 6 rand 7 8 mode 1 height -200',
            # RainData: short drips, coord distance, wind x, y, rand x, y, short mode, coord height
            'payload section 0 large 0: 2c 01 c0 12 08 00 10 00 18 00 20 00 00 00 a0 0f',
            'payload section 2 large 1: 32 00 20 03 05 00 06 00 07 00 08 00 01 00 38 ff',
        ])


class WeatherWiringTests(unittest.TestCase):
    def test_a_merged_maps_rain_is_the_sections(self):
        end = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        assert_has(self, 'SV_CoF_RainMessage( svgame.msg_ent, svgame.msg_dest, name, sv.multicast.pData + cof_msg_payload, svgame.msg_realsize )', end)
        frame = body_of(SERVER / 'sv_cof_weather.c', 'void SV_CoF_WeatherFrame(')
        assert_has(self, 'SV_CoF_PlayerSection( cl->edict ) != host_section', frame)  # the mirror stays in player 1's section
        assert_has(self, 'SV_CoF_WeatherReset();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryShutdown('))

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
