"""A companion's weapons on a revisit (engine/server/sv_story_transition_state.h, sv_save.c): the transition save writes
the old map's snapshot before the companions are dropped, and a companion is never captured, so its weapon_*/item_*
rows got no inventory flag; on the next visit the snapshot created them again with no owner, more per visit
(docs/research/2026-10-18-cof-decompilation-survey.md section 3; evidence/decomp-survey-20261018/B-coop-rules.md 2).
The same items near the exit also arrive with the level change (the game's change list carries them): the engine
adds the companions again with their own kit, so those would be ownerless copies too."""
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import program_output


class CompanionItemTests(unittest.TestCase):
    def test_a_revisit_leaves_out_a_companions_items(self):
        header = strip_includes((SERVER / 'sv_story_transition_state.h').read_text(encoding='latin-1'))
        out = run_native(self, 'cof_transition_skip_harness.c', {'transition_under_test.h': header})
        self.assertEqual(program_output(out), [
            'a player row, revisit: 1',
            "a captured player's item, revisit: 1",
            "a companion's item, revisit: 1",
            "a companion's item, the arriving transition: 1",
            'a map entity, revisit: 0',
            "a captured player's item, arriving: 0",
            'companion item: owner 1, attached 1, neither 0',
        ])

    def test_the_save_marks_them_and_the_restore_reads_the_mark(self):
        source = (SERVER / 'sv_save.c').read_text(encoding='latin-1')
        assert_has(self, '#define FENTTABLE_COF_COMPANION 0x04000000', source)
        assert_has(self, 'if( cof_transition.inventory_owner[i] < 0 ) SetBits( pTable->flags, FENTTABLE_COF_COMPANION );', source)
        assert_has(self, "FBitSet( pTable->flags, FENTTABLE_COF_INVENTORY ), FBitSet( pTable->flags, FENTTABLE_COF_COMPANION )))", source)

    def test_a_companion_is_known_by_its_client_flag(self):
        # the game's player Spawn clears a bot's FL_FAKECLIENT edict flag (sv_cof_companion.c); its client's
        # FCL_FAKECLIENT stays. By the edict flag no spawned companion's item was marked: live 2026-10-18, a c_start
        # revisit had 13 weapon_* against 7, the old companion's three at its spot
        source = (SERVER / 'sv_save.c').read_text(encoding='latin-1')
        assert_has(self, 'return cl && FBitSet( cl->flags, FCL_FAKECLIENT );', source)
        assert_has(self, 'CoF_TransitionCompanionItem( CoF_TransitionFakeClient( ent->v.owner ), '
                         'CoF_TransitionFakeClient( ent->v.aiment ))', source)
        self.assertNotIn('v.owner->v.flags, FL_FAKECLIENT', source)


if __name__ == '__main__':
    unittest.main()
