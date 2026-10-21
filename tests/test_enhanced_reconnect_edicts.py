"""A joining player crashed right after a level change (CL_ParseServerMessage -> CL_ParseMovevars,
clgame.entities->curstate.scale with no entities: the movss at xash.dll RVA 0x196E4F in the 1.1.4 build). A level
change keeps the edicts that svc_changing or the story transition (CL_CoF_StoryChangeLevel_f) made, so serverdata
skips CL_InitEdicts while cls.changelevel is set; a client that disconnected in between (a "reconnect" while it was
still loading the last level) has none left. Serverdata now makes them whenever there are none, and movevars no
longer writes into a missing edict 0."""
import unittest

from tests.native import ROOT, assert_has
from tests.test_enhanced_sections import body_of

PARSE = ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/client/parse/cl_parse.c'


class ReconnectEdictTests(unittest.TestCase):
    def test_serverdata_makes_edicts_whenever_there_are_none(self):
        body = body_of(PARSE, 'static void CL_ParseServerData( sizebuf_t *msg, connprotocol_t proto )')
        assert_has(self, 'if(( !cls.changelevel && !cls.changedemo ) || !clgame.entities )\n'
                         '\t\tCL_InitEdicts( cl.maxclients ); // re-arrange edicts', body)

    def test_movevars_never_writes_into_a_missing_world_edict(self):
        body = body_of(PARSE, 'void CL_ParseMovevars( sizebuf_t *msg )')
        assert_has(self, 'if( clgame.entities )\n\t\tclgame.entities->curstate.scale = clgame.movevars.waveHeight;', body)


if __name__ == '__main__':
    unittest.main()
