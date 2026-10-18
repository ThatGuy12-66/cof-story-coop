"""Cry of Fear's engine projects to exactly zmax (worldspawn MaxRange; hw.dll R_SetupGL); Xash used zmax * 1.73, so
every map showed 73 % further than the original, the forests whose darkness is their short MaxRange included
(docs/research/2026-10-18-cof-decompilation-survey.md 1.3). cof_farclip_scale, default 1 (ref/gl/gl_cof_farclip.h)."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

REF_GL = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/ref/gl'


class FarClipTests(unittest.TestCase):
    def test_scale(self):
        header = strip_includes((REF_GL / 'gl_cof_farclip.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_farclip_harness.c', {'farclip_under_test.h': header})
        self.assertEqual(program_output(out), ['1500 x 1: 1500', '1500 x 1.73: 2595', '4096 x 0: 4096', '4096 x -2: 4096'])

    def test_the_world_view_uses_it(self):
        body = body_of(REF_GL / 'gl_rmain.c', 'static float R_GetFarClip( void )')
        assert_has(self, 'return CoF_FarClip( gp_movevars->zmax, cof_farclip_scale.value );', body)
        self.assertFalse('1.73f' in body)

    def test_retail_by_default(self):
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_farclip_scale, "1",', (REF_GL / 'gl_opengl.c').read_text(encoding='latin-1'))
        assert_has(self, 'gEngfuncs.Cvar_RegisterVariable( &cof_farclip_scale );', (REF_GL / 'gl_opengl.c').read_text(encoding='latin-1'))


if __name__ == '__main__':
    unittest.main()
