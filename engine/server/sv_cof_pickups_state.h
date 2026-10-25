/*
sv_cof_pickups_state.h - Cry of Fear story co-op: per-player map pickups, the rules (sv_cof_pickups.c)
GPL-3.0-or-later.

The retail multiplayer rules never respawn a weapon, ammo or item (multiplay_gamerules.cpp: WeaponShouldRespawn,
AmmoShouldRespawn and ItemShouldRespawn answer *_RESPAWN_NO), so the first player to +use one of the map's syringes,
ammo boxes or guns takes the only one and the partners get nothing (1.1.9 open items I8, I9). Here every human takes
their own copy of each pickup the map placed:
- the map's own pickups are the blocks of its entity text of a class below that nothing names and whose take fires
  nothing, found again in the world where they lie (CoF_PickupMatch). Dropped weapons, monster drops, script gifts,
  the starter kit and the story items (keys, fuses, the taclight, the phone battery: the share ledger,
  sv_cof_share.c) are not among them;
- a take is seen right after the command of the player who made it (CoF_PickupAfterCommand). An identical pickup is
  then made from the same block at the same spot, which the players who took one neither see nor find with +use,
  while a human present has not taken theirs; a player who joins later gets theirs (CoF_PickupWanted);
- AI companions keep their own kit and take none (CoF_PickupRefused);
- the session ledger keeps who took which pickup of which map, so a revisit and a checkpoint load hand nothing out
  twice to the same player (CoF_PickupResolve, CoF_PickupLedgerSaved, CoF_PickupLedgerLoaded);
- the bonus room's items a chapter start lays out (sv_cof_bonus_kit.h) are per-player the same way: the bonus mark
  tells them in the world, the ledger keeps them as blocks of their own (COF_PICKUP_BONUS_BLOCK) and a copy gets the
  kit's keys.

Plain C (standard library only): the offline harness drives it as it is.
*/
#ifndef SV_COF_PICKUPS_STATE_H
#define SV_COF_PICKUPS_STATE_H

#include <stdlib.h>
#include <string.h>

#define COF_PICKUP_MAX 128     // per-player pickups one map file can have (the story's most: 14, c_subtunnel1)
#define COF_PICKUP_TAKERS 8    // players one pickup remembers: a party of 2-4, and players back under another name
#define COF_PICKUP_LEDGER 384  // pickups a session remembers (the 81 story maps place 234 of the classes below)
#define COF_PICKUP_MAPS 160    // map files it remembers them for (the 81 story maps and the merged files)
#define COF_PICKUP_MAPNAME 64
#define COF_PICKUP_SLOTNAME 32
// The mark of a copy: SF_NORESPAWN, which GiveNamedItem sets on everything it gives (player.cpp:7934) and which only
// the retail respawn rules read (commented out, multiplay_gamerules.cpp:1973-2079). It is saved with the entity, so a
// revisit or a checkpoint tells a copy from the map's own pickup.
#define COF_PICKUP_COPY_FLAG ( 1 << 30 )
// The bonus room's items a chapter start lays out (sv_cof_bonus_kit.h) are no block of the map's text: this mark
// (saved with the entity like the copy mark; of the game's code only multi_manager reads bit 29, as
// SF_MULTIMAN_TRIGCHOSEN) tells them, and the ledger keeps them as blocks COF_PICKUP_BONUS_BLOCK + the kit's item.
#define COF_PICKUP_BONUS_FLAG ( 1 << 29 )
#define COF_PICKUP_BONUS_BLOCK 0x10000
// Where a pickup lies against its block: straight below it (a gun falls to the floor, FallInit; ammo is
// MOVETYPE_TOSS too; pills DROP_TO_FLOOR), give or take a little.
#define COF_PICKUP_NEAR 16.0f
#define COF_PICKUP_FALL 256.0f

// COF_PICKUP_ITEM: the kit's night vision, a CItem taken by touch with +use pressed (CNightVision::MyTouch), and
// removed at once, as pills are.
enum { COF_PICKUP_NONE, COF_PICKUP_WEAPON, COF_PICKUP_AMMO, COF_PICKUP_PILLS, COF_PICKUP_ITEM };
// A pickup's state on its map: nothing to hand out, one lying, waiting for a player who has not taken theirs, given up.
enum { COF_PICKUP_OFF, COF_PICKUP_LIVE, COF_PICKUP_WAITING, COF_PICKUP_RETIRED };
// What became of the one lying.
enum { COF_PICKUP_LYING, COF_PICKUP_TAKEN, COF_PICKUP_GONE };

// The per-player classes; spawns: the classname the game's Spawn gives it (the aliases are renamed). Weapons and ammo
// are taken with +use only (weapons.cpp FallInit, CBasePlayerAmmo::Spawn: their touch is set only for iuser4 1, the
// items GiveNamedItem hands out); AoM pills too (effects.cpp CAoMPills). Every class the 81 story maps place is
// here except the story items (item_key, item_phonebattery: the share ledger), item_padlock (a puzzle) and
// item_nightvision (the bonus room's: an item, not a weapon, ammo or medicine, and a CItem is also taken by touch; since
// 1.2.10 the room's gets the bonus mark and is per-player as the kit's, sv_cof_pickups.c). Not placed by the story maps and left out on purpose: weapon_mobile and weapon_flashlight
// (the story's own lights: the starter kit and a gift, sv_cof_party.c), weapon_browning_wheelchair (c_ending4b's
// chair), weapon_radio, weapon_dualweapon and weapon_action (the game builds them).
typedef struct { const char *classname, *spawns; int kind; } cof_pickup_class_t;
static const cof_pickup_class_t cof_pickup_classes[] =
{
	{ "weapon_axe", "weapon_axe", COF_PICKUP_WEAPON },
	{ "weapon_booklaser", "weapon_booklaser", COF_PICKUP_WEAPON },
	{ "weapon_branch", "weapon_branch", COF_PICKUP_WEAPON },
	{ "weapon_camera", "weapon_camera", COF_PICKUP_WEAPON },
	{ "weapon_famas", "weapon_famas", COF_PICKUP_WEAPON },
	{ "weapon_flare", "weapon_flare", COF_PICKUP_WEAPON },
	{ "weapon_g43", "weapon_g43", COF_PICKUP_WEAPON },
	{ "weapon_glock", "weapon_glock", COF_PICKUP_WEAPON },
	{ "weapon_lantern", "weapon_lantern", COF_PICKUP_WEAPON },
	{ "weapon_m16", "weapon_m16", COF_PICKUP_WEAPON },
	{ "weapon_mp5", "weapon_mp5", COF_PICKUP_WEAPON },
	{ "weapon_nightstick", "weapon_nightstick", COF_PICKUP_WEAPON },
	{ "weapon_p345", "weapon_p345", COF_PICKUP_WEAPON },
	{ "weapon_revolver", "weapon_revolver", COF_PICKUP_WEAPON },
	{ "weapon_rifle", "weapon_rifle", COF_PICKUP_WEAPON },
	{ "weapon_shotgun", "weapon_shotgun", COF_PICKUP_WEAPON },
	{ "weapon_sledgehammer", "weapon_sledgehammer", COF_PICKUP_WEAPON },
	{ "weapon_switchblade", "weapon_switchblade", COF_PICKUP_WEAPON },
	{ "weapon_syringe", "weapon_syringe", COF_PICKUP_WEAPON },
	{ "weapon_tmp", "weapon_tmp", COF_PICKUP_WEAPON },
	{ "weapon_vp70", "weapon_vp70", COF_PICKUP_WEAPON },
	{ "weapon_beretta", "weapon_glock", COF_PICKUP_WEAPON },
	{ "weapon_P228", "weapon_p345", COF_PICKUP_WEAPON },
	{ "weapon_knife", "weapon_switchblade", COF_PICKUP_WEAPON },
	{ "weapon_hammer", "weapon_nightstick", COF_PICKUP_WEAPON },
	{ "weapon_uzi", "weapon_tmp", COF_PICKUP_WEAPON },
	{ "weapon_mp5k", "weapon_m16", COF_PICKUP_WEAPON },
	{ "ammo_flashlightbattery", "ammo_flashlightbattery", COF_PICKUP_AMMO },
	{ "ammo_g43", "ammo_g43", COF_PICKUP_AMMO },
	{ "ammo_glock", "ammo_glock", COF_PICKUP_AMMO },
	{ "ammo_m16", "ammo_m16", COF_PICKUP_AMMO },
	{ "ammo_mp5", "ammo_mp5", COF_PICKUP_AMMO },
	{ "ammo_p345", "ammo_p345", COF_PICKUP_AMMO },
	{ "ammo_revolver", "ammo_revolver", COF_PICKUP_AMMO },
	{ "ammo_rifle", "ammo_rifle", COF_PICKUP_AMMO },
	{ "ammo_shells", "ammo_shells", COF_PICKUP_AMMO },
	{ "ammo_tmp", "ammo_tmp", COF_PICKUP_AMMO },
	{ "ammo_vp70", "ammo_vp70", COF_PICKUP_AMMO },
	{ "aom_pills", "aom_pills", COF_PICKUP_PILLS },
};
#define COF_PICKUP_CLASSES ( (int)( sizeof( cof_pickup_classes ) / sizeof( cof_pickup_classes[0] )))

// The class's row, or -1: not a per-player pickup.
static int CoF_PickupClass( const char *classname )
{
	int i;
	if( !classname ) return -1;
	for( i = 0; i < COF_PICKUP_CLASSES; i++ )
		if( !strcmp( cof_pickup_classes[i].classname, classname )) return i;
	return -1;
}

// The row of the class an entity of this row is once spawned (an alias's canonical row), for matching.
static int CoF_PickupCanonical( int row )
{
	if( row < 0 || row >= COF_PICKUP_CLASSES ) return -1;
	return CoF_PickupClass( cof_pickup_classes[row].spawns );
}

// A block of the entity text that becomes per-player: a class above, nothing names it (a script could use or kill
// it), its take fires nothing (DefaultTouch and ItemTouch fire the target and killtarget of what is taken: such a
// pickup stays the only one, so its script runs once), and no iuser4 (taken by touch, which no filter holds). None
// of the story maps' pickups has any of these keys; a custom map's keep the retail rules.
static int CoF_PickupEligible( int row, int named, int fires, int touch )
{
	return row >= 0 && !named && !fires && !touch;
}

// "x y z" of the entity text; 0 unless three numbers.
static int CoF_PickupParseOrigin( const char *text, float *origin )
{
	int i;
	if( !text ) return 0;
	for( i = 0; i < 3; i++ )
	{
		char *end;
		double value = strtod( text, &end );
		if( end == text ) return 0;
		origin[i] = (float)value;
		text = end;
	}
	return 1;
}

// A player as the pickups remember them: their name (the engine keeps the names of the players in game unique,
// sv_client.c; the recovery records go by names too), hashed: FNV-1a, case-insensitive, never 0 (0: nobody).
static unsigned int CoF_PickupName( const char *name )
{
	unsigned int hash = 2166136261u;
	if( !name || !name[0] ) return 0;
	for( ; *name; name++ )
	{
		unsigned char c = (unsigned char)*name;
		if( c >= 'A' && c <= 'Z' ) c = (unsigned char)( c + 32 );
		hash = ( hash ^ c ) * 16777619u;
	}
	return hash ? hash : 1u;
}

typedef struct { int count; unsigned int names[COF_PICKUP_TAKERS]; } cof_pickup_takers_t;

static int CoF_PickupTook( const cof_pickup_takers_t *takers, unsigned int name )
{
	int i;
	if( !takers || !name ) return 0;
	for( i = 0; i < takers->count && i < COF_PICKUP_TAKERS; i++ )
		if( takers->names[i] == name ) return 1;
	return 0;
}

// A copy is not sent to a player who took theirs.
static int CoF_PickupHidden( const cof_pickup_takers_t *takers, unsigned int viewer )
{
	return CoF_PickupTook( takers, viewer );
}

// Nor found by their +use. An AI companion finds no map pickup: it keeps its own kit (sv_cof_companion.c), and its
// CPR +use near one must not take a human's.
static int CoF_PickupRefused( const cof_pickup_takers_t *takers, unsigned int user, int bot )
{
	return bot || CoF_PickupTook( takers, user );
}

// Another copy is wanted while a human present (present: their names) has not taken one.
static int CoF_PickupWanted( const cof_pickup_takers_t *takers, const unsigned int *present, int count )
{
	int i;
	for( i = 0; i < count; i++ )
		if( present[i] && !CoF_PickupTook( takers, present[i] )) return 1;
	return 0;
}

// A take: the human joins the takers. An AI companion's take (only if a filter missed) is not counted: the humans
// still get theirs. 0: no more copies of this pickup, the list being full or this human having had theirs already
// (an engine filter missed: copy after copy must not follow).
static int CoF_PickupSettle( cof_pickup_takers_t *takers, unsigned int name, int bot )
{
	if( bot ) return 1;
	if( !name || CoF_PickupTook( takers, name ) || takers->count >= COF_PICKUP_TAKERS ) return 0;
	takers->names[takers->count++] = name;
	return 1;
}

// One look at the entity a pickup lies as.
typedef struct
{
	int gone;        // freed, its number used again, or FL_KILLME (freed at the frame's end)
	int owner;       // pev->owner's edict number, 0: none
	int follow;      // MOVETYPE_FOLLOW: a gun carried by its owner
	float nextthink; // a think scheduled: SUB_Remove a tenth of a second after some takes
} cof_pickup_look_t;

// What the command a player just ran did to a pickup lying before it. A take: a new gun is attached to them
// (weapons.cpp AttachToPlayer: owner, MOVETYPE_FOLLOW); ammo, or a gun they already had, goes into theirs and is
// removed a tenth of a second later (CBasePlayerAmmo::DefaultTouch, CBasePlayerItem::Kill: SUB_Remove scheduled);
// pills remove themselves at once when they heal (CAoMPills::PillsUse: UTIL_Remove). A gun or ammo removed at once is
// no take: a bonus room weapon still locked, or TMP ammo for a non-donator, removes itself on a player's first
// update, which runs inside their command (SendInitMessages from PreThink). A full inventory or full ammo changes
// nothing: no take. Nothing else in a command touches a lying pickup (its physics runs outside commands).
// The kit's night vision (COF_PICKUP_ITEM) removes itself at once when taken, like pills; its unlock check removes it
// too (CNightVision::SendInitMessage, at every player's first update), so the kit lays it out only when the line it
// checks is unlocked (sv_cof_bonus.c): then nothing but a take removes it in a command.
static int CoF_PickupAfterCommand( int kind, const cof_pickup_look_t *before, const cof_pickup_look_t *after )
{
	if( kind == COF_PICKUP_PILLS || kind == COF_PICKUP_ITEM ) return after->gone ? COF_PICKUP_TAKEN : COF_PICKUP_LYING;
	if( after->gone ) return COF_PICKUP_GONE;
	if( after->owner != before->owner || after->follow != before->follow || after->nextthink != before->nextthink )
		return COF_PICKUP_TAKEN;
	return COF_PICKUP_LYING;
}

// What the story frame sees of a pickup no command was seen taking: a gun on a player (taker: its edict number,
// 1..maxclients) was taken by them, however it got there; one gone was removed by the world (a script, the bonus
// room's lock), not taken; a falling gun's think changes nothing.
static int CoF_PickupAtFrame( const cof_pickup_look_t *look, int maxclients, int *taker )
{
	*taker = 0;
	if( look->gone ) return COF_PICKUP_GONE;
	if( look->owner >= 1 && look->owner <= maxclients )
	{
		*taker = look->owner;
		return COF_PICKUP_TAKEN;
	}
	if( look->owner || look->follow ) return COF_PICKUP_GONE;
	return COF_PICKUP_LYING;
}

// A block's place (classid: CoF_PickupCanonical), or a pickup-class entity's in the world (copy: it has the mark).
typedef struct { int classid, copy; float origin[3]; } cof_pickup_spot_t;

static int CoF_PickupNear( const cof_pickup_spot_t *block, const cof_pickup_spot_t *lying, float *distance )
{
	float dx = lying->origin[0] - block->origin[0];
	float dy = lying->origin[1] - block->origin[1];
	float dz = lying->origin[2] - block->origin[2];
	if( block->classid < 0 || block->classid != lying->classid ) return 0;
	if( dx > COF_PICKUP_NEAR || dx < -COF_PICKUP_NEAR || dy > COF_PICKUP_NEAR || dy < -COF_PICKUP_NEAR ||
		dz > COF_PICKUP_NEAR || dz < -COF_PICKUP_FALL ) return 0;
	*distance = dx * dx + dy * dy + dz * dz;
	return 1;
}

// Each block (at most COF_PICKUP_MAX) paired with the entity of its class lying where it put it, the nearest pairs
// first (two ammo boxes side by side each keep theirs): match[block] = the entity's index in lying, or -1. Of two at
// the same spot the copy is the block's: the other is the one just taken, removed a tenth of a second later.
static void CoF_PickupMatch( const cof_pickup_spot_t *blocks, int nblocks, const cof_pickup_spot_t *lying, int nlying, int *match )
{
	unsigned char paired[COF_PICKUP_MAX];
	int b, l;
	if( nblocks > COF_PICKUP_MAX ) nblocks = COF_PICKUP_MAX;
	if( nlying > COF_PICKUP_MAX ) nlying = COF_PICKUP_MAX;
	memset( paired, 0, sizeof( paired ));
	for( b = 0; b < nblocks; b++ ) match[b] = -1;
	for( ;; )
	{
		int best_block = -1, best_lying = -1;
		float best = 0.0f, distance = 0.0f;
		for( b = 0; b < nblocks; b++ )
		{
			if( match[b] >= 0 ) continue;
			for( l = 0; l < nlying; l++ )
				if( !paired[l] && CoF_PickupNear( &blocks[b], &lying[l], &distance ) && ( best_block < 0 || distance < best ||
					( distance == best && lying[l].copy && !lying[best_lying].copy )))
				{
					best_block = b;
					best_lying = l;
					best = distance;
				}
		}
		if( best_block < 0 ) return;
		match[best_block] = best_lying;
		paired[best_lying] = 1;
	}
}

// What a pickup is when its map starts (a new map, a revisit, a checkpoint). The map's own one lying (no copy mark)
// was taken by nobody in this world, whatever the ledger says: *forget drops its takers. A copy lying is hidden from
// the takers the ledger has (none known, after a Continue: from nobody). None lying waits for the humans who have not
// taken theirs, if someone took it and the world did not remove it. Otherwise there is nothing to hand out.
static int CoF_PickupResolve( int lying, int copy, int takers, int retired, int *forget )
{
	*forget = lying && !copy && ( takers > 0 || retired );
	if( lying ) return COF_PICKUP_LIVE;
	if( takers > 0 && !retired ) return COF_PICKUP_WAITING;
	return COF_PICKUP_OFF;
}

typedef struct
{
	int map, block;             // the ledger's map number, and the pickup's block in the map's entity text
	int retired;                // the world removed it, or no more copies may be made: nothing more is handed out
	cof_pickup_takers_t takers; // in the order they took it
	float origin[3], angles[3]; // where it lay: a copy for a late joiner is made there
	int saved, saved_retired;   // the takers and retired at the last checkpoint; saved -1: not known to it
} cof_pickup_entry_t;

typedef struct
{
	int maps, count;
	char map[COF_PICKUP_MAPS][COF_PICKUP_MAPNAME];
	cof_pickup_entry_t entries[COF_PICKUP_LEDGER];
	char saved_slot[COF_PICKUP_SLOTNAME]; // the checkpoint the saved fields belong to; "": none written this session
} cof_pickup_ledger_t;

static int CoF_PickupSameName( const char *a, const char *b )
{
	for( ; *a && *b; a++, b++ )
	{
		char x = *a, y = *b;
		if( x >= 'A' && x <= 'Z' ) x = (char)( x + 32 );
		if( y >= 'A' && y <= 'Z' ) y = (char)( y + 32 );
		if( x != y ) return 0;
	}
	return *a == *b;
}

// The ledger's number for a map file, or -1 (no name, or no room for another).
static int CoF_PickupLedgerMap( cof_pickup_ledger_t *ledger, const char *name, int create )
{
	size_t length;
	int i;
	if( !name || !name[0] ) return -1;
	length = strlen( name );
	if( length >= COF_PICKUP_MAPNAME ) return -1;
	for( i = 0; i < ledger->maps; i++ )
		if( CoF_PickupSameName( ledger->map[i], name )) return i;
	if( !create || ledger->maps >= COF_PICKUP_MAPS ) return -1;
	memcpy( ledger->map[ledger->maps], name, length + 1 );
	return ledger->maps++;
}

// The entry of a map's pickup, or -1 (none yet, or no room for another).
static int CoF_PickupLedgerFind( cof_pickup_ledger_t *ledger, int map, int block, int create )
{
	cof_pickup_entry_t *entry;
	int i;
	if( map < 0 ) return -1;
	for( i = 0; i < ledger->count; i++ )
		if( ledger->entries[i].map == map && ledger->entries[i].block == block ) return i;
	if( !create || ledger->count >= COF_PICKUP_LEDGER ) return -1;
	entry = &ledger->entries[ledger->count];
	memset( entry, 0, sizeof( *entry ));
	entry->map = map;
	entry->block = block;
	entry->saved = -1;
	return ledger->count++;
}

// A co-op checkpoint was written: what the ledger knows now is what that checkpoint's worlds hold (it bundles the
// current map and the snapshot of every map visited).
static void CoF_PickupLedgerSaved( cof_pickup_ledger_t *ledger, const char *slot )
{
	int i;
	for( i = 0; i < ledger->count; i++ )
	{
		ledger->entries[i].saved = ledger->entries[i].takers.count;
		ledger->entries[i].saved_retired = ledger->entries[i].retired;
	}
	ledger->saved_slot[0] = 0; // a slot name too long to keep: a load of it forgets
	if( slot && strlen( slot ) < COF_PICKUP_SLOTNAME ) memcpy( ledger->saved_slot, slot, strlen( slot ) + 1 );
}

// A co-op checkpoint was loaded. The one written last this session: the ledger goes back to what it knew then (the
// takes since were undone with the inventories). Another one, or Continue after a restart: it knows nothing, and a
// copy lying in that world is anyone's again (a player who had taken theirs before can take it once more). Returns 1
// when it went back.
static int CoF_PickupLedgerLoaded( cof_pickup_ledger_t *ledger, const char *slot )
{
	int i, kept = 0;
	if( !slot || !slot[0] || !ledger->saved_slot[0] || strcmp( slot, ledger->saved_slot ))
	{
		ledger->count = 0;
		ledger->saved_slot[0] = 0;
		return 0;
	}
	for( i = 0; i < ledger->count; i++ )
	{
		cof_pickup_entry_t entry = ledger->entries[i];
		if( entry.saved < 0 ) continue;
		if( entry.takers.count > entry.saved ) entry.takers.count = entry.saved;
		entry.retired = entry.saved_retired;
		ledger->entries[kept++] = entry;
	}
	ledger->count = kept;
	return 1;
}

#endif // SV_COF_PICKUPS_STATE_H
