"""Client commands the retail game trusts too much in a co-op story (docs/research/2026-10-18-cof-decompilation-
survey.md 4 and 7, engine/server/sv_cof_guard.h): votes (anyone could vote a partner out; one vote fills every slot),
and a guest changing the host's server-wide settings (subtitle language, hints). Chat and names cannot carry a printf
format: Xash turns every '%' a client sends into '.', which client.dll's chat labels (the line is their format
string) need."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, ROOT, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class GuardRuleTests(unittest.TestCase):
    def test_refusals(self):
        header = strip_includes((SERVER / 'sv_cof_guard.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_guard_harness.c', {'guard_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'vote story=1 host=1: vote',
            'votekick story=1 host=0: vote',
            'vote story=0 host=0: -',
            'subtitleset story=1 host=0: host-setting',
            'subtitleset story=1 host=1: -',
            'hinttoggle story=1 host=0: host-setting',
            'say story=1 host=0: -',
            'Vote story=1 host=0: -',
            'unfreeze 1024 held=1: scene-holds',
            'unfreeze 1024 held=0: -',
            'unfreeze 512 held=1: -',
            'unfreeze 666 held=1: -',
            'stopcomputer  held=1: -',
            'freeze 1024 held=1: -',
        ])


class GuardWiringTests(unittest.TestCase):
    def test_refused_before_the_game_sees_it(self):
        body = body_of(SERVER / 'sv_client.c', 'static void SV_ExecuteClientCommand( sv_client_t *cl, const char *s )\n{')
        self.assertLess(body.index('SV_CoF_GuardClientCommand( cl )'), body.index('svgame.dllFuncs.pfnClientCommand( cl->edict )'))

    def test_a_camera_hold_is_asked_of_the_game_state(self):
        # every viewport reset (map load, HUD_VidInit) sends "unfreeze 1024": while a trigger_camera holds the view
        # the camera's own end gives control back (CTriggerCamera, triggers.cpp:5565-5583, 5890-5905)
        body = body_of(SERVER / 'sv_cof_guard.c', 'qboolean SV_CoF_GuardClientCommand( sv_client_t *cl )')
        assert_has(self, 'CoF_GuardSceneRefusal( Cmd_Argv( 0 ), Cmd_Argv( 1 ), SV_CoF_RetailCameraHolds( cl ))', body)

    def test_the_host_is_the_local_human(self):
        body = body_of(SERVER / 'sv_cof_guard.c', 'qboolean SV_CoF_GuardClientCommand( sv_client_t *cl )')
        assert_has(self, 'NET_IsLocalAddress( cl->netchan.remote_address ) && !FBitSet( cl->flags, FCL_FAKECLIENT )', body)

    def test_no_printf_format_reaches_the_game_from_a_client(self):
        reader = body_of(ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/common/net_buffer.c',
                         'static char *MSG_ReadStringExt( sizebuf_t *sb, qboolean bLine )')
        assert_has(self, "if( c == '%' ) c = '.';", reader)


if __name__ == '__main__':
    unittest.main()
