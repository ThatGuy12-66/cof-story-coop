/* Story co-op rescues from retail soft-locks (sv_cof_rescue.c) against engine doubles: no game DLL, networking or
 * game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
typedef unsigned char byte;
typedef int qboolean;
typedef int string_t;
typedef float vec3_t[3];
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_spawned 4
#define ss_active 2
#define DEAD_NO 0
#define DEAD_DEAD 2
#define FL_FROZEN (1<<12)
#define FL_KILLME (1<<30)
#define FCL_FAKECLIENT (1<<7)
#define Q_strcmp strcmp
#define Q_strncpy( d, s, n ) ( strncpy( d, s, n ), ( d )[( n ) - 1] = 0 )
#define SetBits(f,b) ((f)|=(b))
#define FBitSet(f,b) ((f)&(b))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
/* only compared, so the squared distance does (cc on Linux links no libm here) */
#define VectorDistance(a,b) ((float)(((a)[0]-(b)[0])*((a)[0]-(b)[0])+((a)[1]-(b)[1])*((a)[1]-(b)[1])+((a)[2]-(b)[2])*((a)[2]-(b)[2])))
typedef struct edict_s edict_t;
typedef struct {
	string_t classname, targetname;
	vec3_t origin, angles;
	float health;
	int deadflag, flags, body;
	edict_t *owner;
} entvars_t;
struct edict_s { int free, serialnumber; void *pvPrivateData; entvars_t v; };
typedef struct { int state, userid; unsigned int flags; edict_t *edict; } sv_client_t;
static struct { int state; double time; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
typedef struct { float time; } globalvars_t;
static globalvars_t globals;
static struct { edict_t *edicts; int numEntities; struct { void (*pfnClientKill)( edict_t * ); } dllFuncs; globalvars_t *globals; } svgame;
#define ENTITIES 24
static edict_t entities[ENTITIES];
static sv_client_t clients[MAX_CLIENTS];
static byte private_data[ENTITIES][0x2900];
static const char *strings[] = { "", "player", "monster_bosschainsaw", "theboss", "third_person_interaction", "monster_slower",
	"item_padlock", "wheelchair", "monster_crazybitch", "monster_roofboss", "monster_doctorboss", "monster_booksimon" };
/* a clear line from one body's eyes to another's (the world only): unless hidden */
static int hidden[ENTITIES];
static qboolean SV_CoF_Sees( edict_t *from, edict_t *to ) { return !hidden[to - entities]; }
static int kills[ENTITIES], controls_given[ENTITIES], messages_to[ENTITIES];
static char message_name[32];
static byte message[16];
static int message_size;
static char physics_key[32], physics_value[32];
static void client_kill( edict_t *e ) { kills[e - entities]++; e->v.health = 0; e->v.deadflag = DEAD_DEAD; }
static qboolean SV_CoF_SendUserMessage( edict_t *to, const char *name, const byte *payload, int size, qboolean reliable )
{
	messages_to[to - entities]++; strcpy( message_name, name ); memcpy( message, payload, size ); message_size = size; return true;
}
/* CBasePlayer::EnableControl( TRUE ), thiscall 100DEFF0 */
static qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
	int i;
	assert( rva == 0xDEFF0 && arg == (void *)1 );
	for( i = 0; i < ENTITIES; i++ ) if( private_data[i] == self ) controls_given[i]++;
	return true;
}
static void SV_CoF_SetPhysicsKey( edict_t *e, const char *key, const char *value ) { strcpy( physics_key, key ); strcpy( physics_value, value ); }
static edict_t *think_first, *think_slot_one;
static void SV_CoF_ThinkFirst( edict_t *e ) { think_first = e; }
static void SV_CoF_ThinkSlotOne( edict_t *e ) { think_slot_one = e; }
static char log_text[8192];
static size_t log_length;
static int gate, spawned;
static char spawned_class[64], spawned_name[64];
static vec3_t spawned_at, spawned_angles;

static qboolean SV_CoF_StoryCoopEnabled( void ) { return gate; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && e >= entities && e < entities + ENTITIES && !e->free; }
static edict_t *SV_EdictNum( int i ) { return &entities[i]; }
static int NUM_FOR_EDICT( const edict_t *e ) { return (int)( e - entities ); }
static const char *SV_GetString( string_t s ) { return strings[s]; }
static byte *SV_CoF_StoryEntityPrivate( edict_t *e, size_t need ) { return SV_IsValidEdict( e ) && need <= 0x2900 ? e->pvPrivateData : NULL; }
/* the map's own boss, spawned again in the first free edict */
static edict_t *SV_CoF_SpawnMapEntity( const char *classname, const char *targetname, const vec3_t origin, const vec3_t angles )
{
	int i;
	spawned++; strcpy( spawned_class, classname ); strcpy( spawned_name, targetname ? targetname : "" );
	VectorCopy( origin, spawned_at ); VectorCopy( angles, spawned_angles );
	for( i = 3; i < ENTITIES && !entities[i].free; i++ );
	if( i == ENTITIES ) return NULL;
	memset( &entities[i].v, 0, sizeof( entities[i].v ));
	entities[i].free = 0; entities[i].serialnumber++; entities[i].v.classname = 2; entities[i].v.targetname = 3; entities[i].v.health = 1000;
	return &entities[i];
}
static char argv0[32];
static const char *Cmd_Argv( int i ) { return i == 0 ? argv0 : ""; }
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

#include "rescue_under_test.c"

static void setup( void )
{
	int i;
	memset( entities, 0, sizeof( entities )); memset( clients, 0, sizeof( clients )); memset( private_data, 0, sizeof( private_data ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	gate = 1; spawned = 0; spawned_class[0] = spawned_name[0] = 0;
	sv.state = ss_active; sv.time = 100; svs.maxclients = 2; svs.clients = clients; svs.spawncount++;
	svgame.edicts = entities; svgame.numEntities = ENTITIES; svgame.dllFuncs.pfnClientKill = client_kill;
	svgame.globals = &globals; globals.time = 100.0f;
	memset( kills, 0, sizeof( kills )); memset( controls_given, 0, sizeof( controls_given )); memset( messages_to, 0, sizeof( messages_to ));
	memset( hidden, 0, sizeof( hidden ));
	message_name[0] = physics_key[0] = physics_value[0] = 0; message_size = 0;
	for( i = 0; i < ENTITIES; i++ ) { entities[i].pvPrivateData = private_data[i]; entities[i].serialnumber = 1; }
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned; clients[i].userid = 100 + i; clients[i].edict = &entities[i + 1];
		entities[i + 1].v.classname = 1; entities[i + 1].v.health = 100;
	}
	for( i = 3; i < ENTITIES; i++ ) entities[i].free = 1;
	SV_CoF_RescueReset();
}
static edict_t *spawn( int i, string_t classname, string_t targetname )
{
	edict_t *e = &entities[i];
	e->free = 0; e->v.classname = classname; e->v.targetname = targetname; e->v.health = 100;
	return e;
}
static void set_handle( int entity, int offset, edict_t *target, int serial )
{
	struct { edict_t *e; int serial; } h = { target, serial };
	memcpy( private_data[entity] + offset, &h, sizeof( h ));
}
static void frame( double dt ) { sv.time += dt; SV_CoF_RescueFrame(); }

/* The chainsaw boss grabs someone: it makes the kill animation (third_person_interaction, its victim at +0x36C and
 * itself at +0x374) and removes itself without dying, so its TriggerTarget never fires. */
static void boss_grabs( int boss, int victim )
{
	edict_t *kill = spawn( 10, 4, 0 );
	VectorCopy( entities[boss].v.origin, kill->v.origin ); kill->v.origin[0] += 30; kill->v.angles[1] = 270;
	set_handle( 10, 0x36C, &entities[victim], entities[victim].serialnumber );
	set_handle( 10, 0x374, &entities[boss], entities[boss].serialnumber );
	entities[boss].v.flags |= FL_KILLME;
}

int main( void )
{
	edict_t *boss;

	/* the guest is grabbed: the host lives on, so after the victim's death the boss comes back */
	setup(); boss = spawn( 5, 2, 3 ); boss->v.origin[0] = 300; boss->v.angles[1] = 90;
	frame( 0.1 ); boss_grabs( 5, 2 );
	frame( 0.1 ); assert( !spawned );                                  /* the victim is still being killed */
	entities[5].free = 1; entities[5].serialnumber++;                  /* the engine freed the removed boss */
	frame( 1.0 ); assert( !spawned );
	entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;      /* the animation killed the guest */
	frame( 1.0 ); assert( !spawned );                                  /* a moment for the death */
	frame( 1.5 );
	assert( spawned == 1 && !strcmp( spawned_class, "monster_bosschainsaw" ) && !strcmp( spawned_name, "theboss" ));
	assert( spawned_at[0] == 330 && spawned_angles[1] == 90 );        /* where it stood, facing as it faced */
	assert( entities[10].v.flags & FL_KILLME );                        /* the animation is gone */
	assert( logged( "phase=boss-respawned class=monster_bosschainsaw name=theboss" ) == 1 );
	frame( 5.0 ); assert( spawned == 1 );                              /* once */

	/* everyone is down: the team wipe restarts the checkpoint, nothing is spawned */
	setup(); boss = spawn( 5, 2, 3 );
	frame( 0.1 ); boss_grabs( 5, 2 );
	entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD; entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	frame( 3.0 ); frame( 3.0 ); assert( !spawned && logged( "phase=boss-respawn-skipped reason=nobody-alive" ) == 1 );

	/* the animation never kills (a victim the handle lost): after 15 s the boss comes back all the same */
	setup(); boss = spawn( 5, 2, 3 );
	frame( 0.1 ); boss_grabs( 5, 2 ); set_handle( 10, 0x36C, NULL, 0 );
	frame( 1.0 ); assert( !spawned ); /* no victim seen dying yet: wait */
	entities[2].v.health = 100; frame( 16.0 ); assert( spawned == 1 );

	/* someone else's kill animation (no chainsaw boss behind it) is not ours */
	setup(); spawn( 5, 5, 0 );
	frame( 0.1 ); boss_grabs( 5, 2 ); entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	frame( 5.0 ); frame( 20.0 ); assert( !spawned );

	/* not story co-op: the game's own rules */
	setup(); boss = spawn( 5, 2, 3 ); gate = 0;
	frame( 0.1 ); boss_grabs( 5, 2 ); entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	frame( 5.0 ); frame( 20.0 ); assert( !spawned );

	/* The c_start padlock: whoever's padlockrotate opened it is "the first player" while its Finished think (2 s later)
	 * runs; that think releases the first player, which was the host, not the guest left frozen in its camera. */
	setup(); spawn( 7, 6, 0 );
	set_handle( 2, 0x2350, &entities[7], 1 ); strcpy( argv0, "padlockrotate" );
	assert( !SV_CoF_RescueClientCommand( &clients[1] ));                 /* observed, the game still turns the dial */
	assert( SV_CoF_RescueThinkFirst( &entities[7] ) == NULL );            /* not open yet: no Finished to run */
	entities[7].v.body = 1;                                               /* the guest's turn opened it */
	assert( SV_CoF_RescueThinkFirst( &entities[7] ) == &entities[2] );
	assert( SV_CoF_RescueThinkFirst( &entities[1] ) == NULL );            /* other thinks are left alone */
	set_handle( 1, 0x2350, &entities[7], 1 ); assert( !SV_CoF_RescueClientCommand( &clients[0] ));
	assert( SV_CoF_RescueThinkFirst( &entities[7] ) == &entities[1] );    /* the last turn before it opened */
	strcpy( argv0, "padlockcancel" ); set_handle( 2, 0x2350, &entities[7], 1 ); SV_CoF_RescueClientCommand( &clients[1] );
	assert( SV_CoF_RescueThinkFirst( &entities[7] ) == &entities[1] );    /* only turns count */
	clients[0].state = 0; assert( SV_CoF_RescueThinkFirst( &entities[7] ) == NULL ); /* the solver left */
	setup(); spawn( 7, 6, 0 ); entities[7].v.body = 1; gate = 0;
	strcpy( argv0, "padlockrotate" ); set_handle( 2, 0x2350, &entities[7], 1 ); SV_CoF_RescueClientCommand( &clients[1] );
	assert( SV_CoF_RescueThinkFirst( &entities[7] ) == NULL );            /* not story co-op */

	/* c_ending4b's wheelchair dies under its rider (m_bDead chair+0x368, rider chair+0x384): retail keeps the rider at
	 * 1 hp, frozen in the dead chair for good (game over only in single player). Like single player's game over, the
	 * party starts the checkpoint again: the rider is let out of the chair and everyone alive dies (the game's team
	 * wipe restarts the map). */
	{
		uint32_t rider = (uint32_t)(uintptr_t)private_data[2], value;
		int hud = 8 | 1;
		setup(); spawn( 8, 7, 0 );
		private_data[8][0x368] = 1; memcpy( private_data[8] + 0x384, &rider, 4 );
		set_handle( 2, 0x390, &entities[8], 1 ); memcpy( private_data[2] + 0x2500, &hud, 4 );
		entities[1].v.health = 100; entities[2].v.health = 1; entities[2].v.flags = FL_FROZEN;
		frame( 0.1 );
		assert( messages_to[2] == 1 && !strcmp( message_name, "Wheelchair" ) && message_size == 3 &&
			message[0] == 0xFF && message[1] == 0xFF && message[2] == 0 );                /* Wheelchair( -1, 0 ) */
		assert( controls_given[2] == 1 );                                                /* EnableControl( TRUE ) */
		memcpy( &hud, private_data[2] + 0x2500, 4 ); assert( hud == 1 );                 /* HIDEHUD_HEALTH off */
		memcpy( &value, private_data[8] + 0x384, 4 ); assert( !value );                  /* the chair lets go */
		memcpy( &value, private_data[2] + 0x390, 4 ); assert( !value );                  /* the rider too */
		assert( !strcmp( physics_key, "chr" ) && !strcmp( physics_value, "1" ));          /* walking again */
		assert( kills[1] == 1 && kills[2] == 1 && logged( "phase=wheelchair-death-restart userid=101" ) == 1 );
		frame( 0.1 ); assert( kills[1] == 1 && kills[2] == 1 && messages_to[2] == 1 );    /* once */
		/* a living chair, or a dead one nobody rides, is left alone */
		setup(); spawn( 8, 7, 0 ); memcpy( private_data[8] + 0x384, &rider, 4 ); frame( 0.1 ); assert( !kills[1] && !kills[2] );
		setup(); spawn( 8, 7, 0 ); private_data[8][0x368] = 1; frame( 0.1 ); assert( !kills[1] && !kills[2] );
	}

	/* The crazybitch picks her moment from her enemy (m_hEnemy +0x184) but makes INDEXENT( 1 ), the host, turn his gun
	 * on himself, wherever he is (MonsterThink 10055860): in the think that takes the gun, edict 1 is her enemy. */
	{
		float due = 99.0f;
		setup(); spawn( 9, 8, 0 ); set_handle( 9, 0x184, &entities[2], 1 );
		/* 1.1.8 (K5): not in any other think (her sight check walks the edicts from edict 1): not started, not ready,
		 * or the attempt not yet due */
		assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );
		private_data[9][0x370] = 1; assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );
		private_data[9][0x372] = 1; due = 101.0f; memcpy( private_data[9] + 0x36C, &due, 4 );
		assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );
		due = 100.0f; memcpy( private_data[9] + 0x36C, &due, 4 ); assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );
		private_data[9][0x370] = 0; due = 99.0f; memcpy( private_data[9] + 0x36C, &due, 4 );
		assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );
		private_data[9][0x370] = 1;
	}
	assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == &entities[2] );  /* the guest she fights */
	set_handle( 9, 0x184, &entities[1], 1 );
	assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );          /* the host: as retail */
	set_handle( 9, 0x184, &entities[2], 1 ); entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );          /* a dead enemy: as retail */
	entities[2].v.health = 100; entities[2].v.deadflag = DEAD_NO; set_handle( 9, 0x184, &entities[2], 7 );
	assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );          /* a stale handle */
	spawn( 11, 5, 0 ); set_handle( 11, 0x184, &entities[2], 1 );
	assert( SV_CoF_RescueThinkSlotOne( &entities[11] ) == NULL );         /* other monsters: as retail */
	set_handle( 9, 0x184, &entities[2], 1 ); gate = 0;
	assert( SV_CoF_RescueThinkSlotOne( &entities[9] ) == NULL );          /* not story co-op */

	/* The c_roofs2 boss looks for "the first player" every think and attacks only while that one is in sight: it goes
	 * for a living player it sees (nearest), and keeps them while they stay so. */
	setup(); spawn( 12, 9, 0 ); entities[12].v.origin[0] = 0;
	entities[1].v.origin[0] = 100; entities[2].v.origin[0] = 300;
	hidden[1] = 1; assert( SV_CoF_RescueThinkFirst( &entities[12] ) == &entities[2] );   /* the host behind cover */
	hidden[1] = 0; assert( SV_CoF_RescueThinkFirst( &entities[12] ) == &entities[2] );   /* kept while still in sight */
	entities[2].v.health = 0; entities[2].v.deadflag = DEAD_DEAD;
	assert( SV_CoF_RescueThinkFirst( &entities[12] ) == &entities[1] );                  /* then the one who lives */
	entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	assert( SV_CoF_RescueThinkFirst( &entities[12] ) == NULL );                          /* nobody: as retail */

	/* The c_attic doctor caches its player (+0x368) and shoots, aims and hurts only that one: a downed or hidden host
	 * left it shooting at nobody. Its think takes the player it should fight. */
	{
		uint32_t host = (uint32_t)(uintptr_t)private_data[1], guest = (uint32_t)(uintptr_t)private_data[2], value;
		setup(); spawn( 13, 10, 0 ); memcpy( private_data[13] + 0x368, &host, 4 );
		entities[1].v.origin[0] = 100; entities[2].v.origin[0] = 300;
		SV_CoF_RescueThinkBegin( &entities[13] ); memcpy( &value, private_data[13] + 0x368, 4 );
		assert( value == host && think_first == &entities[1] ); SV_CoF_RescueThinkEnd();   /* the host in sight: kept */
		entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
		SV_CoF_RescueThinkBegin( &entities[13] ); memcpy( &value, private_data[13] + 0x368, 4 );
		assert( value == guest && think_first == &entities[2] && logged( "phase=boss-target class=monster_doctorboss userid=101" ) == 1 );
		SV_CoF_RescueThinkEnd(); assert( think_first == NULL );

		/* A player who leaves: no boss keeps a raw pointer to them (the book Simons +0x38C/+0x388, the doctor) */
		setup(); spawn( 13, 10, 0 ); spawn( 14, 11, 0 );
		memcpy( private_data[13] + 0x368, &guest, 4 ); memcpy( private_data[14] + 0x38C, &guest, 4 ); memcpy( private_data[14] + 0x388, &guest, 4 );
		memcpy( private_data[14] + 0x380, &host, 4 ); /* not a pointer: the aim state */
		SV_CoF_RescueDrop( &clients[1] );
		memcpy( &value, private_data[13] + 0x368, 4 ); assert( !value );
		memcpy( &value, private_data[14] + 0x38C, 4 ); assert( !value );
		memcpy( &value, private_data[14] + 0x388, 4 ); assert( !value );
		memcpy( &value, private_data[14] + 0x380, 4 ); assert( value == host );
	}

	printf( "rescue scenarios passed\n" );
	return 0;
}
