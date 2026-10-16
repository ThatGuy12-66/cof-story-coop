"""Map merging: the group list, building a group from a player's maps, installing it."""
import dataclasses
import hashlib
import json
import shutil
import tempfile
import unittest
from pathlib import Path

from tests.test_map_merge_bsp import MAPS, retail
from tools.map_merge import groups


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def group_of(*members, **extra):
    values = dict(name='cm_' + members[0][2:], members=list(members),
                  inputs={m: sha(MAPS / f'{m}.bsp') for m in members}, bsp_sha256='0' * 64,
                  sections_sha256='0' * 64, wave=1, enabled=True, accepted={})
    values.update(extra)
    return groups.Group(**values)


class LoadTests(unittest.TestCase):
    def write(self, items):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        path = folder / 'map_groups.json'
        path.write_text(json.dumps({'schema': 1, 'groups': items}), encoding='utf-8')
        return path

    def item(self, **extra):
        values = {'name': 'cm_apartment3', 'members': ['c_apartment3', 'c_apartment4'],
                  'inputs': {'c_apartment3': 'a' * 64, 'c_apartment4': 'b' * 64}, 'bsp_sha256': 'c' * 64,
                  'sections_sha256': 'd' * 64, 'wave': 1, 'enabled': True, 'accepted': {}}
        values.update(extra)
        return values

    def test_loads_a_good_group(self):
        loaded = groups.load_groups(self.write([self.item()]))
        self.assertEqual([g.name for g in loaded], ['cm_apartment3'])
        self.assertEqual(loaded[0].members, ['c_apartment3', 'c_apartment4'])

    def test_rejects_bad_names_and_members(self):
        for bad in (self.item(name='CM_X'), self.item(name='cm_' + 'a' * 30), self.item(name='cm_other'),
                    self.item(members=['c_start', 'c_start2'], name='cm_start',
                              inputs={'c_start': 'a' * 64, 'c_start2': 'b' * 64}),
                    self.item(members=['c_apartment3']), self.item(inputs={'c_apartment3': 'a' * 64}),
                    self.item(bsp_sha256='xyz')):
            with self.assertRaises(ValueError, msg=str(bad)):
                groups.load_groups(self.write([bad]))

    def test_the_committed_list_loads(self):
        groups.load_groups()


class BuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        retail('c_apartment3')
        cls.group = group_of('c_apartment3', 'c_apartment4')
        cls.data, cls.text = groups.build_group(MAPS, cls.group)

    def test_build_is_deterministic(self):
        again = groups.build_group(MAPS, self.group)
        self.assertEqual(again, (self.data, self.text))
        self.assertTrue(self.text.startswith('cof-sections 1 cm_apartment3\nsection c_apartment3 0 1 '))

    def test_changed_input_is_refused(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        for m in self.group.members:
            shutil.copyfile(MAPS / f'{m}.bsp', folder / f'{m}.bsp')
        with (folder / 'c_apartment4.bsp').open('r+b') as stream:
            stream.seek(1000)
            byte = stream.read(1)
            stream.seek(1000)
            stream.write(bytes([byte[0] ^ 1]))
        with self.assertRaises(groups.GroupInputMismatch):
            groups.build_group(folder, self.group)

    def test_install_writes_both_files_with_the_expected_hashes(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        for m in self.group.members:
            shutil.copyfile(MAPS / f'{m}.bsp', folder / f'{m}.bsp')
        good = dataclasses.replace(self.group, bsp_sha256=hashlib.sha256(self.data).hexdigest(),
                                   sections_sha256=hashlib.sha256(self.text.encode('ascii')).hexdigest())
        installed = groups.install_group(folder, good)
        self.assertEqual(installed, {'maps/cm_apartment3.bsp': good.bsp_sha256,
                                     'maps/cm_apartment3.sections': good.sections_sha256})
        self.assertEqual(sha(folder / 'cm_apartment3.bsp'), good.bsp_sha256)
        self.assertEqual((folder / 'cm_apartment3.sections').read_bytes(), self.text.encode('ascii'))

    def test_install_with_a_wrong_expected_hash_leaves_nothing(self):
        folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, folder)
        for m in self.group.members:
            shutil.copyfile(MAPS / f'{m}.bsp', folder / f'{m}.bsp')
        with self.assertRaises(RuntimeError):
            groups.install_group(folder, self.group)
        self.assertEqual(sorted(p.name for p in folder.iterdir()), ['c_apartment3.bsp', 'c_apartment4.bsp'])


if __name__ == '__main__':
    unittest.main()
