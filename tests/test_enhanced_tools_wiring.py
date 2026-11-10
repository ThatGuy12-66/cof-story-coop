"""Where the player tools (engine/server/sv_cof_tools.c, rules in sv_cof_tools.h) are wired into the engine: the four
client commands before the guard, init and frame from the story layer, the outlines after the game's AddToFullPack,
the host's cvars, and nothing the batch left out (AGENT_NOTES.md "Player tools (2026-11-09)"). Source checks only; the
rules themselves are tests/test_enhanced_tools.py."""
import re
import unittest

from tests.native import ENGINE_SERVER as SERVER, assert_has
from tests.test_enhanced_sections import body_of

MODULE = SERVER / 'sv_cof_tools.c'


def module():
    return MODULE.read_text(encoding='latin-1')


class ToolsModuleTests(unittest.TestCase):
    def test_the_glue_uses_the_one_rules_header(self):
        text = module()
        assert_has(self, '#include "sv_cof_tools.h"', text)
        assert_has(self, '#include "sv_cof_story.h"', text)
        # the header's constants are not defined a second time here
        for name in ('COF_TOOLS_COOLDOWN_', 'COF_TOOLS_OBJECTIVE_OFFSET', 'COF_TOOLS_BLACKWHITE_OFFSET',
                     'COF_TOOLS_RENDERFX_GLOWSHELL', 'COF_TOOLS_WEAPON_DUAL', 'COF_TOOLS_WEAPON_MP5', 'COF_TOOLS_AMMO_OFFSET'):
            self.assertNotRegex(text, r'#define\s+' + name, name)
        self.assertFalse((SERVER / 'sv_cof_tools_glue.h').exists())

    def test_the_four_commands(self):
        body = body_of(MODULE, 'qboolean SV_CoF_ToolsClientCommand( sv_client_t *cl )')
        for cmd in ('cof_unstuck', 'cof_ping', 'cof_scare', 'cof_dropammo'):
            assert_has(self, f'"{cmd}"', body)
        assert_has(self, 'SV_CoF_StoryCoopEnabled( )', body)

    def test_each_tool_has_its_cooldown(self):
        for function, cooldown in (('static void CoF_ToolsUnstuck(', 'COF_TOOLS_COOLDOWN_UNSTUCK'),
                                   ('static void CoF_ToolsPing(', 'COF_TOOLS_COOLDOWN_PING'),
                                   ('static void CoF_ToolsScare(', 'COF_TOOLS_COOLDOWN_SCARE'),
                                   ('static void CoF_ToolsDropAmmo(', 'COF_TOOLS_COOLDOWN_DROP')):
            body = body_of(MODULE, function)
            assert_has(self, 'CoF_ToolsReady( sv.time,', body)
            assert_has(self, cooldown, body)

    def test_unstuck_refuses_then_searches_only_when_stuck_moves_beside_or_says_not_stuck(self):
        body = body_of(MODULE, 'static void CoF_ToolsUnstuck(')
        refused = body.index('CoF_ToolsUnstuckRefused( down, in_scene, on_ladder, camera )')
        plan = body.index('CoF_ToolsUnstuckPlan( CoF_ToolsHullStuck( ent ), partner != NULL )')
        self.assertLess(refused, plan)
        for needle in ('SV_CoF_PartyDead( ent )', 'SV_CoF_SceneActive()', 'MOVETYPE_FLY', 'SV_CoF_RetailCameraHolds( cl )',
                       'COF_TOOLS_UNSTUCK_SEARCH', 'COF_TOOLS_UNSTUCK_BESIDE', 'SV_CoF_PartyPlace( cl, spot )',
                       'SV_CoF_PartyMoveBeside( cl, partner )', '"You are not stuck."', 'CoF_ToolsRemind( cl, "unstuck" )'):
            assert_has(self, needle, body)
        # the clear-spot search is only the stuck plan's
        search = body[body.index('COF_TOOLS_UNSTUCK_SEARCH'):body.index('COF_TOOLS_UNSTUCK_BESIDE')]
        assert_has(self, 'CoF_ToolsClearSpot( ent, spot )', search)
        self.assertEqual(body.count('CoF_ToolsClearSpot('), 1)

    def test_the_ping_is_text_with_name_and_distance_and_no_glow(self):
        body = body_of(MODULE, 'static void CoF_ToolsPing(')
        assert_has(self, 'CoF_ToolsForward( ent->v.v_angle[0], ent->v.v_angle[1], fwd )', body)
        assert_has(self, 'cl->name', body)
        assert_has(self, ' m ', body)
        assert_has(self, 'SV_CoF_RetailProFont(', body)
        text = module()
        for banned in ('AngleVectors', 'svc_temp_entity', 'TE_GLOWSPRITE', 'TE_SPRITE', 'TE_BEAM'):
            self.assertNotIn(banned, text)

    def test_the_scare_laughs_at_the_nearest_living_teammate(self):
        body = body_of(MODULE, 'static void CoF_ToolsScare(')
        assert_has(self, 'CoF_ToolsNearest( cl )', body)
        assert_has(self, 'laughs at you', body)
        nearest = body_of(MODULE, 'static sv_client_t *CoF_ToolsNearest(')
        assert_has(self, 'CoF_ToolsAlive( other )', nearest)

    def test_one_plain_box_and_never_the_dual_weapon_or_the_mp5(self):
        body = body_of(MODULE, 'static void CoF_ToolsDropAmmo(')
        refused = body.index('CoF_ToolsDropRefused( id )')
        self.assertLess(body.index('SV_CoF_RetailHeld( cl, &id, NULL, NULL )'), refused)
        self.assertLess(refused, body.index('SV_CreateNamedEntity( NULL, SV_AllocString( classname ))'))
        for needle in ('COF_TOOLS_WEAPON_DUAL', 'CoF_ToolsAmmoEntity( id )', 'CoF_ToolsAmmoBox( id )',
                       'CoF_ToolsAmmoSlotOffset( type )', 'reserve < amount'):
            assert_has(self, needle, body)
        for banned in ('COF_PICKUP_BONUS_FLAG', 'COF_PICKUP_COPY_FLAG', 'SV_CoF_PickupsSpawnBonus'):
            self.assertNotIn(banned, module())

    def test_the_objective_reminder_late_join_respawn_and_unstuck(self):
        remind = body_of(MODULE, 'static qboolean CoF_ToolsRemind(')
        assert_has(self, 'memset( pv + COF_TOOLS_OBJECTIVE_OFFSET, COF_TOOLS_OBJECTIVE_RESEND, sizeof( string_t ))', remind)
        text = module()
        assert_has(self, 'p->why = "late-join";', text)
        assert_has(self, 'p->why = "respawn";', text)
        assert_has(self, 'CoF_ToolsRemind( cl, "unstuck" )', text)

    def test_name_is_down(self):
        body = body_of(MODULE, 'static void CoF_ToolsDown(')
        assert_has(self, '"%s is down."', body)
        frame = body_of(MODULE, 'void SV_CoF_ToolsFrame( void )')
        assert_has(self, 'if( dead && !p->dead ) CoF_ToolsDown( cl );', frame)

    def test_noir_clears_blackwhite_with_an_explicit_message(self):
        body = body_of(MODULE, 'static void CoF_ToolsNoir(')
        assert_has(self, '"sc_noir"', body)
        assert_has(self, 'COF_TOOLS_BLACKWHITE_OFFSET', body)
        clear = body[body.index('else if( !want && p->noir == 1 )'):]
        assert_has(self, 'SV_CoF_SendUserMessage( cl->edict, "BlackWhite", &on, COF_TOOLS_BLACKWHITE_SIZE, true )', clear)


class ToolsCvarTests(unittest.TestCase):
    def test_outlines_off_by_default_and_registered(self):
        text = module()
        assert_has(self, 'CVAR_DEFINE_AUTO( cof_outlines, "0",', text)
        assert_has(self, 'Cvar_RegisterVariable( &cof_outlines );', body_of(MODULE, 'void SV_CoF_ToolsInit( void )'))

    def test_the_host_times_are_kept_in_range(self):
        body = body_of(MODULE, 'static void CoF_ToolsKeepRanges( void )')
        assert_has(self, 'Cvar_FindVar( "sv_coopcprtime" )', body)
        assert_has(self, 'Cvar_Get( "sv_coopcprtime", "5",', body)
        assert_has(self, 'CoF_ToolsClampCprTime( var->value )', body)
        assert_has(self, 'Cvar_FindVar( "cof_story_respawn_delay" )', body)
        assert_has(self, 'CoF_ToolsClampRespawnDelay( var->value )', body)
        assert_has(self, 'CoF_ToolsClampOutlines( (int)cof_outlines.value )', body)
        assert_has(self, 'CoF_ToolsKeepRanges();', body_of(MODULE, 'void SV_CoF_ToolsFrame( void )'))
        # the respawn delay stays the party's own cvar (this project's 10, not retail 384); not registered twice
        party = (SERVER / 'sv_cof_party.c').read_text(encoding='latin-1')
        assert_has(self, 'Cvar_Get( "cof_story_respawn_delay", "10",', party)
        self.assertNotIn('Cvar_Get( "cof_story_respawn_delay"', module())


class ToolsCallSiteTests(unittest.TestCase):
    def test_declared_in_the_story_header(self):
        header = (SERVER / 'sv_cof_story.h').read_text(encoding='latin-1')
        for decl in ('void SV_CoF_ToolsInit( void );', 'void SV_CoF_ToolsFrame( void );',
                     'qboolean SV_CoF_ToolsClientCommand( sv_client_t *cl );',
                     'void SV_CoF_ToolsOutline( const edict_t *viewer, entity_state_t *state, int e );'):
            assert_has(self, decl, header)

    def test_init_and_frame_from_the_story_layer(self):
        assert_has(self, 'SV_CoF_ToolsInit();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryInit('))
        assert_has(self, 'SV_CoF_ToolsFrame();', body_of(SERVER / 'sv_cof_story.c', 'void SV_CoF_StoryFrame('))

    def test_the_commands_come_before_the_guard_which_still_refuses_kick_votes(self):
        body = body_of(SERVER / 'sv_client.c', 'static void SV_ExecuteClientCommand( sv_client_t *cl, const char *s )\n{')
        tools = body.index('if( SV_CoF_ToolsClientCommand( cl ))')
        self.assertLess(tools, body.index('if( SV_CoF_GuardClientCommand( cl ))'))
        self.assertLess(tools, body.index('svgame.dllFuncs.pfnClientCommand( cl->edict )'))
        guard = (SERVER / 'sv_cof_guard.h').read_text(encoding='latin-1')
        assert_has(self, 'if( !strcmp( cmd, "vote" ) || !strcmp( cmd, "votekick" )) return "vote";', guard)
        # no vote of its own in the tools
        self.assertNotRegex(module(), r'"vote(kick)?"')

    def test_outlines_after_the_games_full_pack_for_other_players_only(self):
        text = (SERVER / 'sv_frame.c').read_text(encoding='latin-1')
        full_pack = text.index('svgame.dllFuncs.pfnAddToFullPack( state, e, ent, pClient')
        weapon = text.index('if( player ) SV_CoF_WeaponModelState( state, e );')
        outline = text.index('if( player ) SV_CoF_ToolsOutline( pClient, state, e );')
        self.assertLess(full_pack, weapon)
        self.assertLess(weapon, outline)
        body = body_of(MODULE, 'void SV_CoF_ToolsOutline(')
        assert_has(self, 'if( ent == viewer', body)
        assert_has(self, 'COF_TOOLS_RENDERFX_GLOWSHELL', body)
        assert_has(self, 'COF_TOOLS_OUTLINES_ON', body)

    def test_the_build_picks_up_the_new_module(self):
        wscript = (SERVER.parent / 'wscript').read_text(encoding='latin-1')
        assert_has(self, "'server/*.c'", wscript)


class ToolsScopeTests(unittest.TestCase):
    def test_nothing_the_batch_left_out(self):
        text = module().lower()
        for banned in ('unik0z', 'steamedition', 'hunted', 'hardcore', 'run timer', 'votekick', 'groan'):
            self.assertNotIn(banned, text, banned)
        self.assertIsNone(re.search(r'"bind\b', text))


if __name__ == '__main__':
    unittest.main()
