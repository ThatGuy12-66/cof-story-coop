"""Recovery archives, inventory schemas and lifecycle behavior."""
import unittest

from tests.native import ENGINE_SERVER, assert_has, run_native, strip_includes
from tests.test_enhanced_sections import body_of


class RecoveryTests(unittest.TestCase):
    def test_inventory_snapshot_io(self):
        out = run_native(self, 'cof_snapshot_io_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'snapshot_io_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_snapshot_io.inc').read_text()),
        })
        self.assertIn('inventory IO scenarios passed', out)

    def test_archive_validation(self):
        out = run_native(self, 'cof_archive_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'sv_cof_archive.h': (ENGINE_SERVER / 'sv_cof_archive.h').read_text(),
        })
        self.assertIn('archive scenarios passed', out)

    def test_snapshot_validation(self):
        out = run_native(self, 'cof_snapshot_test.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
        })
        self.assertIn('snapshot validation scenarios passed', out)

    def test_death_rejoin_lifecycle(self):
        out = run_native(self, 'cof_recovery_harness.c', {
            'sv_cof_snapshot.h': (ENGINE_SERVER / 'sv_cof_snapshot.h').read_text(),
            'recovery_under_test.c': strip_includes((ENGINE_SERVER / 'sv_cof_recovery.c').read_text()),
        })
        self.assertIn('recovery lifecycle scenarios passed', out)


class RecoveryWiringTests(unittest.TestCase):
    def test_the_chainsaw_grab_is_seen_around_each_monster_think(self):
        """1.1.8 (E2-4): the capture before the boss's think, the hand-over right after it."""
        think = body_of(ENGINE_SERVER / 'sv_phys.c', 'static qboolean SV_TryThink(')
        self.assertLess(think.index('SV_CoF_RecoveryThinkBegin( ent );'), think.index('svgame.dllFuncs.pfnThink( ent );'))
        self.assertLess(think.index('svgame.dllFuncs.pfnThink( ent );'), think.index('SV_CoF_RecoveryThinkEnd();'))
        assert_has(self, '"monster_bosschainsaw"', body_of(ENGINE_SERVER / 'sv_cof_recovery.c', 'void SV_CoF_RecoveryThinkBegin('))

    def test_the_restored_weapon_is_deployed_with_its_players_globals(self):
        """1.3.1: a dual weapon's Deploy runs on the game's one dual-weapon object; the restore runs outside any command,
        where the DLL holds the host's copy, so a guest's deploy wrote the host's."""
        ready = body_of(ENGINE_SERVER / 'sv_cof_retail_inventory.c', 'void SV_CoF_RetailInventoryReady(')
        self.assertLess(ready.index('SV_CoF_GlobalsEnter( cl );'), ready.index('SV_CoF_StoryDeployWeapon( active );'))
        self.assertLess(ready.index('SV_CoF_StoryDeployWeapon( active );'), ready.index('SV_CoF_GlobalsLeave( cl );'))

    def test_the_leavers_weapons_are_let_go_before_they_are_freed(self):
        """1.1.8 (E2-3): the player object's pointers are cleared before the frees, whatever the drop path."""
        done = body_of(ENGINE_SERVER / 'sv_cof_recovery.c', 'void SV_CoF_RecoveryDropDone(')
        self.assertLess(done.index('memset( player + COF_RECOVERY_ITEMS, 0,'), done.index('SV_FreeEdict('))
        drop = body_of(ENGINE_SERVER / 'sv_client.c', 'void SV_DropClient( sv_client_t *cl, qboolean crash )')
        self.assertLess(drop.index('svgame.dllFuncs.pfnClientDisconnect( cl->edict );'), drop.index('SV_CoF_RecoveryDropDone( cl );'))


if __name__ == '__main__':
    unittest.main()
