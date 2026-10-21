"""1.1.9 (R2): client.dll's level-name question in a merged map (engine/client/cof_levelname.h, cl_game.c). Its
studio renderer culls renderfx 92 props at 800 units only in levels named college1dark or forest1-6, so the merged
files' names (cm_*) lost the cull in c_college1dark and c_forest1 and gave it to c_forest7. The name of the local
player's section is given only while client.dll draws a studio model: its lightmaps are rebuilt whenever the name
changes, and every other caller keeps the merged file's name."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
GAME = ENGINE / 'client/dll_int/cl_game.c'


class LevelNameTests(unittest.TestCase):
    def test_the_rule(self):
        files = {'sections_under_test.h': (ENGINE / 'common/cof_sections.h').read_text(encoding='latin-1'),
                 'levelname_under_test.h': strip_includes((ENGINE / 'client/cof_levelname.h').read_text(encoding='latin-1'))}
        out = run_native(self, 'cof_levelname_harness.c', files)
        self.assertEqual(program_output(out), [
            'cm_forest6, not drawing (lightmaps, HUD): maps/cm_forest6.bsp (props culled at 800)',
            'cm_forest6, props in c_forest6: maps/c_forest6.bsp (props culled at 800)',
            'cm_forest6, props in c_forest7: maps/c_forest7.bsp',
            'cm_forest6, props between, nearer c_forest6: maps/c_forest6.bsp (props culled at 800)',
            'cm_college2, props in c_college1dark: maps/c_college1dark.bsp (props culled at 800)',
            'cm_college2, props in c_college1: maps/c_college1.bsp',
            'cm_college2, not drawing: maps/cm_college2.bsp',
            'cm_bridge, props in c_forest1: maps/c_forest1.bsp (props culled at 800)',
            'cm_bridge, props in c_bridge: maps/c_bridge.bsp',
            'c_forest7 (not merged), props: maps/c_forest7.bsp',
        ])

    def test_the_section_name_only_while_client_dll_draws_a_studio_model(self):
        name = body_of(GAME, 'static const char *pfnGetLevelName( void )')
        assert_has(self, 'qboolean drawing = cl_cof_studio_depth > 0 && cl_cof_studio_frame == host.framecount;', name)
        assert_has(self, 'CoF_LevelName( clgame.mapname, sections,\n\t\t\tdrawing && player != NULL, '
                         'player ? player->origin[2] : 0.0 )', name)
        # the local player is looked up only then (before its entities exist it warns)
        self.assertLess(name.index('if( drawing && sections )'), name.index('player = CL_GetLocalPlayer();'))
        # not connected: still the stock empty name
        assert_has(self, "else mapname[0] = '\\0';", name)
        draw = body_of(GAME, 'static int CL_CoF_StudioDrawModel( int flags )')
        call = draw.index('result = cl_cof_studio->StudioDrawModel( flags );')
        self.assertLess(draw.index('cl_cof_studio_depth++;'), call)
        self.assertLess(call, draw.index('cl_cof_studio_depth--;'))
        # a frame cut short by an error leaves the depth up: the next frame starts again
        self.assertLess(draw.index('cl_cof_studio_frame != host.framecount'), draw.index('cl_cof_studio_depth++;'))
        sections = body_of(GAME, 'static const cof_sections_t *CL_CoF_LevelSections( void )')
        self.assertLess(sections.index('if( cl_cof_sections_count != cl.servercount'),
                        sections.index('COM_CoF_LoadSections( clgame.mapname, &cl_cof_sections )'))

    def test_only_client_dlls_own_studio_drawing_is_wrapped(self):
        init = body_of(GAME, 'static void CL_InitStudioAPI( void )')
        wrap = init.index('cof_wrapped.StudioDrawModel = CL_CoF_StudioDrawModel;')
        self.assertLess(init.index('clgame.dllFuncs.pfnGetStudioModelInterface( STUDIO_INTERFACE_VERSION'), wrap)
        self.assertLess(wrap, init.index('ref.dllFuncs.R_StudioSetDrawInterface( pStudioDraw );'))
        assert_has(self, 'if( pStudioDraw && pStudioDraw != &gDefaultStudioDraw && pStudioDraw->StudioDrawModel )', init)
        assert_has(self, 'cof_wrapped = *pStudioDraw;', init)   # the version and StudioDrawPlayer as they were
        self.assertNotIn('cof_wrapped.StudioDrawPlayer', init)


if __name__ == '__main__':
    unittest.main()
