/* Cry of Fear story co-op: the AI companion. GPL-3.0-or-later.
 * For players who want the campaign with company but no second person: the
 * engine adds a bot player (a fake client: no window, no network) and drives
 * it every frame with usercmds and the game's own player code, so it moves,
 * fights, revives and heals through the retail rules. The same mind can drive
 * an idle second human client (solo testing with two windows): the moment
 * that client's own controls are touched, the person has it back.
 * Decisions are pure state in sv_cof_brain.h; this file perceives and acts,
 * and finds ways round what blocks its path on the map's own route graph
 * (sv_cof_nav.h).
 *
 * The story layer already keeps fake clients out of everything that could
 * change the story for the humans: the exit hold, shared items and state,
 * checkpoints and recovery. Here a bot also never touches a story trigger or
 * an item (sv_world.c asks SV_CoF_CompanionTriggerTouch), so it cannot fire a
 * script, change the level or take what is the people's. The engine drops it
 * at every level change and it is added again once the host is in.
 * The game's player Spawn clears the bot's FL_FAKECLIENT edict flag, which
 * also keeps the person's shots from hurting it (the multiplayer damage rule
 * spares players without the flag); the client flag FCL_FAKECLIENT stays. */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_party.h"
#include "sv_cof_companion.h"
#include "sv_cof_brain.h"
#include "sv_cof_nav.h"

#define COF_COMPANION_THREAT_RANGE 900.0f // the companion engages threats this close to it...
#define COF_COMPANION_GUARD_RANGE 1000.0f // ...and this close to the leader
#define COF_COMPANION_FIRE_CLEARANCE 40.0f // the leader this close to the line of fire: hold fire
#define COF_COMPANION_ENGAGED_BONUS 300.0f // a monster already after a player counts as this much nearer
#define COF_COMPANION_PARTICIPANTS 4 // the game refuses a fifth player
#define COF_COMPANION_ECHO 32 // views the AI handed a driven client lately: its usercmds echo them some frames late
#define COF_COMPANION_AHEAD 24.0f // how far ahead of the feet it looks for a door or a low obstacle
#define COF_COMPANION_STEP 18.0f // the player code steps up this much by itself (sv_stepsize)
#define COF_COMPANION_JUMP 36.0f // a jump lifts the feet about 45 units: this much leaves a margin
#define COF_COMPANION_CONTACT 4.0f // a step is looked for this far past where the feet touch it...
#define COF_COMPANION_LAND_AHEAD 12.0f // ...a landing this far past it...
#define COF_COMPANION_LAND_DROP 48.0f // ...on a floor at most this far under jump height
#define COF_COMPANION_ROUTE 128 // route nodes kept
#define COF_COMPANION_NAV_RETRY 10.0 // no graph for the map yet (the game builds it after the map starts): look again this often
#define COF_COMPANION_NAV_REACH 640.0f // the graph points near a body that it tries to walk to or from
#define COF_COMPANION_NAV_TRIES 6 // ...nearest first, at most this many
#define COF_COMPANION_REPLAN 1.0 // a route is planned again this often while it is used
#define COF_COMPANION_REACHED 32.0f // this close to a route point (flat): on to the next
#define COF_COMPANION_GOAL_MOVED 96.0f // the goal moved this far from where the route was planned to: plan again
#define COF_COMPANION_DOOR_COST 128.0f // a shut door it may open costs this much extra on a route
#define COF_COMPANION_AVOID 4 // links it got stuck on, left out of routes...
#define COF_COMPANION_AVOID_TIME 30.0 // ...for this long
#define COF_COMPANION_MODELS 1024 // brush models ("*N") mapped to their entities
#define COF_COMPANION_PLACE_PATIENCE 3.0 // no spot out of the player's sight this long: the least seen one
#define COF_COMPANION_PARTNER 48.0f // another player this close in its way: as near as it gets for now

static convar_t *cof_companion; // how many companion bots to keep (0: none)
static convar_t *cof_companion_name;
static convar_t *cof_companion_drive; // the AI also drives idle second players (solo testing)
static convar_t *cof_companion_idle; // seconds without input before the AI takes a second player over
static convar_t *cof_companion_regen; // health a companion bot recovers each second out of a fight
static convar_t *cof_companion_medic; // seconds between syringes for the leader (0: never)
static convar_t *cof_companion_ammo; // a bot never runs out of rounds
static convar_t *cof_companion_light; // a bot keeps its flashlight on

typedef struct
{
	int userid;
	qboolean bot; // a fake client the companion module added
	qboolean driving; // the AI drives this client this frame
	cof_brain_t brain;
	cof_brain_output_t out; // the latest decision
	int mode;
	double human_until; // a person used this client's controls: hands off until then
	float own_view[3]; // the client's own last view angles (a moved mouse is a person)
	qboolean have_view;
	float sent[COF_COMPANION_ECHO][2]; // pitch and yaw the AI handed this client, newest at sent_next - 1
	int sent_next, sent_count;
	int taps; // presses still to hand a driven person's client (one usercmd each)
	qboolean tapped; // the last usercmd carried them: release first
	double equip_after, revive_after, getup_after, light_after, give_after;
	char asked[32]; // the weapon last asked for (logged once)
	double calm_since; // no threat in sight since
	qboolean stay; // told to wait here
	vec3_t stay_pos;
	int door; // the entity of the door found ahead this frame (0: none)
	int route[COF_COMPANION_ROUTE], route_length, route_at; // graph nodes round what blocks the way, and the next one
	vec3_t route_goal; // where the route was planned to
	double route_after; // plan again from then
	int routed; // the last plan: 1 found a way, -1 none, 0 not yet (for the log)
	int avoid[COF_COMPANION_AVOID]; // links (+1) it got stuck on
	double avoid_until[COF_COMPANION_AVOID];
	int avoid_next;
	int leader_userid; // whom it looked after last frame (a new one starts a new trail)
	qboolean place_pending, place_near; // a teleport the brain asked for, still to place...
	vec3_t place_at; // ...near here (where the game put the leader) or behind them
	const char *place_why;
	double place_since;
	qboolean leader_held; // the leader is held by the game this frame
} cof_companion_t;

static cof_companion_t cof_companions[MAX_CLIENTS];
static int cof_companion_generation = -1;
static double cof_companion_add_after;
static double cof_companion_medic_after; // the party's next syringe for the leader: one per cooldown, whoever gives it
static int cof_companion_medic_slot = -1; // the bot giving it now
static int cof_companion_down_told; // the leader's userid last told that help is coming (0: nobody down)

// The map's route graph (sv_cof_nav.h), read once a companion needs it.
static cof_nav_t cof_nav;
static int cof_nav_state; // 0 not read yet, 1 read, -1 none (looked for again from cof_nav_after)
static double cof_nav_after;
static qboolean cof_nav_told; // "no graph" logged for this map
static int cof_nav_model[COF_COMPANION_MODELS]; // the entity of brush model "*N" (doors across links)

static const char *cof_companion_modes[] = { "idle", "follow", "fight", "revive", "heal", "held", "dead", "medic", "struggle" };

static void CoF_CompanionNavReset( void )
{
	cof_nav.nodes = cof_nav.links = 0;
	cof_nav_state = 0;
	cof_nav_after = 0.0;
	cof_nav_told = false;
}

void SV_CoF_CompanionReset( void )
{
	memset( cof_companions, 0, sizeof( cof_companions ));
	cof_companion_generation = -1;
	cof_companion_add_after = 0.0;
	cof_companion_medic_after = 0.0;
	cof_companion_medic_slot = -1;
	cof_companion_down_told = 0;
	CoF_CompanionNavReset();
}

static int CoF_CompanionSlot( const sv_client_t *cl )
{
	int slot = (int)( cl - svs.clients );
	return ( slot >= 0 && slot < svs.maxclients && slot < MAX_CLIENTS ) ? slot : -1;
}

static qboolean CoF_CompanionAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO;
}

// The player the companion follows: the first person in the game.
static sv_client_t *CoF_CompanionLeader( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state == cs_spawned && !FBitSet( cl->flags, FCL_FAKECLIENT ) && SV_IsValidEdict( cl->edict ) &&
			!( cof_companion_drive && cof_companion_drive->value && cof_companions[i].driving ))
			return cl;
	}
	return NULL;
}

// Whom a bot looks after: the downed person nearest to it (CPR comes first), else the
// people in turn: the bot of rank k follows person k (counted again if there are fewer).
static sv_client_t *CoF_CompanionLeaderFor( const sv_client_t *self, int rank, int *behind )
{
	sv_client_t *people[MAX_CLIENTS], *down = NULL;
	float best = 0.0f;
	int i, n = 0;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			( cof_companion_drive && cof_companion_drive->value && cof_companions[i].driving )) continue;
		people[n++] = cl;
		if( !CoF_CompanionAlive( cl->edict ) && SV_CoF_RetailRevivable( cl ))
		{
			float distance = (float)VectorDistance( cl->edict->v.origin, self->edict->v.origin );
			if( !down || distance < best ) { down = cl; best = distance; }
		}
	}
	*behind = n ? rank / n : 0; // bots ahead of it behind the same person
	if( down ) return down;
	return n ? people[rank % n] : NULL;
}

// A person is alive in the game. The game declares "Game over" when the last
// living player leaves while everyone else is dead, so a bot only goes then.
static qboolean CoF_CompanionPersonAlive( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && !FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ) &&
			SV_IsValidEdict( svs.clients[i].edict ) && CoF_CompanionAlive( svs.clients[i].edict )) return true;
	return false;
}

static int CoF_CompanionBots( void )
{
	int i, bots = 0;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( cof_companions[i].bot && svs.clients[i].state >= cs_connected && FBitSet( svs.clients[i].flags, FCL_FAKECLIENT )) bots++;
	return bots;
}

// The first of "Companion", "Companion 2", "Companion 3"... no other player has (the
// engine would rename a duplicate "Companion (1)").
static void CoF_CompanionName( char *name, size_t size )
{
	const char *base = cof_companion_name && cof_companion_name->string[0] ? cof_companion_name->string : "Companion";
	int n, i;
	for( n = 1; n <= MAX_CLIENTS; n++ )
	{
		qboolean taken = false;
		if( n == 1 ) Q_strncpy( name, base, size );
		else Q_snprintf( name, size, "%s %d", base, n );
		for( i = 0; i < svs.maxclients && !taken; i++ )
			taken = svs.clients[i].state >= cs_connected && !Q_stricmp( svs.clients[i].name, name );
		if( !taken ) return;
	}
}

static qboolean CoF_CompanionAdd( void )
{
	char reject[128] = "", name[32];
	edict_t *ent;
	sv_client_t *cl;
	int slot, i, players = 0, open_slot = -1;
	for( i = 0; i < svs.maxclients; i++ )
	{
		if( svs.clients[i].state >= cs_connected ) players++;
		else if( svs.clients[i].state == cs_free && open_slot < 0 ) open_slot = i;
	}
	if( players >= COF_COMPANION_PARTICIPANTS || open_slot < 0 ) return false;
	// A reused slot still holds the last player's game object: start from a
	// clean edict, as a person's connection does, or the bot inherits their
	// state (and the game's name-change hook would announce it). SV_FakeConnect
	// takes this same first free slot.
	SV_InitEdict( SV_EdictNum( open_slot + 1 ));
	CoF_CompanionName( name, sizeof( name ));
	if( !( ent = SV_FakeConnect( name )) || !( cl = SV_ClientFromEdict( ent, false )) || ( slot = CoF_CompanionSlot( cl )) < 0 )
		return false;
	if( slot != open_slot ) SV_InitEdict( ent );
	ent->v.flags = FL_CLIENT | FL_FAKECLIENT;
	ent->v.netname = SV_MakeString( cl->name );
	ent->v.colormap = NUM_FOR_EDICT( ent );
	if( !svgame.dllFuncs.pfnClientConnect( ent, cl->name, "127.0.0.1", reject ))
	{
		Con_Printf( "cofstory phase=companion-refused reason=%s\n", reject[0] ? reject : "game" );
		cl->state = cs_connected; // never put in: no game object for its disconnect to touch
		SV_DropClient( cl, false );
		cl->state = cs_free;
		return false;
	}
	svgame.dllFuncs.pfnClientPutInServer( ent );
	memset( &cof_companions[slot], 0, sizeof( cof_companions[slot] ));
	cof_companions[slot].userid = cl->userid;
	cof_companions[slot].bot = true;
	CoF_BrainReset( &cof_companions[slot].brain );
	SV_CoF_PartyPlayerReady( cl, true ); // placed beside the leader once both play
	Con_Printf( "cofstory phase=companion-added userid=%d name=%s\n", cl->userid, cl->name );
	return true;
}

static void CoF_CompanionRemove( sv_client_t *cl )
{
	int slot = CoF_CompanionSlot( cl );
	Con_Printf( "cofstory phase=companion-removed userid=%d\n", cl->userid );
	SV_DropClient( cl, false );
	cl->state = cs_free; // a fake client has no connection to wind down
	if( slot >= 0 ) memset( &cof_companions[slot], 0, sizeof( cof_companions[slot] ));
}

// Someone besides this client is up: a person, or another bot of ours. The game
// declares "Game over" when the last living player leaves while the rest are dead.
static qboolean CoF_CompanionOtherAlive( const sv_client_t *except )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl == except || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !CoF_CompanionAlive( cl->edict )) continue;
		if( !FBitSet( cl->flags, FCL_FAKECLIENT ) || cof_companions[i].bot ) return true;
	}
	return false;
}

qboolean SV_CoF_CompanionMakeRoom( void )
{
	int i;
	for( i = svs.maxclients - 1; i >= 0 && i < MAX_CLIENTS; i-- )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cof_companions[i].bot && cl->state >= cs_connected && FBitSet( cl->flags, FCL_FAKECLIENT ))
		{
			if( !CoF_CompanionOtherAlive( cl )) return false; // the server stays full a moment longer
			CoF_CompanionRemove( cl );
			// The person takes the place at once; a bot comes back only when one is free again.
			cof_companion_add_after = sv.time + 2.0;
			return true;
		}
	}
	return false;
}

void SV_CoF_CompanionDrop( sv_client_t *cl )
{
	int slot = cl ? CoF_CompanionSlot( cl ) : -1;
	if( slot >= 0 && !cof_companions[slot].bot ) memset( &cof_companions[slot], 0, sizeof( cof_companions[slot] ));
}

qboolean SV_CoF_CompanionDriven( const sv_client_t *cl )
{
	int slot = cl ? CoF_CompanionSlot( cl ) : -1;
	return slot >= 0 && cof_companions[slot].userid == cl->userid && ( cof_companions[slot].bot || cof_companions[slot].driving );
}

int SV_CoF_CompanionLeaderUserid( const sv_client_t *cl )
{
	int slot = cl ? CoF_CompanionSlot( cl ) : -1;
	return slot >= 0 && cof_companions[slot].userid == cl->userid && cof_companions[slot].bot ? cof_companions[slot].leader_userid : 0;
}

qboolean SV_CoF_CompanionAway( const sv_client_t *cl )
{
	int slot = cl ? CoF_CompanionSlot( cl ) : -1;
	const cof_companion_t *c;
	if( slot < 0 || FBitSet( cl->flags, FCL_FAKECLIENT ) || !cof_companion_drive || !cof_companion_drive->value ) return false;
	c = &cof_companions[slot];
	if( c->userid != cl->userid ) return false;
	return c->driving || ( c->have_view && sv.time >= c->human_until && cl != CoF_CompanionLeader());
}

qboolean SV_CoF_CompanionTriggerTouch( edict_t *trigger, edict_t *ent )
{
	const char *name;
	int slot;
	if( !trigger || !ent || !SV_CoF_StoryCoopEnabled() ) return false;
	slot = NUM_FOR_EDICT( ent ) - 1;
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !cof_companions[slot].bot ||
		!FBitSet( svs.clients[slot].flags, FCL_FAKECLIENT ) || svs.clients[slot].edict != ent ) return false;
	// Only what moves or hurts a body, and a hurt volume only when it fires
	// nothing (its target would run with the bot as activator). A fall-death
	// floor is a trigger_once: a bot that falls is brought back by the AI instead.
	name = SV_GetString( trigger->v.classname );
	if( !Q_strcmp( name, "trigger_hurt" )) return trigger->v.target != 0 && SV_GetString( trigger->v.target )[0];
	return Q_strcmp( name, "trigger_push" ) && Q_strcmp( name, "trigger_gravity" );
}

// A door the AI may open: an ordinary func_door or func_door_rotating that fires
// nothing and that no trigger opens. The others belong to the story.
static qboolean CoF_CompanionDoor( edict_t *e )
{
	const char *name;
	if( !SV_IsValidEdict( e ) || NUM_FOR_EDICT( e ) <= 0 ) return false;
	name = SV_GetString( e->v.classname );
	if( Q_strcmp( name, "func_door" ) && Q_strcmp( name, "func_door_rotating" )) return false;
	return !( e->v.target && SV_GetString( e->v.target )[0] ) && !( e->v.netname && SV_GetString( e->v.netname )[0] ) &&
		!( e->v.targetname && SV_GetString( e->v.targetname )[0] );
}

// Just ahead of the feet, the way the AI walked last frame (its moves are
// relative to the view yaw it sent): a door it may open; else something low that
// a jump clears. Measured from where the feet touch it: clear at step height just
// past that (a step, or a flight of stairs: the walk climbs them; sweeping the
// whole way at step height met the third riser and hopped up every staircase),
// blocked there but clear at jump height, with a floor to land on beyond.
static void CoF_CompanionAhead( edict_t *ent, cof_companion_t *c, cof_brain_input_t *in )
{
	float yaw = c->out.angles[1] / 57.2957795f, x, y, length, contact;
	vec3_t dir, end, from, to, down;
	trace_t tr;
	c->door = 0;
	if( !in->alive || in->held || !in->onground ) return;
	x = c->out.forward * cosf( yaw ) + c->out.side * sinf( yaw );
	y = c->out.forward * sinf( yaw ) - c->out.side * cosf( yaw );
	length = sqrtf( x * x + y * y );
	if( length < 1.0f ) return;
	VectorSet( dir, x / length, y / length, 0.0f );
	VectorMA( ent->v.origin, COF_COMPANION_AHEAD, dir, end );
	tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.fraction >= 1.0f ) return;
	if( CoF_CompanionDoor( tr.ent ))
	{
		in->door_ahead = 1;
		c->door = NUM_FOR_EDICT( tr.ent );
		return;
	}
	contact = COF_COMPANION_AHEAD * tr.fraction;
	VectorCopy( ent->v.origin, from ); from[2] += COF_COMPANION_STEP;
	VectorMA( from, contact + COF_COMPANION_CONTACT, dir, to );
	tr = SV_Move( from, ent->v.mins, ent->v.maxs, to, MOVE_NOMONSTERS, ent, false );
	if( !tr.startsolid && tr.fraction >= 1.0f ) return; // a step, or stairs
	VectorCopy( ent->v.origin, from ); from[2] += COF_COMPANION_JUMP;
	VectorMA( from, contact + COF_COMPANION_LAND_AHEAD, dir, to );
	tr = SV_Move( from, ent->v.mins, ent->v.maxs, to, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.fraction < 1.0f ) return; // too high
	VectorCopy( to, down ); down[2] -= COF_COMPANION_LAND_DROP;
	tr = SV_Move( to, ent->v.mins, ent->v.maxs, down, MOVE_NOMONSTERS, ent, false );
	in->ledge_ahead = !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f; // somewhere to land
}

// The standing body walks from 'from' along dir for the probe's length (or climbs a step doing it).
static qboolean CoF_CompanionWalks( edict_t *ent, const vec3_t from, const vec3_t dir )
{
	vec3_t to, up, up_to;
	trace_t tr;
	VectorMA( from, COF_COMPANION_AHEAD, dir, to );
	tr = SV_Move( from, ent->v.mins, ent->v.maxs, to, MOVE_NORMAL, ent, false );
	if( !tr.startsolid && tr.fraction >= 1.0f ) return true;
	VectorCopy( from, up ); up[2] += COF_COMPANION_STEP;
	VectorCopy( to, up_to ); up_to[2] += COF_COMPANION_STEP;
	tr = SV_Move( up, ent->v.mins, ent->v.maxs, up_to, MOVE_NORMAL, ent, false );
	return !tr.startsolid && tr.fraction >= 1.0f;
}

// Blocked the way it walked last frame (a door jamb, a corner, a partner): a spot a
// little to one side from which the way is clear, for the brain to line up with.
// Pushing straight into a wall, the player code slides too little to get round it.
static void CoF_CompanionProbe( edict_t *ent, cof_companion_t *c, cof_brain_input_t *in )
{
	static const float offsets[] = { 8.0f, 16.0f, 24.0f, 32.0f };
	vec3_t dir, left, beside;
	float x = c->out.move_to[0] - ent->v.origin[0], y = c->out.move_to[1] - ent->v.origin[1], length = sqrtf( x * x + y * y );
	int i, side;
	if( !c->out.going || !in->alive || in->held || !in->onground || length < 1.0f ) return;
	VectorSet( dir, x / length, y / length, 0.0f );
	VectorSet( left, -dir[1], dir[0], 0.0f );
	if( CoF_CompanionWalks( ent, ent->v.origin, dir )) return;
	for( i = 0; i < (int)ARRAYSIZE( offsets ); i++ )
		for( side = 1; side >= -1; side -= 2 )
		{
			trace_t tr;
			VectorMA( ent->v.origin, offsets[i] * (float)side, left, beside );
			tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, beside, MOVE_NORMAL, ent, false );
			if( tr.startsolid || tr.fraction < 1.0f || !CoF_CompanionWalks( ent, beside, dir )) continue;
			in->side_shift = offsets[i] * (float)side;
			in->probe_yaw = atan2f( dir[1], dir[0] ) * 57.2957795f;
			return;
		}
}

// The entity of brush model "*N" (a door across a link), or NULL.
static edict_t *CoF_CompanionModel( int model )
{
	const char *name;
	edict_t *e;
	if( model <= 0 || model >= COF_COMPANION_MODELS || cof_nav_model[model] <= 0 || cof_nav_model[model] >= svgame.numEntities ) return NULL;
	e = SV_EdictNum( cof_nav_model[model] );
	if( !SV_IsValidEdict( e ) || !e->v.model ) return NULL;
	name = SV_GetString( e->v.model );
	return name[0] == '*' && Q_atoi( name + 1 ) == model ? e : NULL;
}

// The map's route graph, read the first time a companion needs it; a map with
// none (yet: the game builds a missing one after the map starts) is looked at
// again now and then.
static qboolean CoF_CompanionNavLoad( void )
{
	char path[MAX_QPATH + 32];
	fs_offset_t size = 0;
	byte *data;
	int i;
	if( cof_nav_state > 0 ) return true;
	if( cof_nav_state < 0 && sv.time < cof_nav_after ) return false;
	cof_nav_after = sv.time + COF_COMPANION_NAV_RETRY;
	Q_snprintf( path, sizeof( path ), "maps/graphs/%s.nod", sv.name );
	data = FS_LoadFile( path, &size, false );
	if( !data || !CoF_NavLoad( &cof_nav, data, (size_t)size ))
	{
		if( data ) Mem_Free( data );
		cof_nav_state = -1;
		if( !cof_nav_told ) Con_Printf( "cofstory phase=companion-nav map=%s graph=none\n", sv.name );
		cof_nav_told = true;
		return false;
	}
	Mem_Free( data );
	memset( cof_nav_model, 0, sizeof( cof_nav_model ));
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		const char *name;
		int model;
		if( !SV_IsValidEdict( e ) || !e->v.model ) continue;
		name = SV_GetString( e->v.model );
		if( name[0] == '*' && ( model = Q_atoi( name + 1 )) > 0 && model < COF_COMPANION_MODELS ) cof_nav_model[model] = i;
	}
	cof_nav_state = 1;
	Con_Printf( "cofstory phase=companion-nav map=%s nodes=%d links=%d\n", sv.name, cof_nav.nodes, cof_nav.links );
	return true;
}

// A walk straight from a body at from to a body at to: the crouching box from
// step height up, so a stair step does not count (a wall or a shut door does).
static qboolean CoF_CompanionStraight( const vec3_t from, const vec3_t to, edict_t *ignore )
{
	vec3_t mins = { -16.0f, -16.0f, -18.0f }, maxs = { 16.0f, 16.0f, 18.0f };
	trace_t tr = SV_Move( from, mins, maxs, to, MOVE_NOMONSTERS, ignore, false );
	return !tr.startsolid && tr.fraction >= 1.0f;
}

// The nearest graph node a body at pos walks to straight, or -1.
static int CoF_CompanionNode( const vec3_t pos, edict_t *ignore )
{
	int nearest[COF_COMPANION_NAV_TRIES], count, i;
	count = CoF_NavNearest( &cof_nav, pos, COF_COMPANION_NAV_REACH, nearest, COF_COMPANION_NAV_TRIES );
	for( i = 0; i < count; i++ )
	{
		vec3_t point;
		CoF_NavPoint( &cof_nav, nearest[i], point );
		if( CoF_CompanionStraight( pos, point, ignore )) return nearest[i];
	}
	return -1;
}

// What a link costs on top of its length now: nothing; more for a shut door it
// may open; or it is left out (a link it got stuck on lately, a shut door that
// belongs to the story).
static float CoF_CompanionLinkCost( void *context, int link )
{
	const cof_companion_t *c = context;
	const cof_nav_link_t *l = &cof_nav.link[link];
	vec3_t from, to, zero = { 0.0f, 0.0f, 0.0f };
	edict_t *door;
	trace_t tr;
	int i;
	for( i = 0; i < COF_COMPANION_AVOID; i++ )
		if( c->avoid[i] == link + 1 && sv.time < c->avoid_until[i] ) return -1.0f;
	if( !l->door || !( door = CoF_CompanionModel( l->door ))) return 0.0f;
	// shut when the line through the doorway stops on it
	CoF_NavPoint( &cof_nav, l->src, from );
	CoF_NavPoint( &cof_nav, l->dest, to );
	tr = SV_Move( from, zero, zero, to, MOVE_NOMONSTERS, NULL, false );
	if( tr.ent != door ) return 0.0f;
	return CoF_CompanionDoor( door ) ? COF_COMPANION_DOOR_COST : -1.0f;
}

// A route from where it stands to goal: from the nearest node it walks to
// straight, to the nearest node from which goal is straight ahead.
static void CoF_CompanionPlan( sv_client_t *cl, cof_companion_t *c, const vec3_t goal )
{
	edict_t *ent = cl->edict;
	int start, end, length = 0;
	c->route_after = sv.time + COF_COMPANION_REPLAN;
	VectorCopy( goal, c->route_goal );
	c->route_length = c->route_at = 0;
	start = CoF_CompanionNode( ent->v.origin, ent );
	end = start >= 0 ? CoF_CompanionNode( goal, ent ) : -1;
	if( end >= 0 ) length = CoF_NavRoute( &cof_nav, start, end, CoF_CompanionLinkCost, c, c->route, COF_COMPANION_ROUTE );
	c->route_length = length;
	if(( length > 0 ? 1 : -1 ) == c->routed ) return;
	c->routed = length > 0 ? 1 : -1;
	if( length > 0 ) Con_Printf( "cofstory phase=companion-route userid=%d nodes=%d\n", cl->userid, length );
	else Con_Printf( "cofstory phase=companion-route userid=%d none\n", cl->userid );
}

// Where the AI was going last frame (out.goal) lies behind something: the next
// point of a route round it (in->nav). Planned again every second while used and
// at once when the goal moves on; none as soon as the way there is clear.
static void CoF_CompanionNavigate( sv_client_t *cl, cof_companion_t *c, cof_brain_input_t *in )
{
	const cof_brain_output_t *last = &c->out;
	edict_t *ent = cl->edict;
	vec3_t point;
	int i;
	if( !last->going || !in->alive || in->held || !CoF_CompanionNavLoad())
	{
		c->route_length = 0;
		return;
	}
	if( last->replan )
	{
		// stuck on the leg to the next point: leave that link out for a while
		int link = c->route_at > 0 && c->route_at < c->route_length ? CoF_NavLink( &cof_nav, c->route[c->route_at - 1], c->route[c->route_at] ) : -1;
		if( link >= 0 )
		{
			c->avoid[c->avoid_next] = link + 1;
			c->avoid_until[c->avoid_next] = sv.time + COF_COMPANION_AVOID_TIME;
			c->avoid_next = ( c->avoid_next + 1 ) % COF_COMPANION_AVOID;
		}
		Con_Printf( "cofstory phase=companion-replan userid=%d link=%d\n", cl->userid, link );
		c->route_length = 0;
		c->route_after = 0.0;
	}
	if( CoF_BrainDist2D( in->self, last->goal ) < COF_COMPANION_REACHED * 2.0f || CoF_CompanionStraight( ent->v.origin, last->goal, ent ))
	{
		c->route_length = 0;
		return;
	}
	if( sv.time >= c->route_after || VectorDistance( c->route_goal, last->goal ) > COF_COMPANION_GOAL_MOVED )
		CoF_CompanionPlan( cl, c, last->goal );
	// on along it: past the points reached, and past the next one when the one after it is straight ahead
	while( c->route_at < c->route_length )
	{
		CoF_NavPoint( &cof_nav, c->route[c->route_at], point );
		if( CoF_BrainDist2D( in->self, point ) >= COF_COMPANION_REACHED || fabs( point[2] - in->self[2] ) > 48.0f ) break;
		c->route_at++;
	}
	for( i = 0; i < 2 && c->route_at + 1 < c->route_length; i++ ) // a fresh plan starts at the nearest node, maybe one behind
	{
		CoF_NavPoint( &cof_nav, c->route[c->route_at + 1], point );
		if( !CoF_CompanionStraight( ent->v.origin, point, ent )) break;
		c->route_at++;
	}
	if( c->route_at >= c->route_length ) return; // at the last node: the goal is straight ahead from there
	in->nav = 1;
	CoF_NavPoint( &cof_nav, c->route[c->route_at], in->nav_point );
}

// A clear line between two points (world and brushes only; monsters do not block sight).
static qboolean CoF_CompanionSees( const vec3_t from, const vec3_t to, edict_t *ignore, edict_t *target )
{
	vec3_t zero = { 0, 0, 0 };
	trace_t tr = SV_Move( from, zero, zero, to, MOVE_NOMONSTERS, ignore, false );
	return !tr.startsolid && ( tr.fraction >= 1.0f || ( target && tr.ent == target ));
}

// A point within clearance of the segment from a to b.
static qboolean CoF_CompanionInLine( const vec3_t a, const vec3_t b, const vec3_t p )
{
	vec3_t ab, ap, closest;
	float t, length = 0.0f;
	VectorSubtract( b, a, ab );
	VectorSubtract( p, a, ap );
	length = DotProduct( ab, ab );
	if( length < 1.0f ) return false;
	t = DotProduct( ap, ab ) / length;
	if( t <= 0.0f || t >= 1.0f ) return false;
	VectorMA( a, t, ab, closest );
	return VectorDistance( closest, p ) < COF_COMPANION_FIRE_CLEARANCE;
}

// No other living player (a person or another bot) stands in the line of fire.
static qboolean CoF_CompanionFireClear( const sv_client_t *self, const vec3_t eye, const vec3_t target )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		vec3_t body;
		if( cl == self || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !CoF_CompanionAlive( cl->edict )) continue;
		VectorCopy( cl->edict->v.origin, body ); // the chest, and the head
		if( CoF_CompanionInLine( eye, target, body )) return false;
		VectorAdd( cl->edict->v.origin, cl->edict->v.view_ofs, body );
		if( CoF_CompanionInLine( eye, target, body )) return false;
	}
	return true;
}

// How far a point is from a box (0 inside it).
static float CoF_CompanionGap( const vec3_t point, const vec3_t mins, const vec3_t maxs )
{
	vec3_t nearest;
	int i;
	for( i = 0; i < 3; i++ ) nearest[i] = bound( mins[i], point[i], maxs[i] );
	return (float)VectorDistance( point, nearest );
}

// The nearest living, hostile, visible monster near both the companion and the
// leader; one already after a player counts as nearer. A dead monster keeps
// health above zero (BecomeDead): only a living one takes aimed damage.
static edict_t *CoF_CompanionThreat( edict_t *self, const vec3_t eye, const float *guard, vec3_t aim )
{
	edict_t *best = NULL;
	float best_score = 0.0f;
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		vec3_t center;
		float distance, score;
		int hostile;
		if( !SV_IsValidEdict( e ) || ( e->v.flags & FL_KILLME ) || !( e->v.flags & FL_MONSTER ) ||
			e->v.takedamage != DAMAGE_AIM || !CoF_CompanionAlive( e ) || ( e->v.effects & EF_NODRAW ) ||
			( e->v.rendermode != kRenderNormal && e->v.renderamt <= 0.0f )) continue;
		VectorAverage( e->v.absmin, e->v.absmax, center );
		distance = (float)VectorDistance( eye, center );
		if( distance > COF_COMPANION_THREAT_RANGE ) continue;
		if( guard && VectorDistance( guard, center ) > COF_COMPANION_GUARD_RANGE ) continue;
		if( !( hostile = SV_CoF_RetailHostile( e ))) continue;
		score = distance - ( hostile == 2 ? COF_COMPANION_ENGAGED_BONUS : 0.0f );
		if( best && score >= best_score ) continue;
		if( !CoF_CompanionSees( eye, center, self, e )) continue;
		best = e;
		best_score = score;
		VectorCopy( center, aim );
	}
	return best;
}

static int CoF_CompanionWeapon( int arms )
{
	switch( arms )
	{
	case COF_ARMS_MELEE: return COF_WEAPON_MELEE;
	case COF_ARMS_GUN: return COF_WEAPON_GUN;
	case COF_ARMS_HEAL: return COF_WEAPON_HEAL;
	default: return COF_WEAPON_NONE;
	}
}

// Other players around it: one in its way to the goal (within reach), and the
// nearest one closer than personal space (the leader not counted: the brain sees them).
static void CoF_CompanionPartners( sv_client_t *cl, sv_client_t *leader, cof_companion_t *c, cof_brain_input_t *in )
{
	edict_t *ent = cl->edict;
	float best = COF_BRAIN_PERSONAL;
	int i;
	if( c->out.going || leader )
	{
		// toward where it was going, or (standing) toward the leader: else the check would
		// switch itself off the moment it stopped
		const float *goal = c->out.going ? c->out.goal : leader->edict->v.origin;
		float x = goal[0] - ent->v.origin[0], y = goal[1] - ent->v.origin[1], length = sqrtf( x * x + y * y );
		if( length > 1.0f )
		{
			vec3_t to;
			trace_t tr;
			VectorCopy( ent->v.origin, to );
			to[0] += x / length * Q_min( length, COF_COMPANION_PARTNER );
			to[1] += y / length * Q_min( length, COF_COMPANION_PARTNER );
			tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, to, MOVE_NORMAL, ent, false );
			if( tr.fraction < 1.0f && tr.ent && NUM_FOR_EDICT( tr.ent ) >= 1 && NUM_FOR_EDICT( tr.ent ) <= svs.maxclients &&
				( !leader || tr.ent != leader->edict ))
				in->partner_block = 1;
		}
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *other = &svs.clients[i];
		float distance;
		if( other == cl || other == leader || other->state != cs_spawned || !SV_IsValidEdict( other->edict ) || !CoF_CompanionAlive( other->edict )) continue;
		distance = CoF_BrainDist2D( ent->v.origin, other->edict->v.origin );
		if( distance >= best ) continue;
		best = distance;
		in->crowd = 1;
		VectorCopy( other->edict->v.origin, in->crowd_pos );
	}
}

// Held by the game: frozen, watching another view (a cutscene camera), not walking
// (a ladder) or going through a door.
static qboolean CoF_CompanionHeld( sv_client_t *cl )
{
	edict_t *ent = cl->edict;
	return ( ent->v.flags & FL_FROZEN ) || ( cl->pViewEntity && cl->pViewEntity != ent ) || ent->v.movetype != MOVETYPE_WALK ||
		SV_CoF_PartyInDoor( cl );
}

static void CoF_CompanionPerceive( sv_client_t *cl, sv_client_t *leader, cof_companion_t *c, qboolean bot, int rank, cof_brain_input_t *in, cof_retail_arms_t *arms )
{
	edict_t *ent = cl->edict, *threat;
	memset( in, 0, sizeof( *in ));
	in->rank = rank;
	in->now = sv.time;
	in->frametime = bound( 0.001f, (float)sv.frametime, 0.1f );
	VectorCopy( ent->v.origin, in->self );
	VectorAdd( ent->v.origin, ent->v.view_ofs, in->eye );
	VectorCopy( ent->v.v_angle, in->view );
	in->alive = CoF_CompanionAlive( ent );
	in->held = ( ent->v.flags & FL_FROZEN ) || ( cl->pViewEntity && cl->pViewEntity != ent ) || ent->v.movetype != MOVETYPE_WALK;
	in->onground = ( ent->v.flags & FL_ONGROUND ) != 0;
	in->health = (int)ent->v.health;
	in->revived = !in->alive && SV_CoF_RetailRevived( cl );
	in->cpr = in->alive && SV_CoF_RetailReviving( cl );
	in->struggle = in->alive && ent->v.fuser3 == 10.0f; // the suicide struggle's animation state
	if( SV_CoF_RetailArms( cl, arms ))
	{
		in->weapon = CoF_CompanionWeapon( arms->weapon );
		in->ready = arms->ready;
		in->sights = arms->sights;
		in->reloading = arms->reloading;
		in->struggle |= in->alive && arms->struggle;
		in->clip = arms->clip;
		in->reserve = arms->reserve;
		in->has_gun = arms->has_gun;
		in->has_melee = arms->has_melee;
		in->has_heal = bot && arms->has_heal; // a person's syringes are their own
	}
	in->medic = bot && cof_companion_medic && ( cof_companion_medic_slot < 0 || &svs.clients[cof_companion_medic_slot] == cl ) &&
		sv.time >= cof_companion_medic_after ? cof_companion_medic->value : 0.0f;
	if( leader && leader != cl && SV_IsValidEdict( leader->edict ))
	{
		edict_t *le = leader->edict;
		trace_t tr;
		in->leader = 1;
		VectorCopy( le->v.origin, in->leader_pos );
		VectorAdd( le->v.origin, le->v.view_ofs, in->leader_eye );
		VectorCopy( le->v.v_angle, in->leader_view );
		in->leader_alive = CoF_CompanionAlive( le );
		in->leader_health = (int)le->v.health;
		in->leader_down = !in->leader_alive && SV_CoF_RetailRevivable( leader );
		in->leader_held = in->leader_alive && CoF_CompanionHeld( leader );
		in->leader_visible = CoF_CompanionSees( in->eye, in->leader_eye, ent, le );
		if( in->leader_alive && fabs( le->v.origin[2] - ent->v.origin[2] ) < 40.0f )
		{
			vec3_t to;
			VectorCopy( le->v.origin, to );
			to[2] = ent->v.origin[2];
			tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, to, MOVE_NOMONSTERS, ent, false );
			in->leader_walkable = !tr.startsolid && tr.fraction >= 1.0f;
		}
	}
	in->stay = c->stay;
	VectorCopy( c->stay_pos, in->stay_pos );
	c->leader_held = in->leader_held;
	CoF_CompanionAhead( ent, c, in );
	CoF_CompanionNavigate( cl, c, in );
	CoF_CompanionProbe( ent, c, in );
	CoF_CompanionPartners( cl, leader, c, in );
	if(( threat = CoF_CompanionThreat( ent, in->eye, c->stay ? c->stay_pos : in->leader ? leader->edict->v.origin : NULL, in->threat_pos )) != NULL )
	{
		in->threat = NUM_FOR_EDICT( threat );
		in->threat_dist = (float)VectorDistance( in->eye, in->threat_pos );
		in->threat_gap = CoF_CompanionGap( in->eye, threat->v.absmin, threat->v.absmax );
		in->line_clear = CoF_CompanionFireClear( cl, in->eye, in->threat_pos );
	}
}

static void CoF_CompanionAct( sv_client_t *cl, sv_client_t *leader, cof_companion_t *c, const cof_retail_arms_t *arms )
{
	const char *weapon = NULL;
	switch( c->out.action )
	{
	case COF_BRAIN_ACT_TELEPORT:
		c->route_length = 0; // planned from where it stood
		if( !c->place_pending ) c->place_since = sv.time;
		c->place_pending = true;
		c->place_near = c->out.place;
		VectorCopy( c->out.place_at, c->place_at );
		c->place_why = c->out.why;
		break;
	case COF_BRAIN_ACT_OPEN_DOOR:
		// The game's own Use with the AI as the user, as a person's +use would, but
		// only on the door: the use key takes whatever the game finds first.
		if( c->door > 0 && c->door < svgame.numEntities && CoF_CompanionDoor( SV_EdictNum( c->door )))
		{
			svgame.dllFuncs.pfnUse( SV_EdictNum( c->door ), cl->edict );
			Con_Printf( "cofstory phase=companion-door userid=%d entity=%d\n", cl->userid, c->door );
		}
		break;
	case COF_BRAIN_ACT_EQUIP_GUN: weapon = arms->gun; break;
	case COF_BRAIN_ACT_EQUIP_MELEE: weapon = arms->melee; break;
	case COF_BRAIN_ACT_EQUIP_HEAL: weapon = arms->heal; break;
	case COF_BRAIN_ACT_REVIVE:
		if( !leader || sv.time < c->revive_after ) break;
		c->revive_after = sv.time + 1.0;
		if( SV_CoF_RetailRevive( cl, leader ))
			Con_Printf( "cofstory phase=companion-cpr userid=%d leader=%d\n", cl->userid, leader->userid );
		break;
	}
	if(( c->out.buttons & COF_IN_JUMP ) && c->out.mode != COF_BRAIN_DEAD )
		Con_Printf( "cofstory phase=companion-jump userid=%d reason=%s\n", cl->userid, c->out.why ? c->out.why : "?" );
	// A switch takes the game a second or more; asking again sooner is refused
	// anyway, and the game refuses some silently (mid-shot): this logs the asking.
	if( !weapon ) c->asked[0] = 0;
	else if( weapon[0] && sv.time >= c->equip_after )
	{
		c->equip_after = sv.time + 1.5;
		if( SV_CoF_RetailEquip( cl, weapon ) && Q_strcmp( c->asked, weapon ))
		{
			Q_strncpy( c->asked, weapon, sizeof( c->asked ));
			Con_Printf( "cofstory phase=companion-equip userid=%d weapon=%s\n", cl->userid, weapon );
		}
	}
	if( c->out.mode != c->mode )
	{
		// Say so when coming to help a downed leader (who sees only the ceiling): once,
		// from the first that sets off.
		if( c->out.mode == COF_BRAIN_REVIVE && leader && !FBitSet( leader->flags, FCL_FAKECLIENT ) && !CoF_CompanionAlive( leader->edict ) &&
			cof_companion_down_told != leader->userid )
		{
			char text[96];
			cof_companion_down_told = leader->userid;
			Q_snprintf( text, sizeof( text ), "%s is coming to help you up.", cl->name );
			SV_CoF_RetailProFont( leader, 4, text, false );
		}
		c->mode = c->out.mode;
		Con_Printf( "cofstory phase=companion-mode userid=%d mode=%s\n", cl->userid, c->mode >= 0 && c->mode < (int)ARRAYSIZE( cof_companion_modes ) ? cof_companion_modes[c->mode] : "?" );
	}
}

// A teleport the brain asked for: near where the game put the leader (a door exit)
// or behind them, always out of the player's sight; with no such spot it asks again
// every frame, and after a while takes the least seen one. Never during a cutscene.
static void CoF_CompanionPlace( sv_client_t *cl, sv_client_t *leader, cof_companion_t *c, int rank )
{
	edict_t *ent = cl->edict;
	vec3_t at;
	qboolean anywhere;
	if( !c->place_pending ) return;
	if( !leader || !SV_IsValidEdict( leader->edict ) || !CoF_CompanionAlive( ent ) || !CoF_CompanionAlive( leader->edict ))
	{
		c->place_pending = false;
		return;
	}
	if( c->leader_held ) return;
	anywhere = sv.time - c->place_since >= COF_COMPANION_PLACE_PATIENCE;
	VectorCopy( ent->v.origin, at );
	if( !SV_CoF_PartyPlaceUnseen( cl, leader->edict, c->place_near ? c->place_at : NULL, rank, anywhere )) return;
	c->place_pending = false;
	Con_Printf( "cofstory phase=companion-teleport userid=%d leader=%d reason=%s at=%.0f,%.0f,%.0f leader_at=%.0f,%.0f,%.0f to=%.0f,%.0f,%.0f%s\n",
		cl->userid, leader->userid, c->place_why ? c->place_why : "?", at[0], at[1], at[2],
		leader->edict->v.origin[0], leader->edict->v.origin[1], leader->edict->v.origin[2],
		ent->v.origin[0], ent->v.origin[1], ent->v.origin[2], anywhere ? " seen=maybe" : "" );
}

// What the AI looks after besides its decisions: standing up after a hard
// landing; and for a bot, its rounds, its syringe and its flashlight.
static void CoF_CompanionUpkeep( sv_client_t *cl, cof_companion_t *c, qboolean bot, const cof_retail_arms_t *arms, qboolean alive )
{
	edict_t *ent = cl->edict;
	if( !alive ) return;
	if( sv.time >= c->getup_after && SV_CoF_RetailFallen( cl ))
	{
		c->getup_after = sv.time + 1.0;
		SV_CoF_RetailCommand( cl, "tapgetup" );
	}
	if( !bot ) return; // a person's ammo, items and light are their own
	if( cof_companion_ammo && cof_companion_ammo->value ) SV_CoF_RetailSupply( cl );
	// Its own syringe for patching the leader up (never one of the map's), while
	// the inventory keeps room for the story's weapons.
	if( cof_companion_medic && cof_companion_medic->value > 0.0f && !arms->has_heal && sv.time >= c->give_after )
	{
		c->give_after = sv.time + 5.0;
		if( SV_CoF_RetailFreeSlots( cl ) >= 2 && SV_CoF_StoryGiveItem( cl, "weapon_syringe" ))
			Con_Printf( "cofstory phase=companion-syringe userid=%d\n", cl->userid );
	}
	// The co-op flashlight (frags 32, drawn by every client); a weapon light change resets it.
	if( cof_companion_light && cof_companion_light->value && (int)ent->v.frags != 32 && sv.time >= c->light_after )
	{
		c->light_after = sv.time + 2.0;
		SV_CoF_RetailCommand( cl, "flashlight" );
	}
}

// What pfnRunPlayerMove does for a bot of the game's own: one usercmd through the player code.
static void CoF_CompanionRun( sv_client_t *cl, const cof_brain_output_t *out )
{
	sv_client_t *previous = sv.current_client;
	byte msec = (byte)bound( 1, (int)( sv.frametime * 1000.0 ), 100 );
	usercmd_t cmd;
	memset( &cmd, 0, sizeof( cmd ));
	VectorCopy( out->angles, cmd.viewangles );
	cmd.forwardmove = out->forward;
	cmd.sidemove = out->side;
	cmd.upmove = out->up;
	cmd.buttons = (word)out->buttons;
	cmd.msec = msec;
	sv.current_client = cl;
	cl->timebase = ( sv.time + sv.frametime ) - ((double)msec / 1000.0 );
	cl->edict->v.fixangle = 0; // a bot has no client to take a set-angle
	SV_RunCmd( cl, &cmd, COM_RandomLong( 0, 0x7fffffff ));
	cl->lastcmd = cmd;
	sv.current_client = previous;
}

// How far a client's view is from every view the AI handed it lately: the
// client takes a set angle some frames late and echoes it in its usercmds
// (live 2026-10-12: read as a turned mouse, the AI let go within a second).
static float CoF_CompanionEcho( const cof_companion_t *c, const float *view )
{
	float best = fabsf( CoF_BrainNormalize( view[1] - c->out.angles[1] )) + fabsf( CoF_BrainNormalize( view[0] - c->out.angles[0] ));
	int i;
	for( i = 0; i < c->sent_count; i++ )
		best = Q_min( best, fabsf( CoF_BrainNormalize( view[1] - c->sent[i][1] )) + fabsf( CoF_BrainNormalize( view[0] - c->sent[i][0] )));
	return best;
}

void SV_CoF_CompanionUsercmd( sv_client_t *cl, usercmd_t *cmd )
{
	int slot = CoF_CompanionSlot( cl ), buttons;
	cof_companion_t *c;
	qboolean touched;
	if( slot < 0 || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_StoryCoopEnabled() ) return;
	c = &cof_companions[slot];
	if( c->userid != cl->userid ) { memset( c, 0, sizeof( *c )); c->userid = cl->userid; }
	// A person at these controls: moves, buttons, or a mouse turned away from
	// both their own last view and the one the AI last gave them.
	touched = cmd->forwardmove != 0.0f || cmd->sidemove != 0.0f || cmd->upmove != 0.0f || cmd->buttons != 0;
	if( c->have_view && !touched )
	{
		float own = fabsf( CoF_BrainNormalize( cmd->viewangles[1] - c->own_view[1] )) + fabsf( CoF_BrainNormalize( cmd->viewangles[0] - c->own_view[0] ));
		touched = own > 0.5f && CoF_CompanionEcho( c, cmd->viewangles ) > 1.0f;
	}
	VectorCopy( cmd->viewangles, c->own_view );
	c->have_view = true;
	if( touched )
	{
		if( c->driving ) Con_Printf( "cofstory phase=companion-released userid=%d\n", cl->userid );
		c->human_until = sv.time + ( cof_companion_idle ? cof_companion_idle->value : 8.0f );
		c->driving = false;
		c->taps = 0;
		c->tapped = false;
		c->sent_count = 0;
		return;
	}
	if( !c->driving ) return;
	// A client's usercmds do not keep step with server frames: a press the AI
	// made is handed over once, and released in the next usercmd.
	buttons = c->out.buttons & ~COF_BRAIN_TAPS;
	if( c->tapped ) c->tapped = false;
	else if( c->taps )
	{
		buttons |= c->taps;
		c->taps = 0;
		c->tapped = true;
	}
	VectorCopy( c->out.angles, cmd->viewangles );
	c->sent[c->sent_next][0] = c->out.angles[0];
	c->sent[c->sent_next][1] = c->out.angles[1];
	c->sent_next = ( c->sent_next + 1 ) % COF_COMPANION_ECHO;
	if( c->sent_count < COF_COMPANION_ECHO ) c->sent_count++;
	cmd->forwardmove = c->out.forward;
	cmd->sidemove = c->out.side;
	cmd->upmove = c->out.up;
	cmd->buttons = (word)buttons;
	// The client's own view follows the AI's (it would otherwise keep its own).
	// svc_setangle carries v.angles, pitch included (the HL fixangle convention,
	// as CoF_PartyMoveEdict does): that is the view the client echoes back.
	VectorCopy( c->out.angles, cl->edict->v.v_angle );
	VectorCopy( c->out.angles, cl->edict->v.angles );
	cl->edict->v.fixangle = 1;
}

static void CoF_CompanionKeepBots( sv_client_t *leader )
{
	int wanted = cof_companion ? (int)cof_companion->value : 0, bots = CoF_CompanionBots(), i;
	wanted = bound( 0, wanted, svs.maxclients - 1 );
	if( bots > wanted && CoF_CompanionPersonAlive())
	{
		for( i = svs.maxclients - 1; i >= 0 && i < MAX_CLIENTS; i-- )
			if( cof_companions[i].bot && FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ) && svs.clients[i].state >= cs_connected )
			{
				CoF_CompanionRemove( &svs.clients[i] );
				return;
			}
	}
	// Only beside a person who is in the world, one at a time.
	if( bots < wanted && leader && CoF_CompanionAlive( leader->edict ) && sv.time >= cof_companion_add_after )
	{
		cof_companion_add_after = sv.time + 1.0;
		CoF_CompanionAdd();
	}
}

void SV_CoF_CompanionFrame( void )
{
	sv_client_t *leader;
	int i, rank, behind;
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	if( cof_companion_generation != svs.spawncount )
	{
		// The engine dropped every bot at the level change; add them again
		// once the host is in the new map.
		memset( cof_companions, 0, sizeof( cof_companions ));
		cof_companion_generation = svs.spawncount;
		cof_companion_add_after = sv.time + 2.0;
		cof_companion_medic_after = 0.0;
		cof_companion_medic_slot = -1;
		cof_companion_down_told = 0;
		CoF_CompanionNavReset(); // another map, another graph
	}
	SV_CoF_RetailCprRepair(); // for any pair of players, not only companions
	leader = CoF_CompanionLeader();
	CoF_CompanionKeepBots( leader );
	if( !leader || CoF_CompanionAlive( leader->edict )) cof_companion_down_told = 0;
	if( cof_companion_medic_slot >= 0 && !( cof_companions[cof_companion_medic_slot].bot && svs.clients[cof_companion_medic_slot].state == cs_spawned ))
		cof_companion_medic_slot = -1;
	for( i = 0, rank = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i], *mine;
		cof_companion_t *c = &cof_companions[i];
		cof_brain_input_t in;
		cof_retail_arms_t arms;
		qboolean bot = c->bot && FBitSet( cl->flags, FCL_FAKECLIENT ) && cl->state == cs_spawned && c->userid == cl->userid;
		qboolean human = !FBitSet( cl->flags, FCL_FAKECLIENT ) && cl->state == cs_spawned && cl != leader &&
			cof_companion_drive && cof_companion_drive->value && c->userid == cl->userid && sv.time >= c->human_until && c->have_view;
		if( !SV_IsValidEdict( cl->edict ) || ( !bot && !human ))
		{
			c->driving = false;
			c->taps = 0;
			c->tapped = false;
			continue;
		}
		if( !c->driving && human ) Con_Printf( "cofstory phase=companion-driving userid=%d\n", cl->userid );
		c->driving = true;
		// a driven person's client keeps the first person; bots share the people out
		behind = 0;
		mine = bot ? CoF_CompanionLeaderFor( cl, rank++, &behind ) : leader;
		if(( mine ? mine->userid : 0 ) != c->leader_userid )
		{
			// another person: their trail starts now (the old one leads to someone else)
			c->leader_userid = mine ? mine->userid : 0;
			c->brain.count = c->brain.cursor = c->brain.have_sample = c->brain.broken = 0;
			c->brain.following = 0;
			c->route_length = 0;
		}
		CoF_CompanionPerceive( cl, mine, c, bot, behind, &in, &arms );
		CoF_BrainThink( &c->brain, &in, &c->out );
		if( bot )
		{
			// one syringe for the leader at a time, and the cooldown counts for the whole party
			if( c->out.mode == COF_BRAIN_MEDIC ) cof_companion_medic_slot = i;
			else if( cof_companion_medic_slot == i ) cof_companion_medic_slot = -1;
			if( c->brain.medic_after > cof_companion_medic_after ) cof_companion_medic_after = c->brain.medic_after;
		}
		CoF_CompanionAct( cl, mine, c, &arms );
		CoF_CompanionPlace( cl, mine, c, behind );
		CoF_CompanionUpkeep( cl, c, bot, &arms, in.alive );
		if( human ) c->taps |= c->out.buttons & COF_BRAIN_TAPS;
		if( in.threat || c->calm_since == 0.0 ) c->calm_since = sv.time;
		// A bot catches its breath out of a fight (a person's health is their own).
		if( bot && in.alive && cl->edict->v.health < 100.0f && sv.time - c->calm_since > 5.0 && cof_companion_regen )
			cl->edict->v.health = Q_min( 100.0f, cl->edict->v.health + cof_companion_regen->value * (float)sv.frametime );
		// A bot's usercmds run dead or alive: its body only falls, lands and gets up through them.
		if( bot ) CoF_CompanionRun( cl, &c->out );
	}
}

// Every companion waits where it stands, or follows again. Returns how many heard it.
static int CoF_CompanionOrder( int stay )
{
	int i, heard = 0;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		cof_companion_t *c = &cof_companions[i];
		sv_client_t *cl = &svs.clients[i];
		if( !c->userid || cl->userid != c->userid || !( c->bot || c->driving ) || !SV_IsValidEdict( cl->edict )) continue;
		c->stay = stay;
		VectorCopy( cl->edict->v.origin, c->stay_pos );
		CoF_BrainReset( &c->brain ); // a fresh trail from here
		Con_Printf( "cofstory phase=companion-order userid=%d order=%s\n", cl->userid, stay ? "wait" : "follow" );
		heard++;
	}
	return heard;
}

// cof_companion_order (bound to H): toggle wait / follow for every companion.
qboolean SV_CoF_CompanionClientCommand( sv_client_t *cl )
{
	char text[96];
	int i, stay = 1, heard;
	if( Q_strcmp( Cmd_Argv( 0 ), "cof_companion_order" ) || !SV_CoF_StoryCoopEnabled() ) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( cof_companions[i].stay && cof_companions[i].userid ) { stay = 0; break; }
	if( Cmd_Argc() > 1 ) stay = !Q_stricmp( Cmd_Argv( 1 ), "wait" );
	heard = CoF_CompanionOrder( stay );
	if( !heard ) Q_strncpy( text, "No companion to give orders to.", sizeof( text ));
	else if( heard == 1 ) Q_strncpy( text, stay ? "Your companion waits here." : "Your companion follows you.", sizeof( text ));
	else Q_strncpy( text, stay ? "Your companions wait here." : "Your companions follow you.", sizeof( text ));
	SV_CoF_RetailProFont( cl, 5, text, false );
	return true;
}

static void CoF_CompanionCommand_f( void )
{
	const char *what = Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "status";
	int i;
	if( !Q_stricmp( what, "add" ))
	{
		if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) { Con_Printf( "The companion needs a running story co-op game.\n" ); return; }
		Cvar_SetValue( "cof_companion", (float)bound( 0, ( cof_companion ? (int)cof_companion->value : 0 ) + 1, svs.maxclients - 1 ));
		cof_companion_add_after = 0.0;
		return;
	}
	if( !Q_stricmp( what, "remove" ))
	{
		Cvar_SetValue( "cof_companion", 0 );
		return;
	}
	if( !Q_stricmp( what, "wait" ) || !Q_stricmp( what, "follow" ))
	{
		CoF_CompanionOrder( !Q_stricmp( what, "wait" ));
		return;
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( cof_companions[i].userid && svs.clients[i].userid == cof_companions[i].userid && ( cof_companions[i].bot || cof_companions[i].driving ))
			Con_Printf( "companion slot=%d userid=%d name=%s %s mode=%s\n", i, svs.clients[i].userid, svs.clients[i].name,
				cof_companions[i].bot ? "bot" : "driven", cof_companion_modes[bound( 0, cof_companions[i].mode, (int)ARRAYSIZE( cof_companion_modes ) - 1 )] );
	Con_Printf( "Usage: cof_companion_cmd add | remove | wait | follow | status. cof_companion %d keeps that many companions.\n", cof_companion ? (int)cof_companion->value : 0 );
}

void SV_CoF_CompanionInit( void )
{
	cof_companion = Cvar_Get( "cof_companion", "0", 0, "story co-op: AI companions to keep beside a host playing alone (0 off)" );
	cof_companion_name = Cvar_Get( "cof_companion_name", "Companion", 0, "story co-op: the AI companion's name" );
	cof_companion_drive = Cvar_Get( "cof_companion_drive", "0", 0, "story co-op: the AI also drives a second player's idle client (solo testing)" );
	cof_companion_idle = Cvar_Get( "cof_companion_idle", "8", 0, "story co-op: seconds a second player's controls rest before the AI drives them" );
	cof_companion_regen = Cvar_Get( "cof_companion_regen", "3", 0, "story co-op: health a companion bot recovers each second out of a fight" );
	cof_companion_medic = Cvar_Get( "cof_companion_medic", "90", 0, "story co-op: seconds between the companion's syringes for a hurt leader (0 never)" );
	cof_companion_ammo = Cvar_Get( "cof_companion_ammo", "1", 0, "story co-op: a companion bot never runs out of rounds" );
	cof_companion_light = Cvar_Get( "cof_companion_light", "1", 0, "story co-op: a companion bot keeps its flashlight on" );
	Cmd_AddCommand( "cof_companion_cmd", CoF_CompanionCommand_f, "story co-op AI companion: add | remove | wait | follow | status" );
}
