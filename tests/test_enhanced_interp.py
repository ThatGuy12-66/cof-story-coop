"""Remote players' movement history on the client (engine/client/cl_frame.c), 1.1.9: a partner who comes into view or
is moved holds the oldest sample until the drawn time reaches it, instead of being drawn at the newest but one and then
jumping back about ex_interp (docs: the 2026-10-21 movement research, cause 3c)."""
import unittest

from tests.native import ENGINE_SERVER, assert_has, run_native

FRAME = ENGINE_SERVER.parent / 'client/cl_frame.c'


def function(source, signature):
    start = source.index(signature)
    depth, end = 1, source.index('{', start) + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class InterpolationTests(unittest.TestCase):
    def test_no_snap_back_after_a_reset(self):
        source = FRAME.read_text(encoding='latin-1')
        code = '\n\n'.join(function(source, signature) for signature in (
            'static void CL_UpdatePositions( cl_entity_t *ent )',
            'static void CL_ResetPositions( cl_entity_t *ent )',
            'static qboolean CL_FindInterpolationUpdates( cl_entity_t *ent, double targettime, '
            'position_history_t **ph0, position_history_t **ph1 )',
            'static void CL_PureOrigin( cl_entity_t *ent, double t, vec3_t outorigin, vec3_t outangles )'))
        out = run_native(self, 'cof_interp_harness.c', {'interp_under_test.c': code})
        self.assertIn('interpolation history passed', out)

    def test_players_are_drawn_ex_interp_in_the_past(self):
        body = function(FRAME.read_text(encoding='latin-1'), 'void CL_ComputePlayerOrigin( cl_entity_t *ent )')
        assert_has(self, 'double targettime = cl.time - cl_interp.value;', body)
        assert_has(self, 'CL_PureOrigin( ent, targettime, origin, angles );', body)


if __name__ == '__main__':
    unittest.main()
