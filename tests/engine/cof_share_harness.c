/* Offline doubles for sv_cof_share.c: game inventories as plain arrays. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define cs_free 0
#define cs_connected 2
#define cs_spawned 4
#define FCL_FAKECLIENT ( 1 << 7 )
#define DEAD_NO 0
#define FBitSet( a, b ) (( a ) & ( b ))
#define COF_RETAIL_SLOTS 12
#define COF_RETAIL_ITEM 64
#define Q_snprintf snprintf
#define Q_strcmp strcmp
#define Q_stricmp _stricmp
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
typedef struct { struct { float health; int deadflag; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef void ( *xcommand_t )( void );
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; } sv;
static sv_client_t clients[2];
static edict_t edicts[2];
static char inv[2][COF_RETAIL_SLOTS][COF_RETAIL_ITEM], last_line[2][192], console[32768], command_name[32];
static int three[2], gives, takes, refuse_give;
static int active = 6; /* the map's slot count (SV_CoF_InventorySlots) */
static const char *refuse_item; /* this descriptor is never taken */
static size_t console_length;
static xcommand_t ledger;

static int who( sv_client_t *cl ) { return (int)( cl - clients ); }
static qboolean SV_CoF_StoryCoopEnabled( void ) { return true; }
static const char *Cmd_Argv( int i ) { return i == 0 ? command_name : ""; }
static void Cmd_AddCommand( const char *name, xcommand_t f, const char *desc ) { ledger = f; }
static void Con_Printf( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	console_length += (size_t)vsnprintf( console + console_length, sizeof( console ) - console_length, fmt, args );
	va_end( args );
}
static qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	if( cl->state != cs_spawned ) return false;
	memcpy( slots, inv[who( cl )], sizeof( inv[0] ));
	return true;
}
static int SV_CoF_RetailFreeSlots( sv_client_t *cl )
{
	int i, n = 0, usable = three[who( cl )] ? 3 : active;
	for( i = 0; i < usable; i++ ) if( !inv[who( cl )][i][0] ) n++;
	return n;
}
/* the game's AddInventoryItem: first free usable slot, never a second copy */
static qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *d )
{
	int i, p = who( cl );
	gives++;
	if( refuse_give || ( refuse_item && !strcmp( d, refuse_item ))) return false;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) return true;
	for( i = 0; i < ( three[p] ? 3 : active ); i++ ) if( !inv[p][i][0] ) { strcpy( inv[p][i], d ); return true; }
	return false;
}
static qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *d )
{
	int i, p = who( cl );
	takes++;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) { inv[p][i][0] = 0; break; }
	return true;
}
static qboolean SV_CoF_RetailItemName( const char *d, char *out, size_t size )
{
	snprintf( out, size, "%s", strstr( d, "hallkey" ) ? "Hall key" : "fuse" );
	return true;
}
static qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	snprintf( last_line[who( cl )], sizeof( last_line[0] ), "%d:%s", colour, text );
	return true;
}

#include "sv_cof_descriptor.h"
#include "sv_cof_share_state.h"
void SV_CoF_ShareRecovered( sv_client_t *client );
void SV_CoF_ShareExport( cof_share_archive_t *archive );
#include "share_under_test.c"

#define KEY "inventoryitems/hallkey.txt"
#define GLOCK "inventoryitems/weapons/weapon_glock.txt"
#define TACLIGHT "inventoryitems/taclight.txt"
#define FUSE "inventoryitems/fuse.txt"

static void setup( void )
{
	int i;
	memset( clients, 0, sizeof( clients ));
	memset( inv, 0, sizeof( inv ));
	memset( three, 0, sizeof( three ));
	memset( last_line, 0, sizeof( last_line ));
	gives = takes = refuse_give = 0;
	refuse_item = NULL;
	console[0] = 0; console_length = 0;
	sv.time = 10;
	svs.maxclients = 2;
	svs.clients = clients;
	for( i = 0; i < 2; i++ )
	{
		clients[i].state = cs_spawned;
		clients[i].userid = i + 1;
		clients[i].edict = &edicts[i];
		snprintf( clients[i].name, sizeof( clients[i].name ), "Little Rascal %d", i + 1 );
		edicts[i].v.health = 100;
		edicts[i].v.deadflag = DEAD_NO;
	}
	SV_CoF_ShareReset();
	SV_CoF_ShareFrame();
}
static void frame( double dt ) { sv.time += dt; SV_CoF_ShareFrame(); }
static void begin( int p, const char *name ) { strcpy( command_name, name ); SV_CoF_ShareCommandBegin( &clients[p] ); }
static void end( int p ) { SV_CoF_ShareCommandEnd( &clients[p] ); }
static int holds( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) return 1; return 0; }
static void lose( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) inv[p][i][0] = 0; }
static void gain( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !inv[p][i][0] ) { strcpy( inv[p][i], d ); return; } }

int main( void )
{
	int i;

	/* A pickup is handed to the partner through the game, with a line naming the finder. */
	setup(); gain( 0, KEY ); frame( 0.1 );
	assert( holds( 1, KEY ) && gives == 1 );
	assert( !strcmp( last_line[1], "5:Little Rascal 1 found the Hall key" ) && !last_line[0][0] );
	assert( strstr( console, "cofstory phase=item-found userid=1 item=" KEY ));
	frame( 0.1 );
	assert( gives == 1 && strstr( console, "cofstory phase=item-delivered userid=2 item=" KEY ));

	/* Used at a lock (inventoryuse): the partner's copy goes through the game's removal. */
	begin( 0, "inventoryuse" ); lose( 0, KEY ); end( 0 ); frame( 0.1 );
	assert( !holds( 1, KEY ) && takes == 1 && strstr( console, "cofstory phase=item-used userid=1" ));
	frame( 0.1 );
	assert( strstr( console, "cofstory phase=item-removed userid=2" ) && takes == 1 );

	/* Dropped (inventorydrop): the partner keeps it; the dropper is not re-given it. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	begin( 0, "inventorydrop" ); lose( 0, KEY ); end( 0 ); frame( 0.1 ); frame( 2 );
	assert( holds( 1, KEY ) && !holds( 0, KEY ) && gives == 1 && takes == 0 );
	assert( strstr( console, "cofstory phase=item-dropped userid=1" ));

	/* Review focus 2: a pickup just before any command still counts as found. */
	setup(); gain( 0, KEY ); begin( 0, "say" ); end( 0 ); frame( 0.1 );
	assert( holds( 1, KEY ) && strstr( console, "cofstory phase=item-found userid=1" ));

	/* Full inventory: told once to make room; delivered when a slot frees. */
	setup();
	for( i = 0; i < 6; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && strstr( last_line[1], "5:Little Rascal 1 found the Hall key. Make room to carry it." ));
	assert( strstr( console, "cofstory phase=item-deferred userid=2 item=" KEY " reason=inventory-full" ));
	last_line[1][0] = 0; frame( 0.1 );
	assert( !last_line[1][0] );
	inv[1][3][0] = 0; frame( 0.1 );
	assert( holds( 1, KEY ));

	/* Three-slot mode counts only slots 1..3. */
	setup(); three[1] = 1;
	for( i = 0; i < 3; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && gives == 0 );

	/* Review focus 4: the respawn wipe is not a use, and the party's items come back. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	edicts[1].v.health = 0; frame( 0.1 );
	lose( 1, KEY ); edicts[1].v.health = 100; frame( 0.1 );
	assert( !strstr( console, "phase=item-used" ) && holds( 1, KEY ) && holds( 0, KEY ));

	/* Review focus 3: a partner mid level change (connected, not spawned) is served after sign-on. */
	setup(); clients[1].state = cs_connected; gain( 0, KEY ); frame( 0.1 );
	assert( !holds( 1, KEY ) && gives == 0 );
	clients[1].state = cs_spawned; frame( 0.1 );
	assert( holds( 1, KEY ));

	/* Disconnect, then a new player in the slot: they get the party's items. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	clients[1].state = cs_free; frame( 0.1 );
	clients[1].state = cs_spawned; clients[1].userid = 7; memset( inv[1], 0, sizeof( inv[1] )); frame( 0.1 );
	assert( holds( 1, KEY ));

	/* A refused give is logged, backs off, and retries. */
	setup(); refuse_give = 1; gain( 0, KEY ); frame( 0.1 );
	assert( gives == 1 && strstr( console, "cofstory phase=item-deferred userid=2 item=" KEY ));
	frame( 1.2 );
	assert( gives == 1 );
	frame( 5.5 );
	assert( gives == 2 );

	/* Fake clients and weapons are never involved. */
	setup(); clients[1].flags = FCL_FAKECLIENT; gain( 0, KEY ); frame( 0.1 );
	assert( gives == 0 );
	setup(); gain( 0, GLOCK ); frame( 0.1 );
	assert( gives == 0 && !strstr( console, "item-found" ));

	/* A new session empties the ledger: nothing is handed out afterwards. */
	setup(); gain( 0, KEY ); frame( 0.1 );
	SV_CoF_ShareReset(); memset( inv, 0, sizeof( inv )); frame( 0.1 ); frame( 0.1 );
	assert( gives == 1 && !holds( 0, KEY ) && !holds( 1, KEY ));

	/* The ledger command lists the party's items. */
	setup(); gain( 0, KEY ); frame( 0.1 ); SV_CoF_ShareInit(); ledger();
	assert( strstr( console, "cofstory phase=ledger-item item=" KEY " finder=1" ));
	/* Review fix: a weapon-upgrade combine is logged and leaves the partner's copy alone. */
	setup(); gain( 0, TACLIGHT ); frame( 0.1 ); frame( 0.1 );
	begin( 0, "inventorycombine" ); lose( 0, TACLIGHT ); end( 0 ); frame( 0.1 ); frame( 2 );
	assert( holds( 1, TACLIGHT ) && !holds( 0, TACLIGHT ) && takes == 0 );
	assert( strstr( console, "cofstory phase=item-combined userid=1 item=" TACLIGHT ));

	/* Review fix: a give refused just before a level change is retried promptly on
	   the next map, whose clock restarts at 1.0 (sv_init.c). */
	setup(); refuse_give = 1; sv.time = 1000; gain( 0, KEY ); frame( 0.1 );
	assert( gives == 1 );
	refuse_give = 0; svs.spawncount++; sv.time = 1.0; frame( 0.1 ); frame( 0.1 );
	assert( gives == 2 && holds( 1, KEY ));

	/* Review Focus 2, pinned: a pickup and an inventorydrop of it in one packet still
	   make the item the party's (found, then set down by the dropper). */
	setup(); gain( 0, KEY ); begin( 0, "inventorydrop" ); lose( 0, KEY ); end( 0 ); frame( 0.1 );
	assert( holds( 1, KEY ) && strstr( console, "phase=item-found userid=1" ) && strstr( console, "phase=item-dropped userid=1" ));

	/* Review Focus 3, pinned: a partner mid level change keeps its state. The finder
	   uses the key meanwhile; after sign-on the partner's copy is taken, and the key
	   is not "found" again and handed back to the finder. */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	clients[1].state = cs_connected; frame( 0.1 );
	begin( 0, "inventoryuse" ); lose( 0, KEY ); end( 0 ); frame( 0.1 );
	clients[1].state = cs_spawned; frame( 0.1 ); frame( 0.1 ); frame( 0.1 );
	assert( !holds( 1, KEY ) && !holds( 0, KEY ));

	/* Review fix: an item the game keeps refusing is reported once and does not hold
	   back the next one. */
	setup(); refuse_item = KEY; gain( 0, KEY ); gain( 0, FUSE ); frame( 0.1 ); frame( 1.2 ); frame( 0.1 ); frame( 0.1 );
	assert( holds( 1, FUSE ) && !holds( 1, KEY ));
	assert( strstr( console, "cofstory phase=item-stalled userid=2 item=" KEY ));
	frame( 6 ); frame( 1.2 ); frame( 6 ); frame( 1.2 );
	{ const char *first = strstr( console, "item-stalled" ); assert( first && !strstr( first + 1, "item-stalled" )); }

	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	begin( 1, "inventorydrop" ); lose( 1, KEY ); end( 1 ); frame( 0.1 );
	{
		cof_share_archive_t archive;
		strcpy( clients[1].name, "Partner" );
		SV_CoF_ShareExport( &archive );
		strcpy( clients[1].name, "pARTNER" );
		assert( SV_CoF_ShareImport( &archive ));
		frame( 0.1 ); frame( 2 );
		assert( holds( 0, KEY ) && !holds( 1, KEY ));
		archive.party.count = COF_SHARE_ITEMS + 1;
		assert( !SV_CoF_ShareImport( &archive ));
	}
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 );
	{
		cof_share_archive_t archive;
		gain( 0, GLOCK );
		gain( 0, "inventoryitems/weapons/weapon_mobile.txt" );
		gain( 1, "inventoryitems/ammo/ammo_9mm.txt" );
		gain( 1, "inventoryitems/outofit/costume.txt" );
		SV_CoF_ShareExport( &archive ); archive.party.count = 0;
		assert( SV_CoF_ShareImport( &archive ));
		frame( 0.1 ); frame( 2 );
		assert( !holds( 0, KEY ) && !holds( 1, KEY ));
		assert( !strcmp( inv[0][1], GLOCK ) );
		assert( !strcmp( inv[0][2], "inventoryitems/weapons/weapon_mobile.txt" ) );
		assert( !strcmp( inv[1][1], "inventoryitems/ammo/ammo_9mm.txt" ) );
		assert( !strcmp( inv[1][2], "inventoryitems/outofit/costume.txt" ) );
		SV_CoF_ShareRecovered( &clients[0] );
		assert( holds( 0, GLOCK ) && holds( 0, "inventoryitems/weapons/weapon_mobile.txt" ) );
	}
	/* twelve slots: a partner whose slots 1..6 are full still gets the shared key, in slot 7 */
	setup(); active = 12;
	for( i = 0; i < 6; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 ); frame( 1.0 );
	assert( !strcmp( inv[1][6], KEY ));
	active = 6;
	puts( "share scenarios passed" );
	return 0;
}
