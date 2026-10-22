"""Chat and the co-op HUD on the story maps (engine/client/cof_coop_hud.h, cof_coop_hud.c): client.dll's
CHUDControl::solve (1003BF20) shows the say-text lines, the player list, the follow/syringe icons, the "looking at"
name and "<name> died." only when the level name contains "cof_" (HUDControl_guess.cpp:1967 in the decompilation;
docs/research/2026-10-18-cof-decompilation-survey.md section 4). In a multiplayer game the engine skips that test."""
import unittest

from tests.native import assert_has, run_native, strip_includes
from tests.test_coop_engine import file_bytes
from tests.test_enhanced_inventory12 import CLIENT_DLL, CLIENT_SHA, ENGINE_CLIENT, retail
from tests.test_enhanced_sections import body_of, program_output
from tests.native import ENGINE_SERVER as SERVER


def hud_output(test):
    gates = strip_includes((SERVER / 'sv_cof_gates.h').read_text(encoding='latin-1'))
    header = strip_includes((ENGINE_CLIENT / 'cof_coop_hud.h').read_text(encoding='latin-1'))
    return program_output(run_native(test, 'cof_coop_hud_harness.c', {
        'coop_hud_under_test.h': '#include <string.h>\ntypedef unsigned char byte;\n' + gates + '\n' + header}))


class CoopHudTests(unittest.TestCase):
    def test_the_gate_and_when_it_is_wanted(self):
        self.assertEqual(hud_output(self), [
            'coop-hud rva 3C682 0f8417040000 909090909090',
            # CSaveLoad::WriteSaveInfo (10032340, thiscall, ret 4): the slot label written before a tape save
            'save-labels rva 32340 558bec c20400',
            # CStudioModelRenderer::GL_DrawPoints (10065EF0): bMultiplayer = GetMaxClients() > 1 (cmovg) dressed the
            # first-person arms as the police officer's
            'simon-arms rva 65F20 0f4fca 909090',
            'wanted on=1 maxclients=4: 1',
            'wanted on=1 maxclients=1: 0',
            'wanted on=0 maxclients=4: 0',
            'arms c_start: 1, cm_park2: 1, c_cof_hospital: 0, single player: 0, off: 0, hud on cof_: 1',
            'on: 1 first 90 writes 1',
            'off: 1 first 0f writes 2',
        ])

    def test_made_from_the_retail_bytes(self):
        if not retail(CLIENT_DLL, CLIENT_SHA):
            self.skipTest('the retail client.dll is not in runtimes/enhanced')
        self.assertEqual(file_bytes(CLIENT_DLL, 0x3C682, 6).hex(), '0f8417040000')
        self.assertEqual(file_bytes(CLIENT_DLL, 0x32340, 3).hex(), '558bec')
        self.assertEqual(file_bytes(CLIENT_DLL, 0x3277E, 3).hex(), 'c20400')  # its own return: one argument
        # GL_DrawPoints: movzx ecx, [ebp-1] (0); mov edx, 1; cmp eax, edx; cmovg ecx, edx; mov [ebp-0x28], ecx
        self.assertEqual(file_bytes(CLIENT_DLL, 0x65F15, 17).hex(), '0fb64dffba010000003bc20f4fca894dd8')
        # its only two readers: the sleeve and the finger textures (cmp byte ptr [ebp-0x28], 0)
        self.assertEqual(file_bytes(CLIENT_DLL, 0x668B9, 4).hex(), '807dd800')
        self.assertEqual(file_bytes(CLIENT_DLL, 0x66992, 4).hex(), '807dd800')


class CoopHudWiringTests(unittest.TestCase):
    def test_on_by_default_for_the_retail_client_only(self):
        module = (ENGINE_CLIENT / 'cof_coop_hud.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_hud, "1",', module)
        assert_has(self, f'"{CLIENT_SHA}"', module)

    def test_single_player_save_labels_kept_by_default(self):
        module = (ENGINE_CLIENT / 'cof_coop_hud.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_sp_save_labels, "1",', module)
        assert_has(self, 'Cvar_RegisterVariable( &cof_coop_sp_save_labels );', body_of(ENGINE_CLIENT / 'cof_coop_hud.c', 'void CL_CoF_CoopHudInit('))

    def test_simon_arms_by_default_and_asked_with_the_map(self):
        module = (ENGINE_CLIENT / 'cof_coop_hud.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_simon_arms, "1",', module)
        assert_has(self, 'Cvar_RegisterVariable( &cof_coop_simon_arms );', body_of(ENGINE_CLIENT / 'cof_coop_hud.c', 'void CL_CoF_CoopHudInit('))
        frame = body_of(ENGINE_CLIENT / 'cof_coop_hud.c', 'void CL_CoF_CoopHudFrame( void )')
        assert_has(self, 'CoF_ClientGateWanted( i, cof_hud_cvars[i]->value != 0.0f, cl.maxclients, clgame.mapname )', frame)

    def test_the_player_list_gets_its_pings_on_the_story_maps(self):
        # client.dll holds IN_SCORE only on maps named cof_ (CL_ButtonBits), and Xash sends svc_updatepings only to a
        # client holding it: the co-op list's pings stayed 0 on the story's maps
        body = body_of(SERVER / 'sv_client.c', 'qboolean SV_ShouldUpdatePing( sv_client_t *cl )')
        assert_has(self, 'if( SV_CoF_StoryCoopEnabled() && !FBitSet( cl->flags, FCL_FAKECLIENT ))', body)
        assert_has(self, 'cof_ping_next[slot] = host.realtime + 1.0;', body)
        self.assertNotIn('cl->next_checkpingtime = host.realtime + 1.0', body)

    def test_found_at_load_forgotten_before_unload_and_set_every_frame(self):
        cl_game = (ENGINE_CLIENT / 'dll_int/cl_game.c').read_text(encoding='latin-1')
        load = cl_game.index('CL_CoF_RendererLimitsLoad( clgame.hInstance );')
        assert_has(self, 'CL_CoF_CoopHudLoad( clgame.hInstance );', cl_game[load:load + 300])
        unload = cl_game.index('CL_CoF_CoopHudUnload();')
        self.assertLess(unload, cl_game.index('COM_FreeLibrary( clgame.hInstance );', unload))
        frame = body_of(ENGINE_CLIENT / 'cl_main.c', 'static void CL_UpdateClientData( void )\n{')
        assert_has(self, 'CL_CoF_CoopHudFrame();', frame)
        main = (ENGINE_CLIENT / 'cl_main.c').read_text(encoding='latin-1')
        assert_has(self, 'CL_CoF_CoopHudInit();', main)


if __name__ == '__main__':
    unittest.main()
