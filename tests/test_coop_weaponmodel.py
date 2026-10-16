"""Third-person weapons in story co-op: what other players see in each player's hands."""
import unittest

from tests.native import ENGINE_SERVER, run_native


class WeaponModelTests(unittest.TestCase):
    def test_pick_rules(self):
        output = run_native(self, 'cof_weaponmodel_test.c', {
            'weaponmodel_under_test.h': (ENGINE_SERVER / 'sv_cof_weaponmodel.h').read_text(encoding='latin-1'),
        })
        self.assertIn('weapon model scenarios passed', output)

    def test_the_engine_sends_the_pick(self):
        frame = (ENGINE_SERVER / 'sv_frame.c').read_text()
        add = frame.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient')
        self.assertIn('SV_CoF_WeaponModelState( state, e );', frame[add:add + 400])
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_WeaponModelFrame();', story)
        init = (ENGINE_SERVER.parent / 'server/sv_init.c').read_text()
        activate = init[init.index('void SV_ActivateServer'):]
        self.assertLess(activate.index('SV_CoF_WeaponModelActivate();'), activate.index('SV_CreateResourceList'))


if __name__ == '__main__':
    unittest.main()
