"""The engine's own message-strip lines (share messages, quick-save and pickup lines) although the player turned
"Subtitles" off (engine/common/cof_strip.h): client.dll draws the whole strip only while cl_subtitles is on
(HUDControl_guess.cpp:1572 in the decompilation, client.dll 1003B744), and the menu's Subtitles option is cl_subtitles.
The engine marks its own ProFont lines; while one is on the strip, client.dll reads cl_subtitles as 1."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_inventory12 import ENGINE_CLIENT
from tests.test_enhanced_sections import body_of, program_output

COMMON = SERVER.parent / 'common'


class StripRuleTests(unittest.TestCase):
    def test_marked_lines_and_the_cvar_client_dll_reads(self):
        header = strip_includes((COMMON / 'cof_strip.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_strip_harness.c', {'strip_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'payload 28: 04 53 ... 00 c0 f5',
            'too long: 0',
            'ours: 1',
            "the game's: 0",
            'a byte after the mark: 0',
            'cut short: 0',
            'cl_subtitles off, ours on the strip: 1',
            'cl_subtitles off, ours gone: 0',
            'cl_subtitles on: 1',
            'another cvar: 0',
        ])


class StripWiringTests(unittest.TestCase):
    def test_the_server_marks_its_lines(self):
        body = body_of(SERVER / 'sv_cof_retail.c', 'qboolean SV_CoF_RetailProFont(')
        assert_has(self, 'CoF_StripPayload( payload, sizeof( payload ), colour, text )', body)

    def test_the_client_notes_them_and_answers_client_dll(self):
        parse = body_of(ENGINE_CLIENT / 'parse/cl_parse.c', 'void CL_ParseUserMessage(')
        self.assertLess(parse.index('CL_CoF_StripMessage( clgame.msg[i].name, iSize, pbuf );'), parse.index('clgame.msg[i].func( clgame.msg[i].name, iSize, pbuf );'))
        assert_has(self, 'CL_CoF_StripOwn();', (ENGINE_CLIENT / 'cof_quicksave.c').read_text(encoding='latin-1'))
        # slots 7..12's "You got the ..." stands in for the game's own pickup line: the player's choice holds
        self.assertNotIn('CL_CoF_StripOwn', (ENGINE_CLIENT / 'cof_inventory12.c').read_text(encoding='latin-1'))
        game = (ENGINE_CLIENT / 'dll_int/cl_game.c').read_text(encoding='latin-1').replace('\r\n', '\n')
        table = game[game.index('static cl_enginefunc_t gEngfuncs ='):]
        assert_has(self, '\tpfnCvar_RegisterClientVariable,\n\tpfnGetCvarFloat,\n', table)
        assert_has(self, 'CoF_StripSubtitles( name, Cvar_VariableValue( name ), cl.time, cof_strip_until )',
                   body_of(ENGINE_CLIENT / 'dll_int/cl_game.c', 'static float GAME_EXPORT pfnGetCvarFloat('))


if __name__ == '__main__':
    unittest.main()
