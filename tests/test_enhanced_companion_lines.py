"""The AI companions are dropped and added again at every level change; the game announced each one ("Companion has
joined the game", "- Companion has left the game") after every door (docs/research/2026-10-18-cof-decompilation-
survey.md section 3). A new bot's HUD-initialized flag is set before the game's first UpdateClientData, and the game's
messages from a bot dropped for a level change are not sent."""
import unittest

from tests.native import ROOT, assert_has
from tests.test_enhanced_sections import body_of

SERVER = ROOT / 'engine/engine/server'


class CompanionLinesTests(unittest.TestCase):
    def test_a_new_companion_is_not_announced(self):
        body = body_of(SERVER / 'sv_cof_companion.c', 'static qboolean CoF_CompanionAdd( void )')
        put = body.index('svgame.dllFuncs.pfnClientPutInServer( ent );')
        mark = body.index('SV_CoF_RetailHudInitialized( ent );')
        self.assertLess(put, mark)
        retail = (SERVER / 'sv_cof_retail.c').read_text(encoding='latin-1')
        assert_has(self, '#define COF_RETAIL_HUD_INITIALIZED 0x24D0', retail)
        mark_body = body_of(SERVER / 'sv_cof_retail.c', 'qboolean SV_CoF_RetailHudInitialized( edict_t *ent )')
        assert_has(self, '*(int *)( pv + COF_RETAIL_HUD_INITIALIZED ) = 1;', mark_body)

    def test_a_companion_dropped_for_a_level_change_leaves_quietly(self):
        source = (SERVER / 'sv_frame.c').read_text(encoding='latin-1')
        drop = source.index('if( FBitSet( cl->edict->v.flags, FL_FAKECLIENT ) || FBitSet( cl->flags, FCL_FAKECLIENT ))')
        block = source[drop:drop + 300]
        quiet, dropped, loud = (block.index('SV_CoF_QuietGameMessages( true );'), block.index('SV_DropClient( cl, false );'),
                                block.index('SV_CoF_QuietGameMessages( false );'))
        self.assertLess(quiet, dropped)
        self.assertLess(dropped, loud)

    def test_quiet_game_messages_are_not_sent(self):
        body = body_of(SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        quiet = body.index('if( SV_CoF_GameMessagesQuiet( ))')
        self.assertLess(quiet, body.index('SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );'))
        assert_has(self, 'MSG_Clear( &sv.multicast );', body[quiet:quiet + 120])


if __name__ == '__main__':
    unittest.main()
