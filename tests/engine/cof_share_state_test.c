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
#define LADDER "inventoryitems/ladder1.txt"
#define SPADE "inventoryitems/spade.txt"

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
static int copies( int p, const char *d ) { return CoF_ShareCount( inv[p], d ); }
static void deliver( int p, int slot, const char *d, double now ) /* our give, landing */
{
	CoF_ShareIssued( &s, p, COF_SHARE_GIVE, d, now );
	put( p, slot, d );
	look();
}
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

	/* 1.1.8 (M1 finding 1): fuses come from several places and each fuse box uses one up; c_subway2st1 and
	 * c_subway2st3 need two boxes filled with no fuse to be had in between, so one player alone carries two.
	 * The party keeps both: a second fuse is found (not taken), everyone is brought to two. */
	start(); put( 0, 0, FUSE ); look();
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE ));
	deliver( 1, 0, FUSE, 1 );
	s.event_count = 0; put( 0, 1, FUSE ); look(); /* the second box's pull (c_subway2st1 givefuseffs) */
	assert( has_event( COF_SHARE_FOUND, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 && CoF_ShareFinder( &s, FUSE ) == 101 );
	assert( plan( 0, 4, 2, item ) == COF_SHARE_NONE ); /* not taken: the party has two */
	assert( plan( 1, 5, 2, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE ));
	s.event_count = 0; deliver( 1, 1, FUSE, 2 );
	assert( has_event( COF_SHARE_DELIVERED, 1, FUSE ) && !has_event( COF_SHARE_FOUND, 1, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	assert( plan( 1, 4, 3, item ) == COF_SHARE_NONE );
	/* ...the first box (inventoryuse) takes one copy: the party has one left, the partner's second copy goes */
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	inv[0][1][0] = 0; CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventoryuse" ), inv[0] );
	assert( has_event( COF_SHARE_USED, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 1 );
	assert( plan( 0, 5, 4, item ) == COF_SHARE_NONE );
	assert( plan( 1, 4, 4, item ) == COF_SHARE_TAKE && !strcmp( item, FUSE ));
	CoF_ShareIssued( &s, 1, COF_SHARE_TAKE, FUSE, 4 );
	inv[1][0][0] = 0; s.event_count = 0; look(); /* the game clears the first match */
	assert( has_event( COF_SHARE_REMOVED, 1, FUSE ) && !has_event( COF_SHARE_USED, 1, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 1 );
	assert( copies( 0, FUSE ) == 1 && copies( 1, FUSE ) == 1 && plan( 1, 5, 5, item ) == COF_SHARE_NONE );
	/* ...the second box, by the partner: the last copy is used and the finder's taken */
	CoF_ShareObserve( &s, 1, 102, 1, inv[1] ); s.event_count = 0;
	clear( 1, FUSE ); CoF_ShareCommand( &s, 1, CoF_ShareCommandKind( "inventoryuse" ), inv[1] );
	assert( has_event( COF_SHARE_USED, 1, FUSE ) && s.party.count == 0 );
	assert( plan( 0, 5, 6, item ) == COF_SHARE_TAKE && !strcmp( item, FUSE ));

	/* Both boxes filled before any take lands: two uses, two copies gone, nothing left over. */
	start(); put( 0, 0, FUSE ); put( 0, 1, FUSE ); look(); deliver( 1, 0, FUSE, 1 ); deliver( 1, 1, FUSE, 1 );
	assert( CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); inv[0][0][0] = 0; CoF_ShareCommand( &s, 0, COF_SHARE_CMD_OTHER, inv[0] );
	CoF_ShareObserve( &s, 1, 102, 1, inv[1] ); inv[1][1][0] = 0; CoF_ShareCommand( &s, 1, COF_SHARE_CMD_OTHER, inv[1] );
	assert( s.party.count == 0 && plan( 0, 5, 2, item ) == COF_SHARE_TAKE && plan( 1, 5, 2, item ) == COF_SHARE_TAKE );

	/* A pull that lands in the same look as our give: one delivered, one found. */
	start(); put( 0, 0, FUSE ); look();
	CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, FUSE, 1 ); put( 1, 0, FUSE ); put( 1, 1, FUSE ); s.event_count = 0; look();
	assert( has_event( COF_SHARE_DELIVERED, 1, FUSE ) && has_event( COF_SHARE_FOUND, 1, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	assert( plan( 0, 5, 2, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE ));

	/* A partner without their copy yet who finds one: a find, never mistaken for the copy they lack. */
	start(); put( 0, 0, FUSE ); look(); put( 1, 0, FUSE ); look();
	assert( CoF_ShareSetCount( &s.party, FUSE ) == 2 );

	/* Dropping one of two sets it down: the party keeps two, the dropper is not handed another, and picking
	 * it back up is not a new find. A respawn wipe is not a use. */
	start(); put( 0, 0, FUSE ); put( 0, 1, FUSE ); look(); deliver( 1, 0, FUSE, 1 ); deliver( 1, 1, FUSE, 1 );
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	inv[0][1][0] = 0; CoF_ShareCommand( &s, 0, COF_SHARE_CMD_DROP, inv[0] );
	assert( has_event( COF_SHARE_DROPPED, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	assert( plan( 0, 5, 2, item ) == COF_SHARE_NONE && plan( 1, 4, 2, item ) == COF_SHARE_NONE );
	s.event_count = 0; put( 0, 1, FUSE ); look();
	assert( !has_event( COF_SHARE_FOUND, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 && s.players[0].set_down.count == 0 );
	alive[1] = 0; look(); memset( inv[1], 0, sizeof( inv[1] )); alive[1] = 1; look();
	assert( CoF_ShareSetCount( &s.party, FUSE ) == 2 && plan( 1, 6, 3, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE ));
	deliver( 1, 0, FUSE, 3 );
	assert( plan( 1, 5, 4, item ) == COF_SHARE_GIVE ); deliver( 1, 1, FUSE, 4 );
	assert( plan( 1, 4, 5, item ) == COF_SHARE_NONE );

	/* 1.1.9 (I2): a fuse gained while the copy set down still lies in the map (the glue saw it: lying) is one more for
	 * the party, and the copy stays set down; once it lies there no more, the next gain is that copy picked back up. */
	start(); put( 0, 0, FUSE ); look(); deliver( 1, 0, FUSE, 1 );
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); s.event_count = 0;
	inv[0][0][0] = 0; CoF_ShareCommand( &s, 0, COF_SHARE_CMD_DROP, inv[0] );
	assert( has_event( COF_SHARE_DROPPED, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 1 );
	s.players[0].lying = CoF_ShareCountedBit( FUSE ); s.event_count = 0; put( 0, 1, FUSE ); look();
	assert( has_event( COF_SHARE_FOUND, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 && s.players[0].set_down.count == 1 );
	assert( plan( 0, 5, 2, item ) == COF_SHARE_NONE && plan( 1, 5, 2, item ) == COF_SHARE_GIVE && !strcmp( item, FUSE ));
	s.players[0].lying = 0; s.event_count = 0; put( 0, 0, FUSE ); look(); /* the dropped one, picked back up */
	assert( !has_event( COF_SHARE_FOUND, 0, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 && s.players[0].set_down.count == 0 );
	/* ...a ladder's bit is its own: a fuse lying does not make a ladder gain a new one */
	assert( CoF_ShareCountedBit( FUSE ) != CoF_ShareCountedBit( LADDER ) && CoF_ShareCountedBit( LADDER ) && !CoF_ShareCountedBit( KEY ));

	/* A new session sees everyone anew: copies up to the party's count are the party's own, not new finds. */
	start(); put( 0, 0, FUSE ); put( 0, 1, FUSE ); put( 1, 0, FUSE ); put( 1, 1, FUSE );
	CoF_ShareReset( &s ); look();
	assert( CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	/* ...and a newcomer with nothing is brought to two copies. */
	memset( inv[1], 0, sizeof( inv[1] )); CoF_ShareObserve( &s, 1, 0, 0, NULL ); CoF_ShareObserve( &s, 1, 206, 1, inv[1] );
	assert( plan( 1, 6, 1, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, FUSE, 1 ); put( 1, 0, FUSE );
	CoF_ShareObserve( &s, 1, 206, 1, inv[1] );
	assert( plan( 1, 5, 2, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, FUSE, 2 ); put( 1, 1, FUSE );
	CoF_ShareObserve( &s, 1, 206, 1, inv[1] );
	assert( plan( 1, 4, 3, item ) == COF_SHARE_NONE && CoF_ShareSetCount( &s.party, FUSE ) == 2 );

	/* The checkpoint keeps both copies (one entry each); an older checkpoint's single entry is one copy. */
	{
		cof_share_archive_t archive;
		memset( &archive, 0, sizeof( archive ));
		archive.party = s.party;
		assert( CoF_ShareArchiveValid( &archive ) && CoF_ShareSetCount( &archive.party, FUSE ) == 2 );
	}

	/* Ladders count too (two in the maps, each ladder spot uses one); the spade (two, never used up) keeps one. */
	start(); put( 0, 0, LADDER ); look(); put( 0, 1, LADDER ); look();
	assert( CoF_ShareSetCount( &s.party, LADDER ) == 2 && plan( 0, 4, 1, item ) == COF_SHARE_NONE );
	start(); put( 0, 0, SPADE ); look(); put( 0, 1, SPADE ); look();
	assert( s.party.count == 1 && plan( 0, 4, 1, item ) == COF_SHARE_TAKE && !strcmp( item, SPADE ));

	/* 1.1.8: a give or take issued in the old map's last frame lands in the inventory the level change carries;
	 * the first look in the new map confirms it. Forgotten, a fuse given read as one more found (the party had
	 * three for two) and a fuse taken as one used up (the party short of one). */
	start(); put( 0, 0, FUSE ); look(); deliver( 1, 0, FUSE, 1 );
	put( 0, 1, FUSE ); look(); /* the second pull: the partner is owed a second copy */
	assert( plan( 1, 5, 2, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, FUSE, 2 ); put( 1, 1, FUSE );
	CoF_ShareNewMap( &s ); s.event_count = 0; look(); /* the new map, its clock restarted */
	assert( has_event( COF_SHARE_DELIVERED, 1, FUSE ) && !has_event( COF_SHARE_FOUND, 1, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 2 );
	assert( plan( 0, 4, 1.0, item ) == COF_SHARE_NONE && plan( 1, 4, 1.0, item ) == COF_SHARE_NONE );
	/* ...a take of the copy the party no longer has, the same way */
	CoF_ShareObserve( &s, 0, 101, 1, inv[0] ); inv[0][0][0] = 0; CoF_ShareCommand( &s, 0, CoF_ShareCommandKind( "inventoryuse" ), inv[0] );
	assert( CoF_ShareSetCount( &s.party, FUSE ) == 1 && plan( 1, 4, 1.1, item ) == COF_SHARE_TAKE );
	CoF_ShareIssued( &s, 1, COF_SHARE_TAKE, FUSE, 1.1 ); inv[1][1][0] = 0;
	CoF_ShareNewMap( &s ); s.event_count = 0; look();
	assert( has_event( COF_SHARE_REMOVED, 1, FUSE ) && !has_event( COF_SHARE_USED, 1, FUSE ) && CoF_ShareSetCount( &s.party, FUSE ) == 1 );
	assert( copies( 0, FUSE ) == 1 && copies( 1, FUSE ) == 1 && plan( 0, 5, 1.0, item ) == COF_SHARE_NONE && plan( 1, 5, 1.0, item ) == COF_SHARE_NONE );
	/* ...and one that never landed is retried at once on the new map, not backed off or reported */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 900, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 900 );
	CoF_ShareNewMap( &s ); s.event_count = 0; look();
	assert( plan( 1, 6, 1.0, item ) == COF_SHARE_GIVE && !strcmp( item, KEY ) && !has_event( COF_SHARE_STALLED, 1, KEY ));
	/* ...while a dead player keeps theirs until the respawn's look */
	start(); put( 0, 0, KEY ); look();
	assert( plan( 1, 6, 900, item ) == COF_SHARE_GIVE ); CoF_ShareIssued( &s, 1, COF_SHARE_GIVE, KEY, 900 ); put( 1, 0, KEY );
	CoF_ShareNewMap( &s ); alive[1] = 0; s.event_count = 0; look();
	assert( has_event( COF_SHARE_DELIVERED, 1, KEY ) && !s.players[1].pending );

	/* A new session starts with an empty ledger. */
	CoF_ShareReset( &s );
	assert( s.party.count == 0 && !s.players[0].userid );
	puts( "share state scenarios passed" );
	return 0;
}
