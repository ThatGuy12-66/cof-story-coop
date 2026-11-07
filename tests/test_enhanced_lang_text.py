"""1.5: the language setting's engine side. Code pages and UTF-8 (engine/cof_text_logic.h), the pack-wide map text
(CoF_EntText_Apply in engine/common/cof_entpatch.c, strings/entity-strings.tsv), a co-op message's quoted pieces
translated (CoF_Lang_Exact), and the menu's multi-line texts translated line by line (mainui MenuStrings.cpp)."""
import re
import unittest

from tests.native import ROOT, assert_has, run_native

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
LOGIC = ENGINE / 'cof_text_logic.h'
ENTPATCH = ENGINE / 'common/cof_entpatch.c'
LANGUAGE = ENGINE / 'common/cof_language.c'
MAINUI = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/3rdparty/mainui'


def table(name: str) -> list:
    text = LOGIC.read_text(encoding='utf-8')
    body = re.search(rf'{name}\[128\] =\s*\{{(.*?)\}};', text, re.S).group(1)
    return [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{4})', body)]


class CodePageTests(unittest.TestCase):
    def test_the_tables_are_windows_code_pages(self):
        for page in (1250, 1251, 1252):
            values = table(f'cof_text_cp{page}')
            self.assertEqual(len(values), 128)
            for i, value in enumerate(values):
                try:
                    expected = ord(bytes([0x80 + i]).decode(f'cp{page}'))
                except UnicodeDecodeError:
                    expected = 0
                self.assertEqual(value, expected, f'cp{page} byte {0x80 + i:#x}')

    def test_utf8_streams_cells_and_breaks(self):
        # the header's comments quote Polish, Russian and Chinese; the harness is written in the machine's code page
        header = LOGIC.read_text(encoding='utf-8').encode('ascii', 'replace').decode('ascii')
        out = run_native(self, 'cof_text_logic_harness.c', {'cof_text_logic.h': header})
        lines = dict(line.split(' ', 1) for line in out.strip().splitlines() if ' ' in line)   # cl echoes harness.c
        self.assertEqual(lines['utf8'], '41/1 4F60/3 597D/3 E9/2 1F600/4')
        self.assertEqual(lines['utf8-bad'], 'FFFD/1 FFFD/1 41/1 FFFD/1 FFFD/1 FFFD/1 FFFD/1 FFFD/1')   # cut, overlong, surrogate
        self.assertEqual(lines['cp1251'], '41F/1 440/1 438/1 432/1 435/1 442/1')   # "Привет"
        # an English player's cp1252 text with a Chinese host's UTF-8 in it; a lone é stays cp1252
        self.assertEqual(lines['cp1252-tolerant'], '43/1 61/1 66/1 E9/2 20/1 4F60/3 20/1 E9/1 74/1 E9/1')
        self.assertEqual(lines['cp1252-strict'], '43/1 61/1 66/1 C3/1 A9/1')
        self.assertEqual(lines['cp1250-tolerant'], 'D3/1 142/1')   # "Ół" stays Polish
        self.assertEqual(lines['stream'], '61 . . 4F60 . E9 . . 62')
        self.assertEqual(lines['cells'], '-1 -1 -1 -1 0 0 0 2')
        self.assertEqual(lines['breaks'], '1 0 0 0 1 0')
        self.assertEqual(lines['to1252'], '80 E9 0 97')
        self.assertEqual(lines['cjk'], '1 1 0 0')


class MapTextTests(unittest.TestCase):
    def test_the_table_replaces_only_text_keys(self):
        ascii = lambda path: path.read_text(encoding='utf-8').encode('ascii', 'replace').decode('ascii')
        out = run_native(self, 'cof_enttext_harness.c', {
            'cof_entpatch.c': ascii(ENTPATCH), 'cof_entpatch.h': ascii(ENGINE / 'common/cof_entpatch.h')})
        self.assertNotIn('FAIL', out)
        self.assertEqual(out.count('ok '), 11)

    def test_the_engine_loads_and_applies_the_table(self):
        text = LANGUAGE.read_text(encoding='utf-8')
        assert_has(self, '"%s/strings/entity-strings.tsv"', text)
        assert_has(self, 'CoF_Lang_LoadEntityText();', text)
        assert_has(self, 'res = CoF_EntText_Apply( base, baselen, CoF_Lang_EntityTextLookup, NULL, &textlen, &replaced );',
                   text)
        # the English side is the map's own bytes, the translation the pack's code page
        assert_has(self, 'CoF_Lang_Convert( plain, sizeof( plain ), line, 1252, COF_ENC_CODEPAGE );', text)
        assert_has(self, 'CoF_Lang_Convert( converted, sizeof( converted ), tab1, cof_lang.codepage, COF_ENC_CODEPAGE );',
                   text)

    def test_a_captured_piece_that_is_a_row_is_translated(self):
        text = LANGUAGE.read_text(encoding='utf-8')
        assert_has(self, 'const char *put = CoF_Lang_Exact( cap[used], caplen[used], enc );', text)
        self.assertEqual(text.count('CoF_Lang_Rebuild( e->'), 4)
        self.assertEqual(len(re.findall(r'CoF_Lang_Rebuild\( e->\w+, cap, caplen, ncap, enc \)', text)), 4)


class MenuLineTests(unittest.TestCase):
    def test_a_built_text_is_translated_line_by_line(self):
        path = MAINUI / 'MenuStrings.cpp'
        if not path.exists():
            self.skipTest('the mainui submodule is not checked out')
        text = path.read_text(encoding='utf-8')
        assert_has(self, 'static const char *Lang_LookupLines( const char *text )', text)
        assert_has(self, "if( strchr( text, '\\n' ) && ( out = Lang_LookupLines( text )))", text)


if __name__ == '__main__':
    unittest.main()
