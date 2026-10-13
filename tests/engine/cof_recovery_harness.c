#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_snapshot.h"
#define Q_strlen strlen
#define Mem_Calloc(pool,size) calloc(1,size)
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_spawned 4
#define FCL_FAKECLIENT 128
#define DEAD_NO 0
#define FBitSet(value,bits) ((value) & (bits))
#define Q_stricmp _stricmp
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef struct { struct { float health; int deadflag; } v; int inventory; } edict_t;
typedef struct { int state, flags, userid; char name[32]; edict_t *edict; } sv_client_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { int numEntities; } svgame;
static sv_client_t clients[2];
static edict_t edicts[3];
static int gate = 1, transition, captures, restores, fail_restore, reconciled;
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_CoF_TransitionActive( void ) { return transition; }
static qboolean SV_IsValidEdict( edict_t *entity ) { return entity != NULL; }
static edict_t *SV_EdictNum( int index ) { return &edicts[index]; }
static void SV_FreeEdict( edict_t *entity ) { assert( 0 ); }
static qboolean SV_CoF_RetailOwnsWeapon( edict_t *player, edict_t *entity ) { return 0; }
static void Mem_Free( void *memory ) { free( memory ); }
static void Con_Printf( const char *format, ... ) {}
static qboolean SV_CoF_CaptureInventory( sv_client_t *client, byte **output, size_t *size )
{
	*output = malloc( sizeof( int )); *size = sizeof( int );
	memcpy( *output, &client->edict->inventory, *size ); captures++; return true;
}
static qboolean SV_CoF_RestoreInventory( sv_client_t *client, const byte *snapshot, size_t size )
{
	if( fail_restore ) return false;
	assert( size == sizeof( int )); memcpy( &client->edict->inventory, snapshot, size ); restores++; return true;
}
static void SV_CoF_ShareRecovered( sv_client_t *client ) { reconciled++; }
static void SV_CoF_ShareRemember( void ) {}
#include "recovery_under_test.c"

int main( void )
{
	svs.maxclients = 2; svs.clients = clients; svgame.numEntities = 3;
	clients[0].state = clients[1].state = cs_spawned;
	clients[0].userid = 1; clients[1].userid = 2;
	strcpy( clients[0].name, "Host" ); strcpy( clients[1].name, "Partner" );
	clients[0].edict = &edicts[1]; clients[1].edict = &edicts[2];
	edicts[1].v.health = edicts[2].v.health = 100;
	edicts[2].inventory = 73;
	SV_CoF_RecoveryInit(); SV_CoF_RecoveryFrame();
	assert( captures == 0 && restores == 0 );
	edicts[2].v.health = 0; SV_CoF_RecoveryFrame(); SV_CoF_RecoveryFrame();
	assert( captures == 1 );
	edicts[2].inventory = 0; edicts[2].v.health = 100;
	fail_restore = 1; SV_CoF_RecoveryFrame(); assert( edicts[2].inventory == 0 );
	fail_restore = 0; SV_CoF_RecoveryFrame(); SV_CoF_RecoveryFrame();
	assert( restores == 1 && edicts[2].inventory == 73 && reconciled == 1 );
	edicts[2].inventory = 91;
	SV_CoF_RecoveryDrop( &clients[1] );
	clients[1].userid = 8; edicts[2].inventory = 0;
	strcpy( clients[1].name, "Stranger" ); SV_CoF_RecoveryJoin( &clients[1] ); SV_CoF_RecoveryFrame();
	assert( edicts[2].inventory == 0 && restores == 1 );
	SV_CoF_RecoveryDrop( &clients[1] );
	clients[1].userid = 9; strcpy( clients[1].name, "Partner" );
	SV_CoF_RecoveryJoin( &clients[1] ); SV_CoF_RecoveryFrame();
	assert( edicts[2].inventory == 91 && restores == 2 );
	transition = 1; edicts[2].v.health = 0; SV_CoF_RecoveryFrame();
	assert( captures == 3 );
	SV_CoF_RecoveryReset();
	puts( "recovery lifecycle scenarios passed" );
	return 0;
}
