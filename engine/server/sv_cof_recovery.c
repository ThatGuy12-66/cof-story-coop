#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_recovery.h"
#include "sv_cof_share.h"
#include "sv_cof_snapshot.h"

typedef struct
{
	char name[32];
	byte *snapshot;
	size_t size;
	int userid, pending;
} cof_recovery_record_t;

static cof_recovery_record_t cof_recovery[MAX_CLIENTS];
static struct { int userid, alive; } cof_recovery_life[MAX_CLIENTS];
static int cof_recovery_generation = -1;

void SV_CoF_RecoveryReset( void )
{
	int index;
	for( index = 0; index < MAX_CLIENTS; index++ ) if( cof_recovery[index].snapshot ) Mem_Free( cof_recovery[index].snapshot );
	memset( cof_recovery, 0, sizeof( cof_recovery ));
	memset( cof_recovery_life, 0, sizeof( cof_recovery_life ));
	cof_recovery_generation = -1;
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
	if( record->pending == 1 || CoF_RecoveryCapture( cl, record )) record->pending = 2;
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
	if( record->pending == 1 || CoF_RecoveryCapture( cl, record )) record->pending = 2;
	record->userid = 0;
}

void SV_CoF_RecoveryDropDone( sv_client_t *cl )
{
	int index;
	if( !cl || !cl->edict || !SV_CoF_StoryCoopEnabled()) return;
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
		if( SV_CoF_RetailOwnsWeapon( cl->edict, SV_EdictNum( index ))) SV_FreeEdict( SV_EdictNum( index ));
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
		if( !alive && record->pending != 1 && ( cof_recovery_life[slot].alive || cof_recovery_life[slot].userid != client->userid ))
		{
			SV_CoF_ShareRemember();
			if( CoF_RecoveryCapture( client, record )) record->pending = 1;
		}
		if( alive && record->pending && record->snapshot )
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
