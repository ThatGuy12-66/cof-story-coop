"""Cry of Fear's engine appends one int to usercmd_t (+0x34 is_donator) and clientdata_t (+0x1DC isdonator); the game
DLLs read and write it (docs/research/2026-10-18-cof-decompilation-survey.md 1.2). The engine hands them structures
with room for it, and every command carries the host's chosen donator level (engine/common/cof_abi.h)."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

ENGINE = ROOT / 'engine'
ABI = ENGINE / 'engine/common/cof_abi.h'

STANDINS = '''#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define STATIC_ASSERT( x, msg ) extern char cof_static_assert[( x ) ? 1 : -1]
typedef struct { char bytes[52]; } usercmd_t;
typedef struct { char bytes[0x1DC]; } clientdata_t;
'''


class DonatorLevelTests(unittest.TestCase):
    def test_layout_and_level(self):
        out = run_native(self, 'cof_abi_harness.c', {'abi_under_test.h': STANDINS + strip_includes(ABI.read_text(encoding='latin-1'))})
        self.assertEqual(program_output(out), [
            'usercmd 56 is_donator at 52',
            'clientdata 480 isdonator at 476',
            'level 0 -> 0',
            'level 1 -> 1',
            'level 4 -> 1',
            'level -1 -> 1',
        ])


class DonatorWiringTests(unittest.TestCase):
    def test_the_game_gets_a_command_with_the_field(self):
        body = body_of(ENGINE / 'engine/server/sv_pmove.c', 'void SV_RunCmd( sv_client_t *cl, usercmd_t *ucmd, int random_seed )')
        assert_has(self, 'CoF_DonatorCommand( &cofcmd, ucmd, SV_CoF_DonatorLevel( ));', body)
        assert_has(self, 'svgame.dllFuncs.pfnCmdStart( cl->edict, &cofcmd.cmd, random_seed );', body)
        self.assertFalse('pfnCmdStart( cl->edict, ucmd,' in body)

    def test_the_game_writes_client_data_into_room_for_the_field(self):
        source = (ENGINE / 'engine/server/sv_frame.c').read_text(encoding='latin-1')
        assert_has(self, 'svgame.dllFuncs.pfnUpdateClientData( clent, FBitSet( cl->flags, FCL_LOCAL_WEAPONS ), &cofcd.cd );', source)
        call = source.index('pfnUpdateClientData( clent')
        assert_has(self, 'frame->clientdata = cofcd.cd;', source[call:call + 200])

    def test_client_dll_builds_its_command_in_room_for_the_field(self):
        source = (ENGINE / 'engine/client/cl_main.c').read_text(encoding='latin-1')
        assert_has(self, 'clgame.dllFuncs.CL_CreateMove( host.frametime, &cofcmd.cmd, active );', source)
        call = source.index('CL_CreateMove( host.frametime, &cofcmd.cmd')
        assert_has(self, '*cmd = cofcmd.cmd;', source[call:call + 200])

    def test_the_hosts_choice_defaults_to_donator(self):
        source = (ENGINE / 'engine/server/sv_cof_story.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_donator, "1",', source)
        assert_has(self, 'Cvar_RegisterVariable( &cof_donator );', body_of(ENGINE / 'engine/server/sv_cof_story.c', 'void SV_CoF_StoryInit('))


if __name__ == '__main__':
    unittest.main()
