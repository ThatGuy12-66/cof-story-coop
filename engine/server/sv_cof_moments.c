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
 *   frame later. The c_ending4 grey fade (cof_greyfade) the same (1.1.8).
 * - 1.1.8: a trigger_once that teleports its toucher into a kill box (c_city's
 *   flowers, the c_roofs falls) is a fall floor too (M1 findings 2 and 6); of two
 *   trigger_once entrances to one scene the first that fires switches the others
 *   off (M1 finding 4); a boat whose rider leaves goes back to where it started
 *   (K6), and riders are let go before a level change (M2 finding 4); a stopped
 *   wheelchair takes nobody (M3 finding 6); c_down's only ladder down is no
 *   longer removed under a partner still at the top (M1 finding 8). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "pm_local.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_moments.h"

#define COF_THINK 0x78 // CBaseEntity::m_pfnThink
#define COF_USE 0x80 // CBaseEntity::m_pfnUse, after m_pfnThink and m_pfnTouch

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
#define COF_PL_BOAT 0x380 // EHANDLE m_pBoat: the boat they row (Embark sets it, Disembark clears it)
#define COF_PL_ENABLE_CONTROL 0xDEFF0 // CBasePlayer::EnableControl( BOOL ), thiscall (as sv_cof_rescue.c)

#define COF_LADDER_SIZE 0x1B4
#define COF_LADDER_CLIMBER 0x114 // raw CBasePlayer*
#define COF_LADDER_REACH 96.0f
#define COF_CHAIR_SIZE 0x388
#define COF_CHAIR_DEAD 0x368 // bool m_bDead
#define COF_CHAIR_RIDER 0x384 // raw CBasePlayer*
// CBoat : CBaseAnimating (0x100): boattime1-4, then pExitEntity, pExitTeller and m_pPlayer, all raw (CoF_Boat.cpp)
#define COF_BOAT_SIZE 0x11C
#define COF_BOAT_EXIT 0x110 // raw CBaseEntity *pExitEntity: the dock Disembark sets the rider on
#define COF_BOAT_TELLER 0x114 // raw CBaseEntity *pExitTeller: the dock last announced
#define COF_BOAT_RIDER 0x118 // raw CBasePlayer *m_pPlayer: BoatUse takes nobody while it is set
// CMultiManager : CBaseEntity (0xDC): STATE m_iState, m_cTargets, m_index, m_startTime, string_t m_iTargetName[16]
#define COF_MM_SIZE 0x12C
#define COF_MM_COUNT 0xE0 // int m_cTargets: Spawn clamps it to 16
#define COF_MM_INDEX 0xE4 // int m_index: the next target, 0..m_cTargets
#define COF_MM_NAMES 0xEC
#define COF_MM_TARGETS 16
#define COF_TRAP_DAMAGE 100.0f // a trigger_hurt doing this much kills (the traps' do 1000)
// CBaseDelay : CBaseEntity (0xDC): float m_flDelay, string_t m_iszKillTarget, EHANDLE m_hActivator
#define COF_DELAY_SIZE 0xEC
#define COF_DELAY_KILLTARGET 0xE0

#define COF_MOMENT_FLOORS 8
#define COF_MOMENT_LADDERS 16
#define COF_MOMENT_CALLS 8
#define COF_MOMENT_PROMPTS 16
#define COF_MOMENT_LOGOS 8
#define COF_MOMENT_MANAGERS 128 // the most multi_managers a story map has is 70 (cm_park3)
#define COF_MOMENT_CANDIDATES 32 // trigger_once entrances considered per map (at most 11 in a story map); a bit each
#define COF_MOMENT_ROOTS 16
#define COF_MOMENT_BOATS 2
#define COF_MOMENT_FADES 8

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
// GreyFade messages the game sent this frame, and the client slots that got each (1.1.8).
static struct { byte payload[2]; unsigned int sent; } cof_moment_fades[COF_MOMENT_FADES];
static int cof_moment_fade_count;
// What each multi_manager fires, read when a map starts (names in the game's string pool, used only then).
static struct { const char *name; int count; const char *targets[COF_MM_TARGETS]; } cof_moment_managers[COF_MOMENT_MANAGERS];
static int cof_moment_manager_count;
// trigger_once entrances to one scene (siblings: a bit per entry here); fired: seen gone, its siblings switched off.
static struct { int index, serial; unsigned int siblings; qboolean fired; } cof_moment_roots[COF_MOMENT_ROOTS];
static int cof_moment_root_count;
// Each boat where the map put it.
static struct { int index, serial; vec3_t origin, angles; } cof_moment_boats[COF_MOMENT_BOATS];
static int cof_moment_boat_count;

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
	memset( cof_moment_fades, 0, sizeof( cof_moment_fades ));
	cof_moment_fade_count = 0;
	memset( cof_moment_managers, 0, sizeof( cof_moment_managers ));
	cof_moment_manager_count = 0;
	memset( cof_moment_roots, 0, sizeof( cof_moment_roots ));
	cof_moment_root_count = 0;
	memset( cof_moment_boats, 0, sizeof( cof_moment_boats ));
	cof_moment_boat_count = 0;
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

// What each multi_manager fires, from its private data; one whose fields are not what the layout says is left out
// (then nothing is decided from it).
static void CoF_MomentManagers( void )
{
	int i, k, count, next;
	string_t name;
	cof_moment_manager_count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_moment_manager_count < COF_MOMENT_MANAGERS; i++ )
	{
		edict_t *mm = SV_EdictNum( i );
		byte *pv;
		if( !CoF_MomentClass( mm, "multi_manager" ) || !SV_GetString( mm->v.targetname )[0] ||
			!( pv = SV_CoF_StoryEntityPrivate( mm, COF_MM_SIZE ))) continue;
		memcpy( &count, pv + COF_MM_COUNT, sizeof( count ));
		memcpy( &next, pv + COF_MM_INDEX, sizeof( next ));
		if( count < 0 || count > COF_MM_TARGETS || next < 0 || next > count ) continue;
		cof_moment_managers[cof_moment_manager_count].name = SV_GetString( mm->v.targetname );
		cof_moment_managers[cof_moment_manager_count].count = 0;
		for( k = 0; k < count; k++ )
		{
			memcpy( &name, pv + COF_MM_NAMES + k * sizeof( name ), sizeof( name ));
			if( name ) cof_moment_managers[cof_moment_manager_count].targets[cof_moment_managers[cof_moment_manager_count].count++] = SV_GetString( name );
		}
		cof_moment_manager_count++;
	}
}

// A multi_manager of this name fires something.
static qboolean CoF_MomentIsManager( const char *name )
{
	int i;
	for( i = 0; i < cof_moment_manager_count; i++ )
		if( cof_moment_managers[i].count && !Q_strcmp( cof_moment_managers[i].name, name )) return true;
	return false;
}

// A trigger aimed at target fires name: it is target, or a multi_manager of that name fires it (one step, as the
// traps and the scene entrances are wired).
static qboolean CoF_MomentFires( const char *target, const char *name )
{
	int i, k;
	if( !name[0] ) return false;
	if( !Q_strcmp( target, name )) return true;
	for( i = 0; i < cof_moment_manager_count; i++ )
	{
		if( Q_strcmp( cof_moment_managers[i].name, target )) continue;
		for( k = 0; k < cof_moment_managers[i].count; k++ )
			if( !Q_strcmp( cof_moment_managers[i].targets[k], name )) return true;
	}
	return false;
}

// A player standing at origin touches this brush entity: the test SV_TouchLinks makes, a player's hull (the one
// SV_HullForBsp picks for 32 x 32 x 72) against the brush's own shape, not its box. Rotated brushes never count.
static qboolean CoF_MomentTouches( edict_t *brush, const vec3_t origin )
{
	static const vec3_t mins = { -16.0f, -16.0f, -36.0f };
	model_t *mod = SV_ModelHandle( brush->v.modelindex );
	hull_t *hull;
	vec3_t offset, test;
	int k;
	if( !mod || mod->type != mod_brush || !VectorIsNull( brush->v.angles )) return false;
	for( k = 0; k < 3; k++ )
		if( origin[k] + mins[k] > brush->v.absmax[k] || origin[k] - mins[k] < brush->v.absmin[k] ) return false;
	hull = &mod->hulls[1];
	VectorSubtract( hull->clip_mins, mins, offset );
	VectorAdd( offset, brush->v.origin, offset );
	VectorSubtract( origin, offset, test );
	return PM_HullPointContents( hull, hull->firstclipnode, test ) == CONTENTS_SOLID;
}

// 1.1.8 (M1 findings 2 and 6): a trigger aimed at target teleports its toucher (a cof_teleport it fires, without
// a monster to move) to where a trigger_hurt of 100+ damage kills them: one that is on and stays so, or one the
// same chain switches on. In single player that is the death scene of c_city's flowers and of the c_roofs and
// c_roofs2 falls; a teleport anywhere else is left alone.
static qboolean CoF_MomentTrap( const char *target )
{
	int i, j, k;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *tele = SV_EdictNum( i );
		if( !CoF_MomentClass( tele, "cof_teleport" ) || SV_GetString( tele->v.message )[0] || !SV_GetString( tele->v.target )[0] ||
			!CoF_MomentFires( target, SV_GetString( tele->v.targetname ))) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *dest = SV_EdictNum( j );
			vec3_t landing;
			if( !CoF_MomentClass( dest, "info_teleport_destination" ) || Q_strcmp( SV_GetString( dest->v.targetname ), SV_GetString( tele->v.target ))) continue;
			// where cof_teleport puts a player: the destination, raised to the hull's centre and one unit up
			VectorCopy( dest->v.origin, landing );
			landing[2] += 37.0f;
			for( k = svs.maxclients + 1; k < svgame.numEntities; k++ )
			{
				edict_t *hurt = SV_EdictNum( k );
				qboolean on, switched;
				if( !CoF_MomentClass( hurt, "trigger_hurt" ) || hurt->v.dmg < COF_TRAP_DAMAGE || ( hurt->v.spawnflags & 8 ) || // 8: no clients
					!CoF_MomentTouches( hurt, landing )) continue;
				on = hurt->v.solid == SOLID_TRIGGER;
				switched = CoF_MomentFires( target, SV_GetString( hurt->v.targetname ));
				if( on != switched ) return true;
			}
		}
	}
	return false;
}

static int CoF_MomentFloorAt( int index )
{
	int i;
	for( i = 0; i < cof_moment_floor_count; i++ )
		if( cof_moment_floors[i].index == index ) return i;
	return -1;
}

// 1.1.8 (M1 finding 4): trigger_once entrances to one scene, with no name of their own (nothing switches or
// removes them): two whose targets are one multi_manager, or one that fires the other's. c_roofs #265/#681
// (multiroofscene: a second fire replays its 358 s and toggles env_global stater back off), c_nightmare
// #125/#159/#293, c_intro #157/#381. Single player touches only one of them.
static void CoF_MomentRoots( void )
{
	static int candidates[COF_MOMENT_CANDIDATES], entry[COF_MOMENT_CANDIDATES];
	static unsigned int found[COF_MOMENT_CANDIDATES]; // siblings among the candidates, a bit each
	int count = 0, i, a, b;
	for( i = svs.maxclients + 1; i < svgame.numEntities && count < COF_MOMENT_CANDIDATES; i++ )
	{
		edict_t *root = SV_EdictNum( i );
		if( !CoF_MomentClass( root, "trigger_once" ) || SV_GetString( root->v.targetname )[0] || CoF_MomentFloorAt( i ) >= 0 ||
			!CoF_MomentIsManager( SV_GetString( root->v.target ))) continue;
		candidates[count++] = i;
	}
	for( a = 0; a < count; a++ )
	{
		const char *mine = SV_GetString( SV_EdictNum( candidates[a] )->v.target );
		found[a] = 0;
		for( b = 0; b < count; b++ )
		{
			const char *theirs = SV_GetString( SV_EdictNum( candidates[b] )->v.target );
			if( b != a && ( CoF_MomentFires( mine, theirs ) || CoF_MomentFires( theirs, mine ))) found[a] |= 1u << b;
		}
		entry[a] = -1;
		if( !found[a] || cof_moment_root_count >= COF_MOMENT_ROOTS ) continue;
		entry[a] = cof_moment_root_count;
		cof_moment_roots[cof_moment_root_count].index = candidates[a];
		cof_moment_roots[cof_moment_root_count].serial = SV_EdictNum( candidates[a] )->serialnumber;
		cof_moment_root_count++;
		Con_Printf( "cofstory phase=scene-entrance trigger=%d target=%s\n", candidates[a], mine );
	}
	for( a = 0; a < count; a++ )
	{
		if( entry[a] < 0 ) continue;
		for( b = 0; b < count; b++ )
			if(( found[a] & ( 1u << b )) && entry[b] >= 0 ) cof_moment_roots[entry[a]].siblings |= 1u << entry[b];
	}
}

// An entrance that fired is gone (trigger_once removes itself 0.1 s later): its siblings are switched off as
// cof_triggeron does (iuser1: CanTouch refuses every touch).
static void CoF_MomentRootsFired( void )
{
	int i, j;
	for( i = 0; i < cof_moment_root_count; i++ )
	{
		edict_t *root = SV_EdictNum( cof_moment_roots[i].index );
		if( cof_moment_roots[i].fired ) continue;
		if( SV_IsValidEdict( root ) && root->serialnumber == cof_moment_roots[i].serial && !( root->v.flags & FL_KILLME ) &&
			!Q_strcmp( SV_GetString( root->v.classname ), "trigger_once" )) continue;
		cof_moment_roots[i].fired = true;
		for( j = 0; j < cof_moment_root_count; j++ )
		{
			edict_t *sibling = SV_EdictNum( cof_moment_roots[j].index );
			if( !( cof_moment_roots[i].siblings & ( 1u << j )) || cof_moment_roots[j].fired ) continue;
			cof_moment_roots[j].fired = true; // it will not fire now
			if( !SV_IsValidEdict( sibling ) || sibling->serialnumber != cof_moment_roots[j].serial ) continue;
			sibling->v.iuser1 = 1;
			Con_Printf( "cofstory phase=scene-entrance-off trigger=%d fired=%d\n", cof_moment_roots[j].index, cof_moment_roots[i].index );
		}
	}
}

// 1.1.8 (M1 finding 8): c_down kills its ladder's only use brush (trigger_relay killladddd, killtarget ladddd) when
// the first player reaches the bottom; a partner still at the top could never climb down. That use only takes a
// player down from the top, so a relay's kill of a ladder use is dropped.
static void CoF_MomentKeepLadders( void )
{
	int i, j;
	string_t kill, none = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *relay = SV_EdictNum( i );
		const char *name = NULL;
		byte *pv;
		if( !CoF_MomentClass( relay, "trigger_relay" ) || !( pv = SV_CoF_StoryEntityPrivate( relay, COF_DELAY_SIZE ))) continue;
		memcpy( &kill, pv + COF_DELAY_KILLTARGET, sizeof( kill ));
		if( !kill || !( name = SV_GetString( kill ))[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *use = SV_EdictNum( j );
			if( !CoF_MomentClass( use, "cof_ladder_manager_use" ) || Q_strcmp( SV_GetString( use->v.targetname ), name )) continue;
			memcpy( pv + COF_DELAY_KILLTARGET, &none, sizeof( none ));
			Con_Printf( "cofstory phase=ladder-kept relay=%d use=%s\n", i, name );
			break;
		}
	}
}

// Fall-death floors: trigger_once entities whose target is a cof_killplayer.
// The doctor's execution in c_attic reaches its cof_killplayer through
// multi_managers and is not one of them. 1.1.8: and death traps (CoF_MomentTrap).
static void CoF_MomentCheckGeneration( void )
{
	int i, j;
	if( cof_moment_generation == svs.spawncount ) return;
	SV_CoF_MomentsReset();
	cof_moment_generation = svs.spawncount;
	CoF_MomentManagers();
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
		if( CoF_MomentFloorAt( i ) < 0 && cof_moment_floor_count < COF_MOMENT_FLOORS && CoF_MomentTrap( target ))
		{
			cof_moment_floors[cof_moment_floor_count].index = i;
			cof_moment_floors[cof_moment_floor_count].serial = floor->serialnumber;
			cof_moment_floors[cof_moment_floor_count].deathbits = 0;
			cof_moment_floor_count++;
			Con_Printf( "cofstory phase=fall-floor trigger=%d target=%s trap=1\n", i, target );
		}
	}
	CoF_MomentRoots();
	CoF_MomentKeepLadders();
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_moment_boat_count < COF_MOMENT_BOATS; i++ )
	{
		edict_t *boat = SV_EdictNum( i );
		if( !CoF_MomentClass( boat, "boat" )) continue;
		cof_moment_boats[cof_moment_boat_count].index = i;
		cof_moment_boats[cof_moment_boat_count].serial = boat->serialnumber;
		VectorCopy( boat->v.origin, cof_moment_boats[cof_moment_boat_count].origin );
		VectorCopy( boat->v.angles, cof_moment_boats[cof_moment_boat_count].angles );
		cof_moment_boat_count++;
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

// Mid-climb: the ladder froze the player and handed them its camera model (P+0x5AC); its end clears both
// (LadderThink). The climb's FL_GODMODE is no sign: the game's PostThink clears it on every usercmd.
static qboolean CoF_MomentClimbing( edict_t *player )
{
	byte *pv;
	uint32_t model;
	if( !SV_IsValidEdict( player ) || !( player->v.flags & FL_FROZEN ) || !( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE )))
		return false;
	memcpy( &model, pv + COF_PL_CAMERA_MODEL, sizeof( model ));
	return model != 0;
}

// Ladders being climbed: a manager whose owner is mid-climb, and the use brushes that start it.
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
		if( !CoF_MomentClimbing( climber->edict )) continue;
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
// 1.1.8 (M3 finding 6d): after cof_stopwheelchair the empty chair stays, and
// WheelchairUse boards anyone who uses it (it checks only that it carries
// nobody), taking all their weapons (Embark: RemoveAllItems). A chair is made
// carrying its rider (StartWheelchairMode), so an empty living one was stopped:
// it takes nobody now (no use function, as SetUse( NULL )).
static void CoF_MomentWheelchair( void )
{
	int i;
	edict_t *rider = NULL;
	uintptr_t base = SV_CoF_StoryBase();
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *chair = SV_EdictNum( i );
		byte *pv;
		uint32_t carried, use, none = 0;
		if( !CoF_MomentClass( chair, "wheelchair" ) || !( pv = SV_CoF_StoryEntityPrivate( chair, COF_CHAIR_SIZE ))) continue;
		memcpy( &carried, pv + COF_CHAIR_RIDER, sizeof( carried ));
		if( carried )
		{
			if( !rider && CoF_MomentClient( chair->v.owner ) && CoF_MomentAlive( chair->v.owner )) rider = chair->v.owner;
			continue;
		}
		memcpy( &use, pv + COF_USE, sizeof( use ));
		if( pv[COF_CHAIR_DEAD] || !base || use < base ) continue; // a dead chair is the rescue's; no code pointer: left alone
		memcpy( pv + COF_USE, &none, sizeof( none ));
		Con_Printf( "cofstory phase=wheelchair-stopped chair=%d\n", i );
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

// A boat's rider pointers cleared, as Disembark leaves them (CoF_Boat.cpp), and the boat stopped: BoatUse takes
// the next player who uses it. BoatThink tests the rider before use; Embark and Disembark, if one is due, return.
static void CoF_MomentBoatFree( edict_t *boat, byte *pv )
{
	uint32_t none = 0;
	memcpy( pv + COF_BOAT_RIDER, &none, sizeof( none ));
	memcpy( pv + COF_BOAT_EXIT, &none, sizeof( none ));
	memcpy( pv + COF_BOAT_TELLER, &none, sizeof( none ));
	VectorClear( boat->v.velocity );
	VectorClear( boat->v.avelocity );
}

// 1.1.8 (K6, M2 finding 3): the c_lake boat carries one player, through a raw pointer only Disembark clears. A
// rider who leaves mid-crossing left it taken for good (and the pointer dangling once their slot was reused): it is
// freed and put back where the map started it, on the party's shore, for a partner to row.
static void CoF_MomentBoatDrop( sv_client_t *cl, edict_t *boat, byte *pv )
{
	uint32_t carried, leaver = (uint32_t)(uintptr_t)cl->edict->pvPrivateData;
	byte *player;
	int k, moved = 0;
	memcpy( &carried, pv + COF_BOAT_RIDER, sizeof( carried ));
	if( !carried || carried != leaver ) return;
	CoF_MomentBoatFree( boat, pv );
	if(( player = SV_CoF_StoryEntityPrivate( cl->edict, COF_PL_SIZE )) != NULL ) memset( player + COF_PL_BOAT, 0, sizeof( cof_moment_handle_t ));
	for( k = 0; k < cof_moment_boat_count; k++ )
	{
		if( cof_moment_boats[k].index != NUM_FOR_EDICT( boat ) || cof_moment_boats[k].serial != boat->serialnumber ) continue;
		VectorCopy( cof_moment_boats[k].origin, boat->v.origin );
		VectorCopy( cof_moment_boats[k].angles, boat->v.angles );
		SV_LinkEdict( boat, false );
		moved = 1;
	}
	Con_Printf( "cofstory phase=boat-freed userid=%d boat=%d moved=%d\n", cl->userid, NUM_FOR_EDICT( boat ), moved );
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
		if( cl->edict->pvPrivateData && CoF_MomentClass( ent, "boat" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_BOAT_SIZE )))
		{
			CoF_MomentBoatDrop( cl, ent, pv );
			continue;
		}
		if( !SV_IsValidEdict( ent ) || ent->v.owner != cl->edict ) continue;
		if( CoF_MomentClass( ent, "cof_ladder_manager" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_LADDER_SIZE )))
		{
			// The ladder's think and step test both pointers before use (1004ED6A, 1004F482).
			memcpy( pv + COF_LADDER_CLIMBER, &none, sizeof( none ));
			ent->v.owner = NULL;
			// Mid-climb, the leaver's camera model would stay pointed to by their
			// player data, and any player model makes later spawns wait in black
			// (MP PlayerSpawn 100C948D walks every slot, a left one too).
			if( CoF_MomentClimbing( cl->edict ))
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
	// 1.1.8 (M3 finding 7): GreyFade( byte in, byte seconds ). cof_greyfade (c_ending4 fadeoutbw) sends it to "the
	// first player" only (the host, from a think); the book Simons send it to every player themselves.
	if( name && !Q_strcmp( name, "GreyFade" ) && payload && size == 2 && !cof_moment_mirroring &&
		SV_CoF_StoryCoopEnabled() && ( cl = CoF_MomentClient( to )))
	{
		for( i = 0; i < cof_moment_fade_count && memcmp( cof_moment_fades[i].payload, payload, 2 ); i++ );
		if( i == cof_moment_fade_count )
		{
			if( i == COF_MOMENT_FADES ) return;
			memcpy( cof_moment_fades[i].payload, payload, 2 );
			cof_moment_fades[i].sent = 0;
			cof_moment_fade_count++;
		}
		cof_moment_fades[i].sent |= 1u << (int)( cl - svs.clients );
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

// The grey fades the game showed some players this frame, to the others (1.1.8).
static void CoF_MomentFades( void )
{
	int i, j;
	cof_moment_mirroring = true;
	for( i = 0; i < cof_moment_fade_count; i++ )
	{
		int sent = 0;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *cl = &svs.clients[j];
			if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || ( cof_moment_fades[i].sent & ( 1u << j ))) continue;
			SV_CoF_SendUserMessage( cl->edict, "GreyFade", cof_moment_fades[i].payload, 2, true );
			sent++;
		}
		if( sent ) Con_Printf( "cofstory phase=greyfade-shared in=%d seconds=%d to=%d\n", cof_moment_fades[i].payload[0], cof_moment_fades[i].payload[1], sent );
	}
	cof_moment_mirroring = false;
	cof_moment_fade_count = 0;
}

// 1.1.8 (M2 finding 4): a level change while someone rows carries them over frozen, with the health HUD hidden and
// nothing to row (the boat stays). Before the transition is captured every rider is let go as Disembark does it,
// without the move to the dock: the boat's pointers cleared, their m_pBoat cleared (the game sends Boat -1), control
// and the HUD back, the screen faded in. The boat stops where it is.
void SV_CoF_MomentsReleaseRiders( void )
{
	static const byte out[2] = { 0xFF, 0xFF }; // Boat( short -1 ), as Disembark
	int i, j, hud;
	if( !svgame.edicts || !SV_CoF_StoryCoopEnabled() ) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *boat = SV_EdictNum( i );
		sv_client_t *rider = NULL;
		cof_moment_handle_t held;
		byte *pv, *player = NULL;
		uint32_t carried;
		if( !CoF_MomentClass( boat, "boat" ) || !( pv = SV_CoF_StoryEntityPrivate( boat, COF_BOAT_SIZE ))) continue;
		memcpy( &carried, pv + COF_BOAT_RIDER, sizeof( carried ));
		if( !carried ) continue;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS && !rider; j++ )
			if( svs.clients[j].state == cs_spawned && SV_IsValidEdict( svs.clients[j].edict ) &&
				(uint32_t)(uintptr_t)svs.clients[j].edict->pvPrivateData == carried ) rider = &svs.clients[j];
		CoF_MomentBoatFree( boat, pv );
		if( !rider || !( player = SV_CoF_StoryEntityPrivate( rider->edict, COF_PL_SIZE )))
		{
			Con_Printf( "cofstory phase=boat-released boat=%d rider=none\n", i );
			continue;
		}
		memcpy( &held, player + COF_PL_BOAT, sizeof( held ));
		if( held.edict == boat && held.serial == boat->serialnumber )
		{
			// on board (Embark ran): out of the boat's view, the health HUD back
			memset( player + COF_PL_BOAT, 0, sizeof( held ));
			memcpy( &hud, player + COF_PL_HIDEHUD, sizeof( hud ));
			hud &= ~8;
			memcpy( player + COF_PL_HIDEHUD, &hud, sizeof( hud ));
			SV_CoF_SendUserMessage( rider->edict, "Boat", out, sizeof( out ), true );
		}
		SV_CoF_StoryCallMember( COF_PL_ENABLE_CONTROL, player, (void *)1 );
		SV_CoF_RetailScreenFade( rider, 1.0f, 0.0f, 0, 0, 0, 0, 255 );
		Con_Printf( "cofstory phase=boat-released boat=%d userid=%d\n", i, rider->userid );
	}
}

void SV_CoF_MomentsFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	CoF_MomentCheckGeneration();
	CoF_MomentRootsFired();
	CoF_MomentWheelchair();
	CoF_MomentLadders();
	CoF_MomentCalls();
	CoF_MomentLogos();
	CoF_MomentFades();
}
