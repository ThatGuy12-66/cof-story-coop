/* 1.1.9: the chapter start and the story frame's three-slot step (sv_cof_story.c, from the I7 block to
 * SV_CoF_StoryUserinfo), against offline doubles. C1: c_park3's start switches `stater` on through an env_global.
 * I7: the kit stays due (checkpoints wait) until every person carries it, 30 s at most, this map only.
 * I3: a living person not yet in three-slot mode is put in it when the party has it. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define DEAD_NO 0
#define FBitSet( a, b ) (( a ) & ( b ))
#define COF_RETAIL_SLOTS 12
#define COF_RETAIL_ITEM 64
#define Q_snprintf snprintf
#define Q_strcmp strcmp
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef int string_t;
typedef struct { char *szClassName, *szKeyName, *szValue; int fHandled; } KeyValueData;
typedef struct edict_s { int free; void *pvPrivateData; struct { float health; int deadflag, spawnflags; string_t classname; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; edict_t *edict; } sv_client_t;
#include "sv_cof_state_data.h"
#include "sv_cof_chapter_kit.h"

static int fake_keyvalue_calls;
static char kv_state[64], kv_initial[8];
static int spawned_flags = -1, spawns, frees, three_slot_calls, grants, presets;
static void fake_keyvalue( edict_t *ent, KeyValueData *kvd )
{
	fake_keyvalue_calls++;
	assert( !strcmp( kvd->szClassName, "env_global" ) && ent->v.classname == 1 );
	if( !strcmp( kvd->szKeyName, "globalstate" )) snprintf( kv_state, sizeof( kv_state ), "%s", kvd->szValue );
	else if( !strcmp( kvd->szKeyName, "initialstate" )) snprintf( kv_initial, sizeof( kv_initial ), "%s", kvd->szValue );
	else assert( 0 );
	kvd->fHandled = true;
}
static int fake_spawn( edict_t *ent ) { spawns++; spawned_flags = ent->v.spawnflags; return 0; }
static struct { struct { void ( *pfnKeyValue )( edict_t *, KeyValueData * ); int ( *pfnSpawn )( edict_t * ); } dllFuncs; } svgame = { { fake_keyvalue, fake_spawn } };
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; } sv;
static sv_client_t clients[3];
static edict_t edicts[3], helper;
static char inv[3][COF_RETAIL_SLOTS][COF_RETAIL_ITEM];
static int three[3], party_three;
static char cvar[64], console[8192];
static size_t console_length;
static int private_data;

static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static const char *Cvar_VariableString( const char *name ) { assert( !strcmp( name, "cof_chapter_start" )); return cvar; }
static void Cvar_Set( const char *name, const char *value ) { assert( !strcmp( name, "cof_chapter_start" )); snprintf( cvar, sizeof( cvar ), "%s", value ); }
static void SV_CoF_PartyGrant( const char *classname ) { grants++; (void)classname; }
static void SV_CoF_StatePreset( int doctor_weapon, int three_slots ) { presets++; (void)doctor_weapon; party_three |= three_slots; }
static void SV_CoF_StateExport( cof_state_data_t *data ) { memset( data, 0, sizeof( *data )); data->values.three_slots = party_three; }
static string_t SV_AllocString( const char *s ) { assert( !strcmp( s, "env_global" )); return 1; }
static edict_t *SV_CreateNamedEntity( edict_t *unused, string_t classname )
{
	(void)unused;
	memset( &helper, 0, sizeof( helper ));
	helper.v.classname = classname; helper.pvPrivateData = &private_data;
	return &helper;
}
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static void SV_FreeEdict( edict_t *e ) { e->free = 1; frees++; }
static int who( const sv_client_t *cl ) { return (int)( cl - clients ); }
static qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	if( cl->state != cs_spawned ) return false;
	memcpy( slots, inv[who( cl )], sizeof( inv[0] ));
	return true;
}
static qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl ) { return three[who( cl )]; }
static qboolean SV_CoF_RetailThreeSlots( sv_client_t *cl ) { three_slot_calls++; three[who( cl )] = 1; return true; }

#include "story_kit_under_test.c"

static void give( int p, const char *classname )
{
	int i;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ )
		if( !inv[p][i][0] ) { snprintf( inv[p][i], COF_RETAIL_ITEM, "inventoryitems/weapons/%s.txt", classname ); return; }
}
static void setup( const char *map )
{
	int i;
	memset( clients, 0, sizeof( clients )); memset( edicts, 0, sizeof( edicts )); memset( inv, 0, sizeof( inv ));
	memset( three, 0, sizeof( three )); party_three = 0;
	svs.maxclients = 3; svs.clients = clients; svs.spawncount = 7; sv.time = 1;
	for( i = 0; i < 3; i++ )
	{
		clients[i].state = cs_spawned; clients[i].userid = i + 1; clients[i].edict = &edicts[i];
		edicts[i].v.health = 100; edicts[i].v.deadflag = DEAD_NO;
	}
	clients[2].flags = FCL_FAKECLIENT; /* an AI companion: never waited for */
	snprintf( cvar, sizeof( cvar ), "%s", map );
	fake_keyvalue_calls = spawns = frees = three_slot_calls = grants = presets = 0; spawned_flags = -1;
	kv_state[0] = kv_initial[0] = 0; console[0] = 0; console_length = 0;
	cof_story_kit = NULL;
}

int main( void )
{
	/* C1: Chapter 4 switches `stater` on (globalstate stater, initialstate 1, SF_GLOBAL_SET), the helper freed again */
	setup( "c_park3" ); CoF_StoryChapterStart();
	assert( grants == 4 && !cvar[0] && fake_keyvalue_calls == 2 && !strcmp( kv_state, "stater" ) && !strcmp( kv_initial, "1" ));
	assert( spawns == 1 && spawned_flags == 1 && frees == 1 && strstr( console, "cofstory phase=chapter-global state=stater" ));
	/* ...no other chapter touches a global */
	setup( "c_sewer3" ); CoF_StoryChapterStart(); assert( grants == 4 && !spawns && !fake_keyvalue_calls );
	setup( "c_bridge" ); CoF_StoryChapterStart(); assert( !grants && !spawns && !SV_CoF_StoryKitDue());

	/* I7: Chapter 2's kit: checkpoints wait until every person carries the phone, switchblade and glock */
	setup( "c_basement" ); CoF_StoryChapterStart();
	assert( SV_CoF_StoryKitDue());
	give( 0, "weapon_mobile" ); give( 0, "weapon_switchblade" ); give( 0, "weapon_glock" );
	give( 1, "weapon_mobile" ); give( 1, "weapon_glock" );
	sv.time = 5; assert( SV_CoF_StoryKitDue()); /* the partner still lacks the switchblade */
	give( 1, "weapon_switchblade" ); sv.time = 5.5;
	assert( !SV_CoF_StoryKitDue() && strstr( console, "cofstory phase=chapter-kit-delivered" ));
	assert( !SV_CoF_StoryKitDue() && !strstr( console, "wait-over" )); /* done for good: the companion never counted */
	/* ...a person still joining is not waited for here (the save waits for them anyway) */
	setup( "c_basement" ); CoF_StoryChapterStart(); clients[1].state = cs_connected;
	give( 0, "weapon_mobile" ); give( 0, "weapon_switchblade" ); give( 0, "weapon_glock" );
	assert( !SV_CoF_StoryKitDue());
	/* ...at most 30 s from the first checkpoint it held back (a scene before that does not count) */
	setup( "c_attic" ); CoF_StoryChapterStart(); sv.time = 80; /* the opening scene held the saves until now */
	assert( SV_CoF_StoryKitDue()); sv.time = 109.9; assert( SV_CoF_StoryKitDue());
	sv.time = 110.1; assert( !SV_CoF_StoryKitDue() && strstr( console, "cofstory phase=chapter-kit-wait-over" ));
	/* ...this map only: another map or a checkpoint loaded (a new generation) is not this start's */
	setup( "c_ending5" ); CoF_StoryChapterStart(); svs.spawncount++; assert( !SV_CoF_StoryKitDue());
	svs.spawncount--; assert( !SV_CoF_StoryKitDue());
	/* ...New game and Continue (no chapter named) never wait */
	setup( "" ); CoF_StoryChapterStart(); assert( !SV_CoF_StoryKitDue());

	/* C2: Ending 4's partners get Ending 5's kit, in three slots */
	setup( "c_ending4" ); CoF_StoryChapterStart(); assert( grants == 2 && presets == 1 && party_three && SV_CoF_StoryKitDue());

	/* I3: a party in three-slot mode puts a living person in it at once; not the dead, not a companion, nobody twice */
	setup( "" ); party_three = 1; edicts[1].v.health = 0;
	CoF_StoryThreeSlots();
	assert( three[0] && !three[1] && !three[2] && three_slot_calls == 1 && strstr( console, "cofstory phase=three-slots userid=1" ));
	CoF_StoryThreeSlots(); assert( three_slot_calls == 1 );
	edicts[1].v.health = 100; CoF_StoryThreeSlots(); assert( three[1] && three_slot_calls == 2 );
	/* ...a party before c_bridge: nothing */
	setup( "" ); CoF_StoryThreeSlots(); assert( !three[0] && !three[1] && !three_slot_calls );
	/* ...the chapter start's preset counts from its own first frame */
	setup( "c_attic" ); CoF_StoryChapterStart(); CoF_StoryThreeSlots(); assert( three[0] && three[1] && three_slot_calls == 2 );

	puts( "story kit scenarios passed" );
	return 0;
}
