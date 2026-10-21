"""1.1.8, from the final review (E3): a guest who loses the level-change notice, or is still signing on when the party
leaves, is told again and signs on to the new map instead of waiting until the connect timeout drops (and bans) it;
guest sign-ons are LZSS-compressed; F9 on a guest's loading screen stays in co-op; client.dll's prediction copy gets
room for its donator field; the end of the story returns everyone to the menu in co-op too."""
import unittest

from tests.native import ROOT, assert_has
from tests.test_enhanced_sections import body_of

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
SERVER = ENGINE / 'server'
CLIENT = ENGINE / 'client'


class NoticeTests(unittest.TestCase):
    def test_the_notice_carries_the_map_left(self):
        final = body_of(SERVER / 'sv_main.c', 'void SV_FinalMessage(')
        assert_has(self, 'sv_cof_notice_epoch = svs.spawncount;', final)
        assert_has(self, 'MSG_WriteStringf( &msg, "cof_story_changelevel %d\\n", sv_cof_notice_epoch );', final)

    def test_a_guest_still_signing_on_is_told_again(self):
        notices = body_of(SERVER / 'sv_client.c', 'void SV_CoF_StoryNotices( void )')
        for line in ('if( cl->state != cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT ) || Netchan_IsLocal( &cl->netchan )) continue;',
                     'if( cof_signon[i].userid == cl->userid && cof_signon[i].spawncount == svs.spawncount ) continue;',
                     'if( host.realtime < cl->connection_started + 1.5 ) continue;',
                     'MSG_BeginServerCmd( &cl->netchan.message, svc_stufftext );'):
            assert_has(self, line, notices)
        send = body_of(SERVER / 'sv_frame.c', 'void SV_SendClientMessages( void )')
        assert_has(self, 'SV_CoF_StoryNotices();', send)
        new = body_of(SERVER / 'sv_client.c', 'static qboolean SV_New_f( sv_client_t *cl )')
        self.assertLess(new.index('if( cl->state != cs_connected )'), new.index('cof_signon[cl - svs.clients].spawncount = svs.spawncount;'))
        assert_has(self, 'ClearBits( cl->flags, FCL_SEND_RESOURCES );', new)

    def test_a_begin_for_a_map_left_starts_the_sign_on_again(self):
        begin = body_of(SERVER / 'sv_client.c', 'static qboolean SV_Begin_f( sv_client_t *cl )')
        restart = begin.index('if( cl->state == cs_connected && SV_CoF_StoryCoopEnabled() && !FBitSet( cl->flags, FCL_FAKECLIENT ))')
        self.assertLess(restart, begin.index('if( cl->state != cs_spawning )'))
        assert_has(self, 'SV_New_f( cl );', begin[restart:])

    def test_guest_sign_ons_are_compressed(self):
        source = (SERVER / 'sv_client.c').read_text(encoding='latin-1')
        assert_has(self, 'if( !NET_IsLocalAddress( from ))\n\t\tSetBits( netchan_flags, NETCHAN_USE_LZSS );', source)

    def test_the_info_reply_names_the_merged_maps(self):
        # L8: the app compares the host's merged maps with the guest's before a join
        source = (SERVER / 'sv_client.c').read_text(encoding='latin-1')
        assert_has(self, 'if( build[0] && maps[0] ) Info_SetValueForKey( s, "cofmaps", maps, sizeof( s ));', source)
        story = (SERVER / 'sv_cof_story.c').read_text(encoding='latin-1')
        assert_has(self, 'Cvar_Get( "cof_maps", "", 0,', story)

    def test_no_ban_for_a_slow_guest(self):
        play = (ROOT / 'tools/coop_play.py').read_text(encoding='utf-8')
        assert_has(self, '"sv_connect_timeout_ban 0"', play)


class ClientTests(unittest.TestCase):
    def test_quick_load_stays_in_co_op_between_maps(self):
        quick = body_of(CLIENT / 'cof_quicksave.c', 'static qboolean CL_CoF_QuickMultiplayer( void )')
        assert_has(self, 'if( cls.state >= ca_connected && !NET_IsLocalAddress( cls.serveradr ))', quick)

    def test_prediction_copy_has_room_for_the_donator_field(self):
        parse = (CLIENT / 'parse/cl_parse.c').read_text(encoding='latin-1')
        assert_has(self, 'clgame.dllFuncs.pfnTxferPredictionData( ps, pps, &cof_pcd.cd, &cof_ppcd.cd, wd, pwd );', parse)
        assert_has(self, '*pcd = cof_pcd.cd;', parse)

    def test_the_end_of_the_story_returns_to_the_menu_in_co_op(self):
        redirect = body_of(CLIENT / 'cl_main.c', 'qboolean CL_CoF_MenuMapRedirect( const char *cmd, const char *source )')
        backstop = body_of(CLIENT / 'cl_main.c', 'qboolean CL_CoF_MenuMapLevelChange( const char *mapname, const char *source )')
        self.assertNotIn('if( cl.maxclients > 1 )', redirect)
        self.assertNotIn('if( cl.maxclients > 1 )', backstop)


if __name__ == '__main__':
    unittest.main()
