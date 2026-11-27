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
 *   no busy check): nobody can start a ladder someone is climbing. 1.1.9: the
 *   climb's forward and back come from the usercmds too (a held key climbs on).
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
 *   longer removed under a partner still at the top (M1 finding 8).
 * - 1.1.9: a delayed chain started by a player who leaves goes on with the
 *   living person nearest them (S2). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "pm_local.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_party.h"
#include "sv_cof_sections.h"
#include "sv_cof_rescue.h"
#include "cof_strip.h"
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
#define COF_CALL_UNANSWERED 20.0 // seconds a call rings one person before it rings another who can take it (1.1.9, AI2)

#define COF_PL_SIZE 0x281C
#define COF_PL_CALL_UI 0x1EA0 // dword: 3 = incoming-call UI
#define COF_PL_BARS 0x1EB4 // dword: black bars
#define COF_PL_IN_CALL 0x1EE8 // byte m_bInPhoneCall: ClientCommand ignores the player (100190BE)
#define COF_PL_VAN_CALL 0x3A0 // int m_iPhoneNumberState: the 911/112 call's step (1, 2), not saved; PostThink ends it
#define COF_CALL_ORPHAN 2.0 // seconds in a call that nothing will end before it is cleared (1.1.9, S19)
#define COF_PL_HIDEHUD 0x2500 // dword m_iHideHUD
#define COF_PL_SUICIDE 0x22E8 // float m_fNextSuicideTime (ClientKill)
#define COF_PL_DEATHBITS 0x5B4 // dword m_iDeathBits (cof_killplayer)
#define COF_PL_CAMERA_MODEL 0x5AC // raw CBaseEntity*: the player's camera_model (ladder, strangle, model cutscene)
#define COF_PL_CAMERA_HANDLE 0x388 // EHANDLE to the same model
#define COF_PL_BOAT 0x380 // EHANDLE m_pBoat: the boat they row (Embark sets it, Disembark clears it)
#define COF_PL_ENABLE_CONTROL 0xDEFF0 // CBasePlayer::EnableControl( BOOL ), thiscall (as sv_cof_rescue.c)
// bool m_bBossHealthBar, then string_t m_sBossBarEmpty, m_sBossBarFull, int m_iBossMaxHealth, m_iBossCurrentHealth,
// string_t m_sBossName (A-player.md survey); Spawn clears only the first (0x100F44F4).
#define COF_PL_BOSS_BAR 0x5F4
#define COF_PL_BOSS_EMPTY 0x5F8
#define COF_PL_BOSS_FULL 0x5FC
#define COF_PL_BOSS_MAX 0x600
#define COF_PL_BOSS_HEALTH 0x604
#define COF_PL_BOSS_NAME 0x608
#define COF_PL_BOSS_END 0x60C

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
// then float m_flTargetDelay[16], m_flWait, m_flMaxWait, m_sMaster, m_iMode, m_iszThreadName, m_iszLocusThread and
// EHANDLE m_hActivator (SoHL, D/dlls/triggers.cpp:480-520; 1.1.9, S2)
#define COF_MM_DELAYS 0x12C
#define COF_MM_ACTIVATOR 0x184
#define COF_MM_ACTIVATOR_SIZE 0x18C
#define COF_TRAP_DAMAGE 100.0f // a trigger_hurt doing this much kills (the traps' do 1000)
// CBaseDelay : CBaseEntity (0xDC): float m_flDelay, string_t m_iszKillTarget, EHANDLE m_hActivator
#define COF_DELAY_SIZE 0xEC
#define COF_DELAY_DELAY 0xDC
#define COF_DELAY_KILLTARGET 0xE0
#define COF_DELAY_ACTIVATOR 0xE4

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
#define COF_MOMENT_HURTS 8
#define COF_MOMENT_CREDITS_SIZE 256
#define COF_FADE_STAYOUT 0x0004 // ScreenFade: the colour stays until the next fade

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
// 1.1.9: each climber's camera model (index, serial) and the climb keys let go since it was handed out.
static struct { int model, serial; unsigned int armed; } cof_moment_climbs[MAX_CLIENTS];
// next_restart: a dropped call starts again at most this often (the game may
// drop it at once if it disagrees with us about the phone); told: the last
// player announced as taking it, so the messages go out once per callee;
// ringing since: who it rings and since when (1.1.9, AI2).
static struct { int index, serial; double dropped_at, next_restart, ringing_since; qboolean fired; edict_t *told, *ringing; } cof_moment_calls[COF_MOMENT_CALLS];
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
// The Credits message the game last showed (cof_credits sends one to each player in one Use), its frame, and the
// userid of each client slot that has it (1.1.9, S18).
static struct { byte payload[COF_MOMENT_CREDITS_SIZE]; int size; double time; int userids[MAX_CLIENTS]; } cof_moment_credits;
// Per client slot, what the last fade and black-and-white that reached them left on screen (1.1.9, S5). Kept over
// map changes (by userid): a screen thought dark is only ever left alone.
static struct { int userid, grey; qboolean dark; double told; } cof_moment_screens[MAX_CLIENTS];
// What held each client slot frozen last frame: a ladder (its manager and the climb's camera model) or a boat, and
// whether their freeze was put back once already (1.1.9, S13).
static struct { int userid, manager, boat; uint32_t model; qboolean told; } cof_moment_held[MAX_CLIENTS];
// Since when each client slot is in a call nothing will end (1.1.9, S19).
static struct { int userid; double since; } cof_moment_orphans[MAX_CLIENTS];
// Who was alive last frame, per client slot (1.1.9, S11: a respawn).
static struct { int userid; qboolean alive; } cof_moment_lives[MAX_CLIENTS];
// Lethal trigger_hurts the map can switch on or off, and whether each was on last frame (1.1.9, P2).
static struct { int index, serial, solid; } cof_moment_hurts[COF_MOMENT_HURTS];
static int cof_moment_hurt_count;

void SV_CoF_MomentsReset( void )
{
	cof_moment_generation = -1;
	cof_moment_floor_count = cof_moment_ladder_count = 0;
	memset( cof_moment_floors, 0, sizeof( cof_moment_floors ));
	memset( cof_moment_ladders, 0, sizeof( cof_moment_ladders ));
	memset( cof_moment_ladder_told, 0, sizeof( cof_moment_ladder_told ));
	memset( cof_moment_climbs, 0, sizeof( cof_moment_climbs ));
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
	memset( cof_moment_hurts, 0, sizeof( cof_moment_hurts ));
	cof_moment_hurt_count = 0;
	memset( cof_moment_lives, 0, sizeof( cof_moment_lives ));
	memset( &cof_moment_credits, 0, sizeof( cof_moment_credits ));
	memset( cof_moment_orphans, 0, sizeof( cof_moment_orphans ));
	memset( cof_moment_held, 0, sizeof( cof_moment_held ));
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
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_moment_hurt_count < COF_MOMENT_HURTS; i++ )
	{
		edict_t *hurt = SV_EdictNum( i );
		if( !CoF_MomentClass( hurt, "trigger_hurt" ) || hurt->v.dmg < COF_TRAP_DAMAGE || ( hurt->v.spawnflags & 8 ) || // 8: no clients
			!SV_GetString( hurt->v.targetname )[0] ) continue;
		cof_moment_hurts[cof_moment_hurt_count].index = i;
		cof_moment_hurts[cof_moment_hurt_count].serial = hurt->serialnumber;
		cof_moment_hurts[cof_moment_hurt_count].solid = hurt->v.solid;
		cof_moment_hurt_count++;
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

// The ring is a looping wav on the ringed player's entity (WaitForCall 10061000, CHAN_AUTO), and the game stops it
// only on the player who answers (CallThink 1005FD88..1005FDB0). A ring the call moves away from, drops or loses its
// player is stopped here, or it goes on for good: on their entity, or where they stood when they left.
static void CoF_MomentStopRing( edict_t *call, edict_t *player )
{
	if( !player || !SV_IsValidEdict( player )) return;
	SV_StartSound( player, CHAN_AUTO, call->v.iuser1 ? "phone/phone_ringing.wav" : "phone/phone_ringtone.wav",
		0.0f, ATTN_NONE, SND_STOP, PITCH_NORM );
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

// Who should take a call: the one it was for, or the nearest partner who can. keep false: never the one it was for
// (1.1.9, AI2: it rang them unanswered too long).
static edict_t *CoF_MomentCallee( edict_t *previous, qboolean keep )
{
	edict_t *best = NULL;
	float best_distance = 0.0f;
	int i;
	if( keep && previous && CoF_MomentCanAnswer( previous )) return previous;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		edict_t *player = svs.clients[i].edict;
		vec3_t delta;
		float distance = 0.0f;
		if( player == previous || !CoF_MomentCanAnswer( player )) continue;
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
	edict_t *busy[32]; // who running calls ring or talk to (1.1.9, S19)
	int i, k, busy_count = 0;
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
		if( call->v.nextthink > 0.0f && busy_count + 2 <= (int)( sizeof( busy ) / sizeof( busy[0] )))
		{
			busy[busy_count++] = CoF_MomentHandle( pv, COF_CALL_PLAYER );
			busy[busy_count++] = CoF_MomentHandle( pv, COF_CALL_USER );
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
			if( think != base + COF_CALL_WAIT )
			{
				cof_moment_calls[slot].ringing = NULL; // answered: a ring after it counts from its own start
				continue;
			}
			if( cof_moment_calls[slot].ringing != ringing )
			{
				cof_moment_calls[slot].ringing = ringing;
				cof_moment_calls[slot].ringing_since = sv.time;
			}
			// Still ringing for someone who cannot take it: ring a player who can. 1.1.9 (AI2): or ringing one
			// person unanswered for 20 s (AFK, c_park3 sophiecall): ring the nearest other who can take it.
			if( !CoF_MomentCanAnswer( ringing )) callee = CoF_MomentCallee( ringing, true );
			else if( sv.time - cof_moment_calls[slot].ringing_since >= COF_CALL_UNANSWERED ) callee = CoF_MomentCallee( ringing, false );
			else callee = NULL;
			if( callee )
			{
				CoF_MomentStopRing( call, ringing );
				CoF_MomentClearRing( ringing );
				CoF_MomentSetHandle( pv, COF_CALL_PLAYER, callee );
				pv[COF_CALL_RANG] = 0; // the new callee hears it ring
				CoF_MomentTellCall( slot, callee );
				cof_moment_calls[slot].ringing = callee;
				cof_moment_calls[slot].ringing_since = sv.time;
				Con_Printf( "cofstory phase=call-retargeted call=%s entity=%d\n", SV_GetString( call->v.targetname ), NUM_FOR_EDICT( callee ));
			}
			continue;
		}
		// The game dropped it: its think is kept but nothing is scheduled.
		cof_moment_calls[slot].ringing = NULL;
		if( !cof_moment_calls[slot].dropped_at )
		{
			cof_moment_calls[slot].dropped_at = sv.time;
			Con_Printf( "cofstory phase=call-dropped call=%s\n", SV_GetString( call->v.targetname ));
			CoF_MomentStopRing( call, ringing ); // dropped while ringing: the loop is still on them
		}
		CoF_MomentClearCall( CoF_MomentHandle( pv, COF_CALL_USER ));
		CoF_MomentClearCall( ringing );
		CoF_MomentClearRing( ringing );
		if( sv.time < cof_moment_calls[slot].next_restart ) continue;
		if(( callee = CoF_MomentCallee( ringing, true )) != NULL )
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
		byte *pv = NULL;
		int van, j;
		if( svs.clients[k].state == cs_spawned && SV_IsValidEdict( player ) && !CoF_MomentAlive( player ))
			CoF_MomentClearCall( player );
		// 1.1.9 (S19): a checkpoint saved in a 911/112 call (PostThink ends it by its step, which is not saved) loads
		// with m_bInPhoneCall and nothing to end it: all their commands were ignored for good. In a call of no
		// running cof_phonecall and no 911/112 step for 2 s: cleared.
		if( svs.clients[k].state != cs_spawned || !SV_IsValidEdict( player ) || !( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE ))) continue;
		memcpy( &van, pv + COF_PL_VAN_CALL, sizeof( van ));
		for( j = 0; j < busy_count && busy[j] != player; j++ );
		if( cof_moment_orphans[k].userid != svs.clients[k].userid )
		{
			cof_moment_orphans[k].userid = svs.clients[k].userid;
			cof_moment_orphans[k].since = 0.0;
		}
		if( !pv[COF_PL_IN_CALL] || van || j < busy_count )
		{
			cof_moment_orphans[k].since = 0.0;
			continue;
		}
		if( !cof_moment_orphans[k].since ) cof_moment_orphans[k].since = sv.time;
		else if( sv.time - cof_moment_orphans[k].since >= COF_CALL_ORPHAN )
		{
			CoF_MomentClearCall( player );
			cof_moment_orphans[k].since = 0.0;
		}
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

// 1.2.6: the player's camera model is this ladder's (Use: SET_MODEL( pDeathScene, the manager's model )). A manager
// keeps its last climber as owner for good (CoF_Ladder.cpp:539), and a strangle or a model cutscene freezes them with
// another camera model: every ladder they had climbed in the map read as busy for the rest of the party meanwhile.
static qboolean CoF_MomentClimbsThis( edict_t *player, edict_t *manager )
{
	int i;
	if( !manager->v.model ) return false;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *model = SV_EdictNum( i );
		if( CoF_MomentClass( model, "camera_model" ) && model->v.owner == player && model->v.model &&
			!Q_stricmp( SV_GetString( model->v.model ), SV_GetString( manager->v.model )))
			return true;
	}
	return false;
}

// Ladders being climbed: a manager whose owner is mid-climb on it, and the use brushes that start it.
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
		if( !CoF_MomentClimbing( climber->edict ) || !CoF_MomentClimbsThis( climber->edict, manager )) continue;
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

// The camera model of the ladder this player climbs: theirs when a ladder they started (its owner) has that model
// (Use: SET_MODEL( pDeathScene, the manager's model )); a strangle or a model cutscene has another.
static edict_t *CoF_MomentLadderModel( edict_t *player )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *model = SV_EdictNum( i );
		if( !CoF_MomentClass( model, "camera_model" ) || model->v.owner != player || !model->v.model ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *manager = SV_EdictNum( j );
			if( CoF_MomentClass( manager, "cof_ladder_manager" ) && manager->v.owner == player && manager->v.model &&
				!Q_stricmp( SV_GetString( manager->v.model ), SV_GetString( model->v.model )))
				return model;
		}
	}
	return NULL;
}

// 1.1.9 (ladders): a held forward or back climbs on. LadderThink reads them from the camera model's buttons
// (pDeathScene->pev->button, CoF_Ladder.cpp), which only the reliable forwardpressed/backpressed commands set:
// a step waited a round trip or more, and a key pressed before the client knew of the climb was never sent. The
// climber is frozen, so SV_ParseClientMove drops their usercmd buttons after this. As with those commands, a key
// already down when the climb began counts once it was let go (forward still held from walking to a ladder's top
// would take the climber straight back off it).
void SV_CoF_MomentsLadderInput( sv_client_t *cl, int buttons )
{
	const unsigned int keys = IN_FORWARD | IN_BACK;
	edict_t *model;
	int slot;
	if( !cl || !SV_CoF_StoryCoopEnabled() || FBitSet( cl->flags, FCL_FAKECLIENT )) return;
	slot = (int)( cl - svs.clients );
	if( slot < 0 || slot >= MAX_CLIENTS ) return;
	model = CoF_MomentClimbing( cl->edict ) ? CoF_MomentLadderModel( cl->edict ) : NULL;
	if( !model )
	{
		cof_moment_climbs[slot].model = 0;
		return;
	}
	if( cof_moment_climbs[slot].model != NUM_FOR_EDICT( model ) || cof_moment_climbs[slot].serial != model->serialnumber )
	{
		// a new climb: the keys down now wait to be let go
		cof_moment_climbs[slot].model = NUM_FOR_EDICT( model );
		cof_moment_climbs[slot].serial = model->serialnumber;
		cof_moment_climbs[slot].armed = 0;
	}
	cof_moment_climbs[slot].armed |= keys & ~(unsigned int)buttons;
	model->v.button = (int)(( (unsigned int)model->v.button & ~keys ) | ( (unsigned int)buttons & keys & cof_moment_climbs[slot].armed ));
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

// A multi_manager laid out as read here: Spawn sorts its delays, so a list out of order is not the delay list.
static qboolean CoF_MomentManagerLaidOut( const byte *pv )
{
	int count, k;
	float delay, last = -100000.0f;
	memcpy( &count, pv + COF_MM_COUNT, sizeof( count ));
	if( count < 0 || count > COF_MM_TARGETS ) return false;
	for( k = 0; k < count; k++ )
	{
		memcpy( &delay, pv + COF_MM_DELAYS + k * sizeof( delay ), sizeof( delay ));
		if( !( delay >= last ) || delay > 100000.0f || ( delay != 0.0f && delay > -0.001f && delay < 0.001f )) return false;
		last = delay;
	}
	return true;
}

// The living person nearest the leaver (never a companion), or NULL.
static edict_t *CoF_MomentHeir( sv_client_t *leaver )
{
	edict_t *best = NULL;
	float best_d = 0.0f;
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		vec3_t delta;
		float d;
		if( cl == leaver || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			cl->edict == leaver->edict || !CoF_MomentAlive( cl->edict )) continue;
		VectorSubtract( cl->edict->v.origin, leaver->edict->v.origin, delta );
		d = DotProduct( delta, delta );
		if( !best || d < best_d ) { best = cl->edict; best_d = d; }
	}
	return best;
}

// 1.1.9 (S2): a delayed chain fires with the activator that started it, kept by a multi_manager in its own EHANDLE
// (ManagerUse sets it, ManagerThink fires every target with it) and by a delayed trigger in the DelayedUse it makes
// (CBaseDelay, DelayThink). The player who leaves stayed in them: c_ending1-4's house teleport (cof_teleport
// teledes_tele, 27 s after its trigger) moves only a player activator, so with its toucher gone nobody left the house.
// Such a handle (exactly the leaver's edict and serial) now names the living person nearest the leaver.
static void CoF_MomentHandOver( sv_client_t *cl, edict_t *ent, byte *pv, int offset, edict_t **heir, qboolean *looked )
{
	if( CoF_MomentHandle( pv, offset ) != cl->edict ) return;
	if( !*looked ) { *heir = CoF_MomentHeir( cl ); *looked = true; }
	if( !*heir ) return;
	CoF_MomentSetHandle( pv, offset, *heir );
	Con_Printf( "cofstory phase=activator-handed userid=%d entity=%d class=%s to=%d\n", cl->userid, NUM_FOR_EDICT( ent ),
		SV_GetString( ent->v.classname ), NUM_FOR_EDICT( *heir ));
}

void SV_CoF_MomentsDrop( sv_client_t *cl )
{
	int i;
	uint32_t none = 0;
	edict_t *heir = NULL;
	qboolean looked = false;
	if( !cl || !SV_IsValidEdict( cl->edict ) || !svgame.edicts || !SV_CoF_StoryCoopEnabled() ) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		byte *pv = NULL;
		float delay;
		if( CoF_MomentClass( ent, "cof_phonecall" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_CALL_SIZE )))
		{
			uint32_t think;
			memcpy( &think, pv + COF_THINK, sizeof( think ));
			if( SV_CoF_StoryBase() && think == SV_CoF_StoryBase() + COF_CALL_WAIT && CoF_MomentHandle( pv, COF_CALL_PLAYER ) == cl->edict )
				CoF_MomentStopRing( ent, cl->edict );
			continue;
		}
		if( cl->edict->pvPrivateData && CoF_MomentClass( ent, "boat" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_BOAT_SIZE )))
		{
			CoF_MomentBoatDrop( cl, ent, pv );
			continue;
		}
		if( CoF_MomentClass( ent, "multi_manager" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_MM_ACTIVATOR_SIZE )) &&
			CoF_MomentManagerLaidOut( pv ))
		{
			CoF_MomentHandOver( cl, ent, pv, COF_MM_ACTIVATOR, &heir, &looked );
			continue;
		}
		if( CoF_MomentClass( ent, "DelayedUse" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_DELAY_SIZE )))
		{
			memcpy( &delay, pv + COF_DELAY_DELAY, sizeof( delay ));
			if( delay == 0.0f ) CoF_MomentHandOver( cl, ent, pv, COF_DELAY_ACTIVATOR, &heir, &looked ); // SUB_UseTargets makes it with none
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
	// 1.1.9 (S18): the credits (sent once, to the players there): the first player's copy of each Use is kept.
	if( name && !Q_strcmp( name, "Credits" ) && payload && size > 0 && !cof_moment_mirroring &&
		SV_CoF_StoryCoopEnabled() && ( cl = CoF_MomentClient( to )))
	{
		// a new Use: another frame, or this player has the last one already
		if( !cof_moment_credits.size || cof_moment_credits.time != sv.time || cof_moment_credits.userids[cl - svs.clients] == cl->userid )
		{
			memset( &cof_moment_credits, 0, sizeof( cof_moment_credits ));
			if( size > COF_MOMENT_CREDITS_SIZE ) return; // not kept: nothing old is sent instead
			memcpy( cof_moment_credits.payload, payload, size );
			cof_moment_credits.size = size;
			cof_moment_credits.time = sv.time;
		}
		cof_moment_credits.userids[cl - svs.clients] = cl->userid;
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

// 1.1.9 (S18): cof_credits shows the credits (and at the end their Continue) to the players there, once: a person who
// joined during the ending sat in its camera with none. Who has not got the last credits gets a copy of the first
// player's (their statistics with it: the screen shows only them).
static void CoF_MomentCredits( void )
{
	int j;
	if( !cof_moment_credits.size ) return;
	cof_moment_mirroring = true;
	for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
	{
		sv_client_t *cl = &svs.clients[j];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			cof_moment_credits.userids[j] == cl->userid ) continue;
		cof_moment_credits.userids[j] = cl->userid;
		SV_CoF_SendUserMessage( cl->edict, "Credits", cof_moment_credits.payload, cof_moment_credits.size, true );
		Con_Printf( "cofstory phase=credits-resent userid=%d\n", cl->userid );
	}
	cof_moment_mirroring = false;
}

// The section of a merged map a game message comes from: a scene's chain whose think runs (a multi_manager, a
// trigger_* by its bounds when it is a brush, a delayed trigger by its activator: it has no place of its own), else the
// player whose command or move runs; -1: not known (nothing is held then), as for any other entity's think (a phone
// call's lines are its callee's wherever they are).
static int CoF_MomentSourceSection( edict_t *sender )
{
	edict_t *thinker = SV_CoF_RescueThinker(), *activator = NULL;
	const char *classname;
	byte *pv = NULL;
	vec3_t center;
	if( thinker && SV_IsValidEdict( thinker ))
	{
		classname = SV_GetString( thinker->v.classname );
		if( CoF_MomentClient( thinker )) return SV_CoF_PlayerSection( thinker );
		if( Q_strcmp( classname, "multi_manager" ) && Q_strncmp( classname, "trigger_", 8 ) && Q_strcmp( classname, "DelayedUse" )) return -1;
		if( !Q_strcmp( classname, "DelayedUse" ))
		{
			if(( pv = SV_CoF_StoryEntityPrivate( thinker, COF_DELAY_SIZE )) != NULL && ( activator = CoF_MomentHandle( pv, COF_DELAY_ACTIVATOR )) != NULL &&
				CoF_MomentClient( activator )) return SV_CoF_PlayerSection( activator );
			return -1;
		}
		if( SV_GetString( thinker->v.model )[0] == '*' )
		{
			VectorAdd( thinker->v.absmin, thinker->v.absmax, center );
			VectorScale( center, 0.5f, center );
			return SV_CoF_PointSection( center );
		}
		return SV_CoF_PointSection( thinker->v.origin );
	}
	return sender && CoF_MomentClient( sender ) ? SV_CoF_PlayerSection( sender ) : -1;
}

// 1.1.9 (S5, K4 residual): in a merged map, the fade, black-and-white, chapter title and subtitle lines the game sends
// each player for a scene of one section (env_fade, cof_blackandwhite, cof_chapter, subtitle_main) reached the
// players in another, another original level. They are held back from a player in another section than where they
// come from, as a section's fog is (sv_cof_fog.c). Never what could leave a screen dark or grey: a fade reaches a player
// whose screen a fade left dark, black-and-white off always goes; neither "Game over" nor the engine's own lines.
qboolean SV_CoF_MomentsHeldBack( edict_t *to, int dest, const char *name, const byte *payload, int size, edict_t *sender )
{
	sv_client_t *cl = NULL;
	int slot, mine = -1, source = -1, flags = 0, i;
	qboolean fade, grey, chapter, line, held = false;
	if( !name || !payload || cof_moment_mirroring || !SV_CoF_StoryCoopEnabled() ) return false;
	fade = !Q_strcmp( name, "ScreenFade" ) && size == 10;
	grey = !Q_strcmp( name, "BlackWhite" ) && size == 1;
	chapter = !Q_strcmp( name, "Chapter" ) && size > 0;
	line = !Q_strcmp( name, "ProFont" ) && size > 0;
	if( !fade && !grey && !chapter && !line ) return false;
	if( fade ) flags = payload[4] | ( payload[5] << 8 );
	if(( dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE ) || !( cl = CoF_MomentClient( to )) || cl - svs.clients >= MAX_CLIENTS )
	{
		// to everyone: any screen may stay dark now
		if( fade && ( flags & COF_FADE_STAYOUT ) && dest != MSG_ONE && dest != MSG_ONE_UNRELIABLE )
			for( i = 0; i < MAX_CLIENTS; i++ ) cof_moment_screens[i].dark = true;
		return false;
	}
	slot = (int)( cl - svs.clients );
	if( cof_moment_screens[slot].userid != cl->userid )
	{
		memset( &cof_moment_screens[slot], 0, sizeof( cof_moment_screens[slot] ));
		cof_moment_screens[slot].userid = cl->userid;
		cof_moment_screens[slot].dark = true; // not known yet
	}
	if( !FBitSet( cl->flags, FCL_FAKECLIENT ) && ( mine = SV_CoF_PlayerSection( to )) >= 0 &&
		( source = CoF_MomentSourceSection( sender )) >= 0 && source != mine )
	{
		if( fade ) held = !cof_moment_screens[slot].dark;
		else if( grey ) held = payload[0] && !cof_moment_screens[slot].grey;
		else if( chapter ) held = !( size >= 10 && !memcmp( payload, "Game over", 10 ));
		else held = !CoF_StripOwnLine( payload, size );
		if( held && sv.time >= cof_moment_screens[slot].told )
		{
			cof_moment_screens[slot].told = sv.time + 2.0;
			Con_Printf( "cofstory phase=effect-held name=%s userid=%d section=%d from=%d\n", name, cl->userid, mine, source );
		}
	}
	if( held ) return true;
	if( fade ) cof_moment_screens[slot].dark = ( flags & COF_FADE_STAYOUT ) != 0;
	if( grey ) cof_moment_screens[slot].grey = payload[0];
	return false;
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

// The player whose chain switched this hurt on: the activator of a multi_manager that fires it, a living person.
static edict_t *CoF_MomentHurtStarter( const edict_t *hurt )
{
	const char *name = SV_GetString( hurt->v.targetname );
	int i, k, count;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *mm = SV_EdictNum( i ), *starter = NULL;
		sv_client_t *cl = NULL;
		byte *pv = NULL;
		string_t target;
		if( !CoF_MomentClass( mm, "multi_manager" ) || !( pv = SV_CoF_StoryEntityPrivate( mm, COF_MM_ACTIVATOR_SIZE )) ||
			!CoF_MomentManagerLaidOut( pv )) continue;
		memcpy( &count, pv + COF_MM_COUNT, sizeof( count ));
		for( k = 0; k < count; k++ )
		{
			memcpy( &target, pv + COF_MM_NAMES + k * sizeof( target ), sizeof( target ));
			if( target && !Q_strcmp( SV_GetString( target ), name )) break;
		}
		if( k == count || !( starter = CoF_MomentHandle( pv, COF_MM_ACTIVATOR )) || !( cl = CoF_MomentClient( starter )) ||
			FBitSet( cl->flags, FCL_FAKECLIENT ) || !CoF_MomentAlive( starter )) continue;
		return starter;
	}
	return NULL;
}

// 1.1.9 (P2, M2 finding 8): c_bridge's carriage trigger (trigger_once #200 -> destroyall -> multikuken2) drops the
// carriage the party arrived in and switches the trigger_hurt tjenahaha (1000) on under it 3 s later: in single
// player nobody can be in both places; a partner still inside died. When a lethal trigger_hurt is switched on by a
// chain a living person started who is not inside it, everyone else inside it is set down beside the nearest living
// person outside it. Players touch triggers only in their own moves, after this frame: none has been hurt yet. A
// trap whose own starter stands inside (single player's death) is left alone.
static void CoF_MomentHurts( void )
{
	int i, j, k;
	for( i = 0; i < cof_moment_hurt_count; i++ )
	{
		edict_t *hurt = SV_EdictNum( cof_moment_hurts[i].index ), *starter = NULL;
		int was = cof_moment_hurts[i].solid;
		if( !CoF_MomentClass( hurt, "trigger_hurt" ) || hurt->serialnumber != cof_moment_hurts[i].serial ) continue;
		cof_moment_hurts[i].solid = hurt->v.solid;
		if( was == SOLID_TRIGGER || hurt->v.solid != SOLID_TRIGGER ) continue;
		if( !( starter = CoF_MomentHurtStarter( hurt )) || CoF_MomentTouches( hurt, starter->v.origin )) continue;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *cl = &svs.clients[j], *anchor = NULL;
			float best = 0.0f;
			qboolean moved;
			if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
				cl->edict == starter || !CoF_MomentAlive( cl->edict ) || !CoF_MomentTouches( hurt, cl->edict->v.origin )) continue;
			for( k = 0; k < svs.maxclients && k < MAX_CLIENTS; k++ )
			{
				sv_client_t *other = &svs.clients[k];
				vec3_t delta;
				float d;
				if( other == cl || other->state != cs_spawned || FBitSet( other->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( other->edict ) ||
					!CoF_MomentAlive( other->edict ) || CoF_MomentTouches( hurt, other->edict->v.origin )) continue;
				VectorSubtract( other->edict->v.origin, cl->edict->v.origin, delta );
				d = DotProduct( delta, delta );
				if( !anchor || d < best ) { anchor = other; best = d; }
			}
			if( !anchor ) continue; // the starter at least is outside: not reached
			moved = SV_CoF_PartySectionMove( cl, anchor->edict->v.origin, anchor, NULL ) && !CoF_MomentTouches( hurt, cl->edict->v.origin );
			Con_Printf( "cofstory phase=hurt-escaped userid=%d hurt=%d anchor=%d ok=%d\n", cl->userid, cof_moment_hurts[i].index, anchor->userid, moved );
		}
	}
}

// 1.1.9 (S13): a camera's end and a model cutscene's end give control back to every living player (EnableControl,
// D/dlls/triggers.cpp FollowTarget, effects.cpp cof_mdlcutscene): a ladder climber was set free mid-climb in the
// ladder's camera, a boat rider walked off on the shore while the boat still had them. One held last frame by the same
// climb (its manager, the same camera model) or the same boat, now free, is frozen again; the ladder's end and
// Disembark clear what holds them in the same think, so their own release is never undone.
static void CoF_MomentKeepHeld( void )
{
	int i, j, manager[MAX_CLIENTS], boat[MAX_CLIENTS];
	memset( manager, 0, sizeof( manager ));
	memset( boat, 0, sizeof( boat ));
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		sv_client_t *cl = NULL;
		byte *pv = NULL;
		uint32_t carried;
		if( CoF_MomentClass( ent, "cof_ladder_manager" ) && ( cl = CoF_MomentClient( ent->v.owner )) != NULL && cl - svs.clients < MAX_CLIENTS )
			manager[cl - svs.clients] = i;
		else if( CoF_MomentClass( ent, "boat" ) && ( pv = SV_CoF_StoryEntityPrivate( ent, COF_BOAT_SIZE )) != NULL )
		{
			memcpy( &carried, pv + COF_BOAT_RIDER, sizeof( carried ));
			for( j = 0; carried && j < svs.maxclients && j < MAX_CLIENTS; j++ )
			{
				byte *player = NULL;
				if( svs.clients[j].state != cs_spawned || !SV_IsValidEdict( svs.clients[j].edict ) ||
					(uint32_t)(uintptr_t)svs.clients[j].edict->pvPrivateData != carried ) continue;
				// on board: Embark set their m_pBoat (boarding's 1 s and Disembark are the boat's own)
				if(( player = SV_CoF_StoryEntityPrivate( svs.clients[j].edict, COF_PL_SIZE )) != NULL && CoF_MomentHandle( player, COF_PL_BOAT ) == ent )
					boat[j] = i;
			}
		}
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *ent = cl->edict;
		byte *pv = NULL;
		uint32_t model = 0;
		if( cl->state != cs_spawned || !SV_IsValidEdict( ent ) || !CoF_MomentAlive( ent ) || !( pv = SV_CoF_StoryEntityPrivate( ent, COF_PL_SIZE )))
		{
			memset( &cof_moment_held[i], 0, sizeof( cof_moment_held[i] ));
			continue;
		}
		memcpy( &model, pv + COF_PL_CAMERA_MODEL, sizeof( model ));
		if( !model ) manager[i] = 0; // no climb without the ladder's camera model
		if( cof_moment_held[i].userid == cl->userid && !( ent->v.flags & FL_FROZEN ) &&
			(( manager[i] && manager[i] == cof_moment_held[i].manager && model == cof_moment_held[i].model ) ||
			( boat[i] && boat[i] == cof_moment_held[i].boat )))
		{
			SetBits( ent->v.flags, FL_FROZEN );
			VectorClear( ent->v.velocity );
			if( !cof_moment_held[i].told )
				Con_Printf( "cofstory phase=held-refrozen userid=%d by=%s\n", cl->userid, boat[i] ? "boat" : "ladder" );
			cof_moment_held[i].told = true;
		}
		if( cof_moment_held[i].userid != cl->userid || !( ent->v.flags & FL_FROZEN ) || ( !manager[i] && !boat[i] ))
		{
			memset( &cof_moment_held[i], 0, sizeof( cof_moment_held[i] ));
			cof_moment_held[i].userid = cl->userid;
			if( !( ent->v.flags & FL_FROZEN )) continue;
		}
		cof_moment_held[i].manager = manager[i];
		cof_moment_held[i].model = model;
		cof_moment_held[i].boat = boat[i];
	}
}

// The boss a partner's bar is for, still alive.
static qboolean CoF_MomentBossAlive( string_t name )
{
	int i;
	const char *wanted = name ? SV_GetString( name ) : "";
	for( i = svs.maxclients + 1; wanted[0] && i < svgame.numEntities; i++ )
	{
		edict_t *boss = SV_EdictNum( i );
		if( SV_IsValidEdict( boss ) && !( boss->v.flags & FL_KILLME ) && boss->v.health > 0 && !Q_strcmp( SV_GetString( boss->v.targetname ), wanted ))
			return true;
	}
	return false;
}

// 1.1.9 (S11): cof_bosshealthbar gives every player the bar (P+0x5F4..0x60B) and the boss's damage keeps it up for
// each player its name is on, but Spawn clears the bar's flag and the respawn's InitHUD sends BossBar 0: a player who
// died in a boss fight saw no bar until the boss was hit again. A respawned person gets a partner's bar back, while
// that partner's boss lives.
static void CoF_MomentBossBars( void )
{
	int i, j;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		byte *pv = NULL, *from = NULL, payload[192];
		string_t name;
		qboolean alive, respawned;
		int length = 0, value, k;
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
		alive = CoF_MomentAlive( cl->edict );
		respawned = cof_moment_lives[i].userid == cl->userid && !cof_moment_lives[i].alive && alive;
		cof_moment_lives[i].userid = cl->userid;
		cof_moment_lives[i].alive = alive;
		if( !respawned || !( pv = SV_CoF_StoryEntityPrivate( cl->edict, COF_PL_SIZE )) || pv[COF_PL_BOSS_BAR] ) continue;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS && !from; j++ )
		{
			sv_client_t *partner = &svs.clients[j];
			byte *ppv = NULL;
			if( partner == cl || partner->state != cs_spawned || !SV_IsValidEdict( partner->edict ) ||
				!( ppv = SV_CoF_StoryEntityPrivate( partner->edict, COF_PL_SIZE )) || !ppv[COF_PL_BOSS_BAR] ) continue;
			memcpy( &name, ppv + COF_PL_BOSS_NAME, sizeof( name ));
			if( CoF_MomentBossAlive( name )) from = ppv;
		}
		if( !from ) continue;
		memcpy( pv + COF_PL_BOSS_BAR, from + COF_PL_BOSS_BAR, COF_PL_BOSS_END - COF_PL_BOSS_BAR );
		// BossBar( byte 1, string empty, string full, short health, short max ), as UpdateClientData sends it
		payload[length++] = 1;
		for( k = 0; k < 2; k++ )
		{
			const char *tga;
			int size;
			memcpy( &name, pv + ( k ? COF_PL_BOSS_FULL : COF_PL_BOSS_EMPTY ), sizeof( name ));
			tga = name ? SV_GetString( name ) : "";
			size = (int)Q_strlen( tga ) + 1;
			if( length + size > (int)sizeof( payload ) - 4 ) break;
			memcpy( payload + length, tga, size );
			length += size;
		}
		if( k < 2 ) continue;
		memcpy( &value, pv + COF_PL_BOSS_HEALTH, sizeof( value ));
		payload[length++] = (byte)( value & 0xFF ); payload[length++] = (byte)(( value >> 8 ) & 0xFF );
		memcpy( &value, pv + COF_PL_BOSS_MAX, sizeof( value ));
		payload[length++] = (byte)( value & 0xFF ); payload[length++] = (byte)(( value >> 8 ) & 0xFF );
		SV_CoF_SendUserMessage( cl->edict, "BossBar", payload, length, true );
		Con_Printf( "cofstory phase=bossbar-restored userid=%d\n", cl->userid );
	}
}

void SV_CoF_MomentsFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	CoF_MomentCheckGeneration();
	CoF_MomentHurts();
	CoF_MomentBossBars();
	CoF_MomentKeepHeld();
	CoF_MomentRootsFired();
	CoF_MomentWheelchair();
	CoF_MomentLadders();
	CoF_MomentCalls();
	CoF_MomentLogos();
	CoF_MomentFades();
	CoF_MomentCredits();
}
