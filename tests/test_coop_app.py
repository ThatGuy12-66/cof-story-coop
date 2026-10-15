"""The tester app's entry decisions (tools/coop_app.py); the window itself is checked by --self-test."""
import json
import tempfile
import unittest
from pathlib import Path

from tools import coop_app as app
from tools import friend_setup as friend


def installed(folder: Path, build: int):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": f"test{build}", "build": build,
                                                       "xash_sha256": "0" * 64, "models": {}, "source_game": "src"}))


class StartTests(unittest.TestCase):
    def test_where_to_start(self):
        with tempfile.TemporaryDirectory() as temp:
            downloads, copy = Path(temp) / "Downloads", Path(temp) / "CoF-Story-Coop"
            downloads.mkdir()
            self.assertEqual(app.where_to_start(downloads, copy, 1), ("setup", None))
            installed(copy, 1)
            self.assertEqual(app.where_to_start(downloads, copy, 1), ("main", copy))
            self.assertEqual(app.where_to_start(downloads, copy, 2), ("update", copy))
            self.assertEqual(app.where_to_start(downloads, copy, 0), ("newer", copy))
            # the app started from inside a copy plays from that copy
            self.assertEqual(app.where_to_start(copy, Path(temp) / "elsewhere", 1), ("main", copy))

    def test_cli_install_needs_a_payload_outside_the_exe(self):
        self.assertEqual(app.main(["--install", "src", "dst"]), 2)


if __name__ == "__main__":
    unittest.main()
