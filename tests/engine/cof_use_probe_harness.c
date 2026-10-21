/* Offline doubles for sv_cof_useprobe.c: a player, the c_college1 classroom's door 2 (two door brushes with a
 * func_wall between them, cm_college2 coordinates) and the engine's traces as plain data. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
typedef float vec3_t[3];
typedef const char *string_t;
#define true 1
#define false 0
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define SOLID_NOT 0
#define SOLID_TRIGGER 1
#define SOLID_BSP 4
#define MOVE_NORMAL 0
#define MAX_CLIENTS 4
#define M_PI_F 3.14159265358979323846f
#define FBitSet( a, b ) (( a ) & ( b ))
#define Q_min( a, b ) (( a ) < ( b ) ? ( a ) : ( b ))
#define Q_max( a, b ) (( a ) > ( b ) ? ( a ) : ( b ))
#define VectorCopy( a, b ) (( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2] )
#define VectorAdd( a, b, c ) (( c )[0] = ( a )[0] + ( b )[0], ( c )[1] = ( a )[1] + ( b )[1], ( c )[2] = ( a )[2] + ( b )[2] )
#define VectorSubtract( a, b, c ) (( c )[0] = ( a )[0] - ( b )[0], ( c )[1] = ( a )[1] - ( b )[1], ( c )[2] = ( a )[2] - ( b )[2] )
#define VectorMA( a, s, b, c ) (( c )[0] = ( a )[0] + ( s ) * ( b )[0], ( c )[1] = ( a )[1] + ( s ) * ( b )[1], ( c )[2] = ( a )[2] + ( s ) * ( b )[2] )
#define VectorScale( a, s, b ) (( b )[0] = ( a )[0] * ( s ), ( b )[1] = ( a )[1] * ( s ), ( b )[2] = ( a )[2] * ( s ))
#define DotProduct( a, b ) (( a )[0] * ( b )[0] + ( a )[1] * ( b )[1] + ( a )[2] * ( b )[2] )
typedef struct edict_s edict_t;
struct edict_s
{
	int free, serialnumber;
	struct
	{
		string_t classname, model, target;
		vec3_t origin, view_ofs, v_angle, absmin, absmax, size;
		int solid;
		edict_t *owner;
	} v;
};
typedef struct { int state, userid; unsigned int flags; edict_t *edict; } sv_client_t;
typedef struct { qboolean allsolid, startsolid; float fraction; vec3_t endpos; edict_t *ent; } trace_t;
typedef struct { float trace_fraction; edict_t *trace_ent; } globalvars_t;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static globalvars_t globals;
static struct { int numEntities; globalvars_t *globals; } svgame;
static sv_client_t clients[2];
static edict_t edicts[8];
static trace_t move_result; /* what SV_Move returns: the engine's own trace is not under test */
static float world_fraction = 1.0f;
static char console[8192];
static size_t console_length;

static int Q_strcmp( const char *a, const char *b ) { return strcmp( a, b ); }
/* the probe's own cvar (1.1.8: cof_use_probe, default 0; no longer cof_story_trace, which releases have on) */
typedef struct { float value; } convar_t;
static convar_t probe_cvar;
static int probe_cvar_gets;
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *description )
{
	if( strcmp( name, "cof_use_probe" ) || strcmp( value, "0" ) || flags || !description ) abort();
	probe_cvar_gets++;
	return &probe_cvar;
}
static const char *SV_GetString( string_t s ) { return s ? s : ""; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= edicts && e < edicts + 8 && !e->free; }
static edict_t *SV_EdictNum( int n ) { return &edicts[n]; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - edicts ); }
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static float VectorNormalizeLength( vec3_t v )
{
	float length = sqrtf( DotProduct( v, v ));
	if( length ) VectorScale( v, 1.0f / length, v );
	return length;
}
#define VectorNormalize( v ) VectorNormalizeLength( v )
static void AngleVectors( const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up )
{
	float sp = sinf( angles[0] * M_PI_F / 180.0f ), cp = cosf( angles[0] * M_PI_F / 180.0f );
	float sy = sinf( angles[1] * M_PI_F / 180.0f ), cy = cosf( angles[1] * M_PI_F / 180.0f );
	forward[0] = cp * cy; forward[1] = cp * sy; forward[2] = -sp;
}
static qboolean BoundsIntersect( const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2 )
{
	return !( mins1[0] > maxs2[0] || mins1[1] > maxs2[1] || mins1[2] > maxs2[2] ||
		maxs1[0] < mins2[0] || maxs1[1] < mins2[1] || maxs1[2] < mins2[2] );
}
/* A point ray against an entity's box (the world: a set fraction). Writes no globals, like the engine's. */
static void SV_ClipMoveToEntity( edict_t *ent, const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, trace_t *trace )
{
	float enter = 0.0f, leave = 1.0f;
	int i;
	memset( trace, 0, sizeof( *trace ));
	trace->fraction = 1.0f;
	if( ent == edicts ) { trace->fraction = world_fraction; trace->ent = ent; return; }
	for( i = 0; i < 3; i++ )
	{
		float d = end[i] - start[i], a, b;
		if( d == 0.0f )
		{
			if( start[i] < ent->v.absmin[i] || start[i] > ent->v.absmax[i] ) return;
			continue;
		}
		a = ( ent->v.absmin[i] - start[i] ) / d; b = ( ent->v.absmax[i] - start[i] ) / d;
		if( a > b ) { float t = a; a = b; b = t; }
		if( a > enter ) enter = a;
		if( b < leave ) leave = b;
	}
	if( enter > leave ) return;
	trace->startsolid = enter == 0.0f;
	trace->fraction = enter;
	trace->ent = ent;
}
static trace_t SV_Move( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *e, qboolean monsterclip )
{
	globals.trace_fraction = move_result.fraction; /* the engine's SV_Move writes the trace globals */
	globals.trace_ent = move_result.ent;
	return move_result;
}

/* 1.1.9 (R1): the map the probe is on by itself (c_college1, or its section of a merged map) */
#include "cof_sections.h"
static struct { char name[64]; } sv;
static struct { void *mempool; } host;
static int merged, load_calls, player_section[8];
static void *Mem_Calloc( void *pool, size_t size ) { (void)pool; return calloc( 1, size ); }
static void Mem_Free( void *p ) { free( p ); }
static int Q_stricmp( const char *a, const char *b ) { return !CoF_SectCaseEq( a, b ); }
static qboolean COM_CoF_IsMergedMap( const char *map ) { return merged && !strcmp( map, "cm_college2" ); }
static qboolean COM_CoF_LoadSections( const char *map, cof_sections_t *out )
{
	static const char *members[] = { "c_college2", "C_College1", "c_college1dark" }; /* the case as a file may have it */
	int i;
	memset( out, 0, sizeof( *out ));
	if( !COM_CoF_IsMergedMap( map )) return false;
	load_calls++;
	strcpy( out->name, map ); out->count = 3;
	for( i = 0; i < 3; i++ ) strcpy( out->sec[i].map, members[i] );
	return true;
}
static int SV_CoF_PlayerSection( const edict_t *ent ) { return merged ? player_section[ent - edicts] : -1; }

/* ---- the unit under test ---- */
#include "useprobe_under_test.c"

static int failures;
#define CHECK( cond ) do { if( !( cond )) { printf( "FAIL line %d: %s\n--- console ---\n%s---\n", __LINE__, #cond, console ); failures++; exit( 1 ); } } while( 0 )
#define IS( text ) ( !strcmp( console, text ))

static void brush( int n, const char *classname, const char *model, const char *target, float x0, float x1 )
{
	edict_t *e = &edicts[n];
	e->v.classname = classname; e->v.model = model; e->v.target = target;
	e->v.solid = SOLID_BSP;
	e->v.absmin[0] = x0; e->v.absmin[1] = -937; e->v.absmin[2] = 1032;
	e->v.absmax[0] = x1; e->v.absmax[1] = -889; e->v.absmax[2] = 1128;
	VectorSubtract( e->v.absmax, e->v.absmin, e->v.size );
}
static void clear( void ) { console[0] = 0; console_length = 0; }
static void reset( void )
{
	memset( edicts, 0, sizeof( edicts ));
	memset( clients, 0, sizeof( clients ));
	memset( cof_probe_last, 0, sizeof( cof_probe_last ));
	svs.maxclients = 2; svs.spawncount = 1; svs.clients = clients;
	cof_probe_spawncount = 1; cof_probe_where = 0;
	sv.name[0] = 0; merged = 0; memset( player_section, 0, sizeof( player_section ));
	svgame.numEntities = 7; svgame.globals = &globals;
	globals.trace_fraction = 0.5f; globals.trace_ent = NULL;
	probe_cvar.value = 1.0f; world_fraction = 1.0f;
	edicts[0].v.classname = "worldspawn";
	clients[0].state = cs_spawned; clients[0].userid = 1; clients[0].edict = &edicts[1];
	edicts[1].v.classname = "player";
	edicts[1].v.origin[0] = -265; edicts[1].v.origin[1] = -912; edicts[1].v.origin[2] = 1068;
	edicts[1].v.view_ofs[2] = 28;
	edicts[1].v.v_angle[1] = 180; /* facing the door, west */
	clients[1].state = cs_spawned; clients[1].userid = 10; clients[1].flags = FCL_FAKECLIENT; clients[1].edict = &edicts[2];
	edicts[2].v.classname = "player";
	brush( 3, "inter_door", "*55", "oexd2_2", -286, -285 ); /* the room's face: out to the hallway */
	brush( 4, "func_wall", "*21", NULL, -290, -289 );
	brush( 5, "inter_door", "*56", "oexd2_1", -293, -292 ); /* the hallway's face: back into the room */
	brush( 6, "inter_door", "*37", "oexd1_1", -293, -292 ); /* door 1, out of reach */
	edicts[6].v.absmin[1] = -445; edicts[6].v.absmax[1] = -397;
	memset( &move_result, 0, sizeof( move_result ));
	move_result.fraction = 0.3f; move_result.ent = &edicts[4];
	clear();
}

int main( void )
{
	/* no door held: nothing */
	reset();
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));

	/* the player starts the hallway's door from inside the room: the ray and the doors in reach, nearest first */
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( IS(
		"cofstory phase=use-probe userid=1 door=5 model=*56 target=oexd2_1 eye=-265,-912,1096 view=0,180 ofs=28 trace=0.300 hit=4\n"
		"cofstory phase=use-probe-hit userid=1 ent=3 class=inter_door model=*55 fraction=0.250 start=0 owner=0\n"
		"cofstory phase=use-probe-hit userid=1 ent=4 class=func_wall model=*21 fraction=0.300 start=0 owner=0\n"
		"cofstory phase=use-probe-hit userid=1 ent=5 class=inter_door model=*56 fraction=0.338 start=0 owner=1\n"
		"cofstory phase=use-probe-door userid=1 ent=3 model=*55 target=oexd2_2 dot=1.000\n"
		"cofstory phase=use-probe-door userid=1 ent=5 model=*56 target=oexd2_1 dot=1.000\n" ));
	/* observes only: the game's trace globals are as they were */
	CHECK( globals.trace_fraction == 0.5f && globals.trace_ent == NULL );

	/* the same door still held: once per use */
	clear();
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));

	/* let go, then the next door: probed again; the world in the way is a hit; a look off to the side changes the dots */
	edicts[5].v.owner = NULL;
	SV_CoF_UseProbeFrame();
	edicts[1].v.origin[1] = -950; /* south of the door, past its edge */
	edicts[3].v.owner = &edicts[1];
	world_fraction = 0.9f;
	move_result.ent = NULL; move_result.fraction = 0.9f;
	SV_CoF_UseProbeFrame();
	CHECK( strstr( console, "cofstory phase=use-probe userid=1 door=3 model=*55 target=oexd2_2 eye=-265,-950,1096 view=0,180 ofs=28 trace=0.900 hit=-1\n" ));
	CHECK( strstr( console, "cofstory phase=use-probe-hit userid=1 ent=0 class=worldspawn model= fraction=0.900 start=0 owner=0\n" ));
	CHECK( strstr( console, "ent=3 model=*55 target=oexd2_2 dot=0.838\n" ));
	CHECK( strstr( console, "ent=5 model=*56 target=oexd2_1 dot=0.901\n" ));

	/* the AI's own door uses are not probed (they are logged as companion-door) */
	reset();
	edicts[5].v.owner = &edicts[2];
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));

	/* the probe off: nothing */
	reset();
	probe_cvar.value = 0.0f;
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));

	/* a new level: what was held before is forgotten */
	reset();
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	clear();
	svs.spawncount = 2;
	SV_CoF_UseProbeFrame();
	CHECK( strstr( console, "phase=use-probe userid=1 door=5 " ));

	/* 1.1.9 (R1): with cof_use_probe 0 the probe watches c_college1 by itself, where the loop was reported */
	reset();
	probe_cvar.value = 0.0f;
	strcpy( sv.name, "c_college1" ); svs.spawncount = 2;
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( strstr( console, "phase=use-probe userid=1 door=5 " ));
	/* any other map: nothing */
	reset();
	probe_cvar.value = 0.0f;
	strcpy( sv.name, "c_college2" ); svs.spawncount = 2;
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));
	/* a merged map with c_college1 in it (cm_college2): only the humans in that section; the sections file read once */
	reset();
	probe_cvar.value = 0.0f; load_calls = 0;
	strcpy( sv.name, "cm_college2" ); merged = 1; svs.spawncount = 2;
	player_section[1] = 0; /* in the c_college2 section */
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( IS( "" ));
	player_section[1] = 1; /* now in the c_college1 section, still holding the door */
	SV_CoF_UseProbeFrame();
	CHECK( strstr( console, "phase=use-probe userid=1 door=5 " ));
	/* cof_use_probe 1: every section, as before */
	clear();
	probe_cvar.value = 1.0f; player_section[1] = 2; edicts[5].v.owner = NULL;
	SV_CoF_UseProbeFrame();
	edicts[5].v.owner = &edicts[1];
	SV_CoF_UseProbeFrame();
	CHECK( strstr( console, "phase=use-probe userid=1 door=5 " ) && load_calls == 1 );

	CHECK( probe_cvar_gets == 1 ); /* made once, then read */
	printf( "use-probe scenarios passed\n" );
	return failures;
}
