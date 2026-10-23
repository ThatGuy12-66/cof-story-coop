"""Checkpoint validation, identity matching and failure-safe file staging."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native, strip_includes


def session_headers():
    return {name: (ENGINE_SERVER / name).read_text() for name in (
        'sv_cof_session_data.h', 'sv_cof_archive.h', 'sv_cof_snapshot.h',
        'sv_cof_state_data.h', 'sv_cof_share_state.h', 'sv_cof_descriptor.h',
    )}


class SessionTests(unittest.TestCase):
    def test_checkpoint_lifecycle_and_commands(self):
        source = strip_includes((ENGINE_SERVER / 'sv_cof_session.inc').read_text())
        start = source.index('void SV_CoF_SessionFrame')
        end = source.index('void SV_CoF_SessionInit', start)
        output = run_native(self, 'cof_session_lifecycle_test.c', {
            'session_lifecycle_under_test.c': source[start:end],
        })
        self.assertIn('session lifecycle scenarios passed', output)

    def test_format_and_identity(self):
        files = session_headers()
        paths = sorted((ROOT / 'runtimes/enhanced/SAVE').glob('*.HL[123]'))
        files['session_fixture_paths.h'] = (
            'static const char *fixture_paths[] = {' +
            ','.join('"' + path.as_posix() + '"' for path in paths) +
            ',NULL};' if paths else 'static const char *fixture_paths[] = {NULL};')
        output = run_native(self, 'cof_session_test.c', files)
        self.assertIn('session validation scenarios passed', output)

    def test_staging_rolls_back_io_failures(self):
        files = session_headers()
        source = strip_includes((ENGINE_SERVER / 'sv_cof_session.inc').read_text())
        start = source.index('static byte *CoF_SessionReadFile')
        end = source.index('static byte *CoF_SessionGlobalCapture')
        stage = source.index('static qboolean CoF_SessionStage')
        stage_end = source.index('static qboolean CoF_SessionCurrentNames', stage)
        files['session_io_under_test.c'] = source[start:end] + source[stage:stage_end]
        output = run_native(self, 'cof_session_io_test.c', files)
        self.assertIn('session staging scenarios passed', output)


class SessionWiringTests(unittest.TestCase):
    def test_a_name_taken_while_its_player_loads_is_taken(self):
        # 1.2.6: checkpoints know players by name; Xash's duplicate check saw only spawned clients, so someone who
        # joined while the host was loading a level kept the host's name, and every checkpoint was refused silently
        from tests.native import assert_has
        from tests.test_enhanced_sections import body_of
        body = body_of(ENGINE_SERVER / 'sv_client.c', 'static void SV_UserinfoChanged( sv_client_t *cl )\n{')
        assert_has(self, 'if( current == cl || ( current->state != cs_spawned && !( current->state >= cs_connected && '
                         'SV_CoF_StoryCoopEnabled( ) &&', body)
        # ...but not a slot still joining from the player's own address: their own earlier try, until it times out
        assert_has(self, '!NET_CompareBaseAdr( current->netchan.remote_address, cl->netchan.remote_address ))))', body)
        save = body_of(ENGINE_SERVER / 'sv_cof_session.inc', 'qboolean SV_CoF_SessionSave( const char *slot )\n{')
        assert_has(self, 'reason=same-names', save)

    def test_a_wipe_loads_only_this_sessions_own_checkpoint(self):
        # 1.2.6: latest.txt names the previous session's checkpoint until a new game's first autosave lands
        from tests.native import assert_has
        from tests.test_enhanced_sections import body_of
        source = ENGINE_SERVER / 'sv_cof_session.inc'
        wipe = body_of(source, 'qboolean SV_CoF_SessionWipe( const char *map, const char *landmark )\n{')
        self.assertLess(wipe.index('if( !cof_session_owned )'), wipe.index('SV_CoF_SessionLoad( NULL )'))
        assert_has(self, 'cof_session_owned = true;', body_of(source, 'qboolean SV_CoF_SessionSave( const char *slot )\n{'))
        assert_has(self, 'cof_session_owned = true;', body_of(source, 'static qboolean CoF_SessionRunLoad( void )\n{'))
        assert_has(self, 'cof_session_owned = false;', body_of(source, 'void SV_CoF_SessionReset( void )\n{'))

    def test_the_save_screen_is_seen(self):
        from tests.native import assert_has
        from tests.test_enhanced_sections import body_of
        body = body_of(ENGINE_SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        assert_has(self, 'SV_CoF_SessionMessage( svgame.msg_ent, name, sv.multicast.pData + svgame.msg_size_index + 2, svgame.msg_realsize );', body)


if __name__ == '__main__':
    unittest.main()
