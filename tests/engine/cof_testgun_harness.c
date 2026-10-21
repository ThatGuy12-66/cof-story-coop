/* The test gun (sv_cof_testgun.c) against engine doubles: no game DLL,
 * networking, or game process. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define MAX_CLIENTS 4
#define cs_free 0
#define cs_spawned 4
#define DEAD_NO 0
#define DEAD_DEAD 2
#define FL_FROZEN (1<<12)
#define FCL_FAKECLIENT (1<<7)
#define MOVETYPE_WALK 3
#define MOVETYPE_FLY 5
#define ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
#define FBitSet(f,b) ((f)&(b))
#define Q_strcmp strcmp
#define Q_strncmp strncmp
#define Q_stricmp _stricmp
#define Q_strnicmp _strnicmp
#define Q_snprintf snprintf
typedef struct { float health, fuser3; int deadflag, flags, movetype; } entvars_t;
typedef struct edict_s { int free; entvars_t v; } edict_t;
typedef struct { int state, userid; unsigned int flags; edict_t *edict, *pViewEntity; } sv_client_t;
typedef struct { float value; const char *string; } convar_t;
static struct { double time; } sv;
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static edict_t entities[MAX_CLIENTS + 1];
static sv_client_t clients[MAX_CLIENTS];
static convar_t cvar = { 0, "" };
static char log_text[16384];
static size_t log_length;
/* per slot: the game's switch, what the player carries of the gun (0/1/2) */
static int infinite[MAX_CLIENTS], held[MAX_CLIENTS], supplies[MAX_CLIENTS];
static int transition, wheelchair, phone, pending, free_slots, give_ok, gives, equips, switch_ok;
static char last_give[32], last_equip[32], last_asked[32];

static void Q_strncpy( char *d, const char *s, size_t n ) { size_t i; for( i = 0; i + 1 < n && s[i]; i++ ) d[i] = s[i]; d[i] = 0; }
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
	assert( !strcmp( name, "cof_test_gun" ) && !value[0] ); /* off unless asked for */
	return &cvar;
}
static int cheats; /* sv_cheats (1.1.8: the test gun is a cheat) */
static int Cvar_VariableInteger( const char *name ) { assert( !strcmp( name, "sv_cheats" )); return cheats; }
static int slot_of( const sv_client_t *cl ) { return (int)( cl - clients ); }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static qboolean SV_CoF_TransitionActive( void ) { return transition; }
static void SV_CoF_RetailSupply( sv_client_t *cl ) { supplies[slot_of( cl )]++; infinite[slot_of( cl )] = 1; }
/* as the retail layer: what the switch was, or -1 (nothing written) */
static int SV_CoF_RetailInfiniteAmmo( sv_client_t *cl, qboolean on )
{
	int was = infinite[slot_of( cl )];
	if( !switch_ok ) return -1;
	infinite[slot_of( cl )] = on;
	return was;
}
static int SV_CoF_RetailCarrying( sv_client_t *cl, const char *classname )
{
	Q_strncpy( last_asked, classname, sizeof( last_asked ));
	return held[slot_of( cl )];
}
static qboolean SV_CoF_RetailInWheelchair( sv_client_t *cl ) { return wheelchair; }
static qboolean SV_CoF_RetailInPhoneCall( sv_client_t *cl ) { return phone; }
static qboolean SV_CoF_RecoveryPending( sv_client_t *cl ) { return pending; }
static int SV_CoF_RetailFreeSlots( sv_client_t *cl ) { return free_slots; }
static qboolean SV_CoF_StoryGiveItem( sv_client_t *cl, const char *classname )
{
	gives++; Q_strncpy( last_give, classname, sizeof( last_give ));
	if( give_ok ) held[slot_of( cl )] = 1; /* in the inventory, not yet in hand */
	return give_ok;
}
static qboolean SV_CoF_RetailEquip( sv_client_t *cl, const char *classname ) { equips++; Q_strncpy( last_equip, classname, sizeof( last_equip )); return true; }

#include "testgun_under_test.c"

static int logged( const char *text )
{
	int n = 0; const char *p = log_text;
	while(( p = strstr( p, text ))) { n++; p += strlen( text ); }
	return n;
}

static void person( int slot, int userid )
{
	clients[slot].state = cs_spawned; clients[slot].userid = userid; clients[slot].flags = 0;
	clients[slot].edict = &entities[slot + 1]; clients[slot].pViewEntity = NULL;
	entities[slot + 1].free = 0;
	entities[slot + 1].v.health = 100; entities[slot + 1].v.deadflag = DEAD_NO;
	entities[slot + 1].v.movetype = MOVETYPE_WALK; entities[slot + 1].v.flags = 0;
}

static void setup( const char *gun )
{
	memset( entities, 0, sizeof( entities )); memset( clients, 0, sizeof( clients ));
	memset( log_text, 0, sizeof( log_text )); log_length = 0;
	memset( infinite, 0, sizeof( infinite )); memset( held, 0, sizeof( held )); memset( supplies, 0, sizeof( supplies ));
	transition = wheelchair = phone = pending = gives = equips = 0; free_slots = 4; give_ok = 1; switch_ok = 1; cheats = 1;
	last_give[0] = last_equip[0] = last_asked[0] = 0;
	sv.time = 100.0; svs.maxclients = MAX_CLIENTS; svs.spawncount = 1; svs.clients = clients;
	cvar.string = gun;
	SV_CoF_TestGunReset();
	SV_CoF_TestGunInit();
	person( 0, 2 );
}

/* server frames (a quarter second apart, exact in binary) up to this many seconds later */
static void run( double seconds )
{
	double end = sv.time + seconds;
	while( sv.time + 0.25 <= end ) { sv.time += 0.25; SV_CoF_TestGunFrame(); }
}

int main( void )
{
	/* off by default: nothing is touched */
	setup( "" );
	run( 10.0 ); assert( !supplies[0] && !gives && !infinite[0] && !log_length );
	setup( "0" ); run( 3.0 ); assert( !supplies[0] && !gives && !log_length );
	setup( "none" ); run( 3.0 ); assert( !supplies[0] && !gives && !log_length ); /* off, not a gun refused */

	/* on: infinite ammo at once, the gun after two seconds standing free, then taken in hand */
	setup( "weapon_m16" );
	run( 0.25 ); assert( infinite[0] && supplies[0] == 1 && !gives );
	run( 1.75 ); assert( !gives ); /* still settling */
	run( 0.25 ); assert( gives == 1 && !strcmp( last_give, "weapon_m16" ) && equips == 1 && !strcmp( last_equip, "weapon_m16" ));
	assert( logged( "cofstory phase=test-gun-given userid=2 class=weapon_m16" ) == 1 );
	/* carried but not in hand: taken in hand again for a few seconds (a switch is refused mid-shot) */
	run( 1.0 ); assert( equips == 3 && gives == 1 );
	held[0] = 2; run( 1.0 ); assert( equips == 3 ); /* in hand: done */
	held[0] = 1; run( 5.0 ); assert( equips == 3 && gives == 1 ); /* the person put it away: their choice */
	/* the switch is kept on, every half second */
	infinite[0] = 0; run( 0.5 ); assert( infinite[0] );
	/* a short or capitalised name works too; it asks the retail layer for the main classname */
	setup( "shotgun" ); run( 3.0 ); assert( gives == 1 && !strcmp( last_give, "weapon_shotgun" ) && !strcmp( last_asked, "weapon_shotgun" ));
	setup( "Weapon_Glock" ); run( 3.0 ); assert( gives == 1 && !strcmp( last_give, "weapon_glock" ));
	/* not a gun we hand out (the TMP is a donator's, the P345 the story's, a key no gun): refused once, nothing done */
	setup( "weapon_tmp" ); run( 5.0 ); assert( !gives && !supplies[0] && logged( "cofstory phase=test-gun-refused class=weapon_tmp" ) == 1 );
	setup( "p345" ); run( 3.0 ); assert( !gives && logged( "test-gun-refused class=p345" ) == 1 );
	setup( "item_key" ); run( 3.0 ); assert( !gives && logged( "test-gun-refused" ) == 1 );
	setup( "weapon_supercalifragilisticexpialidocious_gun" ); run( 3.0 ); assert( logged( "test-gun-refused" ) == 1 );

	/* 1.1.8 (L): only with sv_cheats on: asked for without it, refused once and nothing done; sv_cheats going to 0
	 * turns its infinite ammo off as switching it off does */
	setup( "weapon_m16" ); cheats = 0; run( 5.0 );
	assert( !gives && !supplies[0] && !infinite[0] && logged( "cofstory phase=test-gun-refused reason=sv_cheats" ) == 1 );
	cheats = 1; run( 3.0 ); assert( gives == 1 && infinite[0] );
	cheats = 0; run( 3.0 ); assert( !infinite[0] && logged( "test-gun-off userid=2" ) == 1 && logged( "reason=sv_cheats" ) == 2 );
	cvar.string = ""; run( 1.0 ); cvar.string = "weapon_m16"; run( 1.0 ); assert( logged( "reason=sv_cheats" ) == 3 ); /* asked again */

	/* switched off: the game's switch goes back off once, with one line */
	setup( "weapon_m16" ); run( 3.0 ); assert( infinite[0] );
	cvar.string = ""; run( 3.0 ); assert( !infinite[0] && logged( "cofstory phase=test-gun-off userid=2" ) == 1 );
	infinite[0] = 1; run( 3.0 ); assert( infinite[0] ); /* somebody else's switch now (cof_infammo): left alone */
	/* a switch that was on before (the game's own unlock) is not ours to turn off */
	setup( "weapon_m16" ); infinite[0] = 1; run( 3.0 ); cvar.string = ""; run( 3.0 );
	assert( infinite[0] && !logged( "test-gun-off" ));
	/* a level change keeps the record while the client loads: off on the next map still turns it off */
	setup( "weapon_m16" ); run( 1.0 ); clients[0].state = 2; run( 1.0 );
	clients[0].state = cs_spawned; svs.spawncount++; sv.time = 1.0; cvar.string = ""; run( 1.0 );
	assert( !infinite[0] && logged( "test-gun-off userid=2" ) == 1 );
	/* refused while on: off as well */
	setup( "weapon_m16" ); run( 1.0 ); cvar.string = "weapon_uzi"; run( 1.0 ); assert( !infinite[0] && logged( "test-gun-off" ) == 1 );

	/* dead: nothing; after the respawn, settle again */
	setup( "weapon_m16" ); entities[1].v.health = 0; entities[1].v.deadflag = DEAD_DEAD;
	run( 5.0 ); assert( !supplies[0] && !gives );
	entities[1].v.health = 30; entities[1].v.deadflag = DEAD_NO;
	run( 1.0 ); assert( supplies[0] && !gives ); run( 1.5 ); assert( gives == 1 );
	/* the respawn's inventory is still owed: never a second gun on top of it */
	setup( "weapon_m16" ); pending = 1; run( 10.0 ); assert( !gives && infinite[0] );
	assert( logged( "cofstory phase=test-gun-wait userid=2 reason=recovery" ) == 1 ); /* said once, not every tick */
	pending = 0; held[0] = 1; run( 3.0 ); assert( !gives ); /* the recovery brought it back */

	/* never while frozen, flying on a ladder, in a camera or in the wheelchair; the clock restarts after */
	setup( "weapon_m16" ); entities[1].v.flags = FL_FROZEN; run( 5.0 ); assert( !gives );
	entities[1].v.flags = 0; run( 1.0 ); assert( !gives ); run( 1.5 ); assert( gives == 1 );
	setup( "weapon_m16" ); entities[1].v.movetype = MOVETYPE_FLY; run( 5.0 ); assert( !gives );
	setup( "weapon_m16" ); clients[0].pViewEntity = &entities[3]; run( 5.0 ); assert( !gives );
	clients[0].pViewEntity = &entities[1]; run( 2.5 ); assert( gives == 1 ); /* viewing through its own eyes is no camera */
	setup( "weapon_m16" ); wheelchair = 1; run( 5.0 ); assert( !gives && infinite[0] );
	/* nor on a phone call, nor in a player animation state (CPR, struggle, syringe, fall) */
	setup( "weapon_m16" ); phone = 1; run( 5.0 ); assert( !gives );
	phone = 0; entities[1].v.fuser3 = 2; run( 5.0 ); assert( !gives );
	entities[1].v.fuser3 = 10; run( 5.0 ); assert( !gives );
	entities[1].v.fuser3 = 0; run( 2.5 ); assert( gives == 1 );
	/* nothing at all during a level change */
	setup( "weapon_m16" ); transition = 1; run( 5.0 ); assert( !gives && !supplies[0] );

	/* room for a key stays: a full inventory gets no gun, told once; a free slot later is enough */
	setup( "weapon_m16" ); free_slots = 1; run( 20.0 );
	assert( !gives && logged( "cofstory phase=test-gun-full userid=2 class=weapon_m16" ) == 1 );
	free_slots = 2; run( 5.0 ); assert( gives == 1 );
	/* a refused gift is tried again every five seconds, not every frame */
	setup( "weapon_m16" ); give_ok = 0; run( 12.25 ); assert( gives == 3 );

	/* people only: a companion bot keeps its own kit */
	setup( "weapon_m16" ); clients[0].flags = FCL_FAKECLIENT; run( 5.0 ); assert( !gives && !supplies[0] );
	/* every person gets one */
	setup( "weapon_m16" ); person( 1, 3 ); person( 2, 4 ); clients[2].flags = FCL_FAKECLIENT;
	run( 3.0 ); assert( gives == 2 && supplies[0] && supplies[1] && !supplies[2] );
	assert( logged( "test-gun-given userid=2" ) == 1 && logged( "test-gun-given userid=3" ) == 1 );

	/* a new map: everyone settles again before a gift */
	setup( "weapon_m16" ); run( 3.0 ); assert( gives == 1 );
	held[0] = 0; svs.spawncount++; run( 1.0 ); assert( gives == 1 ); run( 1.5 ); assert( gives == 2 );
	/* the server clock starts again at every map: the module carries on at once, not after the old map's time */
	setup( "weapon_m16" ); run( 60.0 ); assert( gives == 1 );
	held[0] = 0; infinite[0] = 0; svs.spawncount++; sv.time = 1.0;
	run( 0.5 ); assert( infinite[0] && gives == 1 ); /* the switch at once; the gift after settling */
	run( 2.0 ); assert( gives == 2 );
	/* a second loss on the same map waits its own two seconds */
	setup( "weapon_m16" ); run( 3.0 ); assert( gives == 1 ); run( 5.0 );
	held[0] = 0; run( 1.75 ); assert( gives == 1 ); run( 0.5 ); assert( gives == 2 );
	/* someone else in the slot: their own clock, and the switch the last one had is not theirs to clear */
	setup( "weapon_m16" ); run( 1.0 ); person( 0, 9 ); cvar.string = ""; run( 1.0 );
	assert( !logged( "test-gun-off" ));

	printf( "test gun scenarios passed\n" );
	return 0;
}
