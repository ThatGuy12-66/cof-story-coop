"""Cry of Fear's opengl32.dll (Paranoia's) turns the engine's glDepthRange(0, 1) into (0, 0.8): client.dll draws its
3D-sky entities in 0.8..0.9 and the sky box in 0.9..1 after the world. A driver whose wglGetProcAddress answers for
OpenGL 1.1 functions hands the engine its own glDepthRange instead (SDL asks it first), the world spans 0..1, the sky
covers it and models show through walls (an AMD Radeon RX 5500 player). The engine applies the dll's rule itself
(ref/gl/gl_cof_depthrange.h): reproduced on NVIDIA with the dll's rule switched off
(evidence/amd-repro-20261020)."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

REF_GL = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/ref/gl'
RULE = 'pglDepthRange( gldepthmin, CoF_DepthRangeFar( gldepthmin, gldepthmax ));'


class DepthRangeTests(unittest.TestCase):
    def test_the_dlls_rule(self):
        header = strip_includes((REF_GL / 'gl_cof_depthrange.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_depthrange_harness.c', {'depthrange_under_test.h': header})
        self.assertEqual(program_output(out), ['world 0..1: 0.8', 'half 0..0.5: 0.8', 'view model 0..0.3: 0.3',
                                               'overview 1..0: 0', 'already 0..0.8: 0.8', 'sky 0.9..1: 1'])

    def test_every_frame_starts_the_world_in_0_to_0_8(self):
        body = body_of(REF_GL / 'gl_rmain.c', 'static void R_Clear( int bitMask )')
        assert_has(self, RULE, body)
        assert_has(self, '"[cof-depth] the world draws in depth 0 to 0.8', body)
        self.assertNotIn('pglDepthRange( gldepthmin, gldepthmax );', body)
        assert_has(self, '#include "gl_cof_depthrange.h"', (REF_GL / 'gl_rmain.c').read_text(encoding='latin-1'))

    def test_the_view_model_gives_the_world_its_range_back(self):
        studio = (REF_GL / 'gl_studio.c').read_text(encoding='latin-1')
        assert_has(self, '#include "gl_cof_depthrange.h"', studio)
        assert_has(self, RULE, studio)
        self.assertNotIn('pglDepthRange( gldepthmin, gldepthmax );', studio)

    def test_no_other_depth_range_bypasses_the_rule(self):
        for path in sorted(REF_GL.glob('*.c')):
            for line in path.read_text(encoding='latin-1').splitlines():
                if 'pglDepthRange(' in line and 'gldepthmax' in line and 'gldepthmin + 0.3f' not in line:
                    self.assertIn('CoF_DepthRangeFar(', line, f'{path.name}: {line.strip()}')


if __name__ == '__main__':
    unittest.main()
