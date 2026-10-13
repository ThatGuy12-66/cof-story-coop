/* Offline doubles for sv_cof_exit.c: two players and one walk-in exit. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
typedef float vec3_t[3];
#define true 1
#define false 0
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define DEAD_NO 0
#define FBitSet( a, b ) (( a ) & ( b ))
#define Q_min( a, b ) (( a ) < ( b ) ? ( a ) : ( b ))
#define Q_strlen strlen
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_snprintf snprintf
#define VectorCopy( a, b ) memcpy(( b ), ( a ), sizeof( vec3_t ))
#define COM_StringEmptyOrNULL( s ) ( !( s ) || !*( s ))
static size_t Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t n = strlen( src );
	if( !size ) return n;
	if( n >= size ) { memcpy( dst, src, size - 1 ); dst[size - 1] = 0; }
	else memcpy( dst, src, n + 1 );
	return n;
}
static void COM_StripExtension( char *path )
{
	char *dot = strrchr( path, '.' );
	if( dot ) *dot = 0;
}
typedef struct { struct { float health; int deadflag; vec3_t origin; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; const char *name; } sv;
static sv_client_t clients[2];
static edict_t edicts[2];
static char lines[2][192], console[16384], queued_level[64], queued_landmark[64];
static int line_count[2], queued, model_reads, touching[2], gate, landmark_ok, exit_fires;
static int touching_other[2]; /* standing in the other exit (c_other, model 4) */
static int exit_refires; /* the exit's trigger can ask for it again */
static int exit_scripted;
static size_t console_length;

static qboolean SV_IsValidEdict( const edict_t *e ) { return e != NULL; }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin )
{
	origin[0] = origin[1] = origin[2] = 0;
	return landmark_ok;
}
static int SV_CoF_MapExitModels( const char *map, const char *destination, const char *landmark, int *models, int max, qboolean *fires, qboolean *scripted )
{
	assert( !strcmp( map, "c_start" ) && max >= 1 );
	model_reads++;
	models[0] = strcmp( destination, "c_start2" ) ? 4 : 3;
	*fires = exit_fires;
	*scripted = exit_scripted;
	return 1;
}
static qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count )
{
	assert( count == 1 && ( models[0] == 3 || models[0] == 4 ));
	return models[0] == 3 ? touching[player - edicts] : touching_other[player - edicts];
}
static int away[2]; /* the AI drives this idle second client (solo testing) */
static qboolean SV_CoF_CompanionAway( const sv_client_t *cl ) { return away[cl - clients]; }
static qboolean SV_CoF_PartyExitRefires( const int *models, int count )
{
	assert( count == 1 && ( models[0] == 3 || models[0] == 4 ));
	return exit_refires;
}
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	int p = (int)( cl - clients );
	assert( !reliable ); /* refreshed every 2 s: never queue them up */
	snprintf( lines[p], sizeof( lines[p] ), "%d:%s", colour, text );
	line_count[p]++;
	return true;
}
static void SV_QueueChangeLevel( const char *level, const char *landmark )
{
	queued++;
	Q_strncpy( queued_level, level, sizeof( queued_level ));
	Q_strncpy( queued_landmark, landmark, sizeof( queued_landmark ));
}
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}

#include "sv_cof_exit_state.h"
#include "exit_under_test.c"

/* the host stands in the exit at x=0; the guest is across the map */
static void setup( void )
{
	int i;
	memset( clients, 0, sizeof( clients )); memset( edicts, 0, sizeof( edicts ));
	memset( lines, 0, sizeof( lines )); memset( line_count, 0, sizeof( line_count ));
	console[0] = 0; console_length = 0;
	queued = model_reads = 0; gate = 1; landmark_ok = 1; exit_fires = 0; exit_refires = 1;
	exit_scripted = 0; away[0] = away[1] = 0;
	touching[0] = 1; touching[1] = 0; touching_other[0] = touching_other[1] = 0;
	sv.time = 100; sv.name = "c_start";
	svs.maxclients = 2; svs.spawncount = 5; svs.clients = clients;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		edicts[i].v.health = 100;
	}
	edicts[1].v.origin[0] = 3000;
	SV_CoF_ExitReset();
}
static void tick( double dt ) { sv.time += dt; SV_CoF_ExitFrame(); }
static qboolean ask( void ) { return SV_CoF_ExitRequest( "c_start2", "hahaland" ); }
static int once( const char *phase )
{
	const char *first = strstr( console, phase );
	return first && !strstr( first + 1, phase );
}

int main( void )
{
	/* off, or no landmark (the game's own restarts and co-op doors): never held */
	setup(); gate = 0; assert( ask());
	setup(); assert( SV_CoF_ExitRequest( "c_start2", "" ) && SV_CoF_ExitRequest( "c_start2", NULL ));
	assert( !SV_CoF_ExitWaiter() && !strstr( console, "exit-" ));
	/* an exit the engine would refuse anyway (no live landmark) is not held */
	setup(); landmark_ok = 0; assert( ask());
	/* alone, or with only a fake client: at once */
	setup(); clients[1].state = cs_free; assert( ask());
	setup(); clients[1].flags = FCL_FAKECLIENT; assert( ask());
	/* ...or with a second client the AI drives while nobody plays it (solo testing;
	   live 2026-10-12 the c_intro exit waited for an AI guest that never presses J) */
	setup(); away[1] = 1; assert( ask() && !strstr( console, "exit-hold" ));
	setup(); away[1] = 0; assert( !ask());

	/* the partner is elsewhere: held, said once, and both are told what to do */
	setup(); assert( !SV_CoF_ExitRequest( "c_start2.bsp", "hahaland" ));
	assert( strstr( console, "cofstory phase=exit-hold destination=c_start2 landmark=hahaland waiter=1 walkin=1 missing=2" ));
	assert( SV_CoF_ExitWaiter() == 1 );
	assert( SV_CoF_ExitQuietAlert( "CHANGE LEVEL: c_start2 hahaland\n" ) && !SV_CoF_ExitQuietAlert( "Can't find landmark x\n" ));
	SV_CoF_ExitFrame();
	assert( !strcmp( lines[0], "2:Please wait until everyone is ready! (Little Rascal 2 can press J to join you)" ));
	assert( !strcmp( lines[1], "1:Little Rascal 1 is at the exit. Press J to join." ));
	tick( 1 ); assert( line_count[0] == 1 );
	tick( 1.1 ); assert( line_count[0] == 2 && line_count[1] == 2 );
	/* the game asks again every frame it is touched: the same hold */
	assert( model_reads == 1 );
	assert( !ask() && once( "exit-hold" ) && queued == 0 );
	/* Review Finding 2: the repeated request does not read the map again */
	assert( model_reads == 1 );
	/* Review Focus 1: no further request; the partner arrives and the frame lets it go, once */
	edicts[1].v.origin[0] = 35; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_start2" ) && !strcmp( queued_landmark, "hahaland" ));
	assert( strstr( console, "cofstory phase=exit-ready destination=c_start2 landmark=hahaland waiter=1" ));
	assert( !SV_CoF_ExitWaiter() && !SV_CoF_ExitQuietAlert( "CHANGE LEVEL: c_start2 hahaland\n" ));
	tick( 0.1 ); assert( queued == 1 );

	/* the game asks again once the partner is there: it goes through the game's own request */
	setup(); assert( !ask()); edicts[1].v.origin[0] = 35;
	assert( ask() && strstr( console, "phase=exit-ready" ) && queued == 0 );

	/* stepping out of the exit ends the hold */
	setup(); assert( !ask()); touching[0] = 0; edicts[0].v.origin[0] = 500; tick( 0.1 );
	assert( queued == 0 && strstr( console, "cofstory phase=exit-cancelled waiter=1" ) && !SV_CoF_ExitWaiter());
	/* Review Finding 1: a once-only trigger vanishes; the waiter still stands there */
	setup(); assert( !ask()); touching[0] = 0; tick( 0.1 );
	assert( queued == 0 && !strstr( console, "exit-cancelled" ) && SV_CoF_ExitWaiter() == 1 );
	edicts[1].v.origin[0] = 35; tick( 0.1 ); assert( queued == 1 );
	/* Final review F1: nothing can ask for this exit again (a once-only trigger
	   fired a use-only exit), so walking away does not cancel the hold */
	setup(); assert( !ask()); exit_refires = 0; touching[0] = 0; edicts[0].v.origin[0] = 500; tick( 0.1 );
	assert( queued == 0 && !strstr( console, "exit-cancelled" ) && SV_CoF_ExitWaiter() == 1 );
	edicts[1].v.origin[0] = 535; tick( 0.1 ); assert( queued == 1 );
	/* Final review F3: first come, first served. The partner stands in another exit
	   while the waiter still stands in this one: that request waits, read only once */
	setup(); assert( !ask()); touching_other[1] = 1;
	assert( !SV_CoF_ExitRequest( "c_other", "otherland" ) && !SV_CoF_ExitRequest( "c_other", "otherland" ));
	assert( once( "exit-hold" ) && SV_CoF_ExitWaiter() == 1 && model_reads == 2 && queued == 0 );
	/* the waiter walks away: that hold ends, and the other exit's next request holds */
	touching[0] = 0; edicts[0].v.origin[0] = 500; tick( 0.1 );
	assert( strstr( console, "cofstory phase=exit-cancelled waiter=1" ));
	assert( !SV_CoF_ExitRequest( "c_other", "otherland" ) && SV_CoF_ExitWaiter() == 2 );
	assert( strstr( console, "cofstory phase=exit-hold destination=c_other landmark=otherland waiter=2 walkin=1 missing=1" ));
	/* the partner arrives at the other exit: it goes there, not to the first one */
	edicts[0].v.origin[0] = 2950; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_other" ) && !strcmp( queued_landmark, "otherland" ));
	/* the waiter steps from this exit straight into the one beside it: that one is held now */
	setup(); assert( !ask()); touching[0] = 0; touching_other[0] = 1; edicts[0].v.origin[0] = 60;
	assert( !SV_CoF_ExitRequest( "c_other", "otherland" ));
	assert( strstr( console, "cofstory phase=exit-hold destination=c_other landmark=otherland waiter=1 walkin=1" ));
	edicts[1].v.origin[0] = 95; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_other" ));
	/* a partner who is connected but has not spawned holds the exit */
	setup(); clients[1].state = cs_connected; assert( !ask()); tick( 0.1 ); assert( queued == 0 );
	clients[1].state = cs_spawned; edicts[1].v.origin[0] = 35; tick( 0.1 ); assert( queued == 1 );
	/* Review Finding 4: a hold from an earlier map is dead before the frame notices */
	setup(); assert( !ask()); svs.spawncount++;
	assert( !SV_CoF_ExitWaiter() && !SV_CoF_ExitQuietAlert( "CHANGE LEVEL: c_start2 hahaland\n" ));
	assert( !SV_CoF_ExitWarpKey( &clients[0] ));
	/* Review Focus 2: the partner disconnects mid-hold */
	setup(); assert( !ask()); clients[1].state = cs_free; tick( 0.1 ); assert( queued == 1 );
	/* a dead partner holds the exit and is not nagged about J */
	setup(); edicts[1].v.health = 0; edicts[1].v.origin[0] = 0; touching[1] = 1;
	assert( !ask()); SV_CoF_ExitFrame();
	assert( line_count[0] == 1 && line_count[1] == 0 );

	/* the escape hatch: the waiter's J, after 10 s */
	setup(); assert( !ask());
	assert( !SV_CoF_ExitWarpKey( &clients[1] )); /* the partner's J is a warp */
	sv.time += 3.2; assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 0 );
	assert( !strcmp( lines[0], "2:You can leave without your partner in 7 seconds." ));
	/* Review Focus 3: mashing J neither leaves early nor restarts the wait */
	sv.time += 3; assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 0 );
	sv.time += 4; SV_CoF_ExitFrame();
	assert( !strcmp( lines[0], "2:Please wait until everyone is ready! (press J to leave without Little Rascal 2)" ));
	assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 1 );
	assert( strstr( console, "cofstory phase=exit-forced destination=c_start2 landmark=hahaland waiter=1" ));
	assert( !SV_CoF_ExitWarpKey( &clients[0] )); /* the hold is over: J is a warp again */
	/* Review Finding 3: the waiter still stands in the trigger; it does not hold again */
	assert( ask() && once( "exit-hold" ) && !SV_CoF_ExitWaiter());
	/* one second to go: singular */
	setup(); assert( !ask()); sv.time += 9.5; assert( SV_CoF_ExitWarpKey( &clients[0] ) && queued == 0 );
	assert( !strcmp( lines[0], "2:You can leave without your partner in 1 second." ));

	/* an exit that fires targets is never held, and says so once */
	setup(); exit_fires = 1; assert( ask() && ask());
	assert( once( "cofstory phase=exit-unholdable destination=c_start2 landmark=hahaland" ) && !SV_CoF_ExitWaiter());
	/* the map changed another way: the hold is dropped, nothing is replayed */
	setup(); assert( !ask()); svs.spawncount++; tick( 0.1 );
	assert( queued == 0 && !SV_CoF_ExitWaiter());
	/* a scripted exit (nobody in the trigger): stored once, replayed when the partner comes */
	setup(); touching[0] = 0; edicts[0].v.origin[0] = 40; assert( !ask());
	assert( strstr( console, "waiter=1 walkin=0" ));
	edicts[0].v.origin[0] = 500; tick( 0.1 ); assert( queued == 0 );
	edicts[1].v.origin[0] = 535; tick( 0.1 ); assert( queued == 1 );
	/* a reset forgets the hold */
	setup(); assert( !ask()); SV_CoF_ExitReset(); tick( 0.1 );
	assert( queued == 0 && !SV_CoF_ExitWaiter());
	setup(); exit_scripted = 1; assert( !ask());
	edicts[0].v.health = 0; touching[0] = 0; tick( 0.1 );
	assert( !strstr( console, "exit-cancelled" ) && SV_CoF_ExitWaiter() == 2 );
	edicts[0].v.health = 100; edicts[0].v.origin[0] = 3035; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_start2" ));

	setup(); exit_scripted = 1; assert( !ask());
	clients[0].state = cs_free; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_start2" ));

	setup(); assert( !ask()); touching_other[1] = 1; exit_scripted = 1;
	assert( !SV_CoF_ExitRequest( "c_other", "otherland" ));
	exit_scripted = 0;
	assert( !ask());
	touching_other[1] = 0; edicts[1].v.origin[0] = 500; tick( 0.1 );
	assert( !strstr( console, "exit-cancelled" ));
	edicts[0].v.origin[0] = 535; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_other" ));

	setup(); exit_scripted = 1; assert( !ask());
	exit_scripted = 0; touching[0] = 0; touching_other[1] = 1;
	assert( !SV_CoF_ExitRequest( "c_other", "otherland" ));
	edicts[0].v.origin[0] = 2965; tick( 0.1 );
	assert( queued == 1 && !strcmp( queued_level, "c_start2" ));
	puts( "exit scenarios passed" );
	return 0;
}
