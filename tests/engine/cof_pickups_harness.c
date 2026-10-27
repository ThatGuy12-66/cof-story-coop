/* Per-player map pickups (sv_cof_pickups.c) against engine doubles: a map's entity text, a world of edicts, players
 * and an AI companion, the game's KeyValue and Spawn. No game DLL, networking or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define MAX_EDICTS 64
#define ENTITIES MAX_EDICTS
#define cs_spawned 4
#define ss_active 2
#define MOVETYPE_TOSS 6
#define MOVETYPE_FOLLOW 12
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define Q_strcmp strcmp
#define Q_strncpy( d, s, n ) ( strncpy( d, s, n ), ( d )[( n ) - 1] = 0 )
#define SetBits(f,b) ((f)|=(b))
#define FBitSet(f,b) ((f)&(b))
#define VectorCopy(a,b) ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
typedef struct { const char *name, *string; float value; } convar_t;
#define CVAR_DEFINE_AUTO( cv, cvstr, cvflags, cvdesc ) convar_t cv = { #cv, cvstr, 0.0f }
typedef struct edict_s edict_t;
typedef struct {
	string_t classname;
	vec3_t origin, angles;
	int flags, spawnflags, movetype;
	float nextthink, frags;
	edict_t *owner, *aiment;
} entvars_t;
struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; };
typedef struct { char *szClassName, *szKeyName, *szValue; int fHandled; } KeyValueData;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef struct { char *entities; } model_t;
static struct { int state; double time; char name[64]; model_t *worldmodel; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { int max_edicts; } gameinfo = { 4096 };
#define GI ( &gameinfo )
static struct { edict_t *edicts; int numEntities; struct { void (*pfnKeyValue)( edict_t *, KeyValueData * ); int (*pfnSpawn)( edict_t * ); } dllFuncs; } svgame;

static edict_t entities[ENTITIES];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[ENTITIES][16];
static char strings[64][64];
static int string_count = 1;
static char log_text[16384];
static size_t log_length;
static int gate = 1, spawn_fails;
static char keys_seen[ENTITIES][256];
static model_t world;
static char lump[2048];

static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
/* 1.3.5: a game of Cry of Fear's own co-op (the story off) */
static int official;
static qboolean SV_CoF_StoryOfficialCoop( void ) { return official; }
/* 1.3: the night vision's unlock line (sv_cof_bonus.c), asked for the room's own */
static int nv_unlocked = 1, nv_asked;
static qboolean SV_CoF_BonusItemUnlocked( int item ) { nv_asked++; assert( item == 4 ); return nv_unlocked; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + ENTITIES && !e->free; }
static edict_t *SV_EdictNum( int i ) { return i >= 0 && i < ENTITIES ? &entities[i] : NULL; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static string_t SV_AllocString( const char *s )
{
	int i;
	for( i = 1; i < string_count; i++ ) if( !strcmp( strings[i], s )) return i;
	assert( string_count < 64 );
	strcpy( strings[string_count], s );
	return string_count++;
}
static void Cvar_RegisterVariable( convar_t *cv ) { cv->value = (float)atof( cv->string ); }
static void SV_FreeEdict( edict_t *e ) { memset( &e->v, 0, sizeof( e->v )); e->free = 1; e->serialnumber++; }
static void SV_LinkEdict( edict_t *e, qboolean touch ) { (void)e; (void)touch; }
static edict_t *SV_CreateNamedEntity( edict_t *e, string_t classname )
{
	int i;
	assert( e == NULL );
	for( i = svs.maxclients + 1; i < ENTITIES && !entities[i].free; i++ );
	if( i == ENTITIES ) return NULL;
	memset( &entities[i].v, 0, sizeof( entities[i].v ));
	entities[i].free = 0; entities[i].serialnumber++; entities[i].pvPrivateData = private_data[i];
	entities[i].v.classname = classname;
	keys_seen[i][0] = 0;
	if( i >= svgame.numEntities ) svgame.numEntities = i + 1;
	return &entities[i];
}
static sv_client_t *SV_ClientFromEdict( const edict_t *e, qboolean spawned_only )
{
	int i;
	if( !SV_IsValidEdict( e )) return NULL;
	i = NUM_FOR_EDICT( e ) - 1;
	if( i < 0 || i >= svs.maxclients ) return NULL;
	if( spawned_only && svs.clients[i].state != cs_spawned ) return NULL;
	return &svs.clients[i];
}
/* the engine's tokenizer, as far as entity text needs it: quoted strings, braces */
static char *COM_ParseFile( char *data, char *token, int size )
{
	int n = 0;
	token[0] = 0;
	if( !data ) return NULL;
	while( *data == ' ' || *data == '\n' || *data == '\r' || *data == '\t' ) data++;
	if( !*data ) return NULL;
	if( *data == '{' || *data == '}' ) { token[0] = *data; token[1] = 0; return data + 1; }
	if( *data == '"' )
	{
		for( data++; *data && *data != '"'; data++ ) if( n + 1 < size ) token[n++] = *data;
		token[n] = 0;
		return *data ? data + 1 : data;
	}
	for( ; *data && *data != ' ' && *data != '\n'; data++ ) if( n + 1 < size ) token[n++] = *data;
	token[n] = 0;
	return data;
}
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

/* the game: KeyValue keeps what it is told; Spawn makes a weapon, ammo or pills lie still (landed) */
static void game_keyvalue( edict_t *e, KeyValueData *kvd )
{
	char *seen = keys_seen[NUM_FOR_EDICT( e )];
	assert( !strcmp( kvd->szClassName, SV_GetString( e->v.classname )));
	strcat( seen, kvd->szKeyName ); strcat( seen, "=" ); strcat( seen, kvd->szValue ); strcat( seen, ";" );
	if( !strcmp( kvd->szKeyName, "frags" )) e->v.frags = (float)atof( kvd->szValue );
	if( !strcmp( kvd->szKeyName, "spawnflags" )) e->v.spawnflags = atoi( kvd->szValue );
	kvd->fHandled = true;
}
static int game_spawn( edict_t *e )
{
	if( spawn_fails ) { spawn_fails--; return -1; }
	e->v.movetype = MOVETYPE_TOSS;
	return 0;
}

#include "pickups_under_test.c"

/* the map: a worldspawn, an ammo box, a syringe, a shotgun whose take fires a script, a key, pills, the bonus axe,
 * a FAMAS (locked: it removes itself) */
static const char map_text[] =
	"{\n\"classname\" \"worldspawn\"\n\"wad\" \"x.wad\"\n}\n"
	"{\n\"origin\" \"100 0 40\"\n\"classname\" \"ammo_glock\"\n\"frags\" \"-1\"\n\"renderfx\" \"99\"\n}\n"
	"{\n\"origin\" \"0 0 64\"\n\"angles\" \"0 90 0\"\n\"classname\" \"weapon_syringe\"\n\"frags\" \"2\"\n}\n"
	"{\n\"origin\" \"50 50 0\"\n\"classname\" \"weapon_shotgun\"\n\"target\" \"gift\"\n}\n"
	"{\n\"origin\" \"60 60 0\"\n\"classname\" \"item_key\"\n\"message\" \"inventoryitems/key.txt\"\n}\n"
	"{\n\"origin\" \"200 0 2\"\n\"classname\" \"aom_pills\"\n}\n"
	"{\n\"origin\" \"300 0 10\"\n\"classname\" \"weapon_axe\"\n\"spawnflags\" \"1\"\n}\n"
	"{\n\"origin\" \"400 0 10\"\n\"classname\" \"weapon_famas\"\n}\n";
enum { AMMO = MAX_CLIENTS + 1, SYRINGE, SHOTGUN, KEY, PILLS, AXE, FAMAS, DROPPED };
static const char *map_classes[] = { "ammo_glock", "weapon_syringe", "weapon_shotgun", "item_key", "aom_pills", "weapon_axe", "weapon_famas" };
static const float map_landed[][3] = { { 100, 0, 36 }, { 0, 0, 0 }, { 50, 50, 0 }, { 60, 60, 0 }, { 200, 0, 0 }, { 300, 0, 0 }, { 400, 0, 0 } };

static void player( int slot, const char *name, int userid, int fake )
{
	clients[slot].state = cs_spawned; clients[slot].userid = userid; clients[slot].flags = fake ? FCL_FAKECLIENT : 0;
	strcpy( clients[slot].name, name ); clients[slot].edict = &entities[slot + 1];
	entities[slot + 1].free = 0; entities[slot + 1].v.classname = SV_AllocString( "player" );
}
static edict_t *place( int e, const char *classname, const float *at )
{
	memset( &entities[e].v, 0, sizeof( entities[e].v ));
	entities[e].free = 0; entities[e].serialnumber++; entities[e].pvPrivateData = private_data[e];
	entities[e].v.classname = SV_AllocString( classname );
	VectorCopy( at, entities[e].v.origin );
	entities[e].v.movetype = MOVETYPE_TOSS;
	if( e >= svgame.numEntities ) svgame.numEntities = e + 1;
	return &entities[e];
}
/* a map load: a fresh world as the map spawns it (a revisit or a checkpoint then changes what its snapshot holds) */
static void load_map( void )
{
	static const float there[3] = { 900, 0, 0 };
	int i;
	memset( entities, 0, sizeof( entities ));
	for( i = 0; i < ENTITIES; i++ ) { entities[i].free = 1; entities[i].serialnumber = 100 * svs.spawncount; }
	entities[0].free = 0;
	svgame.numEntities = svs.maxclients + 1;
	svs.spawncount++;
	for( i = 0; i < svs.maxclients; i++ )
		if( clients[i].state ) { entities[i + 1].free = 0; entities[i + 1].v.classname = SV_AllocString( "player" ); }
	for( i = 0; i < 7; i++ ) place( AMMO + i, map_classes[i], map_landed[i] );
	place( DROPPED, "weapon_syringe", there ); /* a gun a player dropped, away from every block */
}
static void setup( void )
{
	memset( clients, 0, sizeof( clients ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	memset( keys_seen, 0, sizeof( keys_seen ));
	strcpy( lump, map_text ); world.entities = lump;
	strcpy( sv.name, "c_test" ); sv.worldmodel = &world; sv.state = ss_active; sv.time = 100.0;
	svs.maxclients = 4; svs.clients = clients; svs.spawncount = 1;
	svgame.edicts = entities; svgame.dllFuncs.pfnKeyValue = game_keyvalue; svgame.dllFuncs.pfnSpawn = game_spawn;
	gate = 1; official = 0; spawn_fails = 0; nv_unlocked = 1; nv_asked = 0;
	player( 0, "Host", 11, 0 ); player( 1, "Guest", 12, 0 ); player( 2, "Companion", 13, 1 );
	SV_CoF_PickupsInit();
	load_map();
}
static edict_t *host( void ) { return &entities[1]; }
static edict_t *guest( void ) { return &entities[2]; }
static edict_t *bot( void ) { return &entities[3]; }
static int sees( edict_t *viewer, int e ) { return SV_CoF_PickupsVisible( viewer, &entities[e] ); }
static int refused( edict_t *user, int e ) { return SV_CoF_PickupsBlocked( user, &entities[e] ); }
static void frame( void ) { sv.time += 0.05; SV_CoF_PickupsFrame(); }
/* the copy of a block made last: the highest edict of that class carrying the copy mark */
static int copy_of( const char *classname )
{
	int e, found = 0;
	for( e = svs.maxclients + 1; e < svgame.numEntities; e++ )
		if( !entities[e].free && !( entities[e].v.flags & FL_KILLME ) && !strcmp( SV_GetString( entities[e].v.classname ), classname ) && ( entities[e].v.spawnflags & COF_PICKUP_COPY_FLAG ))
			found = e;
	return found;
}
/* what the game does when a player takes one: ammo (or a gun they had) removed a tenth of a second later; a new gun
 * attached; pills removed at once */
static void taken_as_ammo( int e ) { entities[e].v.nextthink = (float)sv.time + 0.1f; }
static void taken_as_gun( int e, edict_t *by ) { entities[e].v.owner = by; entities[e].v.aiment = by; entities[e].v.movetype = MOVETYPE_FOLLOW; entities[e].v.nextthink = (float)sv.time + 0.1f; }
static void removed( int e ) { entities[e].v.flags |= FL_KILLME; }
static void command( int slot ) { SV_CoF_PickupsCommand( &clients[slot] ); }

static void a_fresh_map( void )
{
	int copy, again;
	setup();
	frame();
	/* found: the ammo, the syringe, the pills, the axe and the FAMAS; not the shotgun (its take fires a script), the
	 * key (a story item) or the dropped gun */
	assert( logged( "phase=pickups map=c_test found=5 lying=5 copies=0 waiting=0 none=0 skipped=0" ) == 1 );
	assert( sees( host(), AMMO ) && sees( guest(), AMMO ));
	assert( !refused( host(), AMMO ) && !refused( guest(), AMMO ) && refused( bot(), AMMO ));
	assert( !refused( bot(), SHOTGUN ) && !refused( bot(), DROPPED )); /* not per-player: the retail rules */
	frame();
	assert( logged( "phase=pickups map" ) == 1 ); /* once a map */

	/* the host takes the ammo box with +use: noted after their command, settled by the frame, a copy for the guest */
	command( 1 ); /* the guest's command changed nothing */
	taken_as_ammo( AMMO ); /* the host's did */
	command( 0 );
	frame();
	assert( logged( "phase=pickup-taken userid=11 class=ammo_glock block=1 companion=0 takers=1" ) == 1 );
	copy = copy_of( "ammo_glock" );
	assert( copy && copy != AMMO );
	assert( logged( "phase=pickup-copy class=ammo_glock block=1" ) == 1 );
	/* the same pickup again: the block's own keys, not its place (it lies where the taken one lay), with the mark */
	assert( !strcmp( keys_seen[copy], "frags=-1;renderfx=99;" ));
	assert( entities[copy].v.origin[0] == 100.0f && entities[copy].v.origin[2] == 36.0f && entities[copy].v.frags == -1.0f );
	assert( entities[copy].v.spawnflags == COF_PICKUP_COPY_FLAG );
	/* the host neither sees nor finds it; the guest does; the AI companion finds no map pickup */
	assert( !sees( host(), copy ) && sees( guest(), copy ));
	assert( refused( host(), copy ) && !refused( guest(), copy ) && refused( bot(), copy ));
	/* the taken box is the game's again (gone a tenth of a second later) */
	assert( sees( host(), AMMO ) && !refused( host(), AMMO ));
	SV_FreeEdict( &entities[AMMO] );
	frame();

	/* the guest takes the copy: everyone has one, no further copy */
	taken_as_ammo( copy );
	command( 1 );
	frame();
	assert( logged( "phase=pickup-taken userid=12 class=ammo_glock block=1 companion=0 takers=2" ) == 1 );
	assert( logged( "phase=pickup-copy class=ammo_glock" ) == 1 );
	SV_FreeEdict( &entities[copy] );
	frame();

	/* a player joins: theirs is made, hidden from the two who took one */
	player( 3, "Late", 14, 0 );
	frame();
	assert( logged( "phase=pickup-copy class=ammo_glock" ) == 2 );
	copy = copy_of( "ammo_glock" );
	assert( !sees( host(), copy ) && !sees( guest(), copy ) && sees( &entities[4], copy ) && !refused( &entities[4], copy ));
	clients[3].state = 0; /* and leaves again: the copy waits, hidden from those here */

	/* a new gun: attached to the host in their command; the guest's copy keeps the block's keys and angles */
	taken_as_gun( SYRINGE, host());
	command( 0 );
	frame();
	again = copy_of( "weapon_syringe" );
	assert( again && again != SYRINGE && again != DROPPED );
	assert( !strcmp( keys_seen[again], "frags=2;" ) && !sees( host(), again ) && sees( guest(), again ));

	/* a gun found on a player although no command of theirs was seen taking it: theirs all the same */
	taken_as_gun( AXE, guest());
	frame();
	assert( logged( "phase=pickup-taken userid=12 class=weapon_axe block=6 companion=0 takers=1" ) == 1 );
	again = copy_of( "weapon_axe" );
	assert( again && sees( host(), again ) && !sees( guest(), again ));
	assert( !strcmp( keys_seen[again], "spawnflags=1;" ) && entities[again].v.spawnflags == ( 1 | COF_PICKUP_COPY_FLAG ));

	/* the locked FAMAS removes itself in the host's first update (their command): no take, no copy, ever */
	removed( FAMAS );
	command( 0 );
	frame();
	assert( logged( "phase=pickup-gone class=weapon_famas block=7 reason=removed" ) == 1 );
	assert( !copy_of( "weapon_famas" ));
	frame();
	assert( logged( "phase=pickup-gone" ) == 1 );

	/* pills heal the guest and remove themselves at once */
	removed( PILLS );
	command( 1 );
	frame();
	again = copy_of( "aom_pills" );
	assert( again && sees( host(), again ) && !sees( guest(), again ));

	/* the AI companion takes the host's copy anyway (a filter missed): the humans keep theirs, a new copy */
	removed( again );
	command( 2 );
	frame();
	assert( logged( "phase=pickup-taken userid=13 class=aom_pills block=5 companion=1 takers=1" ) == 1 );
	assert( copy_of( "aom_pills" ) && copy_of( "aom_pills" ) != again );
	SV_FreeEdict( &entities[again] );

	/* the guest takes a copy hidden from them (a filter missed): no copy after copy, the pickup is given up */
	again = copy_of( "aom_pills" );
	removed( again );
	command( 1 );
	frame();
	assert( logged( "class=aom_pills block=5 copies=no reason=taken-again" ) == 1 );
	SV_FreeEdict( &entities[again] );
	frame();
	assert( !copy_of( "aom_pills" ));
}

static void a_revisit_and_checkpoints( void )
{
	int ammo_copy, syringe_copy, e;
	setup();
	frame();
	/* the host takes the ammo box and the syringe; the guest's copies lie there when the party leaves */
	taken_as_ammo( AMMO ); taken_as_gun( SYRINGE, host());
	command( 0 );
	frame();
	ammo_copy = copy_of( "ammo_glock" ); syringe_copy = copy_of( "weapon_syringe" );
	assert( ammo_copy && syringe_copy );
	SV_CoF_PickupsCheckpointSaved( "autosave" );
	assert( logged( "phase=pickups-checkpoint saved=autosave entries=2" ) == 1 );

	/* the revisit: the map's snapshot holds the two copies where the originals lay (the originals are in the host's
	 * hands or gone) and the rest of the map */
	load_map();
	SV_FreeEdict( &entities[AMMO] ); SV_FreeEdict( &entities[SYRINGE] );
	place( 20, "ammo_glock", map_landed[0] )->v.spawnflags = COF_PICKUP_COPY_FLAG;
	place( 21, "weapon_syringe", map_landed[1] )->v.spawnflags = COF_PICKUP_COPY_FLAG;
	frame();
	assert( logged( "phase=pickups map=c_test found=5 lying=5 copies=2 waiting=0" ) == 1 );
	/* still hidden from the host, the guest's to take */
	assert( !sees( host(), 20 ) && sees( guest(), 20 ) && refused( host(), 21 ) && !refused( guest(), 21 ));

	/* the guest takes the ammo copy; then the team wipes and the checkpoint loads: that take is undone with the
	 * guest's inventory, so the copy in the checkpoint's world is still the guest's to take, never the host's */
	taken_as_ammo( 20 );
	command( 1 );
	frame();
	assert( logged( "class=ammo_glock block=1 companion=0 takers=2" ) == 1 );
	SV_CoF_PickupsCheckpointLoaded( "autosave" );
	assert( logged( "phase=pickups-checkpoint loaded=autosave ledger=back entries=2" ) == 1 );
	load_map();
	SV_FreeEdict( &entities[AMMO] ); SV_FreeEdict( &entities[SYRINGE] );
	place( 20, "ammo_glock", map_landed[0] )->v.spawnflags = COF_PICKUP_COPY_FLAG;
	place( 21, "weapon_syringe", map_landed[1] )->v.spawnflags = COF_PICKUP_COPY_FLAG;
	frame();
	assert( !sees( host(), 20 ) && sees( guest(), 20 ) && refused( host(), 20 ) && !refused( guest(), 20 ));

	/* everyone took one, nothing lies there: on the next visit nothing is made, until someone new comes */
	taken_as_ammo( 20 );
	command( 1 );
	frame();
	load_map();
	SV_FreeEdict( &entities[AMMO] );
	frame();
	assert( logged( "waiting=1" ) == 1 && !copy_of( "ammo_glock" ));
	player( 3, "Late", 14, 0 );
	frame();
	e = copy_of( "ammo_glock" );
	assert( e && sees( &entities[4], e ) && !sees( host(), e ) && !sees( guest(), e ));
	clients[3].state = 0;

	/* another checkpoint (or Continue after a restart): the ledger forgets, a copy lying is anyone's again */
	SV_CoF_PickupsCheckpointLoaded( "manual" );
	assert( logged( "ledger=forgotten entries=0" ) == 1 );
	load_map();
	SV_FreeEdict( &entities[SYRINGE] );
	place( 21, "weapon_syringe", map_landed[1] )->v.spawnflags = COF_PICKUP_COPY_FLAG;
	frame();
	assert( sees( host(), 21 ) && !refused( host(), 21 ) && refused( bot(), 21 ));

	/* the map's own pickup lying means nobody took it in this world, whatever an older ledger says */
	taken_as_gun( 21, host() );
	command( 0 );
	frame();
	load_map();
	frame();
	assert( sees( host(), SYRINGE ) && !refused( host(), SYRINGE ));

	/* a new session: nobody took anything */
	SV_CoF_PickupsReset();
	load_map();
	frame();
	assert( logged( "ledger=0" ) >= 1 );
}

static void switched_off_and_failures( void )
{
	int copy;
	setup();
	frame();
	taken_as_ammo( AMMO );
	command( 0 );
	frame();
	copy = copy_of( "ammo_glock" );
	assert( copy && !sees( host(), copy ));
	/* the host switches it off: the copy is an ordinary pickup, the retail rules from now on */
	cof_coop_pickups.value = 0.0f;
	frame();
	assert( sees( host(), copy ) && !refused( host(), copy ) && !refused( bot(), copy ) && !refused( bot(), SYRINGE ));
	taken_as_gun( SYRINGE, host());
	command( 0 );
	frame();
	assert( !copy_of( "weapon_syringe" ));
	/* and on again: found again, the copy the ledger knows still hidden from the host */
	cof_coop_pickups.value = 1.0f;
	frame();
	assert( logged( "phase=pickups map" ) == 2 && !sees( host(), copy ));
	/* not story co-op: nothing */
	gate = 0;
	frame();
	assert( sees( host(), copy ) && !refused( bot(), copy ));
	/* 1.3.5: Cry of Fear's own co-op runs them as the story does; switched off and on again there, the copy the
	   ledger knows stays hidden from the host (it is not cleared at the find: its level changes clear it) */
	official = 1;
	frame();
	assert( !sees( host(), copy ) && refused( host(), copy ));
	cof_coop_pickups.value = 0.0f; frame();
	cof_coop_pickups.value = 1.0f; frame();
	assert( !sees( host(), copy ) && refused( host(), copy ));
	official = 0;
	gate = 1;
	frame();

	/* a copy that will not spawn is tried again later, then given up */
	setup();
	frame();
	spawn_fails = 3;
	taken_as_ammo( AMMO );
	command( 0 );
	frame();
	assert( logged( "phase=pickup-copy class=ammo_glock block=1 result=failed" ) == 1 );
	frame();
	assert( logged( "result=failed" ) == 1 ); /* not before the retry */
	sv.time += 5.0;
	frame();
	assert( logged( "result=failed" ) == 2 );
	sv.time += 5.0;
	frame();
	assert( logged( "phase=pickup-gone class=ammo_glock block=1 reason=no-copy" ) == 1 );
	sv.time += 5.0;
	frame();
	assert( !copy_of( "ammo_glock" ));

	/* never the game's last edicts: the copy waits for room, then is made */
	setup();
	frame();
	gameinfo.max_edicts = svgame.numEntities + 100;
	taken_as_gun( SYRINGE, host());
	command( 0 );
	frame();
	assert( logged( "phase=pickup-copy class=weapon_syringe block=2 result=no-room" ) == 1 && !copy_of( "weapon_syringe" ));
	sv.time += 5.0;
	frame();
	assert( logged( "result=no-room" ) == 1 && !copy_of( "weapon_syringe" )); /* told once */
	gameinfo.max_edicts = 4096;
	sv.time += 5.0;
	frame();
	assert( copy_of( "weapon_syringe" ) && !sees( host(), copy_of( "weapon_syringe" )));

	/* a command before the map's pickups are found does nothing */
	setup();
	taken_as_ammo( AMMO );
	command( 0 );
	assert( !logged( "pickup-taken" ));
}

/* After 1.1.9: the bonus room's items a chapter start lays out (sv_cof_bonus_kit.h). No block of the map's text: the
 * bonus mark tells them, the ledger keeps them as blocks COF_PICKUP_BONUS_BLOCK + the kit's item. */
static int bonus_copy_of( const char *classname )
{
	int e, found = 0;
	for( e = svs.maxclients + 1; e < svgame.numEntities; e++ )
		if( !entities[e].free && !( entities[e].v.flags & FL_KILLME ) && !strcmp( SV_GetString( entities[e].v.classname ), classname ) &&
			( entities[e].v.spawnflags & COF_PICKUP_COPY_FLAG ) && ( entities[e].v.spawnflags & COF_PICKUP_BONUS_FLAG ))
			found = e;
	return found;
}

static void the_bonus_kit( void )
{
	static const float famas_at[3] = { 400, 0, 0 }, nv_at[3] = { 440, 0, 0 }, mp5_at[3] = { 480, 0, 0 }, angles[3] = { 0, 56, 0 };
	edict_t *famas, *nv, *mp5, *made;
	int f, n, copy, e;
	setup();
	/* the chapter start lays the kit out (the map's own FAMAS lies at the very same spot: each keeps its own) */
	famas = SV_CoF_PickupsSpawnBonus( 0, famas_at, angles, false );
	nv = SV_CoF_PickupsSpawnBonus( 4, nv_at, angles, false );
	mp5 = SV_CoF_PickupsSpawnBonus( 5, mp5_at, angles, false );
	assert( famas && nv && mp5 && !SV_CoF_PickupsSpawnBonus( 6, famas_at, angles, false ) && !SV_CoF_PickupsSpawnBonus( -1, famas_at, angles, false ));
	f = NUM_FOR_EDICT( famas ); n = NUM_FOR_EDICT( nv );
	assert( famas->v.spawnflags == COF_PICKUP_BONUS_FLAG && !strcmp( keys_seen[f], "rendercolor=0 0 0;frags=-1;" ) && famas->v.frags == -1.0f );
	assert( famas->v.origin[0] == 400.0f && famas->v.angles[1] == 56.0f && !strcmp( keys_seen[n], "frags=-1;" ));
	/* the room's camera keeps its unlock check (spawnflags 1), with the mark */
	made = SV_CoF_PickupsSpawnBonus( 3, mp5_at, angles, true );
	assert( made && made->v.spawnflags == ( 1 | COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG ));
	SV_FreeEdict( made );
	frame();
	assert( logged( "phase=pickups map=c_test found=8 lying=8 copies=0 waiting=0 none=0 skipped=0 ledger=0 bonus=3" ) == 1 );
	assert( sees( host(), f ) && sees( guest(), f ) && !refused( host(), f ) && refused( bot(), f ));
	assert( !refused( host(), FAMAS ) && refused( bot(), FAMAS )); /* the map's own FAMAS is still its block's */

	/* the host takes the kit's FAMAS: the guest's copy, with the kit's keys and both marks, where it lay */
	taken_as_gun( f, host());
	command( 0 );
	frame();
	assert( logged( "phase=pickup-taken userid=11 class=weapon_famas block=65536 companion=0 takers=1" ) == 1 );
	copy = bonus_copy_of( "weapon_famas" );
	assert( copy && copy != f && copy != FAMAS && !strcmp( keys_seen[copy], "rendercolor=0 0 0;frags=-1;" ));
	assert( entities[copy].v.spawnflags == ( COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG ) && entities[copy].v.origin[0] == 400.0f );
	assert( !sees( host(), copy ) && sees( guest(), copy ) && refused( host(), copy ) && !refused( guest(), copy ) && refused( bot(), copy ));

	/* the night vision: a CItem taken with +use (or by touch with +use pressed) and removed at once: a take */
	removed( n );
	command( 1 );
	frame();
	assert( logged( "phase=pickup-taken userid=12 class=item_nightvision block=65540 companion=0 takers=1" ) == 1 );
	e = bonus_copy_of( "item_nightvision" );
	assert( e && sees( host(), e ) && !sees( guest(), e ) && refused( guest(), e ) && !refused( host(), e ) && !strcmp( keys_seen[e], "frags=-1;" ));
	SV_FreeEdict( &entities[n] );
	removed( e );
	command( 0 );
	frame();
	SV_FreeEdict( &entities[e] );
	frame();
	assert( !bonus_copy_of( "item_nightvision" )); /* everyone here has one */
	/* the MP5 removes itself in the host's first update (no donator level 2-4): gone for good, no copy */
	removed( NUM_FOR_EDICT( mp5 ));
	command( 0 );
	frame();
	assert( logged( "phase=pickup-gone class=weapon_mp5 block=65541 reason=removed" ) == 1 && !bonus_copy_of( "weapon_mp5" ));

	/* a late joiner: theirs of each, hidden from those who took one */
	player( 3, "Late", 14, 0 );
	frame();
	e = bonus_copy_of( "item_nightvision" );
	assert( e && sees( &entities[4], e ) && !sees( host(), e ) && !sees( guest(), e ));
	assert( logged( "phase=pickup-copy class=weapon_famas block=65536" ) == 1 ); /* the guest's FAMAS still lies */
	clients[3].state = 0;
	SV_CoF_PickupsCheckpointSaved( "autosave" );

	/* a revisit: the snapshot holds the guest's FAMAS copy and the late joiner's night vision, both marked */
	load_map();
	place( 20, "weapon_famas", famas_at )->v.spawnflags = COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG;
	place( 21, "item_nightvision", nv_at )->v.spawnflags = COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG;
	frame();
	assert( logged( "phase=pickups map=c_test found=7 lying=7 copies=2 waiting=0" ) == 1 && logged( "bonus=2\n" ) == 1 );
	assert( !sees( host(), 20 ) && sees( guest(), 20 ) && !sees( host(), 21 ) && !sees( guest(), 21 ) && refused( guest(), 21 ));
	/* the guest takes theirs; a team wipe reloads the checkpoint: that take is undone, the copy the guest's again */
	taken_as_gun( 20, guest());
	command( 1 );
	frame();
	assert( logged( "class=weapon_famas block=65536 companion=0 takers=2" ) == 1 );
	SV_CoF_PickupsCheckpointLoaded( "autosave" );
	load_map();
	place( 20, "weapon_famas", famas_at )->v.spawnflags = COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG;
	frame();
	assert( !sees( host(), 20 ) && sees( guest(), 20 ) && !refused( guest(), 20 ));
	/* everyone took the FAMAS, nothing lies: the next visit makes none, the ledger still knows the kit's FAMAS */
	taken_as_gun( 20, guest());
	command( 1 );
	frame();
	load_map();
	frame();
	assert( !bonus_copy_of( "weapon_famas" ) && logged( "found=7 lying=5 copies=0 waiting=2 none=0" ) == 1 );
	player( 3, "Late", 14, 0 );
	frame();
	e = bonus_copy_of( "weapon_famas" );
	assert( e && sees( &entities[4], e ) && !sees( host(), e ) && !sees( guest(), e ) && !strcmp( keys_seen[e], "rendercolor=0 0 0;frags=-1;" ));
	clients[3].state = 0;
	/* Continue after a restart: the ledger forgets, a copy lying is anyone's */
	SV_CoF_PickupsCheckpointLoaded( "other" );
	load_map();
	place( 22, "weapon_famas", famas_at )->v.spawnflags = COF_PICKUP_BONUS_FLAG | COF_PICKUP_COPY_FLAG;
	frame();
	assert( sees( host(), 22 ) && !refused( host(), 22 ) && refused( bot(), 22 ));
	/* nothing marked lying and nothing in the ledger: no slot for the kit at all (a map no chapter started on) */
	SV_CoF_PickupsReset();
	load_map();
	frame();
	assert( logged( "ledger=0 bonus=0" ) >= 1 );

	/* a kit laid out with the pickups off (a set each), the pickups on again: the first of each is the slot, the
	 * others ordinary pickups the AI companion is not kept from */
	setup();
	cof_coop_pickups.value = 0.0f;
	famas = SV_CoF_PickupsSpawnBonus( 0, famas_at, angles, false );
	made = SV_CoF_PickupsSpawnBonus( 0, nv_at, angles, false );
	frame();
	assert( !refused( bot(), NUM_FOR_EDICT( famas )) && !refused( bot(), NUM_FOR_EDICT( made )));
	cof_coop_pickups.value = 1.0f;
	frame();
	assert( refused( bot(), NUM_FOR_EDICT( famas )) && !refused( bot(), NUM_FOR_EDICT( made )));
	assert( logged( "bonus=1\n" ) == 1 );
}

/* 1.3: the bonus room's own night vision (c_start, c_bridge; user report 2026-10-25: only one player could take
 * it). It lies unmarked, a CItem of no class of the table: with its line unlocked it gets the bonus mark at the map's
 * first story frame and is the kit's from then on. */
static void the_rooms_own_night_vision( void )
{
	static const float room_at[3] = { 500, 0, 0 };
	int n, e;
	setup();
	n = NUM_FOR_EDICT( place( 30, "item_nightvision", room_at ));
	frame();
	assert( logged( "phase=pickups-room-nightvision entity=30\n" ) == 1 && entities[n].v.spawnflags == COF_PICKUP_BONUS_FLAG );
	assert( logged( "found=6 lying=6 copies=0 waiting=0 none=0 skipped=0 ledger=0 bonus=1\n" ) == 1 && nv_asked == 1 );
	assert( sees( host(), n ) && sees( guest(), n ) && !refused( host(), n ) && !refused( guest(), n ) && refused( bot(), n ));
	frame();
	assert( logged( "pickups-room-nightvision" ) == 1 && nv_asked == 1 ); /* once a map */
	/* the host takes it (by touch with +use pressed: removed at once, in their command): the guest's copy where it
	 * lay, with the room's keys; the host neither sees it nor finds it by touch or +use */
	removed( n );
	command( 0 );
	frame();
	assert( logged( "phase=pickup-taken userid=11 class=item_nightvision block=65540 companion=0 takers=1" ) == 1 );
	e = bonus_copy_of( "item_nightvision" );
	assert( e && e != n && entities[e].v.origin[0] == 500.0f && !strcmp( keys_seen[e], "frags=-1;" ));
	assert( !sees( host(), e ) && sees( guest(), e ) && refused( host(), e ) && !refused( guest(), e ) && refused( bot(), e ));
	/* the guest takes theirs: everyone here has one, no more copies */
	SV_FreeEdict( &entities[n] );
	removed( e );
	command( 1 );
	frame();
	SV_FreeEdict( &entities[e] );
	frame();
	assert( logged( "class=item_nightvision block=65540 companion=0 takers=2" ) == 1 && !bonus_copy_of( "item_nightvision" ));

	/* its line locked: left as the game has it (it removes itself at a first update, in a command: no take read) */
	setup(); nv_unlocked = 0;
	n = NUM_FOR_EDICT( place( 30, "item_nightvision", room_at ));
	frame();
	assert( !logged( "pickups-room-nightvision" ) && entities[n].v.spawnflags == 0 && logged( "bonus=0\n" ) == 1 && nv_asked == 1 );
	removed( n );
	command( 0 );
	frame();
	assert( !logged( "class=item_nightvision" ) && !bonus_copy_of( "item_nightvision" ));

	/* a marked one lies too (the kit's, a revisit's copy): the unmarked one is not made a second slot */
	setup();
	n = NUM_FOR_EDICT( place( 31, "item_nightvision", room_at ));
	place( 32, "item_nightvision", room_at )->v.spawnflags = COF_PICKUP_BONUS_FLAG;
	frame();
	assert( !logged( "pickups-room-nightvision" ) && entities[n].v.spawnflags == 0 && logged( "bonus=1\n" ) == 1 && !nv_asked );
	/* none lying: nothing asked; carried (a held one is no pickup): not marked */
	setup();
	frame();
	assert( !nv_asked && logged( "bonus=0\n" ) == 1 );
	setup();
	n = NUM_FOR_EDICT( place( 30, "item_nightvision", room_at ));
	entities[n].v.owner = host();
	frame();
	assert( entities[n].v.spawnflags == 0 && !nv_asked );
	/* the pickups off: the retail rules, nothing marked */
	setup(); cof_coop_pickups.value = 0.0f;
	n = NUM_FOR_EDICT( place( 30, "item_nightvision", room_at ));
	frame();
	assert( entities[n].v.spawnflags == 0 && !nv_asked );
	cof_coop_pickups.value = 1.0f;
}

int main( void )
{
	a_fresh_map();
	a_revisit_and_checkpoints();
	switched_off_and_failures();
	the_bonus_kit();
	the_rooms_own_night_vision();
	printf( "pickups scenarios passed\n" );
	return 0;
}
