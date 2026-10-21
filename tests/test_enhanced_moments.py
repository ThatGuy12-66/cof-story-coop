"""Story moments: fall floors, phone calls, ladders, wheelchair, drops and yes/no prompts."""
import unittest

from tests.native import ENGINE_SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of


class StoryMomentsTests(unittest.TestCase):
    def test_moments_glue(self):
        out = run_native(self, 'cof_moments_harness.c', {
            'moments_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_moments.c').read_text()),
            'cof_strip.h': (ENGINE_SERVER.parent / 'common/cof_strip.h').read_text(),
        })
        self.assertIn('moments scenarios passed', out)


class StoryMomentsWiringTests(unittest.TestCase):
    def test_fixed_size_messages_reach_the_mirror(self):
        """1.1.9 (S3): CoFLogo is registered with size 1, so the variable-size branch never passed it on and the
        intro logos' mirror never ran; it goes with GreyFade, before the message is sent."""
        body = body_of(ENGINE_SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        branch = "else if( svgame.msg_index > 0 && name && ( !Q_strcmp( name, \"GreyFade\" ) || !Q_strcmp( name, \"CoFLogo\" )))"
        assert_has(self, branch, body)
        mirror = body.index('SV_CoF_MomentsMessage( svgame.msg_ent, name, sv.multicast.pData + cof_msg_payload, svgame.msg_realsize );',
                            body.index(branch))
        self.assertLess(mirror, body.index('SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );'))

    def test_scene_effects_of_another_section_are_held_before_they_go(self):
        """1.1.9 (S5): a merged map's scene fades, black-and-white, chapter titles and subtitle lines are checked against
        the receiver's section, with the player whose command or move runs (the think's entity is the story layer's)."""
        body = body_of(ENGINE_SERVER / 'sv_game.c', 'static void GAME_EXPORT pfnMessageEnd( void )')
        held = body.index('SV_CoF_MomentsHeldBack( svgame.msg_ent, svgame.msg_dest, name, sv.multicast.pData + cof_msg_payload,')
        assert_has(self, 'svgame.msg_realsize, cof_command_sender ? cof_command_sender : cof_usercmd_sender ))', body)
        self.assertLess(body.index('SV_CoF_RainMessage('), held)
        self.assertLess(held, body.index('SV_Multicast( svgame.msg_dest, org, svgame.msg_ent, true, false );'))
        rescue = ENGINE_SERVER / 'sv_cof_rescue.c'
        assert_has(self, 'cof_rescue_thinker = thinker;', body_of(rescue, 'void SV_CoF_RescueThinkBegin( edict_t *thinker )'))
        assert_has(self, 'cof_rescue_thinker = NULL;', body_of(rescue, 'void SV_CoF_RescueThinkEnd( void )'))


if __name__ == '__main__':
    unittest.main()
