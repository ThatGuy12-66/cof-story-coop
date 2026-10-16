"""client.dll's Paranoia renderer limits: 256 lightmap pages instead of 64, and brush entities past
edict 899 and past 512 a frame (engine/client/cof_renderer_patch.h, against the retail DLL)."""
import unittest

from tests.test_enhanced_inventory12 import CLIENT_DLL, CLIENT_SHA, ENGINE_CLIENT, retail, run_x86


class WiringTests(unittest.TestCase):
    def test_engine_applies_it_at_load_and_removes_it_before_unload(self):
        cl_game = (ENGINE_CLIENT / 'dll_int/cl_game.c').read_text()
        load = cl_game.index('CL_CoF_Inventory12Load( clgame.hInstance );')
        self.assertIn('CL_CoF_RendererLimitsLoad( clgame.hInstance );', cl_game[load:load + 400])
        unload = cl_game.index('CL_CoF_RendererLimitsUnload();')
        self.assertLess(unload, cl_game.index('COM_FreeLibrary( clgame.hInstance );', unload))
        module = (ENGINE_CLIENT / 'cof_renderer_limits.c').read_text()
        self.assertIn(f'"{CLIENT_SHA}"', module)


class RetailClientTests(unittest.TestCase):
    def setUp(self):
        if not retail(CLIENT_DLL, CLIENT_SHA):
            self.skipTest('the retail Cry of Fear 1.6 client.dll is not installed locally')

    def test_sites_against_retail_client(self):
        out = run_x86(self, 'cof_renderpatch_sites_harness.c', {
            'patch_under_test.h': (ENGINE_CLIENT / 'cof_renderer_patch.h').read_text(),
        }, [CLIENT_DLL])
        self.assertIn('renderer patch sites passed', out)

    def test_lightmaps_and_brush_entities_run_against_retail_client(self):
        out = run_x86(self, 'cof_renderpatch_run_harness.c', {
            'patch_under_test.h': (ENGINE_CLIENT / 'cof_renderer_patch.h').read_text(),
        }, [CLIENT_DLL])
        self.assertIn('stock: 64 warnings, loaded 64', out)
        self.assertIn('stock bump: 64 warnings, loaded 64', out)
        self.assertIn('patched: 0 warnings, loaded 80', out)
        self.assertIn('patched bump: 0 warnings, loaded 80', out)
        self.assertIn('patched: 1496 brush entities', out)
        self.assertIn('renderer patch run passed', out)


if __name__ == '__main__':
    unittest.main()
