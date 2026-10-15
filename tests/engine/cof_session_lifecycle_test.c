#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define ss_active 1
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT 128
#define DEAD_NO 0
#define FBitSet(value,bits) ((value)&(bits))
#define Q_stricmp _stricmp
#define Q_strncmp strncmp
#define Q_strlen strlen
#define Q_snprintf snprintf
#define MAX_OSPATH 256
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef struct { struct { float health, fuser3; int deadflag; } v; } edict_t;
typedef struct { int state, flags; edict_t *edict; } sv_client_t;
static edict_t edicts[2];
static sv_client_t clients[2];
static struct { int spawncount, maxclients; sv_client_t *clients; } svs;
static struct { int state; double time; char name[64]; } sv;
static int gate = 1, transition, saves, loads, result = 1, argc, usable = 1;
static const char *argv[3];
typedef struct { float value; } convar_t;
static convar_t continue_var, *cof_continue = &continue_var;
static int profont;
static void Cvar_DirectSet( convar_t *var, const char *value ) { var->value = (float)atof( value ); }
static int cof_session_generation = -1, cof_session_auto_needed;
static double cof_session_save_after, cof_session_wipe_after;
static void *cof_session_pending;
static char last_slot[25];
static char cof_session_latest[25];
static void CoF_SessionLatest( void ) {}
static int CoF_SessionUsable( const char *slot ) { return slot && slot[0] && usable; }
static int SV_CoF_SceneActive( void ) { return 0; }
static int SV_CoF_PartyHolding( void ) { return 0; }
static int SV_CoF_StoryCoopEnabled( void ) { return gate; }
static int SV_CoF_TransitionActive( void ) { return transition; }
static int SV_IsValidEdict( edict_t *entity ) { return entity != NULL; }
static int SV_CoF_SessionSave( const char *slot ) { saves++; Q_strncpy( last_slot, slot, sizeof( last_slot )); return result; }
static int SV_CoF_SessionLoad( const char *slot ) { loads++; Q_strncpy( last_slot, slot ? slot : "latest", sizeof( last_slot )); return result; }
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int index ) { return index < argc ? argv[index] : ""; }
static void Con_Printf( const char *format, ... ) {}
static void SV_ClientPrintf( sv_client_t *client, const char *format, ... ) {}
static int SV_CoF_RetailProFont( sv_client_t *client, int colour, const char *text, int reliable ) { profont++; return 1; }
qboolean SV_CoF_SessionWipe( const char *map, const char *landmark );
#include "session_lifecycle_under_test.c"

int main( void )
{
	svs.clients = clients; svs.maxclients = 2; sv.state = ss_active; strcpy( sv.name, "c_start" );
	clients[0].edict = &edicts[0]; clients[1].edict = &edicts[1]; clients[0].state = clients[1].state = cs_spawned;
	edicts[0].v.health = edicts[1].v.health = 100;
	SV_CoF_SessionFrame(); assert( !saves && !loads );
	sv.time = 3; SV_CoF_SessionFrame(); SV_CoF_SessionFrame(); assert( saves == 1 && !strcmp( last_slot, "autosave" ));
	svs.spawncount++; sv.time = 0; transition = 1; SV_CoF_SessionFrame();
	sv.time = 3; SV_CoF_SessionFrame(); assert( saves == 1 );
	transition = 0; SV_CoF_SessionFrame(); assert( saves == 2 );
	edicts[1].v.health = 0; assert( !SV_CoF_SessionWipe( "c_start", NULL ));
	edicts[0].v.health = 0; edicts[1].v.fuser3 = 3; assert( !SV_CoF_SessionWipe( "c_start", NULL ));
	edicts[1].v.fuser3 = 0;
	assert( !SV_CoF_SessionWipe( "c_start2", NULL ) && !SV_CoF_SessionWipe( "c_start", "exit" ));
	/* a partner still joining: no load under their signon (live 2026-10-12 crash) */
	clients[1].state = cs_connected; assert( !SV_CoF_SessionWipe( "c_start", NULL ) && !loads );
	clients[1].state = cs_free; assert( SV_CoF_SessionWipe( "c_start", NULL ) && loads == 1 ); /* gone: the one left is the party */
	loads = 0; clients[1].state = cs_spawned;
	assert( SV_CoF_SessionWipe( "c_start", NULL ) && loads == 1 && !strcmp( last_slot, "latest" ));
	result = 0; assert( !SV_CoF_SessionWipe( "c_start", NULL )); result = 1;
	argc = 2; argv[0] = "cof_load"; argv[1] = "manual";
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && loads == 2 );
	assert( SV_CoF_SessionClientCommand( &clients[0] ) && loads == 3 );
	argv[0] = "savehack"; argv[1] = "cofsave5";
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && saves == 3 && !strcmp( last_slot, "tape5" ));
	gate = 0; assert( !SV_CoF_SessionClientCommand( &clients[1] ));
	assert( !SV_CoF_SessionWipe( "c_start", NULL ));
	gate = 1; edicts[0].v.health = edicts[1].v.health = 100;
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1;
	sv.time = 10; SV_CoF_SessionFrame(); sv.time = 20; SV_CoF_SessionFrame();
	assert( saves == 3 && !cof_session_auto_needed );
	/* a new map: the frame waits two seconds before a wipe recovery, however dead everyone is */
	edicts[0].v.health = edicts[1].v.health = 0; loads = 0; result = 1;
	svs.spawncount++; sv.time = 50; SV_CoF_SessionFrame(); assert( !loads );
	sv.time = 51.5; SV_CoF_SessionFrame(); assert( !loads );
	sv.time = 52.1; SV_CoF_SessionFrame(); assert( loads == 1 );
	edicts[0].v.health = edicts[1].v.health = 100; cof_session_pending = NULL;
	/* Review I-1: a latest checkpoint that cannot be loaded (an older build's)
	 * is not preserved: the first autosave still happens. */
	usable = 0; cof_session_generation = -1;
	sv.time = 30; SV_CoF_SessionFrame(); sv.time = 40; SV_CoF_SessionFrame();
	assert( saves == 4 && !strcmp( last_slot, "autosave" ));
	/* Continue (the tester app): the latest checkpoint once the host has joined, once */
	loads = 0; cof_session_pending = NULL; usable = 1; result = 1; profont = 0;
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1;
	continue_var.value = 1; clients[0].state = cs_connected; sv.time = 100;
	SV_CoF_SessionFrame(); assert( !loads );                 /* the first two seconds */
	sv.time = 103; SV_CoF_SessionFrame(); assert( !loads );  /* the host is still joining (the wipe check moves the settle to 104) */
	clients[0].state = cs_spawned; sv.time = 105; SV_CoF_SessionFrame();
	assert( loads == 1 && !strcmp( last_slot, "latest" ) && continue_var.value == 0 && !profont );
	sv.time = 110; SV_CoF_SessionFrame(); assert( loads == 1 );   /* once */
	/* refused: the host is told, and it is not retried */
	continue_var.value = 1; result = 0; sv.time = 120; SV_CoF_SessionFrame();
	assert( loads == 2 && profont == 1 && continue_var.value == 0 );
	sv.time = 130; SV_CoF_SessionFrame(); assert( loads == 2 ); result = 1;
	puts( "session lifecycle scenarios passed" ); return 0;
}
