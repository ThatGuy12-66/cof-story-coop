"""Checkpoint/session regressions using the actual Tk control window."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from cofcoop import core
from cofcoop.ui import Launcher


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        settings = core.SettingsStore(self.root / "settings.json")
        progress = core.ProgressStore(self.root / "progress.json")
        with patch("cofcoop.ui.core.SettingsStore", return_value=settings), patch("cofcoop.ui.core.ProgressStore", return_value=progress):
            self.app = Launcher()
        self.app.withdraw()
        self.app.active_game_dir = self.root / "actual-game"

    def tearDown(self):
        if self.app.winfo_exists():
            self.app.destroy()
        self.temp.cleanup()

    def test_checkpoint_keeps_session_folder_when_form_is_edited(self):
        self.app.game_dir.set(str(self.root / "other-game"))
        self.app._server_info({"map": "c_intro", "name": "Test", "players": 1, "max_players": 4})
        saved = self.app.progress_store.load()
        self.assertEqual(saved["game_dir"], str(self.root / "actual-game"))
        self.assertEqual(saved["map"], "c_intro")
        self.assertEqual(saved["confirmed_by"], "A2S_INFO")

    def test_stale_server_reply_cannot_replace_new_session_checkpoint(self):
        self.app.session_epoch = 3
        self.app._record_map("c_city", "game log")
        self.app._server_info({"map": "c_intro"}, epoch=2)
        self.assertEqual(self.app.progress_store.load()["map"], "c_city")

    def test_manual_attach_does_not_assign_unverified_folder_to_checkpoint(self):
        from cofcoop.network import Endpoint
        self.app._attached(Endpoint("127.0.0.1"), {"map": "c_intro"}, self.root / "chosen-folder")
        self.assertEqual(self.app.current_map, "c_intro")
        self.assertEqual(self.app.progress_store.load(), {})

    def test_session_change_restarts_polling_after_old_query(self):
        from cofcoop.network import Endpoint
        self.app.query_pending = True
        self.app._attached(Endpoint("127.0.0.1"), {"map": "c_intro"}, self.root)
        self.assertFalse(self.app.query_pending)

    def test_confirmed_log_and_a2s_share_exit_and_checkpoint_handling(self):
        self.app._record_map("c_intro", "game log")
        self.assertEqual(self.app.progress_store.load()["map"], "c_intro")
        self.assertEqual([button.cget("text") for button in self.app.exits.winfo_children()], ["c_nightmare"])
        self.app._record_map("not_a_real_map", "game log")
        self.assertEqual(self.app.progress_store.load()["map"], "c_intro")

    def test_boot_menu_is_not_a_campaign_checkpoint(self):
        self.app._record_map("c_game_menu1", "game log")
        self.assertEqual(self.app.progress_store.load(), {})


if __name__ == "__main__":
    unittest.main()
