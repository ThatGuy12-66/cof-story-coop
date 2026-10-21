/* Story item ledger for Cry of Fear story co-op. Pure state, no engine types,
 * so the offline harness drives it directly. GPL-3.0-or-later.
 * Rules: docs/superpowers/specs/2026-09-30-coop-split-play-design.md, part 2
 * (project repo). The party ledger is a set. Retail AddItemToInventory (100DAD40)
 * dedupes only weapon_* descriptors, so a story item can sit in two of one
 * player's slots (a give again after the confirm window, a world copy picked
 * up): the second copy is taken. Desired state: every player holds every party
 * item once, except those they set down, and nothing the party no longer has.
 * 1.1.8 (M1 finding 1): the counted items below are the exception: the party
 * keeps one entry per copy (copies found minus copies used), every player is
 * brought to that many, and a use takes one copy. */
#ifndef SV_COF_SHARE_STATE_H
#define SV_COF_SHARE_STATE_H
#include <string.h>
#include "sv_cof_descriptor.h"

#define COF_SHARE_PLAYERS 32
#define COF_SHARE_SLOTS 12 // = COF_RETAIL_SLOTS (sv_cof_share.c checks)
#define COF_SHARE_ITEMS 64
#define COF_SHARE_NAME 64
#define COF_SHARE_EVENTS 16
#define COF_SHARE_CONFIRM 1.0 // an issued give or take shows in the inventory within this
#define COF_SHARE_RETRY 5.0 // then that item waits this long; other items go on
#define COF_SHARE_BACKOFF 8 // items one player can have waiting at once

enum { COF_SHARE_NONE, COF_SHARE_GIVE, COF_SHARE_TAKE, COF_SHARE_ROOM };
enum { COF_SHARE_CMD_OTHER, COF_SHARE_CMD_DROP, COF_SHARE_CMD_COMBINE };
enum { COF_SHARE_FOUND, COF_SHARE_USED, COF_SHARE_DROPPED, COF_SHARE_DELIVERED, COF_SHARE_REMOVED, COF_SHARE_COMBINED, COF_SHARE_STALLED };
// What a loss outside a respawn means: used up for everyone, dropped by this
// player, or combined into this player's own weapon (the item stays shared).
enum { COF_SHARE_LOSS_USED, COF_SHARE_LOSS_DROPPED, COF_SHARE_LOSS_PERSONAL };

typedef struct { char name[COF_SHARE_NAME]; int finder; } cof_share_item_t;
typedef struct { int count; cof_share_item_t items[COF_SHARE_ITEMS]; } cof_share_set_t;
typedef struct { char name[COF_SHARE_NAME]; double until; } cof_share_backoff_t;

typedef struct
{
	int userid; // 0: nobody in this client slot
	int alive;
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME]; // story items at the last look, "" otherwise
	cof_share_set_t set_down; // dropped on purpose: not re-delivered
	cof_share_set_t room_told; // already told to make room for these
	int pending; // COF_SHARE_GIVE or COF_SHARE_TAKE awaiting the inventory
	char pending_name[COF_SHARE_NAME];
	double pending_until;
	cof_share_backoff_t backoff[COF_SHARE_BACKOFF]; // items whose give/take never showed
	cof_share_set_t stalled; // reported once until they go through
	int lying; // counted items set down whose copy still lies in this map (CoF_ShareCountedBit), as the glue last saw
} cof_share_player_t;

typedef struct { int kind, slot, userid; char name[COF_SHARE_NAME]; } cof_share_event_t;

typedef struct
{
	cof_share_set_t party;
	cof_share_player_t players[COF_SHARE_PLAYERS];
	int event_count; // drained by the engine glue every frame
	cof_share_event_t events[COF_SHARE_EVENTS];
} cof_share_state_t;

typedef struct
{
	cof_share_set_t party;
	char names[COF_SHARE_PLAYERS][32];
	cof_share_set_t dropped[COF_SHARE_PLAYERS];
} cof_share_archive_t;

static int CoF_ShareArchiveValid( const cof_share_archive_t *archive )
{
	int player, item;
	if( archive->party.count < 0 || archive->party.count > COF_SHARE_ITEMS ) return 0;
	for( item = 0; item < archive->party.count; item++ )
		if( !memchr( archive->party.items[item].name, 0, COF_SHARE_NAME ) || !CoF_DescriptorSafe( archive->party.items[item].name )) return 0;
	for( player = 0; player < COF_SHARE_PLAYERS; player++ )
	{
		if( !memchr( archive->names[player], 0, 32 ) || archive->dropped[player].count < 0 || archive->dropped[player].count > COF_SHARE_ITEMS ) return 0;
		for( item = 0; item < archive->dropped[player].count; item++ )
			if( !memchr( archive->dropped[player].items[item].name, 0, COF_SHARE_NAME ) || !CoF_DescriptorSafe( archive->dropped[player].items[item].name )) return 0;
	}
	return 1;
}

static void CoF_ShareCopy( char *dst, const char *src )
{
	size_t i;
	for( i = 0; i + 1 < COF_SHARE_NAME && src[i]; i++ ) dst[i] = src[i];
	dst[i] = 0;
}

static int CoF_ShareSetFind( const cof_share_set_t *set, const char *name )
{
	int i;
	for( i = 0; i < set->count; i++ )
		if( !strcmp( set->items[i].name, name )) return i;
	return -1;
}

static int CoF_ShareSetAdd( cof_share_set_t *set, const char *name, int finder )
{
	if( !name[0] || strlen( name ) >= COF_SHARE_NAME || set->count >= COF_SHARE_ITEMS || CoF_ShareSetFind( set, name ) >= 0 ) return 0;
	CoF_ShareCopy( set->items[set->count].name, name );
	set->items[set->count].finder = finder;
	set->count++;
	return 1;
}

static int CoF_ShareSetRemove( cof_share_set_t *set, const char *name )
{
	int i = CoF_ShareSetFind( set, name );
	if( i < 0 ) return 0;
	set->items[i] = set->items[--set->count];
	return 1;
}

static int CoF_ShareHolds( const cof_share_player_t *p, const char *name )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
		if( !strcmp( p->held[i], name )) return 1;
	return 0;
}

// 1.1.8 (M1 finding 1): story items the maps hand out more than once and use up
// one copy at a time, so one player alone carries two. Fuses: c_down, c_stad,
// c_college2 and the two c_subway2st1 fuse boxes give them; c_subway2st1's and
// c_subway2st3's gates each need two boxes filled (game_counter health 2), with
// no fuse to be had in between. Ladders: c_avlopp and c_apartment4sick each have
// one, each ladder spot uses one. A set kept one copy, so co-op was a fuse short.
// The party now holds one entry per copy of these (an old checkpoint's single
// entry reads as one copy). The spade (two, never used up) keeps one copy.
static int CoF_ShareCounted( const char *name )
{
	return !strcmp( name, "inventoryitems/fuse.txt" ) || !strcmp( name, "inventoryitems/ladder1.txt" );
}

// 1.1.9 (I2): one bit per counted item, for what the glue sees of the copies a player set down (lying).
static int CoF_ShareCountedBit( const char *name )
{
	if( !strcmp( name, "inventoryitems/fuse.txt" )) return 1;
	if( !strcmp( name, "inventoryitems/ladder1.txt" )) return 2;
	return 0;
}

static int CoF_ShareSetCount( const cof_share_set_t *set, const char *name )
{
	int i, n = 0;
	for( i = 0; i < set->count; i++ ) n += !strcmp( set->items[i].name, name );
	return n;
}

static void CoF_ShareReset( cof_share_state_t *s )
{
	memset( s, 0, sizeof( *s ));
}

static void CoF_ShareEvent( cof_share_state_t *s, int kind, int slot, const char *name )
{
	cof_share_event_t *e;
	if( s->event_count >= COF_SHARE_EVENTS ) return; // only log lines are lost
	e = &s->events[s->event_count++];
	e->kind = kind;
	e->slot = slot;
	e->userid = s->players[slot].userid;
	CoF_ShareCopy( e->name, name );
}

static void CoF_ShareStoryOnly( char out[COF_SHARE_SLOTS][COF_SHARE_NAME], char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	int i;
	for( i = 0; i < COF_SHARE_SLOTS; i++ )
	{
		out[i][0] = 0;
		if( slots && CoF_IsStoryItem( slots[i] ) && strlen( slots[i] ) < COF_SHARE_NAME ) CoF_ShareCopy( out[i], slots[i] );
	}
}

static void CoF_ShareFound( cof_share_state_t *s, int slot, const char *name )
{
	cof_share_player_t *p = &s->players[slot];
	CoF_ShareSetRemove( &p->set_down, name );
	if( CoF_ShareSetAdd( &s->party, name, p->userid )) CoF_ShareEvent( s, COF_SHARE_FOUND, slot, name );
}

// A counted item gained outside a delivery: the copy this player set down,
// picked back up, or one more copy for the party. 1.1.9 (I2): while the copy
// set down still lies in the map, the gain is another one (a c_subway2st1 fuse
// box pulled after a drop): the party was left a fuse short, and the copy set
// down stays set down.
static void CoF_ShareFoundCopy( cof_share_state_t *s, int slot, const char *name )
{
	cof_share_player_t *p = &s->players[slot];
	if(( !( p->lying & CoF_ShareCountedBit( name )) && CoF_ShareSetRemove( &p->set_down, name )) || s->party.count >= COF_SHARE_ITEMS ) return;
	CoF_ShareCopy( s->party.items[s->party.count].name, name );
	s->party.items[s->party.count].finder = p->userid;
	s->party.count++;
	CoF_ShareEvent( s, COF_SHARE_FOUND, slot, name );
}

// Removes one entry: the item, or one copy of a counted item.
static void CoF_ShareUsed( cof_share_state_t *s, int slot, const char *name )
{
	if( CoF_ShareSetRemove( &s->party, name )) CoF_ShareEvent( s, COF_SHARE_USED, slot, name );
}

// One player's inventory changed from p->held to held. ignore_losses: the
// player is dead or was just respawned (the Spawn wipe is not a use).
// loss: what the other losses mean (COF_SHARE_LOSS_*).
static int CoF_ShareCount( char held[COF_SHARE_SLOTS][COF_SHARE_NAME], const char *name )
{
	int i, n = 0;
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) n += name[0] && !strcmp( held[i], name );
	return n;
}

// 1.1.8 (M1 finding 1): the counted items by number of copies, each copy lost or
// gained handled like a whole item below. joining: a new player in this slot;
// their copies up to the party's count are the party's own (a new session sees
// every player anew), only copies beyond it are found.
static void CoF_ShareApplyCounted( cof_share_state_t *s, int slot, char held[COF_SHARE_SLOTS][COF_SHARE_NAME], int ignore_losses, int loss, int joining )
{
	cof_share_player_t *p = &s->players[slot];
	int i, j, seen, before, after, party;
	for( i = 0; i < 2 * COF_SHARE_SLOTS; i++ )
	{
		const char *name = i < COF_SHARE_SLOTS ? p->held[i] : held[i - COF_SHARE_SLOTS];
		if( !CoF_ShareCounted( name )) continue;
		for( seen = j = 0; j < i && !seen; j++ ) seen = !strcmp( j < COF_SHARE_SLOTS ? p->held[j] : held[j - COF_SHARE_SLOTS], name );
		if( seen ) continue; // this item is done
		before = CoF_ShareCount( p->held, name );
		after = CoF_ShareCount( held, name );
		party = CoF_ShareSetCount( &s->party, name );
		if( joining ) before = after < party ? after : party;
		for( ; before > after; before-- ) // one copy fewer
		{
			if( p->pending == COF_SHARE_TAKE && !strcmp( p->pending_name, name ))
			{
				p->pending = COF_SHARE_NONE;
				CoF_ShareSetRemove( &p->stalled, name );
				CoF_ShareEvent( s, COF_SHARE_REMOVED, slot, name );
			}
			else if( ignore_losses ) continue;
			else if( loss != COF_SHARE_LOSS_USED )
			{
				CoF_ShareSetAdd( &p->set_down, name, 0 );
				CoF_ShareEvent( s, loss == COF_SHARE_LOSS_DROPPED ? COF_SHARE_DROPPED : COF_SHARE_COMBINED, slot, name );
			}
			else CoF_ShareUsed( s, slot, name );
		}
		for( ; before < after; before++ ) // one copy more
		{
			if( p->pending == COF_SHARE_GIVE && !strcmp( p->pending_name, name ))
			{
				p->pending = COF_SHARE_NONE;
				CoF_ShareSetRemove( &p->stalled, name );
				CoF_ShareEvent( s, COF_SHARE_DELIVERED, slot, name );
			}
			else CoF_ShareFoundCopy( s, slot, name );
		}
	}
}

static void CoF_ShareApply( cof_share_state_t *s, int slot, char held[COF_SHARE_SLOTS][COF_SHARE_NAME], int ignore_losses, int loss, int joining )
{
	cof_share_player_t *p = &s->players[slot];
	int i, j, kept, now_held;
	// a second copy taken: one fewer, the item still held (counted items: below)
	if( p->pending == COF_SHARE_TAKE && !CoF_ShareCounted( p->pending_name ) && ( now_held = CoF_ShareCount( held, p->pending_name )) > 0 &&
		now_held < CoF_ShareCount( p->held, p->pending_name ))
	{
		p->pending = COF_SHARE_NONE;
		CoF_ShareSetRemove( &p->stalled, p->pending_name );
		CoF_ShareEvent( s, COF_SHARE_REMOVED, slot, p->pending_name );
	}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // losses
	{
		const char *name = p->held[i];
		if( !name[0] || CoF_ShareCounted( name )) continue;
		for( kept = j = 0; j < COF_SHARE_SLOTS && !kept; j++ ) kept = !strcmp( held[j], name );
		if( kept ) continue;
		if( p->pending == COF_SHARE_TAKE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareSetRemove( &p->stalled, name );
			CoF_ShareEvent( s, COF_SHARE_REMOVED, slot, name );
		}
		else if( ignore_losses ) continue;
		else if( loss != COF_SHARE_LOSS_USED )
		{
			CoF_ShareSetAdd( &p->set_down, name, 0 );
			CoF_ShareEvent( s, loss == COF_SHARE_LOSS_DROPPED ? COF_SHARE_DROPPED : COF_SHARE_COMBINED, slot, name );
		}
		else CoF_ShareUsed( s, slot, name );
	}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // gains
	{
		const char *name = held[i];
		if( !name[0] || CoF_ShareCounted( name ) || CoF_ShareHolds( p, name )) continue;
		if( p->pending == COF_SHARE_GIVE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareSetRemove( &p->stalled, name );
			CoF_ShareEvent( s, COF_SHARE_DELIVERED, slot, name );
			continue; // a late delivery must not bring back an item used up meanwhile
		}
		CoF_ShareFound( s, slot, name );
	}
	CoF_ShareApplyCounted( s, slot, held, ignore_losses, loss, joining );
	memcpy( p->held, held, sizeof( p->held ));
}

// A frame's look at one client slot. userid 0: nobody there (slots may be NULL).
static void CoF_ShareObserve( cof_share_state_t *s, int slot, int userid, int alive, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	cof_share_player_t *p;
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int respawned;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS ) return;
	p = &s->players[slot];
	if( !userid )
	{
		memset( p, 0, sizeof( *p ));
		return;
	}
	CoF_ShareStoryOnly( held, slots );
	if( p->userid != userid )
	{
		// A new player in this slot: whatever they carry becomes the party's.
		memset( p, 0, sizeof( *p ));
		p->userid = userid;
		p->alive = alive;
		CoF_ShareApply( s, slot, held, 0, COF_SHARE_LOSS_USED, 1 );
		return;
	}
	respawned = alive && !p->alive; // CBasePlayer::Spawn wiped the inventory
	CoF_ShareApply( s, slot, held, respawned || !alive, COF_SHARE_LOSS_USED, 0 );
	if( respawned )
	{
		memset( &p->set_down, 0, sizeof( p->set_down ));
		memset( &p->room_told, 0, sizeof( p->room_told ));
	}
	p->alive = alive;
}

static int CoF_ShareCommandKind( const char *command )
{
	if( !strcmp( command, "inventorydrop" )) return COF_SHARE_CMD_DROP;
	if( !strcmp( command, "inventorycombine" )) return COF_SHARE_CMD_COMBINE;
	return COF_SHARE_CMD_OTHER;
}

// After a game client command from this player. The glue refreshed the look
// with CoF_ShareObserve right before the command ran, so this change is
// exactly the command's doing.
static void CoF_ShareCommand( cof_share_state_t *s, int slot, int kind, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int i, loss = kind == COF_SHARE_CMD_DROP ? COF_SHARE_LOSS_DROPPED : COF_SHARE_LOSS_USED;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS || !s->players[slot].userid ) return;
	CoF_ShareStoryOnly( held, slots );
	if( kind == COF_SHARE_CMD_COMBINE )
	{
		// A combine that yields no new story item (taclight + Glock, battery +
		// phone, CombineInventoryItems 100E3020) upgraded the combiner's own
		// weapon: the partner keeps their copy for their own weapon.
		loss = COF_SHARE_LOSS_PERSONAL;
		for( i = 0; i < COF_SHARE_SLOTS; i++ )
			if( held[i][0] && !CoF_ShareHolds( &s->players[slot], held[i] )) loss = COF_SHARE_LOSS_USED;
	}
	CoF_ShareApply( s, slot, held, !s->players[slot].alive, loss, 0 );
}

static int CoF_ShareBackingOff( const cof_share_player_t *p, const char *name, double now )
{
	int i;
	for( i = 0; i < COF_SHARE_BACKOFF; i++ )
		if( now < p->backoff[i].until && !strcmp( p->backoff[i].name, name )) return 1;
	return 0;
}

// This item waits until `until`; it takes its own old entry or the one due soonest.
static void CoF_ShareBackoff( cof_share_player_t *p, const char *name, double until )
{
	int i, pick = 0;
	for( i = 0; i < COF_SHARE_BACKOFF; i++ )
	{
		if( !strcmp( p->backoff[i].name, name )) { pick = i; break; }
		if( p->backoff[i].until < p->backoff[pick].until ) pick = i;
	}
	CoF_ShareCopy( p->backoff[pick].name, name );
	p->backoff[pick].until = until;
}

// The next step toward the desired state for one player. free_slots: usable
// empty inventory slots, -1 when unknown. The item goes to out.
static int CoF_SharePlan( cof_share_state_t *s, int slot, int free_slots, double now, char out[COF_SHARE_NAME] )
{
	cof_share_player_t *p;
	int i, j;
	out[0] = 0;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS ) return COF_SHARE_NONE;
	p = &s->players[slot];
	if( !p->userid || !p->alive ) return COF_SHARE_NONE;
	if( p->pending )
	{
		if( now < p->pending_until ) return COF_SHARE_NONE;
		// Never confirmed: this item backs off and is reported once; the rest go on. One carried over a level
		// change (pending_until -1, CoF_ShareNewMap) had its look: it is simply retried.
		if( p->pending_until >= 0 )
		{
			CoF_ShareBackoff( p, p->pending_name, now + COF_SHARE_RETRY );
			if( CoF_ShareSetAdd( &p->stalled, p->pending_name, 0 )) CoF_ShareEvent( s, COF_SHARE_STALLED, slot, p->pending_name );
		}
		p->pending = COF_SHARE_NONE;
	}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // a second copy of one item
		for( j = i + 1; j < COF_SHARE_SLOTS; j++ )
			if( p->held[i][0] && !strcmp( p->held[i], p->held[j] ) && !CoF_ShareCounted( p->held[j] ) && !CoF_ShareBackingOff( p, p->held[j], now ))
			{
				CoF_ShareCopy( out, p->held[j] );
				return COF_SHARE_TAKE;
			}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // more copies of a counted item than the party has left (1.1.8)
		if( CoF_ShareCounted( p->held[i] ) && CoF_ShareCount( p->held, p->held[i] ) > CoF_ShareSetCount( &s->party, p->held[i] ) &&
			!CoF_ShareBackingOff( p, p->held[i], now ))
		{
			CoF_ShareCopy( out, p->held[i] );
			return COF_SHARE_TAKE;
		}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // copies of items the party used up
		if( p->held[i][0] && CoF_ShareSetFind( &s->party, p->held[i] ) < 0 && !CoF_ShareBackingOff( p, p->held[i], now ))
		{
			CoF_ShareCopy( out, p->held[i] );
			return COF_SHARE_TAKE;
		}
	for( i = 0; i < s->party.count; i++ )
	{
		const char *name = s->party.items[i].name;
		// held already: once, or as many copies as the party has of a counted item (1.1.8)
		if(( CoF_ShareCounted( name ) ? CoF_ShareCount( p->held, name ) >= CoF_ShareSetCount( &s->party, name ) : CoF_ShareHolds( p, name )) ||
			CoF_ShareSetFind( &p->set_down, name ) >= 0 || CoF_ShareBackingOff( p, name, now )) continue;
		if( free_slots > 0 )
		{
			CoF_ShareCopy( out, name );
			return COF_SHARE_GIVE;
		}
		if( free_slots < 0 ) continue;
		if( CoF_ShareSetAdd( &p->room_told, name, 0 ))
		{
			CoF_ShareCopy( out, name );
			return COF_SHARE_ROOM;
		}
	}
	return COF_SHARE_NONE;
}

static void CoF_ShareIssued( cof_share_state_t *s, int slot, int action, const char *name, double now )
{
	cof_share_player_t *p;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS || ( action != COF_SHARE_GIVE && action != COF_SHARE_TAKE )) return;
	p = &s->players[slot];
	p->pending = action;
	CoF_ShareCopy( p->pending_name, name );
	p->pending_until = now + COF_SHARE_CONFIRM;
}

// A new map restarts the server clock, so timers from the previous map would
// hold the queue for as long as that map took. 1.1.8: a give or take issued in
// the old map's last frame landed in the inventory the level change carries, and
// the player's first look in the new map has not seen it yet. Forgotten, it read
// as found or used up: harmless for a key (the party has it, or not), but a
// fuse or ladder copy was added to or taken from the whole party. It stays in
// flight until that look (the plan runs after it); one that did not land is then
// retried at once (pending_until -1: no backoff, no stall report).
static void CoF_ShareNewMap( cof_share_state_t *s )
{
	int i;
	for( i = 0; i < COF_SHARE_PLAYERS; i++ )
	{
		s->players[i].pending_until = -1;
		memset( s->players[i].backoff, 0, sizeof( s->players[i].backoff ));
	}
}

static int CoF_ShareFinder( const cof_share_state_t *s, const char *name )
{
	int i = CoF_ShareSetFind( &s->party, name );
	return i < 0 ? 0 : s->party.items[i].finder;
}
#endif
