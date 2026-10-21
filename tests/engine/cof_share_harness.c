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
#ifdef _WIN32
#define Q_stricmp _stricmp
#else
#include <strings.h>
#define Q_stricmp strcasecmp
#endif
static void Q_strncpy( char *target, const char *source, size_t size ) { snprintf( target, size, "%s", source ); }
#define FL_KILLME ( 1 << 30 )
typedef int string_t;
/* players are edicts[0..1]; the world's items (1.1.9, I2: a key a player dropped) are world[3..7] */
typedef struct edict_s { int free; struct { float health, armorvalue; int deadflag, flags; string_t classname, message; struct edict_s *owner; } v; } edict_t;
typedef struct { int state, userid; unsigned int flags; char name[32]; edict_t *edict; } sv_client_t;
typedef void ( *xcommand_t )( void );
static struct { int maxclients, spawncount; sv_client_t *clients; } svs;
static struct { double time; } sv;
static struct { int numEntities; } svgame;
static sv_client_t clients[2];
static edict_t edicts[2], world[8];
static const char *strings[] = { "", "item_key", "inventoryitems/fuse.txt", "inventoryitems/hallkey.txt" };
static char inv[2][COF_RETAIL_SLOTS][COF_RETAIL_ITEM], last_line[2][192], console[32768], command_name[32];
static int three[2], gives, takes, refuse_give, late_give;
static int active = 6; /* the map's slot count (SV_CoF_InventorySlots) */
static const char *refuse_item; /* this descriptor is never taken */
static const char *late[2]; /* a give that shows in the inventory only by the next frame (late_give) */
static size_t console_length;
static xcommand_t ledger;

static edict_t *SV_EdictNum( int index ) { return &world[index]; }
static qboolean SV_IsValidEdict( const edict_t *e ) { return e && !e->free; }
static const char *SV_GetString( string_t s ) { return strings[s]; }
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
/* the game's AddItemToInventory for an item_key: first free usable slot. Only weapon_* are deduplicated
   (player.cpp:11264-11276), so a story item already held lands in a second slot. */
static qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *d )
{
	int i, p = who( cl );
	gives++;
	if( refuse_give || ( refuse_item && !strcmp( d, refuse_item ))) return false;
	if( late_give ) { late[p] = d; return true; }
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
	memset( world, 0, sizeof( world ));
	for( i = 0; i < 8; i++ ) world[i].free = 1;
	svgame.numEntities = 3;
	gives = takes = refuse_give = late_give = 0;
	late[0] = late[1] = NULL;
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
static void gain( int p, const char *d );
static void frame( double dt )
{
	int p;
	for( p = 0; p < 2; p++ ) if( late[p] ) { gain( p, late[p] ); late[p] = NULL; } /* a late give shows now */
	sv.time += dt; SV_CoF_ShareFrame();
}
static void begin( int p, const char *name ) { strcpy( command_name, name ); SV_CoF_ShareCommandBegin( &clients[p] ); }
static void end( int p ) { SV_CoF_ShareCommandEnd( &clients[p] ); }
static int holds( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) return 1; return 0; }
static void lose( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) inv[p][i][0] = 0; }
static void gain( int p, const char *d ) { int i; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !inv[p][i][0] ) { strcpy( inv[p][i], d ); return; } }
static int copies( int p, const char *d ) { int i, n = 0; for( i = 0; i < COF_RETAIL_SLOTS; i++ ) n += !strcmp( inv[p][i], d ); return n; }
static void use_one( int p, const char *d ) /* a fuse box (func_button, ClearOutSlot): one slot */
{
	int i;
	begin( p, "inventoryuse" );
	for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) { inv[p][i][0] = 0; break; }
	end( p );
}

/* 1.1.9 (I2): the game's inventorydrop of a key item: the slot cleared, an item_key left owned by the dropper (armorvalue
   666, message the descriptor); returns its world index. Taking it back zeroes armorvalue and removes it. */
static int drop_key( int p, const char *d )
{
	int i, k;
	begin( p, "inventorydrop" );
	for( i = 0; i < COF_RETAIL_SLOTS; i++ ) if( !strcmp( inv[p][i], d )) { inv[p][i][0] = 0; break; }
	end( p );
	for( k = 3; k < 8 && !world[k].free; k++ );
	assert( k < 8 );
	memset( &world[k], 0, sizeof( world[k] ));
	world[k].v.classname = 1; world[k].v.message = !strcmp( d, "inventoryitems/fuse.txt" ) ? 2 : 3;
	world[k].v.owner = clients[p].edict; world[k].v.armorvalue = 666.0f;
	if( k >= svgame.numEntities ) svgame.numEntities = k + 1;
	return k;
}
static void take_key( int p, int k )
{
	world[k].v.armorvalue = 0.0f; world[k].free = 1;
	gain( p, strings[world[k].v.message] );
}

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
	/* 1.1.8 (M1 finding 1): c_subway2st3's gate needs two fuse boxes filled, and both fuses come from the two
	   c_subway2st1 pulls. Each pull is a find, everyone carries two, each box takes one copy, none is left. */
	setup(); gain( 0, FUSE ); frame( 0.1 ); frame( 0.1 );
	assert( copies( 1, FUSE ) == 1 );
	gain( 1, FUSE ); frame( 0.1 ); frame( 0.1 ); /* the partner pulls the second while holding the first's copy */
	assert( copies( 0, FUSE ) == 2 && copies( 1, FUSE ) == 2 && takes == 0 );
	assert( strstr( console, "cofstory phase=item-found userid=2 item=" FUSE ) && !strcmp( last_line[0], "5:Little Rascal 2 found the fuse" ));
	use_one( 0, FUSE ); frame( 0.1 ); frame( 0.1 );
	assert( copies( 0, FUSE ) == 1 && copies( 1, FUSE ) == 1 && takes == 1 );
	assert( strstr( console, "cofstory phase=item-used userid=1 item=" FUSE ) && strstr( console, "cofstory phase=item-removed userid=2 item=" FUSE ));
	use_one( 1, FUSE ); frame( 0.1 ); frame( 0.1 ); frame( 2 );
	assert( copies( 0, FUSE ) == 0 && copies( 1, FUSE ) == 0 && takes == 2 );
	/* ...a key (one copy) still loses a second copy at once */
	setup(); gain( 0, KEY ); frame( 0.1 ); frame( 0.1 ); gain( 1, KEY ); frame( 0.1 ); frame( 0.1 );
	assert( copies( 1, KEY ) == 1 && takes == 1 );

	/* Continue keeps both copies: after the checkpoint loads, a returning player and a brand-new joiner are
	   each brought to two fuses. */
	setup(); gain( 0, FUSE ); gain( 0, FUSE ); for( i = 0; i < 6; i++ ) frame( 0.1 );
	assert( copies( 1, FUSE ) == 2 );
	{
		cof_share_archive_t archive;
		size_t mark;
		SV_CoF_ShareExport( &archive );
		SV_CoF_ShareReset(); memset( inv, 0, sizeof( inv ));
		assert( SV_CoF_ShareImport( &archive ));
		clients[1].userid = 9; strcpy( clients[1].name, "Newcomer" );
		mark = console_length;
		for( i = 0; i < 6; i++ ) frame( 0.1 );
		assert( copies( 0, FUSE ) == 2 && copies( 1, FUSE ) == 2 && !strstr( console + mark, "item-found" ));
	}

	/* 1.1.8: a fuse handed over in the old map's last frame reaches the new map in the carried inventory: it is the
	   delivery, not one more fuse for the party (the partner signs on a few frames later, the clock restarted).
	   1.1.9 (I1): a give the inventory shows at once is settled in its own frame; this one shows only later. */
	setup(); late_give = 1; gain( 0, FUSE ); frame( 0.1 );
	assert( gives == 1 && cof_share.players[1].pending == COF_SHARE_GIVE );
	late_give = 0;
	clients[0].state = clients[1].state = cs_connected; frame( 0.1 ); /* it shows in the inventory the change carries */
	assert( copies( 1, FUSE ) == 1 && gives == 1 );
	svs.spawncount++; sv.time = 1.0; frame( 0.1 );
	clients[0].state = clients[1].state = cs_spawned;
	{
		size_t mark = console_length;
		for( i = 0; i < 4; i++ ) frame( 0.1 );
		assert( !strstr( console + mark, "item-found" ) && strstr( console + mark, "item-delivered userid=2 item=" FUSE ));
		assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 1 && copies( 0, FUSE ) == 1 && copies( 1, FUSE ) == 1 && gives == 1 );
	}

	/* 1.1.8: a rejoiner served while the level change held their restore back (the recovery waits, the ledger does not):
	   the restore replaces the inventory, so that give is no longer in flight. The fuse they pull next is a new one. */
	setup(); gain( 0, FUSE ); frame( 0.1 ); frame( 0.1 );
	clients[1].state = cs_free; frame( 0.1 );
	clients[1].state = cs_spawned; clients[1].userid = 8; memset( inv[1], 0, sizeof( inv[1] ));
	{
		cof_share_archive_t archive;
		SV_CoF_ShareExport( &archive ); /* the leaver's name, as the recovery's capture remembers it */
		strcpy( archive.names[1], clients[1].name );
		cof_share_saved = archive;
	}
	late_give = 1; frame( 0.1 ); late_give = 0; /* served from the empty inventory: a give in flight (1.1.9: not yet shown) */
	assert( gives == 2 && cof_share.players[1].pending == COF_SHARE_GIVE );
	memset( inv[1], 0, sizeof( inv[1] )); strcpy( inv[1][0], FUSE ); /* the recovery's restore: what they left with */
	late[1] = NULL; /* ...in place of the inventory that give went into */
	SV_CoF_ShareRecovered( &clients[1] );
	gain( 1, FUSE ); frame( 0.1 ); frame( 0.1 ); frame( 0.1 );
	assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 1, FUSE ) == 2 && copies( 0, FUSE ) == 2 );
	assert( strstr( console, "cofstory phase=item-found userid=8 item=" FUSE ) && !strstr( console, "item-stalled" ));

	/* twelve slots: a partner whose slots 1..6 are full still gets the shared key, in slot 7 */
	setup(); active = 12;
	for( i = 0; i < 6; i++ ) strcpy( inv[1][i], GLOCK );
	gain( 0, KEY ); frame( 0.1 ); frame( 1.0 );
	assert( !strcmp( inv[1][6], KEY ));
	active = 6;

	/* 1.1.9 (I1): the give and the take are settled by a look right after them. A partner whose second fuse copy is
	   taken (the finder used one) and who picks a fuse up before the next look keeps it: it is one more for the party.
	   Left to the next look, the take read as stalled (no net change) and, retried 5 s later, removed the fuse found. */
	setup(); gain( 0, FUSE ); gain( 0, FUSE ); for( i = 0; i < 6; i++ ) frame( 0.1 );
	assert( copies( 1, FUSE ) == 2 && CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 );
	use_one( 0, FUSE ); frame( 0.1 ); /* the partner's second copy is taken in this frame */
	assert( copies( 1, FUSE ) == 1 && takes == 1 && cof_share.players[1].pending == COF_SHARE_NONE );
	gain( 1, FUSE ); /* a pickup before the next look */
	for( i = 0; i < 80; i++ ) frame( 0.1 );
	assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 1, FUSE ) == 2 && copies( 0, FUSE ) == 2 && takes == 1 );
	assert( strstr( console, "cofstory phase=item-removed userid=2 item=" FUSE ) && strstr( console, "cofstory phase=item-found userid=2 item=" FUSE ));
	assert( !strstr( console, "item-stalled" ));
	/* ...and a give the game takes is settled the same way: delivered in its own frame, never stalled */
	setup(); gain( 0, KEY ); frame( 0.1 );
	assert( holds( 1, KEY ) && cof_share.players[1].pending == COF_SHARE_NONE );

	/* 1.1.9 (I2): a fuse set down (inventorydrop leaves an item_key owned by the dropper) and another one gained while
	   it still lies there (a c_subway2st1 train box): one more for the party, not that one picked back up. Picking
	   the dropped one up later is not a new find. */
	setup(); gain( 0, FUSE ); for( i = 0; i < 4; i++ ) frame( 0.1 );
	assert( copies( 1, FUSE ) == 1 && CoF_ShareSetCount( &cof_share.party, FUSE ) == 1 );
	{
		int dropped = drop_key( 0, FUSE ), mark;
		frame( 0.1 );
		assert( strstr( console, "cofstory phase=item-dropped userid=1 item=" FUSE ) && copies( 0, FUSE ) == 0 );
		gain( 0, FUSE ); for( i = 0; i < 4; i++ ) frame( 0.1 ); /* the train box's fuse */
		assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 1, FUSE ) == 2 && copies( 0, FUSE ) == 1 );
		assert( cof_share.players[0].set_down.count == 1 ); /* the dropped one is still theirs to pick up */
		mark = (int)console_length;
		take_key( 0, dropped ); for( i = 0; i < 4; i++ ) frame( 0.1 );
		assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 0, FUSE ) == 2 && copies( 1, FUSE ) == 2 );
		assert( !strstr( console + mark, "item-found" ) && cof_share.players[0].set_down.count == 0 && takes == 0 );
	}
	/* ...the copy set down picked up by the partner instead: their find, and the dropper's next fuse reads as theirs
	   back (as before 1.1.9); the two cancel out, everyone ends with the party's two */
	setup(); gain( 0, FUSE ); for( i = 0; i < 4; i++ ) frame( 0.1 );
	{
		int dropped = drop_key( 0, FUSE );
		frame( 0.1 );
		take_key( 1, dropped ); for( i = 0; i < 4; i++ ) frame( 0.1 );
		assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 1, FUSE ) == 2 && copies( 0, FUSE ) == 0 );
		gain( 0, FUSE ); for( i = 0; i < 4; i++ ) frame( 0.1 );
		assert( CoF_ShareSetCount( &cof_share.party, FUSE ) == 2 && copies( 0, FUSE ) == 2 && copies( 1, FUSE ) == 2 );
		assert( cof_share.players[0].set_down.count == 0 && takes == 0 );
	}
	puts( "share scenarios passed" );
	return 0;
}
