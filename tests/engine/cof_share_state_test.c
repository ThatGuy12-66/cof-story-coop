/* The story item ledger's rules, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_share_state.h"

#define KEY "inventoryitems/hallkey.txt"
#define LEFT "inventoryitems/leftplate.txt"
#define RIGHT "inventoryitems/rightplate.txt"
#define PLATE "inventoryitems/plate.txt"
#define PHONE "inventoryitems/weapons/weapon_mobile.txt"
#define TACLIGHT "inventoryitems/taclight.txt"
#define FUSE "inventoryitems/fuse.txt"

static cof_share_state_t s;
static char inv[2][COF_SHARE_SLOTS][COF_SHARE_NAME];
static int alive[2];

static void put( int p, int slot, const char *d ) { strcpy( inv[p][slot], d ); }
static void clear( int p, const char *d )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
		if( !strcmp( inv[p][i], d )) inv[p][i][0] = 0;
}
static void look( void )
{
	CoF_ShareObserve( &s, 0, 101, alive[0], inv[0] );
	CoF_ShareObserve( &s, 1, 102, alive[1], inv[1] );
}
static int has_event( int kind, int slot, const char *d )
{
	int i;
	for( i = 0; i < s.event_count; i++ )
		if( s.events[i].kind == kind && s.events[i].slot == slot && !strcmp( s.events[i].name, d )) return 1;
	return 0;
}
static int plan( int p, int free_slots, double now, char *out ) { return CoF_SharePlan( &s, p, free_slots, now, out ); }
static void start( void )
{
	CoF_ShareReset( &s );
	memset( inv, 0, sizeof( inv ));
	alive[0] = alive[1] = 1;
	look();
	s.event_count = 0;
}

int main( void )
{
	char item[COF_SHARE_NAME];

	/* A pickup joins the ledger, remembers its finder, and is planned for the partner. */
	start(); put( 0, 0, KEY ); look();
	assert( has_event( COF_SHARE_FOUND, 0, KEY ) && s.party.count == 1 && CoF_ShareFinder( &s, KEY ) == 101 );
	assert( plan( 1, 6, 10, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ));
	assert( plan( 0, 5, 10, item ) == COF_SHARE_NONE );

	/* The delivery lands: delivered, not found again; nothing more to do. */
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 10 );
	assert( plan( 1, 6, 10.5, item ) == COF_SHARE_NONE ); /* waiting for the inventory */
	s.event_count = 0; put( 1, 0, KEY ); look();
	assert( has_event( COF_SHARE_DELIVERED, 1, KEY ) && !has_event( COF_SHARE_FOUND, 1, KEY ));
	assert( plan( 1, 5, 11, item ) == COF_SHARE_NONE );

	/* Used up by the finder's game command: the partner's copy is taken. */
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventoryuse" ), inv[0] );
	assert( has_event( COF_SHARE_USED, 0, KEY ) && s.party.count == 0 );
	assert( plan( 1, 5, 12, item ) == COF_SHARE_TAKE && !strcmp( item, KEY ));
	CoF_ShareIssued( &s, 1, COF_SHARE_TAKE, KEY, 12 );
	clear( 1, KEY ); s.event_count = 0; look();
	assert( has_event( COF_SHARE_REMOVED, 1, KEY ) && !has_event( COF_SHARE_USED, 1, KEY ));

	/* Used outside any command (a map script such as cof_clearitems): same result. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look(); s.event_count = 0;
	clear( 1, KEY ); look();
	assert( has_event( COF_SHARE_USED, 1, KEY ) && s.party.count == 0 );
	assert( plan( 0, 5, 1, item ) == COF_SHARE_TAKE );

	/* A drop sets the item down: the party keeps it, the dropper is not re-given it. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look(); s.event_count = 0;
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventorydrop" ), inv[0] );
	assert( has_event( COF_SHARE_DROPPED, 0, KEY ) && s.party.count == 1 );
	assert( plan( 0, 6, 1, item ) == COF_SHARE_NONE && plan( 1, 5, 1, item ) == COF_SHARE_NONE );
	/* ...picked back up later: no new find, and it counts as theirs again. */
	s.event_count = 0; put( 0, 2, KEY ); look();
	assert( !has_event( COF_SHARE_FOUND, 0, KEY ) && s.players[0].set_down.count == 0 );

	/* A full inventory: told once to make room; delivered once a slot frees. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 0, 1, item ) == COF_SHARE_ROOM && !strcmp( item, KEY ));
	assert( plan( 1, 0, 2, item ) == COF_SHARE_NONE ); /* told only once */
	assert( plan( 1, -1, 3, item ) == COF_SHARE_NONE ); /* unknown room: no promise */
	assert( plan( 1, 1, 4, item ) == COF_SHARE_GIVE );

	/* An unconfirmed give backs off, then retries. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 1 );
	assert( plan( 1, 6, 2.5, item ) == COF_SHARE_NONE ); /* expired: backing off */
	assert( plan( 1, 6, 7.0, item ) == COF_SHARE_NONE );
	assert( plan( 1, 6, 8.0, item ) == COF_SHARE_GIVE );

	/* Review focus 1: a delivery that lands after its item was used up is taken again, never re-added. */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 1 );
	clear( 0, KEY ); CoF_ShareCommand( &s, 0, COF_SHARE_CMD_OTHER, inv[0] );
	s.event_count = 0; put( 1, 0, KEY ); look();
	assert( has_event( COF_SHARE_DELIVERED, 1, KEY ) && s.party.count == 0 );
	assert( plan( 1, 5, 1.2, item ) == COF_SHARE_TAKE );

	/* Review focus 4: death keeps items; the respawn wipe is not a use; everything comes back. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look();
	alive[1] = 0; look();
	assert( plan( 1, 5, 1, item ) == COF_SHARE_NONE ); /* nothing is handed to the dead */
	clear( 1, KEY ); alive[1] = 1; s.event_count = 0; look(); /* CBasePlayer::Spawn wiped it */
	assert( !has_event( COF_SHARE_USED, 1, KEY ) && s.party.count == 1 );
	assert( plan( 1, 6, 2, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ));
	/* ...and a respawn forgets what was set down. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look();
	clear( 1, KEY ); CoF_ShareCommand( &s, 1, COF_SHARE_CMD_DROP, inv[1] );
	alive[1] = 0; look(); alive[1] = 1; look();
	assert( plan( 1, 6, 2, item ) == COF_SHARE_GIVE );

	/* Combine: both halves used up and the result found; the partner follows. */
	start(); put( 0, 0, LEFT ); put( 0, 1, RIGHT ); put( 1, 0, LEFT ); put( 1, 1, RIGHT ); look();
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	clear( 0, LEFT ); clear( 0, RIGHT ); put( 0, 0, PLATE );
	CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventorycombine" ), inv[0] );
	assert( has_event( COF_SHARE_USED, 0, LEFT ) && has_event( COF_SHARE_USED, 0, RIGHT ) && has_event( COF_SHARE_FOUND, 0, PLATE ));
	assert( plan( 1, 4, 1, item ) == COF_SHARE_TAKE );

	/* Weapons, ammo and costumes are never shared. */
	start(); put( 0, 0, PHONE ); look();
	assert( s.party.count == 0 && plan( 1, 6, 1, item ) == COF_SHARE_NONE );

	/* An emptied client slot forgets its player; a newcomer is served. */
	start(); put( 0, 0, KEY ); look();
	CoF_ShareObserve( &s, 1, 0, 0, NULL );
	assert( s.players[1].userid == 0 );
	CoF_ShareObserve( &s, 1, 205, 1, inv[1] );
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE );

	/* Review fix: a combine that upgrades a weapon (taclight + Glock, battery + phone) leaves
	   no new story item. The input stays the partner's and is set down for the combiner. */
	start(); put( 0, 0, TACLIGHT ); put( 1, 0, TACLIGHT ); look();
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	clear( 0, TACLIGHT ); CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventorycombine" ), inv[0] );
	assert( !has_event( COF_SHARE_USED, 0, TACLIGHT ) && s.party.count == 1 );
	assert( plan( 1, 5, 1, item ) == COF_SHARE_NONE ); /* the partner keeps theirs */
	assert( plan( 0, 6, 1, item ) == COF_SHARE_NONE ); /* the combiner is not handed another */

	/* Review fix: an item that never lands backs off on its own; the next item still
	   arrives, and the stall is reported once. */
	start(); put( 0, 0, KEY ); put( 0, 1, FUSE ); look(); s.event_count = 0;
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ));
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 1 ); /* never lands */
	assert( plan( 1, 6, 2.5, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE )); /* not blocked */
	assert( has_event( COF_SHARE_STALLED, 1, KEY ));
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, FUSE, 2.5 ); put( 1, 0, FUSE ); look(); s.event_count = 0;
	assert( plan( 1, 5, 3.0, item ) == COF_SHARE_NONE ); /* KEY still backing off */
	assert( plan( 1, 5, 8.0, item ) == COF_SHARE_GIVE && !strcmp( item, KEY )); /* retried */
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 8.0 );
	assert( plan( 1, 5, 9.5, item ) == COF_SHARE_NONE && !has_event( COF_SHARE_STALLED, 1, KEY )); /* reported once */

	/* Review fix: a story-looking name the retail layer would refuse never enters the ledger. */
	start(); put( 0, 0, "inventoryitems/bad;name.txt" ); look();
	assert( s.party.count == 0 );

	/* The game dedupes only weapons (AddItemToInventory 100DAD40): a story item can sit in two slots (a give again
	 * after the confirm window, a world copy picked up). The second copy is taken; the item stays the party's. */
	start(); put( 0, 0, KEY ); put( 1, 0, KEY ); look(); put( 1, 3, KEY ); look(); s.event_count = 0;
	assert( plan( 1, 4, 1, item ) == COF_SHARE_TAKE && !strcmp( item, KEY ));
	CoF_ShareIssued( &s, 1, COF_SHARE_TAKE, KEY, 1 );
	inv[1][3][0] = 0; look(); /* the game clears the first match: one copy is left either way */
	assert( has_event( COF_SHARE_REMOVED, 1, KEY ) && !has_event( COF_SHARE_USED, 1, KEY ) && s.party.count == 1 );
	assert( plan( 1, 5, 2, item ) == COF_SHARE_NONE && !has_event( COF_SHARE_STALLED, 1, KEY ));

	/* A new session starts with an empty ledger. */
	CoF_ShareReset( &s );
	assert( s.party.count == 0 && !s.players[0].userid );
	puts( "share state scenarios passed" );
	return 0;
}
