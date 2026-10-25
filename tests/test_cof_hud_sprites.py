"""The Chapter 4 start crash (2026-10-24, 1.2.9). Cry of Fear's client.dll asks hud.txt for 15 names its own hud.txt
lacks; GetSpriteIndex answers -1 and the HUD reads the rect array at [-1]. A Chapter 4 (c_park3) start put the rects 8
bytes into a page after an unmapped one, and CHudAmmo::VidInit's read of bucket1's rect closed the game, every time.
The engine's SPR_GetList adds an empty entry for each missing name to sprites/hud.txt's list."""
import re
import unittest

from tests.native import ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
HEADER = ENGINE / 'client/cof_hud_sprites.h'
GAME = ENGINE / 'client/dll_int/cl_game.c'


class HudSpriteTests(unittest.TestCase):
    def test_every_missing_name_gets_an_empty_entry(self):
        text = HEADER.read_text(encoding='latin-1')
        code = '\n'.join(line for line in text.splitlines() if not line.startswith('#include'))
        out = run_native(self, 'cof_hud_sprites_harness.c', {'hud_sprites_under_test.h': code})
        self.assertIn('hud sprite scenarios passed', out)

    def test_the_names_are_the_ones_the_client_lacks(self):
        # the 15 GetSpriteIndex names of Cry of Fear Enhanced's client.dll (D2A04641...) that its hud.txt
        # (B3067B95..., 57 entries at 640) does not have
        names = re.findall(r'"([a-z0-9_]+)"', HEADER.read_text(encoding='latin-1').split('cof_hud_missing_names[] =')[1]
                           .split('};')[0])
        self.assertEqual(sorted(names), sorted([
            'bucket1', 'cof_m76_mag', 'cof_m76_mag_icon', 'cross', 'd_skull', 'dmg_bio', 'flash_beam', 'flash_empty',
            'flash_full', 'machinegun', 'selection', 'suit_empty', 'suit_full', 'title_half', 'title_life']))

    def test_sprite_lists_make_room_and_add_them_for_hud_txt_only(self):
        body = body_of(GAME, 'static client_sprite_t *SPR_GetList( char *psz, int *piCount )')
        assert_has(self, 'qboolean	cof_hud = !Q_stricmp( psz, "sprites/hud.txt" );', body)
        assert_has(self, 'sizeof( client_sprite_t ) * ( numSprites + ( cof_hud ? COF_HUD_EXTRA : 0 ))', body)
        assert_has(self, 'pEntry->count = CoF_HudSpriteDefaults( pEntry->pList, parsed, Q_max( numSprites, parsed ) + COF_HUD_EXTRA );', body)
        self.assertLess(body.index('pEntry->count++;'), body.index('CoF_HudSpriteDefaults('))
        self.assertLess(body.index('CoF_HudSpriteDefaults('), body.index('if( piCount ) *piCount = pEntry->count;\n\tMem_Free'))


if __name__ == '__main__':
    unittest.main()
