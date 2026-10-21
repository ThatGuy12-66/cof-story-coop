"""The tester app's start options, checks and checkpoint reading (tools/coop_play.py)."""
import re
import struct
import tempfile
import unittest
from pathlib import Path

from cofcoop import network
from tests.native import ENGINE_SERVER
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
                                     password='s3cret', companions=2, port=27015, cont=False, video=[])
        self.assertEqual(args[0], str(runtime / 'CoFLaunchApp.exe'))
        self.assertEqual(args[-2:], ['+map', 'c_start'])
        for absent in ('-console', 's3cret', '-windowed', 'rcon'):
            self.assertFalse(any(absent in a for a in args), absent)
        self.assertLess(args.index(play.STORY_CFG), args.index(play.SESSION_CFG))
        for line in ('maxplayers 3', 'sv_lan 0', 'sv_password "s3cret"', 'cof_companion 2', f'cof_build {play.VERSION}',
                     'cof_continue 0', 'hostname "Bob\'s game"', 'name "Bob"'):
            self.assertIn(line + '\n', cfg)
        self.assertNotIn('rcon', cfg)
        args, cfg = play.host_launch(runtime, solo=True, name='Bob', map_name='c_intro', players=4, password='x',
                                     companions=3, port=27015, cont=True, video=[])
        for line in ('maxplayers 4', 'sv_lan 1', 'sv_password ""', 'cof_companion 3', 'cof_continue 1'):
            self.assertIn(line + '\n', cfg)
        # the AIs fit the places: solo has at least one, and nobody has more than the places left
        for solo, players, companions in ((True, 2, 0), (True, 2, 2), (False, 3, 3), (False, 2, -1)):
            with self.subTest(solo=solo, players=players, companions=companions), self.assertRaises(ValueError):
                play.host_launch(runtime, solo=solo, name='Bob', map_name='c_start', players=players, password='',
                                 companions=companions, port=27015, cont=False, video=[])

    def test_party_size(self):
        # (maxplayers, AI companions): solo is you plus 1-3 AIs; a host's AIs fill the empty places
        self.assertEqual(play.party_size(solo=True, players=2, companions=3, fill=False, checkpoint_players=None), (4, 3))
        self.assertEqual(play.party_size(solo=True, players=2, companions=1, fill=False, checkpoint_players=None), (2, 1))
        self.assertEqual(play.party_size(solo=False, players=4, companions=1, fill=True, checkpoint_players=None), (4, 3))
        self.assertEqual(play.party_size(solo=False, players=3, companions=1, fill=False, checkpoint_players=None), (3, 0))
        # Continue keeps the checkpoint's places (the engine refuses another count)
        self.assertEqual(play.party_size(solo=True, players=2, companions=3, fill=False, checkpoint_players=2), (2, 1))
        self.assertEqual(play.party_size(solo=True, players=2, companions=1, fill=False, checkpoint_players=4), (4, 1))
        self.assertEqual(play.party_size(solo=False, players=2, companions=1, fill=True, checkpoint_players=3), (3, 2))
        self.assertEqual(play.party_size(solo=True, players=2, companions=9, fill=False, checkpoint_players=None), (4, 3))
        self.assertEqual(play.DEFAULT_SETTINGS['companions'], 1)

    def test_bonus_room_is_the_hosts_choice_and_shut_by_default(self):
        _, cfg = play.host_launch(Path('copy'), solo=True, name='Bob', map_name='c_start', players=2, password='',
                                  companions=1, port=27015, cont=False, video=[])
        self.assertIn('cof_bonus_room 0\n', cfg)                       # shut, whatever the tester's own game says
        _, cfg = play.host_launch(Path('copy'), solo=False, name='Bob', map_name='c_start', players=2, password='',
                                  companions=0, port=27015, cont=False, video=[], bonus_room=True)
        self.assertIn('cof_bonus_room 1\n', cfg)
        self.assertIs(play.DEFAULT_SETTINGS['bonus_room'], False)

    def test_merged_maps_are_the_hosts_choice_and_on_by_default(self):
        _, cfg = play.host_launch(Path('copy'), solo=True, name='Bob', map_name='c_start', players=2, password='',
                                  companions=1, port=27015, cont=False, video=[])
        self.assertIn('cof_merged_maps 1\n', cfg)
        _, cfg = play.host_launch(Path('copy'), solo=True, name='Bob', map_name='c_start', players=2, password='',
                                  companions=1, port=27015, cont=False, video=[], merged=False)
        self.assertIn('cof_merged_maps 0\n', cfg)
        self.assertIs(play.DEFAULT_SETTINGS['merged_maps'], True)

    def test_the_coop_flashlight_is_the_hosts_choice_and_off_by_default(self):
        # the original story has only Simon's phone; F's free torch is Cry of Fear's co-op mode
        _, cfg = play.host_launch(Path('copy'), solo=True, name='Bob', map_name='c_start', players=2, password='',
                                  companions=1, port=27015, cont=False, video=[])
        self.assertIn('cof_coop_flashlight 0\n', cfg)
        _, cfg = play.host_launch(Path('copy'), solo=False, name='Bob', map_name='c_start', players=2, password='',
                                  companions=0, port=27015, cont=False, video=[], coop_flashlight=True)
        self.assertIn('cof_coop_flashlight 1\n', cfg)
        self.assertIs(play.DEFAULT_SETTINGS['coop_flashlight'], False)

    def test_story_config_removes_the_test_tools(self):
        cfg = play.story_config()
        for line in ('cof_story_coop 1', 'cof_story_trace 1', 'sv_cheats 0', 'sv_enttools_enable 0', 'cof_test_gun 0',
                     'fps_max 60'):
            self.assertIn(line + '\n', cfg)
        self.assertNotIn('rcon_password', cfg)

    def test_story_config_sends_movement_often_enough(self):
        # 1.1.9: a 60 fps host sends every frame at 100 updates a second (at 60 it skipped some), so partners move
        # smoothly; each value at the engine's limit (MAX_RATE, cl_cmdrate 100, MAX_UPDATERATE 102, MAX_EX_INTERP)
        lines = play.story_config().splitlines()
        for line in ('rate 100000', 'cl_cmdrate 100', 'cl_updaterate 100', 'sv_maxupdaterate 100', 'ex_interp 0.1',
                     'fps_max 60'):
            self.assertIn(line, lines)
        for key in ('rate', 'cl_cmdrate', 'cl_updaterate', 'sv_maxupdaterate', 'ex_interp', 'fps_max', 'sv_fps'):
            self.assertLessEqual(sum(line.split()[0] == key for line in lines), 1, key)
        self.assertFalse(any(line.startswith('sv_fps') and line != 'sv_fps 0' for line in lines))  # cof_skip needs 0

    def test_story_config_rates_are_engine_cvars_within_their_limits(self):
        engine = ENGINE_SERVER.parent
        client_h = (engine / 'client/client.h').read_text(encoding='latin-1')
        cl_main = (engine / 'client/cl_main.c').read_text(encoding='latin-1')
        netchan = (engine / 'common/netchan.h').read_text(encoding='latin-1')
        sv_main = (engine / 'server/sv_main.c').read_text(encoding='latin-1')
        value = {line.split()[0]: float(line.split()[1]) for line in play.story_config().splitlines()
                 if line.split()[0] in ('rate', 'cl_cmdrate', 'cl_updaterate', 'sv_maxupdaterate', 'ex_interp')}
        for name, source in (('rate', cl_main), ('cl_cmdrate', cl_main), ('cl_updaterate', cl_main),
                             ('sv_maxupdaterate', sv_main)):
            self.assertRegex(source, r'CVAR_DEFINE_AUTO\( %s,' % name)
        self.assertIn('CVAR_DEFINE( cl_interp, "ex_interp",', cl_main)
        limit = lambda text, name: float(re.search(r'#define %s\s+([0-9.]+)f' % name, text).group(1))
        self.assertLessEqual(value['rate'], limit(netchan, 'MAX_RATE'))  # sv_client.c bounds a client's rate by it
        self.assertLessEqual(value['cl_updaterate'], limit(client_h, 'MAX_UPDATERATE'))
        self.assertGreaterEqual(value['cl_updaterate'], limit(client_h, 'MIN_UPDATERATE'))
        self.assertIn('else if( cl_cmdrate.value > 100.0f )', cl_main)
        self.assertLessEqual(value['cl_cmdrate'], 100)
        self.assertLessEqual(value['ex_interp'], limit(client_h, 'MAX_EX_INTERP'))
        self.assertGreaterEqual(value['ex_interp'], 1.0 / value['cl_updaterate'])  # CL_ComputeClientInterpolationAmount
        self.assertGreaterEqual(value['sv_maxupdaterate'], value['cl_updaterate'])  # SV_CheckUpdateRate caps at it

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
        self.assertEqual(play.clean_port('65534'), 65534)
        for bad in ('80', '70000', 'abc', '65535'):   # 1.1.8 (L14.5): 65535's -clientport would be 65536
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
        # 1.1.8: chapter 5 starts where it begins in the story, at the end of c_subwaysick2
        self.assertIn(('Chapter 5 — Leaving this for good', 'c_subwaysick2'), play.CHAPTER_CHOICES)

    def test_a_chapter_start_asks_for_its_kit(self):
        def config(map_name, cont):
            return play.host_launch(Path('copy'), solo=True, name='Test', map_name=map_name, players=2, password='',
                                    companions=1, port=27015, cont=cont, video=[])[1]
        self.assertIn('cof_chapter_start c_attic\n', config('c_attic', False))
        self.assertNotIn('cof_chapter_start', config('c_intro', False))
        self.assertNotIn('cof_chapter_start', config('c_attic', True))


class ReleaseTests(unittest.TestCase):
    def test_the_release_is_1_1_5_and_updates_1_1_4(self):
        # 1.1.1 (J), 1.1.2 (the blur), 1.1.3 and 1.1.4 (AMD graphics), 1.1.5 (a join crash), 1.1.6 (level changes), 1.1.7 (faster
        # level changes), 1.1.8 (the debug pass), 1.1.9 (the polish) and 1.2.0 (the bonus kit) keep 1.1's version, so 1.1 to 1.2.0 players join
        self.assertEqual((play.VERSION, play.VERSION_LABEL), ('1.1', '1.2.0'))
        self.assertGreater(play.BUILD, 14)   # 1.1.9 was build 14: its copies must be offered the update

    def test_bug_reports_go_to_the_public_discord(self):
        self.assertEqual(play.REPORT_URL, 'https://discord.gg/4uwV6jmmY7')


class HostCheckTests(unittest.TestCase):
    def test_check_host_refuses_other_or_missing_build(self):
        for info in ({}, {'cofbuild': 'test1'}, {'cofbuild': 'test2'}, {'cofbuild': '1.0'}):
            with self.subTest(info=info), self.assertRaisesRegex(ValueError, 'different version'):
                play.check_host(dict(info, numcl='1', maxcl='2'), password='')
        play.check_host({'cofbuild': '1.1', 'numcl': '1', 'maxcl': '2', 'password': '0'}, password='')

    def test_check_host_full_or_needs_a_password(self):
        with self.assertRaisesRegex(ValueError, 'full'):
            play.check_host({'cofbuild': '1.1', 'numcl': '2', 'maxcl': '2'}, password='')
        with self.assertRaisesRegex(ValueError, 'password'):
            play.check_host({'cofbuild': '1.1', 'numcl': '1', 'maxcl': '2', 'password': '1'}, password='')


class MergedMapsCheckTests(unittest.TestCase):
    """1.1.8 (L8): the join check compared only cofbuild; a guest without the host's merged maps (or another
    version of them) failed at the first merged level."""

    MAPS = {'maps/cm_apartment1.bsp': '1' * 64, 'maps/cm_apartment1.sections': '2' * 64}

    def test_the_tag_names_the_merged_maps_installed(self):
        tag = play.merged_maps_tag(self.MAPS)
        self.assertRegex(tag, r'\A[0-9a-f]{8}\Z')
        self.assertEqual(tag, play.merged_maps_tag(dict(reversed(list(self.MAPS.items())))))
        self.assertNotEqual(tag, play.merged_maps_tag(dict(self.MAPS, **{'maps/cm_apartment1.bsp': '3' * 64})))
        self.assertNotEqual(tag, play.merged_maps_tag({}))
        self.assertEqual(play.merged_maps_tag(None), play.merged_maps_tag({}))

    def test_the_host_sends_it_only_with_merged_maps_on(self):
        tag = play.merged_maps_tag(self.MAPS)
        for merged, maps, expected in ((True, tag, f'cof_maps {tag}\n'), (False, tag, None), (True, '', None),
                                       (True, 'x;quit', None)):
            with self.subTest(merged=merged, maps=maps):
                _, cfg = play.host_launch(Path('copy'), solo=False, name='Bob', map_name='c_start', players=2,
                                          password='', companions=0, port=27015, cont=False, video=[],
                                          merged=merged, maps=maps)
                if expected:
                    self.assertIn(expected, cfg)
                else:
                    self.assertNotIn('cof_maps', cfg)
                self.assertIn(f'cof_build {play.VERSION}\n', cfg)   # 1.1.x guests still compare only this

    def test_a_guest_with_other_merged_maps_is_told_to_update(self):
        ours = play.merged_maps_tag(self.MAPS)
        host = {'cofbuild': play.VERSION, 'numcl': '1', 'maxcl': '2', 'password': '0'}
        with self.assertRaisesRegex(ValueError, 'merged maps differ.*Update'):
            play.check_host(dict(host, cofmaps=play.merged_maps_tag({})), password='', maps=ours)
        play.check_host(dict(host, cofmaps=ours), password='', maps=ours)
        play.check_host(host, password='', maps=ours)   # a 1.1.7 host, or merged maps off: not checked
        play.check_host(dict(host, cofmaps=ours), password='')   # no tag of our own: not checked


class CheckpointTests(unittest.TestCase):
    def test_reads_map_players_and_names(self):
        cp = play.parse_checkpoint('autosave', archive([('global.dat', b'x' * 10), ('session.dat', session())]))
        self.assertEqual((cp.slot, cp.map, cp.players, cp.names), ('autosave', 'c_apartment1', 2, ('Bob', 'Companion')))

    def test_reads_the_merged_mode_and_the_original_map_of_a_merged_save(self):
        merged = archive([('session.dat', session('cm_apartment3')), ('merged.txt', b'merged 1\nsection c_apartment4\n')])
        cp = play.parse_checkpoint('autosave', merged)
        self.assertEqual((cp.map, cp.merged, cp.section, cp.label), ('cm_apartment3', True, 'c_apartment4', 'c_apartment4'))
        # a save from before merged maps: unmerged, labelled by its map
        cp = play.parse_checkpoint('autosave', archive([('session.dat', session('c_apartment4'))]))
        self.assertEqual((cp.merged, cp.section, cp.label), (False, '', 'c_apartment4'))
        for junk in (b'merged maybe\nsection x\n', b'merged 1\n', b'merged 1\nsection ../x\n', b'\xff' * 300):
            with self.subTest(junk=junk[:20]):
                cp = play.parse_checkpoint('autosave', archive([('session.dat', session('cm_apartment3')), ('merged.txt', junk)]))
                self.assertEqual((cp.merged, cp.label), (False, 'cm_apartment3'))

    def test_continue_keeps_the_saves_mode_and_a_new_game_takes_the_checkbox(self):
        merged = play.Checkpoint('autosave', 'cm_apartment3', 2, (), merged=True, section='c_apartment4')
        older = play.Checkpoint('autosave', 'c_apartment4', 2, ())
        self.assertIs(play.merged_for_start(merged, checkbox=False), True)
        self.assertIs(play.merged_for_start(older, checkbox=True), False)
        self.assertIs(play.merged_for_start(None, checkbox=True), True)
        self.assertIs(play.merged_for_start(None, checkbox=False), False)

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
        return {'numcl': '1', 'maxcl': '4', 'cofbuild': '1.0'}


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

    def test_a_missed_answer_after_the_game_is_up_keeps_the_last_status(self):
        # a level change can make one status query time out: it is not the game starting again
        net = FakeNet()
        watch = play.HostWatch(27015, lan_only=True, net=net)
        up = watch.poll(True)
        self.assertIn('1/4', up)
        net.up = False
        self.assertEqual(watch.poll(True), up)

    def test_status_names_the_ai_companions(self):
        class Bots(FakeNet):
            def query_info(self, endpoint, timeout=2):
                return {'numcl': '1', 'maxcl': '4', 'bots': '3', 'cofbuild': '1.0'}
        status = play.HostWatch(27015, lan_only=True, net=Bots()).poll(True)
        self.assertIn('1 player and 3 AI companions (4 places)', status)

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
