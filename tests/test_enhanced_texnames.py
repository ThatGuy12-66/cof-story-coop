"""client.dll binds the engine's texture numbers straight to GL (studio skins, sprite frames) and names its own
textures from 1 << 25 without asking GL. A driver that offers the name after the highest in use (an AMD Radeon RX 5500
player: walls in the sky, grey walls in front of him) gives the engine names in that range, so the engine takes the
lowest free slot's number instead (ref/gl/gl_cof_texnames.h, gl_image.c GL_AllocTexture)."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

REF_GL = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/ref/gl'
ALLOC = 'static gl_texture_t *GL_AllocTexture( const char *name, texFlags_t flags )'


class TextureNameTests(unittest.TestCase):
    def test_the_lowest_free_slot(self):
        header = strip_includes((REF_GL / 'gl_cof_texnames.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_texnames_harness.c', {'texnames_under_test.h': header})
        self.assertEqual(program_output(out), ['nothing loaded: 1', '1 2 4 in use: 3',
                                               'every slot below the sky in use: 5807', 'every slot in use: 0',
                                               'names asked past the slots: 0'])

    def test_the_allocator_takes_a_slot_number_for_a_name_past_the_slots(self):
        body = body_of(REF_GL / 'gl_image.c', ALLOC)
        assert_has(self, 'if( !skyboxhack && texnum >= MAX_TEXTURES )', body)
        assert_has(self, 'CoF_SlotTextureName( MAX_TEXTURES, SKYBOX_BASE_NUM, SKYBOX_BASE_NUM + SKYBOX_MAX_SIDES, '
                         'GL_CoF_NameTaken, NULL )', body)
        assert_has(self, 'texnum = slot;', body)
        assert_has(self, '"[cof-textures] the driver offered texture name %u', body)
        # after the driver's offer, before the slot matching that makes the name the slot
        self.assertLess(body.index('pglGenTextures( 1, &texnum );'), body.index('CoF_SlotTextureName('))
        self.assertLess(body.index('CoF_SlotTextureName('), body.index('// try to match texture slot'))
        self.assertNotIn('pglDeleteTextures', body)

    def test_a_name_is_taken_by_a_slot_or_by_gl(self):
        body = body_of(REF_GL / 'gl_image.c', 'static int GL_CoF_NameTaken( unsigned int name, void *ctx )')
        assert_has(self, 'gl_textures[name].texnum != 0', body)
        assert_has(self, 'gl_textures[i].texnum == name', body)
        assert_has(self, 'pglIsTexture( name )', body)
        assert_has(self, '#include "gl_cof_texnames.h"', (REF_GL / 'gl_image.c').read_text(encoding='latin-1'))


if __name__ == '__main__':
    unittest.main()
