"""The tester app's start options, checks and checkpoint reading (tools/coop_play.py)."""
import struct
import tempfile
import unittest
from pathlib import Path

from cofcoop import network
from tools import coop_play as play


def archive(members):
    """A checkpoint archive (engine sv_cof_archive.h) holding the given members."""
    body = b''.join(struct.pack('<64sI', name.encode(), len(data)) + data for name, data in members)
    return struct.pack('<5I', 0x53434643, 1, len(members), 20 + len(body), 0) + body


def session(map_name='c_apartment1', clients=2, names=('Bob', 'Companion'), version=2):
    """A cof_session_data_t record (engine sv_cof_session_data.h), long enough for the names."""
    record = bytearray(16 + 64 + 256 * 64 + 32 * 32 * 2 + 64)
    struct.pack_into('<4I64s', record, 0, version, clients, 100, 0, map_name.encode())
    for index, name in enumerate(names):
        struct.pack_into('32s', record, 16 + 64 + 256 * 64 + index * 32, name.encode())
    return bytes(record)


class StartTests(unittest.TestCase):
    def test_host_and_solo_command_lines_hold_no_dev_tools_or_password(self):
        runtime = Path('copy')
        args, cfg = play.host_launch(runtime, solo=False, name='Bob', map_name='c_start', players=3,
                                     password='s3cret', companion=True, port=27015, cont=False, video=[])
        self.assertEqual(args[0], str(runtime / 'CoFLaunchApp.exe'))
        self.assertEqual(args[-2:], ['+map', 'c_start'])
        for absent in ('-console', 's3cret', '-windowed', 'rcon'):
            self.assertFalse(any(absent in a for a in args), absent)
        self.assertLess(args.index(play.STORY_CFG), args.index(play.SESSION_CFG))
        for line in ('maxplayers 3', 'sv_lan 0', 'sv_password "s3cret"', 'cof_companion 1', 'cof_build test1',
                     'cof_continue 0', 'hostname "Bob\'s game"', 'name "Bob"'):
            self.assertIn(line + '\n', cfg)
        self.assertNotIn('rcon', cfg)
        args, cfg = play.host_launch(runtime, solo=True, name='Bob', map_name='c_intro', players=2, password='x',
                                     companion=False, port=27015, cont=True, video=[])
        for line in ('sv_lan 1', 'sv_password ""', 'cof_companion 1', 'cof_continue 1'):
            self.assertIn(line + '\n', cfg)

    def test_story_config_removes_the_test_tools(self):
        cfg = play.story_config()
        for line in ('cof_story_coop 1', 'cof_story_trace 1', 'sv_cheats 0', 'sv_enttools_enable 0', 'cof_test_gun 0',
                     'fps_max 60'):
            self.assertIn(line + '\n', cfg)
        self.assertNotIn('rcon_password', cfg)

    def test_join_takes_a_code_or_an_address(self):
        code = network.make_join_code(network.Endpoint('203.0.113.7', 27016))
        args, cfg = play.join_launch(Path('copy'), name='Ann', password='pw', endpoint=code, video=[])
        self.assertEqual(args[-2:], ['+connect', '203.0.113.7:27016'])
        self.assertNotIn('pw', ' '.join(args))
        self.assertEqual(cfg, 'name "Ann"\npassword "pw"\n')
        for bad in ('host;quit', '1.2.3.999', 'COF1-AAAA-00000000'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                play.join_launch(Path('copy'), name='Ann', password='', endpoint=bad, video=[])

    def test_names_passwords_ports_players(self):
        self.assertEqual(play.clean_name("  Bob's  "), "Bob's")
        for bad in ('', '   ', 'a"b', 'a;b', 'a\\b', 'Ž', 'x' * 25):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                play.clean_name(bad)
        self.assertEqual(play.clean_password(''), '')
        self.assertEqual(play.clean_port('27020'), 27020)
        for bad in ('80', '70000', 'abc'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                play.clean_port(bad)
        self.assertEqual(play.clean_players('4'), 4)
        for bad in ('1', '5', 'x'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                play.clean_players(bad)

    def test_first_launch_fills_the_desktop_once(self):
        self.assertEqual(play.video_flags(True, (2560, 1440)), ['-borderless', '-width', '2560', '-height', '1440'])
        self.assertEqual(play.video_flags(False, (2560, 1440)), [])
        self.assertEqual(play.video_flags(True, None), [])

    def test_chapters_start_after_the_intro(self):
        self.assertEqual(play.NEW_GAME_MAP, 'c_intro')
        self.assertNotIn('c_intro', [name for _, name in play.CHAPTER_CHOICES])
        self.assertEqual(play.CHAPTER_CHOICES[1][1], 'c_start')


class HostCheckTests(unittest.TestCase):
    def test_check_host_refuses_other_or_missing_build(self):
        for info in ({}, {'cofbuild': 'test2'}):
            with self.subTest(info=info), self.assertRaisesRegex(ValueError, 'different version'):
                play.check_host(dict(info, numcl='1', maxcl='2'), password='')
        play.check_host({'cofbuild': 'test1', 'numcl': '1', 'maxcl': '2', 'password': '0'}, password='')

    def test_check_host_full_or_needs_a_password(self):
        with self.assertRaisesRegex(ValueError, 'full'):
            play.check_host({'cofbuild': 'test1', 'numcl': '2', 'maxcl': '2'}, password='')
        with self.assertRaisesRegex(ValueError, 'password'):
            play.check_host({'cofbuild': 'test1', 'numcl': '1', 'maxcl': '2', 'password': '1'}, password='')


class CheckpointTests(unittest.TestCase):
    def test_reads_map_players_and_names(self):
        cp = play.parse_checkpoint('autosave', archive([('global.dat', b'x' * 10), ('session.dat', session())]))
        self.assertEqual((cp.slot, cp.map, cp.players, cp.names), ('autosave', 'c_apartment1', 2, ('Bob', 'Companion')))

    def test_refuses_other_versions_and_damage(self):
        good = archive([('session.dat', session())])
        for data in (archive([('session.dat', session(version=1))]), good[:-10], b'', archive([('global.dat', b'x')]),
                     archive([('session.dat', session(clients=1))]), b'XXXX' + good[4:]):
            with self.subTest(size=len(data)):
                self.assertIsNone(play.parse_checkpoint('autosave', data))

    def test_latest_checkpoint_in_the_copy(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(play.latest_checkpoint(root))
            folder = root / 'SAVE/cofcoop'
            folder.mkdir(parents=True)
            (folder / 'latest.txt').write_text('autosave')
            (folder / 'autosave.cof').write_bytes(archive([('session.dat', session('c_start2', 3))]))
            cp = play.latest_checkpoint(root)
            self.assertEqual((cp.map, cp.players), ('c_start2', 3))
            (folder / 'latest.txt').write_text('../evil')
            self.assertIsNone(play.latest_checkpoint(root))

    def test_continue_warns_about_another_name(self):
        cp = play.Checkpoint('autosave', 'c_start', 2, ('Bob', 'Companion'))
        self.assertIsNone(play.continue_warning(cp, 'bob'))
        self.assertIn('Bob', play.continue_warning(cp, 'Ann'))


class FakeNet:
    NetworkError = network.NetworkError
    Endpoint = network.Endpoint
    make_join_code = staticmethod(network.make_join_code)

    def __init__(self, upnp=True, up=True):
        self.upnp, self.up, self.unmapped = upnp, up, 0

    def upnp_map(self, port):
        if not self.upnp:
            raise network.NetworkError('no UPnP router')
        return {'external_ip': '203.0.113.7', 'port': port}

    def upnp_unmap(self, mapping):
        self.unmapped += 1

    def discover_public_address(self):
        return '198.51.100.2'

    def query_info(self, endpoint, timeout=2):
        if not self.up:
            raise network.NetworkError('no answer')
        return {'numcl': '1', 'maxcl': '4', 'cofbuild': 'test1'}


class HostWatchTests(unittest.TestCase):
    def test_router_mapping_gives_a_join_code(self):
        watch = play.HostWatch(27015, lan_only=False, net=FakeNet())
        watch.open_router()
        self.assertEqual(watch.address, '203.0.113.7:27015')
        self.assertEqual(network.parse_endpoint(watch.code), network.Endpoint('203.0.113.7', 27015))
        self.assertIn('1/4', watch.poll(True))

    def test_without_upnp_it_explains_and_still_shows_the_address(self):
        watch = play.HostWatch(27015, lan_only=False, net=FakeNet(upnp=False))
        watch.open_router()
        self.assertIn('Tailscale', watch.router)
        self.assertEqual(watch.address, '198.51.100.2:27015')

    def test_watch_unmaps_once_when_the_game_ends_or_the_app_closes(self):
        net = FakeNet()
        watch = play.HostWatch(27015, lan_only=False, net=net)
        watch.open_router()
        self.assertIn('ended', watch.poll(False))
        watch.close()
        watch.close()
        self.assertEqual(net.unmapped, 1)
        net = FakeNet()
        watch = play.HostWatch(27015, lan_only=False, net=net)
        watch.open_router()
        watch.close()
        self.assertEqual(net.unmapped, 1)

    def test_watch_reports_a_server_that_never_answers(self):
        now = [0.0]
        watch = play.HostWatch(27015, lan_only=True, net=FakeNet(up=False), clock=lambda: now[0], wait=90)
        self.assertIn('Starting', watch.poll(True))
        now[0] = 91
        self.assertIn('another program', watch.poll(True))

    def test_closing_the_app_while_hosting_asks_first(self):
        # closing removes the router port, which can drop friends who are still playing
        self.assertIsNone(play.close_warning(None))
        self.assertIsNone(play.close_warning(play.HostWatch(27015, lan_only=True, net=FakeNet())))
        watch = play.HostWatch(27015, lan_only=False, net=FakeNet())
        watch.open_router()
        self.assertIn('drop', play.close_warning(watch))
        watch.close()
        self.assertIsNone(play.close_warning(watch))

    def test_solo_never_touches_the_router(self):
        net = FakeNet()
        watch = play.HostWatch(27015, lan_only=True, net=net)
        watch.open_router()
        watch.close()
        self.assertEqual((watch.code, net.unmapped), ('', 0))


class SettingsTests(unittest.TestCase):
    def test_settings_round_trip_with_defaults(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertEqual(play.load_settings(root)['port'], 27015)
            play.save_settings(root, dict(play.load_settings(root), name='Bob', first_launch_done=True))
            self.assertEqual(play.load_settings(root)['name'], 'Bob')
            (root / play.SETTINGS).write_text('{broken')
            self.assertEqual(play.load_settings(root)['name'], '')


if __name__ == '__main__':
    unittest.main()
