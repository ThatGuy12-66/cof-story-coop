#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_recovery.h"
#include "sv_cof_share.h"
#include "sv_cof_snapshot.h"
#include "sv_cof_cheats.h"

#define COF_RECOVERY_PLAYER_SIZE 0x281C // the whole CBasePlayer
// The player's raw pointers to its weapons (the fields of sv_cof_retail_inventory.c): m_rgpPlayerItems[6] +0x2514,
// m_pActiveItem, m_pClientActiveItem, m_pLastItem, m_pNextItem, m_pStoredActionItem, up to m_rgAmmo +0x2540; and
// m_pStoredNightVisionWeapon +0x3F0.
#define COF_RECOVERY_ITEMS     0x2514
#define COF_RECOVERY_ITEMS_END 0x2540
#define COF_RECOVERY_NV_WEAPON 0x03F0
#define COF_RECOVERY_CARRIED   0x2530 // the buckets and m_pActiveItem: what RemoveAllItems always empties
#define COF_RECOVERY_LEFT      4
#define COF_RECOVERY_GRAB_REACH 128.0f

typedef struct
{
	char name[32];
	byte *snapshot;
	size_t size;
	int userid, pending; // pending 1: a death's, restored at the respawn; 2: a leaver's, at the rejoin; 3: a grab's (below)
} cof_recovery_record_t;

static cof_recovery_record_t cof_recovery[MAX_CLIENTS];
static struct { int userid, alive; } cof_recovery_life[MAX_CLIENTS];
static int cof_recovery_generation = -1;
static struct { byte *snapshot; size_t size; int userid; } cof_recovery_grab[MAX_CLIENTS];
static edict_t *cof_recovery_grabber;

static void CoF_RecoveryGrabClear( void )
{
	int index;
	for( index = 0; index < MAX_CLIENTS; index++ ) if( cof_recovery_grab[index].snapshot ) Mem_Free( cof_recovery_grab[index].snapshot );
	memset( cof_recovery_grab, 0, sizeof( cof_recovery_grab ));
	cof_recovery_grabber = NULL;
}

void SV_CoF_RecoveryReset( void )
{
	int index;
	for( index = 0; index < MAX_CLIENTS; index++ ) if( cof_recovery[index].snapshot ) Mem_Free( cof_recovery[index].snapshot );
	memset( cof_recovery, 0, sizeof( cof_recovery ));
	memset( cof_recovery_life, 0, sizeof( cof_recovery_life ));
	cof_recovery_generation = -1;
	CoF_RecoveryGrabClear();
}

static cof_recovery_record_t *CoF_RecoveryRecord( sv_client_t *cl, qboolean create )
{
	int index, empty = -1;
	if( !cl || !cl->name[0] ) return NULL;
	for( index = 0; index < MAX_CLIENTS; index++ )
	{
		cof_recovery_record_t *record = &cof_recovery[index];
		if( !record->name[0] && empty < 0 ) empty = index;
		if( record->userid == cl->userid && cl->userid )
		{
			Q_strncpy( record->name, cl->name, sizeof( record->name ));
			return record;
		}
	}
	for( index = 0; index < MAX_CLIENTS; index++ )
		if( !cof_recovery[index].userid && !Q_stricmp( cof_recovery[index].name, cl->name )) return &cof_recovery[index];
	if( !create || empty < 0 ) return NULL;
	Q_strncpy( cof_recovery[empty].name, cl->name, sizeof( cof_recovery[empty].name ));
	cof_recovery[empty].userid = cl->userid;
	return &cof_recovery[empty];
}

static qboolean CoF_RecoveryCapture( sv_client_t *cl, cof_recovery_record_t *record )
{
	byte *snapshot = NULL;
	size_t size = 0;
	if( !record || !SV_CoF_CaptureInventory( cl, &snapshot, &size )) return false;
	if( record->snapshot ) Mem_Free( record->snapshot );
	record->snapshot = snapshot; record->size = size;
	Con_Printf( "cofstory phase=inventory-captured userid=%d bytes=%u\n", cl->userid, (unsigned int)size );
	return true;
}

void SV_CoF_RecoveryDrop( sv_client_t *cl )
{
	cof_recovery_record_t *record;
	if( !cl || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_StoryCoopEnabled()) return;
	SV_CoF_ShareRemember();
	record = CoF_RecoveryRecord( cl, true );
	if( !record ) return;
	if( record->pending == 1 || record->pending == 3 || CoF_RecoveryCapture( cl, record )) record->pending = 2;
	record->userid = 0;
}

// A player a story level change carried over who leaves before signing on in the new map (a crash, a timeout while
// loading): the restored player still holds what they carried. Without this, they came back with only the shared
// starting weapons (host log 2026-10-20: a joiner's crash in the c_city -> c_sewer3 change).
void SV_CoF_RecoveryDropCarried( sv_client_t *cl )
{
	cof_recovery_record_t *record;
	if( !cl || cl->state == cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_StoryCoopEnabled() || !SV_IsValidEdict( cl->edict )) return;
	SV_CoF_ShareRemember();
	record = CoF_RecoveryRecord( cl, true );
	if( !record ) return;
	if( record->pending == 1 || record->pending == 3 || CoF_RecoveryCapture( cl, record )) record->pending = 2;
	record->userid = 0;
}

// The weapons the story has one of and needs (1.1.8, K7): c_asylum1's hand-over asks for the P345 (one, in
// c_asylum2) and c_subway2st2's wall breaks only to the sledgehammer (c_subway2st3). A leaver took theirs along.
static const char *CoF_RecoveryUnique( const char *classname )
{
	static const char *unique[] = { "weapon_p345", "weapon_sledgehammer" };
	int i;
	for( i = 0; i < (int)ARRAYSIZE( unique ); i++ )
		if( !Q_strcmp( classname, unique[i] )) return unique[i];
	return NULL;
}

// A new world copy of such a weapon where the leaver stood, picked up with +use as the map's own.
static void CoF_RecoveryLeave( const sv_client_t *cl, const char *classname, const vec3_t at )
{
	edict_t *ent = SV_CreateNamedEntity( NULL, SV_AllocString( classname ));
	if( !SV_IsValidEdict( ent )) return;
	if( !ent->pvPrivateData )
	{
		SV_FreeEdict( ent );
		return;
	}
	VectorCopy( at, ent->v.origin );
	SetBits( ent->v.spawnflags, BIT( 30 )); // SF_NORESPAWN, as GiveNamedItem does
	if( svgame.dllFuncs.pfnSpawn( ent ) == -1 || ent->free || FBitSet( ent->v.flags, FL_KILLME ))
	{
		if( !ent->free ) SV_FreeEdict( ent );
		return;
	}
	Con_Printf( "cofstory phase=weapon-left userid=%d weapon=%s entity=%d\n", cl->userid, classname, NUM_FOR_EDICT( ent ));
}

void SV_CoF_RecoveryDropDone( sv_client_t *cl )
{
	const char *left[COF_RECOVERY_LEFT];
	int index, owned = 0, count = 0;
	byte *player;
	uint32_t none = 0;
	vec3_t at;
	if( !cl || !cl->edict || !SV_CoF_StoryCoopEnabled()) return;
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
	{
		edict_t *weapon = SV_EdictNum( index );
		const char *unique;
		if( !SV_CoF_RetailOwnsWeapon( cl->edict, weapon )) continue;
		owned++;
		unique = CoF_RecoveryUnique( SV_GetString( weapon->v.classname ));
		if( unique && count < COF_RECOVERY_LEFT && !FBitSet( cl->flags, FCL_FAKECLIENT )) left[count++] = unique;
	}
	if( !owned ) return;
	// 1.1.8 (E2-3): a fast reconnect and a drop while loading a level change never ran the game's ClientDisconnect
	// (whose RemoveAllItems empties these): the player object, which stays until the slot is used again, kept
	// pointers to the weapons freed below (its Killed and the next level change's Save follow them). Let go of
	// them first; after a ClientDisconnect only the two stored ones are still set.
	if( !( player = SV_CoF_StoryEntityPrivate( cl->edict, COF_RECOVERY_PLAYER_SIZE ))) return;
	memset( player + COF_RECOVERY_ITEMS, 0, COF_RECOVERY_ITEMS_END - COF_RECOVERY_ITEMS );
	memcpy( player + COF_RECOVERY_NV_WEAPON, &none, sizeof( none ));
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
		if( SV_CoF_RetailOwnsWeapon( cl->edict, SV_EdictNum( index ))) SV_FreeEdict( SV_EdictNum( index ));
	// Not while a story level change restores the map: its tickets own the edict numbers until everyone is in.
	if( !count || sv.state != ss_active || SV_CoF_TransitionActive()) return;
	VectorCopy( cl->edict->v.origin, at );
	for( index = 0; index < count; index++ ) CoF_RecoveryLeave( cl, left[index], at );
}

// 1.1.8 (E2-4): the chainsaw boss's hit spawns a third_person_interaction whose Spawn strips the human it holds
// (RemoveAllItems( FALSE ), CoF_ThirdPersonInteraction.cpp:76-84) seconds before its kill, so the inventory taken
// at the death was empty. Around each boss think (sv_phys.c): the humans in its reach are captured before; the one
// the think strips (frozen, its weapons gone) keeps that capture (pending 3) for the respawn after the kill.
static qboolean CoF_RecoveryArmed( sv_client_t *cl )
{
	byte *player = SV_CoF_StoryEntityPrivate( cl->edict, COF_RECOVERY_PLAYER_SIZE );
	int index;
	if( !player ) return false;
	for( index = COF_RECOVERY_ITEMS; index < COF_RECOVERY_CARRIED; index++ ) if( player[index] ) return true;
	return false;
}

void SV_CoF_RecoveryThinkBegin( edict_t *thinker )
{
	int slot;
	if( !thinker || Q_strcmp( SV_GetString( thinker->v.classname ), "monster_bosschainsaw" ) || !SV_CoF_StoryCoopEnabled() ||
		SV_CoF_TransitionActive() || svgame.globals->pSaveData ) return;
	CoF_RecoveryGrabClear();
	cof_recovery_grabber = thinker;
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *cl = &svs.clients[slot];
		vec3_t delta;
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			cl->edict->v.health <= 0 || cl->edict->v.deadflag != DEAD_NO || FBitSet( cl->edict->v.flags, FL_FROZEN )) continue;
		VectorSubtract( cl->edict->v.origin, thinker->v.origin, delta );
		if( DotProduct( delta, delta ) > COF_RECOVERY_GRAB_REACH * COF_RECOVERY_GRAB_REACH || !CoF_RecoveryArmed( cl )) continue;
		if( SV_CoF_CaptureInventory( cl, &cof_recovery_grab[slot].snapshot, &cof_recovery_grab[slot].size ))
			cof_recovery_grab[slot].userid = cl->userid;
	}
}

void SV_CoF_RecoveryThinkEnd( void )
{
	int slot;
	if( !cof_recovery_grabber ) return;
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *cl = &svs.clients[slot];
		cof_recovery_record_t *record;
		if( !cof_recovery_grab[slot].snapshot || cl->state != cs_spawned || cl->userid != cof_recovery_grab[slot].userid ||
			!SV_IsValidEdict( cl->edict ) || !FBitSet( cl->edict->v.flags, FL_FROZEN ) || CoF_RecoveryArmed( cl )) continue;
		if( !( record = CoF_RecoveryRecord( cl, true )) || record->pending ) continue;
		if( record->snapshot ) Mem_Free( record->snapshot );
		record->snapshot = cof_recovery_grab[slot].snapshot; record->size = cof_recovery_grab[slot].size;
		cof_recovery_grab[slot].snapshot = NULL;
		record->pending = 3;
		Con_Printf( "cofstory phase=inventory-captured userid=%d bytes=%u reason=grab\n", cl->userid, (unsigned int)record->size );
	}
	CoF_RecoveryGrabClear();
}

void SV_CoF_RecoveryJoin( sv_client_t *cl )
{
	cof_recovery_record_t *record;
	if( !SV_CoF_StoryCoopEnabled()) return;
	record = CoF_RecoveryRecord( cl, false );
	if( record && record->pending == 2 ) record->userid = cl->userid;
}

void SV_CoF_RecoveryFrame( void )
{
	int slot;
	if( !SV_CoF_StoryCoopEnabled() || SV_CoF_TransitionActive()) return;
	if( cof_recovery_generation != svs.spawncount )
	{
		memset( cof_recovery_life, 0, sizeof( cof_recovery_life ));
		cof_recovery_generation = svs.spawncount;
	}
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *client = &svs.clients[slot];
		cof_recovery_record_t *record;
		int alive;
		if( client->state != cs_spawned || FBitSet( client->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( client->edict )) continue;
		record = CoF_RecoveryRecord( client, true );
		if( !record ) continue;
		alive = client->edict->v.health > 0 && client->edict->v.deadflag == DEAD_NO;
		// A grab's capture (pending 3) waits for the kill, then serves as the death's; let go alive, it is given back.
		if( record->pending == 3 && ( !alive || !FBitSet( client->edict->v.flags, FL_FROZEN ))) record->pending = 1;
		if( !alive && record->pending != 1 && ( cof_recovery_life[slot].alive || cof_recovery_life[slot].userid != client->userid ))
		{
			SV_CoF_ShareRemember();
			if( CoF_RecoveryCapture( client, record )) record->pending = 1;
		}
		if( alive && record->pending && record->pending != 3 && record->snapshot )
		{
			if( SV_CoF_RestoreInventory( client, record->snapshot, record->size ))
			{
				SV_CoF_ShareRecovered( client );
				Con_Printf( "cofstory phase=inventory-restored userid=%d reason=%s\n", client->userid, record->pending == 1 ? "respawn" : "rejoin" );
				record->pending = 0;
				Mem_Free( record->snapshot ); record->snapshot = NULL; record->size = 0;
			}
		}
		record->userid = client->userid;
		cof_recovery_life[slot].userid = client->userid;
		cof_recovery_life[slot].alive = alive;
	}
}

qboolean SV_CoF_RecoveryPending( sv_client_t *cl )
{
	cof_recovery_record_t *record;
	if( !cl || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT )) return false;
	record = CoF_RecoveryRecord( cl, false );
	return record && record->pending && record->snapshot;
}

void SV_CoF_RecoveryInit( void )
{
	SV_CoF_RecoveryReset();
}

qboolean SV_CoF_RecoveryExport( int index, char name[32], const byte **snapshot, size_t *size )
{
	cof_recovery_record_t *record;
	if( index < 0 || index >= MAX_CLIENTS ) return false;
	record = &cof_recovery[index];
	if( !record->pending || !record->snapshot ) return false;
	Q_strncpy( name, record->name, 32 );
	*snapshot = record->snapshot; *size = record->size;
	return true;
}

qboolean SV_CoF_RecoveryImport( const char *name, const byte *snapshot, size_t size )
{
	int index;
	cof_recovery_record_t *record;
	if( !name || !name[0] || Q_strlen( name ) >= 32 || !CoF_SnapshotValid( snapshot, size )) return false;
	for( index = 0; index < MAX_CLIENTS; index++ )
		if( !Q_stricmp( cof_recovery[index].name, name )) break;
	if( index == MAX_CLIENTS )
		for( index = 0; index < MAX_CLIENTS; index++ ) if( !cof_recovery[index].name[0] ) break;
	if( index == MAX_CLIENTS ) return false;
	record = &cof_recovery[index];
	if( record->snapshot ) Mem_Free( record->snapshot );
	memset( record, 0, sizeof( *record ));
	Q_strncpy( record->name, name, sizeof( record->name ));
	record->snapshot = Mem_Calloc( host.mempool, size );
	memcpy( record->snapshot, snapshot, size );
	record->size = size; record->pending = 2;
	return true;
}
