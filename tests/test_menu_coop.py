"""1.3.6: Co-op in the game's main menu (the owner: "integrate the mod into the main menu somehow"). The menu's pages
(mainui menus/CoFStoryCoop.cpp) send what menus/CoFStoryCoopLogic.h builds; that header is compiled alone here and
every text compared with what the Cry of Fear Co-op app (tools/coop_play.py, cofcoop/network.py) does for the same
choices, so a game started from the menu is the game the app starts."""
import itertools
import unittest

from cofcoop import network
from cofcoop.core import CHAPTERS
from tests.native import ROOT, assert_has, run_native_cpp
from tests.test_coop_play import archive, session
from tests.test_enhanced_sections import body_of
from tools import coop_play as play

MAINUI = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/3rdparty/mainui'
LOGIC = MAINUI / 'menus/CoFStoryCoopLogic.h'
PAGES = MAINUI / 'menus/CoFStoryCoop.cpp'
MAIN = MAINUI / 'menus/Main.cpp'
OPTIONS = MAINUI / 'menus/CoFOptions.cpp'
OFFICIAL = MAINUI / 'menus/CoFCoop.cpp'


def text(path):
    return path.read_text(encoding='utf-8')


def run_cases(case, cases):
    """Runs the cases (tuples of str) through the harness; their answers, in order."""
    lines = '\n'.join('\t'.join(field.encode('utf-8').hex() for field in fields) for fields in cases) + '\n'
    out = run_native_cpp(case, 'cof_menu_coop_harness.cpp', {'logic_under_test.h': text(LOGIC), 'cases.txt': lines})
    answers = {}
    for line in out.splitlines():
        number, tab, value = line.partition('\t')
        if tab and number.isdigit():   # MSVC prints the source file's name first
            answers[int(number)] = bytes.fromhex(value).decode('utf-8')
    return [answers[i] for i in range(len(cases))]


def flag(value):
    return '1' if value else '0'


def app_start(*, solo, cont, name, password, map_name, slot, maps, places, companions, bonus, merged, torch):
    """The app's text for a start: cof_story.cfg, the session cfg (without the release lines the app sets when it opens
    the game, cof_maps and cof_chapter_start always written), then the "+map"."""
    args, config = play.host_launch(ROOT, solo=solo, name=name, map_name=map_name, players=places, password=password,
                                    companions=companions, port=27015, cont=cont, video=[], bonus_room=bonus,
                                    merged=merged, coop_flashlight=torch, maps=maps, slot=slot)
    lines = [line for line in config.splitlines() if not line.startswith(('cof_build ', 'cof_label '))]
    maps_line = next((line for line in lines if line.startswith('cof_maps ')), None)
    chapter = next((line for line in lines if line.startswith('cof_chapter_start ')), None)
    lines = [line for line in lines if line not in (maps_line, chapter)]
    lines.append(f'cof_maps "{maps_line.split()[1] if maps_line else ""}"')
    lines.append(f'cof_chapter_start "{chapter.split()[1] if chapter else ""}"')
    assert args[args.index('+map') + 1] == map_name
    return play.story_config() + '\n'.join(lines) + f'\nmap {map_name}\n'


class MenuLogicTests(unittest.TestCase):
    def test_every_text_matches_the_apps(self):
        cases, expected = [], []

        def add(fields, answer):
            cases.append(fields)
            expected.append(answer)

        add(('settings',), play.story_config())
        add(('starts',), ''.join(f'{label}\t{name}\n' for label, name in
                                 [('New game (the intro; Enter skips cutscenes)', play.NEW_GAME_MAP), *play.CHAPTER_CHOICES]))
        self.assertEqual(CHAPTERS[0][1], play.NEW_GAME_MAP)

        # the party: every combination the pages can ask for
        for solo, players, companions, fill, checkpoint in itertools.product(
                (True, False), (2, 3, 4), (0, 1, 2, 3, 4), (True, False), (0, 2, 3, 4)):
            places, ais = play.party_size(solo=solo, players=players, companions=companions, fill=fill,
                                          checkpoint_players=checkpoint or None)
            add(('party', flag(solo), str(players), str(companions), flag(fill), str(checkpoint)), f'{places} {ais}')

        # starts: Play with AI and Host, a new game, a chapter, Continue (latest, a slot), the options
        starts = []
        for solo, (map_name, cont, slot), places, companions, bonus, merged, torch, maps in itertools.product(
                (True, False),
                (('c_intro', False, ''), ('c_park3', False, ''), ('c_apartment1', True, ''), ('c_sewer3', True, 'tape2')),
                (2, 4), (0, 1, 3), (True, False), (True, False), (True, False), ('0123abcd', '')):
            starts.append(dict(solo=solo, cont=cont, name='Bob', password='' if solo else 'pass word', map_name=map_name,
                               slot=slot, maps=maps, places=places, companions=companions, bonus=bonus, merged=merged,
                               torch=torch))
        # refused alike: a bad map, too many AIs, a slot without Continue, a bad slot
        starts += [dict(starts[0], map_name='c_intro!'), dict(starts[0], companions=5), dict(starts[0], slot='tape1'),
                   dict(starts[0], cont=True, slot='Tape 1'), dict(starts[0], places=1, companions=0)]
        for start in starts:
            try:
                answer = app_start(**start)
            except ValueError:
                answer = 'REFUSED'
            add(('start', flag(start['solo']), flag(start['cont']), start['name'], start['password'], start['map_name'],
                 start['slot'], start['maps'], str(start['places']), str(start['companions']), flag(start['bonus']),
                 flag(start['merged']), flag(start['torch'])), answer)

        # joins: addresses and join codes, good and bad, as the app's Join reads them
        endpoints = ['192.168.1.20', '192.168.1.20:27016', ' Example.COM.:28000 ', 'my-host.example.org',
                     network.make_join_code(network.Endpoint('203.0.113.7', 27015)),
                     network.make_join_code(network.Endpoint('host.example.net', 1234)).lower(),
                     network.make_join_code(network.Endpoint('10.0.0.1', 27015))[:-1] + '0',
                     'COF1-AAAA-12345678', 'COF1-ABC', 'COF1-A-B-C', 'COF1-' + 'A' * 700,
                     '256.1.1.1', '1.2.3', '01.2.3.4', '1..2.3.4', '-bad.example', 'ex_ample.com', 'fe80::1',
                     '1.2.3.4:', '1.2.3.4:0', '1.2.3.4:65536', '1.2.3.4:123456', '1.2.3.4:12a', '', '   ', 'a' * 254,
                     'a' * 64 + '.com', 'b' * 63 + '.com']
        for endpoint in endpoints:
            try:
                answer = f'ok {network.parse_endpoint(endpoint)}'
            except ValueError as error:
                answer = f'error {error}'
            add(('endpoint', endpoint), answer)
            if answer.startswith('ok '):
                host, port = answer[3:].rsplit(':', 1)
                args, config = play.join_launch(ROOT, name='Bob', password='pw', endpoint=endpoint, video=[])
                self.assertEqual(args[-1], answer[3:])
                add(('join', 'Bob', 'pw', host, port), play.story_config() + config + f'connect {answer[3:]}\n')

        # the text fields: what clean_name and clean_password accept, kept; the rest dropped
        for value, limit, trim, kept in (('  Bob  ', 24, True, 'Bob'), ('Bo"b;\\x', 24, True, 'Bobx'),
                                         ('a' * 30, 24, True, 'a' * 24), ('p a s s', 64, False, 'p a s s'),
                                         ('caf\xe9', 24, True, 'caf')):
            add(('clean', value, str(limit), flag(trim)), kept)
            if trim:
                self.assertEqual(play.clean_name(kept), kept)
            else:
                self.assertEqual(play.clean_password(kept), kept)

        # what the pages refuse, with the app's message: as the app's own fields (coop_play._clean)
        for value, limit, trim, allow, label in (
                ('Bob', 24, True, False, 'Your name'), ('  Bob  ', 24, True, False, 'Your name'),
                ('Bo"b', 24, True, True, 'Your name'), ('a;b', 24, True, True, 'Your name'),
                ('back\\slash', 24, True, True, 'Your name'), ('caf\xe9', 24, True, True, 'Your name'),
                ('a' * 24, 24, True, True, 'Your name'), ('a' * 25, 24, True, True, 'Your name'),
                ('   ', 24, True, False, 'Your name'), ('', 24, True, False, 'Your name'),
                ('p\x01ss', 64, False, True, 'The password'), ('p\u00e4ssword', 64, False, True, 'The password'),
                ('pass word', 64, False, True, 'The password'), ('', 64, False, True, 'The password'),
                ('x' * 65, 64, False, True, 'The password'), (' spaced ', 64, False, True, 'The password')):
            try:
                play._clean(value.strip() if trim else value, limit, label, allow)
                answer = 'ok'
            except ValueError as error:
                answer = str(error)
            add(('field', value, str(limit), flag(trim), flag(allow), label), answer)
        add(('build',), play.VERSION)

        # checkpoints: as the app's Continue list reads them
        merged = b'merged 1\nsection c_apartment2\n'
        archives = [
            ('autosave', archive([('session.dat', session())])),
            ('tape3', archive([('players.dat', b'x' * 10), ('session.dat', session('c_sewer3', 3, ('Ann', 'Bob', 'Cid')))])),
            ('autosave', archive([('session.dat', session('cm_apartment', 2)), ('merged.txt', merged)])),
            ('autosave', archive([('session.dat', session(version=1))])),
            ('autosave', archive([('session.dat', session('C_START', 1))])),
            ('autosave', archive([('session.dat', session()), ('merged.txt', b'merged 2\nsection c_x\n')])),
            ('autosave', archive([('session.dat', session())])[:-5]),
            ('bad slot', archive([('session.dat', session())])),
            ('autosave', b'junk'),
        ]
        for slot, data in archives:
            checkpoint = play.parse_checkpoint(slot, data) if play._SLOT.match(slot) else None
            answer = 'NONE' if checkpoint is None else (
                f'{checkpoint.slot}|{checkpoint.map}|{checkpoint.players}|{checkpoint.section}|'
                f'{int(checkpoint.merged)}|' + ','.join(checkpoint.names))
            add(('checkpoint', slot, data.hex()), answer)

        for slot in ('autosave', 'tape1', 'tape5', 'tape6', 'mine'):
            add(('slot', slot), play.slot_title(slot))

        for name, names in (('Bob', 'Bob,Companion'), (' bob ', 'Bob'), ('Ann', 'Bob,Cid'), ('Ann', '')):
            checkpoint = play.Checkpoint('autosave', 'c_start', 2, tuple(n for n in names.split(',') if n))
            add(('warning', name, names), play.continue_warning(checkpoint, name) or 'NONE')

        # the invite file the app writes (MenuWatch.write_invite)
        add(('invite', 'code COF1-ABC-12345678\naddress 203.0.113.7:27015\r\nlan 192.168.1.5:27015\nother x\n'),
            'COF1-ABC-12345678|203.0.113.7:27015|192.168.1.5:27015')
        add(('invite', 'code bad value\naddress \n'), '||')

        answers = run_cases(self, cases)
        for fields, answer, want in zip(cases, answers, expected):
            self.assertEqual(answer, want, fields[:8])

    def test_the_games_own_co_op_turns_the_story_off_as_the_app_does(self):
        args, config = play.official_launch(ROOT, name='Bob', map_name='cof_campaign_01', players=3, password='pw',
                                            port=27015, video=[])
        answer, refused = run_cases(self, [('official', 'Bob', 'Bob\'s co-op', 'pw', 'cof_campaign_01', '3', '0', '120',
                                            '384', '2'),
                                           ('official', 'Bob', 'x', '', 'c_start', '3', '0', '0', '0', '2')])
        self.assertEqual(refused, 'REFUSED')
        self.assertTrue(answer.startswith(play.story_config()))
        sent = answer[len(play.story_config()):].splitlines()
        for line in config.splitlines():
            if line.startswith(('cof_build ', 'cof_label ', 'hostname ')):
                continue
            if line == 'cof_continue_slot ""':
                self.assertIn(line, sent)
                continue
            self.assertIn(line, sent)
        self.assertEqual(sent[-1], 'map cof_campaign_01')
        for line in ('cof_maps ""', 'cof_chapter_start ""', 'mp_footsteps 120', 'sv_auto_respawn_time 384',
                     'difficulty 2', 'hostname "Bob\'s co-op"'):
            self.assertIn(line, sent)

    def test_the_games_own_co_op_offers_every_map_the_app_did(self):
        # 1.3.9 (a player: "it would be great to have the option to select a specific level"): the app's own co-op
        # page (1.3.3 to 1.3.6) offered the co-op campaign's later parts; the menu's list had only its first map
        listed = text(OFFICIAL)
        listed = listed[listed.index('s_cofCoopCampaigns[] ='):listed.index('#define COF_COOP_NUM_CAMPAIGNS')]
        maps = [map_name for _, map_name in play.OFFICIAL_CHOICES]
        answers = run_cases(self, [('official', 'Bob', 'x', '', m, '2', '0', '120', '384', '2') for m in maps])
        for map_name, answer in zip(maps, answers):
            self.assertIn(f'{{ "{map_name}",', listed)
            self.assertEqual(answer.splitlines()[-1], f'map {map_name}')


class MenuWiringTests(unittest.TestCase):
    """The pages send what the logic builds, from the right places in the main menu."""

    def test_co_op_sits_under_new_game_and_opens_its_own_list(self):
        main = text(MAIN)
        init = body_of(MAIN, 'void CMenuMain::InitCoF( void )')
        self.assertLess(init.index('AddItem( newGame );'), init.index('AddItem( cofCoop );'))
        self.assertLess(init.index('AddItem( cofCoop );'), init.index('AddItem( saveRestore );'))
        assert_has(self, 'cofCoop.onReleased = VoidCb( &CMenuMain::CoFCoopCb );', init)
        assert_has(self, 'CoFSetLevel( COF_LEVEL_COOP );', body_of(MAIN, 'void CMenuMain::CoFCoopCb()'))
        layout = body_of(MAIN, 'void CMenuMain::VidInitCoF( bool connected )')
        for item in ('cofStoryHost', 'cofJoinServer', 'cofStoryContinue', 'cofStorySolo', 'cofHostServer'):
            assert_has(self, f'{item}.SetVisibility( lvlCoop );', layout)
        assert_has(self, 'cofCoop.SetVisibility( lvlMain );', layout)   # never in the pause list
        assert_has(self, 'cofBack.SetVisibility( lvlExtras || lvlCoop );', layout)
        # the list in the owner's order, then Back; Extras keeps Unlockables, Links and Credits
        order = layout[layout.index('CMenuPicButton *order[] ='):layout.index('const size_t count')]
        names = ['&cofStoryHost', '&cofJoinServer', '&cofStoryContinue', '&cofStorySolo', '&cofHostServer', '&cofBack']
        self.assertEqual([order.index(n) for n in names], sorted(order.index(n) for n in names))
        self.assertNotIn('cofJoinServer.SetVisibility( lvlExtras );', layout)
        assert_has(self, 'ADD_COMMAND( menu_cof_coop_list, UI_CoFMainCoopList );', main)
        for name, page in (('Host co-op', 'UI_CoFStoryHost_Menu'), ('Continue co-op', 'UI_CoFStoryContinue_Menu'),
                           ('Play with AI', 'UI_CoFStorySolo_Menu'), ('Join co-op', 'UI_CoFJoin_Menu'),
                           ("Cry of Fear's own co-op", 'UI_CoFHost_Menu')):
            assert_has(self, f'L( "{name}" )', init)
            assert_has(self, page, init)

    def test_the_pages_are_one_co_op_window_and_out_of_extras(self):
        options = text(OPTIONS)
        coop = options[options.index('static const cofTab_t g_cofCoopTabs[]'):]
        coop = coop[:coop.index('};')]
        for title in ('"HOST CO-OP",     UI_CoFStoryHost_Menu', '"JOIN CO-OP",     UI_CoFJoin_Menu',
                      '"CONTINUE CO-OP", UI_CoFStoryContinue_Menu', '"PLAY WITH AI",   UI_CoFStorySolo_Menu',
                      '"ORIGINAL CO-OP", UI_CoFHost_Menu'):
            assert_has(self, title, coop)
        extras = options[options.index('static const cofTab_t g_cofExtrasTabs[]'):]
        extras = extras[:extras.index('};')]
        self.assertNotIn('UI_CoFHost_Menu', extras)
        self.assertNotIn('UI_CoFJoin_Menu', extras)
        assert_has(self, '{ "CO-OP",       g_cofCoopTabs,     (int)V_ARRAYSIZE( g_cofCoopTabs ) },', options)
        # each page's panel title is its tab's
        pages, official = text(PAGES), text(OFFICIAL)
        for title in ('"HOST CO-OP"', '"PLAY WITH AI"', '"CONTINUE CO-OP"'):
            assert_has(self, title, pages)
        assert_has(self, 'SetPanel( "ORIGINAL CO-OP", 900, 560 );', official)
        assert_has(self, 'SetPanel( "JOIN CO-OP", 760, 400 );', official)

    def test_each_start_sends_the_logics_text_in_one_write(self):
        start = body_of(PAGES, 'bool CMenuCoFStoryStart::Start( void )')
        assert_has(self, 'CoFStory_PartySize( m_bSolo, wanted, ais, fill.bChecked, 0, &places, &count );', start)
        assert_has(self, 'CoFStory_StartCommands( text, sizeof( text ), &s )', start)
        assert_has(self, 's.cont = false;', start)
        assert_has(self, 's.maps = EngFuncs::GetCvarString( COF_STORY_CVAR_MAPS );', start)
        self.assertLess(start.index('UI_CoFStoryCanStart( )'), start.index('UI_CoFStoryRun( what, text );'))
        cont = body_of(PAGES, 'bool CMenuCoFStoryContinue::Start( bool solo )')
        # Continue keeps the checkpoint's places and merged maps, as the app's
        assert_has(self, 'CoFStory_PartySize( solo, c->players, ais, fill.bChecked, c->players, &places, &count );', cont)
        assert_has(self, 's.merged = c->merged;', cont)
        assert_has(self, 's.cont = true;', cont)
        assert_has(self, 's.slot = c->slot;', cont)
        run = body_of(PAGES, 'static void UI_CoFStoryRun( const char *what, const char *text )')
        assert_has(self, 'EngFuncs::ClientCmd( false, text );', run)
        join = body_of(OFFICIAL, 'bool CMenuCoFJoin::Connect( void )')
        self.assertLess(join.index('CoFStory_ParseEndpoint('), join.index('CoFStory_JoinCommands('))
        assert_has(self, 'EngFuncs::ClientCmd( false, text );', join)
        official = body_of(OFFICIAL, 'bool CMenuCoFHost::Start( void )')
        assert_has(self, 'CoFStory_OfficialCommands( cmd, sizeof( cmd ), &o )', official)
        self.assertNotIn('"Story co-op"', text(OFFICIAL))   # the game's own campaign, not the story

    def test_extras_opens_its_own_window_and_continue_shows_the_options(self):
        # the review of 1.3.6: Extras still opened UI_CoFHost_Menu (now a CO-OP tab), and Unlockables, Links and
        # Credits could not be reached with the theme on
        extras = body_of(MAIN, 'void CMenuMain::CoFExtrasCb()')
        assert_has(self, 'UI_CoFUnlockables_Menu();', extras)
        self.assertNotIn('UI_CoFHost_Menu', extras)
        # Continue: bonus room and flashlight on the page, as the app's Continue shows them; merged maps the checkpoint's
        cont = body_of(PAGES, 'bool CMenuCoFStoryContinue::Start( bool solo )')
        assert_has(self, 's.bonusRoom = bonus.bChecked;', cont)
        assert_has(self, 's.flashlight = torch.bChecked;', cont)
        # only the folder the engine loads from, every file sorted before the list is cut
        update = body_of(PAGES, 'void CMenuCoFStoryCheckpointModel::Update( void )')
        self.assertEqual(update.count('EngFuncs::GetFilesList('), 1)
        self.assertLess(update.index('EngFuncs::CompareFileTime('), update.index('CoFStory_ParseCheckpoint('))
        # a refused field is refused, not changed; a game opened without the app still sends the join check's version
        for page in ('bool CMenuCoFStoryStart::Start( void )', 'bool CMenuCoFStoryContinue::Start( bool solo )'):
            body = body_of(PAGES, page)
            self.assertLess(body.index('UI_CoFStoryFieldsValid('), body.index('CoFStory_StartCommands('))
        self.assertLess(body_of(OFFICIAL, 'bool CMenuCoFJoin::Connect( void )').index('CoFStory_FieldValid('),
                        body_of(OFFICIAL, 'bool CMenuCoFJoin::Connect( void )').index('CoFStory_JoinCommands('))
        assert_has(self, 'EngFuncs::ClientCmd( false, "cof_build " COF_STORY_BUILD "\\n" );',
                   body_of(PAGES, 'static void UI_CoFStoryRun( const char *what, const char *text )'))
        assert_has(self, 'EngFuncs::ClientCmd( false, "cof_build " COF_STORY_BUILD "\\n" );',
                   body_of(OFFICIAL, 'bool CMenuCoFHost::Start( void )'))

    def test_the_app_and_the_menu_agree_on_names(self):
        pages = text(PAGES)
        assert_has(self, f'#define COF_STORY_INVITE          "{play.INVITE}"', pages)
        assert_has(self, '#define COF_STORY_CVAR_MAPS       "cof_menu_maps"', pages)
        _, config = play.menu_launch(ROOT, name='Bob', port=27015, maps='0123abcd', video=[])
        assert_has(self, 'set cof_menu_maps "0123abcd"', config)
        assert_has(self, 'EngFuncs::CvarRegister( COF_STORY_CVAR_MAPS,       "", 0 );', pages)
