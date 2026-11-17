"""Who has the pointer, and the arrow (1.2.8). A tester's friend, mouse only, had the arrow invisible in the menus and
over the in-play panels until he restarted the game: still clickable, moving the mouse did not bring it back. No log
of it. A stick that drifts or an axis that rests off centre sends motion events all the time, and each took the
pointer back for the gamepad (the arrow hidden) right after the mouse had it: now an axis counts once per push from
rest. Every change to or from the gamepad is logged, and an arrow the engine wants up but has down is logged and
shown again."""
import re
import unittest

from tests.native import ROOT, assert_has, run_native
from tests.test_enhanced_sections import body_of

ENGINE = ROOT / 'engine/engine'
PAD = ENGINE / 'client/input/cof_gamepad.c'


class PadPointerTests(unittest.TestCase):
    def test_an_axis_counts_once_per_push_from_rest(self):
        text = PAD.read_text(encoding='latin-1')
        code = '\n'.join(re.findall(r'#define COF_AXIS_(?:PUSH|REST) \d+', text))
        code += '\n' + body_of(PAD, 'static qboolean CL_CoF_AxisPushed( int source, int axis, int value )') + '\n}\n'
        out = run_native(self, 'cof_axis_push_harness.c', {'axis_under_test.c': code})
        self.assertIn('axis push scenarios passed', out)

    def test_both_axis_paths_use_it(self):
        events = body_of(PAD, 'void CL_CoF_InputEvent( const void *sdlEvent )')
        assert_has(self, 'if( CL_CoF_AxisPushed( 0, ev->caxis.axis, ev->caxis.value ))', events)
        self.assertNotIn('> 12000', events)
        assert_has(self, 'if( ev->jhat.value != SDL_HAT_CENTERED )', events)
        engine = body_of(PAD, 'void CL_CoF_NotePadAxis( int axis, short value )')
        assert_has(self, 'if( CL_CoF_AxisPushed( 1, axis, value ))', engine)

    def test_the_gamepad_taking_or_giving_back_the_pointer_is_logged(self):
        body = body_of(PAD, 'static void CL_CoF_NoteInput( int device, const char *why )')
        self.assertLess(body.index('if( CL_CoF_DeviceFromCvar( ) == device )'), body.index('CL_CoF_InputLog( "phase=last-input'))
        assert_has(self, 'if( device == COF_INPUT_PAD || CL_CoF_DeviceFromCvar( ) == COF_INPUT_PAD )', body)
        self.assertLess(body.index('CL_CoF_InputLog('), body.index('Cvar_DirectSet( &cof_last_input'))
        log = body_of(PAD, 'static void CL_CoF_InputLog( const char *fmt, ... )\n{')
        assert_has(self, 'cof_pad.log_next = host.realtime + 2.0;', log)

    def test_a_missing_arrow_is_logged_and_shown(self):
        frame = body_of(PAD, 'qboolean CL_CoF_PadFrame( void )')
        assert_has(self, 'CL_CoF_ArrowWatch( );', frame)
        watch = body_of(PAD, 'static void CL_CoF_ArrowWatch( void )')
        # only where this file's own rules want the arrow up
        assert_has(self, "if(( cls.key_dest != key_menu && !cof_pad.panel_open ) || CL_CoF_PadHidesOSCursor( ) || cof_pad.engaged", watch)
        assert_has(self, 'SDL_WINDOW_INPUT_FOCUS', watch)
        assert_has(self, 'host.realtime - cof_pad.arrow_wrong_since < 1.0', watch)
        assert_has(self, 'cofinput phase=arrow-missing', watch)
        # shown again only when SDL alone has it down; a cursor type of none is only logged
        assert_has(self, 'if( host.mouse_visible && !relative && !shown )\n\t\tSDL_ShowCursor( SDL_ENABLE );', watch)


if __name__ == '__main__':
    unittest.main()
