/* Story item ledger for Cry of Fear story co-op. Pure state, no engine types,
 * so the offline harness drives it directly. GPL-3.0-or-later.
 * Rules: docs/superpowers/specs/2026-09-30-coop-split-play-design.md, part 2
 * (project repo). A player can never hold two copies of one descriptor
 * (retail AddInventoryItem 100DAD40 has an exact duplicate check), so the
 * party ledger is a set. Desired state: every player holds every party item
 * except those they set down, and nothing the party no longer has. */
#ifndef SV_COF_SHARE_STATE_H
#define SV_COF_SHARE_STATE_H
#include <string.h>
#include "sv_cof_descriptor.h"

#define COF_SHARE_PLAYERS 32
#define COF_SHARE_SLOTS 6
#define COF_SHARE_ITEMS 64
#define COF_SHARE_NAME 64
#define COF_SHARE_EVENTS 16
#define COF_SHARE_CONFIRM 1.0 // an issued give or take shows in the inventory within this
#define COF_SHARE_RETRY 5.0 // then it waits this long before trying again

enum { COF_SHARE_NONE, COF_SHARE_GIVE, COF_SHARE_TAKE, COF_SHARE_ROOM };
enum { COF_SHARE_CMD_OTHER, COF_SHARE_CMD_DROP };
enum { COF_SHARE_FOUND, COF_SHARE_USED, COF_SHARE_DROPPED, COF_SHARE_DELIVERED, COF_SHARE_REMOVED };

typedef struct { char name[COF_SHARE_NAME]; int finder; } cof_share_item_t;
typedef struct { int count; cof_share_item_t items[COF_SHARE_ITEMS]; } cof_share_set_t;

typedef struct
{
	int userid; // 0: nobody in this client slot
	int alive;
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME]; // story items at the last look, "" otherwise
	cof_share_set_t set_down; // dropped on purpose: not re-delivered
	cof_share_set_t room_told; // already told to make room for these
	int pending; // COF_SHARE_GIVE or COF_SHARE_TAKE awaiting the inventory
	char pending_name[COF_SHARE_NAME];
	double pending_until, retry_after;
} cof_share_player_t;

typedef struct { int kind, slot, userid; char name[COF_SHARE_NAME]; } cof_share_event_t;

typedef struct
{
	cof_share_set_t party;
	cof_share_player_t players[COF_SHARE_PLAYERS];
	int event_count; // drained by the engine glue every frame
	cof_share_event_t events[COF_SHARE_EVENTS];
} cof_share_state_t;

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

static void CoF_ShareUsed( cof_share_state_t *s, int slot, const char *name )
{
	if( CoF_ShareSetRemove( &s->party, name )) CoF_ShareEvent( s, COF_SHARE_USED, slot, name );
}

// One player's inventory changed from p->held to held. ignore_losses: the
// player is dead or was just respawned (the Spawn wipe is not a use).
// dropping: the change came from an inventorydrop command.
static void CoF_ShareApply( cof_share_state_t *s, int slot, char held[COF_SHARE_SLOTS][COF_SHARE_NAME], int ignore_losses, int dropping )
{
	cof_share_player_t *p = &s->players[slot];
	int i, j, kept;
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // losses
	{
		const char *name = p->held[i];
		if( !name[0] ) continue;
		for( kept = j = 0; j < COF_SHARE_SLOTS && !kept; j++ ) kept = !strcmp( held[j], name );
		if( kept ) continue;
		if( p->pending == COF_SHARE_TAKE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareEvent( s, COF_SHARE_REMOVED, slot, name );
		}
		else if( ignore_losses ) continue;
		else if( dropping )
		{
			CoF_ShareSetAdd( &p->set_down, name, 0 );
			CoF_ShareEvent( s, COF_SHARE_DROPPED, slot, name );
		}
		else CoF_ShareUsed( s, slot, name );
	}
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // gains
	{
		const char *name = held[i];
		if( !name[0] || CoF_ShareHolds( p, name )) continue;
		if( p->pending == COF_SHARE_GIVE && !strcmp( p->pending_name, name ))
		{
			p->pending = COF_SHARE_NONE;
			CoF_ShareEvent( s, COF_SHARE_DELIVERED, slot, name );
			continue; // a late delivery must not bring back an item used up meanwhile
		}
		CoF_ShareFound( s, slot, name );
	}
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
		CoF_ShareApply( s, slot, held, 0, 0 );
		return;
	}
	respawned = alive && !p->alive; // CBasePlayer::Spawn wiped the inventory
	CoF_ShareApply( s, slot, held, respawned || !alive, 0 );
	if( respawned )
	{
		memset( &p->set_down, 0, sizeof( p->set_down ));
		memset( &p->room_told, 0, sizeof( p->room_told ));
	}
	p->alive = alive;
}

static int CoF_ShareCommandKind( const char *command )
{
	return !strcmp( command, "inventorydrop" ) ? COF_SHARE_CMD_DROP : COF_SHARE_CMD_OTHER;
}

// After a game client command from this player. The glue refreshed the look
// with CoF_ShareObserve right before the command ran, so this change is
// exactly the command's doing.
static void CoF_ShareCommand( cof_share_state_t *s, int slot, int kind, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME] )
{
	char held[COF_SHARE_SLOTS][COF_SHARE_NAME];
	if( slot < 0 || slot >= COF_SHARE_PLAYERS || !s->players[slot].userid ) return;
	CoF_ShareStoryOnly( held, slots );
	CoF_ShareApply( s, slot, held, !s->players[slot].alive, kind == COF_SHARE_CMD_DROP );
}

// The next step toward the desired state for one player. free_slots: usable
// empty inventory slots, -1 when unknown. The item goes to out.
static int CoF_SharePlan( cof_share_state_t *s, int slot, int free_slots, double now, char out[COF_SHARE_NAME] )
{
	cof_share_player_t *p;
	int i;
	out[0] = 0;
	if( slot < 0 || slot >= COF_SHARE_PLAYERS ) return COF_SHARE_NONE;
	p = &s->players[slot];
	if( !p->userid || !p->alive ) return COF_SHARE_NONE;
	if( p->pending )
	{
		if( now < p->pending_until ) return COF_SHARE_NONE;
		p->pending = COF_SHARE_NONE; // never confirmed: back off, then retry
		p->retry_after = now + COF_SHARE_RETRY;
	}
	if( now < p->retry_after ) return COF_SHARE_NONE;
	for( i = 0; i < COF_SHARE_SLOTS; i++ ) // copies of items the party used up
		if( p->held[i][0] && CoF_ShareSetFind( &s->party, p->held[i] ) < 0 )
		{
			CoF_ShareCopy( out, p->held[i] );
			return COF_SHARE_TAKE;
		}
	for( i = 0; i < s->party.count; i++ )
	{
		const char *name = s->party.items[i].name;
		if( CoF_ShareHolds( p, name ) || CoF_ShareSetFind( &p->set_down, name ) >= 0 ) continue;
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

static int CoF_ShareFinder( const cof_share_state_t *s, const char *name )
{
	int i = CoF_ShareSetFind( &s->party, name );
	return i < 0 ? 0 : s->party.items[i].finder;
}
#endif
