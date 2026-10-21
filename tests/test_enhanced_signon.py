"""Faster story level changes (engine/server/sv_cof_signon.h): a guest's sign-on went out one 1.4 KB reliable fragment
per round trip; it now goes ten split pieces per round trip (TCP's initial window, RFC 6928), each piece still at
cl_dlmax so nothing is IP-fragmented (RFC 8900). The socket queues get room for the burst, and the microphone stays
open across a level change instead of being closed and opened in every sign-on."""
import unittest

from tests.native import ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output

ENGINE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine'
SERVER = ENGINE / 'server'


class SignonTests(unittest.TestCase):
    def test_the_rule(self):
        header = strip_includes((SERVER / 'sv_cof_signon.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_signon_harness.c', {'signon_under_test.h': header})
        self.assertEqual(program_output(out), [
            'co-op cl_dlmax 1400 signing on: 13836 bytes in 10 pieces of at most 1400',
            "co-op cl_dlmax 1400 in game: the engine's",
            "no split size: the engine's",
            "cl_dlmax 508: the engine's",
            'cl_dlmax 1200: 11836 bytes in 10 pieces of at most 1200',
            'cl_dlmax 64000: 63966 bytes in 2 pieces of at most 64000',
            'window 1: 0',
        ])

    def test_the_server_uses_it_only_for_split_clients_signing_on(self):
        size = body_of(SERVER / 'sv_client.c', 'static int SV_GetFragmentSize( void *pcl, fragsize_t mode )')
        rule = size.index('CoF_SignonFragmentSize( cl_frag_size, cl->state == cs_spawned, COF_SIGNON_WINDOW, FRAGMENT_MAX_SIZE - HEADER_BYTES )')
        self.assertLess(size.index('if( mode != FRAGSIZE_FRAG )'), rule)
        self.assertLess(size.index('if( FBitSet( cl->extensions, NET_EXT_SPLITSIZE ))'), rule)
        self.assertLess(rule, size.index('if( cl->state == cs_spawned )'))
        self.assertLess(size.index('return FRAGMENT_LOCAL_SIZE;'), rule)

    def test_sockets_have_room_for_a_burst(self):
        net = (ENGINE / 'common/net_ws.c').read_text(encoding='latin-1')
        socket = body_of(ENGINE / 'common/net_ws.c', 'static int NET_IPSocket( const char *net_iface, int port, int family )')
        assert_has(self, 'NET_RaiseSocketBuffer( net_socket, SO_RCVBUF, "SO_RCVBUF", port );', socket)
        assert_has(self, 'NET_RaiseSocketBuffer( net_socket, SO_SNDBUF, "SO_SNDBUF", port );', socket)
        assert_has(self, '#define NET_SOCKET_BUFFER ( 256 * 1024 )', net)

    def test_the_microphone_stays_open_across_a_level_change(self):
        init = body_of(ENGINE / 'client/sound/voice.c', 'qboolean Voice_Init( const char *pszCodecName, int quality, qboolean preinit )')
        same = init.index('if( same )')
        self.assertLess(init.index('if( preinit )'), same)
        self.assertLess(same, init.index('if( Voice_IsGoldSrcMode( pszCodecName ))'))
        self.assertLess(init.index('const qboolean same ='), init.index('Q_strncpy( voice.codec, pszCodecName'))
        assert_has(self, 'if( voice.encoder || voice.gs_encoder || voice.custom_mode )\n\t\tVoice_Disconnect();', init)


if __name__ == '__main__':
    unittest.main()
