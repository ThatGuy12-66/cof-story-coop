# First Tester Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One zip with one exe that sets up a tester's own co-op copy of Cry of Fear and then lets them play solo with the AI companion, host friends (UPnP, join code) or join a friend, with every developer tool removed.

**Architecture:** The existing setup/join app (`tools/friend_setup.py`) keeps the install logic and gains update-in-place, the inventory art and shortcuts. A new pure module (`tools/coop_play.py`) builds every command line and config, reads checkpoints and checks hosts. A new Tk app (`tools/coop_app.py`) is the window and the PyInstaller entry point, with the engine payload packed inside the exe. Two small engine changes: a `cofbuild` key in the server's info reply, and `cof_continue` (load the latest checkpoint once the host has joined).

**Tech Stack:** Python 3.13 (miniconda `python`), Tk/ttk, PyInstaller 6.19 (installed for `python`), the Xash3D FWGS engine (C, MSVC via `tools\build_enhanced.cmd`), unittest plus native C harnesses (`tests/native.py`).

**Spec:** `docs/superpowers/specs/2026-10-15-tester-package-design.md`

## Global Constraints

- Version: `VERSION = "test1"`, label `"Test 1"`, `BUILD = 1`. Zip `CoF-Story-Coop-Test1.zip`; exe `CoF Story Co-op.exe`; default copy `%LOCALAPPDATA%\CoF-Story-Coop`; shortcut name `Cry of Fear Co-op`.
- The Steam game folder is never written; `D:\SteamLibrary\steamapps\common\Cry of Fear` is not touched at all (smoke tests copy from `baseline\Cry of Fear`). `runtimes\*` and the disposable copies under `evidence\story-live-20261012` are not changed.
- No password on any command line; passwords only in `cryoffear\cof_story_session.cfg`.
- What the tester's game runs: no `-console`, no `rcon_password`, `cof_test_gun 0`, `sv_cheats 0`, `sv_enttools_enable 0`, `fps_max 60`, `cof_story_trace 1` (log only).
- Never launch a game without the user's go-ahead (AGENTS.md). Commits only on the branches named in Task 1.
- Python tests run as `python -m unittest ...` (a hook blocks `python script.py`); run tools as `python -m tools.<name>`.
- Free disk: C: had 14 GB on 2026-10-15; one smoke copy (~5 GB) only.

## Review Focus

1. A tester joins a host that runs another build, or an older build with no `cofbuild` key: Join must refuse with "different version" before starting the game (Task 4, `test_check_host_refuses_other_or_missing_build`).
2. The host's port is busy or the game fails to start: the hosting panel must say so after the wait instead of "Starting..." forever (Task 4, `test_watch_reports_a_server_that_never_answers`).
3. The tester closes the app (or the game ends) while hosting: the UPnP mapping is removed exactly once (Task 4, `test_watch_unmaps_once_when_the_game_ends_or_the_app_closes`).
4. Update clicked while that copy's game runs: refused, nothing changed (Task 5, `test_update_refuses_while_the_game_runs`).
5. Continue with a different name than the checkpoint's: the app warns that items return only under the saved name (Task 4, `test_continue_warns_about_another_name`).

---

### Task 1: Commit the current work and branch

**Files:** the uncommitted files in the three repos (see `git status` in each); `docs/superpowers/specs/2026-10-15-tester-package-design.md`, this plan.

- [ ] **Step 1: FreeVGUI** (`upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/3rdparty/freevgui`, branch `story-coop`)

```bash
git add controls/image.h platform/xash3d-fwgs/cofinventory.cpp platform/xash3d-fwgs/cofinventory_logic.h platform/xash3d-fwgs/support.h platform/xash3d-fwgs/surface.cpp
git commit -m "UI pass: menu text stays in place, 12-slot grid aligned, full inventory background" (+ trailer)
```

- [ ] **Step 2: Engine** (`…/xash3d-fwgs-4857…`, branch `companion-ai`)

```bash
git add engine/server/sv_client.c engine/server/sv_cof_brain.h engine/server/sv_cof_companion.c engine/server/sv_cof_nav.h 3rdparty/freevgui
git commit -m "Companion routes over the node graph; RCON echo hides the password; FreeVGUI UI pass" (+ trailer)
git switch -c tester-package
```

- [ ] **Step 3: Launcher** (branch `companion-ai`)

```bash
git add AGENT_NOTES.md docs/campaign-test-guide.md docs/companion.md tests/engine/cof_brain_test.c tests/engine/cof_companion_harness.c tests/engine/cof_inventory_logic_test.cpp tests/test_enhanced_companion.py tests/test_inventory12_art.py tools/Start-CampaignTest.ps1 tools/make_inventory12_art.py tests/engine/cof_nav_test.c tests/engine/cof_nav_real_test.c
git commit -m "Companion routes and the UI pass: tests, art tool, docs, notes" (+ trailer)
git switch -c tester-package
git add docs/superpowers/specs/2026-10-15-tester-package-design.md docs/superpowers/plans/2026-10-15-tester-package.md
git commit -m "Tester package: design and plan" (+ trailer)
```

- [ ] **Step 4: Check** `git status --short` is empty in all three (untracked evidence is ignored).

---

### Task 2: Engine: `cofbuild` in the info reply, `cof_continue`

**Files:**
- Modify: `engine/server/sv_client.c` (`SV_Info`), `engine/server/sv_cof_story.c` (`SV_CoF_StoryInit`), `engine/server/sv_cof_session.inc` (statics, `SV_CoF_SessionFrame`, `SV_CoF_SessionInit`)
- Test: `tests/engine/cof_session_lifecycle_test.c`, `tests/test_coop_engine.py` (new)

**Interfaces:**
- Produces: server cvar `cof_build` (string, default empty; info key `cofbuild` when set); server cvar `cof_continue` (0/1, cleared after one attempt); log lines `cofstory phase=session-continue result=queued|refused`.

- [ ] **Step 1: Failing harness scenario.** In `tests/engine/cof_session_lifecycle_test.c` add after the `static const char *argv[3];` line:

```c
typedef struct { float value; } convar_t;
static convar_t continue_var, *cof_continue = &continue_var;
static int profont;
static void Cvar_DirectSet( convar_t *var, const char *value ) { var->value = (float)atof( value ); }
```

change the ProFont stub to `{ profont++; return 1; }`, and before `puts( "session lifecycle scenarios passed" )` add:

```c
	/* Continue (the tester app): the latest checkpoint once the host has joined, once */
	loads = 0; cof_session_pending = NULL; usable = 1; result = 1; profont = 0;
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1;
	continue_var.value = 1; clients[0].state = cs_connected; sv.time = 100;
	SV_CoF_SessionFrame(); assert( !loads );                 /* the first two seconds */
	sv.time = 103; SV_CoF_SessionFrame(); assert( !loads );  /* the host is still joining (the wipe check moves the settle to 104) */
	clients[0].state = cs_spawned; sv.time = 105; SV_CoF_SessionFrame();
	assert( loads == 1 && !strcmp( last_slot, "latest" ) && continue_var.value == 0 && !profont );
	sv.time = 110; SV_CoF_SessionFrame(); assert( loads == 1 );   /* once */
	/* refused: the host is told, and it is not retried */
	continue_var.value = 1; result = 0; sv.time = 120; SV_CoF_SessionFrame();
	assert( loads == 2 && profont == 1 && continue_var.value == 0 );
	sv.time = 130; SV_CoF_SessionFrame(); assert( loads == 2 ); result = 1;
```

Create `tests/test_coop_engine.py`:

```python
"""Engine parts of the tester package: the build key in the info reply."""
import re
import unittest

from tests.native import ENGINE_SERVER


class CoopEngineTests(unittest.TestCase):
    def test_info_reply_carries_the_build(self):
        source = (ENGINE_SERVER / 'sv_client.c').read_text()
        body = source[source.index('static void SV_Info('):source.index('static void SV_ConnectNatClient')]
        self.assertIn('Cvar_VariableString( "cof_build" )', body)
        self.assertLess(body.index('"cofbuild"'), body.index('"host"'))   # before the hostname, which is cut to fit
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertRegex(story, r'Cvar_Get\( "cof_build", "", 0,')

    def test_continue_is_registered(self):
        session = (ENGINE_SERVER / 'sv_cof_session.inc').read_text()
        self.assertRegex(session, r'cof_continue = Cvar_Get\( "cof_continue", "0", 0,')
```

- [ ] **Step 2: Run, expect failures**

`python -m unittest tests.test_enhanced_session tests.test_coop_engine -v` → the lifecycle harness fails to compile (`cof_continue` unused is fine, but assert `loads == 1` fails) and both source checks fail.

- [ ] **Step 3: Implement.** `sv_cof_session.inc`, after `static int cof_session_deferred; …` add `static convar_t *cof_continue; // the tester app's Continue: load the latest checkpoint once the host has joined`. In `SV_CoF_SessionFrame`, right before `if( sv.time >= cof_session_wipe_after && !SV_CoF_TransitionActive() && !cof_session_pending )`:

```c
	// Continue (the tester app's start option): the latest checkpoint, once,
	// when the listen host has joined; after the same two-second settle.
	if( cof_continue && cof_continue->value && sv.time >= cof_session_wipe_after && !cof_session_pending &&
		!SV_CoF_TransitionActive() && svs.clients[0].state == cs_spawned )
	{
		qboolean queued;
		Cvar_DirectSet( cof_continue, "0" );
		queued = SV_CoF_SessionLoad( NULL );
		Con_Printf( "cofstory phase=session-continue result=%s\n", queued ? "queued" : "refused" );
		if( !queued ) SV_CoF_RetailProFont( &svs.clients[0], 5, "The checkpoint could not be loaded: this map starts from its beginning.", true );
		return;
	}
```

In `SV_CoF_SessionInit` add `cof_continue = Cvar_Get( "cof_continue", "0", 0, "story co-op: load the latest checkpoint once the host has joined (the tester app's Continue)" );`.

`sv_cof_story.c` `SV_CoF_StoryInit`, after the `cof_story_coop` Cvar_Get: `Cvar_Get( "cof_build", "", 0, "story co-op: the tester build this server runs, sent as cofbuild in its info reply" );`.

`sv_client.c` `SV_Info`, in the `else` block: declare `const char *build = Cvar_VariableString( "cof_build" );` with the other locals, and after the `password` key:

```c
		if( build[0] ) Info_SetValueForKey( s, "cofbuild", build, sizeof( s )); // story co-op: the tester app checks it before a join
```

- [ ] **Step 4: Run, expect pass:** same command → OK.
- [ ] **Step 5: Commit** engine (`sv_client.c`, `sv_cof_story.c`, `sv_cof_session.inc`: "Tester package: cofbuild in the info reply, cof_continue") and launcher (harness + test: "Test cof_continue and the cofbuild key").

---

### Task 3: `network.query_info`

**Files:** Modify `cofcoop/network.py` (after `query_server`); Test `tests/test_network.py`.

**Interfaces:**
- Produces: `network.parse_info_reply(payload: bytes) -> dict[str, str]`; `network.query_info(endpoint: Endpoint, timeout: float = 2) -> dict[str, str]` (raises `NetworkError`).

- [ ] **Step 1: Failing tests** (append to `tests/test_network.py`, class `InfoQueryTests`):

```python
import socket, threading
from cofcoop import network

class InfoQueryTests(unittest.TestCase):
    def test_parse_info_reply(self):
        info = network.parse_info_reply(b'info\n\\p\\49\\map\\c_start\\numcl\\1\\maxcl\\2\\password\\0\\cofbuild\\test1\\host\\Bob\'s game')
        self.assertEqual((info['map'], info['cofbuild'], info['host']), ('c_start', 'test1', "Bob's game"))
        with self.assertRaisesRegex(network.NetworkError, 'different version'):
            network.parse_info_reply(b"info\nBob's game: wrong version\n")
        for bad in (b'xyz', b'info\n\\odd'):
            with self.subTest(bad=bad), self.assertRaises(network.NetworkError):
                network.parse_info_reply(bad)

    def test_query_info_against_a_loopback_server(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
            server.bind(('127.0.0.1', 0))
            def answer():
                data, peer = server.recvfrom(256)
                assert data == b'\xff\xff\xff\xffinfo 49', data
                server.sendto(b'\xff\xff\xff\xffinfo\n\\p\\49\\numcl\\0\\maxcl\\4\\cofbuild\\test1', peer)
            thread = threading.Thread(target=answer); thread.start()
            info = network.query_info(network.Endpoint('127.0.0.1', server.getsockname()[1]), timeout=2)
            thread.join()
        self.assertEqual(info['maxcl'], '4')

    def test_query_info_without_a_server(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as idle:
            idle.bind(('127.0.0.1', 0)); port = idle.getsockname()[1]
        with self.assertRaises(network.NetworkError):
            network.query_info(network.Endpoint('127.0.0.1', port), timeout=0.5)
```

- [ ] **Step 2:** `python -m unittest tests.test_network -v` → AttributeError (no `parse_info_reply`).
- [ ] **Step 3: Implement** in `cofcoop/network.py`:

```python
def parse_info_reply(payload: bytes) -> dict:
    """A Xash `info` reply: 'info\\n' and an infostring \\key\\value..., or '<host>: wrong version'."""
    text = payload.decode('latin-1')
    if not text.startswith('info\n'):
        raise NetworkError('The server did not answer like a Cry of Fear co-op game.')
    body = text[5:].rstrip('\n')
    if not body.startswith('\\'):
        raise NetworkError('The host runs a different version of the game engine.')
    parts = body[1:].split('\\')
    if len(parts) % 2:
        raise NetworkError('The server sent a damaged reply.')
    return dict(zip(parts[::2], parts[1::2]))


def query_info(endpoint: Endpoint, timeout: float = 2) -> dict:
    """Ask a Xash server for its info string (the engine's `info <protocol>` query)."""
    deadline = _deadline(timeout)
    address = _resolve(endpoint, deadline)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(address)
            sock.send(_HEADER + b'info 49')
            return parse_info_reply(_recv_message(sock, deadline))
    except (OSError, TimeoutError) as exc:
        raise NetworkError(f'No answer from {endpoint}. Check the address, that the host’s game is running, '
                           f'and that UDP {endpoint.port} reaches the host (router port or a shared VPN).') from exc
```

- [ ] **Step 4:** same command → OK.
- [ ] **Step 5: Commit** "network: the Xash info query, for the tester app's join check".

---

### Task 4: `tools/coop_play.py`: what each start option runs

**Files:** Create `tools/coop_play.py`; Test `tests/test_coop_play.py`.

**Interfaces:**
- Consumes: `cofcoop.core.CHAPTERS`; `cofcoop.network` (`Endpoint`, `parse_endpoint`, `make_join_code`, `query_info`, `upnp_map`, `upnp_unmap`, `discover_public_address`, `NetworkError`).
- Produces: `VERSION`, `VERSION_LABEL`, `BUILD`, `LOG`, `STORY_CFG`, `SESSION_CFG`, `DEFAULT_PORT`, `NEW_GAME_MAP`, `CHAPTER_CHOICES: list[tuple[str, str]]`, `ISSUES_URL`; `story_config() -> str`; `clean_name(str) -> str`; `clean_password(str) -> str`; `clean_port(str|int) -> int`; `clean_players(str|int) -> int`; `video_flags(first_launch: bool, desktop: tuple[int,int]|None) -> list[str]`; `host_launch(runtime, *, solo, name, map_name, players, password, companion, port, cont, video) -> tuple[list[str], str]`; `join_launch(runtime, *, name, password, endpoint, video) -> tuple[list[str], str]`; `check_host(info: dict, *, password: str) -> None`; `Checkpoint(slot, map, players, names)`; `parse_checkpoint(slot: str, data: bytes) -> Checkpoint|None`; `latest_checkpoint(runtime) -> Checkpoint|None`; `continue_warning(cp, name) -> str|None`; `load_settings(root) -> dict`; `save_settings(root, settings) -> None`; `class HostWatch(port, *, lan_only, net=network, clock=time.monotonic, wait=90)` with `open_router()`, `poll(running: bool) -> str`, `close()`, attributes `code`, `address`, `router`, `closed`.

- [ ] **Step 1: Failing tests** `tests/test_coop_play.py`:

```python
"""The tester app's start options, checks and checkpoint reading (tools/coop_play.py)."""
import struct
import tempfile
import types
import unittest
from pathlib import Path

from cofcoop import network
from tools import coop_play as play


def archive(members):
    """A checkpoint archive (engine sv_cof_archive.h) holding the given members."""
    body = b''.join(struct.pack('<64sI', name.encode(), len(data)) + data for name, data in members)
    data = struct.pack('<5I', 0x53434643, 1, len(members), 20 + len(body), 0) + body
    return data


def session(map_name='c_apartment1', clients=2, names=('Bob', 'Companion'), version=2):
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
        for line in ('cof_story_coop 1', 'cof_story_trace 1', 'sv_cheats 0', 'sv_enttools_enable 0', 'cof_test_gun 0', 'fps_max 60'):
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
        self.assertNotIn('c_intro', [m for _, m in play.CHAPTER_CHOICES])
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
            folder = root / 'SAVE/cofcoop'; folder.mkdir(parents=True)
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
        watch.close(); watch.close()
        self.assertEqual(net.unmapped, 1)
        net = FakeNet(); watch = play.HostWatch(27015, lan_only=False, net=net); watch.open_router()
        watch.close(); self.assertEqual(net.unmapped, 1)

    def test_watch_reports_a_server_that_never_answers(self):
        now = [0.0]
        watch = play.HostWatch(27015, lan_only=True, net=FakeNet(up=False), clock=lambda: now[0], wait=90)
        self.assertIn('Starting', watch.poll(True))
        now[0] = 91
        self.assertIn('another program', watch.poll(True))

    def test_solo_never_touches_the_router(self):
        net = FakeNet()
        watch = play.HostWatch(27015, lan_only=True, net=net)
        watch.open_router(); watch.close()
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
```

- [ ] **Step 2:** `python -m unittest tests.test_coop_play -v` → ImportError.
- [ ] **Step 3: Implement** `tools/coop_play.py`:

```python
"""Story co-op sessions for the tester app: what each start option runs.

Builds command lines and config text, reads the latest co-op checkpoint, checks
a host before joining and watches a hosted game. Starts no process itself.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import struct
import time

from cofcoop import network
from cofcoop.core import CHAPTERS

VERSION = "test1"
VERSION_LABEL = "Test 1"
BUILD = 1
LOG = "story-session.log"
STORY_CFG = "cof_story.cfg"
SESSION_CFG = "cof_story_session.cfg"
SETTINGS = "cof-story-coop-settings.json"
DEFAULT_PORT = 27015
NEW_GAME_MAP = "c_intro"
CHAPTER_CHOICES = [(label, name) for label, name in CHAPTERS if name != NEW_GAME_MAP]
ISSUES_URL = "https://github.com/charliee1w/cof-story-coop/issues/new/choose"
STORY_SETTINGS = (
    "cof_story_coop 1", "cof_story_trace 1", "sv_cheats 0", "sv_enttools_enable 0", "developer 0",
    "coop 0", "deathmatch 1", "mp_timelimit 0", "mp_fraglimit 0", "cof_test_gun 0", "fps_max 60",
    "rate 50000", "cl_cmdrate 60", "cl_updaterate 60", "cl_forwardspeed 300", "cl_backspeed 200",
    "cl_sidespeed 200", "+mlook",
)
DEFAULT_SETTINGS = {"name": "", "port": DEFAULT_PORT, "players": 2, "companion": False, "password": "",
                    "first_launch_done": False}
_MAP = re.compile(r"[a-z0-9_]{1,32}\Z")
_SLOT = re.compile(r"[a-z0-9_-]{1,24}\Z")
_ARCHIVE = struct.Struct("<5I")
_ENTRY = struct.Struct("<64sI")
_SESSION = struct.Struct("<4I64s")
_NAMES_AT = 16 + 64 + 256 * 64      # cof_session_data_t.players (engine sv_cof_session_data.h)


def story_config() -> str:
    return "\n".join(STORY_SETTINGS) + "\n"


def _clean(value, limit: int, label: str, allow_empty: bool) -> str:
    if not isinstance(value, str) or len(value) > limit or any(ord(c) < 32 or ord(c) > 126 or c in '\\";' for c in value):
        raise ValueError(f"{label}: use up to {limit} plain letters, digits and punctuation (no \", \\ or ;).")
    if not value and not allow_empty:
        raise ValueError(f"Enter {label.lower()}.")
    return value


def clean_name(value) -> str:
    return _clean(value.strip() if isinstance(value, str) else value, 24, "Your name", False)


def clean_password(value) -> str:
    return _clean(value, 64, "The password", True)


def _number(value, low: int, high: int, message: str) -> int:
    try:
        number = int(str(value).strip())
    except ValueError:
        raise ValueError(message) from None
    if not low <= number <= high:
        raise ValueError(message)
    return number


def clean_port(value) -> int:
    return _number(value, 1024, 65535, "The port must be a number from 1024 to 65535.")


def clean_players(value) -> int:
    return _number(value, 2, 4, "Players: 2 to 4.")


def video_flags(first_launch: bool, desktop) -> list[str]:
    """The first game fills the desktop; later ones keep the game's own video options."""
    if not first_launch or not desktop or desktop[0] < 640 or desktop[1] < 480:
        return []
    return ["-borderless", "-width", str(desktop[0]), "-height", str(desktop[1])]


def host_launch(runtime: Path, *, solo: bool, name: str, map_name: str, players: int, password: str,
                companion: bool, port: int, cont: bool, video: list[str]) -> tuple[list[str], str]:
    """Play solo (LAN only, the AI companion) or host: the command line and the session cfg."""
    name = clean_name(name)
    password = "" if solo else clean_password(password)
    map_name = map_name.lower()
    if not _MAP.match(map_name):
        raise ValueError("Unknown map.")
    if not 2 <= players <= 32:
        raise ValueError("Players: 2 to 4.")
    lines = [f'name "{name}"', f"maxplayers {players}", f"sv_lan {1 if solo else 0}",
             f'sv_password "{password}"', f'password "{password}"', f'hostname "{name}\'s game"',
             f"cof_companion {1 if solo or companion else 0}", "cof_companion_drive 0",
             f"cof_build {VERSION}", f"cof_continue {1 if cont else 0}"]
    args = [str(runtime / "CoFLaunchApp.exe"), *video, "-port", str(port), "-clientport", str(port + 1),
            "-log", LOG, "-logtime", "+exec", STORY_CFG, "+exec", SESSION_CFG, "+map", map_name]
    return args, "\n".join(lines) + "\n"


def join_launch(runtime: Path, *, name: str, password: str, endpoint: str, video: list[str]) -> tuple[list[str], str]:
    target = network.parse_endpoint(endpoint)
    config = f'name "{clean_name(name)}"\npassword "{clean_password(password)}"\n'
    args = [str(runtime / "CoFLaunchApp.exe"), *video, "-log", LOG, "-logtime",
            "+exec", STORY_CFG, "+exec", SESSION_CFG, "+connect", str(target)]
    return args, config


def check_host(info: dict, *, password: str) -> None:
    """Refuse a join that cannot work, before the game starts (info: network.query_info)."""
    theirs = info.get("cofbuild", "")
    if theirs != VERSION:
        raise ValueError(f"The host runs a different version of story co-op (theirs: {theirs or 'unknown'}, "
                         f"yours: {VERSION_LABEL}). Everyone needs the same package.")
    try:
        players, limit = int(info.get("numcl", "0")), int(info.get("maxcl", "0"))
    except ValueError:
        players = limit = 0
    if limit and players >= limit:
        raise ValueError("The host's game is full.")
    if info.get("password") == "1" and not password:
        raise ValueError("The host's game has a password. Enter it and join again.")


@dataclass(frozen=True)
class Checkpoint:
    slot: str
    map: str
    players: int
    names: tuple[str, ...]


def parse_checkpoint(slot: str, data: bytes) -> Checkpoint | None:
    """The map, player limit and names of a co-op checkpoint archive (format 2), or None."""
    if len(data) < _ARCHIVE.size:
        return None
    magic, version, count, size, _ = _ARCHIVE.unpack_from(data)
    if magic != 0x53434643 or version != 1 or size != len(data) or not 0 < count <= 1024:
        return None
    position = _ARCHIVE.size
    for _ in range(count):
        if position + _ENTRY.size > len(data):
            return None
        raw, length = _ENTRY.unpack_from(data, position)
        position += _ENTRY.size
        if position + length > len(data):
            return None
        if raw.split(b"\0", 1)[0].lower() == b"session.dat":
            body = data[position:position + length]
            if len(body) < _NAMES_AT + 32 * 32:
                return None
            session_version, players, _, _, raw_map = _SESSION.unpack_from(body)
            map_name = raw_map.split(b"\0", 1)[0].decode("latin-1").lower()
            if session_version != 2 or not 2 <= players <= 32 or not _MAP.match(map_name):
                return None
            names = tuple(name for name in (body[_NAMES_AT + i * 32:_NAMES_AT + (i + 1) * 32].split(b"\0", 1)[0]
                                            .decode("latin-1") for i in range(32)) if name)
            return Checkpoint(slot, map_name, players, names)
        position += length
    return None


def latest_checkpoint(runtime: Path) -> Checkpoint | None:
    folders = [folder for folder in (Path(runtime) / "SAVE/cofcoop", Path(runtime) / "cryoffear/save/cofcoop")
               if (folder / "latest.txt").is_file()]
    if not folders:
        return None
    folder = max(folders, key=lambda f: (f / "latest.txt").stat().st_mtime)
    try:
        slot = (folder / "latest.txt").read_text("latin-1").strip()
        if not _SLOT.match(slot):
            return None
        return parse_checkpoint(slot, (folder / f"{slot}.cof").read_bytes())
    except OSError:
        return None


def continue_warning(checkpoint: Checkpoint, name: str) -> str | None:
    if not checkpoint.names or name.strip().casefold() in {n.casefold() for n in checkpoint.names}:
        return None
    return (f"This checkpoint was saved by {', '.join(checkpoint.names)}. Items come back only under the "
            f"same name: change Your name to one of those, or continue without your items.")


def load_settings(root: Path) -> dict:
    try:
        stored = json.loads((Path(root) / SETTINGS).read_text("utf-8"))
    except (OSError, ValueError):
        stored = {}
    if not isinstance(stored, dict):
        stored = {}
    return {key: stored.get(key, value) if isinstance(stored.get(key, value), type(value)) else value
            for key, value in DEFAULT_SETTINGS.items()}


def save_settings(root: Path, settings: dict) -> None:
    path = Path(root) / SETTINGS
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(settings, indent=2) + "\n", "utf-8")
    os.replace(temp, path)


class HostWatch:
    """While a hosted (or solo) game runs: the router mapping, the join code and the player count.

    The window calls open_router() once and poll() every few seconds, both off the UI thread."""

    def __init__(self, port: int, *, lan_only: bool, net=network, clock=time.monotonic, wait: float = 90):
        self.port, self.lan_only, self.net, self.clock, self.wait = port, lan_only, net, clock, wait
        self.started = clock()
        self.up = self.closed = False
        self.mapping = None
        self.code = self.address = self.router = ""

    def open_router(self) -> None:
        if self.lan_only:
            return
        public = ""
        try:
            self.mapping = self.net.upnp_map(self.port)
            public = self.mapping.get("external_ip", "")
            self.router = "Your router opened the port for this game (UPnP)."
        except self.net.NetworkError as error:
            self.router = (f"Your router did not open UDP {self.port} by itself ({error}). Forward UDP {self.port} "
                           "to this PC in your router, or all use a VPN such as Tailscale, ZeroTier or Radmin "
                           "and share this PC's VPN address instead.")
            try:
                public = self.net.discover_public_address()
            except self.net.NetworkError:
                public = ""
        if public:
            endpoint = self.net.Endpoint(public, self.port)
            self.address, self.code = str(endpoint), self.net.make_join_code(endpoint)

    def poll(self, running: bool) -> str:
        if self.closed or not running:
            self.close()
            return "The game has ended."
        try:
            info = self.net.query_info(self.net.Endpoint("127.0.0.1", self.port), timeout=1)
        except self.net.NetworkError:
            if not self.up and self.clock() - self.started > self.wait:
                return (f"The game has not answered on UDP {self.port}. If it showed an error, another program "
                        "may be using that port: close it, or pick another port.")
            return "Starting the game..."
        self.up = True
        return f"Your game is up: {info.get('numcl', '?')}/{info.get('maxcl', '?')} players."

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        if self.mapping:
            try:
                self.net.upnp_unmap(self.mapping)
            except self.net.NetworkError:
                pass
            self.mapping = None
```

- [ ] **Step 4:** `python -m unittest tests.test_coop_play -v` → OK.
- [ ] **Step 5: Commit** "Tester app: start options, host checks, checkpoints, the host watch".

---

### Task 5: `tools/friend_setup.py`: payload in the exe, art, update in place, shortcuts

**Files:** Modify `tools/friend_setup.py`; Test `tests/test_friend_setup.py` (rewrite the join/bundle tests, add the new ones), `tests/test_warp_bind.py` unchanged.

**Interfaces:**
- Consumes: `tools.make_inventory12_art.generate(game) -> list[Path]`; `tools.prepare_simon_models` (as today); `coop_play.STORY_CFG`, `coop_play.story_config()`, `coop_play.LOG`.
- Produces: `APP_EXE = "CoF Story Co-op.exe"`, `PAYLOAD_ZIP`, `PAYLOAD_JSON`, `INSTALLED = "cof-story-installed.json"`; `inspect_payload(folder) -> dict`; `setup(source, destination, payload, *, exe, progress=..., shortcuts=True) -> Path`; `update(root, payload, *, exe, progress=..., shortcuts=True) -> Path`; `installed_state(root) -> dict|None`; `validate_runtime(root) -> Path`; `game_is_running(root) -> bool` (this copy only); `keep_previous_log(root, name) -> Path|None`; `write_shortcuts(root) -> bool`; `steam_candidates`, `validate_source`, `validate_destination`, `copy_plan`, `copy_assets`, `safe_zip_members`, `default_controls` (as today, plus `h`). Removed: `inspect_bundle`, `join_config`, `join_arguments`, `validate_join_runtime`, `open_window`, `main`, `TESTER_LOG`, `JOIN_CFG`.

- [ ] **Step 1: Tests.** In `tests/test_friend_setup.py`: delete `test_join_validation_blocks_console_injection_and_password_not_in_argv`, `test_blank_player_name_is_refused_and_setup_default_is_generic`, `test_bundle_hashes_and_engine_identity_required` (covered by `tests/test_coop_play.py` and the new payload test); replace `test_existing_retail_or_enhanced_game_blocks_a_second_client` and `test_join_writes_a_timestamped_log_and_keeps_the_previous_one`; add:

```python
def make_payload(folder: Path, engine=b"engine", build=1):
    folder.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(folder / friend.PAYLOAD_ZIP, "w") as archive:
        archive.writestr("cof-enhanced/files/xash.dll", engine)
        archive.writestr("cof-enhanced/install.ps1", b"# installer")
        archive.writestr("README.txt", b"readme")
    (folder / friend.PAYLOAD_JSON).write_text(json.dumps({"schema": 2, "version": f"test{build}", "build": build,
                                                          "xash_sha256": hashlib.sha256(engine).hexdigest()}))
    return folder


def fake_installer(engine):
    """Stands in for install.ps1: it puts files\\xash.dll in the game folder, as the real one does."""
    def run(args, cwd, **kwargs):
        (Path(cwd) / "xash.dll").write_bytes((Path(cwd) / "cof-enhanced/files/xash.dll").read_bytes())
        return subprocess.CompletedProcess(args, 0)
    return run


class FriendSetupTests(unittest.TestCase):  # existing class: add these methods
    def test_only_a_game_from_this_copy_counts_as_running(self):
        root = Path("C:/Games/CoF-Story-Coop")
        for path, expected in ((root / "CoFLaunchApp.exe", True), (Path("C:/Games/CoF-Story-Coop2/cof.exe"), False),
                               (None, False)):
            reply = subprocess.CompletedProcess([], 0, json.dumps([{"ProcessId": 1, "Name": "CoFLaunchApp.exe",
                                                                    "ExecutablePath": str(path) if path else None}]), "")
            with self.subTest(path=path), mock.patch.object(friend.subprocess, "run", return_value=reply):
                self.assertEqual(friend.game_is_running(root), expected)

    def test_each_game_keeps_the_previous_log(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(friend.keep_previous_log(root, "story-session.log"))
            for text in ("first", "second"):
                (root / "story-session.log").write_text(text)
                kept = friend.keep_previous_log(root, "story-session.log")
                self.assertEqual((kept.parent, kept.read_text()), (root / "story-logs", text))
                self.assertTrue(kept.name.startswith("story-session-"))
            self.assertEqual(len(list((root / "story-logs").iterdir())), 2)

    def test_payload_identity_is_checked(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = make_payload(Path(temp) / "payload")
            self.assertEqual(friend.inspect_payload(folder)["build"], 1)
            manifest = json.loads((folder / friend.PAYLOAD_JSON).read_text()); manifest["xash_sha256"] = "0" * 64
            (folder / friend.PAYLOAD_JSON).write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "damaged"):
                friend.inspect_payload(folder)

    def test_update_lays_the_new_engine_over_and_keeps_saves_settings_models(self):
        with tempfile.TemporaryDirectory() as temp:
            root, exe = Path(temp) / "CoF-Story-Coop", Path(temp) / "new.exe"
            exe.write_bytes(b"new app")
            for rel, data in {"xash.dll": b"old engine", "SAVE/cofcoop/autosave.cof": b"save",
                              "cryoffear/config.cfg": b"my binds", "cryoffear/models/player/cof_s1/cof_s1.mdl": b"model",
                              "cof-story-coop-settings.json": b"{}"}.items():
                path = root / rel; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            models = {"models/player/cof_s1/cof_s1.mdl": hashlib.sha256(b"model").hexdigest()}
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test1", "build": 1,
                "xash_sha256": hashlib.sha256(b"old engine").hexdigest(), "models": models, "source_game": "src"}))
            payload = make_payload(Path(temp) / "payload", b"new engine", build=2)
            with mock.patch.object(friend, "game_is_running", return_value=False), \
                 mock.patch.object(friend.subprocess, "run", side_effect=fake_installer(b"new engine")), \
                 mock.patch.object(friend.art, "generate", return_value=[]) as art:
                friend.update(root, payload, exe=exe, shortcuts=False)
            self.assertEqual((root / "xash.dll").read_bytes(), b"new engine")
            art.assert_called_once_with(root.resolve())
            for rel, data in {"SAVE/cofcoop/autosave.cof": b"save", "cryoffear/config.cfg": b"my binds",
                              "cryoffear/models/player/cof_s1/cof_s1.mdl": b"model"}.items():
                self.assertEqual((root / rel).read_bytes(), data, rel)
            self.assertEqual((root / friend.APP_EXE).read_bytes(), b"new app")
            state = friend.installed_state(root)
            self.assertEqual((state["build"], state["models"]), (2, models))
            self.assertIn("cof_test_gun 0", (root / "cryoffear/cof_story.cfg").read_text())

    def test_update_refuses_while_the_game_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "copy"; root.mkdir()
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 2, "version": "test1", "build": 1,
                "xash_sha256": "0" * 64, "models": {}, "source_game": "src"}))
            (root / "xash.dll").write_bytes(b"old")
            with mock.patch.object(friend, "game_is_running", return_value=True), self.assertRaisesRegex(ValueError, "Close"):
                friend.update(root, make_payload(Path(temp) / "payload"), exe=root / "x.exe", shortcuts=False)
            self.assertEqual((root / "xash.dll").read_bytes(), b"old")

    def test_installed_state_needs_this_apps_schema(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.assertIsNone(friend.installed_state(root))
            (root / friend.INSTALLED).write_text(json.dumps({"schema": 1, "protocol": 49}))
            self.assertIsNone(friend.installed_state(root))

    def test_shortcuts_pass_paths_through_the_environment(self):
        with mock.patch.object(friend.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run:
            self.assertTrue(friend.write_shortcuts(Path("C:/Games/CoF-Story-Coop")))
        args, kwargs = run.call_args
        self.assertNotIn("CoF-Story-Coop", " ".join(args[0]))
        self.assertEqual(kwargs["env"]["COF_APP_EXE"], str(Path("C:/Games/CoF-Story-Coop") / friend.APP_EXE))
        self.assertIn("Cry of Fear Co-op.lnk", args[0][-1])

    def test_controls_bind_the_coop_keys(self):
        controls = friend.default_controls()
        for line in ('bind "j" "cof_warp"', 'bind "h" "cof_companion_order"', 'bind "ENTER" "cof_skip"'):
            self.assertIn(line, controls)
```

Keep `test_enter_skips_cutscenes_for_the_party`, the copy, destination, source, archive and Steam tests unchanged.

- [ ] **Step 2:** `python -m unittest tests.test_friend_setup -v` → failures (new names missing).
- [ ] **Step 3: Implement** in `tools/friend_setup.py`:
  - Module docstring: "Private-copy installer for the story co-op tester app (setup and update; the window is tools/coop_app.py)."
  - Imports: drop `cofcoop.core.validate_endpoint`, `queue`, `threading`; add `from tools import make_inventory12_art as art` and `from tools import coop_play as play`.
  - Constants: remove `JOIN_CFG`, `TESTER_LOG`; add `APP_EXE = "CoF Story Co-op.exe"`, `PAYLOAD_ZIP = "engine-payload.zip"`, `PAYLOAD_JSON = "payload.json"`, `INSTALLED = "cof-story-installed.json"`, `SHORTCUT = "Cry of Fear Co-op"`.
  - `default_controls()`: add `"h": "cof_companion_order"` after `"j"`.
  - Replace `inspect_bundle` with:

```python
def inspect_payload(folder: Path) -> dict:
    try:
        manifest = json.loads((Path(folder) / PAYLOAD_JSON).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise ValueError("This package is incomplete. Download it again.") from None
    if manifest.get("schema") != 2 or not isinstance(manifest.get("build"), int):
        raise ValueError("This package is incomplete. Download it again.")
    with zipfile.ZipFile(Path(folder) / PAYLOAD_ZIP) as archive:
        list(safe_zip_members(archive))
        if digest_file_from_archive(archive, "cof-enhanced/files/xash.dll") != manifest["xash_sha256"]:
            raise ValueError("This package is damaged. Download it again.")
    return manifest
```

  - `extract_payload(payload, destination, *, overwrite=False)`: open targets with `"wb" if overwrite else "xb"`.
  - Factor the install.ps1 call out of `setup` into `run_installer(destination: Path) -> None` (same code; raises `RuntimeError("The engine could not be installed. See friend-setup-install.log in the copy.")`).
  - New helpers:

```python
def make_art(root: Path) -> None:
    try:
        art.generate(root)
    except SystemExit as exit:
        raise RuntimeError(f"The inventory art could not be made: {exit}") from None


def installed_state(root: Path) -> dict | None:
    try:
        state = json.loads((Path(root) / INSTALLED).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(state, dict) or state.get("schema") != 2 or not isinstance(state.get("build"), int):
        return None
    return state


SHORTCUT_SCRIPT = (
    "$shell = New-Object -ComObject WScript.Shell; "
    "foreach ($folder in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))) { "
    "$link = $shell.CreateShortcut((Join-Path $folder 'Cry of Fear Co-op.lnk')); "
    "$link.TargetPath = $env:COF_APP_EXE; $link.WorkingDirectory = $env:COF_APP_DIR; "
    "$link.IconLocation = $env:COF_APP_ICON + ',0'; $link.Description = 'Cry of Fear story co-op'; $link.Save() }")


def write_shortcuts(root: Path) -> bool:
    """Desktop and Start-menu shortcuts; paths go through the environment, never into the script."""
    env = dict(os.environ, COF_APP_EXE=str(root / APP_EXE), COF_APP_DIR=str(root), COF_APP_ICON=str(root / "CoFLaunchApp.exe"))
    shell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    result = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-Command", SHORTCUT_SCRIPT], env=env,
                            capture_output=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
    return result.returncode == 0


def finish(root: Path, manifest: dict, models: dict, source_game: str, exe: Path, shortcuts: bool, progress) -> None:
    """The parts setup and update share: story cfg, the app in the copy, the installed record, shortcuts."""
    (root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
    target = root / APP_EXE
    if Path(exe).resolve() != target.resolve():
        shutil.copyfile(exe, target)
    state = {"schema": 2, "version": manifest["version"], "build": manifest["build"], "xash_sha256": manifest["xash_sha256"],
             "models": models, "source_game": source_game, "campaign_verified": False}
    (root / INSTALLED).write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    if shortcuts and not write_shortcuts(root):
        progress("Done, but the desktop shortcut could not be made: start CoF Story Co-op.exe in the copy.", 1)
        return
    progress("Done. Use the Cry of Fear Co-op shortcut on your desktop or in the Start menu.", 1)


def update(root: Path, payload: Path, *, exe: Path, progress=lambda *_: None, shortcuts=True) -> Path:
    root = Path(root).resolve()
    state = installed_state(root)
    if not state:
        raise ValueError("This folder is not a story co-op copy made by this app.")
    if game_is_running(root):
        raise ValueError("Close Cry of Fear first, then update.")
    manifest = inspect_payload(payload)
    progress("Installing the new engine", .2)
    extract_payload(Path(payload) / PAYLOAD_ZIP, root, overwrite=True)
    run_installer(root)
    if digest_file(root / "xash.dll") != manifest["xash_sha256"]:
        raise RuntimeError("Installed engine hash differs from the package.")
    progress("Making the inventory art", .7)
    make_art(root)
    finish(root, manifest, state["models"], state["source_game"], exe, shortcuts, progress)
    return root
```

  - `setup(source, destination, payload, *, exe, progress=..., shortcuts=True)`: as today with `inspect_payload(payload)` instead of `inspect_bundle(bundle)`, `extract_payload(payload / PAYLOAD_ZIP, …)`, `run_installer(destination)`; after the models: `progress("Making the inventory art", .96); make_art(destination)`; write `cryoffear/config.cfg` = `default_controls() + 'name "Player"\nsensitivity 4\n'` (once, at setup); remove `JOIN_CFG`, `CoF-Story-Join.exe` and `JOIN.cmd`; end with `finish(destination, manifest, model_hashes, str(source), exe, shortcuts, progress)`; keep `SETUP-INCOMPLETE.txt` (removed before `finish`).
  - `validate_runtime(root)`: the old `validate_join_runtime` with `installed_state` (schema 2) instead of the schema/protocol check; message "This game copy does not match its install. Run the newest CoF Story Co-op.exe and click Update."
  - `game_is_running(root)`: query `Select-Object ProcessId,Name,ExecutablePath`; true only when a row's `ExecutablePath` starts with `str(Path(root)) + os.sep` (case-insensitive).
  - `keep_previous_log(root, name)`: as today with `name` instead of `TESTER_LOG`; the kept file is `story-logs/<stem>-<stamp>.log` (`stem` = `Path(name).stem`).
  - Delete `open_window` and `main` (and the `__main__` block).

- [ ] **Step 4:** `python -m unittest tests.test_friend_setup tests.test_warp_bind -v` → OK.
- [ ] **Step 5: Commit** "Setup: payload in the app, the inventory art, update in place, shortcuts".

---

### Task 6: `tools/coop_app.py`: the window and the exe entry point

**Files:** Create `tools/coop_app.py`; Test `tests/test_coop_app.py`.

**Interfaces:**
- Consumes: everything produced in Tasks 3–5.
- Produces: `where_to_start(exe_dir: Path, default_copy: Path, build: int) -> tuple[str, Path|None]` ("main", "update", "newer", "setup"); `main(argv) -> int` with `--self-test`, `--install SOURCE DEST`, `--update ROOT`, `--payload DIR`, `--no-shortcuts`, `--copy ROOT`.

- [ ] **Step 1: Tests** `tests/test_coop_app.py`:

```python
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
            self.assertEqual(app.where_to_start(copy, Path(temp) / "elsewhere", 1), ("main", copy))  # the app inside a copy

    def test_cli_install_needs_a_payload_outside_the_exe(self):
        self.assertEqual(app.main(["--install", "src", "dst"]), 2)
```

- [ ] **Step 2:** `python -m unittest tests.test_coop_app -v` → ImportError.
- [ ] **Step 3: Implement** `tools/coop_app.py`:

```python
"""Cry of Fear story co-op: the tester app. Setup on first run, then Play solo, Host or Join."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time
import webbrowser

if not getattr(sys, "frozen", False):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cofcoop import network
from tools import coop_play as play
from tools import friend_setup as setup_lib

DEFAULT_COPY = Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "CoF-Story-Coop"


def payload_dir(explicit=None) -> Path:
    if explicit:
        return Path(explicit)
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "payload"
    raise ValueError("Run the packaged CoF Story Co-op.exe, or pass --payload <folder>.")


def this_exe() -> Path:
    return Path(sys.executable)


def desktop_size():
    if os.name != "nt":
        return None
    import ctypes
    user32 = ctypes.windll.user32
    try:
        user32.SetProcessDPIAware()
    except (AttributeError, OSError):
        pass
    return user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)


def where_to_start(exe_dir: Path, default_copy: Path, build: int):
    if setup_lib.installed_state(exe_dir):
        return "main", Path(exe_dir)
    state = setup_lib.installed_state(default_copy)
    if not state:
        return "setup", None
    if state["build"] < build:
        return "update", Path(default_copy)
    if state["build"] > build:
        return "newer", Path(default_copy)
    return "main", Path(default_copy)


class App:
    def __init__(self, window, payload, exe):
        import tkinter as tk
        from tkinter import ttk
        self.tk, self.ttk = tk, ttk
        self.window, self.payload, self.exe = window, payload, exe
        self.root = None
        self.settings = {}
        self.watch = None
        self.messages = queue.Queue()
        window.title("Cry of Fear Co-op")
        window.geometry("720x520")
        window.minsize(640, 480)
        self.body = ttk.Frame(window, padding=20)
        self.body.pack(fill="both", expand=True)
        foot = ttk.Frame(window, padding=(20, 0, 20, 12))
        foot.pack(fill="x")
        ttk.Label(foot, text=f"Story co-op {play.VERSION_LABEL} · early test build").pack(side="left")
        ttk.Button(foot, text="Report a bug", command=lambda: webbrowser.open(play.ISSUES_URL)).pack(side="right")
        self.logs = ttk.Button(foot, text="Open logs folder", command=self.open_logs)
        self.logs.pack(side="right", padx=6)
        window.protocol("WM_DELETE_WINDOW", self.close)
        window.after(100, self.pump)

    # plumbing
    def clear(self, title, subtitle=""):
        for child in self.body.winfo_children():
            child.destroy()
        self.ttk.Label(self.body, text=title, font=("Segoe UI", 18, "bold")).pack(anchor="w")
        if subtitle:
            self.ttk.Label(self.body, text=subtitle, wraplength=660).pack(anchor="w", pady=(4, 12))
        return self.body

    def work(self, job, done, failed=None):
        def run():
            try:
                self.messages.put((done, job()))
            except Exception as error:   # shown to the tester, never a traceback
                self.messages.put((failed or self.error, error))
        threading.Thread(target=run, daemon=True).start()

    def pump(self):
        while True:
            try:
                handler, value = self.messages.get_nowait()
            except queue.Empty:
                break
            handler(value)
        self.window.after(100, self.pump)

    def error(self, error):
        from tkinter import messagebox
        messagebox.showerror("Cry of Fear Co-op", str(error), parent=self.window)

    def guard(self, action):
        def run(*_):
            try:
                action()
            except (OSError, ValueError, RuntimeError, network.NetworkError) as error:
                self.error(error)
        return run

    def close(self):
        if self.watch:
            self.watch.close()
        self.window.destroy()

    def open_logs(self):
        if not self.root:
            return
        log = self.root / play.LOG
        if log.is_file():
            subprocess.Popen(["explorer", "/select,", str(log)])
        else:
            os.startfile(self.root / "story-logs" if (self.root / "story-logs").is_dir() else self.root)

    # setup and update
    def show_setup(self):
        ttk = self.ttk
        body = self.clear("Set up story co-op", "The app makes a separate copy of Cry of Fear for co-op. "
                          "Your Steam game, saves and settings are not changed. About 6 GB of free space is needed.")
        source = self.tk.StringVar()
        destination = self.tk.StringVar(value=str(DEFAULT_COPY))
        for label, var, pick in (("Cry of Fear folder", source, self.pick_folder), ("Co-op copy", destination, self.pick_parent)):
            row = ttk.Frame(body); row.pack(fill="x", pady=4)
            ttk.Label(row, text=label, width=18).pack(side="left")
            ttk.Entry(row, textvariable=var).pack(side="left", fill="x", expand=True)
            ttk.Button(row, text="Browse…", command=lambda v=var, p=pick: p(v)).pack(side="left", padx=4)
        bar = ttk.Progressbar(body, maximum=100)
        status = self.tk.StringVar(value="Looking for Cry of Fear in Steam…")
        button = ttk.Button(body, text="Set up")
        button.pack(anchor="w", pady=14); bar.pack(fill="x")
        ttk.Label(body, textvariable=status, wraplength=660).pack(anchor="w", pady=8)

        def found(candidates):
            if candidates and not source.get():
                source.set(str(candidates[0]))
            status.set("Ready." if source.get() else "Pick your Cry of Fear folder (the one with CoFLaunchApp.exe).")

        def progress(text, fraction):
            self.messages.put((lambda value: (status.set(value[0]), bar.configure(value=value[1] * 100)), (text, fraction)))

        def start():
            button.configure(state="disabled")
            src, dst = Path(source.get()), Path(destination.get())
            self.work(lambda: setup_lib.setup(src, dst, self.payload, exe=self.exe, progress=progress),
                      self.show_main, lambda error: (button.configure(state="normal"),
                                                     status.set("Setup stopped. Your Steam game is unchanged."), self.error(error)))
        button.configure(command=start)
        self.work(setup_lib.steam_candidates, found, lambda _: found([]))

    def pick_folder(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window)
        if chosen:
            var.set(chosen)

    def pick_parent(self, var):
        from tkinter import filedialog
        chosen = filedialog.askdirectory(parent=self.window, title="Where should the CoF-Story-Coop folder go?")
        if chosen:
            var.set(str(Path(chosen) / "CoF-Story-Coop"))

    def show_update(self, copy):
        ttk = self.ttk
        body = self.clear("Update story co-op", f"{play.VERSION_LABEL} is ready to install over your copy in {copy}. "
                          "Your saves and settings are kept.")
        status = self.tk.StringVar()
        button = ttk.Button(body, text="Update")
        button.pack(anchor="w", pady=14)
        ttk.Label(body, textvariable=status, wraplength=660).pack(anchor="w")

        def start():
            button.configure(state="disabled")
            self.work(lambda: setup_lib.update(copy, self.payload, exe=self.exe,
                                               progress=lambda text, _: self.messages.put((status.set, text))),
                      self.show_main, lambda error: (button.configure(state="normal"), self.error(error)))
        button.configure(command=start)

    # main
    def show_main(self, copy):
        ttk = self.ttk
        self.root = Path(copy)
        self.settings = play.load_settings(self.root)
        body = self.clear("Cry of Fear co-op", "Play the campaign together, or alone with an AI companion.")
        row = ttk.Frame(body); row.pack(fill="x", pady=(0, 16))
        ttk.Label(row, text="Your name", width=12).pack(side="left")
        self.name = self.tk.StringVar(value=self.settings["name"])
        ttk.Entry(row, textvariable=self.name, width=28).pack(side="left")
        for text, action in (("Play solo (with the AI companion)", lambda: self.show_start(solo=True)),
                             ("Host a game", lambda: self.show_start(solo=False)),
                             ("Join a game", self.show_join)):
            ttk.Button(body, text=text, command=action).pack(fill="x", ipady=8, pady=4)

    def remember(self, **values):
        self.settings.update(values)
        play.save_settings(self.root, self.settings)

    def launch(self, args, config):
        setup_lib.validate_runtime(self.root)
        if setup_lib.game_is_running(self.root):
            raise ValueError("Cry of Fear is already running from this copy. Close it first.")
        (self.root / "cryoffear" / play.STORY_CFG).write_text(play.story_config(), encoding="ascii")
        (self.root / "cryoffear" / play.SESSION_CFG).write_text(config, encoding="ascii")
        setup_lib.keep_previous_log(self.root, play.LOG)
        subprocess.Popen(args, cwd=self.root)
        self.remember(name=self.name.get().strip(), first_launch_done=True)

    def video(self):
        return play.video_flags(not self.settings["first_launch_done"], desktop_size())

    def start_choice(self, body):
        ttk, tk = self.ttk, self.tk
        checkpoint = play.latest_checkpoint(self.root)
        kind = tk.StringVar(value="new")
        ttk.Radiobutton(body, text="New game (the intro; Enter skips cutscenes)", variable=kind, value="new").pack(anchor="w")
        row = ttk.Frame(body); row.pack(anchor="w", fill="x")
        ttk.Radiobutton(row, text="Chapter:", variable=kind, value="chapter").pack(side="left")
        chapter = ttk.Combobox(row, values=[label for label, _ in play.CHAPTER_CHOICES], state="readonly", width=44)
        chapter.current(0); chapter.pack(side="left", padx=6)
        text = f"Continue: {checkpoint.map} (experimental)" if checkpoint else "Continue (no co-op checkpoint yet)"
        ttk.Radiobutton(body, text=text, variable=kind, value="continue",
                        state="normal" if checkpoint else "disabled").pack(anchor="w")

        def resolve():
            if kind.get() == "continue":
                return checkpoint.map, checkpoint
            if kind.get() == "chapter":
                return play.CHAPTER_CHOICES[chapter.current()][1], None
            return play.NEW_GAME_MAP, None
        return resolve

    def show_start(self, solo):
        ttk, tk = self.ttk, self.tk
        body = self.clear("Play solo" if solo else "Host a game",
                          "You and the AI companion, on this PC only." if solo else
                          "Friends join you with the join code shown after the game starts.")
        resolve = self.start_choice(body)
        players = tk.StringVar(value=str(self.settings["players"]))
        password = tk.StringVar(value=self.settings["password"])
        companion = tk.BooleanVar(value=self.settings["companion"])
        port = tk.StringVar(value=str(self.settings["port"]))
        if not solo:
            for label, widget in (("Players", lambda p: ttk.Spinbox(p, from_=2, to=4, textvariable=players, width=5)),
                                  ("Password", lambda p: ttk.Entry(p, textvariable=password, show="*", width=28)),
                                  ("Port (UDP)", lambda p: ttk.Entry(p, textvariable=port, width=8))):
                row = ttk.Frame(body); row.pack(anchor="w", pady=3)
                ttk.Label(row, text=label, width=12).pack(side="left"); widget(row).pack(side="left")
            ttk.Checkbutton(body, text="AI companion until a friend takes its place", variable=companion).pack(anchor="w", pady=4)

        def start():
            from tkinter import messagebox
            name = play.clean_name(self.name.get())
            map_name, checkpoint = resolve()
            if checkpoint:
                warning = play.continue_warning(checkpoint, name)
                if warning and not messagebox.askokcancel("Continue", warning, parent=self.window):
                    return
            count = checkpoint.players if checkpoint else 2 if solo else play.clean_players(players.get())
            number = play.DEFAULT_PORT if solo else play.clean_port(port.get())
            args, config = play.host_launch(self.root, solo=solo, name=name, map_name=map_name, players=count,
                                            password=password.get(), companion=companion.get(), port=number,
                                            cont=checkpoint is not None, video=self.video())
            self.launch(args, config)
            if not solo:
                self.remember(players=count if not checkpoint else self.settings["players"], password=password.get(),
                              companion=companion.get(), port=number)
            self.show_running(play.HostWatch(number, lan_only=solo), solo)
        buttons = ttk.Frame(body); buttons.pack(anchor="w", pady=16)
        ttk.Button(buttons, text="Start", command=self.guard(start)).pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=6)

    def show_running(self, watch, solo):
        ttk, tk = self.ttk, self.tk
        self.watch = watch
        body = self.clear("Playing solo" if solo else "Hosting",
                          "Keep this window open while you play." if solo else
                          "Send your friends the join code. Keep this window open while you host.")
        status = tk.StringVar(value="Starting the game...")
        ttk.Label(body, textvariable=status, wraplength=660).pack(anchor="w", pady=4)
        code, address, lan, router = tk.StringVar(), tk.StringVar(), tk.StringVar(), tk.StringVar()
        if not solo:
            for label, var in (("Join code", code), ("Address", address), ("Same place (LAN)", lan)):
                row = ttk.Frame(body); row.pack(fill="x", pady=3)
                ttk.Label(row, text=label, width=16).pack(side="left")
                ttk.Entry(row, textvariable=var, state="readonly").pack(side="left", fill="x", expand=True)
                ttk.Button(row, text="Copy", command=lambda v=var: (self.window.clipboard_clear(),
                                                                     self.window.clipboard_append(v.get()))).pack(side="left", padx=4)
            ttk.Label(body, textvariable=router, wraplength=660).pack(anchor="w", pady=8)

            def routed(_):
                code.set(watch.code or "(no public address found)"); address.set(watch.address); router.set(watch.router)
            self.work(watch.open_router, routed, lambda error: router.set(str(error)))
            self.work(lambda: network.local_addresses(),
                      lambda rows: lan.set(f"{rows[0]['address']}:{watch.port}" if rows else ""), lambda _: None)
        ttk.Button(body, text="Back", command=lambda: self.show_main(self.root)).pack(anchor="w", pady=12)
        root = self.root

        def loop():
            time.sleep(5)
            while not watch.closed:
                text = watch.poll(setup_lib.game_is_running(root))
                self.messages.put((status.set, text))
                time.sleep(3)
        threading.Thread(target=loop, daemon=True).start()

    def show_join(self):
        ttk, tk = self.ttk, self.tk
        body = self.clear("Join a game", "Paste the join code (or the address) your host sent you.")
        target, password = tk.StringVar(), tk.StringVar()
        for label, var, hidden in (("Join code", target, False), ("Password", password, True)):
            row = ttk.Frame(body); row.pack(fill="x", pady=3)
            ttk.Label(row, text=label, width=12).pack(side="left")
            ttk.Entry(row, textvariable=var, show="*" if hidden else "").pack(side="left", fill="x", expand=True)
        status = tk.StringVar()

        def join():
            name = play.clean_name(self.name.get())
            secret = play.clean_password(password.get())
            endpoint = network.parse_endpoint(target.get())
            status.set(f"Checking {endpoint}…")

            def ready(_):
                args, config = play.join_launch(self.root, name=name, password=secret, endpoint=str(endpoint), video=self.video())
                self.guard(lambda: (self.launch(args, config), status.set("Joining. The game window opens in a moment.")))()
            self.work(lambda: play.check_host(network.query_info(endpoint, timeout=3), password=secret), ready,
                      lambda error: (status.set(""), self.error(error)))
        buttons = ttk.Frame(body); buttons.pack(anchor="w", pady=16)
        ttk.Button(buttons, text="Join", command=self.guard(join)).pack(side="left")
        ttk.Button(buttons, text="Back", command=lambda: self.show_main(self.root)).pack(side="left", padx=6)
        ttk.Label(body, textvariable=status, wraplength=660).pack(anchor="w")


def open_window(payload, exe, copy=None):
    import tkinter as tk
    window = tk.Tk()
    app = App(window, payload, exe)
    if copy:
        app.show_main(copy)
    else:
        mode, found = where_to_start(exe.parent, DEFAULT_COPY, play.BUILD)
        if mode == "setup":
            app.show_setup()
        elif mode == "update":
            app.show_update(found)
        else:
            app.show_main(found)
            if mode == "newer":
                from tkinter import messagebox
                messagebox.showinfo("Cry of Fear Co-op", "Your copy is newer than this package. "
                                    "Use the Cry of Fear Co-op shortcut.", parent=window)
    window.mainloop()


def self_test(payload) -> int:
    import tkinter
    assert tkinter.Tcl().eval("info patchlevel")
    if getattr(sys, "frozen", False):
        assert setup_lib.inspect_payload(payload)["build"] == play.BUILD
    args, config = play.host_launch(Path("copy"), solo=True, name="Test", map_name="c_intro", players=2, password="",
                                    companion=False, port=27015, cont=False, video=[])
    assert "cof_build " + play.VERSION in config and "-console" not in args
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--install", nargs=2, metavar=("SOURCE", "DEST"))
    parser.add_argument("--update", metavar="COPY")
    parser.add_argument("--payload")
    parser.add_argument("--copy")
    parser.add_argument("--no-shortcuts", action="store_true")
    args = parser.parse_args(argv)
    try:
        payload = payload_dir(args.payload) if (args.payload or getattr(sys, "frozen", False)) else None
        if args.self_test:
            return self_test(payload)
        if args.install or args.update:
            if payload is None:
                return 2
            if args.install:
                setup_lib.setup(Path(args.install[0]), Path(args.install[1]), payload, exe=this_exe(),
                                shortcuts=not args.no_shortcuts)
            else:
                setup_lib.update(Path(args.update), payload, exe=this_exe(), shortcuts=not args.no_shortcuts)
            return 0
        open_window(payload, this_exe(), args.copy)
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4:** `python -m unittest tests.test_coop_app -v` → OK. Also a hand check of the window from source (no game is started): `python -m tools.coop_app --payload <any folder> --copy evidence\story-live-20261012\host` opens the main window over the disposable host copy; click through Solo/Host/Join panels and Back without pressing Start/Join. Close it.
- [ ] **Step 5: Commit** "Tester app: the window (setup, update, solo, host, join) and the exe entry point".

---

### Task 7: Package builder

**Files:**
- Create: `tools/build_tester_package.py`, `tools/tester_readme.txt`, `tests/test_build_tester_package.py`
- Modify: `tools/package_enhanced_story.py` (drop `package_friend_bundle`; neutral notice texts; `LOCAL_SOURCES` gains the app files), delete `tools/build_friend_setup.py`

**Interfaces:**
- Consumes: `package_enhanced_story.package(out) -> dict` (keys `archives`, `binaries`, and new `player_zip`, `source_zip` paths); `coop_play.VERSION`, `VERSION_LABEL`, `BUILD`; `friend_setup.APP_EXE`, `PAYLOAD_ZIP`, `PAYLOAD_JSON`.
- Produces: `python -m tools.build_tester_package --out evidence\<new folder>` → `<out>\CoF-Story-Coop-Test1.zip` and `<out>\package-report.json`; functions `dirty(repo: Path) -> list[str]`, `write_zip(folder: Path, path: Path) -> None`.

- [ ] **Step 1: Tests** `tests/test_build_tester_package.py`:

```python
"""The tester package builder's refusals and the zip it writes (no real build here)."""
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from tools import build_tester_package as builder


class BuilderTests(unittest.TestCase):
    def test_uncommitted_work_is_refused(self):
        reply = subprocess.CompletedProcess([], 0, " M engine/server/sv_client.c\n?? new.c\n", "")
        with mock.patch.object(builder.subprocess, "run", return_value=reply):
            self.assertEqual(builder.dirty(Path(".")), [" M engine/server/sv_client.c", "?? new.c"])
            with tempfile.TemporaryDirectory() as temp, self.assertRaisesRegex(ValueError, "Commit"):
                builder.build(Path(temp) / "out")

    def test_zip_layout(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp) / "bundle"
            for rel in ("CoF Story Co-op.exe", "READ ME FIRST.txt", "source-and-licences/corresponding-source.zip",
                        "source-and-licences/notices/PYTHON-LICENSE.txt"):
                (folder / rel).parent.mkdir(parents=True, exist_ok=True); (folder / rel).write_bytes(b"x")
            path = Path(temp) / "CoF-Story-Coop-Test1.zip"
            builder.write_zip(folder, path)
            with zipfile.ZipFile(path) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(sorted(archive.namelist())[0], "CoF Story Co-op.exe")
                self.assertIn("source-and-licences/notices/PYTHON-LICENSE.txt", archive.namelist())

    def test_readme_names_the_version_and_no_dev_tools(self):
        text = builder.README.read_text(encoding="utf-8")
        self.assertIn("Test 1", text)
        for word in ("protocol", "sha256", "rcon", "console"):
            self.assertNotIn(word, text.lower())
```

- [ ] **Step 2:** `python -m unittest tests.test_build_tester_package -v` → ImportError.
- [ ] **Step 3: Implement.**

`tools/package_enhanced_story.py`: `EXPERIMENTAL_NOTICE = "Cry of Fear story co-op tester build. Experimental: not an official Cry of Fear or Cry of Fear: Enhanced release.\nNo retail game files are included; the app copies them from the player's own Steam installation.\n"`; `FRIEND_README` removed; `START_REQUIREMENTS = EXPERIMENTAL_NOTICE`; in `package()` delete the `package_friend_bundle` call and function, add `report.update(player_zip=str(player_zip), source_zip=str(source_zip))`; `LOCAL_SOURCES`: replace `"tools/build_friend_setup.py"` with `"tools/build_tester_package.py", "tools/coop_app.py", "tools/coop_play.py", "tools/tester_readme.txt", "tools/make_inventory12_art.py", "cofcoop/network.py", "tests/test_coop_play.py", "tests/test_coop_app.py"`.

`tools/build_tester_package.py`:

```python
"""Build the tester package: CoF-Story-Coop-<version>.zip with the app exe, the readme and the source.

Run with the game closed is not needed: nothing here launches the game. The engine must be built
(tools\\build_enhanced.cmd) and every repository committed first.
"""
from __future__ import annotations

import argparse
from importlib.metadata import distribution
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import coop_play as play
from tools import friend_setup as setup_lib
from tools import package_enhanced_story as enhanced

README = ROOT / "tools/tester_readme.txt"
REPOS = {"launcher": ROOT, "engine": enhanced.ENGINE, "freevgui": enhanced.ENGINE / "3rdparty/freevgui"}
ZIP_NAME = f"CoF-Story-Coop-{play.VERSION_LABEL.replace(' ', '')}.zip"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dirty(repo: Path) -> list[str]:
    result = subprocess.run(["git", "-C", str(repo), "status", "--porcelain"], capture_output=True, text=True, check=True)
    return [line for line in result.stdout.splitlines() if line.strip()]


def write_zip(folder: Path, path: Path) -> None:
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in sorted(folder.rglob("*")):
            if item.is_file():
                archive.write(item, item.relative_to(folder).as_posix())
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("The package zip failed its CRC check.")


def notices(target: Path) -> None:
    target.mkdir(parents=True)
    licences = {"PYTHON-LICENSE.txt": Path(sys.base_prefix) / "LICENSE_PYTHON.txt",
                "TK-LICENSE.txt": Path(sys.base_prefix) / "Library/lib/tk8.6/license.terms"}
    package = distribution("pyinstaller")
    licences["PYINSTALLER-COPYING.txt"] = next(Path(package.locate_file(f)) for f in package.files if str(f).endswith("licenses/COPYING.txt"))
    for name, path in licences.items():
        shutil.copyfile(path, target / name)


def build(out: Path) -> dict:
    problems = {name: lines for name, path in REPOS.items() if (lines := dirty(path))}
    if problems:
        raise ValueError(f"Commit everything first, so the source matches the build: {problems}")
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    stage = enhanced.package(out / "enhanced")
    payload = out / "payload"
    payload.mkdir()
    shutil.copyfile(stage["player_zip"], payload / setup_lib.PAYLOAD_ZIP)
    manifest = {"schema": 2, "version": play.VERSION, "build": play.BUILD,
                "xash_sha256": stage["binaries"]["xash.dll"]["sha256"]}
    (payload / setup_lib.PAYLOAD_JSON).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    setup_lib.inspect_payload(payload)
    name = Path(setup_lib.APP_EXE).stem
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--windowed", "--name", name,
               "--paths", str(ROOT), "--add-data", f"{payload}{os.pathsep}payload",
               "--distpath", str(out / "dist"), "--workpath", str(out / "work"), "--specpath", str(out),
               str(ROOT / "tools/coop_app.py")]
    with (out / "pyinstaller.log").open("wb") as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    exe = out / "dist" / setup_lib.APP_EXE
    subprocess.run([str(exe), "--self-test"], check=True, timeout=120)
    bundle = out / "bundle"
    (bundle / "source-and-licences").mkdir(parents=True)
    shutil.copyfile(exe, bundle / setup_lib.APP_EXE)
    shutil.copyfile(README, bundle / "READ ME FIRST.txt")
    shutil.copyfile(stage["source_zip"], bundle / "source-and-licences/corresponding-source.zip")
    notices(bundle / "source-and-licences/notices")
    path = out / ZIP_NAME
    write_zip(bundle, path)
    report = {"zip": str(path), "zip_sha256": digest(path), "zip_bytes": path.stat().st_size,
              "exe_sha256": digest(exe), "payload": manifest,
              "commits": {name: subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True,
                                               text=True, check=True).stdout.strip() for name, repo in REPOS.items()}}
    (out / "package-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="a new folder under evidence\\")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(build(args.out), indent=2))
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Package refused: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

(The `enhanced.package` call puts its output in `<out>/enhanced`, which must be a new folder under `evidence/`, as that function already requires.)

`tools/tester_readme.txt` (the zip's `READ ME FIRST.txt`):

```text
Cry of Fear story co-op - Test 1

Play the Cry of Fear campaign together (2 to 4 players), or alone with an AI companion.
This is an early test build: expect bugs, and please report them.

YOU NEED
- Windows 10 or 11, and Cry of Fear installed from Steam (it's free). Keep Steam running.
- About 6 GB of free disk space.

INSTALL
1. Extract this zip to a normal folder, such as Downloads (not into the game folder).
2. Double-click "CoF Story Co-op.exe". Windows may warn you because the app isn't signed:
   click "More info", then "Run anyway".
3. Click "Set up". The app finds your game through Steam (or use Browse to pick the
   Cry of Fear folder) and makes a separate copy for co-op. Your Steam game, saves and
   settings are not changed.
4. Afterwards, start it with the "Cry of Fear Co-op" shortcut on your desktop or in the
   Start menu.

PLAY
- Play solo: you and an AI companion.
- Host a game: friends join you. Send them the join code the app shows.
  If they can't connect, your router didn't open the port by itself: forward UDP 27015
  to your PC in the router, or all use a VPN such as Tailscale, ZeroTier or Radmin and
  share that address. If Windows Firewall asks about Cry of Fear, allow it.
- Join a game: paste the host's join code (or address) and their password, if any.
Everyone must use the same test package.

Start a new game (the intro), any chapter, or Continue from the last co-op checkpoint.
The game makes a co-op checkpoint by itself after every level change.

CO-OP KEYS
J      join your partner (when the screen says "Press J to join")
Enter  skip a cutscene for everyone (press again to watch the rest)
H      tell the AI companion to wait or to follow
Space  when dead: respawn next to your partner after about 10 seconds

UPDATES
When you get a newer package, run its "CoF Story Co-op.exe" and click "Update".
Your saves and settings are kept.

UNINSTALL
Delete the CoF-Story-Coop folder (%LOCALAPPDATA%\CoF-Story-Coop unless you chose
another place) and the two "Cry of Fear Co-op" shortcuts.

REPORTING BUGS
Click "Report a bug" in the app, or send the details to whoever gave you this package:
what happened, the map or the place, roughly when, and the log ("Open logs folder" in
the app; the log may show your Windows user name). Never post your password or join
code publicly.

The engine is free software (GPL); its source is in the source-and-licences folder.
Cry of Fear itself is not included: the app uses your own Steam copy.
```

Delete `tools/build_friend_setup.py` (`git rm`).

- [ ] **Step 4:** `python -m unittest tests.test_build_tester_package -v` → OK.
- [ ] **Step 5: Commit** "Tester package builder: one zip with the app, the readme and the source".

---

### Task 8: Docs and notes

**Files:** Modify `docs/playtesting.md` (rewrite for the app: install, play, keys, updates, reporting; keep "What works so far" and "Known problems"), `README.md` (Playtesters: "Download the tester zip, read READ ME FIRST.txt, see docs/playtesting.md"; Hosting a playtest: mention the app), `docs/building.md` (the package section: `python -m tools.build_tester_package --out evidence\<new folder>`), `AGENT_NOTES.md` (new section "Tester package (2026-10-15)", Next steps, build table).

- [ ] **Step 1:** Write the docs. `docs/playtesting.md` starts from `tools/tester_readme.txt` (same wording for install, play, keys) and keeps the existing "How co-op behaves", "What works so far", "Known problems" and log/report sections, with `story-session.log` instead of `story-tester.log`.
- [ ] **Step 2:** `python -m unittest discover -s tests -p "test_*.py"` → all OK (expect about 200 tests).
- [ ] **Step 3: Commit** "Docs: the tester app".

---

### Task 9: Build, package, smoke install

**Files:** none in git; evidence in `evidence/tester-package-20261015/`.

- [ ] **Step 1: Engine build:** `cmd /d /c tools\build_enhanced.cmd > evidence\tester-package-20261015\build.log 2>&1` → exit 0; record `xash.dll` and `vgui.dll` SHA-256.
- [ ] **Step 2: Full suite:** `python -m unittest discover -s tests -p "test_*.py" > evidence\tester-package-20261015\full-suite.log 2>&1` → OK.
- [ ] **Step 3: Commit anything left, then package:** `git status --short` empty in the three repos; `python -m tools.build_tester_package --out evidence\tester-package-20261015\package` → prints the report (zip path, hashes, commits); `pyinstaller.log` has no error; the self-test ran.
- [ ] **Step 4: Smoke install** (≈5 GB on C:, no shortcuts): `Start-Process -Wait -PassThru "evidence\tester-package-20261015\package\dist\CoF Story Co-op.exe" -ArgumentList '--install', '"<repo>\baseline\Cry of Fear"', '"<repo>\evidence\tester-package-20261015\smoke\CoF-Story-Coop"', '--no-shortcuts'` → ExitCode 0. Check: `cof-story-installed.json` (schema 2, build 1), `xash.dll` hash = the report's, `vgui.dll` = the build's, `cryoffear\gfx\vgui\inventory12_full.tga` exists, `cryoffear\cof_story.cfg` has `cof_test_gun 0` and no `rcon`, `CoF Story Co-op.exe` in the copy, the Steam folder untouched (not opened at all), `baseline` unchanged (`git`-ignored; compare `pristine-manifest.sha256` for the DLL only).
- [ ] **Step 5: Update path:** the same exe with `--update <smoke copy> --no-shortcuts` → ExitCode 0; saves/configs unchanged; `friend-setup-install.log` says it upgraded in place.
- [ ] **Step 6: Extract test:** unzip `CoF-Story-Coop-Test1.zip` into `evidence\tester-package-20261015\unzipped\`: exactly the exe, `READ ME FIRST.txt`, `source-and-licences\`.

---

### Task 10: Live check (only with the user's go-ahead)

- [ ] **Step 1:** Ask the user. Then run `"<smoke copy>\CoF Story Co-op.exe"` (opens the main window over the smoke copy). Play solo → New game: the game opens borderless at the desktop size, no console key, the companion joins; `story-session.log` has `cof_build`… and no `rcon`.
- [ ] **Step 2:** With the game running, from this repo: `python -c "from cofcoop import network; print(network.query_info(network.Endpoint('127.0.0.1', 27015)))"` shows `cofbuild: test1`.
- [ ] **Step 3:** Host (2 players) and check the router line and join code appear; the user (or a friend) joins if they want to.
- [ ] **Step 4:** Update `AGENT_NOTES.md` with the live results (dated), commit.
- [ ] **Step 5:** Hand over: the zip path, its SHA-256 and size, and what is not done (signing, auto-update, companion fixes).
