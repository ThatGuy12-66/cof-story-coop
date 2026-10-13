"""The story trace log: the unit against offline doubles, and where the engine calls it."""
import unittest

from tests.native import ENGINE_SERVER, run_native, strip_includes


def body(text: str, start: str) -> str:
    text = text[text.index(start):]
    return text[:text.index('\n}\n')]


class TraceTests(unittest.TestCase):
    def test_trace_in_the_engine(self):
        out = run_native(self, 'cof_trace_harness.c', {
            'trace_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_trace.c').read_text()),
        })
        self.assertIn('trace scenarios passed', out)

    def test_command_is_traced_before_the_game_sees_it(self):
        client = (ENGINE_SERVER / 'sv_client.c').read_text()
        self.assertIn('SV_CoF_TraceCommand( cl );', client)
        self.assertLess(client.index('SV_CoF_TraceCommand( cl );'), client.index('SV_CoF_ShareCommandBegin( cl );'))
        self.assertIn('#include "sv_cof_trace.h"', client)

    def test_message_end_closes_the_capture_before_any_return(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        end = body(game, 'static void GAME_EXPORT pfnMessageEnd( void )')
        self.assertIn('SV_CoF_TraceMsgEnd();', end)
        self.assertLess(end.index('SV_CoF_TraceMsgEnd();'), end.index('return;'))
        self.assertLess(end.index('SV_CoF_TraceMsgEnd();'), end.index('Host_Error'))

    def test_message_writes_feed_the_capture(self):
        game = (ENGINE_SERVER / 'sv_game.c').read_text()
        self.assertIn('SV_CoF_TraceMsgBegin( svgame.msg_name, msg_dest, ed );',
                      body(game, 'static void GAME_EXPORT pfnMessageBegin('))
        for fn, call in (('pfnWriteByte', 'SV_CoF_TraceMsgByte( iValue );'), ('pfnWriteChar', 'SV_CoF_TraceMsgByte( iValue );'),
                         ('pfnWriteShort', 'SV_CoF_TraceMsgShort( iValue );'), ('pfnWriteString', 'SV_CoF_TraceMsgString( src );'),
                         ('pfnWriteLong', 'SV_CoF_TraceMsgOther();'), ('pfnWriteAngle', 'SV_CoF_TraceMsgOther();'),
                         ('pfnWriteCoord', 'SV_CoF_TraceMsgOther();'), ('pfnWriteEntity', 'SV_CoF_TraceMsgOther();')):
            self.assertIn(call, body(game, f'static void GAME_EXPORT {fn}('), fn)
        self.assertIn('#include "sv_cof_trace.h"', game)

    def test_story_frame_and_init_call_it(self):
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_TraceFrame();', body(story, 'void SV_CoF_StoryFrame('))
        self.assertIn('SV_CoF_TraceInit();', body(story, 'void SV_CoF_StoryInit('))
        self.assertIn('#include "sv_cof_trace.h"', story)


if __name__ == '__main__':
    unittest.main()
