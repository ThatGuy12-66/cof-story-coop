/* Cry of Fear story co-op: story moments the retail game gets wrong with two
 * players. GPL-3.0-or-later. Research: docs/research/2026-10-12-story-moments-re.md.
 *
 * - Fall-death floors: a trigger_once that fires a cof_killplayer kills EVERY
 *   player (100A42C0) when one falls. Only the player who fell dies, through
 *   the game's own ClientKill (1001CEF0); the floor stays armed.
 * - Phone calls (cof_phonecall): the call rings its activator only and is
 *   dropped for good when that player has no phone, dies or puts it away
 *   (WaitForCall 10061000, CallThink 1005FC20, EndCall 1005FF90). The call
 *   goes to a player who can take it; a dropped call starts again; with
 *   nobody able to take it for 30 s its target fires once. A player who died
 *   in a call no longer keeps m_bInPhoneCall, which blocks all their commands.
 * - Ladders (cof_ladder_manager Use 1004FB50 keeps one raw climber pointer and
 *   no busy check): nobody can start a ladder someone is climbing.
 * - The wheelchair (c_ending4b): the rider is "the first player", so the stop
 *   and the teleport after the boss (both first-player) find them.
 * - A player who leaves: no ladder or wheelchair keeps a pointer to them.
 * - Yes/no prompts (cof_yesno): only a player who was asked answers; the
 *   first answer wins (c_roofs2 asks both players).
 * - The intro's logos and credits (cof_logo, c_intro): shown to the activator
 *   only (Use 100A7370, the only CoFLogo sender); everyone else sees each one a
 *   frame later. */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_moments.h"

#define COF_THINK 0x78 // CBaseEntity::m_pfnThink

#define COF_CALL_SIZE 0x204
#define COF_CALL_RANG 0xDC // byte: the ring has started
#define COF_CALL_PLAYER 0xE0 // EHANDLE m_pPlayer: who it rings
#define COF_CALL_USER 0x1F8 // EHANDLE m_hPhoneUser: who is talking
#define COF_CALL_ANSWERED 0x200 // byte m_bAnswered
#define COF_CALL_WAIT 0x61000 // RVA WaitForCall
#define COF_CALL_TALK 0x5FC20 // RVA CallThink
#define COF_CALL_END 0x5FF90 // RVA EndCall
#define COF_CALL_GIVE_UP 30.0
#define COF_CALL_RETRY 3.0

#define COF_PL_SIZE 0x281C
#define COF_PL_CALL_UI 0x1EA0 // dword: 3 = incoming-call UI
#define COF_PL_BARS 0x1EB4 // dword: black bars
#define COF_PL_IN_CALL 0x1EE8 // byte m_bInPhoneCall: ClientCommand ignores the player (100190BE)
#define COF_PL_HIDEHUD 0x2500 // dword m_iHideHUD
#define COF_PL_SUICIDE 0x22E8 // float m_fNextSuicideTime (ClientKill)
#define COF_PL_DEATHBITS 0x5B4 // dword m_iDeathBits (cof_killplayer)
#define COF_PL_CAMERA_MODEL 0x5AC // raw CBaseEntity*: the player's camera_model (ladder, strangle, model cutscene)
#define COF_PL_CAMERA_HANDLE 0x388 // EHANDLE to the same model

#define COF_LADDER_SIZE 0x1B4
#define COF_LADDER_CLIMBER 0x114 // raw CBasePlayer*
#define COF_LADDER_REACH 96.0f
#define COF_CHAIR_SIZE 0x388
#define COF_CHAIR_RIDER 0x384 // raw CBasePlayer*

#define COF_MOMENT_FLOORS 8
#define COF_MOMENT_LADDERS 16
#define COF_MOMENT_CALLS 8
#define COF_MOMENT_PROMPTS 16
#define COF_MOMENT_LOGOS 8

static const char cof_moment_phone[] = "inventoryitems/weapons/weapon_mobile.txt";

typedef struct
{
	edict_t *edict;
	int serial;
} cof_moment_handle_t; // the retail EHANDLE on this 32-bit build

static int cof_moment_generation = -1;
static struct { int index, serial, deathbits; } cof_moment_floors[COF_MOMENT_FLOORS];
static int cof_moment_floor_count;
static struct { vec3_t mins, maxs; int climber; } cof_moment_ladders[COF_MOMENT_LADDERS];
static int cof_moment_ladder_count;
static double cof_moment_ladder_told[MAX_CLIENTS];
// next_restart: a dropped call starts again at most this often (the game may
// drop it at once if it disagrees with us about the phone); told: the last
// player announced as taking it, so the messages go out once per callee.
static struct { int index, serial; double dropped_at, next_restart; qboolean fired; edict_t *told; } cof_moment_calls[COF_MOMENT_CALLS];
// A yes/no entity asked a player (userid), or was answered (answered set);
// closed: answered "yes", which no later asking reopens (c_roofs2 asks every 5 s).
static struct { int index, serial, userid; qboolean answered, closed; } cof_moment_prompts[COF_MOMENT_PROMPTS];
// CoFLogo messages the game sent to one player, to send the others next frame
// (the message is still being built when the engine sees it).
static struct { int from; byte logo; } cof_moment_logos[COF_MOMENT_LOGOS];
static int cof_moment_logo_count;
static qboolean cof_moment_mirroring; // our own copies are not copied again

void SV_CoF_MomentsReset( void )
{
	cof_moment_generation = -1;
	cof_moment_floor_count = cof_moment_ladder_count = 0;
	memset( cof_moment_floors, 0, sizeof( cof_moment_floors ));
	memset( cof_moment_ladders, 0, sizeof( cof_moment_ladders ));
	memset( cof_moment_ladder_told, 0, sizeof( cof_moment_ladder_told ));
	memset( cof_moment_calls, 0, sizeof( cof_moment_calls ));
	memset( cof_moment_prompts, 0, sizeof( cof_moment_prompts ));
	memset( cof_moment_logos, 0, sizeof( cof_moment_logos ));
	cof_moment_logo_count = 0;
	SV_CoF_PreferFirst( NULL );
}

static qboolean CoF_MomentClass( const edict_t *ent, const char *classname )
{
	return SV_IsValidEdict( ent ) && !( ent->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( ent->v.classname ), classname );
}

static sv_client_t *CoF_MomentClient( const edict_t *ent )
{
	int index;
	if( !ent || !svgame.edicts ) return NULL;
	index = (int)( ent - svgame.edicts );
	if( index < 1 || index > svs.maxclients || index > MAX_CLIENTS ) return NULL;
	if( svs.clients[index - 1].state != cs_spawned || svs.clients[index - 1].edict != ent ) return NULL;
	return &svs.clients[index - 1];
}

static qboolean CoF_MomentAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO;
}

static edict_t *CoF_MomentHandle( const byte *pv, int offset )
{
	cof_moment_handle_t h;
	memcpy( &h, pv + offset, sizeof( h ));
	if( !h.edict || h.edict < svgame.edicts || h.edict >= svgame.edicts + svgame.numEntities ) return NULL;
	if( !SV_IsValidEdict( h.edict ) || h.edict->serialnumber != h.serial ) return NULL;
	return h.edict;
}

static void CoF_MomentSetHandle( byte *pv, int offset, edict_t *ent )
{
	cof_moment_handle_t h;
	h.edict = ent;
	h.serial = ent ? ent->serialnumber : 0;
	memcpy( pv + offset, &h, sizeof( h ));
}

// Fall-death floors: trigger_once entities whose target is a cof_killplayer.
// The doctor's execution in c_attic reaches its cof_killplayer through
// multi_managers and is not one of them.
static void CoF_MomentCheckGeneration( void )
{
	int i, j;
	if( cof_moment_generation == svs.spawncount ) return;
	SV_CoF_MomentsReset();
	cof_moment_generation = svs.spawncount;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *floor = SV_EdictNum( i );
		const char *target;
		if( !CoF_MomentClass( floor, "trigger_once" )) continue;
		target = SV_GetString( floor->v.target );
		if( !target[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities && cof_moment_floor_count < COF_MOMENT_FLOORS; j++ )
		{
			edict_t *kill = SV_EdictNum( j );
			if( !CoF_MomentClass( kill, "cof_killplayer" ) || Q_strcmp( SV_GetString( kill->v.targetname ), target )) continue;
			cof_moment_floors[cof_moment_floor_count].index = i;
			cof_moment_floors[cof_moment_floor_count].serial = floor->serialnumber;
			cof_moment_floors[cof_moment_floor_count].deathbits = kill->v.spawnflags & 0x3F;
			cof_moment_floor_count++;
			Con_Printf( "cofstory phase=fall-floor trigger=%d target=%s\n", i, target );
			break;
		}
	}
}

// Retail MP PreThink makes a player SOLID_NOT while its box overlaps another
// player's (docs/research/2026-09-30-hl-exits-coop-re.md), and the engine skips
// trigger touches for SOLID_NOT bodies. A party sharing one spot (c_intro's last
// room, kunitei, fits only one) then never touches the exit it stands in: with an
// idle AI companion on the player, never at all (live 2026-10-15). A living player
// that is non-solid only because it is stacked on a living partner still touches.
qboolean SV_CoF_MomentsStackedTouch( const edict_t *ent )
{
	int i;
	if( !ent || !svgame.edicts || ent->v.solid != SOLID_NOT || sv.state != ss_active || !SV_CoF_StoryCoopEnabled() ) return false;
	if( !CoF_MomentClient( ent ) || !CoF_MomentAlive( ent )) return false;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		const edict_t *other = svs.clients[i].edict;
		if( other == ent || !CoF_MomentClient( other ) || !CoF_MomentAlive( other )) continue;
		if( ent->v.absmin[0] < other->v.absmax[0] && other->v.absmin[0] < ent->v.absmax[0] &&
			ent->v.absmin[1] < other->v.absmax[1] && other->v.absmin[1] < ent->v.absmax[1] &&
			ent->v.absmin[2] < other->v.absmax[2] && other->v.absmin[2] < ent->v.absmax[2] )
			return true;
	}
	return false;
}

qboolean SV_CoF_MomentsTriggerTouch( edict_t *trigger, edict_t *ent )
{
	sv_client_t *cl;
	byte *pv;
	int i, index, bits;
	float never = 0.0f;
	if( !trigger || !ent || !svgame.edicts ) return false;
	index = (int)( ent - svgame.edicts );
	if( index < 1 || index > svs.maxclients || sv.state != ss_active || !SV_CoF_StoryCoopEnabled() ) return false;
	// A trigger switched off (iuser1, cof_triggeron 100A7F00) refuses every touch itself.
	if( trigger->v.iuser1 ) return false;
	CoF_MomentCheckGeneration();
	index = (int)( trigger - svgame.edicts );
	for( i = 0; i < cof_moment_floor_count; i++ )
		if( cof_moment_floors[i].index == index && cof_moment_floors[i].serial == trigger->serialnumber ) break;
	if( i == cof_moment_floor_count ) return false;
	// The floor is the partner's too: whoever touches it is handled here, alive or not.
	if( !( cl = CoF_MomentClient( ent )) || !CoF_MomentAlive( ent ) || !svgame.dllFuncs.pfnClientKill ) return true;
	if(( pv = SV_CoF_StoryEntityPrivate( ent, COF_PL_SIZE )) != NULL )
	{
		memcpy( &bits, pv + COF_PL_DEATHBITS, sizeof( bits ));
		bits |= cof_moment_floors[i].deathbits; // the same death presentation the floor gives
		memcpy( pv + COF_PL_DEATHBITS, &bits, sizeof( bits ));
		memcpy( pv + COF_PL_SUICIDE, &never, sizeof( never ));
	}
	svgame.dllFuncs.pfnClientKill( ent );
	Con_Printf( "cofstory phase=fall-death userid=%d trigger=%d\n", cl->userid, index );
	return true;
}

// The three fields a call leaves on its player, cleared as EndCall does.
static void CoF_MomentClearCall( edict_t *player )
{
	byte *pv;
	int value;
	if( !player || !( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE ))) return;
	if( !pv[COF_PL_IN_CALL] ) return;
	pv[COF_PL_IN_CALL] = 0;
	value = 0; memcpy( pv + COF_PL_BARS, &value, sizeof( value ));
	memcpy( &value, pv + COF_PL_HIDEHUD, sizeof( value )); value &= ~8; memcpy( pv + COF_PL_HIDEHUD, &value, sizeof( value ));
	memcpy( &value, pv + COF_PL_CALL_UI, sizeof( value ));
	if( value == 3 ) { value = 0; memcpy( pv + COF_PL_CALL_UI, &value, sizeof( value )); }
	player->v.fuser3 = 0.0f;
	Con_Printf( "cofstory phase=call-flags-cleared entity=%d\n", NUM_FOR_EDICT( player ));
}

// What WaitForCall puts on the player it rings before they answer (call UI,
// black bars, hidden HUD, fuser3 5), for a ringer the call moved away from.
static void CoF_MomentClearRing( edict_t *player )
{
	sv_client_t *cl = CoF_MomentClient( player );
	byte *pv;
	int value;
	if( !cl || !( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE )) || pv[COF_PL_IN_CALL] ) return;
	memcpy( &value, pv + COF_PL_CALL_UI, sizeof( value ));
	if( value == 3 ) { value = 0; memcpy( pv + COF_PL_CALL_UI, &value, sizeof( value )); }
	if( player->v.fuser3 == 5.0f )
	{
		player->v.fuser3 = 0.0f;
		// Bars and the hidden HUD were the ring's, unless a camera holds them.
		if( !cl->pViewEntity || cl->pViewEntity == player )
		{
			value = 0; memcpy( pv + COF_PL_BARS, &value, sizeof( value ));
			memcpy( &value, pv + COF_PL_HIDEHUD, sizeof( value )); value &= ~8; memcpy( pv + COF_PL_HIDEHUD, &value, sizeof( value ));
		}
	}
}

static qboolean CoF_MomentHasPhone( sv_client_t *cl )
{
	char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM];
	int i;
	if( !SV_CoF_RetailReadSlots( cl, slots )) return false;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ )
		if( !Q_stricmp( slots[i], cof_moment_phone )) return true;
	return false;
}

static qboolean CoF_MomentCanAnswer( edict_t *player )
{
	sv_client_t *cl = CoF_MomentClient( player );
	byte *pv;
	if( !cl || FBitSet( cl->flags, FCL_FAKECLIENT ) || !CoF_MomentAlive( player )) return false;
	if( !( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE )) || pv[COF_PL_IN_CALL] ) return false;
	return CoF_MomentHasPhone( cl );
}

// Who should take a call: the one it was for, or the nearest partner who can.
static edict_t *CoF_MomentCallee( edict_t *previous )
{
	edict_t *best = NULL;
	float best_distance = 0.0f;
	int i;
	if( previous && CoF_MomentCanAnswer( previous )) return previous;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		edict_t *player = svs.clients[i].edict;
		vec3_t delta;
		float distance = 0.0f;
		if( !CoF_MomentCanAnswer( player )) continue;
		if( previous && SV_IsValidEdict( previous ))
		{
			VectorSubtract( player->v.origin, previous->v.origin, delta );
			distance = DotProduct( delta, delta );
		}
		if( !best || distance < best_distance ) { best = player; best_distance = distance; }
	}
	return best;
}

static void CoF_MomentTellCall( int slot, edict_t *callee )
{
	sv_client_t *cl = CoF_MomentClient( callee );
	char text[96];
	int i;
	if( !cl || cof_moment_calls[slot].told == callee ) return;
	cof_moment_calls[slot].told = callee;
	SV_CoF_RetailProFont( cl, 5, "Your phone is ringing. Take it out to answer.", true );
	Q_snprintf( text, sizeof( text ), "%s is taking the call.", cl->name );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( &svs.clients[i] != cl && svs.clients[i].state == cs_spawned && !FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ))
			SV_CoF_RetailProFont( &svs.clients[i], 5, text, false );
}

static void CoF_MomentCalls( void )
{
	uintptr_t base = SV_CoF_StoryBase();
	int i, k;
	if( !base ) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *call = SV_EdictNum( i ), *ringing, *callee;
		byte *pv;
		uint32_t think;
		int slot = -1;
		if( !CoF_MomentClass( call, "cof_phonecall" ) || !( pv = SV_CoF_StoryEntityPrivate( call, COF_CALL_SIZE ))) continue;
		memcpy( &think, pv + COF_THINK, sizeof( think ));
		for( k = 0; k < COF_MOMENT_CALLS; k++ )
		{
			if( cof_moment_calls[k].index == i && cof_moment_calls[k].serial == call->serialnumber ) { slot = k; break; }
			if( slot < 0 && !cof_moment_calls[k].index ) slot = k;
		}
		if( think != base + COF_CALL_WAIT && think != base + COF_CALL_TALK && think != base + COF_CALL_END )
		{
			// Never started, or finished (EndCall clears the think).
			if( slot >= 0 && cof_moment_calls[slot].index == i ) memset( &cof_moment_calls[slot], 0, sizeof( cof_moment_calls[slot] ));
			continue;
		}
		if( slot < 0 ) continue;
		if( cof_moment_calls[slot].index != i )
		{
			memset( &cof_moment_calls[slot], 0, sizeof( cof_moment_calls[slot] ));
			cof_moment_calls[slot].index = i;
			cof_moment_calls[slot].serial = call->serialnumber;
		}
		ringing = CoF_MomentHandle( pv, COF_CALL_PLAYER );
		if( call->v.nextthink > 0.0f )
		{
			cof_moment_calls[slot].dropped_at = 0.0;
			// Still ringing for someone who cannot take it: ring a player who can.
			if( think == base + COF_CALL_WAIT && !CoF_MomentCanAnswer( ringing ) && ( callee = CoF_MomentCallee( ringing )) != NULL )
			{
				CoF_MomentClearRing( ringing );
				CoF_MomentSetHandle( pv, COF_CALL_PLAYER, callee );
				pv[COF_CALL_RANG] = 0; // the new callee hears it ring
				CoF_MomentTellCall( slot, callee );
				Con_Printf( "cofstory phase=call-retargeted call=%s entity=%d\n", SV_GetString( call->v.targetname ), NUM_FOR_EDICT( callee ));
			}
			continue;
		}
		// The game dropped it: its think is kept but nothing is scheduled.
		if( !cof_moment_calls[slot].dropped_at )
		{
			cof_moment_calls[slot].dropped_at = sv.time;
			Con_Printf( "cofstory phase=call-dropped call=%s\n", SV_GetString( call->v.targetname ));
		}
		CoF_MomentClearCall( CoF_MomentHandle( pv, COF_CALL_USER ));
		CoF_MomentClearCall( ringing );
		CoF_MomentClearRing( ringing );
		if( sv.time < cof_moment_calls[slot].next_restart ) continue;
		if(( callee = CoF_MomentCallee( ringing )) != NULL )
		{
			uint32_t wait = (uint32_t)( base + COF_CALL_WAIT );
			CoF_MomentSetHandle( pv, COF_CALL_PLAYER, callee );
			pv[COF_CALL_ANSWERED] = 0;
			pv[COF_CALL_RANG] = 0; // ring again
			memcpy( pv + COF_THINK, &wait, sizeof( wait ));
			call->v.nextthink = (float)sv.time + 0.1f;
			cof_moment_calls[slot].dropped_at = 0.0;
			cof_moment_calls[slot].next_restart = sv.time + COF_CALL_RETRY;
			CoF_MomentTellCall( slot, callee );
			Con_Printf( "cofstory phase=call-restarted call=%s entity=%d\n", SV_GetString( call->v.targetname ), NUM_FOR_EDICT( callee ));
		}
		else if( sv.time - cof_moment_calls[slot].dropped_at > COF_CALL_GIVE_UP && !cof_moment_calls[slot].fired )
		{
			// Nobody can take it: what the call leads to happens anyway, once.
			uint32_t none = 0;
			cof_moment_calls[slot].fired = true;
			memcpy( pv + COF_THINK, &none, sizeof( none ));
			Con_Printf( "cofstory phase=call-forced call=%s target=%s\n", SV_GetString( call->v.targetname ), SV_GetString( call->v.target ));
			if( call->v.target ) SV_CoF_StoryFireTargets( SV_GetString( call->v.target ), pv, pv );
		}
	}
	// A player who died in a call keeps its lock through respawn: clear it.
	for( k = 0; k < svs.maxclients && k < MAX_CLIENTS; k++ )
	{
		edict_t *player = svs.clients[k].edict;
		if( svs.clients[k].state == cs_spawned && SV_IsValidEdict( player ) && !CoF_MomentAlive( player ))
			CoF_MomentClearCall( player );
	}
}

// Ladders being climbed: a manager whose owner is a frozen, god-moded client
// (the climb sets both, its end clears both), and the use brushes that start it.
static void CoF_MomentLadders( void )
{
	int i, j;
	cof_moment_ladder_count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *manager = SV_EdictNum( i );
		sv_client_t *climber;
		const char *name;
		if( !CoF_MomentClass( manager, "cof_ladder_manager" ) || !( climber = CoF_MomentClient( manager->v.owner ))) continue;
		if(( climber->edict->v.flags & ( FL_FROZEN | FL_GODMODE )) != ( FL_FROZEN | FL_GODMODE )) continue;
		name = SV_GetString( manager->v.targetname );
		if( !name[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities && cof_moment_ladder_count < COF_MOMENT_LADDERS; j++ )
		{
			edict_t *use = SV_EdictNum( j );
			if( !CoF_MomentClass( use, "cof_ladder_manager_use" ) || Q_strcmp( SV_GetString( use->v.target ), name )) continue;
			VectorCopy( use->v.absmin, cof_moment_ladders[cof_moment_ladder_count].mins );
			VectorCopy( use->v.absmax, cof_moment_ladders[cof_moment_ladder_count].maxs );
			cof_moment_ladders[cof_moment_ladder_count].climber = climber->userid;
			cof_moment_ladder_count++;
		}
	}
}

void SV_CoF_MomentsCommand( sv_client_t *cl, usercmd_t *cmd )
{
	int i, k;
	if( !cl || !cmd || !( cmd->buttons & IN_USE ) || !cof_moment_ladder_count || !SV_IsValidEdict( cl->edict ) || !SV_CoF_StoryCoopEnabled() ) return;
	for( i = 0; i < cof_moment_ladder_count; i++ )
	{
		const float *mins = cof_moment_ladders[i].mins, *maxs = cof_moment_ladders[i].maxs;
		const float *o = cl->edict->v.origin;
		for( k = 0; k < 3; k++ )
			if( o[k] < mins[k] - COF_LADDER_REACH || o[k] > maxs[k] + COF_LADDER_REACH ) break;
		if( k < 3 ) continue;
		cmd->buttons &= ~IN_USE;
		if( cl->userid != cof_moment_ladders[i].climber && cl - svs.clients < MAX_CLIENTS && sv.time >= cof_moment_ladder_told[cl - svs.clients] )
		{
			int j;
			char text[96];
			for( j = 0; j < svs.maxclients && svs.clients[j].userid != cof_moment_ladders[i].climber; j++ );
			Q_snprintf( text, sizeof( text ), "%s is climbing. Wait a moment.", j < svs.maxclients ? svs.clients[j].name : "Someone" );
			SV_CoF_RetailProFont( cl, 5, text, false );
			cof_moment_ladder_told[cl - svs.clients] = sv.time + 3.0;
			Con_Printf( "cofstory phase=ladder-busy userid=%d climber=%d\n", cl->userid, cof_moment_ladders[i].climber );
		}
		return;
	}
}

// The wheelchair's rider, while it carries one: they are "the first player".
static void CoF_MomentWheelchair( void )
{
	int i;
	edict_t *rider = NULL;
	for( i = svs.maxclients + 1; i < svgame.numEntities && !rider; i++ )
	{
		edict_t *chair = SV_EdictNum( i );
		byte *pv;
		uint32_t carried;
		if( !CoF_MomentClass( chair, "wheelchair" ) || !CoF_MomentClient( chair->v.owner ) || !CoF_MomentAlive( chair->v.owner )) continue;
		if( !( pv = SV_CoF_StoryEntityPrivate( chair, COF_CHAIR_SIZE ))) continue;
		memcpy( &carried, pv + COF_CHAIR_RIDER, sizeof( carried ));
		if( carried ) rider = chair->v.owner;
	}
	SV_CoF_PreferFirst( rider );
}

// The camera model a player holds (P+0x5AC raw, P+0x388 EHANDLE), let go and removed.
static void CoF_MomentDropModel( edict_t *player )
{
	byte *pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE );
	uint32_t none = 0;
	int i;
	if( !pv ) return;
	memcpy( pv + COF_PL_CAMERA_MODEL, &none, sizeof( none ));
	memset( pv + COF_PL_CAMERA_HANDLE, 0, sizeof( cof_moment_handle_t ));
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *model = SV_EdictNum( i );
		if( CoF_MomentClass( model, "camera_model" ) && model->v.owner == player ) SetBits( model->v.flags, FL_KILLME );
	}
}

void SV_CoF_MomentsDrop( sv_client_t *cl )
{
	int i;
	uint32_t none = 0;
	if( !cl || !SV_IsValidEdict( cl->edict ) || !svgame.edicts || !SV_CoF_StoryCoopEnabled() ) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		byte *pv;
		if( !SV_IsValidEdict( ent ) || ent->v.owner != cl->edict ) continue;
		if( CoF_MomentClass( ent, "cof_ladder_manager" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_LADDER_SIZE )))
		{
			// The ladder's think and step test both pointers before use (1004ED6A, 1004F482).
			memcpy( pv + COF_LADDER_CLIMBER, &none, sizeof( none ));
			ent->v.owner = NULL;
			// Mid-climb, the leaver's camera model would stay pointed to by their
			// player data, and any player model makes later spawns wait in black
			// (MP PlayerSpawn 100C948D walks every slot, a left one too).
			if(( cl->edict->v.flags & ( FL_FROZEN | FL_GODMODE )) == ( FL_FROZEN | FL_GODMODE ))
				CoF_MomentDropModel( cl->edict );
			Con_Printf( "cofstory phase=ladder-released userid=%d\n", cl->userid );
		}
		else if( CoF_MomentClass( ent, "wheelchair" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_CHAIR_SIZE )))
		{
			// The chair's think tests its rider pointer first (10087383); the
			// rider holds the chair only through an EHANDLE.
			memcpy( pv + COF_CHAIR_RIDER, &none, sizeof( none ));
			SetBits( ent->v.flags, FL_KILLME );
			Con_Printf( "cofstory phase=wheelchair-removed userid=%d\n", cl->userid );
		}
		// The leaver's camera_model stays: their player data keeps a raw pointer
		// to it (P+0x5AC) that a running model cutscene still walks.
	}
}

void SV_CoF_MomentsMessage( edict_t *to, const char *name, const byte *payload, int size )
{
	sv_client_t *cl;
	int length, index, i, slot = -1;
	if( name && !Q_strcmp( name, "CoFLogo" ) && payload && size == 1 && !cof_moment_mirroring &&
		SV_CoF_StoryCoopEnabled() && ( cl = CoF_MomentClient( to )))
	{
		if( cof_moment_logo_count < COF_MOMENT_LOGOS )
		{
			cof_moment_logos[cof_moment_logo_count].from = cl->userid;
			cof_moment_logos[cof_moment_logo_count].logo = payload[0];
			cof_moment_logo_count++;
		}
		return;
	}
	if( !name || Q_strcmp( name, "YesNo" ) || !payload || !SV_CoF_StoryCoopEnabled() || !( cl = CoF_MomentClient( to ))) return;
	// YesNo( string tga, short entindex ) to the player who is asked (cof_yesno Use 100A8140).
	for( length = 0; length < size && payload[length]; length++ );
	if( length + 3 > size ) return;
	index = payload[length + 1] | ( payload[length + 2] << 8 );
	if( index <= svs.maxclients || index >= svgame.numEntities ) return;
	for( i = 0; i < COF_MOMENT_PROMPTS; i++ )
	{
		// A "yes" is final; after a "no", asking again reopens the question.
		if( cof_moment_prompts[i].index == index && cof_moment_prompts[i].closed ) return;
		if( cof_moment_prompts[i].index == index && cof_moment_prompts[i].answered ) memset( &cof_moment_prompts[i], 0, sizeof( cof_moment_prompts[i] ));
		if( cof_moment_prompts[i].index == index && cof_moment_prompts[i].userid == cl->userid ) return;
		if( slot < 0 && !cof_moment_prompts[i].index ) slot = i;
	}
	if( slot < 0 ) slot = 0;
	cof_moment_prompts[slot].index = index;
	cof_moment_prompts[slot].serial = SV_EdictNum( index )->serialnumber;
	cof_moment_prompts[slot].userid = cl->userid;
	cof_moment_prompts[slot].answered = false;
}

qboolean SV_CoF_MomentsClientCommand( sv_client_t *cl )
{
	const char *command = Cmd_Argv( 0 );
	int index, i, asked = -1, others = 0, answered = -1;
	edict_t *yesno;
	if( Cmd_Argc() < 2 || ( Q_stricmp( command, "yesnoactivate" ) && Q_stricmp( command, "yesnono" )) || !SV_CoF_StoryCoopEnabled() ) return false;
	index = Q_atoi( Cmd_Argv( 1 ));
	if( index <= svs.maxclients || index >= svgame.numEntities ) return false;
	yesno = SV_EdictNum( index );
	for( i = 0; i < COF_MOMENT_PROMPTS; i++ )
	{
		if( cof_moment_prompts[i].index != index || cof_moment_prompts[i].serial != yesno->serialnumber ) continue;
		if( cof_moment_prompts[i].answered ) answered = i;
		else if( cof_moment_prompts[i].userid == cl->userid ) asked = i;
		else others++;
	}
	if( asked >= 0 )
	{
		// First answer wins: the question is closed for everyone else.
		for( i = 0; i < COF_MOMENT_PROMPTS; i++ )
			if( cof_moment_prompts[i].index == index ) memset( &cof_moment_prompts[i], 0, sizeof( cof_moment_prompts[i] ));
		cof_moment_prompts[asked].index = index;
		cof_moment_prompts[asked].serial = yesno->serialnumber;
		cof_moment_prompts[asked].answered = true;
		cof_moment_prompts[asked].closed = !Q_stricmp( command, "yesnoactivate" );
		Con_Printf( "cofstory phase=yesno-answer userid=%d entity=%d command=%s\n", cl->userid, index, command );
		return false; // the game takes the answer
	}
	if( answered < 0 && !others ) return false; // a question the story layer never saw: the game decides
	SV_CoF_RetailProFont( cl, 5, answered >= 0 ? "That has already been answered." : "Someone else was asked that.", true );
	// c_roofs2's ryesno marks whoever it asks (fuser3 1) until they answer.
	if( !Q_strcmp( SV_GetString( yesno->v.targetname ), "ryesno" ) && SV_IsValidEdict( cl->edict ) && cl->edict->v.fuser3 == 1.0f )
		cl->edict->v.fuser3 = 0.0f;
	Con_Printf( "cofstory phase=yesno-refused userid=%d entity=%d\n", cl->userid, index );
	return true;
}

// The logos one player was shown, to everyone else.
static void CoF_MomentLogos( void )
{
	int i, j;
	cof_moment_mirroring = true;
	for( i = 0; i < cof_moment_logo_count; i++ )
	{
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *cl = &svs.clients[j];
			if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || cl->userid == cof_moment_logos[i].from ) continue;
			SV_CoF_SendUserMessage( cl->edict, "CoFLogo", &cof_moment_logos[i].logo, 1, true );
		}
		Con_Printf( "cofstory phase=logo-shared from=%d logo=%d\n", cof_moment_logos[i].from, cof_moment_logos[i].logo );
	}
	cof_moment_mirroring = false;
	cof_moment_logo_count = 0;
}

void SV_CoF_MomentsFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	CoF_MomentCheckGeneration();
	CoF_MomentWheelchair();
	CoF_MomentLadders();
	CoF_MomentCalls();
	CoF_MomentLogos();
}
