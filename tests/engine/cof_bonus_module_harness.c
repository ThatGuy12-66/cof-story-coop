/* The bonus room module (sv_cof_bonus.c) against engine doubles: a chapter start's kit laid out in a world of solid
 * boxes (the floor, a wall, a bounding-box prop), the map's entities (info_player_start, a level exit, a trigger that
 * fires a cof_teleport), a merged map's sections, scriptsettings.dat as the game would read it; and cof_wardrobe with
 * its Close. No game DLL, networking or game process. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define GAME_EXPORT
#define MAX_CLIENTS 4
#define ENTITIES 48
#define cs_connected 2
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define DEAD_DEAD 2
#define FL_FROZEN ( 1 << 12 )
#define FL_KILLME ( 1 << 30 )
#define FCL_FAKECLIENT ( 1 << 7 )
#define MOVETYPE_WALK 3
#define MOVETYPE_FLY 5
#define MOVE_NORMAL 0
#define MOVE_NOMONSTERS 1
#define CONTENTS_EMPTY -1
#define CONTENTS_WATER -3
#define FBitSet( f, b ) (( f ) & ( b ))
#define VectorCopy( a, b ) (( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2] )
#define VectorSet( v, x, y, z ) (( v )[0] = ( x ), ( v )[1] = ( y ), ( v )[2] = ( z ))
#define ARRAYSIZE( a ) ( sizeof( a ) / sizeof( ( a )[0] ))
#define bound( min, num, max ) (( num ) >= ( min ) ? (( num ) < ( max ) ? ( num ) : ( max )) : ( min ))
#define DEG2RAD( x ) (( double )( x ) * ( 3.14159265358979323846 / 180.0 ))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_strlen strlen
static void Q_strncpy( char *d, const char *s, size_t n ) { snprintf( d, n, "%s", s ); }
/* no libm here: the yaws the scenarios use are exact */
static float harness_cos( double a ) { double x = a, t = 1, s = 1; int i; for( i = 1; i < 30; i++ ) { t *= -x * x / (( 2 * i - 1 ) * ( 2 * i )); s += t; } return (float)s; }
static float harness_sin( double a ) { double x = a, t = x, s = x; int i; for( i = 1; i < 30; i++ ) { t *= -x * x / (( 2 * i ) * ( 2 * i + 1 )); s += t; } return (float)s; }
#define cosf harness_cos
#define sinf harness_sin

typedef struct { const char *name, *string; int flags; float value; } convar_t;
typedef struct edict_s edict_t;
typedef struct
{
	string_t classname, targetname, target;
	vec3_t origin, angles, absmin, absmax;
	int flags, deadflag, movetype;
	float health;
} entvars_t;
struct edict_s { int free, serialnumber; entvars_t v; };
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict, *pViewEntity; } sv_client_t;
typedef struct { float normal[3]; } plane_t;
typedef struct { qboolean allsolid, startsolid; float fraction; vec3_t endpos; plane_t plane; } trace_t;
static struct { int state; double time; char name[64]; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { int numEntities; } svgame;

static edict_t entities[ENTITIES];
static sv_client_t clients[MAX_CLIENTS];
static char strings[64][64];
static int string_count = 1;
static char log_text[32768], argv0[64];
static size_t log_length;
static int h_gate = 1, pickups = 1, merged, h_outfit = 5, h_boat[MAX_CLIENTS], attached[MAX_CLIENTS], camera_holds[MAX_CLIENTS], wardrobe_user = -1;
static convar_t *bonus_cvar, *costume_cvar;
static convar_t cvars[2];
static unsigned char file_text[4096];
static int file_length = -1; /* -1: no scriptsettings.dat in the game folder */
static int files_open; /* files the game folder gave that are not freed yet */

static void Con_Printf( const char *fmt, ... )
{
	va_list args; int n;
	va_start( args, fmt );
	n = vsnprintf( log_text + log_length, sizeof( log_text ) - log_length, fmt, args );
	va_end( args );
	assert( n >= 0 && (size_t)n < sizeof( log_text ) - log_length );
	log_length += (size_t)n;
}
static int logged( const char *needle )
{
	int n = 0; const char *p = log_text;
	while(( p = strstr( p, needle ))) { n++; p++; }
	return n;
}
static convar_t *Cvar_Get( const char *name, const char *value, int flags, const char *desc )
{
	convar_t *cv = !strcmp( name, "cof_bonus_room" ) ? &cvars[0] : &cvars[1];
	(void)flags; (void)desc;
	assert( !strcmp( name, "cof_bonus_room" ) || !strcmp( name, "cof_costumes" ));
	cv->name = name; cv->string = value; cv->value = (float)atof( value );
	return cv;
}
static byte *COM_LoadFileForMe( const char *name, int *length )
{
	byte *data;
	assert( !strcmp( name, "scriptsettings.dat" ));
	if( file_length < 0 ) { *length = 0; return NULL; }
	data = malloc( (size_t)file_length + 1 );
	memcpy( data, file_text, (size_t)file_length ); data[file_length] = 0;
	*length = file_length;
	files_open++;
	return data;
}
static void COM_FreeFile( void *data ) { if( data ) { free( data ); files_open--; } }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return h_gate; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + ENTITIES && !e->free; }
static edict_t *SV_EdictNum( int i ) { return i >= 0 && i < ENTITIES ? &entities[i] : NULL; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static string_t S( const char *s )
{
	int i;
	for( i = 1; i < string_count; i++ ) if( !strcmp( strings[i], s )) return i;
	assert( string_count < 64 );
	strcpy( strings[string_count], s );
	return string_count++;
}
static const char *Cmd_Argv( int i ) { assert( i == 0 ); return argv0; }

/* the world: solid boxes the traces stop at; props (bounding boxes) only a MOVE_NORMAL trace meets; water */
typedef struct { float mins[3], maxs[3]; } box_t;
static box_t solids[8], props[4], waters[4];
static int nsolids, nprops, nwaters, traces, point_contents;
static void box( box_t *list, int *n, float x0, float y0, float z0, float x1, float y1, float z1 )
{
	box_t *b = &list[( *n )++];
	b->mins[0] = x0; b->mins[1] = y0; b->mins[2] = z0; b->maxs[0] = x1; b->maxs[1] = y1; b->maxs[2] = z1;
}
/* a box swept from start to end against one box grown by the mover's size (the slab test) */
static void sweep( const float *start, const float *mins, const float *maxs, const float *end, const box_t *b, trace_t *tr )
{
	float t0 = -1.0f, t1 = 1.0f, lo[3], hi[3];
	int k, axis = -1, in = 1;
	for( k = 0; k < 3; k++ )
	{
		lo[k] = b->mins[k] - maxs[k]; hi[k] = b->maxs[k] - mins[k];
		if( start[k] <= lo[k] || start[k] >= hi[k] ) in = 0;
	}
	if( in ) { tr->startsolid = tr->allsolid = true; tr->fraction = 0.0f; return; }
	for( k = 0; k < 3; k++ )
	{
		float d = end[k] - start[k], u, v;
		if( d == 0.0f ) { if( start[k] <= lo[k] || start[k] >= hi[k] ) return; continue; }
		u = ( lo[k] - start[k] ) / d; v = ( hi[k] - start[k] ) / d;
		if( u > v ) { float w = u; u = v; v = w; }
		if( u > t0 ) { t0 = u; axis = k; }
		if( v < t1 ) t1 = v;
		if( t0 >= t1 ) return;
	}
	if( t0 < 0.0f || t0 >= tr->fraction ) return;
	tr->fraction = t0;
	memset( tr->plane.normal, 0, sizeof( tr->plane.normal ));
	tr->plane.normal[axis] = end[axis] > start[axis] ? -1.0f : 1.0f;
}
static trace_t SV_Move( const float *start, float *mins, float *maxs, const float *end, int type, edict_t *e, qboolean monsterclip )
{
	trace_t tr;
	int i, k;
	(void)e; (void)monsterclip;
	traces++;
	memset( &tr, 0, sizeof( tr ));
	tr.fraction = 1.0f;
	for( i = 0; i < nsolids && !tr.startsolid; i++ ) sweep( start, mins, maxs, end, &solids[i], &tr );
	for( i = 0; type == MOVE_NORMAL && i < nprops && !tr.startsolid; i++ ) sweep( start, mins, maxs, end, &props[i], &tr );
	for( k = 0; k < 3; k++ ) tr.endpos[k] = start[k] + tr.fraction * ( end[k] - start[k] );
	return tr;
}
static int SV_PointContents( const float *p )
{
	int i;
	point_contents++;
	for( i = 0; i < nwaters; i++ )
		if( p[0] > waters[i].mins[0] && p[0] < waters[i].maxs[0] && p[1] > waters[i].mins[1] && p[1] < waters[i].maxs[1] && p[2] > waters[i].mins[2] && p[2] < waters[i].maxs[2] )
			return CONTENTS_WATER;
	return CONTENTS_EMPTY;
}
/* a merged map: section 1 (c_attic, the chapter's) from z 995 up, section 0 (another map) below */
static int SV_CoF_PointSection( const float *origin ) { return merged ? ( origin[2] >= 995.0f ? 1 : 0 ) : -1; }
static int SV_CoF_SectionOfMap( const char *map ) { return merged && !strcmp( map, "c_attic" ) ? 1 : -1; }
static qboolean SV_CoF_PickupsEnabled( void ) { return pickups; }
/* the pickups module's maker: records what it was asked; the game's Spawn of an item over nothing fails */
static struct { int item, copy; float origin[3], angles[3]; } spawned[32];
static int nspawned, spawn_fails;
static edict_t *SV_CoF_PickupsSpawnBonus( int item, const float *origin, const float *angles, qboolean copy )
{
	edict_t *ent;
	if( spawn_fails ) { spawn_fails--; return NULL; }
	assert( nspawned < 32 );
	spawned[nspawned].item = item; spawned[nspawned].copy = copy;
	VectorCopy( origin, spawned[nspawned].origin ); VectorCopy( angles, spawned[nspawned].angles );
	nspawned++;
	ent = &entities[ENTITIES - 1];
	VectorCopy( origin, ent->v.origin );
	return ent;
}
static qboolean SV_CoF_RetailCameraHolds( sv_client_t *cl ) { return camera_holds[cl - clients]; }
static qboolean SV_CoF_RetailAttachedCamera( sv_client_t *cl ) { return attached[cl - clients]; }
static qboolean SV_CoF_RetailInBoat( sv_client_t *cl ) { return h_boat[cl - clients]; }
static int SV_CoF_RetailOutfit( sv_client_t *cl ) { (void)cl; return h_outfit; }
static char profont[256];
static int profonts;
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	(void)cl; (void)colour; (void)reliable;
	snprintf( profont, sizeof( profont ), "%s", text ); profonts++;
	return true;
}
static byte sent[16];
static int sent_size, sends, send_ok = 1;
static edict_t *sent_to;
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	assert( !strcmp( name, "Clothes" ) && reliable && size == 7 );
	if( !send_ok ) return false;
	memcpy( sent, payload, (size_t)size ); sent_size = size; sent_to = to; sends++;
	return true;
}
static qboolean SV_CoF_SceneWardrobeUser( const sv_client_t *cl ) { return wardrobe_user == (int)( cl - clients ); }

#include "bonus_module_under_test.c"

/* a map: the world spawns, the entities the scenarios need */
static edict_t *ent_at( int e, const char *classname, float x, float y, float z )
{
	edict_t *ent = &entities[e];
	memset( &ent->v, 0, sizeof( ent->v ));
	ent->free = 0; ent->serialnumber++;
	ent->v.classname = S( classname );
	VectorSet( ent->v.origin, x, y, z );
	if( e >= svgame.numEntities ) svgame.numEntities = e + 1;
	return ent;
}
static void brush( edict_t *ent, float x0, float y0, float z0, float x1, float y1, float z1 )
{
	VectorSet( ent->v.absmin, x0, y0, z0 ); VectorSet( ent->v.absmax, x1, y1, z1 );
}
static void player( int slot, int userid, int fake )
{
	sv_client_t *cl = &clients[slot];
	cl->state = cs_spawned; cl->userid = userid; cl->flags = fake ? FCL_FAKECLIENT : 0; cl->edict = &entities[slot + 1]; cl->pViewEntity = NULL;
	entities[slot + 1].free = 0; entities[slot + 1].v.classname = S( "player" );
	entities[slot + 1].v.health = 100; entities[slot + 1].v.deadflag = DEAD_NO; entities[slot + 1].v.movetype = MOVETYPE_WALK; entities[slot + 1].v.flags = 0;
}
/* scriptsettings.dat: every line locked, but those asked open */
static void settings( int door, int famas, int costumes )
{
	file_length = (int)CoF_BonusSynthesize( file_text, sizeof( file_text ), 0, costumes > 0 );
	if( door ) CoF_BonusRewriteTable( file_text, (size_t)file_length, &cof_bonus_lines[1], 1, 1 );
	if( famas ) CoF_BonusRewriteTable( file_text, (size_t)file_length, CoF_BonusLine( 39 ), 1, 1 );
}
static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities ));
	for( i = 0; i < ENTITIES; i++ ) entities[i].free = 1;
	entities[0].free = 0;
	memset( clients, 0, sizeof( clients ));
	memset( h_boat, 0, sizeof( h_boat )); memset( attached, 0, sizeof( attached )); memset( camera_holds, 0, sizeof( camera_holds ));
	memset( spawned, 0, sizeof( spawned )); nspawned = spawn_fails = 0;
	log_text[0] = 0; log_length = 0; profont[0] = 0; profonts = sends = 0; send_ok = 1; sent_to = NULL;
	files_open = 0; /* a file the switch makes (none in the game folder) is freed too: counted from here */
	nsolids = nprops = nwaters = 0;
	svs.maxclients = 4; svs.clients = clients; svs.spawncount++;
	svgame.numEntities = svs.maxclients + 1;
	sv.state = ss_active; sv.time = 10.0; strcpy( sv.name, "cm_basement" );
	h_gate = 1; pickups = 1; merged = 0; h_outfit = 5; wardrobe_user = -1;
	SV_CoF_BonusInit();
	bonus_cvar = &cvars[0]; costume_cvar = &cvars[1];
	bonus_cvar->value = 1.0f; costume_cvar->value = 1.0f; /* the app writes cof_bonus_room 1 with the room ticked */
	settings( 0, 0, 0 );
	/* the room the party starts in: a floor, a wall far ahead, a crate (a prop) to the left */
	box( solids, &nsolids, -1000, -1000, -16, 1000, 1000, 0 );
	box( solids, &nsolids, 400, -1000, 0, 416, 1000, 300 );
	box( props, &nprops, -20, 50, 0, 40, 110, 40 );
	ent_at( 10, "info_player_start", 0, 0, 37 )->v.angles[1] = 0.0f;
	/* a level exit just behind the start; a trigger that fires a cof_teleport (a bounce) to the right */
	brush( ent_at( 11, "trigger_changelevel", -130, 0, 50 ), -200, -100, 0, -60, 100, 120 );
	ent_at( 12, "cof_teleport", 0, 0, 0 )->v.targetname = S( "bounce" );
	brush( ent_at( 13, "trigger_multiple", 0, -120, 50 ), -100, -200, 0, 300, -70, 120 );
	entities[13].v.target = S( "bounce" );
	/* a harmless trigger over the start (music): no reason to keep away */
	brush( ent_at( 14, "trigger_once", 0, 0, 0 ), -300, -300, 0, 300, 300, 200 );
	player( 0, 11, 0 );
}

static void kit_laid_out_once_and_safely( void )
{
	int i;
	setup();
	SV_CoF_BonusKitChapterStart( "c_basement" );
	/* one of each, in the kit's order, as the map's own (no copy mark), facing as in the room */
	assert( nspawned == COF_BONUS_KIT_ITEMS && files_open == 0 );
	for( i = 0; i < nspawned; i++ )
	{
		float feet[3];
		assert( spawned[i].item == i && !spawned[i].copy && spawned[i].angles[1] == cof_bonus_kit[i].yaw );
		/* on the floor (lifted to fall onto it), out of the exit and the bounce (and the walk up to them), off the crate,
		 * this side of the wall, spread out */
		assert( spawned[i].origin[2] == COF_BONUS_KIT_LIFT );
		feet[0] = spawned[i].origin[0]; feet[1] = spawned[i].origin[1]; feet[2] = 1.0f;
		assert( !CoF_BonusKitTouches( feet, entities[11].v.absmin, entities[11].v.absmax ));
		assert( !CoF_BonusKitTouches( feet, entities[13].v.absmin, entities[13].v.absmax ));
		assert( !( feet[0] + 16 > -20 && feet[0] - 16 < 40 && feet[1] + 16 > 50 && feet[1] - 16 < 110 ));
		assert( feet[0] < 400 - 16 );
		if( i ) assert(( spawned[i].origin[0] - spawned[0].origin[0] ) * ( spawned[i].origin[0] - spawned[0].origin[0] ) +
			( spawned[i].origin[1] - spawned[0].origin[1] ) * ( spawned[i].origin[1] - spawned[0].origin[1] ) >= COF_BONUS_KIT_SPREAD * COF_BONUS_KIT_SPREAD );
	}
	assert( spawned[0].origin[0] == 64.0f && spawned[0].origin[1] == 0.0f ); /* straight ahead of the start */
	assert( logged( "cofstory phase=bonus-kit map=cm_basement chapter=c_basement result=placed sets=1 placed=6 of=6 close=0 locked=0 section=-1 start=0.0,0.0,37.0" ) == 1 );
	assert( logged( "phase=bonus-kit-item class=" ) == 6 && logged( "phase=bonus-kit-item class=weapon_famas entity=47 origin=64.0,0.0,8.0\n" ) == 1 );

	/* the per-player pickups off: a set for each place, four at most */
	setup(); pickups = 0;
	SV_CoF_BonusKitChapterStart( "c_ending5" );
	assert( nspawned == 24 && logged( "result=placed sets=4 placed=24 of=24" ) == 1 );
	for( i = 0; i < 24; i++ ) assert( spawned[i].item == i / 4 && !spawned[i].copy );
	setup(); pickups = 0; svs.maxclients = 2;
	SV_CoF_BonusKitChapterStart( "c_ending5" );
	assert( nspawned == 12 && logged( "sets=2 placed=12" ) == 1 );

	/* an item the game will not make (an item with no floor removes itself) is told, the rest lie */
	setup(); spawn_fails = 1;
	SV_CoF_BonusKitChapterStart( "c_sewer3" );
	assert( nspawned == 5 && logged( "phase=bonus-kit-item class=weapon_famas result=failed" ) == 1 && logged( "placed=5 of=6" ) == 1 );
}

static void kit_decided_by_the_room( void )
{
	/* cof_bonus_room 0: the room shut for this game, nothing */
	setup(); bonus_cvar->value = 0.0f; settings( 1, 1, 1 );
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( !nspawned && logged( "cofstory phase=bonus-kit map=cm_basement chapter=c_basement result=shut" ) == 1 );
	/* -1: the file decides. No ending: shut. One ending, only the FAMAS unlocked: the door opens; the FAMAS lies, the
	 * MP5 too (the game's own rule removes it), the locked ones are not laid out at all */
	setup(); bonus_cvar->value = -1.0f; settings( 0, 1, 1 );
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( !nspawned && logged( "result=shut" ) == 1 );
	setup(); bonus_cvar->value = -1.0f; settings( 1, 1, 1 );
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( nspawned == 2 && spawned[0].item == 0 && spawned[1].item == 5 && files_open == 0 );
	assert( logged( "result=locked" ) == 4 && logged( "phase=bonus-kit-item class=item_nightvision result=locked" ) == 1 );
	assert( logged( "placed=2 of=2 close=0 locked=4" ) == 1 );
	/* -1 and no file: shut */
	setup(); bonus_cvar->value = -1.0f; file_length = -1;
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( !nspawned && logged( "result=shut" ) == 1 );
	/* 1 and no file: every item open (the switch makes the game a file with the asked lines open) */
	setup(); file_length = -1;
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( nspawned == 6 );
	/* the two maps with a room of their own and the prologue before c_start's: nothing */
	setup(); SV_CoF_BonusKitChapterStart( "c_bridge" ); SV_CoF_BonusKitChapterStart( "c_start" ); SV_CoF_BonusKitChapterStart( "c_nightmare" );
	assert( !nspawned && logged( "result=own-room" ) == 3 );
	/* not story co-op, or no chapter: nothing, not a line */
	setup(); h_gate = 0; SV_CoF_BonusKitChapterStart( "c_basement" ); h_gate = 1; SV_CoF_BonusKitChapterStart( "" );
	assert( !nspawned && !log_length );
	/* no info_player_start */
	setup(); entities[10].free = 1;
	SV_CoF_BonusKitChapterStart( "c_basement" );
	assert( !nspawned && logged( "result=no-start" ) == 1 );
}

static void kit_in_a_merged_maps_section( void )
{
	int i;
	/* cm_asylum1day: c_asylum1day below (section 0), c_attic above (section 1); the first info_player_start is the
	 * other map's, the chapter's start is the second */
	setup(); merged = 1; strcpy( sv.name, "cm_asylum1day" ); pickups = 0; /* a set each: the outer rings too */
	nsolids = 0;
	box( solids, &nsolids, -1000, -1000, -16, 1000, 1000, 0 );        /* c_asylum1day's floor */
	box( solids, &nsolids, 1000, -1000, 984, 2000, 1000, 1000 );      /* c_attic's floor */
	box( solids, &nsolids, 2000, -1000, 964, 3000, 1000, 980 );       /* a step down, in the other section's band */
	entities[10].v.origin[0] = 0; entities[10].v.origin[1] = 0; entities[10].v.origin[2] = 37;
	ent_at( 15, "info_player_start", 1900, 0, 1037 )->v.angles[1] = 90.0f;
	SV_CoF_BonusKitChapterStart( "c_attic" );
	assert( nspawned == 24 );
	for( i = 0; i < nspawned; i++ )
	{
		assert( spawned[i].origin[2] == 1000.0f + COF_BONUS_KIT_LIFT && SV_CoF_PointSection( spawned[i].origin ) == 1 );
		assert( spawned[i].origin[0] > 1000.0f && spawned[i].origin[0] < 2000.0f );
	}
	/* facing +y: the first straight ahead that way */
	assert( spawned[0].origin[1] == 64.0f && spawned[0].origin[0] > 1899.9f && spawned[0].origin[0] < 1900.1f );
	assert( logged( "chapter=c_attic result=placed sets=4 placed=24 of=24 close=0 locked=0 section=1 start=1900.0,0.0,1037.0" ) == 1 );
	/* not merged (the same map unmerged): the first info_player_start, which is the map's own */
	setup(); ent_at( 15, "info_player_start", 1900, 0, 1037 );
	SV_CoF_BonusKitChapterStart( "c_attic" );
	assert( nspawned == 6 && spawned[0].origin[0] == 64.0f && logged( "section=-1 start=0.0,0.0,37.0" ) == 1 );
}

static void wardrobe( sv_client_t *cl, const char *cmd, qboolean handled )
{
	snprintf( argv0, sizeof( argv0 ), "%s", cmd );
	assert( SV_CoF_BonusClientCommand( cl ) == handled );
}

static void the_wardrobe_key( void )
{
	sv_client_t *host = &clients[0], *guest = &clients[1], *bot = &clients[2];
	setup();
	player( 1, 12, 0 ); player( 2, 13, 1 );
	/* the menu for whoever pressed it: every outfit open (cof_costumes 1), the one they wear, shown */
	wardrobe( guest, "cof_wardrobe", true );
	assert( sends == 1 && sent_to == guest->edict && sent_size == 7 );
	assert( sent[0] == 0xFF && sent[1] == 0x0F && sent[2] == 0 && sent[3] == 0 && sent[4] == 1 && sent[5] == 5 && sent[6] == 1 );
	assert( logged( "cofstory phase=wardrobe userid=12 outfits=0xfff outfit=5" ) == 1 && files_open == 0 );
	/* a held or mashed key: one answer a second, nothing in between, not even a line */
	wardrobe( guest, "cof_wardrobe", true );
	assert( sends == 1 && logged( "phase=wardrobe" ) == 1 );
	sv.time += 1.0;
	wardrobe( guest, "cof_wardrobe", true );
	assert( sends == 2 );
	/* a new map (its time starts again): never too soon */
	sv.time = 1.0;
	wardrobe( guest, "cof_wardrobe", true );
	assert( sends == 3 );
	/* its Close goes no further, once; a later Close (the real wardrobe's) goes to the game */
	wardrobe( guest, "closeclothes", true );
	assert( logged( "cofstory phase=wardrobe-closed userid=12" ) == 1 );
	wardrobe( guest, "closeclothes", false );
	wardrobe( host, "closeclothes", false ); /* never opened by the key */
	/* opened by the key, then the real wardrobe used: that Close is the real one's */
	sv.time += 2.0;
	wardrobe( host, "cof_wardrobe", true );
	wardrobe_user = 0;
	wardrobe( host, "closeclothes", false );
	wardrobe( host, "closeclothes", false );
	wardrobe_user = -1;
	/* setclothes and the rest are the game's */
	wardrobe( host, "setclothes", false );
	wardrobe( host, "cof_warp", false );

	/* outfits as the file says (cof_costumes -1, nothing unlocked): none, canseeclothes 0 */
	sv.time += 2.0; costume_cvar->value = -1.0f; h_outfit = 0;
	wardrobe( host, "cof_wardrobe", true );
	assert( sends == 5 && sent[0] == 0 && sent[1] == 0 && sent[4] == 0 && sent[5] == 0 && sent[6] == 1 );
	costume_cvar->value = 1.0f;
	wardrobe( host, "closeclothes", true );

	/* refused: dead (the respawn prompt speaks: no line on screen), held by a camera or a scene, a model cutscene or a
	 * ladder climb, a plain ladder, the boat, the bonus room shut */
	profonts = 0;
	sv.time += 2.0; host->edict->v.health = 0; wardrobe( host, "cof_wardrobe", true );
	assert( logged( "phase=wardrobe-refused userid=11 reason=dead" ) == 1 && !profonts );
	host->edict->v.health = 100; host->edict->v.deadflag = DEAD_DEAD;
	sv.time += 2.0; wardrobe( host, "cof_wardrobe", true ); assert( logged( "reason=dead" ) == 2 );
	host->edict->v.deadflag = DEAD_NO;
	sv.time += 2.0; host->pViewEntity = &entities[30]; wardrobe( host, "cof_wardrobe", true ); host->pViewEntity = NULL;
	sv.time += 2.0; camera_holds[0] = 1; wardrobe( host, "cof_wardrobe", true ); camera_holds[0] = 0;
	sv.time += 2.0; attached[0] = 1; wardrobe( host, "cof_wardrobe", true ); attached[0] = 0;
	sv.time += 2.0; host->edict->v.flags = FL_FROZEN; wardrobe( host, "cof_wardrobe", true ); host->edict->v.flags = 0;
	assert( logged( "reason=scene" ) == 4 && profonts == 4 && !strcmp( profont, "The wardrobe cannot be opened right now." ));
	sv.time += 2.0; host->edict->v.movetype = MOVETYPE_FLY; wardrobe( host, "cof_wardrobe", true ); host->edict->v.movetype = MOVETYPE_WALK;
	sv.time += 2.0; h_boat[0] = 1; wardrobe( host, "cof_wardrobe", true ); h_boat[0] = 0;
	assert( logged( "reason=ladder" ) == 1 && logged( "reason=boat" ) == 1 );
	sv.time += 2.0; bonus_cvar->value = 0.0f; wardrobe( host, "cof_wardrobe", true ); bonus_cvar->value = 1.0f;
	assert( logged( "reason=shut" ) == 1 && strstr( profont, "bonus room is shut" ));
	assert( sends == 5 );
	/* a refused press still waits its second */
	wardrobe( host, "cof_wardrobe", true );
	assert( sends == 5 && logged( "phase=wardrobe-refused" ) == 9 );
	/* the message not sent (the game has no Clothes message): told, and its Close stays the game's */
	sv.time += 2.0; send_ok = 0; wardrobe( host, "cof_wardrobe", true ); send_ok = 1;
	assert( logged( "reason=no-message" ) == 1 );
	wardrobe( host, "closeclothes", false );
	/* an AI companion, a player not spawned yet: nothing */
	sv.time += 2.0; wardrobe( bot, "cof_wardrobe", true );
	guest->state = cs_connected; wardrobe( guest, "cof_wardrobe", true ); guest->state = cs_spawned;
	assert( sends == 5 );
	/* another player in the slot (a new userid): a fresh record, so the old one's open menu is not theirs */
	sv.time += 2.0; wardrobe( guest, "cof_wardrobe", true ); assert( sends == 6 );
	guest->userid = 99;
	wardrobe( guest, "closeclothes", false );
	/* not story co-op, or no map running: the commands are not ours */
	h_gate = 0; wardrobe( host, "cof_wardrobe", false ); h_gate = 1;
	sv.state = 1; wardrobe( host, "cof_wardrobe", false ); sv.state = ss_active;
}

/* 1.2.10: an item's own unlock line, as the kit reads it (the room's own night vision is per-player only then) */
static void item_unlock_lines( void )
{
	setup(); bonus_cvar->value = -1.0f; settings( 1, 1, 1 );
	assert( SV_CoF_BonusItemUnlocked( 0 ) && !SV_CoF_BonusItemUnlocked( 4 ) && SV_CoF_BonusItemUnlocked( 5 ) && files_open == 0 );
	CoF_BonusRewriteTable( file_text, (size_t)file_length, CoF_BonusLine( 75 ), 1, 1 );
	assert( SV_CoF_BonusItemUnlocked( 4 ) && files_open == 0 );
	/* cof_bonus_room 1 opens every line; 0 shuts them; the MP5 has no line (the game's other rule) */
	setup(); settings( 0, 0, 0 );
	assert( SV_CoF_BonusItemUnlocked( 4 ));
	bonus_cvar->value = 0.0f; settings( 1, 1, 1 );
	CoF_BonusRewriteTable( file_text, (size_t)file_length, CoF_BonusLine( 75 ), 1, 1 );
	assert( !SV_CoF_BonusItemUnlocked( 4 ) && SV_CoF_BonusItemUnlocked( 5 ));
	/* no file: as the switch makes it (1: open; -1: none, locked) */
	setup(); file_length = -1;
	assert( SV_CoF_BonusItemUnlocked( 4 ) && files_open == -1 ); /* the file the switch made, freed */
	bonus_cvar->value = -1.0f;
	assert( !SV_CoF_BonusItemUnlocked( 4 ));
	/* no such item, or not story co-op */
	setup();
	assert( !SV_CoF_BonusItemUnlocked( -1 ) && !SV_CoF_BonusItemUnlocked( COF_BONUS_KIT_ITEMS ));
	h_gate = 0;
	assert( !SV_CoF_BonusItemUnlocked( 4 ));
}

int main( void )
{
	kit_laid_out_once_and_safely();
	kit_decided_by_the_room();
	kit_in_a_merged_maps_section();
	the_wardrobe_key();
	item_unlock_lines();
	assert( traces > 0 && point_contents > 0 );
	puts( "bonus module scenarios passed" );
	return 0;
}
