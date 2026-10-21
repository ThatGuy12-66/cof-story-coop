"""Players stamped by their own commands (engine/server/sv_cof_cmdclock.h), 1.1.9: a guest's position was stamped with
the server frame its packet arrived in, so commands slipping across a host frame drew them speeding up and slowing
down. Each person has a command clock now, and players' animtime travels in milliseconds (movement research 3a, 5)."""
import unittest

from tests.native import ENGINE_SERVER, assert_has, run_native
from tests.test_enhanced_interp import function

ENGINE = ENGINE_SERVER.parent


class CommandClockTests(unittest.TestCase):
    def test_clock(self):
        out = run_native(self, 'cof_cmdclock_test.c', {
            'sv_cof_cmdclock.h': (ENGINE_SERVER / 'sv_cof_cmdclock.h').read_text(),
        })
        self.assertIn('command clock passed', out)

    def test_every_command_of_a_person_moves_their_clock(self):
        body = function((ENGINE_SERVER / 'sv_pmove.c').read_text(encoding='latin-1'),
                        'void SV_RunCmd( sv_client_t *cl, usercmd_t *ucmd, int random_seed )')
        # after the speed-hack check and the chop (each half runs here), with the time base the game's stamp comes from
        self.assertLess(body.index('cl->ignorecmdtime = 0.0;'), body.index('SV_CoF_CmdClockRun( cl, frametime );'))
        self.assertLess(body.index('cl->timebase += frametime;'), body.index('SV_CoF_CmdClockRun( cl, frametime );'))
        run = function((ENGINE_SERVER / 'sv_pmove.c').read_text(encoding='latin-1'),
                       'void SV_CoF_CmdClockRun( const sv_client_t *cl, double seconds )')
        assert_has(self, 'FBitSet( cl->flags, FCL_FAKECLIENT )', run)  # an AI companion's commands are made per frame
        assert_has(self, 'CoF_CmdClockAdvance( cof_cmdclock[i].clock, seconds, sv.time + sv.frametime )', run)
        assert_has(self, 'cof_cmdclock[i].spawncount != svs.spawncount || cof_cmdclock[i].userid != cl->userid', run)
        stamp = function((ENGINE_SERVER / 'sv_pmove.c').read_text(encoding='latin-1'),
                         'float SV_CoF_CmdClockStamp( int e, float animtime )')
        assert_has(self, '!SV_CoF_StoryCoopEnabled( )', stamp)
        assert_has(self, 'cof_cmdclock[i].userid != svs.clients[i].userid', stamp)

    def test_players_are_sent_with_it(self):
        body = function((ENGINE_SERVER / 'sv_frame.c').read_text(encoding='latin-1'),
                        'static void SV_AddEntitiesToPacket( edict_t *pViewEnt, edict_t *pClient, client_frame_t *frame, '
                        'sv_ents_t *ents, qboolean from_client )')
        full_pack = body.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient, sv.hostflags, player, pset )')
        stamp = body.index('if( player ) state->animtime = SV_CoF_CmdClockStamp( e, state->animtime );')
        self.assertLess(full_pack, stamp)
        self.assertLess(stamp, body.index('ents->num_entities++;'))

    def test_players_animtime_in_milliseconds(self):
        encode = (ENGINE / 'common/net_encode.c').read_text(encoding='latin-1')
        init = function(encode, 'void Delta_Init( void )')
        self.assertLess(init.index('Delta_InitFields ();'), init.index('Delta_CoF_PlayerAnimtime ();'))
        change = function(encode, 'static void Delta_CoF_PlayerAnimtime( void )')
        for line in ('Delta_FindStruct( "entity_state_player_t" )', 'Q_strcmp( pField->name, "animtime" )',
                     '!FBitSet( pField->flags, DT_TIMEWINDOW_8 )', 'ClearBits( pField->flags, DT_TIMEWINDOW_8 );',
                     'SetBits( pField->flags, DT_TIMEWINDOW_BIG );', 'pField->bits = 16;', 'pField->multiplier = 1000.0f;'):
            assert_has(self, line, change)
        # every client takes the server's description of each field (older 1.1 clients too): flags in 10 bits, bits - 1
        # in 5, the multiplier as a float; the time is relative to the server's (sv.time) and the client's (cl.mtime[0])
        table = function(encode, 'static void Delta_WriteTableField( sizebuf_t *msg, int tableIndex, const delta_t *pField )')
        assert_has(self, 'MSG_WriteUBitLong( msg, pField->flags, 10 );', table)
        assert_has(self, 'MSG_WriteUBitLong( msg, pField->bits - 1, 5 );', table)
        assert_has(self, 'MSG_WriteFloat( msg, pField->multiplier );', table)
        parse = function(encode, 'void Delta_ParseTableField( sizebuf_t *msg )')
        assert_has(self, 'Delta_AddField( dt, pName, flags, bits, mul, post_mul );', parse)
        assert_has(self, 'dt = Q_rint(( timebase - flValue ) * pField->multiplier );', encode)
        assert_has(self, 'flTime = ( timebase * pField->multiplier - (int)iValue ) / pField->multiplier;', encode)
        frame = (ENGINE_SERVER / 'sv_frame.c').read_text(encoding='latin-1')
        assert_has(self, 'MSG_WriteDeltaEntity( oldent, newent, msg, false, player, sv.time, 0 );', frame)
        client = (ENGINE / 'client/cl_frame.c').read_text(encoding='latin-1')
        assert_has(self, 'MSG_ReadDeltaEntity( msg, old, state, newnum, delta_type, cl.mtime[0] );', client)


if __name__ == '__main__':
    unittest.main()
