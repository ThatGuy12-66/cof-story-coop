"""J is bound to cof_warp for the host, the local guest and the friend's setup."""
import unittest
from pathlib import Path

from tools import friend_setup

ROOT = Path(__file__).resolve().parents[1]


class WarpBindTests(unittest.TestCase):
    def test_host_launch_binds_j(self):
        self.assertIn('+bind j cof_warp +map c_start', (ROOT / 'tools/Start-StoryHost.ps1').read_text())

    def test_local_guest_config_binds_j(self):
        self.assertIn("'bind j cof_warp'", (ROOT / 'tools/Start-LocalGuest.ps1').read_text())

    def test_friend_setup_binds_j(self):
        self.assertIn('bind "j" "cof_warp"', friend_setup.default_controls())


if __name__ == '__main__':
    unittest.main()
