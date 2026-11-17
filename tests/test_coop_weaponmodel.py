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
        self.assertIn(f"gasmask {mapping['gasmask']['model']} {mapping['gasmask']['flash_flag']}", output)
        # 1.1.9 (ladders): a climber is drawn in pistol_idle (both arms forward; the player models have no climb)
        self.assertIn(f"climbsequence {weapons.SEQUENCES['pistol_idle']}", output)

    def test_the_engine_sends_the_pick(self):
        frame = (ENGINE_SERVER / 'sv_frame.c').read_text()
        add = frame.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient')
        self.assertIn('SV_CoF_WeaponModelState( state, e );', frame[add:add + 400])
        write = frame[frame.index('static void SV_WriteEntitiesToClient'):]
        # the other hand's follow entities go in before the sort the delta compression needs
        self.assertLess(write.index('SV_CoF_WeaponModelFollowers('), write.index('qsort( frame_ents.entities'))
        # 1.1.8 (E2-5): the pose goes out only in the plain standing one; the model in any
        body = (ENGINE_SERVER / 'sv_cof_weaponmodel.c').read_text()
        pick = body[body.index('void SV_CoF_WeaponModelFrame('):body.index('void SV_CoF_WeaponModelState(')]
        self.assertLess(pick.index('CoF_WeaponModelStanding('), pick.index('.sequence = pick.sequence;'))
        self.assertIn('.model = CoF_WeaponModelIndex( pick.model );', pick)
        # 1.1.9 (movement 1): a sprinting player keeps the game's sprint sequence (flags bit 18, 2D speed)
        self.assertIn('CoF_WeaponModelSprinting( cl->edict->v.flags, cl->edict->v.velocity[0], cl->edict->v.velocity[1] )', pick)
        story = (ENGINE_SERVER / 'sv_cof_story.c').read_text()
        self.assertIn('SV_CoF_WeaponModelFrame();', story)
        init = (ENGINE_SERVER / 'sv_init.c').read_text()
        activate = init[init.index('void SV_ActivateServer'):]
        self.assertLess(activate.index('SV_CoF_WeaponModelActivate();'), activate.index('SV_CreateResourceList'))

    def test_the_mask_ladders_and_the_boat_are_wired_into_the_sends(self):
        """1.1.9: the gas mask follower, the ladder body and hands, the rower (P5): where sv_frame.c applies them."""
        from tests.test_enhanced_sections import body_of
        add = body_of(ENGINE_SERVER / 'sv_frame.c', 'static void SV_AddEntitiesToPacket(')
        pack = add.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient')
        # the rower's body is skipped for the others before the game fills a state (its own client still gets it)
        self.assertLess(add.index('if( SV_CoF_WeaponModelHidden( pClient, e ))\n\t\t\t\tcontinue;'), pack)
        # a climber is not culled by where the body stands; the hands model is held back from the others
        climbing = add.index('if( player && SV_CoF_WeaponModelClimbing( pClient, e ))\n\t\t\tpset = NULL;')
        self.assertLess(add.index('pset = clientpvs;'), climbing)
        self.assertLess(climbing, pack)
        self.assertLess(add.index('else if( !player && SV_CoF_CameraModelHiddenFor( pClient, ent ))\n\t\t\tcontinue;'), pack)
        followers = body_of(ENGINE_SERVER / 'sv_frame.c', 'static void SV_CoF_WeaponModelFollowers(')
        self.assertIn('SV_CoF_WeaponModelClimb( &ents->entities[i], cl->edict );', followers)
        self.assertIn('SV_CoF_WeaponModelMask( cl, e, &ents->entities[ents->num_entities] )', followers)
        self.assertIn('if( !CHECKVISBIT( ents->sended, e )', followers)  # only for players this client is sent
        frame = (ENGINE_SERVER / 'sv_frame.c').read_text(encoding='latin-1')
        for prototype in ('qboolean SV_CoF_WeaponModelHidden( const edict_t *viewer, int e );',
                          'qboolean SV_CoF_WeaponModelClimbing( const edict_t *viewer, int e );',
                          'void SV_CoF_WeaponModelClimb( entity_state_t *state, const edict_t *viewer );',
                          'int SV_CoF_WeaponModelMask( const sv_client_t *viewer, int e, entity_state_t *out );',
                          'qboolean SV_CoF_CameraModelHiddenFor( const edict_t *viewer, const edict_t *ent );'):
            self.assertIn(prototype, frame)
        body = (ENGINE_SERVER / 'sv_cof_weaponmodel.c').read_text(encoding='latin-1')
        for definition in ('qboolean SV_CoF_WeaponModelHidden( const edict_t *viewer, int e )\n{',
                           'qboolean SV_CoF_WeaponModelClimbing( const edict_t *viewer, int e )\n{',
                           'void SV_CoF_WeaponModelClimb( entity_state_t *state, const edict_t *viewer )\n{',
                           'int SV_CoF_WeaponModelMask( const sv_client_t *viewer, int e, entity_state_t *out )\n{'):
            self.assertIn(definition, body)
        self.assertIn('qboolean SV_CoF_CameraModelHiddenFor( const edict_t *viewer, const edict_t *ent )\n{',
                      (ENGINE_SERVER / 'sv_cof_camera_models.c').read_text(encoding='latin-1'))
        self.assertIn('CVAR_DEFINE_AUTO( cof_coop_gasmask, "1",', body)
        self.assertIn('Cvar_RegisterVariable( &cof_coop_gasmask );', body)

    def test_the_mask_follows_only_for_the_others(self):
        from tests.test_enhanced_sections import body_of
        server = ENGINE_SERVER / 'sv_cof_weaponmodel.c'
        mask = body_of(server, 'int SV_CoF_WeaponModelMask(')
        self.assertIn('viewer == &svs.clients[slot]', mask)  # the wearer sees the game's own
        self.assertIn('CoF_WeaponModelViewerOnCamera( viewer )', mask)
        self.assertIn('CoF_WeaponModelNumber( COF_WEAPONMODEL_GASMASK, slot,', mask)
        # 1.1.9 side finding: the other hand is held back on any camera too, not only the engine's view entity
        follower = body_of(server, 'int SV_CoF_WeaponModelFollower(')
        self.assertIn('CoF_WeaponModelViewerOnCamera( viewer )', follower)
        self.assertNotIn('pViewEntity', follower)
        self.assertIn('CoF_WeaponModelNumber( COF_WEAPONMODEL_OFFHAND, slot,', follower)
        self.assertIn('CoF_WeaponModelNumber( COF_WEAPONMODEL_LIGHT, slot,', body_of(server, 'int SV_CoF_WeaponModelLight('))
        camera = body_of(server, 'static qboolean CoF_WeaponModelOnCamera(')
        for needle in ('cl->pViewEntity && cl->pViewEntity != ent', 'COF_WEAPONMODEL_VIEW_ENT', 'COF_WEAPONMODEL_ATTACHED_CAMERA',
                       'camera->serialnumber == serial', 'svgame.edicts + svgame.numEntities'):
            self.assertIn(needle, camera)
        frame = body_of(server, 'void SV_CoF_WeaponModelFrame(')
        # the mask before the held item (a player holding nothing wears it too), the boat before the spawned test
        self.assertLess(frame.index('CoF_WeaponModelMasked( SV_CoF_RetailFlashFlags( cl )'), frame.index('SV_CoF_RetailHeld('))
        self.assertLess(frame.index('CoF_WeaponModelBoat( cl, i );'), frame.index('cl->state != cs_spawned'))
        self.assertIn('CoF_WeaponModelIndex( COF_WEAPONMODEL_MASK )', frame)  # only a precached mask
        # a climber shows no other hand and no held light
        self.assertIn('if( !cof_weaponmodel_players[i].climb ) CoF_WeaponModelHeldLight(', frame)
        self.assertIn('if( !cof_weaponmodel_players[i].climb ) cof_weaponmodel_players[i].offhand =', frame)

    def test_the_ladder_body_and_the_rower(self):
        from tests.test_enhanced_sections import body_of
        server = ENGINE_SERVER / 'sv_cof_weaponmodel.c'
        climb = body_of(server, 'static void CoF_WeaponModelClimb(')
        for needle in ('SV_CoF_CameraModelLadder( slot + 1 )', 'Mod_StudioGetAttachment( hands, 0, eye, NULL );',
                       'Mod_StudioGetAttachment( hands, 1, ahead, NULL );', 'ent->v.view_ofs[2]',
                       'atan2( heading[1], heading[0] )', 'ent->v.deadflag != DEAD_NO'):
            self.assertIn(needle, climb)
        state = body_of(server, 'void SV_CoF_WeaponModelClimb(')
        for needle in ('state->weaponmodel = 0;', 'state->sequence = COF_WEAPONMODEL_CLIMB_SEQUENCE;', 'state->gaitsequence = 0;'):
            self.assertIn(needle, state)
        # never the climber's own state (their view is the hands model's)
        self.assertIn('viewer != svs.clients[slot].edict', body_of(server, 'qboolean SV_CoF_WeaponModelClimbing('))
        self.assertIn('viewer != svs.clients[slot].edict', body_of(server, 'qboolean SV_CoF_WeaponModelHidden('))
        boat = body_of(server, 'static void CoF_WeaponModelBoat(')
        for needle in ('SV_CoF_RetailInBoat( cl )', 'SetBits( ent->v.flags, FL_NOTARGET );', 'ent->v.takedamage = DAMAGE_NO;',
                       'SV_CoF_TransitionActive( )', 'cl->userid == cof_weaponmodel_boat[slot].userid',
                       'phase=boat-rower-hidden', 'phase=boat-rower-restored'):
            self.assertIn(needle, boat)
        restore = body_of(server, 'static void CoF_WeaponModelBoatRestore(')
        self.assertIn('ClearBits( ent->v.flags, FL_NOTARGET );', restore)
        self.assertIn('if( ent->v.takedamage == DAMAGE_NO ) ent->v.takedamage = DAMAGE_AIM;', restore)
        # the protection outlives a map change: the activation clears only the per-frame picks
        self.assertNotIn('cof_weaponmodel_boat', body_of(server, 'void SV_CoF_WeaponModelActivate('))
        models = ENGINE_SERVER / 'sv_cof_camera_models.c'
        frame = body_of(models, 'void SV_CoF_CameraModelsFrame(')
        self.assertLess(frame.index('CoF_CameraModelsLadders( );'), frame.index('cof_coop_camera_models.value == 0.0f'))
        ladders = body_of(models, 'static void CoF_CameraModelsLadders(')
        for needle in ('"cof_ladder_manager"', '"camera_model"', 'FL_FROZEN', 'COF_CAMERA_MODEL_SCENE', 'svs.spawncount'):
            self.assertIn(needle, ladders)

    def test_a_missing_model_never_reaches_the_retail_client(self):
        # the retail client merges a player's weaponmodel without a NULL check: a guest without
        # a generated file must see empty hands, not crash (pmodels-report.md section 5)
        client = (ROOT / 'engine/engine/client/cl_frame.c').read_text()
        body = client[client.index('static void CL_ProcessPlayerState'):]
        body = body[:body.index('pfnProcessPlayerState')]
        self.assertIn('!CL_ModelHandle( state->weaponmodel )', body)


if __name__ == '__main__':
    unittest.main()
