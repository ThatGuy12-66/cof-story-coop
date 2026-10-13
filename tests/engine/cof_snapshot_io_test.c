#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_snapshot.h"
typedef unsigned char byte;
typedef int qboolean;
typedef const char *string_t;
#define true 1
#define false 0
#define SAVE_HEAPSIZE 65536
#define SAVE_HASHSTRINGS 8
#define Q_strlen strlen
#define Mem_Calloc(pool,size) calloc(1,size)
#define Mem_Free free
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef struct edict_s edict_t;
struct edict_s { struct { int weapons; float health; } v; int free, owner, clip, ammo; edict_t *active; int slots[6]; const char *classname; };
typedef struct { edict_t *edict; } sv_client_t;
typedef struct { int id, location, size; string_t classname; edict_t *pent; } ENTITYTABLE;
typedef struct { char *pBaseData, *pCurrentData; char **pTokens; ENTITYTABLE *pTable; int size, tokenCount, tableCount, currentIndex, fUseLandmark; double time; } SAVERESTOREDATA;
static void save_weapon( edict_t *entity, SAVERESTOREDATA *saved );
static int restore_weapon( edict_t *entity, SAVERESTOREDATA *saved, int global );
static struct { void *pSaveData; } globals;
static struct { int numEntities; struct { void *pSaveData; } *globals; struct { void (*pfnSave)(edict_t *,SAVERESTOREDATA *); int (*pfnRestore)(edict_t *,SAVERESTOREDATA *,int); } dllFuncs; } svgame;
static struct { int maxclients; } svs;
static struct { double time; } sv;
static edict_t entities[32];
static int ready, fail_restore;
#define NUM_FOR_EDICT(entity) ((int)((entity)-entities))
static int SV_CoF_StoryCoopEnabled( void ) { return 1; }
static int SV_IsValidEdict( edict_t *entity ) { return entity && !entity->free; }
static edict_t *SV_EdictNum( int index ) { assert( index < 32 ); return &entities[index]; }
static string_t SV_AllocString( const char *name ) { return name; }
static const char *SV_GetString( string_t name ) { return name; }
static edict_t *SV_CreateNamedEntity( edict_t *entity, string_t name )
{
	if( !entity ) { assert( svgame.numEntities < 32 ); entity = &entities[svgame.numEntities++]; }
	entity->free = 0; entity->classname = name; return entity;
}
static void SV_FreeEdict( edict_t *entity ) { entity->free = 1; }
static SAVERESTOREDATA *SaveInit( size_t size, int tokens )
{
	SAVERESTOREDATA *saved = calloc( 1, sizeof( *saved ));
	assert( !svgame.globals->pSaveData ); svgame.globals->pSaveData = saved;
	saved->pBaseData = saved->pCurrentData = calloc( 1, size );
	saved->pTokens = calloc( (size_t)tokens, sizeof( char * ));
	saved->tokenCount = tokens; saved->pTokens[0] = "fields"; return saved;
}
static void SaveFinish( SAVERESTOREDATA *saved )
{
	free( saved->pBaseData ); free( saved->pTokens ); free( saved->pTable ); free( saved );
	svgame.globals->pSaveData = NULL;
}
static void InitEntityTable( SAVERESTOREDATA *saved, int count )
{
	int index;
	saved->tableCount = count; saved->pTable = calloc( (size_t)count, sizeof( ENTITYTABLE ));
	for( index = 0; index < count; index++ ) { saved->pTable[index].id = index; saved->pTable[index].pent = &entities[index]; }
}
static int SV_CoF_RetailOwnsWeapon( edict_t *player, edict_t *entity )
{
	return entity && !entity->free && entity->owner == NUM_FOR_EDICT( player );
}
static void SV_CoF_RetailInventoryReady( sv_client_t *client ) { ready++; }
static int SV_CoF_RetailInventoryIO( sv_client_t *client, SAVERESTOREDATA *saved, int restore )
{
	int data[8];
	if( !restore )
	{
		data[0] = client->edict->ammo;
		data[1] = client->edict->active ? NUM_FOR_EDICT( client->edict->active ) : 0;
		memcpy( data + 2, client->edict->slots, sizeof( client->edict->slots ));
		memcpy( saved->pCurrentData, data, sizeof( data ));
	}
	else
	{
		memcpy( data, saved->pCurrentData, sizeof( data ));
		client->edict->ammo = data[0];
		assert( data[1] > 0 && data[1] < saved->tableCount );
		client->edict->active = saved->pTable[data[1]].pent;
		memcpy( client->edict->slots, data + 2, sizeof( client->edict->slots ));
	}
	saved->size += sizeof( data ); saved->pCurrentData += sizeof( data );
	return 1;
}
static void save_weapon( edict_t *entity, SAVERESTOREDATA *saved )
{
	int data[2] = {entity->owner, entity->clip};
	ENTITYTABLE *entry = &saved->pTable[saved->currentIndex];
	memcpy( saved->pCurrentData, data, sizeof( data )); saved->pCurrentData += sizeof( data ); saved->size += sizeof( data );
	entry->classname = entity->classname; entry->size = sizeof( data );
}
static int restore_weapon( edict_t *entity, SAVERESTOREDATA *saved, int global )
{
	int data[2];
	if( fail_restore ) return -1;
	memcpy( data, saved->pCurrentData, sizeof( data ));
	entity->owner = NUM_FOR_EDICT( saved->pTable[data[0]].pent ); entity->clip = data[1];
	return 1;
}
#include "snapshot_io_under_test.c"

int main( void )
{
	sv_client_t source = {&entities[1]}, target = {&entities[2]};
	byte *snapshot;
	size_t size;
	int original_count;
	svgame.globals = (void *)&globals;
	svgame.dllFuncs.pfnSave = save_weapon; svgame.dllFuncs.pfnRestore = restore_weapon;
	svgame.numEntities = 5; svs.maxclients = 2; sv.time = 25;
	entities[1].v.weapons = 42; entities[1].ammo = 54; entities[1].slots[0] = 5; entities[1].slots[1] = 2;
	entities[1].active = &entities[3];
	entities[3].owner = 1; entities[3].clip = 17; entities[3].classname = "weapon_glock";
	entities[4].owner = 2; entities[4].clip = 2; entities[4].classname = "weapon_knife";
	entities[2].active = &entities[4]; entities[2].v.health = 72; entities[2].ammo = 1;
	assert( SV_CoF_CaptureInventory( &source, &snapshot, &size ));
	assert( CoF_SnapshotValid( snapshot, size ) && !globals.pSaveData );
	original_count = svgame.numEntities;
	snapshot[size - 1] ^= 1;
	assert( !SV_CoF_RestoreInventory( &target, snapshot, size ));
	assert( svgame.numEntities == original_count && !entities[4].free );
	snapshot[size - 1] ^= 1; fail_restore = 1;
	assert( !SV_CoF_RestoreInventory( &target, snapshot, size ));
	assert( !entities[4].free && entities[2].active == &entities[4] && entities[2].ammo == 1 );
	fail_restore = 0;
	assert( SV_CoF_RestoreInventory( &target, snapshot, size ));
	assert( entities[4].free && !entities[3].free && entities[2].active != &entities[3] );
	assert( entities[2].active->owner == 2 && entities[2].active->clip == 17 );
	assert( entities[2].ammo == 54 && entities[2].v.health == 72 && entities[2].v.weapons == 42 );
	assert( entities[2].slots[0] == 5 && entities[2].slots[1] == 2 && ready == 1 && !globals.pSaveData );
	free( snapshot ); puts( "inventory IO scenarios passed" ); return 0;
}
