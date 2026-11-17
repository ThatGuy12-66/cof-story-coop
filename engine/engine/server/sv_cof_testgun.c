/* Cry of Fear story co-op: a test gun. GPL-3.0-or-later.
 * For playing the campaign through to test it (cof_test_gun weapon_m16): every
 * person keeps that gun, and every gun they carry has infinite ammo through the
 * game's own unlockable switch (m_bInfiniteAmmo: no shot, syringe or flare is
 * spent and no gun needs reloading). A companion bot keeps its own kit.
 * The gun travels like any other weapon: a level change or a checkpoint keeps
 * it, and a respawn gives it back with the rest of the inventory
 * (sv_cof_recovery). When it is missing all the same (c_start, c_bridge and the
 * stairway clear the inventory; it was dropped), it is handed over again once
 * the player has stood free for a moment: never frozen, in a camera, a call,
 * a CPR or a struggle or in the wheelchair, never before a respawn's inventory
 * is back, and only while a slot stays free for the story's items.
 * The server clock starts again at every map (sv_init), so every timer here is
 * per map. Infinite ammo for everyone is a cheat: only with sv_cheats on (1.1.8;
 * the console is open in co-op, and the app writes sv_cheats 0). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_recovery.h"
#include "sv_cof_testgun.h"

#define COF_TESTGUN_TICK 0.5 // how often the players are looked after
#define COF_TESTGUN_SETTLE 2.0 // seconds a player stands free before a missing gun is handed over
#define COF_TESTGUN_RETRY 5.0
#define COF_TESTGUN_EQUIP 3.0 // after a gift, seconds it is taken in hand (a switch can be refused mid-shot)
#define COF_TESTGUN_ROOM 2 // free slots a gift needs: one for the gun, one left for a key

// The guns anyone can be given: not the TMP (a donator's only) nor the P345
// (the story asks for it and takes it back in c_asylum1).
static const char *cof_testgun_guns[] = { "weapon_m16", "weapon_shotgun", "weapon_vp70", "weapon_revolver", "weapon_rifle", "weapon_glock" };

typedef struct
{
	int userid;
	double free_since; // standing free (alive, not frozen) since; 0: not now
	double give_after, equip_until;
	qboolean infinite; // this module switched the game's infinite ammo on (it was off)
	qboolean told_full, told_wait; // "no room" / "waiting for the recovery" logged for this time it is missing
} cof_testgun_player_t;

static convar_t *cof_test_gun;
static cof_testgun_player_t cof_testgun_players[MAX_CLIENTS];
static int cof_testgun_generation = -1;
static double cof_testgun_time;
static char cof_testgun_refused[32]; // the setting last reported as not a gun
static qboolean cof_testgun_cheats_told; // "needs sv_cheats" logged while it is asked for

void SV_CoF_TestGunReset( void )
{
	memset( cof_testgun_players, 0, sizeof( cof_testgun_players ));
	cof_testgun_generation = -1;
	cof_testgun_time = 0.0;
	cof_testgun_refused[0] = 0;
	cof_testgun_cheats_told = false;
}

void SV_CoF_TestGunInit( void )
{
	cof_test_gun = Cvar_Get( "cof_test_gun", "", 0, "story co-op testing: every person keeps this gun (weapon_m16, shotgun...) and never runs out of ammo; empty: off" );
}

// The gun the setting names ("m16" or "weapon_m16"), or NULL: off ("", "0", "none") or not one of ours.
static const char *CoF_TestGunName( void )
{
	const char *value = cof_test_gun ? cof_test_gun->string : NULL;
	char name[48];
	int i;
	if( !value || !value[0] || !Q_strcmp( value, "0" ) || !Q_stricmp( value, "none" ))
	{
		cof_testgun_refused[0] = 0;
		cof_testgun_cheats_told = false;
		return NULL;
	}
	if( !Cvar_VariableInteger( "sv_cheats" ))
	{
		if( !cof_testgun_cheats_told ) Con_Printf( "cofstory phase=test-gun-refused reason=sv_cheats\n" );
		cof_testgun_cheats_told = true;
		return NULL;
	}
	cof_testgun_cheats_told = false;
	Q_snprintf( name, sizeof( name ), "%s%s", Q_strnicmp( value, "weapon_", 7 ) ? "weapon_" : "", value );
	for( i = 0; i < (int)ARRAYSIZE( cof_testgun_guns ); i++ )
	{
		if( Q_stricmp( name, cof_testgun_guns[i] )) continue;
		cof_testgun_refused[0] = 0;
		return cof_testgun_guns[i];
	}
	if( Q_strncmp( cof_testgun_refused, value, sizeof( cof_testgun_refused ) - 1 ))
	{
		Q_strncpy( cof_testgun_refused, value, sizeof( cof_testgun_refused ));
		Con_Printf( "cofstory phase=test-gun-refused class=%s\n", cof_testgun_refused );
	}
	return NULL;
}

// In ordinary play: on foot, not frozen, not in a camera, a call or the wheelchair,
// and in no player animation state (fall, CPR, phone, struggle, syringe).
static qboolean CoF_TestGunFree( sv_client_t *cl )
{
	edict_t *ent = cl->edict;
	return ent->v.movetype == MOVETYPE_WALK && !FBitSet( ent->v.flags, FL_FROZEN ) && ent->v.fuser3 == 0.0f &&
		( !cl->pViewEntity || cl->pViewEntity == ent ) && !SV_CoF_RetailInWheelchair( cl ) && !SV_CoF_RetailInPhoneCall( cl );
}

static void CoF_TestGunPlayer( sv_client_t *cl, cof_testgun_player_t *p, const char *gun )
{
	edict_t *ent = cl->edict;
	int carrying;
	if( !gun )
	{
		// Only a switch this module turned on: the game's own unlock and cof_infammo stay.
		if( p->infinite && SV_CoF_RetailInfiniteAmmo( cl, false ) >= 0 )
			Con_Printf( "cofstory phase=test-gun-off userid=%d\n", cl->userid );
		p->infinite = false;
		return;
	}
	if( !( ent->v.health > 0 ) || ent->v.deadflag != DEAD_NO )
	{
		p->free_since = 0.0;
		return;
	}
	if( SV_CoF_RetailInfiniteAmmo( cl, true ) == 0 ) p->infinite = true;
	SV_CoF_RetailSupply( cl ); // the switch again, and a magazine in reserve for every gun carried
	carrying = SV_CoF_RetailCarrying( cl, gun );
	if( carrying < 0 ) return;
	if( carrying > 0 )
	{
		p->free_since = 0.0; // a later loss waits its own moment
		p->told_full = p->told_wait = false;
		if( carrying == 2 ) p->equip_until = 0.0;
		else if( sv.time < p->equip_until ) SV_CoF_RetailEquip( cl, gun );
		return;
	}
	if( SV_CoF_RecoveryPending( cl ))
	{
		if( !p->told_wait ) Con_Printf( "cofstory phase=test-gun-wait userid=%d reason=recovery\n", cl->userid );
		p->told_wait = true;
		p->free_since = 0.0;
		return;
	}
	if( !CoF_TestGunFree( cl ))
	{
		p->free_since = 0.0;
		return;
	}
	if( !p->free_since ) p->free_since = sv.time;
	if( sv.time - p->free_since < COF_TESTGUN_SETTLE || sv.time < p->give_after ) return;
	p->give_after = sv.time + COF_TESTGUN_RETRY;
	if( SV_CoF_RetailFreeSlots( cl ) < COF_TESTGUN_ROOM )
	{
		if( !p->told_full ) Con_Printf( "cofstory phase=test-gun-full userid=%d class=%s\n", cl->userid, gun );
		p->told_full = true;
		return;
	}
	if( !SV_CoF_StoryGiveItem( cl, gun )) return;
	p->told_full = p->told_wait = false;
	p->equip_until = sv.time + COF_TESTGUN_EQUIP;
	SV_CoF_RetailSupply( cl );
	SV_CoF_RetailEquip( cl, gun );
	Con_Printf( "cofstory phase=test-gun-given userid=%d class=%s\n", cl->userid, gun );
}

void SV_CoF_TestGunFrame( void )
{
	const char *gun;
	int i;
	if( SV_CoF_TransitionActive()) return;
	if( cof_testgun_generation != svs.spawncount || cof_testgun_time - sv.time > COF_TESTGUN_TICK )
	{
		// A new map, and a clock that started again: everyone settles again before a gift.
		for( i = 0; i < MAX_CLIENTS; i++ )
			cof_testgun_players[i].free_since = cof_testgun_players[i].give_after = cof_testgun_players[i].equip_until = 0.0;
		cof_testgun_generation = svs.spawncount;
		cof_testgun_time = 0.0;
	}
	if( sv.time < cof_testgun_time ) return;
	cof_testgun_time = sv.time + COF_TESTGUN_TICK;
	gun = CoF_TestGunName();
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_testgun_player_t *p = &cof_testgun_players[i];
		// Kept by userid through a level change, while the client loads the next map.
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
		if( p->userid != cl->userid )
		{
			memset( p, 0, sizeof( *p ));
			p->userid = cl->userid;
		}
		CoF_TestGunPlayer( cl, p, gun );
	}
}
