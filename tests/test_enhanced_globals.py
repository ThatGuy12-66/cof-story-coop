"""hl.dll's single globals that hold one player's state, kept per player in story co-op (engine/server/sv_cof_globals.h,
sv_cof_globals.c; docs/research/2026-10-18-cof-decompilation-survey.md section 4): the morphine muffle, the cutscene
flag and the weapon-spread statics are swapped around each player's command; the game's room_type cvar is the host's
own DSP, so a guest's goes to that guest."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_coop_engine import HL_DLL, HL_SHA
from tests.test_enhanced_inventory12 import retail
from tests.test_enhanced_sections import body_of, program_output


class GlobalsRuleTests(unittest.TestCase):
    def test_each_player_keeps_their_own(self):
        header = strip_includes((SERVER / 'sv_cof_globals.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_globals_harness.c', {'globals_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'global morphine-muffle 226489 1',
            'global cutscene 226477 1',
            'global spread-moving 231F0A 1',
            'global spread-jumping 231F0B 1',
            'global spread-ducking 231F0E 1',
            'global spread-sprinting 231F0F 1',
            'global spread-minimum 231F5C 4',
            'bytes 10',
            'after the host\'s command: morphine-muffle=1 cutscene=1 spread-moving=0 spread-jumping=0 spread-ducking=0 spread-sprinting=0 spread-minimum=0',
            # the guest's movement stays theirs; outside a command the host's state is back
            'after the guest\'s command: morphine-muffle=1 cutscene=1 spread-moving=0 spread-jumping=0 spread-ducking=0 spread-sprinting=0 spread-minimum=0',
            'the guest\'s own command: morphine-muffle=0 cutscene=0 spread-moving=1 spread-jumping=0 spread-ducking=0 spread-sprinting=0 spread-minimum=3',
            'room_type owner: none -1, guest 1, host -1, host without the cvar 0, other cvar -1',
        ])

    def test_the_globals_are_where_the_retail_code_uses_them(self):
        if not retail(HL_DLL, HL_SHA):
            self.skipTest('the retail hl.dll is not in runtimes/enhanced')
        from tests.test_coop_engine import file_bytes
        # PreThink 100EF7C7: cmp al, [m_bMorphineMuffle]; PostThink 100EDF36: cmp [m_gbIsInCutscene], 0;
        # CalculateWeaponAccuracy 1011BBD0: mov [minimum], 3 and 1011BBE7: cmp [wasmoving], 0
        self.assertEqual(file_bytes(HL_DLL, 0xEF7C7, 6).hex(), '3a0589642210')
        self.assertEqual(file_bytes(HL_DLL, 0xEDF36, 7).hex(), '803d7764221000')
        self.assertEqual(file_bytes(HL_DLL, 0x11BBD0, 10).hex(), 'c7055c1f231003000000')
        self.assertEqual(file_bytes(HL_DLL, 0x11BBE7, 7).hex(), '803d0a1f231000')


class GlobalsWiringTests(unittest.TestCase):
    def test_on_by_default_with_the_story_layer(self):
        module = (SERVER / 'sv_cof_globals.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_player_state, "1",', module)
        assert_has(self, 'SV_CoF_GlobalsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))

    def test_around_every_players_command(self):
        body = body_of(SERVER / 'sv_pmove.c', 'void SV_RunCmd( sv_client_t *cl, usercmd_t *ucmd, int random_seed )\n{')
        enter, start = body.index('SV_CoF_GlobalsEnter( cl );'), body.index('svgame.dllFuncs.pfnCmdStart(')
        end, leave = body.index('svgame.dllFuncs.pfnCmdEnd( clent );'), body.index('SV_CoF_GlobalsLeave( cl );')
        self.assertLess(enter, start)
        self.assertLess(end, leave)

    def test_the_games_room_type_goes_through_the_engine(self):
        source = (SERVER / 'sv_game.c').read_text(encoding='latin-1')
        table = source[source.index('static enginefuncs_t gEngfuncs ='):]
        assert_has(self, '\tpfnCVarGetFloat,\n\tCvar_VariableString,\n\tpfnCVarSetFloat,', table.replace('\r\n', '\n'))
        assert_has(self, 'SV_CoF_RoomTypeGet( name, &value )', body_of(SERVER / 'sv_game.c', 'static float GAME_EXPORT pfnCVarGetFloat('))
        assert_has(self, 'SV_CoF_RoomTypeSet( name, value )', body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnCVarSetFloat('))


if __name__ == '__main__':
    unittest.main()
