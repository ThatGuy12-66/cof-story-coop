"""1.5: the app's language (tools/app_lang.py) and the translations the language packs are built from
(tools/langpack/text/<lang>/)."""
import json
import re
import unittest
from pathlib import Path

from tools import app_lang
from tools import build_tester_package as builder
from tools.app_lang import T

ROOT = Path(__file__).resolve().parents[1]
TEXT = ROOT / "tools/langpack/text"
FIELDS = re.compile(r"\{(\w+)\}")
CONVERSIONS = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?[sdiufxc]")


class AppLanguageTests(unittest.TestCase):
    def setUp(self):
        self.addCleanup(app_lang.set_current, app_lang.current())

    def test_english_is_the_code_as_written(self):
        app_lang.set_current("english")
        self.assertEqual(T("Play"), "Play")
        self.assertEqual(T("Version {label} is out", label="1.5"), "Version 1.5 is out")
        self.assertEqual(T("Step 2 of 6: Copying game files"), "Step 2 of 6: Copying game files")

    def test_every_table_has_the_fields_of_its_english(self):
        for path in sorted((ROOT / "tools/app_lang").glob("*.json")):
            table = json.loads(path.read_text(encoding="utf-8"))
            self.assertGreater(len(table), 100, path.name)
            for english, text in table.items():
                self.assertEqual(set(FIELDS.findall(english)), set(FIELDS.findall(text)), (path.name, english))

    def test_the_list_offers_every_table(self):
        tables = {p.stem for p in (ROOT / "tools/app_lang").glob("*.json")}
        self.assertEqual(tables, set(app_lang.CODES) - {"english"})

    def test_a_message_from_another_module_is_matched_and_its_pieces_translated(self):
        app_lang.set_current("schinese")
        self.assertEqual(T("Step 2 of 6: Copying game files, 120 of 4300 MB, cryoffear/maps/c_start.bsp"),
                         "第 2/6 步：复制游戏文件，120 / 4300 MB，cryoffear/maps/c_start.bsp")
        self.assertEqual(T('Your name: use up to 24 plain letters, digits and punctuation (no ", \\ or ;).'),
                         '你的名字：最多 24 个英文字母、数字和标点（不能有 "、\\ 或 ;）。')
        self.assertEqual(T("Your game is up: 2 players and 1 AI companion (4 places)."),
                         "你的游戏已启动：2 名玩家和 1 名 AI 同伴（4 个位置）。")
        self.assertEqual(T("Something no table has"), "Something no table has")

    def test_a_repeated_field_must_repeat_the_same_text(self):
        app_lang.set_current("russian")
        same = ("Your router did not open UDP 27015 by itself (x) Forward UDP 27015 to this PC in your router, or all "
                "use a VPN such as Tailscale, ZeroTier or Radmin and share this PC's VPN address instead.")
        self.assertTrue(T(same).startswith("Ваш роутер сам не открыл UDP 27015 (x)."))
        other = same.replace("Forward UDP 27015", "Forward UDP 27016")
        self.assertEqual(T(other), other)

    def test_the_game_language_from_config_cfg(self):
        self.assertEqual(app_lang.game_language('cof_language "russian"\ncof_subtitlelanguage "1"\n'), "russian")
        self.assertEqual(app_lang.game_language('cof_language ""\ncof_subtitlelanguage "4"\n'), "german")
        self.assertEqual(app_lang.game_language('cof_language ""\ncof_subtitlelanguage "1"\n'), "english")
        self.assertIsNone(app_lang.game_language('cl_subtitles "1"\n'))   # a copy that never ran the game
        self.assertIsNone(app_lang.game_language('cof_language "klingon"\n'))

    def test_the_window_language_into_config_cfg(self):
        text = 'name "x"\r\ncof_subtitlelanguage "4"\r\n'
        new = app_lang.config_with_language(text, "schinese")
        self.assertEqual(new, 'name "x"\r\ncof_subtitlelanguage "1"\r\ncof_language "schinese"\r\n')
        self.assertEqual(app_lang.game_language(new), "schinese")
        back = app_lang.config_with_language(new, "english")
        self.assertIn('cof_language ""', back)
        self.assertEqual(app_lang.game_language(back), "english")
        self.assertEqual(app_lang.game_language(app_lang.config_with_language("", "spanish")), "spanish")

    def test_the_choice_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "lang.txt"
            self.assertIsNone(app_lang.saved_choice(path))
            app_lang.save_choice("polish", path)
            self.assertEqual(app_lang.saved_choice(path), "polish")
            path.write_text("nonsense", encoding="utf-8")
            self.assertIsNone(app_lang.saved_choice(path))

    def test_the_app_carries_the_tables(self):
        command = builder.pyinstaller_command(Path("C:/out"), Path("C:/out/payload"))
        self.assertIn(f"{ROOT / 'tools/app_lang'}{builder.os.pathsep}tools/app_lang", command)


class LanguagePackTextTests(unittest.TestCase):
    """What build_packs.py makes the packs from: every pack's rows keep their English's conversions, the map text can
    sit in an entity value, and every line is in the pack's code page."""

    ENC = {"russian": "cp1251", "schinese": "utf-8", "polish": "cp1250"}

    def tables(self, name):
        for folder in sorted(p for p in TEXT.iterdir() if p.is_dir()):
            path = folder / name
            if path.exists():
                yield folder.name, json.loads(path.read_text(encoding="utf-8"))

    def test_conversions_survive(self):
        for name in ("dll.json", "menu.json", "engine.json"):
            for lang, table in self.tables(name):
                for english, text in table.items():
                    a, b = CONVERSIONS.findall(english), CONVERSIONS.findall(text)
                    if english.startswith("You can leave without"):   # the plural "s" is dropped on purpose
                        b = b + a[len(b):]
                    self.assertEqual(a, b, (lang, name, english))

    def test_map_text_fits_an_entity_value(self):
        for lang, table in self.tables("entities.json"):
            enc = self.ENC.get(lang, "cp1252")
            self.assertGreater(len(table), 250, lang)
            for english, text in table.items():
                self.assertNotIn('"', text, (lang, english))
                self.assertLess(len(text.encode(enc)), 80, (lang, english))   # the HUD's message line

    def test_game_text_is_in_the_code_page(self):
        for name in ("items.json", "entities.json", "dll.json", "engine.json"):
            for lang, table in self.tables(name):
                for text in table.values():
                    text.encode(self.ENC.get(lang, "cp1252"))

    def test_every_language_has_the_co_op_strings(self):
        engine = {lang for lang, _ in self.tables("engine.json")}
        menu = {lang for lang, _ in self.tables("menu.json")}
        expected = set(app_lang.CODES) - {"english"}
        self.assertEqual(engine, expected)
        self.assertEqual(menu, expected)


if __name__ == "__main__":
    unittest.main()
