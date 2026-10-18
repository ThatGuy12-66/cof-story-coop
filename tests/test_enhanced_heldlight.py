"""A held lantern, flare or book laser lights the room for the other players too (engine/server/sv_cof_heldlight.h,
sv_cof_weaponmodel.c): client.dll draws those lights only for its own player, from its HUD weapon (view.cpp:1213-1264
in the decompilation; docs/research/2026-10-18-cof-decompilation-survey.md section 3). The engine sends every other
player a model-less light entity (client.dll's renderfx 71: radius renderamt * 8, colour rendercolor / 128)."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of, program_output


class HeldLightRuleTests(unittest.TestCase):
    def test_what_lights_and_how(self):
        header = strip_includes((SERVER / 'sv_cof_heldlight.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_heldlight_harness.c', {'heldlight_under_test.h': '#include <string.h>\n' + header})
        self.assertEqual(program_output(out), [
            'lantern: lantern',
            'lantern in a dual pair: lantern',
            'flare, not lit: none',
            'flare, lit: flare',
            'lit flare in a dual pair: flare',
            'book laser, lit: booklaser',
            # the phone light and the flashlight weapon are already drawn for others (scoreboard flags 1 and 8)
            'a pistol with the phone light: none',
            'the flashlight weapon: none',
            'lantern rolls 0 0 0: renderamt 27 colour 51 51 51',
            'lantern rolls 999 999 999: renderamt 28 colour 51 51 51',
            'flare rolls 0 0 0: renderamt 26 colour 83 13 0',
            'flare rolls 999 999 999: renderamt 29 colour 211 32 19',
            'booklaser rolls 500 500 500: renderamt 28 colour 147 74 10',
            'origin 114 194 321',
        ])


class HeldLightWiringTests(unittest.TestCase):
    def test_on_by_default(self):
        module = (SERVER / 'sv_cof_weaponmodel.c').read_text(encoding='latin-1')
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_coop_heldlights, "1",', module)
        assert_has(self, 'SV_CoF_WeaponModelInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))

    def test_from_the_games_own_flags_to_every_viewer_but_the_holder(self):
        frame = body_of(SERVER / 'sv_cof_weaponmodel.c', 'void SV_CoF_WeaponModelFrame(')
        assert_has(self, 'CoF_WeaponModelHeldLight( cl, i, id, right, left );', frame)
        assert_has(self, 'SV_CoF_RetailFlashFlags( cl )', body_of(SERVER / 'sv_cof_weaponmodel.c', 'static void CoF_WeaponModelHeldLight('))
        light = body_of(SERVER / 'sv_cof_weaponmodel.c', 'int SV_CoF_WeaponModelLight(')
        assert_has(self, 'viewer == &svs.clients[slot]', light)
        assert_has(self, 'out->renderfx = COF_HELDLIGHT_RENDERFX;', light)
        followers = body_of(SERVER / 'sv_frame.c', 'static void SV_CoF_WeaponModelFollowers( sv_client_t *cl, sv_ents_t *ents )')
        assert_has(self, 'SV_CoF_WeaponModelLight( cl, e, &ents->entities[ents->num_entities] )', followers)


if __name__ == '__main__':
    unittest.main()
