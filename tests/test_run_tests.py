"""tools/run_tests.py: the parallel runner gives the same verdict as unittest, and splits the slow module."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import run_tests as rt

TINY = '''import os, unittest
class T(unittest.TestCase):
    def test_one(self):
        self.assertTrue(True)
    @unittest.skip("not here")
    def test_two(self):
        pass
    def test_shard(self):
        self.assertEqual(os.environ.get("COF_TEST_SHARD", ""), os.environ.get("EXPECT_SHARD", ""))
'''


class ShardTests(unittest.TestCase):
    def test_the_parts_cover_every_group_once(self):
        groups = list(range(29))
        parts = [groups[i - 1::8] for i in range(1, 9)]
        self.assertEqual(sorted(g for part in parts for g in part), groups)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        (self.root / "tests").mkdir()
        (self.root / "tests/test_tiny.py").write_text(TINY, encoding="utf-8")
        patcher = mock.patch.object(rt, "ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_a_module_runs_like_discover_and_is_counted(self):
        done = rt.run_module("test_tiny")
        self.assertEqual((done["code"], done["tests"], done["skipped"]), (0, 3, 1))

    def test_a_failure_is_reported(self):
        (self.root / "tests/test_tiny.py").write_text(TINY.replace("assertTrue(True)", "assertTrue(False)"),
                                                     encoding="utf-8")
        done = rt.run_module("test_tiny")
        self.assertNotEqual(done["code"], 0)
        self.assertIn("FAILED", done["output"])

    def test_a_shard_gets_its_part_and_only_shard_0_is_counted(self):
        with mock.patch.dict(os.environ, {"EXPECT_SHARD": "2/8"}):
            done = rt.run_module("test_tiny", shard="2/8", counts=0, only="test_shard")
        self.assertEqual((done["code"], done["tests"]), (0, 0))
        self.assertIn("Ran 1 test", done["output"])

    def test_the_slow_module_is_split_in_8(self):
        jobs = rt.jobs_for("test_map_merge_groups")
        self.assertEqual(len(jobs), 9)
        self.assertEqual(jobs[0], ("test_map_merge_groups", "0/8", 1, None))   # the module, no groups in it
        self.assertEqual(jobs[3], ("test_map_merge_groups [3/8]", "3/8", 0,
                                   "test_every_group_builds_as_recorded_and_passes_the_planner"))
        self.assertEqual(rt.jobs_for("test_core"), [("test_core", None, 1, None)])


if __name__ == "__main__":
    unittest.main()
