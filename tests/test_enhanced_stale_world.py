"""A decal from the world the listen server just replaced (engine/client/parse/cl_parse.c CL_ParseUserMessage). On a
listen server the server loads the next world into the model slot the local client draws from; the client sets it up
later (R_NewMap, cl.world_generation). The engine keeps a client's unsent messages across a level change (sv_frame.c
SV_InactivateClients, for the game's HideHud and PlayMp3). Live 2026-10-19: a level change in the frame of a respawn,
whose init messages carry every env_static_decal's "customdecal" (player.cpp SendInitMessages in the decompilation),
delivered cm_sewer3's decals after cm_basement was loaded, and the retail client.dll's CreateDecal read a world surface
whose polygons R_NewMap had not built (client.dll RVA 0x580A8, from its customdecal handler 0x594E9)."""
from pathlib import Path
import unittest

from tests.native import assert_has
from tests.test_enhanced_sections import body_of

CLIENT = Path(__file__).resolve().parents[1] / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/client'
GUARD = ('if( !Q_strcmp( clgame.msg[i].name, "customdecal" ) && clgame.entities && clgame.entities->model &&\n'
         '\t\tcl.world_generation != Mod_WorldGeneration( ))\n\t\treturn;')


class StaleWorldDecalTests(unittest.TestCase):
    def test_an_old_worlds_decal_never_reaches_the_client_dll(self):
        body = body_of(CLIENT / 'parse/cl_parse.c', 'void CL_ParseUserMessage(')
        assert_has(self, GUARD, body)
        # read first, so the rest of the packet still parses; dropped before the game's handler
        self.assertLess(body.index('MSG_ReadBytes( msg, pbuf, sizeof( pbuf ), iSize );'), body.index(GUARD))
        self.assertLess(body.index(GUARD), body.index('clgame.msg[i].func( clgame.msg[i].name, iSize, pbuf );'))


if __name__ == '__main__':
    unittest.main()
