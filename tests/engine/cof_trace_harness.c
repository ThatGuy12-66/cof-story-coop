/* Offline doubles for sv_cof_trace.c: clients, edicts, the command line and the console as plain data. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
typedef unsigned char byte;
typedef const char *string_t;
#define true 1
#define false 0
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define FL_FROZEN ( 1U << 12 )
#define EF_NODRAW 128
#define DEAD_NO 0
#define MSG_ONE 1
#define MSG_ALL 2
#define MSG_ONE_UNRELIABLE 8
#define FBitSet( a, b ) (( a ) & ( b ))
#define SV_IsValidEdict( e ) (( e ) && !( e )->free )
typedef struct { int free; struct { unsigned int flags; int effects; float health; int deadflag; string_t classname; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; edict_t *edict, *pViewEntity; } sv_client_t;
typedef struct { float value; } convar_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; } sv;
static sv_client_t clients[4];
static edict_t edicts[8];
static convar_t trace_cvar = { 1.0f };
static int story_on = 1;
static char console[65536], argv_store[8][512];
static size_t console_length;
static int argc;

static size_t Q_strlen( const char *s ) { return strlen( s ); }
static int Q_strcmp( const char *a, const char *b ) { return strcmp( a, b ); }
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) n = size - 1;
	memcpy( dst, src, n );
	dst[n] = 0;
	return strlen( src );
}
static size_t Q_strncat( char *dst, const char *src, size_t size )
{
	size_t d = strlen( dst );
	if( d + 1 >= size ) return d;
	Q_strncpy( dst + d, src, size - d );
	return strlen( dst );
}
#define Q_snprintf snprintf
#define Q_stricmp _stricmp
static qboolean SV_CoF_StoryCoopEnabled( void ) { return story_on; }
static convar_t *Cvar_Get( const char *name, const char *value, unsigned int flags, const char *desc ) { return &trace_cvar; }
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int i ) { return i >= 0 && i < argc ? argv_store[i] : ""; }
static const char *SV_GetString( string_t s ) { return s; }
static sv_client_t *SV_ClientFromEdict( const edict_t *e, qboolean spawned_only )
{
	int i;
	if( !SV_IsValidEdict( e )) return NULL;
	for( i = 0; i < svs.maxclients; i++ )
		if( clients[i].edict == e && ( !spawned_only || clients[i].state == cs_spawned )) return &clients[i];
	return NULL;
}
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}

/* ---- the unit under test ---- */
#include "trace_under_test.c"

static int failures;
#define CHECK( cond ) do { if( !( cond )) { printf( "FAIL line %d: %s\n--- console ---\n%s---\n", __LINE__, #cond, console ); failures++; exit( 1 ); } } while( 0 )
#define IS( text ) ( !strcmp( console, text ))

static void reset( void )
{
	int i;
	memset( clients, 0, sizeof( clients ));
	memset( edicts, 0, sizeof( edicts ));
	memset( cof_trace_slots, 0, sizeof( cof_trace_slots ));
	memset( cof_trace_rates, 0, sizeof( cof_trace_rates ));
	memset( &cof_trace_msg, 0, sizeof( cof_trace_msg ));
	svs.maxclients = 4;
	svs.spawncount = 1;
	svs.clients = clients;
	cof_trace_spawncount = 1;
	sv.time = 100.0;
	trace_cvar.value = 1.0f;
	story_on = 1;
	for( i = 0; i < 4; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = 10 + i;
		clients[i].edict = &edicts[i + 1];
		edicts[i + 1].v.health = 100;
	}
	SV_CoF_TraceInit();
	console[0] = 0;
	console_length = 0;
}
static void clear( void ) { console[0] = 0; console_length = 0; }
static void cmd( int n, ... )
{
	va_list list;
	int i;
	va_start( list, n );
	for( i = 0; i < n; i++ ) snprintf( argv_store[i], sizeof( argv_store[i] ), "%s", va_arg( list, const char * ));
	va_end( list );
	argc = n;
}
static int lines( void )
{
	int n = 0;
	const char *p;
	for( p = console; *p; p++ ) if( *p == '\n' ) n++;
	return n;
}
static void msg( const char *name, int dest, edict_t *ent ) { SV_CoF_TraceMsgBegin( name, dest, ent ); }

int main( void )
{
	int i;
	char big[400], expect[600];

	/* off: nothing from any entry point */
	reset();
	trace_cvar.value = 0.0f;
	cmd( 2, "use", "x" );
	SV_CoF_TraceCommand( &clients[0] );
	SV_CoF_TraceFrame();
	clients[0].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	msg( "Padlock", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	reset();
	story_on = 0;
	SV_CoF_TraceCommand( &clients[0] );
	SV_CoF_TraceFrame();
	clients[0].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	msg( "Padlock", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	story_on = 1; /* turning the story back on mid-message starts clean */
	SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));

	/* the voice handshake every client repeats all game long is no line at all (live
	   2026-10-12: 2,580 of 2,600 command lines), in any case */
	reset();
	cmd( 2, "VModEnable", "1" ); SV_CoF_TraceCommand( &clients[0] );
	cmd( 3, "vban", "0", "0" ); SV_CoF_TraceCommand( &clients[1] );
	cmd( 2, "vmodenable", "1" ); SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "" ));
	cmd( 1, "vbanx" ); SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd userid=10 cmd=vbanx args=\"\"\n" ));

	/* command format and sanitizing */
	reset();
	cmd( 3, "cof_warp", "a", "b" );
	SV_CoF_TraceCommand( &clients[1] );
	CHECK( IS( "cofstory phase=cmd userid=11 cmd=cof_warp args=\"a b\"\n" ));
	reset();
	cmd( 1, "impulse" );
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd userid=10 cmd=impulse args=\"\"\n" ));
	reset();
	cmd( 2, "+use;x y\n", "q\"t\x01\xe9z" );
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd userid=10 cmd=+use?x?y? args=\"q't??z\"\n" ));
	reset();
	memset( big, 'n', 40 ); big[40] = 0;
	cmd( 1, big );
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd userid=10 cmd=nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn args=\"\"\n" ));
	reset();
	memset( big, 'x', 120 ); big[120] = 0;
	cmd( 2, "give", big );
	SV_CoF_TraceCommand( &clients[0] );
	memset( expect, 0, sizeof( expect ));
	strcpy( expect, "cofstory phase=cmd userid=10 cmd=give args=\"" );
	memset( expect + strlen( expect ), 'x', 96 );
	strcat( expect, "...\"\n" );
	CHECK( IS( expect ));
	reset();
	memset( big, 'y', 60 ); big[60] = 0;
	cmd( 3, "give", big, big );
	SV_CoF_TraceCommand( &clients[0] ); /* joined: 60 + space + 60, cut at 96 */
	CHECK( strstr( console, "..." ) && lines() == 1 );

	/* chat is never logged */
	reset();
	cmd( 2, "say", "my secret chat" );
	SV_CoF_TraceCommand( &clients[0] );
	cmd( 3, "say_team", "more", "chat" );
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd userid=10 cmd=say args=\"<chat>\"\ncofstory phase=cmd userid=10 cmd=say_team args=\"<chat>\"\n" ));
	CHECK( !strstr( console, "secret" ));

	/* rate limit */
	reset();
	cmd( 1, "x" );
	for( i = 0; i < 25; i++ ) SV_CoF_TraceCommand( &clients[0] );
	CHECK( lines() == 20 );
	sv.time += 0.5;
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( lines() == 20 ); /* still the same window */
	clear();
	sv.time += 1.0;
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( IS( "cofstory phase=cmd-suppressed userid=10 count=6\ncofstory phase=cmd userid=10 cmd=x args=\"\"\n" ));
	clear();
	sv.time += 1.0;
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( lines() == 1 ); /* the count was reset */
	/* another slot has its own window */
	reset();
	cmd( 1, "x" );
	for( i = 0; i < 25; i++ ) SV_CoF_TraceCommand( &clients[0] );
	clear();
	SV_CoF_TraceCommand( &clients[1] );
	CHECK( lines() == 1 );
	reset();
	cmd( 1, "x" );
	for( i = 0; i < 25; i++ ) SV_CoF_TraceCommand( &clients[0] );
	clear();
	sv.time += 1.0;
	SV_CoF_TraceCommand( &clients[0] );
	CHECK( strstr( console, "cofstory phase=cmd-suppressed userid=10 count=5\n" ) == console );

	/* fake and not-spawned clients */
	reset();
	cmd( 1, "x" );
	clients[0].flags = FCL_FAKECLIENT;
	clients[1].state = cs_connected;
	SV_CoF_TraceCommand( &clients[0] );
	SV_CoF_TraceCommand( &clients[1] );
	CHECK( IS( "" ));

	/* state: first look silent, then changes */
	reset();
	SV_CoF_TraceFrame();
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));
	clients[0].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( IS( "cofstory phase=state userid=10 frozen=1 camera=none hidden=0 alive=1 changed=frozen\n" ));
	clear();
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));
	edicts[6].v.classname = "trigger_camera";
	clients[0].pViewEntity = &edicts[6];
	SV_CoF_TraceFrame();
	CHECK( IS( "cofstory phase=state userid=10 frozen=1 camera=trigger_camera hidden=0 alive=1 changed=camera\n" ));
	clear();
	clients[0].pViewEntity = clients[0].edict; /* its own edict is no camera */
	clients[0].edict->v.effects |= EF_NODRAW;
	clients[0].edict->v.health = 0;
	SV_CoF_TraceFrame();
	CHECK( IS( "cofstory phase=state userid=10 frozen=1 camera=none hidden=1 alive=0 changed=camera,hidden,alive\n" ));
	clear();
	clients[0].edict->v.health = 50;
	clients[0].edict->v.deadflag = 1;
	SV_CoF_TraceFrame();
	CHECK( strstr( console, "alive=0" ) == NULL ); /* dead flag keeps it dead: no change */
	CHECK( IS( "" ));

	/* a map change forgets everyone: a changed state on its first frame is silent */
	reset();
	SV_CoF_TraceFrame();
	svs.spawncount++;
	clients[0].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));
	clients[0].edict->v.flags &= ~FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( lines() == 1 ); /* and it is tracked again afterwards */

	/* a new userid in the slot, or a client that left and came back, is a first look */
	reset();
	SV_CoF_TraceFrame();
	clients[0].userid = 99;
	clients[0].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));
	clients[0].state = cs_connected;
	SV_CoF_TraceFrame();
	clients[0].state = cs_spawned;
	clients[0].edict->v.flags &= ~FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));
	clients[1].flags = FCL_FAKECLIENT;
	clients[1].edict->v.flags |= FL_FROZEN;
	SV_CoF_TraceFrame();
	CHECK( IS( "" ));

	/* messages */
	reset();
	msg( "Padlock", MSG_ONE, clients[2].edict ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=Padlock to=12 data=1\n" ));
	clear();
	msg( "Padlock", MSG_ONE_UNRELIABLE, &edicts[7] ); SV_CoF_TraceMsgByte( 2 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=Padlock to=? data=2\n" ));
	clear();
	msg( "Padlock", MSG_ONE, NULL ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=Padlock to=? data=\n" ));
	clear();
	/* 1.1.8 (L13): one sent to an AI companion alone is not traced; to everyone it still is */
	clients[3].flags = FCL_FAKECLIENT;
	msg( "ScreenFade", MSG_ONE, clients[3].edict ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	msg( "ScreenFade", MSG_ONE_UNRELIABLE, clients[3].edict ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	msg( "ScreenFade", MSG_ONE, clients[2].edict ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=ScreenFade to=12 data=1\n" ));
	clear();
	msg( "ScreenFade", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 2 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=ScreenFade to=all data=2\n" ));
	clients[3].flags = 0;
	clear();
	clear();
	msg( "HudText", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	msg( "AddNote", MSG_ONE, clients[0].edict ); SV_CoF_TraceMsgByte( 3 ); SV_CoF_TraceMsgString( "" ); SV_CoF_TraceMsgString( "" ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	msg( "AddNote", MSG_ONE, clients[0].edict ); SV_CoF_TraceMsgByte( 3 ); SV_CoF_TraceMsgString( "" ); SV_CoF_TraceMsgString( "Dear \"Bob\"" ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=AddNote to=10 data=3,\"\",\"Dear 'Bob'\"\n" ));
	clear();
	msg( "Keypad", MSG_ALL, NULL ); SV_CoF_TraceMsgShort( 1234 ); SV_CoF_TraceMsgString( "nerds" ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=Keypad to=all data=1234,\"nerds\"\n" ));
	clear();
	msg( "Computer", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 4 ); SV_CoF_TraceMsgOther(); SV_CoF_TraceMsgByte( 5 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "cofstory phase=msg name=Computer to=all data=4,?,5\n" ));
	clear();
	msg( "ProFont", MSG_ALL, NULL ); SV_CoF_TraceMsgString( big ); SV_CoF_TraceMsgEnd();
	CHECK( strlen( console ) > 0 );
	clear();
	memset( big, 'a', 200 ); big[200] = 0;
	msg( "ProFont", MSG_ALL, NULL ); SV_CoF_TraceMsgString( big ); SV_CoF_TraceMsgEnd();
	memset( expect, 0, sizeof( expect ));
	strcpy( expect, "cofstory phase=msg name=ProFont to=all data=\"" );
	memset( expect + strlen( expect ), 'a', 159 );
	strcat( expect, "...\n" ); /* 160 chars of the rendering, then the cut marker */
	CHECK( IS( expect ));
	clear();
	/* a message that outgrows the buffer still ends cleanly */
	msg( "Objectives", MSG_ALL, NULL );
	for( i = 0; i < 300; i++ ) SV_CoF_TraceMsgString( big );
	SV_CoF_TraceMsgEnd();
	CHECK( lines() == 1 );
	clear();
	/* End always closes the capture */
	msg( "Padlock", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	clear();
	msg( "HudText", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 9 ); SV_CoF_TraceMsgEnd();
	SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	/* a Begin without an End does not leak into the next unwatched message */
	msg( "Padlock", MSG_ALL, NULL );
	msg( "HudText", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 9 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));
	/* cvar off in the middle of the run */
	trace_cvar.value = 0.0f;
	msg( "Padlock", MSG_ALL, NULL ); SV_CoF_TraceMsgByte( 1 ); SV_CoF_TraceMsgEnd();
	CHECK( IS( "" ));

	printf( "trace scenarios passed\n" );
	return failures;
}
