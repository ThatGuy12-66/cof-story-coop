/* The AI companion glue (sv_cof_companion.c) against engine doubles: no game
 * DLL, networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <math.h>
typedef unsigned char byte;
typedef unsigned short word;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define MAX_CLIENTS 6
#define cs_free 0
#define cs_zombie 1
#define cs_connected 2
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define DEAD_DEAD 2
#define DAMAGE_NO 0
#define DAMAGE_AIM 2
#define FL_CLIENT (1<<3)
#define FL_FAKECLIENT (1<<13)
#define FL_ONGROUND (1<<9)
#define FL_MONSTER (1<<5)
#define FL_FROZEN (1<<12)
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define EF_NODRAW 128
#define MOVETYPE_WALK 3
#define MOVE_NORMAL 0
#define MOVE_NOMONSTERS 1
#define kRenderNormal 0
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define MAX_QPATH 64
#define Q_atoi atoi
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
#define Q_snprintf snprintf
#define Q_min(a,b) ((a)<(b)?(a):(b))
#define FBitSet(f,b) ((f)&(b))
#define bound(lo,v,hi) ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSet(v,x,y,z) ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define VectorAdd(a,b,c) ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2])
#define VectorSubtract(a,b,c) ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2])
#define VectorMA(a,s,b,c) ((c)[0]=(a)[0]+(s)*(b)[0],(c)[1]=(a)[1]+(s)*(b)[1],(c)[2]=(a)[2]+(s)*(b)[2])
#define VectorAverage(a,b,o) ((o)[0]=((a)[0]+(b)[0])*0.5f,(o)[1]=((a)[1]+(b)[1])*0.5f,(o)[2]=((a)[2]+(b)[2])*0.5f)
#define DotProduct(x,y) ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define VectorDistance(a,b) (sqrt(((a)[0]-(b)[0])*((a)[0]-(b)[0])+((a)[1]-(b)[1])*((a)[1]-(b)[1])+((a)[2]-(b)[2])*((a)[2]-(b)[2])))
typedef struct edict_s edict_t;
typedef struct {
	string_t classname;
	vec3_t origin, view_ofs, v_angle, angles, mins, maxs, absmin, absmax;
	float health, takedamage, fuser3, frags, renderamt;
	int deadflag, flags, effects, movetype, fixangle, rendermode, colormap;
	string_t netname, target, targetname, model;
} entvars_t;
struct edict_s { int free, serialnumber; entvars_t v; };
typedef struct { vec3_t viewangles; float forwardmove, sidemove, upmove; word buttons; byte msec; } usercmd_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict, *pViewEntity; double timebase; usercmd_t lastcmd; } sv_client_t;
typedef struct { qboolean startsolid, allsolid; float fraction; edict_t *ent; vec3_t endpos; struct { vec3_t normal; } plane; } trace_t;
typedef struct { float value; const char *string; } convar_t;
static struct { int state; double time, frametime; sv_client_t *current_client; char name[MAX_QPATH]; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { int numEntities; struct { qboolean (*pfnClientConnect)( edict_t *, const char *, const char *, char * ); void (*pfnClientPutInServer)( edict_t * ); void (*pfnUse)( edict_t *, edict_t * ); } dllFuncs; } svgame;
static edict_t entities[16];
static sv_client_t clients[MAX_CLIENTS];
static const char *strings[] = { "", "player", "monster_slower", "monster_friend", "monster_engaged", "trigger_once", "trigger_hurt", "killthemall", "func_door", "func_door_rotating", "*5" };
static char log_text[32768];
static size_t log_length;
static int gate, connect_ok = 1, put_in, placed, ready, equips, runs, next_userid = 200, wall, inits;
/* a door (entity 9) across x = door_x at the feet; a low box from x = ledge_x on, ledge_top over the floor */
static int door_x, ledge_x, uses;
static float ledge_top;
/* Solid boxes (stairs, a ledge, walls, pillars). Bodies stand with their origin 36 over the floor
 * (z = -36 under a body at z = 0), as the game's players do. */
#define FLOOR (-36.0f)
typedef struct { float x0, y0, x1, y1, z0, z1; } box_t;
static box_t boxes[24];
static int box_count;
static void add_box( float x0, float y0, float x1, float y1, float z0, float z1 )
{
	box_t *b = &boxes[box_count++];
	b->x0 = x0; b->y0 = y0; b->x1 = x1; b->y1 = y1; b->z0 = z0; b->z1 = z1;
}
static int box_hit( const box_t *b, const float *p, const float *mins, const float *maxs )
{
	return p[0] + maxs[0] > b->x0 && p[0] + mins[0] < b->x1 && p[1] + maxs[1] > b->y0 && p[1] + mins[1] < b->y1 &&
		p[2] + mins[2] < b->z1 && p[2] + maxs[2] > b->z0;
}
/* where a hull sweep first overlaps a box: 0 clear, 1 blocked (fraction, startsolid and the plane set) */
static int boxes_block( const float *start, const float *mins, const float *maxs, const float *end, trace_t *t )
{
	box_t legacy;
	float d[3], length, p[3];
	int n, k, i;
	VectorSubtract( end, start, d );
	length = (float)sqrt( DotProduct( d, d ));
	n = length < 1.0f ? 1 : (int)ceil( length );
	if( ledge_x ) { legacy.x0 = (float)ledge_x; legacy.x1 = (float)ledge_x + 200.0f; legacy.y0 = -10000; legacy.y1 = 10000; legacy.z0 = -10000; legacy.z1 = FLOOR + ledge_top; }
	for( k = 0; k <= n; k++ )
	{
		int hit = 0;
		VectorMA( start, (float)k / (float)n, d, p );
		for( i = 0; i < box_count && !hit; i++ ) hit = box_hit( &boxes[i], p, mins, maxs );
		if( !hit && ledge_x ) hit = box_hit( &legacy, p, mins, maxs );
		if( !hit ) continue;
		t->startsolid = k == 0;
		t->fraction = k == 0 ? 0.0f : (float)( k - 1 ) / (float)n;
		VectorMA( start, t->fraction, d, t->endpos );
		t->plane.normal[2] = d[2] < -0.5f * length ? 1.0f : 0.0f;
		return 1;
	}
	return 0;
}
static edict_t *used, *user;
static int supplies, revives, revive_ok, reviving, repairs, fallen, gives, free_slots, command_count;
static char last_equip[32], commands[16][32];
static usercmd_t last_run;
static convar_t cvars[8] = { { 0, "0" }, { 0, "Companion" }, { 0, "0" }, { 8, "8" }, { 3, "3" }, { 90, "90" }, { 1, "1" }, { 1, "1" } };

static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + 16 && !e->free; }
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static void Q_strncpy( char *d, const char *s, size_t n ) { size_t i; for( i = 0; i + 1 < n && s[i]; i++ ) d[i] = s[i]; d[i] = 0; }
static long COM_RandomLong( long lo, long hi ) { return lo; }
static void Con_Printf( const char *fmt, ... )
{
	va_list args; int n;
	va_start( args, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, args );
	va_end( args );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *description )
{
	if( !strcmp( name, "cof_companion" )) return &cvars[0];
	if( !strcmp( name, "cof_companion_name" )) return &cvars[1];
	if( !strcmp( name, "cof_companion_drive" )) return &cvars[2];
	if( !strcmp( name, "cof_companion_regen" )) return &cvars[4];
	if( !strcmp( name, "cof_companion_medic" )) return &cvars[5];
	if( !strcmp( name, "cof_companion_ammo" )) return &cvars[6];
	if( !strcmp( name, "cof_companion_light" )) return &cvars[7];
	return &cvars[3];
}
static void Cvar_SetValue( const char *name, float value ) { Cvar_Get( name, "", 0, "" )->value = value; }
static void Cmd_AddCommand( const char *name, void (*f)( void ), const char *description ) {}
static const char *argv0 = "", *argv1 = ""; static int argc = 1;
static int Cmd_Argc( void ) { return argc; }
static const char *Cmd_Argv( int i ) { return i == 0 ? argv0 : i == 1 ? argv1 : ""; }
static char told[MAX_CLIENTS][96];
static int told_count[MAX_CLIENTS];
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable ) { told_count[cl - clients]++; snprintf( told[cl - clients], sizeof( told[0] ), "%s", text ); return true; }
static edict_t *SV_FakeConnect( const char *name )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( clients[i].state == cs_free )
		{
			memset( &clients[i], 0, sizeof( clients[i] ));
			clients[i].state = cs_spawned; clients[i].flags = FCL_FAKECLIENT; clients[i].userid = next_userid++;
			clients[i].edict = &entities[i + 1]; Q_strncpy( clients[i].name, name, sizeof( clients[i].name ));
			entities[i + 1].free = 0;
			entities[i + 1].v.flags = FL_CLIENT | FL_FAKECLIENT;
			return &entities[i + 1];
		}
	return NULL;
}
static sv_client_t *SV_ClientFromEdict( const edict_t *e, qboolean spawned ) { int i = NUM_FOR_EDICT( e ) - 1; return i >= 0 && i < MAX_CLIENTS ? &clients[i] : NULL; }
static int in_game[16]; /* the edict has a game object (put in, not cleaned since) */
static void SV_DropClient( sv_client_t *cl, qboolean crash )
{
	if( cl->state == cs_spawned ) assert( in_game[cl->edict - entities] ); /* the game's disconnect reads its player object */
	cl->state = cs_zombie; cl->flags = 0;
}
static qboolean game_connect( edict_t *e, const char *name, const char *address, char *reject ) { if( !connect_ok ) strcpy( reject, "nope" ); return connect_ok; }
static void game_use( edict_t *e, edict_t *other ) { uses++; used = e; user = other; }
/* The game's Spawn: a fresh player, its flags wiped down to FL_CLIENT (FL_FAKECLIENT goes). */
static void game_put_in( edict_t *e )
{
	put_in++;
	in_game[e - entities] = 1;
	assert( e->v.flags == ( FL_CLIENT | FL_FAKECLIENT ) && e->v.health == 0 ); /* the engine cleaned the edict first */
	e->v.classname = 1; e->v.health = 100; e->v.movetype = MOVETYPE_WALK; e->v.view_ofs[2] = 28; e->v.flags = FL_CLIENT | FL_ONGROUND;
	e->v.mins[0] = e->v.mins[1] = -16; e->v.mins[2] = -36; e->v.maxs[0] = e->v.maxs[1] = 16; e->v.maxs[2] = 36;
}
static void SV_InitEdict( edict_t *e ) { inits++; memset( &e->v, 0, sizeof( e->v )); in_game[e - entities] = 0; }
static string_t SV_MakeString( const char *s ) { return 1; }
static void SV_CoF_PartyPlayerReady( sv_client_t *cl, qboolean fresh ) { ready++; }
static int place_ok = 1, place_rank, place_anywhere, place_had_near, in_door;
static vec3_t place_near;
static qboolean SV_CoF_PartyPlaceUnseen( sv_client_t *mover, edict_t *leader, const float *exit_at, int rank, qboolean anywhere )
{
	place_rank = rank; place_anywhere = anywhere; place_had_near = exit_at != NULL;
	if( exit_at ) VectorCopy( exit_at, place_near );
	if( !place_ok && !anywhere ) return false;
	placed++; VectorCopy( exit_at ? exit_at : leader->v.origin, mover->edict->v.origin ); mover->edict->v.origin[0] -= 64; return true;
}
static qboolean SV_CoF_PartyInDoor( const sv_client_t *cl ) { return in_door && cl == &clients[0]; }
/* A wall across x = gap_x everywhere below y = gap_y (when gap_x is set): a gap at its far end. */
static float gap_x, gap_y;
/* A wall at x = 1000 blocks sight (when on); everything else is open. */
static trace_t SV_Move( const vec3_t start, vec3_t mins, vec3_t maxs, const vec3_t end, int type, edict_t *ignore, qboolean monsterclip )
{
	trace_t t; memset( &t, 0, sizeof( t )); t.fraction = 1.0f;
	if( wall && (( start[0] < 1000 ) != ( end[0] < 1000 ))) t.fraction = 0.5f;
	if( gap_x && (( start[0] < gap_x ) != ( end[0] < gap_x )) &&
		start[1] + ( end[1] - start[1] ) * ( gap_x - start[0] ) / ( end[0] - start[0] ) < gap_y ) t.fraction = 0.5f;
	/* the door blocks moves at the feet, not the line of sight from an eye */
	if( door_x && !entities[9].free && start[2] <= 20.0f && (( start[0] < door_x ) != ( end[0] < door_x ))) { t.fraction = 0.5f; t.ent = &entities[9]; }
	{ vec3_t d; VectorSubtract( end, start, d ); VectorMA( start, t.fraction, d, t.endpos ); }
	if( mins && maxs && t.fraction >= 1.0f && boxes_block( start, mins, maxs, end, &t ) && t.startsolid ) t.allsolid = 1;
	if( type != MOVE_NOMONSTERS && mins && maxs && t.fraction >= 1.0f )
	{
		/* other players' bodies (a partner in a doorway) */
		int i, k, n;
		vec3_t d, q;
		float length;
		VectorSubtract( end, start, d );
		length = (float)sqrt( DotProduct( d, d ));
		n = length < 1.0f ? 1 : (int)ceil( length );
		for( k = 1; k <= n && t.fraction >= 1.0f; k++ )
		{
			VectorMA( start, (float)k / (float)n, d, q );
			for( i = 1; i <= svs.maxclients; i++ )
			{
				edict_t *e = &entities[i];
				if( e == ignore || e->free || clients[i - 1].state != cs_spawned ) continue;
				if( q[0] + maxs[0] > e->v.origin[0] - 16 && q[0] + mins[0] < e->v.origin[0] + 16 &&
					q[1] + maxs[1] > e->v.origin[1] - 16 && q[1] + mins[1] < e->v.origin[1] + 16 &&
					q[2] + mins[2] < e->v.origin[2] + 36 && q[2] + maxs[2] > e->v.origin[2] - 36 )
				{
					t.fraction = (float)( k - 1 ) / (float)n; t.ent = e; VectorMA( start, t.fraction, d, t.endpos );
					break;
				}
			}
		}
	}
	return t;
}
/* The player code: the view follows the usercmd. */
static usercmd_t ran[MAX_CLIENTS];
static void SV_RunCmd( sv_client_t *cl, usercmd_t *cmd, int seed ) { runs++; last_run = *cmd; ran[cl - clients] = *cmd; VectorCopy( cmd->viewangles, cl->edict->v.v_angle ); }
enum { COF_ARMS_NONE, COF_ARMS_MELEE, COF_ARMS_GUN, COF_ARMS_HEAL };
static int arms_weapon = COF_ARMS_MELEE, arms_has_gun, arms_clip, arms_reserve, arms_has_heal, arms_sights, arms_has_melee = 1;
typedef struct { int weapon, ready, sights, reloading, struggle, clip, reserve, has_gun, has_melee, has_heal; char gun[32], melee[32], heal[32]; } cof_retail_arms_t;
static qboolean SV_CoF_RetailArms( sv_client_t *cl, cof_retail_arms_t *arms )
{
	memset( arms, 0, sizeof( *arms ));
	arms->weapon = arms_weapon; arms->ready = 1; arms->has_melee = arms_has_melee; arms->has_gun = arms_has_gun; arms->has_heal = arms_has_heal;
	if( arms_weapon == COF_ARMS_GUN ) { arms->clip = arms_clip; arms->reserve = arms_reserve; arms->sights = arms_sights; }
	strcpy( arms->gun, "weapon_glock" ); strcpy( arms->melee, "weapon_switchblade" ); strcpy( arms->heal, arms_has_heal ? "weapon_syringe" : "" );
	return true;
}
static qboolean SV_CoF_RetailEquip( sv_client_t *cl, const char *classname ) { equips++; strcpy( last_equip, classname ); return true; }
static void SV_CoF_RetailSupply( sv_client_t *cl ) { supplies++; }
static qboolean SV_CoF_RetailReviving( sv_client_t *cl ) { return reviving; }
static qboolean SV_CoF_RetailRevived( sv_client_t *cl ) { return false; }
/* as the retail layer: dead, nobody at it, not in its get-up */
static qboolean SV_CoF_RetailRevivable( sv_client_t *cl ) { return cl->edict->v.health <= 0 && !reviving && cl->edict->v.fuser3 != 3.0f; }
static qboolean SV_CoF_RetailRevive( sv_client_t *reviver, sv_client_t *body ) { revives++; reviving = revive_ok; return revive_ok; }
static void SV_CoF_RetailCprRepair( void ) { repairs++; }
static qboolean SV_CoF_RetailFallen( sv_client_t *cl ) { return fallen; }
static qboolean SV_CoF_RetailCommand( sv_client_t *cl, const char *command )
{
	if( command_count < 16 ) Q_strncpy( commands[command_count++], command, sizeof( commands[0] ));
	return true;
}
static int SV_CoF_RetailFreeSlots( sv_client_t *cl ) { return free_slots; }
static qboolean SV_CoF_StoryGiveItem( sv_client_t *cl, const char *classname ) { gives++; return !strcmp( classname, "weapon_syringe" ); }
/* hostile: monster_slower; already after a player: monster_engaged; never: monster_friend */
static int SV_CoF_RetailHostile( edict_t *monster ) { return monster->v.classname == 2 ? 1 : monster->v.classname == 4 ? 2 : 0; }

#include "sv_cof_brain.h"
#include "sv_cof_nav.h"

/* The map's route graph as the game saves it (maps/graphs/c_test.nod), when have_graph:
 * N0 (200,100), N1 (500,400) in the gap, N2 (800,100), linked N0-N1-N2 both ways
 * (the N1-N2 links through door "*5" when graph_door). */
typedef long long fs_offset_t;
static int have_graph, graph_door, loads;
static unsigned char graph_file[16384];
static void put_int( size_t at, int v ) { memcpy( graph_file + at, &v, 4 ); }
static void put_float( size_t at, float v ) { memcpy( graph_file + at, &v, 4 ); }
static size_t build_graph( void )
{
	static const float at[3][2] = { { 200, 100 }, { 500, 400 }, { 800, 100 } };
	static const int links[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 2 }, { 2, 1 } };
	size_t nodes = 4 + COF_NAV_GRAPH, link_at = nodes + 3 * COF_NAV_NODE, size = link_at + 4 * COF_NAV_LINK + 3 * COF_NAV_DIST;
	int i, k, written = 0;
	memset( graph_file, 0, size );
	put_int( 0, COF_NAV_VERSION ); put_int( 4 + 24, 3 ); put_int( 4 + 28, 4 );
	for( i = 0; i < 3; i++ )
	{
		size_t p = nodes + (size_t)i * COF_NAV_NODE;
		int first = written;
		put_float( p, at[i][0] ); put_float( p + 4, at[i][1] ); put_float( p + 8, -28 ); /* 8 over a floor 36 under the bodies */
		put_int( p + 28, COF_NAV_LAND ); put_int( p + 36, first );
		for( k = 0; k < 4; k++ )
		{
			size_t q = link_at + (size_t)written * COF_NAV_LINK;
			if( links[k][0] != i ) continue;
			put_int( q, links[k][0] ); put_int( q + 4, links[k][1] ); put_int( q + 16, COF_NAV_HUMAN | 1 );
			if( graph_door && links[k][0] + links[k][1] == 3 ) memcpy( graph_file + q + 12, "*5", 2 );
			written++;
		}
		put_int( p + 32, written - first );
	}
	return size;
}
static byte *FS_LoadFile( const char *path, fs_offset_t *size, qboolean gamedironly )
{
	byte *copy;
	size_t length;
	loads++;
	if( !have_graph || strcmp( path, "maps/graphs/c_test.nod" )) return NULL;
	length = build_graph();
	copy = malloc( length ); memcpy( copy, graph_file, length );
	*size = (fs_offset_t)length;
	return copy;
}
static void Mem_Free( void *p ) { free( p ); }

#include "companion_under_test.c"

static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities )); memset( clients, 0, sizeof( clients ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	gate = 1; connect_ok = 1; put_in = placed = ready = equips = runs = wall = inits = 0; next_userid = 200;
	supplies = revives = reviving = repairs = fallen = gives = command_count = 0; revive_ok = 1; free_slots = 4;
	memset( commands, 0, sizeof( commands )); last_equip[0] = 0;
	arms_weapon = COF_ARMS_MELEE; arms_has_gun = arms_has_heal = arms_sights = 0; arms_clip = arms_reserve = 0; arms_has_melee = 1;
	memset( in_game, 0, sizeof( in_game )); in_game[1] = 1;
	cvars[0].value = 0; cvars[2].value = 0; cvars[3].value = 8; cvars[5].value = 90; cvars[6].value = 1; cvars[7].value = 1;
	sv.state = ss_active; sv.time = 100; sv.frametime = 0.05; svs.maxclients = 2; svs.clients = clients; svs.spawncount++;
	svgame.numEntities = 16; svgame.dllFuncs.pfnClientConnect = game_connect; svgame.dllFuncs.pfnClientPutInServer = game_put_in;
	svgame.dllFuncs.pfnUse = game_use; door_x = ledge_x = uses = 0; ledge_top = 0.0f; used = user = NULL; box_count = 0;
	place_ok = 1; place_rank = place_anywhere = place_had_near = in_door = 0;
	strcpy( sv.name, "c_test" ); have_graph = graph_door = loads = 0; gap_x = gap_y = 0.0f;
	for( i = 3; i < 16; i++ ) entities[i].free = 1;
	SV_CoF_CompanionInit();
	SV_CoF_CompanionReset();
	/* the host, alive, at the origin */
	clients[0].state = cs_spawned; clients[0].userid = 1; clients[0].edict = &entities[1]; strcpy( clients[0].name, "Host" );
	entities[1].v.classname = 1; entities[1].v.health = 100; entities[1].v.movetype = MOVETYPE_WALK; entities[1].v.view_ofs[2] = 28;
	entities[2].free = 0; entities[2].v.health = 77; /* a stale object from the slot's last player */
}
static void frame( double dt ) { sv.time += dt; SV_CoF_CompanionFrame(); }
/* the first frame of the map, then the two seconds the host gets to settle */
static void settle( void ) { frame( 0.1 ); frame( 2.5 ); }
static int logged( const char *s ) { int n = 0; const char *at = log_text; while(( at = strstr( at, s ))) { n++; at++; } return n; }
static int commanded( const char *s ) { int i, n = 0; for( i = 0; i < command_count; i++ ) n += !strcmp( commands[i], s ); return n; }
static edict_t *monster( int i, string_t classname, float x )
{
	edict_t *e = &entities[i];
	memset( e, 0, sizeof( *e ));
	e->v.classname = classname; e->v.flags = FL_MONSTER; e->v.takedamage = DAMAGE_AIM; e->v.health = 50;
	e->v.origin[0] = x; e->v.absmin[0] = x - 16; e->v.absmax[0] = x + 16; e->v.absmin[2] = 0; e->v.absmax[2] = 72;
	return e;
}

int main( void )
{
	edict_t *bot;
	int i;

	/* Off: nobody is added. On: one bot joins once the host is in (after the map settles). */
	setup(); frame( 0.1 ); frame( 3.0 ); assert( clients[1].state == cs_free && !put_in );
	setup(); cvars[0].value = 1; frame( 0.1 ); assert( clients[1].state == cs_free ); /* settling */
	frame( 2.5 );
	assert( clients[1].state == cs_spawned && ( clients[1].flags & FCL_FAKECLIENT ) && put_in == 1 && ready == 1 );
	assert( logged( "phase=companion-added userid=200 name=Companion" ) == 1 && SV_CoF_CompanionDriven( &clients[1] ));
	/* it is driven every frame through the player code */
	runs = 0; frame( 0.05 ); assert( runs == 1 && last_run.msec == 50 );
	/* never more than asked */
	frame( 2.0 ); frame( 2.0 ); assert( put_in == 1 );
	/* turned off: it leaves */
	cvars[0].value = 0; frame( 0.1 ); assert( clients[1].state == cs_free && logged( "companion-removed" ) == 1 );
	/* the game refuses the connection: the slot is freed again */
	setup(); cvars[0].value = 1; connect_ok = 0; settle(); assert( clients[1].state == cs_free && logged( "companion-refused reason=nope" ) == 1 );
	/* not beside a dead host */
	setup(); cvars[0].value = 1; entities[1].v.health = 0; settle(); assert( clients[1].state == cs_free );

	/* A new map: the engine dropped the bot; it comes back once the host is in. */
	setup(); cvars[0].value = 1; settle(); assert( clients[1].state == cs_spawned );
	clients[1].state = cs_free; svs.spawncount++; frame( 0.1 ); assert( clients[1].state == cs_free );
	frame( 2.5 ); assert( clients[1].state == cs_spawned && put_in == 2 );

	/* A person wants to join a full server: the bot gives its place while they play, and comes
	 * back once they leave. */
	setup(); cvars[0].value = 1; settle(); assert( clients[1].state == cs_spawned );
	assert( SV_CoF_CompanionMakeRoom() && clients[1].state == cs_free );
	clients[1].state = cs_spawned; clients[1].userid = 50; clients[1].edict = &entities[2]; /* the friend took it */
	frame( 1.0 ); frame( 5.0 ); assert( put_in == 1 && clients[1].userid == 50 );
	clients[1].state = cs_free; frame( 0.1 ); frame( 2.5 );
	assert( put_in == 2 && clients[1].state == cs_spawned && ( clients[1].flags & FCL_FAKECLIENT ));
	/* nothing to give: false */
	setup(); assert( !SV_CoF_CompanionMakeRoom() );

	/* Perception: a hostile monster in sight near the leader becomes the target;
	 * a friendly one, one behind a wall, and one far from the leader do not. */
	setup(); cvars[0].value = 1; settle(); bot = &entities[2]; bot->v.origin[0] = 50;
	monster( 5, 3, 300 ); frame( 0.05 ); assert( !logged( "mode=fight" ));
	monster( 6, 2, 300 ); frame( 0.05 ); assert( logged( "mode=fight" ) == 1 );
	/* the host stands in the line of fire: it holds fire (no attack) */
	arms_weapon = COF_ARMS_GUN; arms_has_gun = 1; arms_clip = 5; arms_reserve = 5; arms_sights = 1;
	entities[1].v.origin[0] = 200; bot->v.v_angle[1] = 0; frame( 1.0 ); frame( 1.0 );
	assert( !( last_run.buttons & COF_IN_ATTACK ));
	entities[1].v.origin[0] = 0; entities[1].v.origin[1] = 200; frame( 1.0 );
	assert( last_run.buttons & COF_IN_ATTACK );
	/* out of sight: no target */
	setup(); cvars[0].value = 1; settle(); entities[2].v.origin[0] = 50; wall = 1;
	monster( 6, 2, 1200 ); frame( 0.05 ); assert( !logged( "mode=fight" ));
	/* dead or hidden monsters are not threats */
	wall = 0; entities[6].v.origin[0] = 300; entities[6].v.absmin[0] = 284; entities[6].v.absmax[0] = 316;
	entities[6].v.health = 0; frame( 0.05 ); assert( !logged( "mode=fight" ));
	entities[6].v.health = 50; entities[6].v.effects = EF_NODRAW; frame( 0.05 ); assert( !logged( "mode=fight" ));

	/* Far from the leader: teleported behind them. */
	setup(); cvars[0].value = 1; settle(); entities[2].v.origin[0] = 3000; frame( 0.05 );
	assert( placed == 1 && entities[2].v.origin[0] == -64 && logged( "companion-teleport userid=200 leader=1 reason=far" ) == 1 );

	/* An ordinary door shut across its way: opened through the game's own use (never the
	 * use key: that takes whatever the game finds first, a story key included). */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle(); bot = &entities[2];
	bot->v.origin[0] = 0; bot->v.v_angle[1] = 0;
	entities[9].free = 0; entities[9].v.classname = 8; door_x = 20;
	frame( 0.05 ); frame( 0.05 );
	assert( uses == 1 && used == &entities[9] && user == bot && logged( "companion-door userid=200 entity=9" ) == 1 );
	frame( 0.3 ); assert( uses == 1 ); /* once a second */
	frame( 0.8 ); assert( uses == 2 );
	/* a rotating door too; never one that fires something or that a trigger opens (the story's) */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle();
	entities[9].free = 0; entities[9].v.classname = 9; door_x = 20;
	frame( 0.05 ); frame( 0.05 ); assert( uses == 1 );
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle();
	entities[9].free = 0; entities[9].v.classname = 8; entities[9].v.target = 7; door_x = 20;
	frame( 0.05 ); frame( 0.05 ); frame( 1.0 ); assert( uses == 0 && !logged( "companion-door" ));
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle();
	entities[9].free = 0; entities[9].v.classname = 8; entities[9].v.targetname = 7; door_x = 20;
	frame( 0.05 ); frame( 0.05 ); frame( 1.0 ); assert( uses == 0 );

	/* A low box a jump clears (blocked at the feet and at step height, clear at jump height):
	 * one hop, logged. A stair step (clear at step height): the walk climbs it, no hop. */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle(); ledge_x = 20; ledge_top = 26;
	for( i = 0; i < 4; i++ ) { entities[2].v.origin[0] = 0; frame( 0.05 ); }
	assert( logged( "companion-jump userid=200 reason=ledge" ) == 1 );
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle(); ledge_x = 20; ledge_top = 12;
	for( i = 0; i < 10; i++ ) { frame( 0.05 ); assert( !( last_run.buttons & COF_IN_JUMP )); }
	assert( !logged( "companion-jump" ));

	/* Stairs (8-unit risers every 10) are climbed by the walk: never a jump, wherever on the
	 * flight it stands (live 2026-10-14: 6 hops in 13 s at the foot of c_apartment2's stairwell).
	 * A 30-unit box is jumped; a 60-unit wall and a single 16-unit step are not. */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; entities[1].v.origin[2] = 64; settle();
	for( i = 1; i <= 8; i++ ) add_box( 100.0f + 10.0f * ( i - 1 ), -500, 1000, 500, -1000, FLOOR + 8.0f * i );
	for( i = 0; i < 40; i++ )
	{
		float x = 60.0f + 4.0f * i, top = FLOOR;
		int k;
		for( k = 0; k < box_count; k++ ) if( x + 16 > boxes[k].x0 && boxes[k].z1 > top ) top = boxes[k].z1;
		entities[2].v.origin[0] = x; entities[2].v.origin[1] = 0; entities[2].v.origin[2] = top + 36; entities[2].v.flags |= FL_ONGROUND;
		frame( 0.05 );
	}
	assert( !logged( "companion-jump" ) && logged( "mode=follow" ));
	{
		static const float heights[] = { 30, 60, 16 };
		int k;
		for( k = 0; k < 3; k++ )
		{
			setup(); cvars[0].value = 1; entities[1].v.origin[0] = 600; settle();
			add_box( 30, -500, 400, 500, -1000, FLOOR + heights[k] );
			for( i = 0; i < 4; i++ ) { entities[2].v.origin[0] = 0; entities[2].v.origin[2] = 0; frame( 0.05 ); }
			assert( logged( "companion-jump userid=200 reason=ledge" ) == ( k == 0 ));
		}
	}

	/* An idle second person (solo testing): the AI takes over after the idle time and
	 * gives it back the moment their controls move. */
	setup(); cvars[2].value = 1;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Guest" );
	entities[2].v.classname = 1; entities[2].v.health = 100; entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.origin[0] = 600;
	{
		usercmd_t cmd; memset( &cmd, 0, sizeof( cmd ));
		frame( 0.05 ); /* the map's first frame */
		SV_CoF_CompanionUsercmd( &clients[1], &cmd ); /* first command: the AI knows the client */
		frame( 0.05 ); assert( SV_CoF_CompanionDriven( &clients[1] ) && logged( "companion-driving userid=2" ) == 1 );
		memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( cmd.forwardmove != 0.0f && entities[2].v.fixangle == 1 ); /* the AI walks it to the host */
		/* the person moves: released at once, and left alone for the idle time */
		memset( &cmd, 0, sizeof( cmd )); cmd.forwardmove = 200; SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( cmd.forwardmove == 200 && !SV_CoF_CompanionDriven( &clients[1] ) && logged( "companion-released userid=2" ) == 1 );
		frame( 1.0 ); assert( !SV_CoF_CompanionDriven( &clients[1] ));
		frame( 8.0 ); assert( SV_CoF_CompanionDriven( &clients[1] ));
		/* a mouse turn counts as a person too */
		memset( &cmd, 0, sizeof( cmd )); cmd.viewangles[1] = 120; SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( !SV_CoF_CompanionDriven( &clients[1] ));
		/* driving off: the person's commands are never touched */
		cvars[2].value = 0; frame( 20.0 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( cmd.forwardmove == 0.0f && !SV_CoF_CompanionDriven( &clients[1] ));
	}

	/* The client echoes the view the AI handed it some usercmds late (live 2026-10-12:
	 * read as a turned mouse, the AI let go of the guest within a second, every 8 s,
	 * all intro): it stays the AI's while the AI keeps turning after the host. */
	setup(); cvars[2].value = 1;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Guest" );
	entities[2].v.classname = 1; entities[2].v.health = 100; entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.origin[0] = 600;
	/* the person looked down before walking off: the model pitch is -v_angle/3 (review) */
	entities[2].v.v_angle[0] = 30; entities[2].v.angles[0] = -10;
	{
		usercmd_t cmd; float echo[4][3], turned = 0.0f, first = 0.0f;
		memset( &cmd, 0, sizeof( cmd )); memset( echo, 0, sizeof( echo ));
		cmd.viewangles[0] = 30; for( i = 0; i < 4; i++ ) echo[i][0] = 30; /* the client's own view until the AI sets it */
		frame( 0.05 ); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[0], &cmd ); /* the host idles too */
		frame( 0.05 ); assert( SV_CoF_CompanionDriven( &clients[1] ));
		for( i = 0; i < 24; i++ )
		{
			entities[2].v.origin[1] = 30.0f * i; /* the guest drifts sideways: the AI turns to keep facing the host */
			frame( 0.05 );
			memset( &cmd, 0, sizeof( cmd )); VectorCopy( echo[i % 4], cmd.viewangles ); /* what it was handed 4 usercmds ago */
			SV_CoF_CompanionUsercmd( &clients[1], &cmd );
			if( i == 4 ) first = cmd.viewangles[1];
			turned = fabsf( cmd.viewangles[1] - first );
			VectorCopy( entities[2].v.angles, echo[i % 4] ); /* what svc_setangle carries to the client */
		}
		assert( turned > 5.0f ); /* the AI really turned meanwhile */
		assert( SV_CoF_CompanionDriven( &clients[1] ) && !logged( "companion-released" ));
		/* the host, idle as long, is the one the AI follows: never "away", always holds an exit */
		assert( SV_CoF_CompanionAway( &clients[1] ) && !SV_CoF_CompanionAway( &clients[0] ) && cof_companions[0].have_view );
		/* a person's mouse is still a person */
		memset( &cmd, 0, sizeof( cmd )); cmd.viewangles[1] = 30; cmd.viewangles[0] = 40; SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( !SV_CoF_CompanionDriven( &clients[1] ) && !SV_CoF_CompanionAway( &clients[1] ));
		frame( 9.0 ); assert( SV_CoF_CompanionAway( &clients[1] )); /* left alone again */
		cvars[2].value = 0; assert( !SV_CoF_CompanionAway( &clients[1] ));
	}

	/* Orders: H (cof_companion_order) makes every companion wait where it stands, then follow
	 * again; the person who gave the order is told. Other commands are the game's. */
	setup(); cvars[0].value = 1; settle(); entities[2].v.origin[0] = 3000; memset( told, 0, sizeof( told ));
	argv0 = "cof_companion_order"; argc = 1;
	assert( SV_CoF_CompanionClientCommand( &clients[0] ) && !strcmp( told[0], "Your companion waits here." ));
	placed = 0; frame( 0.05 ); frame( 5.0 ); assert( !placed && entities[2].v.origin[0] == 3000 ); /* waits, however far */
	assert( SV_CoF_CompanionClientCommand( &clients[0] ) && !strcmp( told[0], "Your companion follows you." ));
	frame( 0.05 ); assert( placed == 1 );
	argv0 = "say"; assert( !SV_CoF_CompanionClientCommand( &clients[0] )); argv0 = "";
	setup(); argv0 = "cof_companion_order"; assert( SV_CoF_CompanionClientCommand( &clients[0] ) && !strcmp( told[0], "No companion to give orders to." )); argv0 = "";
	/* Out of a fight a bot recovers its health; in one, it does not. */
	setup(); cvars[0].value = 1; settle(); entities[2].v.health = 40; frame( 1.0 ); frame( 1.0 );
	assert( entities[2].v.health == 40 ); /* calm only just began */
	frame( 5.0 ); frame( 1.0 ); assert( entities[2].v.health > 40 && entities[2].v.health <= 100 );
	{
		float before;
		entities[2].v.origin[0] = 50; monster( 6, 2, 300 ); frame( 0.05 ); before = entities[2].v.health; frame( 1.0 );
		assert( entities[2].v.health == before );
	}

	/* A bot's slot is cleaned before the game spawns it (no stale object from the slot's last
	 * player); the game's Spawn takes FL_FAKECLIENT off the edict, the client flag stays. */
	setup(); cvars[0].value = 1; settle();
	assert( inits == 1 && put_in == 1 && entities[2].v.health == 100 && !( entities[2].v.flags & FL_FAKECLIENT ) && ( clients[1].flags & FCL_FAKECLIENT ));
	assert( repairs > 0 ); /* stale CPR links are looked after every frame */
	/* The game takes four players at most: no bot beyond that, even with a slot free. */
	setup(); svs.maxclients = 5; cvars[0].value = 1;
	for( i = 1; i < 4; i++ ) { clients[i].state = cs_connected; clients[i].userid = 10 + i; clients[i].edict = &entities[i + 1]; entities[i + 1].free = 0; }
	settle(); assert( !put_in );

	/* A bot sets off no story trigger, exit or item; hurt and push still reach it. People are not filtered. */
	setup(); cvars[0].value = 1; settle();
	entities[7].free = 0; entities[7].v.classname = 5; /* trigger_once */
	entities[8].free = 0; entities[8].v.classname = 6; /* trigger_hurt */
	assert( SV_CoF_CompanionTriggerTouch( &entities[7], &entities[2] ) && !SV_CoF_CompanionTriggerTouch( &entities[8], &entities[2] ));
	assert( !SV_CoF_CompanionTriggerTouch( &entities[7], &entities[1] ));
	entities[8].v.target = 7; assert( SV_CoF_CompanionTriggerTouch( &entities[8], &entities[2] )); /* a hurt volume that fires something */
	gate = 0; assert( !SV_CoF_CompanionTriggerTouch( &entities[7], &entities[2] )); gate = 1;

	/* Upkeep: a bot never runs dry, carries a syringe of its own, keeps its flashlight on and
	 * gets up after a hard landing. */
	setup(); cvars[0].value = 1; settle();
	assert( supplies > 0 && gives == 1 && logged( "companion-syringe userid=200" ) == 1 && commanded( "flashlight" ) == 1 );
	command_count = 0; entities[2].v.frags = 32; frame( 3.0 ); assert( !commanded( "flashlight" )); /* on: left alone */
	entities[2].v.frags = 0; frame( 0.05 ); assert( commanded( "flashlight" ) == 1 );
	frame( 0.5 ); assert( commanded( "flashlight" ) == 1 ); /* asked again only every two seconds */
	fallen = 1; frame( 0.05 ); assert( commanded( "tapgetup" ) == 1 ); fallen = 0;
	/* a nearly full inventory: no syringe (the story's weapons come first); everything off: nothing */
	setup(); cvars[0].value = 1; free_slots = 1; settle(); assert( !gives );
	setup(); cvars[0].value = 1; cvars[5].value = 0; cvars[6].value = 0; cvars[7].value = 0; settle();
	assert( !gives && !supplies && !commanded( "flashlight" ));

	/* The host goes down beside the bot: it says it is coming, starts the game's CPR once and
	 * holds +use while it runs. */
	setup(); cvars[0].value = 1; settle(); memset( told, 0, sizeof( told ));
	entities[2].v.origin[0] = 40; entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	frame( 0.05 );
	assert( revives == 1 && logged( "companion-cpr userid=200 leader=1" ) == 1 && !strcmp( told[0], "Companion is coming to help you up." ));
	frame( 0.05 ); assert( revives == 1 && ( last_run.buttons & COF_IN_USE ));
	/* done: the host plays its get-up, still dead: no second CPR, no second announcement */
	reviving = 0; entities[1].v.fuser3 = 3; memset( told, 0, sizeof( told ));
	frame( 0.05 ); frame( 1.5 ); assert( revives == 1 && !told[0][0] && !( last_run.buttons & COF_IN_USE ));
	/* the game refused: again a second later, and no +use meanwhile (it would use whatever is nearest) */
	setup(); cvars[0].value = 1; settle(); revive_ok = 0;
	entities[2].v.origin[0] = 40; entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	frame( 0.05 ); frame( 0.05 ); assert( revives == 1 && !( last_run.buttons & COF_IN_USE ));
	frame( 1.0 ); assert( revives == 2 );

	/* While nobody else is alive a bot stays (its leaving would end the game), then goes. */
	setup(); cvars[0].value = 1; settle(); entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	cvars[0].value = 0; frame( 0.1 ); assert( clients[1].state == cs_spawned );
	assert( !SV_CoF_CompanionMakeRoom() && clients[1].state == cs_spawned );
	entities[1].v.health = 100; entities[1].v.deadflag = DEAD_NO; frame( 0.1 ); assert( clients[1].state == cs_free );

	/* Of two monsters, the one already after a player goes first, even somewhat farther away. */
	setup(); cvars[0].value = 1; settle(); entities[2].v.origin[0] = 50;
	monster( 5, 2, 300 );
	monster( 6, 4, 50 ); entities[6].v.origin[1] = 450; entities[6].v.absmin[1] = 434; entities[6].v.absmax[1] = 466;
	for( i = 0; i < 6; i++ ) frame( 0.05 );
	assert( last_run.viewangles[1] > 45.0f );
	/* a corpse keeps its health but takes no aimed damage: no threat */
	setup(); cvars[0].value = 1; settle(); entities[2].v.origin[0] = 50;
	monster( 6, 2, 300 ); entities[6].v.takedamage = 1; frame( 0.05 ); assert( !logged( "mode=fight" ));

	/* Driving a person's client: a press the AI makes reaches the game once, then is released. */
	setup(); cvars[2].value = 1; arms_weapon = COF_ARMS_GUN; arms_has_gun = 1; arms_clip = 5; arms_reserve = 5; arms_sights = 1;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Guest" );
	entities[2].v.classname = 1; entities[2].v.health = 100; entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.origin[0] = 100;
	monster( 6, 2, 500 );
	{
		usercmd_t cmd;
		int pressed = 0;
		frame( 0.05 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		for( i = 0; i < 20 && !pressed; i++ )
		{
			frame( 0.05 );
			memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
			pressed = ( cmd.buttons & COF_IN_ATTACK ) != 0;
		}
		assert( pressed );
		memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( !( cmd.buttons & COF_IN_ATTACK ) && SV_CoF_CompanionDriven( &clients[1] ));
		/* a press latched when driving stops is dropped, never handed over at a later drive */
		frame( 0.3 ); frame( 0.3 ); assert( SV_CoF_CompanionDriven( &clients[1] ) && ( cof_companions[1].taps & COF_IN_ATTACK ));
		cvars[2].value = 0; frame( 0.05 ); entities[6].free = 1; arms_sights = 0; cvars[2].value = 1; frame( 0.05 );
		memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		assert( !( cmd.buttons & COF_BRAIN_TAPS ));
	}
	/* a person's syringes are their own: the AI driving them neither injects nor gives one */
	setup(); cvars[2].value = 1; arms_has_heal = 1;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Guest" );
	entities[2].v.classname = 1; entities[2].v.health = 10; entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.origin[0] = 60;
	entities[1].v.health = 30;
	{
		usercmd_t cmd;
		frame( 0.05 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		for( i = 0; i < 40; i++ ) { frame( 0.25 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd ); }
		assert( SV_CoF_CompanionDriven( &clients[1] ) && strcmp( last_equip, "weapon_syringe" ) && !logged( "mode=medic" ) && !logged( "mode=heal" ));
	}
	/* a bot holding no weapon at all with a gun carried draws the gun when calm */
	setup(); cvars[0].value = 1; arms_weapon = COF_ARMS_NONE; arms_has_gun = 1; arms_has_melee = 0; settle(); frame( 2.0 );
	assert( !strcmp( last_equip, "weapon_glock" ) && logged( "companion-equip userid=200 weapon=weapon_glock" ) == 1 );

	/* ...and a button the AI holds frame after frame (a melee swing) reaches it pressed, released, pressed. */
	setup(); cvars[2].value = 1;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Guest" );
	entities[2].v.classname = 1; entities[2].v.health = 100; entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.origin[0] = 100;
	monster( 6, 2, 120 );
	{
		usercmd_t cmd;
		int pressed = 0;
		frame( 0.05 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
		for( i = 0; i < 20 && !pressed; i++ )
		{
			frame( 0.05 );
			memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd );
			pressed = ( cmd.buttons & COF_IN_ATTACK ) != 0;
		}
		assert( pressed );
		frame( 0.05 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd ); assert( !( cmd.buttons & COF_IN_ATTACK ));
		frame( 0.05 ); memset( &cmd, 0, sizeof( cmd )); SV_CoF_CompanionUsercmd( &clients[1], &cmd ); assert( cmd.buttons & COF_IN_ATTACK );
	}

	/* The map's route graph: the host stands behind a wall with a gap at its far end. The
	 * straight way is blocked, so it heads for the gap along the graph (N0, N1, N2; N1 is
	 * already straight ahead, so straight at N1), not into the wall. */
	setup(); cvars[0].value = 1; have_graph = 1; gap_x = 500; gap_y = 300;
	entities[1].v.origin[0] = 800; settle(); bot = &entities[2]; bot->v.origin[0] = 200;
	for( i = 0; i < 6; i++ ) frame( 0.05 );
	assert( logged( "companion-nav map=c_test nodes=3 links=4" ) == 1 && logged( "companion-route userid=200 nodes=3" ) == 1 );
	assert( last_run.forwardmove > 0 && last_run.viewangles[1] > 50 && last_run.viewangles[1] < 56 ); /* to (500,400): 53 degrees */
	assert( loads == 1 && !logged( "companion-teleport" ));
	/* through the gap the host is in sight and the way straight: no route, straight at them */
	bot->v.origin[0] = 480; bot->v.origin[1] = 390;
	for( i = 0; i < 6; i++ ) frame( 0.05 );
	assert( last_run.forwardmove > 0 && last_run.viewangles[1] < -45 && last_run.viewangles[1] > -56 ); /* to (800,0) */
	/* Stuck on the way: first another route without that leg (there is none here), then
	 * the usual teleport. */
	setup(); cvars[0].value = 1; have_graph = 1; gap_x = 500; gap_y = 300;
	entities[1].v.origin[0] = 800; settle(); bot = &entities[2]; bot->v.origin[0] = 200;
	for( i = 0; i < 100 && !logged( "companion-teleport" ); i++ ) frame( 0.1 );
	assert( logged( "companion-replan userid=200 link=" ) == 1 && logged( "companion-route userid=200 none" ) == 1 );
	assert( logged( "companion-teleport userid=200 leader=1 reason=lost" ) == 1 );
	/* A shut door across N1-N2: one of the story's (a trigger opens it) leaves no way; an
	 * ordinary one (it may open it) does. */
	setup(); cvars[0].value = 1; have_graph = 1; graph_door = 1; gap_x = 500; gap_y = 300; door_x = 650;
	entities[9].free = 0; entities[9].v.classname = 8; entities[9].v.model = 10; entities[9].v.targetname = 7;
	entities[1].v.origin[0] = 800; settle(); entities[2].v.origin[0] = 200;
	frame( 0.05 ); frame( 0.05 );
	assert( logged( "companion-route userid=200 none" ) == 1 && !logged( "companion-route userid=200 nodes" ));
	setup(); cvars[0].value = 1; have_graph = 1; graph_door = 1; gap_x = 500; gap_y = 300; door_x = 650;
	entities[9].free = 0; entities[9].v.classname = 8; entities[9].v.model = 10;
	entities[1].v.origin[0] = 800; settle(); entities[2].v.origin[0] = 200;
	frame( 0.05 ); frame( 0.05 );
	assert( logged( "companion-route userid=200 nodes=3" ) == 1 );
	/* No graph for the map: said once, looked for again every ten seconds (the game builds a
	 * missing one after the map starts), read once it is there; read again on a new map. */
	setup(); cvars[0].value = 1; gap_x = 500; gap_y = 300;
	entities[1].v.origin[0] = 800; settle();
	for( i = 0; i < 25; i++ ) { entities[2].v.origin[0] = 200; entities[2].v.origin[1] = 0; frame( 0.2 ); }
	assert( logged( "companion-nav map=c_test graph=none" ) == 1 && loads == 1 );
	have_graph = 1;
	for( i = 0; i < 30; i++ ) { entities[2].v.origin[0] = 200; entities[2].v.origin[1] = 0; frame( 0.2 ); }
	assert( logged( "companion-nav map=c_test graph=none" ) == 1 && logged( "companion-nav map=c_test nodes=3" ) == 1 && loads == 2 );
	clients[1].state = cs_free; svs.spawncount++; frame( 0.1 ); frame( 2.5 ); assert( clients[1].state == cs_spawned );
	for( i = 0; i < 4; i++ ) { entities[2].v.origin[0] = 200; entities[2].v.origin[1] = 0; frame( 0.05 ); }
	assert( logged( "companion-nav map=c_test nodes=3" ) == 2 && loads == 3 );

	/* Three AI companions (a party of four): one a second after the settle, with names of their own. */
	setup(); svs.maxclients = 4; cvars[0].value = 3; settle(); frame( 1.0 ); frame( 1.0 );
	assert( put_in == 3 && clients[1].state == cs_spawned && clients[2].state == cs_spawned && clients[3].state == cs_spawned );
	assert( !strcmp( clients[1].name, "Companion" ) && !strcmp( clients[2].name, "Companion 2" ) && !strcmp( clients[3].name, "Companion 3" ));
	/* the host is down: a friend still gets a place while another bot stays up to help */
	entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	assert( SV_CoF_CompanionMakeRoom() && clients[3].state == cs_free );
	assert( SV_CoF_CompanionMakeRoom() && clients[2].state == cs_free );
	assert( !SV_CoF_CompanionMakeRoom() && clients[1].state == cs_spawned ); /* the last one up stays */
	/* back up: the missing two return, under the free names */
	entities[1].v.health = 100; entities[1].v.deadflag = DEAD_NO; frame( 1.0 ); frame( 1.0 ); frame( 1.0 );
	assert( put_in == 5 && !strcmp( clients[2].name, "Companion 2" ) && !strcmp( clients[3].name, "Companion 3" ));
	/* none of them up either: no place is given */
	entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	for( i = 2; i <= 4; i++ ) { entities[i].v.health = 0; entities[i].v.deadflag = DEAD_DEAD; }
	assert( !SV_CoF_CompanionMakeRoom() && clients[3].state == cs_spawned );

	/* The host hurt with three bots beside them: one syringe for the party per cooldown, not three in a row. */
	setup(); svs.maxclients = 4; cvars[0].value = 3; settle(); frame( 1.0 ); frame( 1.0 );
	arms_has_heal = 1; arms_weapon = COF_ARMS_HEAL; entities[1].v.health = 40;
	for( i = 2; i <= 4; i++ ) entities[i].v.origin[0] = 10.0f * i;
	for( i = 0; i < 100; i++ ) frame( 0.05 );
	assert( logged( "mode=medic" ) == 1 );

	/* The host goes down among three bots: told once that help is coming; the order reply speaks of them all. */
	setup(); svs.maxclients = 4; cvars[0].value = 3; settle(); frame( 1.0 ); frame( 1.0 ); memset( told_count, 0, sizeof( told_count ));
	for( i = 2; i <= 4; i++ ) entities[i].v.origin[0] = 300.0f;
	entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	frame( 0.05 ); frame( 0.05 ); assert( told_count[0] == 1 && strstr( told[0], "coming to help you up" ));
	entities[1].v.health = 100; entities[1].v.deadflag = DEAD_NO;
	argv0 = "cof_companion_order"; argc = 1;
	assert( SV_CoF_CompanionClientCommand( &clients[0] ) && !strcmp( told[0], "Your companions wait here." ));
	assert( SV_CoF_CompanionClientCommand( &clients[0] ) && !strcmp( told[0], "Your companions follow you." )); argv0 = "";

	/* Line of fire: a bot holds fire while another player (another bot here) stands in it. */
	setup(); svs.maxclients = 3; cvars[0].value = 2; settle(); frame( 1.0 );
	arms_weapon = COF_ARMS_GUN; arms_has_gun = 1; arms_clip = 10; arms_reserve = 10;
	entities[1].v.origin[1] = 300; /* the host aside */
	entities[2].v.origin[0] = -100; entities[3].v.origin[0] = 100; monster( 6, 2, 300 );
	{
		int first = 0, second = 0;
		for( i = 0; i < 40; i++ )
		{
			entities[2].v.origin[0] = -100; entities[2].v.origin[1] = 0; entities[3].v.origin[0] = 100; entities[3].v.origin[1] = 0;
			frame( 0.05 );
			first += ( ran[1].buttons & COF_IN_ATTACK ) != 0; second += ( ran[2].buttons & COF_IN_ATTACK ) != 0;
		}
		assert( !first && second > 0 );
	}

	/* Two people and two bots: each bot looks after one of them (the far one is followed by
	 * the second bot), and a downed person gets CPR from the nearest bot. */
	setup(); svs.maxclients = 4; cvars[0].value = 2;
	clients[1].state = cs_spawned; clients[1].userid = 2; clients[1].edict = &entities[2]; strcpy( clients[1].name, "Friend" );
	memset( &entities[2].v, 0, sizeof( entities[2].v )); entities[2].v.classname = 1; entities[2].v.health = 100;
	entities[2].v.movetype = MOVETYPE_WALK; entities[2].v.view_ofs[2] = 28; in_game[2] = 1;
	settle(); frame( 1.0 );
	assert( put_in == 2 && clients[2].state == cs_spawned && clients[3].state == cs_spawned );
	entities[2].v.origin[0] = 2000; frame( 0.05 ); frame( 0.05 );
	assert( logged( "companion-teleport userid=201 leader=2" ) == 1 && !logged( "companion-teleport userid=200" ));
	assert( entities[4].v.origin[0] > 1500 && entities[3].v.origin[0] < 100 );
	memset( told, 0, sizeof( told ));
	entities[2].v.origin[0] = 1960; entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	entities[4].v.origin[0] = 1936; frame( 0.05 );
	assert( logged( "companion-cpr userid=201 leader=2" ) == 1 && !logged( "companion-cpr userid=200" ));
	assert( strstr( told[1], "coming to help you up" ) && !told[0][0] );

	/* Through a door after the host: the game takes them 600 units on in one frame, behind a wall;
	 * the bot walks to where they left and comes out near where they arrived (not in front of them). */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 900; settle(); wall = 1;
	entities[2].v.origin[0] = 700; frame( 0.05 );
	entities[1].v.origin[0] = 1500; frame( 0.05 );
	assert( !placed && cof_companions[1].brain.door );
	entities[2].v.origin[0] = 880; frame( 0.05 );
	assert( placed == 1 && place_had_near && place_near[0] == 1500 && logged( "reason=door" ) == 1 );
	/* the teleport line says where everyone was */
	assert( logged( "companion-teleport userid=200 leader=1 reason=door at=880,0,0 leader_at=1500,0,0" ) == 1 );
	/* no spot out of the player's sight: asked again each frame, then (after 3 s) the least seen */
	setup(); cvars[0].value = 1; settle(); place_ok = 0; entities[2].v.origin[0] = 3000; frame( 0.05 );
	assert( !placed && !logged( "companion-teleport" ));
	for( i = 0; i < 20; i++ ) frame( 0.1 );
	assert( !placed );
	for( i = 0; i < 15 && !placed; i++ ) frame( 0.1 );
	assert( placed == 1 && place_anywhere && logged( "reason=far" ) == 1 );
	/* the host held by the game (a cutscene, a door): nobody teleports meanwhile */
	setup(); cvars[0].value = 1; settle(); entities[1].v.flags |= FL_FROZEN; entities[2].v.origin[0] = 3000;
	for( i = 0; i < 50; i++ ) frame( 0.1 );
	assert( !placed );
	entities[1].v.flags &= ~FL_FROZEN; in_door = 1;
	for( i = 0; i < 10; i++ ) frame( 0.1 );
	assert( !placed ); in_door = 0;
	frame( 0.1 ); frame( 0.6 ); assert( placed == 1 );

	/* Two bots behind one person keep their own distances; the second comes along behind
	 * the first (in the way: arrived, not stuck), and two bots on one spot step apart. */
	setup(); svs.maxclients = 3; cvars[0].value = 2; settle(); frame( 1.0 );
	assert( clients[1].state == cs_spawned && clients[2].state == cs_spawned );
	entities[1].v.origin[0] = 0; entities[2].v.origin[0] = -150; entities[2].v.origin[1] = 20;
	entities[3].v.origin[0] = -150; entities[3].v.origin[1] = -40;
	frame( 0.05 ); frame( 0.05 );
	assert( cof_companions[1].out.mode == COF_BRAIN_FOLLOW && cof_companions[2].out.mode == COF_BRAIN_IDLE );
	assert( place_rank == 0 );
	setup(); svs.maxclients = 3; cvars[0].value = 2; entities[1].v.origin[0] = 800; settle(); frame( 1.0 );
	for( i = 0; i < 80; i++ )
	{
		/* the first walks on toward the host; the second is right behind it */
		entities[2].v.origin[0] = 8.0f * i; entities[3].v.origin[0] = 8.0f * i - 40; entities[2].v.origin[1] = entities[3].v.origin[1] = 0;
		frame( 0.05 );
		assert( !logged( "companion-teleport" ));
	}
	assert( cof_companions[2].out.mode == COF_BRAIN_IDLE && !ran[2].forwardmove && logged( "userid=201 mode=follow" ) <= 1 );
	entities[1].v.origin[0] = 0; entities[2].v.origin[0] = 60; entities[3].v.origin[0] = 60; entities[3].v.origin[1] = 10;
	frame( 0.05 ); assert( ran[2].forwardmove != 0 || ran[2].sidemove != 0 );

	/* Door jambs and corners: blocked straight ahead, the bot lines up with the opening the engine
	 * probed beside it instead of pushing into the wall until it teleports. */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = 400; settle();
	add_box( 200, -1000, 216, -24, -1000, 1000 ); add_box( 200, 24, 216, 1000, -1000, 1000 );
	for( i = 0; i < 3; i++ ) { entities[2].v.origin[0] = 180; entities[2].v.origin[1] = 20; frame( 0.05 ); }
	assert( cof_companions[1].out.move_to[1] < 15 && cof_companions[1].out.move_to[0] > 180 );
	/* a pillar's corner, the leader beyond it (seen over its top) */
	setup(); cvars[0].value = 1; entities[1].v.origin[0] = -200; entities[1].v.origin[1] = 128; settle();
	add_box( 100, 134, 140, 1000, -1000, 0 );
	for( i = 0; i < 3; i++ ) { entities[2].v.origin[0] = 150; entities[2].v.origin[1] = 136; frame( 0.05 ); }
	assert( cof_companions[1].out.move_to[1] < 134 && cof_companions[1].out.move_to[0] < 150 );

	/* The story layer off: nothing at all. */
	setup(); cvars[0].value = 1; gate = 0; settle(); assert( clients[1].state == cs_free );

	puts( "companion glue scenarios passed" );
	return 0;
}
