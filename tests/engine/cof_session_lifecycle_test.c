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
#ifdef _WIN32
#define Q_stricmp _stricmp
#else
#include <strings.h>
#define Q_stricmp strcasecmp
#endif
#define Q_strcmp strcmp
#define MAX_CLIENTS 32
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
typedef struct { float value; char string[32]; } convar_t;
static convar_t continue_var, *cof_continue = &continue_var;
static convar_t continue_slot_var, *cof_continue_slot = &continue_slot_var; /* 1.3.3: the app's pick */
static int profont;
static void Cvar_DirectSet( convar_t *var, const char *value ) { var->value = (float)atof( value ); snprintf( var->string, sizeof( var->string ), "%s", value ); }
/* sv_cof_session_data.h's rule: 1 to 24 of a-z 0-9 _ - */
static int CoF_SessionSlotValid( const char *slot )
{
	size_t i, n = strlen( slot );
	if( !n || n > 24 ) return 0;
	for( i = 0; i < n; i++ ) if( !strchr( "abcdefghijklmnopqrstuvwxyz0123456789_-", slot[i] )) return 0;
	return 1;
}
static const char *CoF_SessionContinueSlot( void );
static int cof_session_generation = -1, cof_session_auto_needed;
static double cof_session_save_after, cof_session_wipe_after;
static void *cof_session_pending;
static char last_slot[25];
static char cof_session_latest[25];
static int cof_session_owned; /* 1.2.6: set by this session's own successful save (or checkpoint load) */
static void CoF_SessionLatest( void ) {}
static int CoF_SessionUsable( const char *slot ) { return slot && slot[0] && usable; }
static int SV_CoF_SceneActive( void ) { return 0; }
static int SV_CoF_PartyHolding( void ) { return 0; }
static int wait_reason; /* CoF_SessionWait: 1 cutscene, 2 ending door, 3 boat, 4 chapter kit (1.1.9) */
static int CoF_SessionWait( void ) { return wait_reason; }
static int SV_CoF_StoryCoopEnabled( void ) { return gate; }
static int SV_CoF_TransitionActive( void ) { return transition; }
static int SV_IsValidEdict( edict_t *entity ) { return entity != NULL; }
static int SV_CoF_SessionSave( const char *slot ) { saves++; Q_strncpy( last_slot, slot, sizeof( last_slot )); if( result ) cof_session_owned = 1; return result; }
static int SV_CoF_SessionLoad( const char *slot ) { loads++; Q_strncpy( last_slot, slot ? slot : "latest", sizeof( last_slot )); return result; }
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int index ) { return index < argc ? argv[index] : ""; }
#include <stdarg.h>
static char console_line[256];
static void Con_Printf( const char *format, ... )
{
	va_list args;
	va_start( args, format );
	vsnprintf( console_line, sizeof( console_line ), format, args );
	va_end( args );
}
static void SV_ClientPrintf( sv_client_t *client, const char *format, ... ) {}
static int SV_CoF_RetailProFont( sv_client_t *client, int colour, const char *text, int reliable ) { profont++; return 1; }
static int revived[2];
static qboolean SV_CoF_RetailRevived( sv_client_t *client ) { return revived[client - clients]; }
qboolean SV_CoF_SessionWipe( const char *map, const char *landmark );
void SV_CoF_SessionAutosaveSoon( void );
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
	/* A tape save only after the game's own save point: its SaveLoad screen (byte 0 = save), then the player's
	 * unfreezesave for that slot (the game's PreSave), then savehack 3.25 s later. */
	argv[0] = "savehack"; argv[1] = "cofsave5";
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && saves == 2 ); /* no save point: refused */
	{
		static const unsigned char menu[] = { 0, 'B', 'a', 's', 'e', 'm', 'e', 'n', 't', 0 };
		SV_CoF_SessionMessage( clients[1].edict, "SaveLoad", menu, sizeof( menu ));
	}
	argc = 3; argv[0] = "unfreezesave"; argv[1] = "1024"; argv[2] = "4";
	assert( !SV_CoF_SessionClientCommand( &clients[1] )); /* the game's own command, seen */
	argc = 2; argv[0] = "savehack"; argv[1] = "cofsave5";
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && saves == 2 ); /* another slot than chosen: refused */
	argc = 3; argv[0] = "unfreezesave"; argv[1] = "1024"; argv[2] = "5"; SV_CoF_SessionClientCommand( &clients[1] );
	argc = 2; argv[0] = "savehack"; argv[1] = "cofsave5";
	assert( SV_CoF_SessionClientCommand( &clients[0] ) && saves == 2 ); /* someone else's save point */
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && saves == 3 && !strcmp( last_slot, "tape5" ));
	assert( SV_CoF_SessionClientCommand( &clients[1] ) && saves == 3 ); /* once per save point */
	gate = 0; assert( !SV_CoF_SessionClientCommand( &clients[1] ));
	assert( !SV_CoF_SessionWipe( "c_start", NULL ));
	gate = 1; edicts[0].v.health = edicts[1].v.health = 100;
	/* 1.1.8: a new game or a chapter start (no Continue) saves its own first checkpoint, so a team wipe there loads
	 * this session's, not the last one of another session */
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1;
	sv.time = 10; SV_CoF_SessionFrame(); sv.time = 20; SV_CoF_SessionFrame();
	assert( saves == 4 && !strcmp( last_slot, "autosave" ) && !cof_session_auto_needed );
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
	assert( saves == 5 && !strcmp( last_slot, "autosave" ));
	/* Continue (the tester app): the latest checkpoint once the host has joined, once */
	loads = 0; cof_session_pending = NULL; usable = 1; result = 1; profont = 0;
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1;
	continue_var.value = 1; clients[0].state = cs_connected; sv.time = 100;
	SV_CoF_SessionFrame(); assert( !loads );                 /* the first two seconds */
	sv.time = 103; SV_CoF_SessionFrame(); assert( !loads );  /* the host is still joining (the wipe check moves the settle to 104) */
	clients[0].state = cs_spawned; sv.time = 105; SV_CoF_SessionFrame();
	assert( loads == 1 && !strcmp( last_slot, "autosave" ) && continue_var.value == 0 && !profont );
	assert( saves == 5 ); /* Continue keeps the checkpoint it loads: no autosave over it first */
	sv.time = 110; SV_CoF_SessionFrame(); assert( loads == 1 );   /* once */
	/* refused: the host is told, and it is not retried */
	continue_var.value = 1; result = 0; cof_session_auto_needed = 0; sv.time = 120; SV_CoF_SessionFrame();
	assert( loads == 2 && profont == 1 && continue_var.value == 0 );
	assert( cof_session_auto_needed ); /* 1.2.6: the map starts over as this session's own and saves its own checkpoint */
	sv.time = 130; SV_CoF_SessionFrame(); assert( loads == 2 ); result = 1;
	/* 1.3.3 (players asked for more than one save to go back to): Continue loads the checkpoint the app picked, a tape
	 * recorder's or the automatic one, and keeps that one at the first frame; the pick is used once */
	loads = 0; cof_session_pending = NULL; usable = 1; profont = 0; cof_session_auto_needed = 0;
	strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1; svs.spawncount++;
	Cvar_DirectSet( cof_continue_slot, "tape3" ); continue_var.value = 1; sv.time = 400;
	SV_CoF_SessionFrame(); assert( !loads && strstr( console_line, "session-checkpoint-preserved slot=tape3" ) && !cof_session_auto_needed );
	sv.time = 405; SV_CoF_SessionFrame();
	assert( loads == 1 && !strcmp( last_slot, "tape3" ) && continue_var.value == 0 && !continue_slot_var.string[0] );
	assert( strstr( console_line, "session-continue slot=tape3 result=queued" ));
	/* ...a pick that names no slot is the latest */
	loads = 0; cof_session_generation = -1; svs.spawncount++;
	Cvar_DirectSet( cof_continue_slot, "../tape1" ); continue_var.value = 1; sv.time = 500;
	SV_CoF_SessionFrame(); sv.time = 505; SV_CoF_SessionFrame(); assert( loads == 1 && !strcmp( last_slot, "autosave" ));
	/* ...and with no Continue, a stale pick changes nothing: a new game keeps saving its own */
	loads = 0; cof_session_generation = -1; svs.spawncount++; cof_session_auto_needed = 0;
	Cvar_DirectSet( cof_continue_slot, "tape2" ); continue_var.value = 0; sv.time = 600;
	SV_CoF_SessionFrame(); assert( strstr( console_line, "session-checkpoint-not-ours slot=autosave" ) && cof_session_auto_needed );
	sv.time = 605; SV_CoF_SessionFrame(); assert( !loads );
	Cvar_DirectSet( cof_continue_slot, "" );
	/* a living AI companion can still revive the person (or they respawn beside it):
	 * no checkpoint load under its CPR; a downed companion does not count */
	loads = 0; cof_session_pending = NULL; sv.time = 200; cof_session_wipe_after = 0;
	edicts[0].v.health = 0; edicts[0].v.fuser3 = edicts[1].v.fuser3 = 0;
	clients[1].flags = FCL_FAKECLIENT; edicts[1].v.health = 100;
	assert( !SV_CoF_SessionWipe( "c_start", NULL ) && !loads );
	edicts[1].v.health = 0; assert( SV_CoF_SessionWipe( "c_start", NULL ) && loads == 1 );
	clients[1].state = cs_connected; edicts[1].v.health = 100; /* a companion not in the world yet */
	assert( SV_CoF_SessionWipe( "c_start", NULL ) && loads == 2 ); clients[1].state = cs_spawned;
	/* a person someone is reviving (the reviver link is set) is not wiped either */
	loads = 0; clients[1].flags = 0; edicts[1].v.health = 0; revived[1] = 1;
	assert( !SV_CoF_SessionWipe( "c_start", NULL ) && !loads );
	revived[1] = 0; assert( SV_CoF_SessionWipe( "c_start", NULL ) && loads == 1 );
	/* 1.1.9 (P6, I7): cof_save says why a checkpoint waits: a cutscene or the ending door, a boat crossing, a chapter
	 * start's kit still on its way; otherwise it was refused */
	gate = 1; result = 0; argc = 1; argv[0] = "cof_save";
	wait_reason = 3; assert( SV_CoF_SessionClientCommand( &clients[0] ) && strstr( console_line, "a player is in the boat" ));
	wait_reason = 4; assert( SV_CoF_SessionClientCommand( &clients[0] ) && strstr( console_line, "starting weapons" ));
	wait_reason = 1; assert( SV_CoF_SessionClientCommand( &clients[0] ) && strstr( console_line, "a cutscene is running" ));
	wait_reason = 2; assert( SV_CoF_SessionClientCommand( &clients[0] ) && strstr( console_line, "a cutscene is running" ));
	wait_reason = 0; assert( SV_CoF_SessionClientCommand( &clients[0] ) && strstr( console_line, "Co-op save refused" ));
	result = 1;
	/* 1.2.6: a new game or a chapter start before its own first checkpoint lands (a cutscene holds the autosave, a
	 * chapter kit is on its way): a team wipe runs the game's own restart, never the previous session's checkpoint,
	 * which latest.txt still names (another map or chapter, maybe another party) */
	{
		int saves_before;
		cof_session_owned = 0; strcpy( cof_session_latest, "autosave" ); cof_session_generation = -1; continue_var.value = 0;
		clients[0].flags = clients[1].flags = 0; clients[0].state = clients[1].state = cs_spawned; revived[1] = 0;
		edicts[0].v.fuser3 = edicts[1].v.fuser3 = 0; edicts[0].v.health = edicts[1].v.health = 0;
		loads = 0; cof_session_pending = NULL; saves_before = saves; result = 0; /* the autosave is held back */
		sv.time = 300; SV_CoF_SessionFrame(); assert( strstr( console_line, "session-checkpoint-not-ours" ));
		sv.time = 303; SV_CoF_SessionFrame(); assert( !loads && strstr( console_line, "session-wipe-no-checkpoint" ));
		assert( !SV_CoF_SessionWipe( "c_start", NULL ) && !loads ); /* the game's own restart (pfnChangeLevel) goes on */
		result = 1; sv.time = 310; SV_CoF_SessionFrame(); assert( !loads && saves > saves_before && cof_session_owned );
		sv.time = 312; SV_CoF_SessionFrame(); assert( loads == 1 && !strcmp( last_slot, "latest" )); /* now its own */
	}
	puts( "session lifecycle scenarios passed" ); return 0;
}
