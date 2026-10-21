/* Map-start patches of the shared story state (SV_CoF_StateActivate, sv_cof_state.c) against engine doubles: no game
 * DLL, networking or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_spawned 4
#define DEAD_NO 0
#define FCL_FAKECLIENT (1<<7)
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define FBitSet(f,b) ((f)&(b))
static void Q_strncpy( char *dst, const char *src, size_t size ) { strncpy( dst, src, size - 1 ); dst[size - 1] = 0; }
#include "sv_cof_state_data.h"
typedef struct { string_t classname; float health; int deadflag, spawnflags; } entvars_t;
typedef struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
static struct { int maxclients; sv_client_t *clients; } svs;
static struct { edict_t *edicts; int numEntities; } svgame;
#define ENTITIES 12
static edict_t entities[ENTITIES];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[ENTITIES][0x100];
static const char *strings[] = { "", "player", "cof_clearitems", "env_fade", "env_global", "stater", "env_sprite" };
static char log_text[4096];
static size_t log_length;
static int gate;
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return SV_IsValidEdict( e ) && need <= 0x100 ? e->pvPrivateData : NULL; }
static void Con_Printf( const char *fmt, ... )
{
	va_list args; int n;
	va_start( args, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, args );
	va_end( args );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static int logged( const char *needle ) { int n = 0; const char *p = log_text; while(( p = strstr( p, needle ))) { n++; p++; } return n; }
/* the rest of the state module is not driven here */
static const char *Cmd_Argv( int i ) { (void)i; return ""; }
static sv_client_t *SV_ClientFromEdict( edict_t *e, qboolean spawned ) { (void)e; (void)spawned; return NULL; }
static int SV_CoF_RetailNoteSlot( sv_client_t *cl, const char *note, int start ) { (void)cl; (void)note; (void)start; return 0; }
static qboolean SV_CoF_RetailArchiveNote( sv_client_t *cl, int slot, const byte *payload, int size ) { (void)cl; (void)slot; (void)payload; (void)size; return false; }
static qboolean SV_CoF_RetailStoryRead( sv_client_t *cl, cof_story_values_t *v ) { (void)cl; (void)v; return false; }
static qboolean SV_CoF_RetailStoryWrite( sv_client_t *cl, const cof_story_values_t *v ) { (void)cl; (void)v; return false; }
static qboolean SV_CoF_RetailThreeSlots( sv_client_t *cl ) { (void)cl; return false; }
static edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl ) { (void)cl; return NULL; }
static qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc ) { (void)cl; (void)doc; return false; }

#include "state_under_test.c"

#define GLOBAL_STATE 0xDC
#define GLOBAL_MODE 0xE0
#define GLOBAL_INITIAL 0xE4

static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities )); memset( private_data, 0, sizeof( private_data ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	gate = 1; svs.maxclients = 2; svs.clients = clients; svgame.edicts = entities; svgame.numEntities = ENTITIES;
	for( i = 0; i < ENTITIES; i++ ) { entities[i].pvPrivateData = private_data[i]; entities[i].free = i > 2; }
	entities[1].v.classname = entities[2].v.classname = 1;
}
static edict_t *spawn( int i, string_t classname, int spawnflags )
{
	entities[i].free = 0; entities[i].v.classname = classname; entities[i].v.spawnflags = spawnflags;
	return &entities[i];
}
/* an env_global (CEnvGlobal private data) */
static void global( int i, string_t state, int mode, int initial )
{
	spawn( i, 4, 0 );
	memcpy( private_data[i] + GLOBAL_STATE, &state, 4 ); memcpy( private_data[i] + GLOBAL_MODE, &mode, 4 );
	memcpy( private_data[i] + GLOBAL_INITIAL, &initial, 4 );
}
static int mode_of( int i ) { int mode; memcpy( &mode, private_data[i] + GLOBAL_MODE, 4 ); return mode; }

int main( void )
{
	/* cof_clearitems with spawnflag 1 clears every player (2), as before */
	setup(); spawn( 3, 2, 1 ); spawn( 4, 2, 0 ); SV_CoF_StateActivate();
	assert( entities[3].v.spawnflags == 3 && entities[4].v.spawnflags == 0 );

	/* 1.1.8 (K3): c_roofs/c_roofs2 fadefall (spawnflag 8, stays black) is its activator's alone (4); fades that go
	 * away by themselves, or are already one player's, are left as they are */
	setup(); spawn( 3, 3, 8 ); spawn( 4, 3, 0 ); spawn( 5, 3, 1 ); spawn( 6, 3, 8 | 4 ); spawn( 7, 3, 8 | 1 ); spawn( 8, 6, 8 );
	SV_CoF_StateActivate();
	assert( entities[3].v.spawnflags == 12 && entities[4].v.spawnflags == 0 && entities[5].v.spawnflags == 1 );
	assert( entities[6].v.spawnflags == 12 && entities[7].v.spawnflags == 13 && entities[8].v.spawnflags == 8 );

	/* 1.1.8 (M2 finding 6, M1 finding 4): a toggling env_global of a global that starts off only sets it on: a
	 * second fire (c_asylumb's restore_power, c_roofs's scene) no longer switches it back off */
	setup(); global( 3, 5, 3, 0 ); global( 4, 5, 1, 0 ); global( 5, 5, 3, 1 ); global( 6, 0, 3, 0 ); global( 7, 5, 0, 0 );
	SV_CoF_StateActivate();
	assert( mode_of( 3 ) == 1 && logged( "global-set-only entity=3 state=stater" ) == 1 );
	assert( mode_of( 4 ) == 1 && mode_of( 5 ) == 3 && mode_of( 6 ) == 3 && mode_of( 7 ) == 0 && logged( "global-set-only" ) == 1 );
	/* again (a save restored with it): nothing more */
	SV_CoF_StateActivate(); assert( logged( "global-set-only" ) == 1 );

	/* story co-op off: the map as it is */
	setup(); gate = 0; spawn( 3, 3, 8 ); global( 4, 5, 3, 0 ); SV_CoF_StateActivate();
	assert( entities[3].v.spawnflags == 8 && mode_of( 4 ) == 3 && !log_length );
	puts( "state activation scenarios passed" );
	return 0;
}
