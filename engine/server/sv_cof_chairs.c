/* Cry of Fear story co-op 1.3.4: everyone rides a wheelchair in the fight at the end of Ending 4 (c_ending4b).
 * GPL-3.0-or-later. Research: docs/research/2026-10-12-player-state-re.md (cof_wheelchairmode), the decompilation
 * (CoF_WheelchairPlayer.cpp, player.cpp StartWheelchairMode/StopWheelchairMode, client.cpp's wheelchair commands,
 * CoF_SickSimon.cpp).
 *
 * The game's wheelchair is per player: each player's m_pWheelchair (P+0x390), each chair's own rider (+0x384), the
 * chair steered by its rider's own commands and the Browning's aim taken from whoever sends it. The map seats only the
 * first player to touch its spawn brush (trigger_once -> multistart -> cof_wheelchairmode 'wmode'); a partner fought
 * on foot. Here:
 * - While a person rides a living chair and the map has not stopped the ride, every other person (not an AI
 *   companion), alive, on foot and free for half a second, is set down beside that chair, facing its way, and seated
 *   through the game's own CBasePlayer::StartWheelchairMode (100F5B30): a chair of their own where they stand, their
 *   inventory for the Browning, frozen, no target. A late joiner is seated the same way. An AI companion the map
 *   seated first is let out once a person rides.
 * - Their body is then set down on the first rider's: the game leaves a rider's body where they sat down (the story
 *   has no wheelchair_hide_spot), and bodies that overlap are made non-solid by the game's PreThink, so no parked body
 *   stands in another's chair's way. Riders' bodies are not drawn for the others (sv_cof_weaponmodel.c): the chair's
 *   model shows Simon in it.
 * - The map ends the ride for one player (cof_stopwheelchair 'stoooop' at the boss's death acts on "the first
 *   player"): a chair that carried someone last frame, still standing, alive and empty, ends it for everyone
 *   (StopWheelchairMode 100F5D50 on each rider, as the map does to its one), and nobody is seated again this map.
 * - The book Simons that pop up with a gun (monster_booksimon, frags != 0) fight the rider "the first player" was
 *   when they first aimed, and ignored the others: while one has none yet, the first player of its think is the
 *   living rider whose chair is nearest (sv_cof_rescue.c asks SV_CoF_ChairsSimonTarget).
 * - A chair does not save its rider (CWheelchair's save fields end at m_bDead), so a checkpoint taken during the ride
 *   restores frozen players with no chair to drive: checkpoints wait while anyone rides (sv_cof_session.inc). Before
 *   a team wipe loads the checkpoint from before the level, every rider is let out (SV_CoF_ChairsReleaseAll).
 * cof_coop_wheelchairs 0: only the first, as the game does. */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_party.h"
#include "sv_cof_scene.h"
#include "sv_cof_chairs.h"
#include "sv_cof_chairs_state.h"

#define COF_CHAIRS_PL_SIZE 0x281C
#define COF_CHAIRS_SIZE 0x388 // CWheelchair : CBaseMonster (factory 10018800)
#define COF_CHAIRS_DEAD 0x368 // bool m_bDead
#define COF_CHAIRS_RIDER 0x384 // raw CBasePlayer *m_pPlayer
#define COF_CHAIRS_START 0xF5B30 // CBasePlayer::StartWheelchairMode, thiscall, no arguments
#define COF_CHAIRS_STOP 0xF5D50 // CBasePlayer::StopWheelchairMode, thiscall, no arguments
#define COF_CHAIRS_SIMON_SIZE 0x390 // CBookSimon
#define COF_CHAIRS_SIMON_PLAYER 0x38C // raw CBasePlayer *pPlayer: the one it fights, set once (1006F4EA)
#define COF_CHAIRS_MAX 32
#define COF_CHAIRS_TOLD 16

static CVAR_DEFINE_AUTO( cof_coop_wheelchairs, "1", 0, "Cry of Fear story co-op: everyone rides a wheelchair in the fight at the end of Ending 4 (c_ending4b); 0: only the first player in, as the game does" );

static int cof_chairs_generation = -1;
static qboolean cof_chairs_over; // the map stopped the ride: nobody rides again this map
static struct { int index, serial; } cof_chairs_carried[COF_CHAIRS_MAX]; // chairs carrying someone last frame
static int cof_chairs_carried_count;
static struct { int userid; double free_since, retry_after; qboolean told, refused; } cof_chairs_people[MAX_CLIENTS];
static struct { int index, serial; } cof_chairs_simons[COF_CHAIRS_TOLD]; // book Simons whose rider was logged
static int cof_chairs_simon_next;

void SV_CoF_ChairsInit( void )
{
	Cvar_RegisterVariable( &cof_coop_wheelchairs );
}

void SV_CoF_ChairsReset( void )
{
	cof_chairs_generation = -1;
	cof_chairs_over = false;
	memset( cof_chairs_carried, 0, sizeof( cof_chairs_carried ));
	cof_chairs_carried_count = 0;
	memset( cof_chairs_people, 0, sizeof( cof_chairs_people ));
	memset( cof_chairs_simons, 0, sizeof( cof_chairs_simons ));
	cof_chairs_simon_next = 0;
}

static qboolean CoF_ChairsIs( const edict_t *ent )
{
	return SV_IsValidEdict( ent ) && !( ent->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( ent->v.classname ), "wheelchair" );
}

static qboolean CoF_ChairsAny( void )
{
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
		if( CoF_ChairsIs( SV_EdictNum( i ))) return true;
	return false;
}

static uint32_t CoF_ChairsRider( edict_t *chair, byte **pv_out )
{
	byte *pv = SV_CoF_StoryEntityPrivate( chair, COF_CHAIRS_SIZE );
	uint32_t rider = 0;
	if( pv ) memcpy( &rider, pv + COF_CHAIRS_RIDER, sizeof( rider ));
	if( pv_out ) *pv_out = pv;
	return rider;
}

// The chair cl rides (its rider is cl's player data), or NULL.
static edict_t *CoF_ChairsOf( sv_client_t *cl, qboolean *dead )
{
	int i;
	uint32_t me;
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !cl->edict->pvPrivateData ) return NULL;
	me = (uint32_t)(uintptr_t)cl->edict->pvPrivateData;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *chair = SV_EdictNum( i );
		byte *pv;
		if( !CoF_ChairsIs( chair ) || CoF_ChairsRider( chair, &pv ) != me || !pv ) continue;
		if( dead ) *dead = pv[COF_CHAIRS_DEAD] != 0;
		return chair;
	}
	return NULL;
}

static qboolean CoF_ChairsAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO;
}

// The game's StopWheelchairMode on cl: the chair's Disembark lets them out where it stands.
static qboolean CoF_ChairsLetOut( sv_client_t *cl, const char *reason )
{
	byte *pv = SV_CoF_StoryEntityPrivate( cl->edict, COF_CHAIRS_PL_SIZE );
	if( !pv || !SV_CoF_StoryCallMember0( COF_CHAIRS_STOP, pv )) return false;
	Con_Printf( "cofstory phase=wheelchair-out userid=%d reason=%s\n", cl->userid, reason );
	return true;
}

qboolean SV_CoF_ChairsRiding( void )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() ) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && SV_IsValidEdict( svs.clients[i].edict ) && SV_CoF_RetailInWheelchair( &svs.clients[i] ))
			return true;
	return false;
}

void SV_CoF_ChairsReleaseAll( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		qboolean dead = false;
		if( CoF_ChairsOf( cl, &dead ) && !dead ) CoF_ChairsLetOut( cl, "restart" );
	}
}

// A chair that carried someone last frame and stands empty now: the map ended the ride.
static void CoF_ChairsWatch( void )
{
	int k, i;
	for( k = 0; k < cof_chairs_carried_count; k++ )
	{
		edict_t *chair = SV_EdictNum( cof_chairs_carried[k].index );
		byte *pv = NULL;
		qboolean same = SV_IsValidEdict( chair ) && chair->serialnumber == cof_chairs_carried[k].serial;
		qboolean removed = !same || !CoF_ChairsIs( chair );
		uint32_t carries = removed ? 0 : CoF_ChairsRider( chair, &pv );
		if( !CoF_ChairsStopped( true, same, removed || !pv, pv && pv[COF_CHAIRS_DEAD], carries != 0 )) continue;
		if( !cof_chairs_over ) Con_Printf( "cofstory phase=wheelchair-ride-over chair=%d\n", cof_chairs_carried[k].index );
		cof_chairs_over = true;
	}
	if( !cof_chairs_over ) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		qboolean dead = false;
		if( CoF_ChairsOf( cl, &dead ) && !dead ) CoF_ChairsLetOut( cl, "ride-over" );
	}
}

static void CoF_ChairsRemember( void )
{
	int i;
	cof_chairs_carried_count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_chairs_carried_count < COF_CHAIRS_MAX; i++ )
	{
		edict_t *chair = SV_EdictNum( i );
		byte *pv;
		if( !CoF_ChairsIs( chair ) || !CoF_ChairsRider( chair, &pv ) || !pv || pv[COF_CHAIRS_DEAD] ) continue;
		cof_chairs_carried[cof_chairs_carried_count].index = i;
		cof_chairs_carried[cof_chairs_carried_count].serial = chair->serialnumber;
		cof_chairs_carried_count++;
	}
}

// cl into a wheelchair of their own beside anchor's, facing its way; their body then on anchor's.
static void CoF_ChairsSeatOne( sv_client_t *cl, sv_client_t *anchor, edict_t *anchor_chair, int slot )
{
	edict_t *ent = cl->edict, *mine;
	byte *pv = SV_CoF_StoryEntityPrivate( ent, COF_CHAIRS_PL_SIZE );
	vec3_t park, facing = { 0, 0, 0 };
	cof_chairs_people[slot].retry_after = sv.time + COF_CHAIRS_RETRY;
	// nobody is moved when the game's member cannot be called (no verified DLL at its load address)
	if( !pv || !SV_CoF_StoryBase() || cof_chairs_people[slot].refused ) return;
	VectorCopy( anchor->edict->v.origin, park ); // where the game left the first rider's body
	if( !SV_CoF_PartyMoveBeside( cl, anchor ))
	{
		if( !cof_chairs_people[slot].told ) Con_Printf( "cofstory phase=wheelchair-seat-failed userid=%d reason=no-room\n", cl->userid );
		cof_chairs_people[slot].told = true;
		return;
	}
	facing[YAW] = anchor_chair->v.angles[YAW];
	VectorCopy( facing, ent->v.angles );
	VectorCopy( facing, ent->v.v_angle );
	ent->v.fixangle = 1;
	// CWheelchair::Spawn finds the floor along -gpGlobals->v_up, whatever the game last made vectors from (a pitched
	// view would set the chair down on the side, or on the chair beside it): straight down, as the map's own chair is
	// set down level (its wmode moves it to wheelchstart)
	if( svgame.globals ) AngleVectors( facing, svgame.globals->v_forward, svgame.globals->v_right, svgame.globals->v_up );
	if( !SV_CoF_StoryCallMember0( COF_CHAIRS_START, pv ) || !( mine = CoF_ChairsOf( cl, NULL )))
	{
		// the game would not seat them: not again this map (each try moves them beside the chair)
		Con_Printf( "cofstory phase=wheelchair-seat-failed userid=%d reason=start\n", cl->userid );
		cof_chairs_people[slot].refused = true;
		return;
	}
	SV_CoF_PartyPlace( cl, park );
	cof_chairs_people[slot].told = false;
	Con_Printf( "cofstory phase=wheelchair-seated userid=%d chair=%d beside=%d\n", cl->userid, NUM_FOR_EDICT( mine ), anchor->userid );
}

static void CoF_ChairsSeatAll( void )
{
	sv_client_t *anchor = NULL;
	edict_t *anchor_chair = NULL;
	qboolean scene = SV_CoF_SceneActive() || SV_CoF_TransitionActive(), person = false;
	int i;
	// the ride is on: someone rides a living chair (a person first; an AI companion the map seated, until one does)
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS && !person; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		qboolean dead = false;
		edict_t *chair;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !CoF_ChairsAlive( cl->edict )) continue;
		if( !( chair = CoF_ChairsOf( cl, &dead )) || dead || ( anchor && FBitSet( cl->flags, FCL_FAKECLIENT ))) continue;
		anchor = cl;
		anchor_chair = chair;
		person = !FBitSet( cl->flags, FCL_FAKECLIENT );
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *ent = cl->edict;
		cof_chairs_person_t p;
		qboolean dead = false;
		memset( &p, 0, sizeof( p ));
		if( cof_chairs_people[i].userid != cl->userid )
		{
			memset( &cof_chairs_people[i], 0, sizeof( cof_chairs_people[i] ));
			cof_chairs_people[i].userid = cl->userid;
		}
		p.spawned = cl->state == cs_spawned && SV_IsValidEdict( ent );
		if( p.spawned )
		{
			p.fake = FBitSet( cl->flags, FCL_FAKECLIENT ) != 0;
			p.alive = CoF_ChairsAlive( ent );
			p.riding = CoF_ChairsOf( cl, &dead ) != NULL || SV_CoF_RetailInWheelchair( cl );
			p.frozen = FBitSet( ent->v.flags, FL_FROZEN ) != 0 || ent->v.movetype != MOVETYPE_WALK;
			p.camera = cl->pViewEntity && cl->pViewEntity != ent;
			p.busy = SV_CoF_RetailInBoat( cl ) || SV_CoF_RetailInPhoneCall( cl ) || SV_CoF_PartyInDoor( cl );
			p.moving = scene;
			// an AI companion the map seated first: out once a person rides
			if( p.fake && person && p.riding && !dead ) CoF_ChairsLetOut( cl, "companion" );
		}
		if( CoF_ChairsSeat( anchor != NULL && !cof_chairs_over, &p, &cof_chairs_people[i].free_since, sv.time, cof_chairs_people[i].retry_after ))
			CoF_ChairsSeatOne( cl, anchor, anchor_chair, i );
	}
}

void SV_CoF_ChairsFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	if( cof_chairs_generation != svs.spawncount )
	{
		SV_CoF_ChairsReset();
		cof_chairs_generation = svs.spawncount;
	}
	if( !cof_chairs_carried_count && !CoF_ChairsAny( )) return; // every other map: nothing to look after
	CoF_ChairsWatch(); // first: last frame's riders against now
	if( cof_coop_wheelchairs.value != 0.0f && !cof_chairs_over ) CoF_ChairsSeatAll();
	CoF_ChairsRemember(); // last: what this frame let out on purpose is not carried any more
}

edict_t *SV_CoF_ChairsSimonTarget( edict_t *simon )
{
	float spots[MAX_CLIENTS][3];
	int usable[MAX_CLIENTS], n = 0, i, k, best;
	sv_client_t *who[MAX_CLIENTS];
	byte *pv;
	uint32_t fights;
	if( !simon || simon->v.frags == 0.0f || !( pv = SV_CoF_StoryEntityPrivate( simon, COF_CHAIRS_SIMON_SIZE ))) return NULL;
	memcpy( &fights, pv + COF_CHAIRS_SIMON_PLAYER, sizeof( fights ));
	if( fights ) return NULL; // it keeps the one it took
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		qboolean dead = false;
		edict_t *chair = CoF_ChairsOf( cl, &dead );
		if( !chair || dead || !CoF_ChairsAlive( cl->edict )) continue;
		VectorCopy( chair->v.origin, spots[n] );
		usable[n] = 1;
		who[n++] = cl;
	}
	if(( best = CoF_ChairsNearest( (const float (*)[3])spots, usable, n, simon->v.origin )) < 0 ) return NULL;
	for( k = 0; k < COF_CHAIRS_TOLD; k++ )
		if( cof_chairs_simons[k].index == NUM_FOR_EDICT( simon ) && cof_chairs_simons[k].serial == simon->serialnumber ) break;
	if( k == COF_CHAIRS_TOLD && n > 1 )
	{
		cof_chairs_simons[cof_chairs_simon_next].index = NUM_FOR_EDICT( simon );
		cof_chairs_simons[cof_chairs_simon_next].serial = simon->serialnumber;
		cof_chairs_simon_next = ( cof_chairs_simon_next + 1 ) % COF_CHAIRS_TOLD;
		Con_Printf( "cofstory phase=boss-target class=monster_booksimon userid=%d reason=nearest-chair\n", who[best]->userid );
	}
	return who[best]->edict;
}
