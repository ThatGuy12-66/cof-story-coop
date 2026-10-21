"""The model a guest's view rides on a ladder (engine/client/cl_frame.c), 1.1.9: client.dll draws it at the frame sent
(no interpolation for the AttachCam entity) and the server moves that frame only with the climber's usercmds, so a
guest's view stepped at packet rate. Between packets its frame now moves on at the sequence's rate (ladders research,
fix 3)."""
import re
import unittest

from tests.native import ENGINE_SERVER, assert_has, run_native
from tests.test_enhanced_interp import function

CLIENT = ENGINE_SERVER.parent / 'client'


class AttachCamTests(unittest.TestCase):
    def test_frame_between_packets(self):
        source = (CLIENT / 'cl_frame.c').read_text(encoding='latin-1')
        ahead = re.search(r'#define CL_COF_ATTACHCAM_AHEAD .*\n', source).group(0)
        typedef_end = source.index('} cl_cof_attachcam_t;') + len('} cl_cof_attachcam_t;')
        typedef = source[source.rindex('typedef struct', 0, typedef_end):typedef_end]
        estimate = function(source, 'static float CL_CoF_AttachCamEstimate(')
        out = run_native(self, 'cof_attachcam_harness.c', {
            'attachcam_under_test.c': ahead + typedef + '\n\n' + estimate + '\n'})
        self.assertIn('attach camera frame passed', out)

    def test_only_the_model_the_view_rides_on_a_guest(self):
        source = (CLIENT / 'cl_frame.c').read_text(encoding='latin-1')
        frame = function(source, 'static void CL_CoF_AttachCamFrame( cl_entity_t *ent, const entity_state_t *state )')
        assert_has(self, 'state->number != cl_cof_attachcam', frame)
        assert_has(self, 'NET_IsLocalAddress( cls.netchan.remote_address )', frame)  # the host's view as it was
        assert_has(self, 'cls.demoplayback', frame)
        assert_has(self, 'cl.time - state->animtime', frame)
        assert_has(self, 'seq->fps * state->framerate', frame)
        link = function(source, 'static void CL_LinkPacketEntities( frame_t *frame )')
        self.assertLess(link.index('ent->curstate.frame = state->frame;'), link.index('CL_CoF_AttachCamFrame( ent, state );'))
        self.assertLess(link.index('if( !ent->model ) continue;'), link.index('CL_CoF_AttachCamFrame( ent, state );'))
        message = function(source, 'void CL_CoF_AttachCamMessage( const char *name, int size, const byte *data )')
        assert_has(self, 'Q_strcmp( name, "AttachCam" )', message)
        assert_has(self, 'cl_cof_attachcam = (short)( data[0] | ( data[1] << 8 ));', message)
        parse = function((CLIENT / 'parse/cl_parse.c').read_text(encoding='latin-1'),
                         'void CL_ParseUserMessage( sizebuf_t *msg, int svc_num, connprotocol_t proto )')
        # seen before the inventory messages may stop the parse, and before client.dll gets it
        self.assertLess(parse.index('CL_CoF_AttachCamMessage( clgame.msg[i].name, iSize, pbuf );'),
                        parse.index('if( CL_CoF_Inventory12Message( clgame.msg[i].name, iSize, pbuf ))'))
        clear = function((CLIENT / 'cl_main.c').read_text(encoding='latin-1'), 'void CL_ClearState( void )')
        assert_has(self, 'CL_CoF_AttachCamForget();', clear)


if __name__ == '__main__':
    unittest.main()
