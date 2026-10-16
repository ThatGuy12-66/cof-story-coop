"""Third-person weapons in story co-op: what other players see in each player's hands."""
import unittest

from tests.native import ENGINE_SERVER, ROOT, run_native
from tools import prepare_coop_weapons as weapons


class WeaponModelTests(unittest.TestCase):
    def test_pick_rules_and_the_generators_mapping(self):
        output = run_native(self, 'cof_weaponmodel_test.c', {
            'weaponmodel_under_test.h': (ENGINE_SERVER / 'sv_cof_weaponmodel.h').read_text(encoding='latin-1'),
        })
        self.assertIn('weapon model scenarios passed', output)
        # the engine's table is the generator's mapping: same models, same poses, same hands
        mapping = weapons.engine_mapping()
        single = {int(i): (row['model'], -1 if row['sequence'] is None else row['sequence'])
                  for i, row in mapping['single'].items()}
        dual = {int(i): (path, mapping['dual']['left'][i]) for i, path in mapping['dual']['right'].items()}
        rows = [line.split() for line in output.splitlines()]
        self.assertEqual({int(r[1]): (r[2], int(r[3])) for r in rows if r[0] == 'single'}, single)
        self.assertEqual({int(r[1]): (r[2], r[3]) for r in rows if r[0] == 'dual'}, dual)
        self.assertIn(f"dualsequence {mapping['dual']['sequence']}", output)

    def test_the_engine_sends_the_pick(self):
        frame = (ENGINE_SERVER / 'sv_frame.c').read_text()
        add = frame.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient')
        self.assertIn('SV_CoF_WeaponModelState( state, e );', frame[add:add + 400])
        write = frame[frame.index('static void SV_WriteEntitiesToClient'):]
        # the other hand's follow entities go in before the sort the delta compression needs
        self.assertLess(write.index('SV_CoF_WeaponModelFollowers('), write.index('qsort( frame_ents.entities'))
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_WeaponModelFrame();', story)
        init = (ENGINE_SERVER / 'sv_init.c').read_text()
        activate = init[init.index('void SV_ActivateServer'):]
        self.assertLess(activate.index('SV_CoF_WeaponModelActivate();'), activate.index('SV_CreateResourceList'))

    def test_a_missing_model_never_reaches_the_retail_client(self):
        # the retail client merges a player's weaponmodel without a NULL check: a guest without
        # a generated file must see empty hands, not crash (pmodels-report.md section 5)
        client = (ROOT / 'upstream/cof-enhanced/xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a/engine/client/cl_frame.c').read_text()
        body = client[client.index('static void CL_ProcessPlayerState'):]
        body = body[:body.index('pfnProcessPlayerState')]
        self.assertIn('!CL_ModelHandle( state->weaponmodel )', body)


if __name__ == '__main__':
    unittest.main()
