"""The engine's own Half-Life gamepad binds (in_keys.c keynames: +speed on the left trigger and stick, lastinv, BACK
on pause) stayed in every profile the co-op app set up: its config.cfg has no "unbindall", so the gamepad defaults
check saw pad keys bound and never applied the Cry of Fear layout. A guest's log of 2026-10-21 shows "Unknown command:
+speed" and "lastinv" from a controller. 1.2.3 (generation 6) moves every key still on one of those to the layout."""
import re
import unittest

from tests.native import ROOT, run_native
from tests.test_enhanced_sections import body_of

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
PAD = ENGINE / 'client/input/cof_gamepad.c'


def table(text, name):
    start = text.index(f'static const cof_pad_bind_t {name}[] =')
    return text[start:text.index('};', start) + 2]


class PadBuiltinTests(unittest.TestCase):
    def test_the_defaults_check(self):
        text = PAD.read_text(encoding='latin-1')
        start = text.index('typedef struct\n{\n\tint        key;')
        code = [text[start:text.index('} cof_pad_bind_t;', start) + len('} cof_pad_bind_t;')],
                re.search(r'#define COF_PAD_DEFAULTS_GENERATION \d+', text)[0]]
        code += [table(text, name) for name in ('cof_pad_layout', 'cof_pad_layout_builtin', 'cof_pad_layout_gen1',
                                                'cof_pad_layout_gen2', 'cof_pad_layout_gen3', 'cof_pad_layout_gen4')]
        for signature in ('static qboolean CL_CoF_PadAnyBound( void )', 'static int CL_CoF_PadApplyLayout( qboolean force )',
                          'static const char *CL_CoF_PadLayoutBind(', 'static void CL_CoF_PadMigrate(',
                          'static void CL_CoF_PadDefaultsCheck( void )'):
            code.append(body_of(PAD, signature) + '\n}\n')
        keydefs = (ENGINE / 'keydefs.h').read_text(encoding='latin-1')
        out = run_native(self, 'cof_pad_defaults_harness.c', {'pad_under_test.c': '\n'.join(code), 'keydefs.h': keydefs})
        self.assertIn('pad defaults scenarios passed', out)

    def test_the_builtin_list_is_the_engines_own(self):
        # every pad key of in_keys.c's keynames with a default bind, and nothing else
        keys = (ENGINE / 'client/input/in_keys.c').read_text(encoding='latin-1')
        pad = keys[keys.index('// Gamepad'):keys.index('{ "JOY3",')]
        engine = {key: bind for key, bind in re.findall(r'\{\s*"\w+",\s*(K_\w+),\s*"([^"]*)"\s*\}', pad) if bind}
        builtin = table(PAD.read_text(encoding='latin-1'), 'cof_pad_layout_builtin')
        ours = dict(re.findall(r'\{\s*(K_\w+),\s*"([^"]*)"\s*\}', builtin))
        self.assertEqual(ours, engine)


if __name__ == '__main__':
    unittest.main()
