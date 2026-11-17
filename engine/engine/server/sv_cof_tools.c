/*
sv_cof_tools.c - Cry of Fear story co-op: the player tools (rules in sv_cof_tools.h)
GPL-3.0-or-later.

AGENT_NOTES.md "Player tools (2026-11-09)" (the launcher repo). This project's own code for this engine.

Client commands (SV_ExecuteClientCommand, before the guard; true when handled), in story co-op only:
- cof_unstuck: refused while down, in a scene, on a ladder or while a camera holds the player. A hull stuck in the
  world is set down on a clear spot nearby; a free player is moved beside the nearest partner, or, alone, told they
  are not stuck. A success reminds them of the objective.
- cof_ping: a text line to everyone: the pinger's name and how far the spot looked at is (no glow temp-entity).
- cof_scare: a laugh at the nearest living teammate (text lines).
- cof_dropammo: one box of spare ammo for the gun in hand, in front of the player (refused for the dual weapon, the
  MP5 and anything without a box).
Every story frame: sv_coopcprtime and cof_story_respawn_delay kept in their ranges, "<name> is down." to the others,
the objective reminder on a late join and a respawn, noir (setinfo sc_noir 1: BlackWhite; off: an explicit message).
sv_frame.c: another player far from the viewer is sent with the glow shell while cof_outlines is 1 (it may not draw
in Cry of Fear's renderer: not checked live).

Logs: phase=tools-unstuck, tools-unstuck-refused, tools-ping, tools-scare, tools-dropammo, tools-dropammo-refused,
tools-down, tools-objective, tools-noir, tools-range.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_party.h"
#include "sv_cof_retail.h"
#include "sv_cof_scene.h"
#include "sv_cof_tools.h"

#define COF_TOOLS_PLAYER_NEED      ( COF_TOOLS_AMMO_OFFSET + 4 * COF_TOOLS_AMMO_SLOTS )
#define COF_TOOLS_ACTIVE_ITEM      0x252C // CBasePlayerItem *m_pActiveItem (sv_cof_retail.c)
#define COF_TOOLS_ITEM_ID          0x168  // CBasePlayerWeapon int m_iId (sv_cof_retail.c)
#define COF_TOOLS_ITEM_AMMO_TYPE   0x1DC  // int m_iPrimaryAmmoType: index into m_rgAmmo
#define COF_TOOLS_ITEM_NEED        0x1F4
#define COF_TOOLS_SF_NORESPAWN     BIT( 30 ) // as GiveNamedItem does: the dropped box does not come back
#define COF_TOOLS_DROP_AHEAD       40.0f  // units in front of the player
#define COF_TOOLS_PING_RANGE       4096.0f
#define COF_TOOLS_OUTLINE_FAR      512.0f // units: a teammate nearer than this is not outlined
#define COF_TOOLS_METRES( units )  ((int)(( units ) * 0.0254f + 0.5f ))

static CVAR_DEFINE_AUTO( cof_outlines, "0", 0, "Cry of Fear story co-op: teammates far away are outlined (glow shell; may not draw in Cry of Fear's renderer); 0: off" );

typedef struct
{
	int userid;
	qboolean dead;
	qboolean remind;     // the objective reminder is due once they are alive and signed on
	const char *why;     // late-join, respawn
	int noir;            // sc_noir as last applied, -1 not yet
	double next_unstuck, next_ping, next_scare, next_drop;
} cof_tools_player_t;

static cof_tools_player_t cof_tools_players[MAX_CLIENTS];
static int cof_tools_known[MAX_CLIENTS]; // the userids of the map before: carried over, not a late join
static int cof_tools_generation = -1;

static void CoF_ToolsCheckGeneration( void )
{
	int i;
	if( cof_tools_generation == svs.spawncount ) return; // sv.time starts again with each map
	cof_tools_generation = svs.spawncount;
	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		cof_tools_known[i] = cof_tools_players[i].userid;
		memset( &cof_tools_players[i], 0, sizeof( cof_tools_players[i] ));
		cof_tools_players[i].noir = -1;
	}
}

static qboolean CoF_ToolsKnown( int userid )
{
	int i;
	for( i = 0; i < MAX_CLIENTS; i++ )
		if( userid && cof_tools_known[i] == userid ) return true;
	return false;
}

// The player's game object, verified and big enough for every field the tools touch, or NULL.
static byte *CoF_ToolsPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_TOOLS_PLAYER_NEED );
}

static qboolean CoF_ToolsAlive( sv_client_t *cl )
{
	return CoF_ToolsPlayer( cl ) != NULL && !SV_CoF_PartyDead( cl->edict );
}

// This slot's record, for the person in it now (a new person starts it again).
static cof_tools_player_t *CoF_ToolsSlot( sv_client_t *cl )
{
	int slot = (int)( cl - svs.clients );
	cof_tools_player_t *p;
	if( slot < 0 || slot >= MAX_CLIENTS || slot >= svs.maxclients ) return NULL;
	p = &cof_tools_players[slot];
	if( p->userid != cl->userid )
	{
		memset( p, 0, sizeof( *p ));
		p->userid = cl->userid;
		p->noir = -1;
		p->dead = SV_CoF_PartyDead( cl->edict );
		if( !FBitSet( cl->flags, FCL_FAKECLIENT ) && !CoF_ToolsKnown( cl->userid ))
		{
			p->remind = true;
			p->why = "late-join";
		}
	}
	return p;
}

// The game sends the objective again when this field is 0xFF (as a session's restore does, sv_cof_retail.c).
static qboolean CoF_ToolsRemind( sv_client_t *cl, const char *why )
{
	byte *pv = CoF_ToolsPlayer( cl );
	if( !pv || FBitSet( cl->flags, FCL_FAKECLIENT )) return false;
	memset( pv + COF_TOOLS_OBJECTIVE_OFFSET, COF_TOOLS_OBJECTIVE_RESEND, sizeof( string_t ));
	Con_Printf( "cofstory phase=tools-objective userid=%d why=%s\n", cl->userid, why );
	return true;
}

// The nearest other living player, or NULL.
static sv_client_t *CoF_ToolsNearest( sv_client_t *cl )
{
	sv_client_t *best = NULL;
	float best_d = 0.0f, d;
	vec3_t delta;
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *other = &svs.clients[i];
		if( other == cl || !CoF_ToolsAlive( other )) continue;
		VectorSubtract( other->edict->v.origin, cl->edict->v.origin, delta );
		d = DotProduct( delta, delta );
		if( !best || d < best_d ) { best = other; best_d = d; }
	}
	return best;
}

static qboolean CoF_ToolsHullStuck( edict_t *ent )
{
	trace_t tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NOMONSTERS, ent, false );
	return tr.startsolid || tr.allsolid;
}

// A spot near the player where the hull is free and a floor is in reach: nearest rings first, level first.
static qboolean CoF_ToolsClearSpot( edict_t *ent, vec3_t out )
{
	static const float dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
		{ 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f } };
	static const float radii[] = { 0.0f, 16.0f, 32.0f, 48.0f, 64.0f, 96.0f };
	static const float lifts[] = { 0.0f, 18.0f, 36.0f, -18.0f };
	size_t r, l, d;
	vec3_t spot, down;
	trace_t tr;
	for( r = 0; r < ARRAYSIZE( radii ); r++ )
		for( l = 0; l < ARRAYSIZE( lifts ); l++ )
			for( d = 0; d < ARRAYSIZE( dirs ); d++ )
			{
				if( radii[r] == 0.0f && d ) break; // the spot itself once
				VectorCopy( ent->v.origin, spot );
				spot[0] += dirs[d][0] * radii[r];
				spot[1] += dirs[d][1] * radii[r];
				spot[2] += lifts[l];
				tr = SV_Move( spot, ent->v.mins, ent->v.maxs, spot, MOVE_NOMONSTERS, ent, false );
				if( tr.startsolid || tr.allsolid ) continue;
				VectorCopy( spot, down );
				down[2] -= 128.0f;
				tr = SV_Move( spot, ent->v.mins, ent->v.maxs, down, MOVE_NOMONSTERS, ent, false );
				if( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) continue;
				VectorCopy( tr.endpos, out );
				return true;
			}
	return false;
}

static void CoF_ToolsUnstuck( sv_client_t *cl, cof_tools_player_t *p )
{
	edict_t *ent = cl->edict;
	sv_client_t *partner;
	qboolean down, in_scene, on_ladder, camera, done = false;
	const char *how = "";
	char text[96];
	vec3_t spot;
	int plan;
	if( !CoF_ToolsReady( sv.time, p->next_unstuck )) return;
	p->next_unstuck = sv.time + COF_TOOLS_COOLDOWN_UNSTUCK; // a refusal starts it too: one line per cooldown
	down = !CoF_ToolsPlayer( cl ) || SV_CoF_PartyDead( ent );
	in_scene = !down && ( SV_CoF_SceneActive() || FBitSet( ent->v.flags, FL_FROZEN ) || SV_CoF_PartyInDoor( cl ));
	on_ladder = !down && ent->v.movetype == MOVETYPE_FLY;
	camera = !down && ( SV_CoF_RetailCameraHolds( cl ) || SV_CoF_RetailAttachedCamera( cl ));
	if( CoF_ToolsUnstuckRefused( down, in_scene, on_ladder, camera ))
	{
		const char *reason = down ? "down" : in_scene ? "scene" : on_ladder ? "ladder" : "camera";
		Con_Printf( "cofstory phase=tools-unstuck-refused userid=%d reason=%s\n", cl->userid, reason );
		if( !down ) SV_CoF_RetailProFont( cl, 5, on_ladder ? "Not on a ladder." : "Not right now.", false );
		return;
	}
	partner = CoF_ToolsNearest( cl );
	plan = CoF_ToolsUnstuckPlan( CoF_ToolsHullStuck( ent ), partner != NULL );
	if( plan == COF_TOOLS_UNSTUCK_SEARCH )
	{
		how = "search";
		done = CoF_ToolsClearSpot( ent, spot ) && SV_CoF_PartyPlace( cl, spot );
		if( !done ) SV_CoF_RetailProFont( cl, 5, "No clear spot nearby.", false );
	}
	else if( plan == COF_TOOLS_UNSTUCK_BESIDE )
	{
		how = "beside";
		done = SV_CoF_PartyMoveBeside( cl, partner );
		if( !done )
		{
			Q_snprintf( text, sizeof( text ), "No room next to %s.", partner->name );
			SV_CoF_RetailProFont( cl, 5, text, false );
		}
	}
	else
	{
		Con_Printf( "cofstory phase=tools-unstuck userid=%d plan=free\n", cl->userid );
		SV_CoF_RetailProFont( cl, 5, "You are not stuck.", false );
		return;
	}
	Con_Printf( "cofstory phase=tools-unstuck userid=%d plan=%s done=%d partner=%d\n", cl->userid, how, done, partner ? partner->userid : 0 );
	if( done ) CoF_ToolsRemind( cl, "unstuck" );
}

static void CoF_ToolsPing( sv_client_t *cl, cof_tools_player_t *p )
{
	edict_t *ent = cl->edict;
	vec3_t eye, end, fwd, delta, zero = { 0, 0, 0 };
	char text[96];
	trace_t tr;
	int i;
	if( !CoF_ToolsReady( sv.time, p->next_ping )) return;
	p->next_ping = sv.time + COF_TOOLS_COOLDOWN_PING;
	if( !CoF_ToolsAlive( cl )) return;
	CoF_ToolsForward( ent->v.v_angle[0], ent->v.v_angle[1], fwd );
	VectorAdd( ent->v.origin, ent->v.view_ofs, eye );
	VectorMA( eye, COF_TOOLS_PING_RANGE, fwd, end );
	tr = SV_Move( eye, zero, zero, end, MOVE_NORMAL, ent, false );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *other = &svs.clients[i];
		if( other->state != cs_spawned || !SV_IsValidEdict( other->edict ) || FBitSet( other->flags, FCL_FAKECLIENT )) continue;
		VectorSubtract( tr.endpos, other->edict->v.origin, delta );
		if( other == cl ) Q_snprintf( text, sizeof( text ), "You pinged a spot %d m away.", COF_TOOLS_METRES( VectorLength( delta )));
		else Q_snprintf( text, sizeof( text ), "%s pinged a spot %d m from you.", cl->name, COF_TOOLS_METRES( VectorLength( delta )));
		SV_CoF_RetailProFont( other, 1, text, false );
	}
	Con_Printf( "cofstory phase=tools-ping userid=%d at=%.0f,%.0f,%.0f\n", cl->userid, tr.endpos[0], tr.endpos[1], tr.endpos[2] );
}

static void CoF_ToolsScare( sv_client_t *cl, cof_tools_player_t *p )
{
	sv_client_t *target;
	char text[96];
	if( !CoF_ToolsReady( sv.time, p->next_scare )) return;
	p->next_scare = sv.time + COF_TOOLS_COOLDOWN_SCARE;
	if( !CoF_ToolsAlive( cl )) return;
	if( !( target = CoF_ToolsNearest( cl )))
	{
		SV_CoF_RetailProFont( cl, 5, "Nobody to scare.", false );
		return;
	}
	Q_snprintf( text, sizeof( text ), "%s laughs at you.", cl->name );
	SV_CoF_RetailProFont( target, 2, text, false );
	Q_snprintf( text, sizeof( text ), "You laugh at %s.", target->name );
	SV_CoF_RetailProFont( cl, 5, text, false );
	Con_Printf( "cofstory phase=tools-scare userid=%d target=%d\n", cl->userid, target->userid );
}

static void CoF_ToolsDropRefusal( sv_client_t *cl, const char *reason, const char *text )
{
	Con_Printf( "cofstory phase=tools-dropammo-refused userid=%d reason=%s\n", cl->userid, reason );
	if( text ) SV_CoF_RetailProFont( cl, 5, text, false );
}

static void CoF_ToolsDropAmmo( sv_client_t *cl, cof_tools_player_t *p )
{
	edict_t *ent = cl->edict, *box = NULL;
	byte *pv, *active = NULL, *weapon = NULL;
	const char *classname;
	vec3_t fwd, end, spot, zero = { 0, 0, 0 };
	int id = 0, type = -1, offset, reserve, amount, i;
	float ahead;
	trace_t tr;
	if( !CoF_ToolsReady( sv.time, p->next_drop )) return;
	p->next_drop = sv.time + COF_TOOLS_COOLDOWN_DROP;
	if( !( pv = CoF_ToolsPlayer( cl )) || SV_CoF_PartyDead( ent )) return;
	if( !SV_CoF_RetailHeld( cl, &id, NULL, NULL ))
	{
		CoF_ToolsDropRefusal( cl, "nothing-held", "Nothing in hand to drop ammo for." );
		return;
	}
	if( CoF_ToolsDropRefused( id ))
	{
		if( id == COF_TOOLS_WEAPON_DUAL ) CoF_ToolsDropRefusal( cl, "dual", "Not while holding two weapons." );
		else CoF_ToolsDropRefusal( cl, id == COF_TOOLS_WEAPON_MP5 ? "mp5" : "no-box", "No ammo box for this weapon." );
		return;
	}
	classname = CoF_ToolsAmmoEntity( id );
	amount = CoF_ToolsAmmoBox( id );
	// the item in hand's own edict, by comparing pointers (the game's pointer is never followed)
	memcpy( &active, pv + COF_TOOLS_ACTIVE_ITEM, sizeof( active ));
	for( i = svs.maxclients + 1; active && i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		if( !SV_IsValidEdict( e ) || e->pvPrivateData != (void *)active ) continue;
		weapon = SV_CoF_StoryEntityPrivate( e, COF_TOOLS_ITEM_NEED );
		break;
	}
	if( weapon )
	{
		int held;
		memcpy( &held, weapon + COF_TOOLS_ITEM_ID, sizeof( held ));
		if( held == id ) memcpy( &type, weapon + COF_TOOLS_ITEM_AMMO_TYPE, sizeof( type ));
	}
	if( !classname || amount <= 0 || ( offset = CoF_ToolsAmmoSlotOffset( type )) < 0 )
	{
		CoF_ToolsDropRefusal( cl, "no-ammo-type", "No ammo box for this weapon." );
		return;
	}
	memcpy( &reserve, pv + offset, sizeof( reserve ));
	if( reserve < amount )
	{
		CoF_ToolsDropRefusal( cl, "too-little", "Not enough spare ammo to drop a box." );
		return;
	}
	// in front of the player, level, not through a wall
	CoF_ToolsForward( 0.0f, ent->v.v_angle[1], fwd );
	VectorMA( ent->v.origin, COF_TOOLS_DROP_AHEAD, fwd, end );
	tr = SV_Move( ent->v.origin, zero, zero, end, MOVE_NOMONSTERS, ent, false );
	ahead = COF_TOOLS_DROP_AHEAD * tr.fraction - 8.0f;
	if( tr.startsolid || ahead < 0.0f ) ahead = 0.0f;
	VectorMA( ent->v.origin, ahead, fwd, spot );
	// a plain box of the game's own: no bonus or copy mark (sv_cof_pickups.c)
	box = SV_CreateNamedEntity( NULL, SV_AllocString( classname ));
	if( !SV_IsValidEdict( box ) || !box->pvPrivateData )
	{
		if( SV_IsValidEdict( box )) SV_FreeEdict( box );
		CoF_ToolsDropRefusal( cl, "no-entity", NULL );
		return;
	}
	VectorCopy( spot, box->v.origin );
	box->v.angles[1] = ent->v.v_angle[1];
	SetBits( box->v.spawnflags, COF_TOOLS_SF_NORESPAWN );
	if( svgame.dllFuncs.pfnSpawn( box ) == -1 || box->free || FBitSet( box->v.flags, FL_KILLME ))
	{
		if( !box->free ) SV_FreeEdict( box );
		CoF_ToolsDropRefusal( cl, "spawn-failed", NULL );
		return;
	}
	SV_LinkEdict( box, false );
	reserve -= amount;
	memcpy( pv + offset, &reserve, sizeof( reserve ));
	SV_CoF_RetailProFont( cl, 5, "You dropped a box of ammo.", false );
	Con_Printf( "cofstory phase=tools-dropammo userid=%d weapon=%d box=%s amount=%d left=%d\n", cl->userid, id, classname, amount, reserve );
}

qboolean SV_CoF_ToolsClientCommand( sv_client_t *cl )
{
	const char *cmd = Cmd_Argv( 0 );
	cof_tools_player_t *p;
	if( Q_strcmp( cmd, "cof_unstuck" ) && Q_strcmp( cmd, "cof_ping" ) && Q_strcmp( cmd, "cof_scare" ) && Q_strcmp( cmd, "cof_dropammo" ))
		return false;
	if( !SV_CoF_StoryCoopEnabled( )) return false;
	if( sv.state != ss_active || !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return true;
	CoF_ToolsCheckGeneration();
	if( !( p = CoF_ToolsSlot( cl ))) return true;
	if( !Q_strcmp( cmd, "cof_unstuck" )) CoF_ToolsUnstuck( cl, p );
	else if( !Q_strcmp( cmd, "cof_ping" )) CoF_ToolsPing( cl, p );
	else if( !Q_strcmp( cmd, "cof_scare" )) CoF_ToolsScare( cl, p );
	else CoF_ToolsDropAmmo( cl, p );
	return true;
}

// The host's settings kept in range: anything else is the default (sv_cof_tools.h).
static void CoF_ToolsKeepRanges( void )
{
	convar_t *var;
	float value;
	int outlines;
	// the game's own (CPR, read by name): created here only when the game did not register it
	if(( var = Cvar_FindVar( "sv_coopcprtime" )) == NULL )
		var = Cvar_Get( "sv_coopcprtime", "5", 0, "Cry of Fear co-op: seconds of CPR to revive a downed partner (1-20)" );
	if( var && ( value = CoF_ToolsClampCprTime( var->value )) != var->value )
	{
		Con_Printf( "cofstory phase=tools-range cvar=sv_coopcprtime set=%g\n", value );
		Cvar_DirectSetValue( var, value );
	}
	if(( var = Cvar_FindVar( "cof_story_respawn_delay" )) != NULL && ( value = CoF_ToolsClampRespawnDelay( var->value )) != var->value )
	{
		Con_Printf( "cofstory phase=tools-range cvar=cof_story_respawn_delay set=%g\n", value );
		Cvar_DirectSetValue( var, value );
	}
	outlines = CoF_ToolsClampOutlines( (int)cof_outlines.value );
	if( (float)outlines != cof_outlines.value ) Cvar_DirectSetValue( &cof_outlines, (float)outlines );
}

static void CoF_ToolsDown( sv_client_t *cl )
{
	char text[96];
	int i;
	Q_snprintf( text, sizeof( text ), "%s is down.", cl->name );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *other = &svs.clients[i];
		if( other == cl || other->state != cs_spawned || !SV_IsValidEdict( other->edict ) || FBitSet( other->flags, FCL_FAKECLIENT )) continue;
		SV_CoF_RetailProFont( other, 2, text, false );
	}
	Con_Printf( "cofstory phase=tools-down userid=%d\n", cl->userid );
}

// Noir: setinfo sc_noir 1 keeps BlackWhite on; turning it off clears it with an explicit message. A story grey of its
// own is left alone while sc_noir stays 0.
static void CoF_ToolsNoir( sv_client_t *cl, cof_tools_player_t *p )
{
	byte *pv, on;
	int want;
	if( FBitSet( cl->flags, FCL_FAKECLIENT ) || !( pv = CoF_ToolsPlayer( cl ))) return;
	want = Q_atoi( Info_ValueForKey( cl->userinfo, "sc_noir" )) ? 1 : 0;
	if( want && pv[COF_TOOLS_BLACKWHITE_OFFSET] != 1 )
	{
		on = 1;
		memset( pv + COF_TOOLS_BLACKWHITE_OFFSET, on, COF_TOOLS_BLACKWHITE_SIZE );
		SV_CoF_SendUserMessage( cl->edict, "BlackWhite", &on, COF_TOOLS_BLACKWHITE_SIZE, true );
		Con_Printf( "cofstory phase=tools-noir userid=%d on=1\n", cl->userid );
	}
	else if( !want && p->noir == 1 )
	{
		on = 0;
		memset( pv + COF_TOOLS_BLACKWHITE_OFFSET, on, COF_TOOLS_BLACKWHITE_SIZE );
		SV_CoF_SendUserMessage( cl->edict, "BlackWhite", &on, COF_TOOLS_BLACKWHITE_SIZE, true );
		Con_Printf( "cofstory phase=tools-noir userid=%d on=0\n", cl->userid );
	}
	p->noir = want;
}

void SV_CoF_ToolsInit( void )
{
	int i;
	Cvar_RegisterVariable( &cof_outlines );
	memset( cof_tools_players, 0, sizeof( cof_tools_players ));
	memset( cof_tools_known, 0, sizeof( cof_tools_known ));
	for( i = 0; i < MAX_CLIENTS; i++ ) cof_tools_players[i].noir = -1;
}

void SV_CoF_ToolsFrame( void )
{
	int i;
	if( sv.state != ss_active || !SV_CoF_StoryCoopEnabled( )) return;
	CoF_ToolsCheckGeneration();
	CoF_ToolsKeepRanges();
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_tools_player_t *p;
		qboolean dead;
		if( cl->state == cs_free )
		{
			memset( &cof_tools_players[i], 0, sizeof( cof_tools_players[i] ));
			cof_tools_players[i].noir = -1;
			continue;
		}
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !( p = CoF_ToolsSlot( cl ))) continue;
		dead = SV_CoF_PartyDead( cl->edict );
		if( dead && !p->dead ) CoF_ToolsDown( cl );
		else if( !dead && p->dead && !FBitSet( cl->flags, FCL_FAKECLIENT ))
		{
			p->remind = true;
			p->why = "respawn";
		}
		p->dead = dead;
		if( p->remind && !dead && !SV_CoF_ArrivalLoading( cl ) && CoF_ToolsRemind( cl, p->why ? p->why : "late-join" ))
			p->remind = false;
		CoF_ToolsNoir( cl, p );
	}
}

// sv_frame.c, after the game's AddToFullPack and SV_CoF_WeaponModelState: another player far from the viewer, drawn
// as the game left them (no render effect, normal render mode), is sent with the glow shell while cof_outlines is 1.
void SV_CoF_ToolsOutline( const edict_t *viewer, entity_state_t *state, int e )
{
	edict_t *ent;
	vec3_t delta;
	if( !state || !viewer || !SV_CoF_StoryCoopEnabled( )) return;
	if( CoF_ToolsClampOutlines( (int)cof_outlines.value ) != COF_TOOLS_OUTLINES_ON ) return;
	if( e < 1 || e > svs.maxclients ) return;
	ent = SV_EdictNum( e );
	if( ent == viewer || !SV_IsValidEdict( ent )) return; // other players only
	if( state->renderfx != kRenderFxNone || state->rendermode != kRenderNormal ) return;
	VectorSubtract( ent->v.origin, viewer->v.origin, delta );
	if( DotProduct( delta, delta ) < COF_TOOLS_OUTLINE_FAR * COF_TOOLS_OUTLINE_FAR ) return;
	state->renderfx = COF_TOOLS_RENDERFX_GLOWSHELL;
	state->rendercolor.r = 96;
	state->rendercolor.g = 160;
	state->rendercolor.b = 255;
	state->renderamt = 4; // the shell's thickness
}
