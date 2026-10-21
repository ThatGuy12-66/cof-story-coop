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
#define cs_free 0
#define cs_spawned 4
#define FCL_FAKECLIENT 128
#define DEAD_NO 0
#include <stdarg.h>
#include <stdint.h>
#define FBitSet(value,bits) ((value) & (bits))
#define SetBits(value,bits) ((value) |= (bits))
#define BIT(n) ( 1U << ( n ))
#define ARRAYSIZE(x) ( sizeof( x ) / sizeof(( x )[0] ))
#ifdef _WIN32
#define Q_stricmp _stricmp
#else
#include <strings.h>
#define Q_stricmp strcasecmp
#endif
#define Q_strcmp strcmp
#define FL_FROZEN ( 1 << 12 )
#define FL_KILLME ( 1 << 30 )
#define FL_NOTARGET ( 1 << 7 )
#define DAMAGE_NO 0
#define DAMAGE_AIM 2
#define SOLID_NOT 0
#define SOLID_SLIDEBOX 3
#define ss_active 2
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef float vec3_t[3];
typedef int string_t;
/* owner: a weapon's m_pPlayer (the player object it belongs to) */
typedef struct { int free, serialnumber; void *pvPrivateData, *owner; struct { float health, takedamage; int deadflag, flags, spawnflags, solid; string_t classname; vec3_t origin; } v; int inventory; } edict_t;
typedef struct { int state, flags, userid; char name[32]; edict_t *edict; } sv_client_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { int state, loadgame; double time; } sv;
static struct { void *pSaveData; } globals;
static int fake_spawn( edict_t *ent );
static struct { int numEntities; struct { int ( *pfnSpawn )( edict_t *ent ); } dllFuncs; struct { void *pSaveData; } *globals; } svgame = { 0, { fake_spawn }, ( void * )&globals };
static sv_client_t clients[2];
static edict_t edicts[12];
static byte player_private[2][0x2820], weapon_private[8][0x200];
static const char *strings[] = { "", "weapon_glock", "weapon_p345", "weapon_sledgehammer", "monster_bosschainsaw", "player" };
static int gate = 1, transition, captures, restores, fail_restore, reconciled, frees, spawns, unsafe_frees, links;
static char console[4096];
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_CoF_TransitionActive( void ) { return transition; }
static qboolean SV_IsValidEdict( const edict_t *entity ) { return entity != NULL && !entity->free; }
static edict_t *SV_EdictNum( int index ) { return &edicts[index]; }
#define NUM_FOR_EDICT( e ) ((int)(( e ) - edicts ))
static const char *SV_GetString( string_t s ) { assert( s >= 0 && s < (int)ARRAYSIZE( strings )); return strings[s]; }
static string_t SV_AllocString( const char *text ) { string_t i; for( i = 0; i < (string_t)ARRAYSIZE( strings ); i++ ) if( !strcmp( strings[i], text )) return i; assert( 0 ); return 0; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return e && !e->free && need <= sizeof( player_private[0] ) ? e->pvPrivateData : NULL; }
/* A weapon is freed only once its player object points at no weapon any more: the buckets, active/client/last/next
 * item and stored action item (+0x2514..+0x2540) and the stored night-vision weapon (+0x3F0) are cleared first. */
static void SV_FreeEdict( edict_t *entity )
{
	int p, o;
	for( p = 0; p < 2; p++ )
	{
		if( entity->owner != player_private[p] ) continue;
		for( o = 0x2514; o < 0x2540; o++ ) if( player_private[p][o] ) unsafe_frees++;
		for( o = 0x3F0; o < 0x3F4; o++ ) if( player_private[p][o] ) unsafe_frees++;
	}
	entity->free = 1; entity->serialnumber++; frees++; /* as SV_FreeEdict: an old EHANDLE no longer matches */
}
static void SV_LinkEdict( edict_t *entity, qboolean touch ) { (void)entity; (void)touch; links++; }
static qboolean SV_CoF_RetailOwnsWeapon( edict_t *player, edict_t *entity )
{
	return SV_IsValidEdict( player ) && SV_IsValidEdict( entity ) && !FBitSet( entity->v.flags, FL_KILLME ) &&
		!strncmp( SV_GetString( entity->v.classname ), "weapon_", 7 ) && entity->owner && entity->owner == player->pvPrivateData;
}
static edict_t *SV_CreateNamedEntity( edict_t *unused, string_t classname )
{
	int i;
	for( i = 3; i < 12; i++ )
		if( edicts[i].free )
		{
			int serial = edicts[i].serialnumber;
			memset( &edicts[i], 0, sizeof( edicts[i] ));
			edicts[i].serialnumber = serial;
			edicts[i].pvPrivateData = weapon_private[i - 3]; edicts[i].v.classname = classname;
			if( i >= svgame.numEntities ) svgame.numEntities = i + 1;
			return &edicts[i];
		}
	return NULL;
}
static vec3_t spawned_at;
static int fake_spawn( edict_t *ent ) { spawns++; VectorCopy( ent->v.origin, spawned_at ); assert( ent->v.spawnflags & BIT( 30 )); return 0; }
static void Mem_Free( void *memory ) { free( memory ); }
static void Con_Printf( const char *format, ... )
{
	va_list args;
	size_t used = strlen( console );
	va_start( args, format );
	vsnprintf( console + used, sizeof( console ) - used, format, args );
	va_end( args );
}
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

/* a carried weapon: entity i, its object w, the player object it belongs to, in the player's bucket (or not) */
static void weapon( int i, string_t classname, int player, int bucket )
{
	void *pv = weapon_private[i - 3];
	int serial = edicts[i].serialnumber;
	memset( &edicts[i], 0, sizeof( edicts[i] ));
	edicts[i].serialnumber = serial;
	edicts[i].pvPrivateData = pv; edicts[i].owner = player_private[player]; edicts[i].v.classname = classname;
	if( bucket >= 0 ) memcpy( player_private[player] + 0x2514 + bucket * 4, &pv, sizeof( uint32_t ));
	if( i >= svgame.numEntities ) svgame.numEntities = i + 1;
}

int main( void )
{
	int i;
	svs.maxclients = 2; svs.clients = clients; svgame.numEntities = 3; sv.state = ss_active;
	for( i = 3; i < 12; i++ ) edicts[i].free = 1;
	clients[0].state = clients[1].state = cs_spawned;
	clients[0].userid = 1; clients[1].userid = 2;
	strcpy( clients[0].name, "Host" ); strcpy( clients[1].name, "Partner" );
	clients[0].edict = &edicts[1]; clients[1].edict = &edicts[2];
	edicts[1].pvPrivateData = player_private[0]; edicts[2].pvPrivateData = player_private[1];
	edicts[1].v.classname = edicts[2].v.classname = 5;
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
	transition = 0; edicts[2].v.health = 100; captures = restores = 0;

	/* 1.1.8 (E2-3): a drop the game never disconnected (a fast reconnect, a drop while loading): the player object still
	 * holds its weapons. They are freed only after the object let go of them (buckets, active/last/next, stored action
	 * item, stored night-vision weapon). (K7) The P345 is left where the leaver stood; the glock is not. */
	weapon( 3, 1, 1, 0 ); weapon( 4, 2, 1, 1 );
	memcpy( player_private[1] + 0x252C, &edicts[4].pvPrivateData, 4 ); /* the P345 in hand */
	memcpy( player_private[1] + 0x253C, &edicts[3].pvPrivateData, 4 ); /* a stored action item */
	memcpy( player_private[1] + 0x3F0, &edicts[3].pvPrivateData, 4 );  /* a stored night-vision weapon */
	edicts[2].v.origin[0] = 10; edicts[2].v.origin[1] = 20; edicts[2].v.origin[2] = 30;
	console[0] = 0;
	SV_CoF_RecoveryDropDone( &clients[1] );
	assert( frees == 2 && !unsafe_frees && edicts[4].free ); /* both freed (the copy may take a freed number) */
	assert( spawns == 1 && spawned_at[0] == 10 && spawned_at[1] == 20 && spawned_at[2] == 30 );
	assert( strstr( console, "cofstory phase=weapon-left userid=9 weapon=weapon_p345" ));
	for( i = 3; i < 12; i++ ) if( !edicts[i].free ) { assert( edicts[i].v.classname == 2 && !edicts[i].owner ); edicts[i].free = 1; }
	/* nothing owned: nothing touched */
	frees = spawns = 0; memset( player_private[1] + 0x2514, 0xEE, 4 );
	SV_CoF_RecoveryDropDone( &clients[1] ); assert( !frees && !spawns && player_private[1][0x2514] == 0xEE );
	memset( player_private[1] + 0x2514, 0, 4 );
	/* a companion leaves no copy; nor does a drop while a story level change restores the map */
	weapon( 3, 3, 1, 0 ); clients[1].flags = FCL_FAKECLIENT;
	SV_CoF_RecoveryDropDone( &clients[1] ); assert( frees == 1 && !spawns && !unsafe_frees );
	clients[1].flags = 0; weapon( 3, 3, 1, 0 ); transition = 1;
	SV_CoF_RecoveryDropDone( &clients[1] ); assert( frees == 2 && !spawns && !unsafe_frees );
	transition = 0; weapon( 3, 3, 1, 0 ); /* the sledgehammer is the other one */
	SV_CoF_RecoveryDropDone( &clients[1] ); assert( frees == 3 && spawns == 1 && !unsafe_frees );
	for( i = 3; i < 12; i++ ) edicts[i].free = 1;
	svgame.numEntities = 3;

	/* 1.1.8 (E2-4): the chainsaw boss's grab strips its human victim before the kill. Around the boss's think the
	 * humans in reach are captured; the one the think strips (frozen, buckets empty) keeps that capture for the
	 * respawn after the kill, not the empty one at death. */
	frees = spawns = 0; captures = restores = reconciled = 0;
	SV_CoF_RecoveryInit(); SV_CoF_RecoveryFrame();
	memset( &edicts[3], 0, sizeof( edicts[3] )); edicts[3].v.classname = 4; edicts[3].v.origin[0] = 60; svgame.numEntities = 4;
	edicts[1].v.origin[0] = 0; edicts[2].v.origin[0] = 10;
	weapon( 4, 1, 1, 0 ); edicts[2].inventory = 55;
	edicts[1].inventory = 7; /* the host is unarmed: nothing to keep */
	SV_CoF_RecoveryThinkBegin( &edicts[3] ); assert( captures == 1 );
	SV_CoF_RecoveryThinkEnd(); /* a think that took nothing: nothing handed over */
	sv.time += 2; /* 1.1.9 (I4): a capture under a second old would be used again */
	SV_CoF_RecoveryThinkBegin( &edicts[3] ); assert( captures == 2 );
	memset( player_private[1] + 0x2514, 0, 0x2C ); SetBits( edicts[2].v.flags, FL_FROZEN ); edicts[2].inventory = 0; /* stripped */
	console[0] = 0; SV_CoF_RecoveryThinkEnd();
	assert( strstr( console, "cofstory phase=inventory-captured userid=9 bytes=4 reason=grab" ));
	SV_CoF_RecoveryFrame(); assert( !restores && edicts[2].inventory == 0 );  /* held in the grab: nothing given back */
	edicts[2].v.health = 0; SV_CoF_RecoveryFrame(); assert( captures == 2 ); /* the kill: no capture of the empty hands */
	edicts[2].v.flags = 0; edicts[2].v.health = 100; SV_CoF_RecoveryFrame();
	assert( restores == 1 && edicts[2].inventory == 55 );                    /* the respawn: what they had before */
	/* other things never capture: another monster, a human out of reach, a frozen one */
	SV_CoF_RecoveryThinkBegin( &edicts[4] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 2 );
	memcpy( player_private[1] + 0x2514, &edicts[4].pvPrivateData, 4 );
	edicts[2].v.origin[0] = 300; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 2 );
	edicts[2].v.origin[0] = 10; SetBits( edicts[2].v.flags, FL_FROZEN );
	SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 2 );
	/* let go alive by a grab that never killed: given back once free */
	edicts[2].v.flags = 0; SV_CoF_RecoveryThinkBegin( &edicts[3] ); assert( captures == 3 );
	memset( player_private[1] + 0x2514, 0, 0x2C ); SetBits( edicts[2].v.flags, FL_FROZEN ); edicts[2].inventory = 0;
	SV_CoF_RecoveryThinkEnd(); SV_CoF_RecoveryFrame(); assert( restores == 1 );
	edicts[2].v.flags = 0; SV_CoF_RecoveryFrame(); assert( restores == 2 && edicts[2].inventory == 55 );
	SV_CoF_RecoveryReset();

	/* 1.1.9 (I4): a capture of the same inventory less than a second old is used again: one capture a second for a
	 * person in the boss's reach, not one per think (about ten a second, each a 4 MB save heap) */
	for( i = 3; i < 12; i++ ) edicts[i].free = 1;
	memset( &edicts[3], 0, sizeof( edicts[3] )); edicts[3].v.classname = 4; edicts[3].v.origin[0] = 60; svgame.numEntities = 4;
	SV_CoF_RecoveryInit(); SV_CoF_RecoveryFrame(); captures = restores = 0;
	edicts[2].v.flags = 0; edicts[2].v.health = 100; edicts[2].v.origin[0] = 10; edicts[2].inventory = 55;
	memset( player_private[1] + 0x2514, 0, 0x2C ); weapon( 4, 1, 1, 0 );
	for( i = 0; i < 10; i++ ) { sv.time = 300 + 0.1 * i; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); }
	assert( captures == 1 );
	sv.time = 301.05; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 2 ); /* over a second */
	/* ...an inventory that changed (a slot's descriptor, a weapon bucket) is captured again at once */
	player_private[1][0x21FC + 8] = 0x5A; sv.time = 301.1;
	SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 3 );
	weapon( 5, 1, 1, 1 ); sv.time = 301.2;
	SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 4 );
	/* ...another person in that client slot, or another map, never gets it */
	clients[1].userid = 11; sv.time = 301.3; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 5 );
	svs.spawncount++; sv.time = 301.4; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 6 );
	/* ...a capture used again is the one handed over when the grab strips them: what they had a moment before */
	sv.time = 301.6; SV_CoF_RecoveryThinkBegin( &edicts[3] ); assert( captures == 6 );
	memset( player_private[1] + 0x2514, 0, 0x2C ); SetBits( edicts[2].v.flags, FL_FROZEN ); edicts[2].inventory = 0;
	console[0] = 0; SV_CoF_RecoveryThinkEnd(); assert( strstr( console, "reason=grab" ));
	edicts[2].v.health = 0; SV_CoF_RecoveryFrame(); edicts[2].v.flags = 0; edicts[2].v.health = 100; SV_CoF_RecoveryFrame();
	assert( restores == 1 && edicts[2].inventory == 55 );
	/* ...a think whose begin did not see the person (out of reach) hands nothing over, however young the capture */
	weapon( 4, 1, 1, 0 ); sv.time = 302.0; SV_CoF_RecoveryThinkBegin( &edicts[3] ); SV_CoF_RecoveryThinkEnd(); assert( captures == 7 );
	edicts[2].v.origin[0] = 400; sv.time = 302.1; SV_CoF_RecoveryThinkBegin( &edicts[3] );
	memset( player_private[1] + 0x2514, 0, 0x2C ); SetBits( edicts[2].v.flags, FL_FROZEN ); console[0] = 0;
	SV_CoF_RecoveryThinkEnd(); assert( !strstr( console, "reason=grab" ));
	edicts[2].v.flags = 0; edicts[2].v.origin[0] = 10;
	SV_CoF_RecoveryReset();

	/* 1.1.9 (I5): a leaver's P345. (2) A player object the game never disconnected is made what ClientDisconnect makes
	 * of one: not solid, no damage, no target. (1) The copy left is the leaver's record's: the rejoin's restore gives
	 * the P345 back and takes the copy away while it still lies there; one a partner carries stays theirs. */
	for( i = 3; i < 12; i++ ) edicts[i].free = 1;
	svgame.numEntities = 3; frees = spawns = links = 0; captures = restores = 0; console[0] = 0;
	clients[1].state = cs_spawned; clients[1].userid = 20; clients[1].flags = 0; strcpy( clients[1].name, "Partner" );
	SV_CoF_RecoveryInit(); SV_CoF_RecoveryFrame();
	memset( player_private[1] + 0x2514, 0, 0x2C ); memset( player_private[1] + 0x3F0, 0, 4 );
	weapon( 3, 2, 1, 0 ); edicts[2].inventory = 64; edicts[2].v.solid = SOLID_SLIDEBOX; edicts[2].v.takedamage = DAMAGE_AIM;
	edicts[2].v.origin[0] = 10; edicts[2].v.origin[1] = 20; edicts[2].v.origin[2] = 30;
	SV_CoF_RecoveryDrop( &clients[1] ); SV_CoF_RecoveryDropDone( &clients[1] ); clients[1].state = cs_free;
	assert( captures == 1 && spawns == 1 && strstr( console, "cofstory phase=weapon-left userid=20 weapon=weapon_p345" ));
	assert( edicts[2].v.solid == SOLID_NOT && edicts[2].v.takedamage == DAMAGE_NO && FBitSet( edicts[2].v.flags, FL_NOTARGET ) && links == 1 );
	assert( strstr( console, "cofstory phase=body-let-go userid=20" ));
	{
		int copy = -1;
		for( i = 3; i < 12; i++ ) if( !edicts[i].free && edicts[i].v.classname == 2 ) copy = i;
		assert( copy >= 0 && !edicts[copy].owner );
		clients[1].state = cs_spawned; clients[1].userid = 21; edicts[2].inventory = 0; /* the rejoin, by name */
		SV_CoF_RecoveryJoin( &clients[1] ); SV_CoF_RecoveryFrame();
		assert( restores == 1 && edicts[2].inventory == 64 && edicts[copy].free );
		assert( strstr( console, "cofstory phase=weapon-taken-back userid=21 weapon=weapon_p345" ));
	}
	/* ...one the host picked up meanwhile is the host's */
	console[0] = 0; frees = spawns = 0; clients[1].userid = 22; edicts[2].v.solid = SOLID_SLIDEBOX;
	weapon( 3, 2, 1, 0 ); edicts[2].inventory = 65;
	SV_CoF_RecoveryDrop( &clients[1] ); SV_CoF_RecoveryDropDone( &clients[1] ); clients[1].state = cs_free;
	{
		int copy = -1;
		for( i = 3; i < 12; i++ ) if( !edicts[i].free && edicts[i].v.classname == 2 ) copy = i;
		assert( copy >= 0 && spawns == 1 );
		edicts[copy].owner = player_private[0]; /* the host carries it */
		clients[1].state = cs_spawned; clients[1].userid = 23; SV_CoF_RecoveryJoin( &clients[1] ); SV_CoF_RecoveryFrame();
		assert( edicts[2].inventory == 65 && !edicts[copy].free && !strstr( console, "weapon-taken-back" ));
		edicts[copy].free = 1;
	}
	/* ...a body the game disconnected (already not solid) is not linked again; nor is anything touched while a saved
	 * game loads (the next one in the slot is given that object as it is) */
	links = 0; edicts[2].v.solid = SOLID_NOT; SV_CoF_RecoveryDropDone( &clients[1] ); assert( !links );
	sv.loadgame = 1; edicts[2].v.solid = SOLID_SLIDEBOX; edicts[2].v.flags = 0; edicts[2].v.takedamage = DAMAGE_AIM;
	SV_CoF_RecoveryDropDone( &clients[1] ); assert( !links && edicts[2].v.solid == SOLID_SLIDEBOX && !FBitSet( edicts[2].v.flags, FL_NOTARGET ));
	sv.loadgame = 0;
	/* (3) A leave while a story level change restores the map: the copy waits for everyone to be in, then lies where
	 * the leaver was, and is the record's like any other (here: no rejoin, so it stays) */
	console[0] = 0; frees = spawns = 0; clients[1].userid = 24; weapon( 3, 3, 1, 0 ); edicts[2].inventory = 66;
	edicts[2].v.origin[0] = 70;
	transition = 1;
	SV_CoF_RecoveryDrop( &clients[1] ); SV_CoF_RecoveryDropDone( &clients[1] ); clients[1].state = cs_free;
	assert( frees == 1 && !spawns && strstr( console, "cofstory phase=weapon-left-later userid=24 weapon=weapon_sledgehammer" ));
	SV_CoF_RecoveryFrame(); assert( !spawns ); /* still restoring */
	transition = 0; SV_CoF_RecoveryFrame();
	assert( spawns == 1 && spawned_at[0] == 70 && strstr( console, "cofstory phase=weapon-left userid=24 weapon=weapon_sledgehammer" ));
	SV_CoF_RecoveryFrame(); assert( spawns == 1 ); /* once */
	/* ...and a level change in between: that map's copy is not left in another */
	clients[1].state = cs_spawned; clients[1].userid = 25; weapon( 4, 3, 1, 0 ); transition = 1; spawns = 0;
	SV_CoF_RecoveryDrop( &clients[1] ); SV_CoF_RecoveryDropDone( &clients[1] ); clients[1].state = cs_free;
	svs.spawncount++; transition = 0; SV_CoF_RecoveryFrame(); assert( !spawns );
	SV_CoF_RecoveryReset();
	puts( "recovery lifecycle scenarios passed" );
	return 0;
}
