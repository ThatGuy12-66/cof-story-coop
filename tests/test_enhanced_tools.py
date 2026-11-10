"""The player tools' rules (engine/server/sv_cof_tools.h): the host's revive and respawn times and outlines, each
tool's cooldown, what unstuck does, the view's forward vector, which ammo box a gun drops, and which sprays are taken.
The glue (sv_cof_tools.c) and its call sites are tested elsewhere."""
import re
import unittest

from tests.native import ENGINE_SERVER as SERVER, ROOT, run_native, strip_includes
from tests.test_enhanced_sections import program_output

HEADER = SERVER / 'sv_cof_tools.h'
HARNESS = ROOT / 'tests/engine/cof_tools_harness.c'


class ToolsRuleTests(unittest.TestCase):
    def test_the_rules(self):
        header = strip_includes(HEADER.read_text(encoding='latin-1'))
        out = run_native(self, 'cof_tools_harness.c',
                         {'tools_under_test.h': '#include <stddef.h>\n#include <string.h>\n#include <math.h>\n' + header})
        self.assertEqual(program_output(out), [
            'cpr 5: 5', 'cpr 1: 1', 'cpr 20: 20', 'cpr 2.5: 2.5',
            'cpr 0: 5', 'cpr 21: 5', 'cpr -3: 5',
            'respawn 10: 10', 'respawn 5: 5', 'respawn 90: 90',
            'respawn 4: 10', 'respawn 91: 10', 'respawn 384: 10', 'respawn 0: 10',
            'outlines 0: 0', 'outlines 1: 1', 'outlines 2: 0', 'outlines -1: 0',
            'defaults cpr=5 respawn=10 outlines=0',
            'cooldowns unstuck=10 ping=2 scare=20 drop=1',
            'ready now=5 next=4: 1', 'ready now=4 next=4: 1', 'ready now=3.9 next=4: 0', 'ready now=0 next=0: 1',
            'unstuck refused down=0 scene=0 ladder=0 camera=0: 0',
            'unstuck refused down=1 scene=0 ladder=0 camera=0: 1',
            'unstuck refused down=0 scene=1 ladder=0 camera=0: 1',
            'unstuck refused down=0 scene=0 ladder=1 camera=0: 1',
            'unstuck refused down=0 scene=0 ladder=0 camera=1: 1',
            'unstuck plan stuck=1 partner=0: 1', 'unstuck plan stuck=1 partner=1: 1',
            'unstuck plan stuck=0 partner=1: 2', 'unstuck plan stuck=0 partner=0: 0',
            'plans free=0 search=1 beside=2',
            'forward pitch=0 yaw=0: 1.000 0.000 0.000',
            'forward pitch=0 yaw=90: 0.000 1.000 0.000',
            'forward pitch=0 yaw=180: -1.000 0.000 0.000',
            'forward pitch=0 yaw=270: 0.000 -1.000 0.000',
            'forward pitch=90 yaw=0: 0.000 0.000 -1.000',
            'forward pitch=-90 yaw=0: 0.000 0.000 1.000',
            'forward pitch=45 yaw=0: 0.707 0.000 -0.707',
            'forward pitch=30 yaw=60: 0.433 0.750 -0.500',
            'offsets ammo=0x2540 objective=0x1E8C/0xFF blackwhite=0x1EF0/1 glowshell=19',
            'slot 0: 9536, 2: 9544, 31: 9660, 32: -1, -1: -1',
            'ammo 3: ammo_glock box=15 refused=0',
            'ammo 7: ammo_p345 box=8 refused=0',
            'ammo 21: ammo_revolver box=5 refused=0',
            'ammo 4: ammo_shells box=9 refused=0',
            'ammo 11: ammo_rifle box=5 refused=0',
            'ammo 9: ammo_m16 box=20 refused=0',
            'ammo 19: ammo_vp70 box=18 refused=0',
            'ammo 14: ammo_tmp box=30 refused=0',
            'ammo 15: - box=0 refused=1',
            'ammo 27: - box=0 refused=1',
            'ammo 23: - box=0 refused=1',
            'ammo 20: - box=0 refused=1',
            'ammo 13: - box=0 refused=1',
            'ammo 8: - box=0 refused=1',
            'ammo 6: - box=0 refused=1',
            'ammo 1: - box=0 refused=1',
            'ammo 0: - box=0 refused=1',
            'ammo -1: - box=0 refused=1',
            'spray a.png: 1', 'spray A.PNG: 1', 'spray logos/spray.bmp: 1', 'spray me.Bmp: 1',
            'spray x.jpg: 0', 'spray x.tga: 0', 'spray png: 0', 'spray .png: 0',
            'spray dir/.png: 0', 'spray dir.png/file: 0', 'spray x.png.exe: 0',
            'spray x.pngx: 0', 'spray empty: 0', 'spray null: 0',
        ])

    def test_the_header_is_plain_c_with_lf_line_endings(self):
        raw = HEADER.read_bytes()
        self.assertNotIn(b'\r', raw)
        self.assertNotIn(b'\r', HARNESS.read_bytes())
        includes = re.findall(r'^\s*#include\s*<([^>]+)>', raw.decode('latin-1'), re.M)
        self.assertEqual(sorted(includes), ['math.h', 'stddef.h', 'string.h'])
        self.assertNotIn('#include "', raw.decode('latin-1'))
        self.assertNotIn('AngleVectors(', raw.decode('latin-1'))

    def test_the_dual_and_mp5_ids_match_the_weapon_model_table(self):
        weaponmodel = (SERVER / 'sv_cof_weaponmodel.h').read_text(encoding='latin-1')
        self.assertIn('#define COF_WEAPONMODEL_DUAL 15', weaponmodel)
        self.assertIn('{ 27, "models/coop/p_tmp.mdl"', weaponmodel)  # the MP5
        header = HEADER.read_text(encoding='latin-1')
        self.assertIn('#define COF_TOOLS_WEAPON_DUAL 15', header)
        self.assertIn('#define COF_TOOLS_WEAPON_MP5  27', header)


if __name__ == '__main__':
    unittest.main()
