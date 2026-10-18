"""The engine's VBO pass leaves every texture unit at GL's initial environment, as GoldSrc leaves client.dll's
renderer (ref/gl/gl_cof_texenv.h): its lightmap pass (gl_rsurf.c R_SetLightmap, gl_overbright 1) set the lightmap
unit's alpha to REPLACE from PREVIOUS, and the client's texture modes set only the colour combine, so the alpha test
of a solid brush entity let every hole of its texture through: c_village1's {invisible use-boxes round the cars
were black boxes, the house doors black (seen live 2026-10-17; gl_overbright 0 at runtime took them away). The
client draws them outside its triangle callbacks (both switched off, the boxes stayed), so the reset is where the
state is left, not before those callbacks."""
import unittest

from tests.native import ROOT, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

REF_GL = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/ref/gl'

# GL 1.3 / ARB_texture_env_combine initial values (the texture environment GoldSrc leaves the client)
INITIAL = {
    'TEXTURE_ENV_MODE': 'MODULATE', 'COMBINE_RGB': 'MODULATE', 'COMBINE_ALPHA': 'MODULATE',
    'SOURCE0_RGB': 'TEXTURE', 'SOURCE1_RGB': 'PREVIOUS', 'SOURCE2_RGB': 'CONSTANT',
    'SOURCE0_ALPHA': 'TEXTURE', 'SOURCE1_ALPHA': 'PREVIOUS', 'SOURCE2_ALPHA': 'CONSTANT',
    'OPERAND0_RGB': 'SRC_COLOR', 'OPERAND1_RGB': 'SRC_COLOR', 'OPERAND2_RGB': 'SRC_ALPHA',
    'OPERAND0_ALPHA': 'SRC_ALPHA', 'OPERAND1_ALPHA': 'SRC_ALPHA', 'OPERAND2_ALPHA': 'SRC_ALPHA',
    'RGB_SCALE': '1', 'ALPHA_SCALE': '1', 'TEXTURE_ENV_COLOR': '0,0,0,0',
}


def units(out):
    """{unit: {state: value}} and the unit left active, from the harness's lines."""
    found, active = {}, None
    for line in program_output(out):
        if line.startswith('active '):
            active = int(line.split()[1])
        elif line.startswith('unit '):
            head, _, rest = line.partition(':')
            found[int(head.split()[1])] = dict(pair.split('=') for pair in rest.split())
    return found, active


class DefaultsTests(unittest.TestCase):
    def test_every_unit_gets_the_initial_environment_and_unit_0_is_left_active(self):
        header = strip_includes((REF_GL / 'gl_cof_texenv.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_texenv_harness.c', {'texenv_under_test.h': header})
        found, active = units(out)
        self.assertEqual(sorted(found), [0, 1, 2, 3])
        for unit, state in found.items():
            self.assertEqual(state, INITIAL, f'unit {unit}')
        self.assertEqual(active, 0)


class WiringTests(unittest.TestCase):
    def test_the_vbo_pass_ends_with_the_reset(self):
        body = body_of(REF_GL / 'gl_rsurf.c', 'static void R_ClearVBOState( qboolean drawlightmap, qboolean drawtextures )\n{')
        reset = body.index('R_CoFTexEnvInitial();')
        # after the unit cleanup (which walks down from the active unit) and the last unit change
        self.assertLess(body.index('GL_CleanUpTextureUnits( 1 );'), reset)
        self.assertLess(body.rindex('GL_SelectTexture('), reset)
        self.assertNotIn('pglTexEnv', body[reset:])
        # the only place R_SetLightmap is reached ends there
        rsurf = (REF_GL / 'gl_rsurf.c').read_text(encoding='latin-1')
        self.assertEqual(rsurf.count('R_SetLightmap();'), 1)
        draw = body_of(REF_GL / 'gl_rsurf.c', 'void R_DrawVBO( qboolean drawlightmap, qboolean drawtextures )\n{')
        after = draw[draw.index('R_ClearVBOState( drawlightmap, drawtextures );'):].split('\n', 1)[1]
        self.assertNotIn('pgl', after)   # bookkeeping only after it
        self.assertNotIn('GL_', after)

    def test_on_by_default_and_registered(self):
        opengl = (REF_GL / 'gl_opengl.c').read_text(encoding='latin-1')
        self.assertIn('CVAR_DEFINE_AUTO( cof_client_texenv, "1", FCVAR_GLCONFIG,', opengl)
        self.assertIn('gEngfuncs.Cvar_RegisterVariable( &cof_client_texenv );', opengl)
        self.assertIn('extern convar_t cof_client_texenv;', (REF_GL / 'gl_local.h').read_text(encoding='latin-1'))


if __name__ == '__main__':
    unittest.main()
