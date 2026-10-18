"""Simon's voice in story co-op (engine/server/sv_cof_voice.h, sv_cof_voice.c): in multiplayer the game plays the
police officer's pain, death, swing, morphine, low-health and breathing samples (player.cpp:1374-1440, 13178-13335 in
the decompilation; docs/research/2026-10-18-cof-decompilation-survey.md section 4); the engine plays Simon's
single-player ones instead and precaches them with the police set."""
import hashlib
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_coop_engine import HL_DLL, HL_SHA
from tests.test_enhanced_sections import body_of, program_output


def voice_output(test):
    header = strip_includes((SERVER / 'sv_cof_voice.h').read_text(encoding='latin-1'))
    out = run_native(test, 'cof_voice_harness.c', {'voice_under_test.h': '#include <ctype.h>\n#include <stdio.h>\n#include <string.h>\n' + header})
    return program_output(out)


class VoiceRuleTests(unittest.TestCase):
    def test_police_samples_become_simons(self):
        self.assertEqual([line for line in voice_output(self) if not line.startswith('name ')], [
            'player/police/Pain/Pain3.wav roll 0: player/vocal/Pain/Pain1.wav',
            'player/police/Pain/Pain10.wav roll 15: player/vocal/Pain/Pain16.wav',
            'player/police/LowHealth/lhealth4.wav roll 4: player/vocal/LowHealth/lhealth5.wav',
            'player/police/Swing/Swing6.wav roll 9: player/vocal/Swing/Swing10.wav',
            'player/police/death/death2.wav roll 6: player/vocal/Death/Death7.wav',
            'player/police/MorphineInjection/Morphine1.wav roll 7: player/vocal/MorphineInjection/Morphine8.wav',
            'player/police/player_out_of_breath2.wav roll 3: player/player_out_of_breath.wav',
            'player/police/sprint_breathe9.wav roll 4: player/sprint/sprint_breathe5.wav',
            'player/police/adlib/help_1.wav: none',
            'player/police/police_pager.wav: none',
            'player/police/Pain/Pain.wav: none',
            'player/police/Pain/Pain1.wavx: none',
            'player/vocal/Pain/Pain1.wav: none',
            ': none',
        ])

    def test_every_simon_sample_is_one_the_game_names(self):
        names = [line[5:] for line in voice_output(self) if line.startswith('name ')]
        self.assertEqual(len(names), 16 + 5 + 10 + 7 + 8 + 1 + 5)
        if not HL_DLL.exists() or hashlib.sha256(HL_DLL.read_bytes()).hexdigest() != HL_SHA:
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        dll = HL_DLL.read_bytes()
        for name in names:
            self.assertIn(name.encode() + b'\0', dll, name)


class VoiceWiringTests(unittest.TestCase):
    def test_on_by_default_and_registered(self):
        module = (SERVER / 'sv_cof_voice.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_simon_voice, "1",', module)
        assert_has(self, 'SV_CoF_VoiceInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))

    def test_the_game_sound_is_swapped_before_it_is_sent(self):
        body = body_of(SERVER / 'sv_game.c', 'void GAME_EXPORT SV_StartSound(')
        self.assertLess(body.index('SV_CoF_VoiceSample( ent, sample'), body.index('SV_BuildSoundMsg('))

    def test_simons_samples_are_precached_with_the_police_ones(self):
        wrapper = body_of(SERVER / 'sv_game.c', 'static int GAME_EXPORT pfnPrecacheSound(')
        self.assertLess(wrapper.index('SV_SoundIndex( name )'), wrapper.index('SV_CoF_VoicePrecache( name )'))
        table = (SERVER / 'sv_game.c').read_text(encoding='latin-1')
        table = table[table.index('static enginefuncs_t gEngfuncs ='):]
        self.assertEqual(table.split('\n')[3].strip(), 'pfnPrecacheSound,')


if __name__ == '__main__':
    unittest.main()
