/*
sv_cof_pickups.c - Cry of Fear story co-op: per-player map pickups (the rules: sv_cof_pickups_state.h)
GPL-3.0-or-later.

1.1.9 (open items I8, I9): every human takes their own copy of each weapon, ammo box and medicine the map placed.
- A map's first story frame finds its pickups: the blocks of its entity text that become per-player, each paired
  with the pickup lying where the block put it, and what the session ledger knows of each (CoF_PickupsFind).
- Right after each player's command (SV_RunCmd) SV_CoF_PickupsCommand notes a take that command made.
- The story frame settles it (the taker joins the ledger) and, while a human present has not taken theirs (a late
  joiner too), makes a copy from the same block at the same spot with the copy mark. A copy is not sent to the players
  who took one (SV_CoF_PickupsVisible) and their +use does not find it (SV_CoF_PickupsBlocked); an AI companion finds
  no map pickup at all.
- The ledger lives through level changes (a revisit hands nothing out twice) and goes back with a checkpoint
  (SV_CoF_PickupsCheckpointSaved/Loaded).
- The bonus room's items a chapter start lays out (sv_cof_bonus.c, sv_cof_bonus_kit.h) are per-player too: found by
  their bonus mark (or, all taken, by the ledger), each with a slot of its own after the map's; a copy gets the kit's
  keys (SV_CoF_PickupsSpawnBonus, which also lays the kit out).
cof_coop_pickups 0: the retail multiplayer rules, the first player takes the only one.
Logs: phase=pickups (once a map), phase=pickup-taken, phase=pickup-copy, phase=pickup-gone, phase=pickups-checkpoint.
*/
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_pickups.h"
#include "sv_cof_pickups_state.h"
#include "sv_cof_bonus_kit.h"

static CVAR_DEFINE_AUTO( cof_coop_pickups, "1", 0, "Cry of Fear story co-op: every player takes their own copy of each weapon, ammo box and medicine the map placed; 0: the first player takes the only one (the retail multiplayer rules)" );

#define COF_PICKUPS_RETRY 5.0 // a copy that would not spawn is tried again after this long
#define COF_PICKUPS_TRIES 3   // and given up after this many tries
// Edicts a copy never takes: SV_AllocEdict ends the game when none is free (a merged map holds two or three maps'
// entities); the margin is the retail multiplayer respawn's own (ENTITY_INTOLERANCE, FlWeaponTryRespawn).
#define COF_PICKUPS_ROOM 100

typedef struct
{
	int offset;              // the block's place in sv.worldmodel->entities
	int block;               // its number there: the ledger's key
	int row;                 // in cof_pickup_classes (-1: the kit's night vision)
	int kind;                // COF_PICKUP_WEAPON, AMMO, PILLS or ITEM
	int bonus;               // 0: a block of the map's text; else the bonus kit's item + 1 (offset unused)
	char classname[32];      // as the block names it
	int state;               // COF_PICKUP_OFF, LIVE, WAITING or RETIRED
	int entity, serial;      // the pickup lying, while LIVE
	cof_pickup_look_t look;  // as the last frame or command left it
	vec3_t origin, angles;   // where it lies
	int entry;               // its ledger entry; -1: nobody took it yet
	int taken, taker_bot, taker_userid; // a take a command saw, for the frame to settle
	unsigned int taker;
	int tries, waited;       // copies that failed; told once that it waits for room
	double retry;
} cof_pickups_slot_t;

static struct
{
	int generation; // svs.spawncount of the map whose pickups were found; -1: none
	int map;        // its ledger number; -1: the ledger has no room for it (its takes make no copies)
	int count;
	cof_pickups_slot_t slots[COF_PICKUP_MAX];
	byte index[MAX_EDICTS]; // edict number -> slot + 1, while that slot's pickup lies there
} cof_pickups;
static cof_pickup_ledger_t cof_pickups_ledger;
static const cof_pickup_takers_t cof_pickups_nobody = { 0 }; // the takers of a pickup nobody took yet

// The map's pickups (not the ledger): found again at the next story frame.
static void CoF_PickupsForget( void )
{
	memset( &cof_pickups, 0, sizeof( cof_pickups ));
	cof_pickups.generation = -1;
	cof_pickups.map = -1;
}

void SV_CoF_PickupsReset( void )
{
	CoF_PickupsForget();
	memset( &cof_pickups_ledger, 0, sizeof( cof_pickups_ledger ));
}

void SV_CoF_PickupsInit( void )
{
	Cvar_RegisterVariable( &cof_coop_pickups );
	SV_CoF_PickupsReset();
}

qboolean SV_CoF_PickupsEnabled( void )
{
	return cof_coop_pickups.value != 0.0f;
}

// The slot of a pickup lying as this entity, or NULL: called for every entity sent and searched, so cheap.
static cof_pickups_slot_t *CoF_PickupsAt( const edict_t *ent )
{
	cof_pickups_slot_t *slot;
	int e;
	if( !ent || cof_pickups.generation != svs.spawncount ) return NULL;
	e = NUM_FOR_EDICT( ent );
	if( e <= 0 || e >= MAX_EDICTS || !cof_pickups.index[e] ) return NULL;
	slot = &cof_pickups.slots[cof_pickups.index[e] - 1];
	if( slot->state != COF_PICKUP_LIVE || slot->entity != e || slot->serial != ent->serialnumber ) return NULL;
	return slot;
}

static const cof_pickup_takers_t *CoF_PickupsTakers( const cof_pickups_slot_t *slot )
{
	return slot->entry >= 0 ? &cof_pickups_ledger.entries[slot->entry].takers : &cof_pickups_nobody;
}

qboolean SV_CoF_PickupsVisible( const edict_t *viewer, const edict_t *ent )
{
	const cof_pickups_slot_t *slot = CoF_PickupsAt( ent );
	const sv_client_t *cl;
	if( !slot || slot->entry < 0 ) return true;
	cl = SV_ClientFromEdict( viewer, true );
	return !cl || !CoF_PickupHidden( CoF_PickupsTakers( slot ), CoF_PickupName( cl->name ));
}

qboolean SV_CoF_PickupsBlocked( const edict_t *user, const edict_t *ent )
{
	const cof_pickups_slot_t *slot = CoF_PickupsAt( ent );
	const sv_client_t *cl;
	if( !slot ) return false;
	cl = SV_ClientFromEdict( user, true );
	if( !cl ) return false;
	return CoF_PickupRefused( CoF_PickupsTakers( slot ), CoF_PickupName( cl->name ), FBitSet( cl->flags, FCL_FAKECLIENT ) != 0 );
}

static void CoF_PickupsLook( const cof_pickups_slot_t *slot, cof_pickup_look_t *look )
{
	edict_t *ent = SV_EdictNum( slot->entity );
	memset( look, 0, sizeof( *look ));
	if( !ent || ent->free || ent->serialnumber != slot->serial || FBitSet( ent->v.flags, FL_KILLME ))
	{
		look->gone = 1;
		return;
	}
	look->owner = SV_IsValidEdict( ent->v.owner ) ? NUM_FOR_EDICT( ent->v.owner ) : 0;
	look->follow = ent->v.movetype == MOVETYPE_FOLLOW;
	look->nextthink = ent->v.nextthink;
}

static void CoF_PickupsUnlist( cof_pickups_slot_t *slot )
{
	if( slot->entity > 0 && slot->entity < MAX_EDICTS && cof_pickups.index[slot->entity] == (int)( slot - cof_pickups.slots ) + 1 )
		cof_pickups.index[slot->entity] = 0;
	slot->entity = slot->serial = 0;
}

static void CoF_PickupsList( cof_pickups_slot_t *slot, edict_t *ent )
{
	slot->state = COF_PICKUP_LIVE;
	slot->entity = NUM_FOR_EDICT( ent );
	slot->serial = ent->serialnumber;
	VectorCopy( ent->v.origin, slot->origin );
	VectorCopy( ent->v.angles, slot->angles );
	CoF_PickupsLook( slot, &slot->look );
	if( slot->entity > 0 && slot->entity < MAX_EDICTS ) cof_pickups.index[slot->entity] = (byte)(( slot - cof_pickups.slots ) + 1 );
}

void SV_CoF_PickupsCommand( sv_client_t *cl )
{
	int i;
	if( !cl || cof_pickups.generation != svs.spawncount ) return;
	for( i = 0; i < cof_pickups.count; i++ )
	{
		cof_pickups_slot_t *slot = &cof_pickups.slots[i];
		cof_pickup_look_t look;
		if( slot->state != COF_PICKUP_LIVE || slot->taken ) continue;
		CoF_PickupsLook( slot, &look );
		if( CoF_PickupAfterCommand( slot->kind, &slot->look, &look ) != COF_PICKUP_TAKEN )
		{
			if( !look.gone ) slot->look = look; // gone: the frame retires it
			continue;
		}
		slot->taken = 1;
		slot->taker = CoF_PickupName( cl->name );
		slot->taker_bot = FBitSet( cl->flags, FCL_FAKECLIENT ) != 0;
		slot->taker_userid = cl->userid;
	}
}

static void CoF_PickupsGone( cof_pickups_slot_t *slot, const char *why )
{
	CoF_PickupsUnlist( slot );
	slot->state = COF_PICKUP_RETIRED;
	if( slot->entry >= 0 ) cof_pickups_ledger.entries[slot->entry].retired = 1;
	Con_Printf( "cofstory phase=pickup-gone class=%s block=%d reason=%s\n", slot->classname, slot->block, why );
}

static void CoF_PickupsSettle( cof_pickups_slot_t *slot, unsigned int name, int bot, int userid )
{
	cof_pickup_entry_t *entry;
	if( slot->entry < 0 ) slot->entry = CoF_PickupLedgerFind( &cof_pickups_ledger, cof_pickups.map, slot->block, true );
	CoF_PickupsUnlist( slot );
	slot->taken = 0;
	if( slot->entry < 0 )
	{
		slot->state = COF_PICKUP_RETIRED;
		Con_Printf( "cofstory phase=pickup-taken userid=%d class=%s block=%d copies=no reason=ledger-full\n", userid, slot->classname, slot->block );
		return;
	}
	entry = &cof_pickups_ledger.entries[slot->entry];
	VectorCopy( slot->origin, entry->origin );
	VectorCopy( slot->angles, entry->angles );
	if( !CoF_PickupSettle( &entry->takers, name, bot ))
	{
		entry->retired = 1;
		slot->state = COF_PICKUP_RETIRED;
		Con_Printf( "cofstory phase=pickup-taken userid=%d class=%s block=%d copies=no reason=%s\n", userid, slot->classname, slot->block,
			entry->takers.count >= COF_PICKUP_TAKERS ? "takers-full" : "taken-again" );
		return;
	}
	slot->state = COF_PICKUP_WAITING;
	slot->tries = 0;
	slot->retry = 0.0;
	Con_Printf( "cofstory phase=pickup-taken userid=%d class=%s block=%d companion=%d takers=%d\n", userid, slot->classname, slot->block, bot, entry->takers.count );
}

// One of the bonus room's items (sv_cof_bonus_kit.h) at this spot, made as the map makes the room's: the kit's keys
// through the game's KeyValue, the bonus mark (and the copy mark for a copy), then its Spawn. NULL when the game did
// not make it (an item with no floor under it removes itself, CItem::Spawn).
edict_t *SV_CoF_PickupsSpawnBonus( int item, const vec3_t origin, const vec3_t angles, qboolean copy )
{
	char key[64], value[64], classname[32];
	const cof_bonus_kit_item_t *kit;
	edict_t *ent;
	int i;
	if( item < 0 || item >= COF_BONUS_KIT_ITEMS || !svgame.dllFuncs.pfnKeyValue || !svgame.dllFuncs.pfnSpawn ) return NULL;
	kit = &cof_bonus_kit[item];
	Q_strncpy( classname, kit->classname, sizeof( classname ));
	ent = SV_CreateNamedEntity( NULL, SV_AllocString( classname ));
	if( !SV_IsValidEdict( ent )) return NULL;
	if( !ent->pvPrivateData )
	{
		SV_FreeEdict( ent );
		return NULL;
	}
	for( i = 0; i + 1 < COF_BONUS_KIT_KEYS && kit->keys[i] && kit->keys[i + 1]; i += 2 )
	{
		KeyValueData kvd;
		Q_strncpy( key, kit->keys[i], sizeof( key ));
		Q_strncpy( value, kit->keys[i + 1], sizeof( value ));
		kvd.szClassName = classname;
		kvd.szKeyName = key;
		kvd.szValue = value;
		kvd.fHandled = false;
		svgame.dllFuncs.pfnKeyValue( ent, &kvd );
	}
	VectorCopy( origin, ent->v.origin );
	VectorCopy( angles, ent->v.angles );
	SetBits( ent->v.spawnflags, COF_PICKUP_BONUS_FLAG );
	if( copy ) SetBits( ent->v.spawnflags, COF_PICKUP_COPY_FLAG );
	if( svgame.dllFuncs.pfnSpawn( ent ) == -1 || ent->free || FBitSet( ent->v.flags, FL_KILLME ))
	{
		if( !ent->free ) SV_FreeEdict( ent );
		return NULL;
	}
	SV_LinkEdict( ent, false );
	return ent;
}

// The same pickup again: the block's own keys through the game's KeyValue (as SV_ParseEdict hands them, the
// classname first), at the spot the taken one lay, with the copy mark, then its Spawn (SV_CoF_SpawnMapEntity does
// the same for a block found by its name; a pickup has none). A bonus kit item's: the kit's keys.
static edict_t *CoF_PickupsSpawn( const cof_pickups_slot_t *slot )
{
	char key[256], value[2048], classname[32];
	char *cursor;
	edict_t *ent;
	if( slot->bonus ) return SV_CoF_PickupsSpawnBonus( slot->bonus - 1, slot->origin, slot->angles, true );
	if( !sv.worldmodel || !sv.worldmodel->entities ) return NULL;
	cursor = sv.worldmodel->entities + slot->offset;
	if(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) == NULL || key[0] != '{' ) return NULL;
	Q_strncpy( classname, slot->classname, sizeof( classname ));
	ent = SV_CreateNamedEntity( NULL, SV_AllocString( classname ));
	if( !SV_IsValidEdict( ent )) return NULL;
	if( !ent->pvPrivateData )
	{
		SV_FreeEdict( ent );
		return NULL;
	}
	while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
	{
		KeyValueData kvd;
		size_t length;
		if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
		for( length = strlen( key ); length > 0 && key[length - 1] == ' '; length-- ) key[length - 1] = 0;
		if( !key[0] || !value[0] || !Q_strcmp( key, "classname" ) || !Q_strcmp( key, "origin" ) || !Q_strcmp( key, "angle" ) || !Q_strcmp( key, "angles" ))
			continue;
		kvd.szClassName = classname;
		kvd.szKeyName = key;
		kvd.szValue = value;
		kvd.fHandled = false;
		svgame.dllFuncs.pfnKeyValue( ent, &kvd );
	}
	if( !cursor )
	{
		SV_FreeEdict( ent );
		return NULL;
	}
	VectorCopy( slot->origin, ent->v.origin );
	VectorCopy( slot->angles, ent->v.angles );
	SetBits( ent->v.spawnflags, COF_PICKUP_COPY_FLAG );
	if( svgame.dllFuncs.pfnSpawn( ent ) == -1 || ent->free || FBitSet( ent->v.flags, FL_KILLME ))
	{
		if( !ent->free ) SV_FreeEdict( ent );
		return NULL;
	}
	SV_LinkEdict( ent, false );
	return ent;
}

static void CoF_PickupsCopy( cof_pickups_slot_t *slot )
{
	edict_t *ent;
	if( sv.time < slot->retry ) return;
	if( svgame.numEntities >= GI->max_edicts - COF_PICKUPS_ROOM )
	{
		slot->retry = sv.time + COF_PICKUPS_RETRY;
		if( !slot->waited++ ) Con_Printf( "cofstory phase=pickup-copy class=%s block=%d result=no-room entities=%d\n", slot->classname, slot->block, svgame.numEntities );
		return;
	}
	if(( ent = CoF_PickupsSpawn( slot )) == NULL )
	{
		slot->retry = sv.time + COF_PICKUPS_RETRY;
		if( ++slot->tries >= COF_PICKUPS_TRIES ) CoF_PickupsGone( slot, "no-copy" );
		else Con_Printf( "cofstory phase=pickup-copy class=%s block=%d result=failed\n", slot->classname, slot->block );
		return;
	}
	slot->tries = 0;
	CoF_PickupsList( slot, ent );
	Con_Printf( "cofstory phase=pickup-copy class=%s block=%d entity=%d takers=%d\n", slot->classname, slot->block, slot->entity, CoF_PickupsTakers( slot )->count );
}

// Held: carried by someone (a gun in an inventory), not lying in the world.
static qboolean CoF_PickupsHeld( const edict_t *ent )
{
	return ent->v.movetype == MOVETYPE_FOLLOW || ( SV_IsValidEdict( ent->v.owner ) && ent->v.owner != svgame.edicts ) ||
		( SV_IsValidEdict( ent->v.aiment ) && ent->v.aiment != svgame.edicts );
}

// The bonus kit's items of this map (sv_cof_bonus_kit.h): one lying with the bonus mark (the first of each, should a
// kit laid out with the pickups off hold several), or one the ledger knows was taken here; resolved as the map's own
// (the one the chapter start laid out is the "map's", its copies have the copy mark). Their slots come after the map's.
// Returns how many.
static int CoF_PickupsFindBonus( int *states, int *copies, int *skipped )
{
	int lying[COF_BONUS_KIT_ITEMS], item, e, found = 0;
	memset( lying, 0, sizeof( lying ));
	for( e = svs.maxclients + 1; e < svgame.numEntities; e++ )
	{
		edict_t *ent = SV_EdictNum( e );
		if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ) || !FBitSet( ent->v.spawnflags, COF_PICKUP_BONUS_FLAG ) ||
			CoF_PickupsHeld( ent )) continue;
		if(( item = CoF_BonusKitItem( SV_GetString( ent->v.classname ))) < 0 || lying[item] ) continue;
		lying[item] = e;
	}
	for( item = 0; item < COF_BONUS_KIT_ITEMS; item++ )
	{
		int entry = CoF_PickupLedgerFind( &cof_pickups_ledger, cof_pickups.map, COF_PICKUP_BONUS_BLOCK + item, false ), forget = 0;
		cof_pickup_entry_t *known = entry >= 0 ? &cof_pickups_ledger.entries[entry] : NULL;
		edict_t *ent = lying[item] ? SV_EdictNum( lying[item] ) : NULL;
		qboolean copy = ent && FBitSet( ent->v.spawnflags, COF_PICKUP_COPY_FLAG );
		cof_pickups_slot_t *slot;
		if( !ent && !known ) continue;
		if( cof_pickups.count >= COF_PICKUP_MAX )
		{
			( *skipped )++;
			continue;
		}
		slot = &cof_pickups.slots[cof_pickups.count++];
		slot->offset = -1;
		slot->block = COF_PICKUP_BONUS_BLOCK + item;
		slot->row = CoF_PickupClass( cof_bonus_kit[item].classname );
		slot->kind = cof_bonus_kit[item].item ? COF_PICKUP_ITEM : COF_PICKUP_WEAPON;
		slot->bonus = item + 1;
		slot->entry = entry;
		Q_strncpy( slot->classname, cof_bonus_kit[item].classname, sizeof( slot->classname ));
		slot->state = CoF_PickupResolve( ent != NULL, copy, known ? known->takers.count : 0, known ? known->retired : 0, &forget );
		if( forget && known )
		{
			// the one the chapter start laid out lies here, so this world never saw it taken
			known->takers.count = 0;
			known->retired = 0;
			known->saved = -1;
		}
		if( slot->state == COF_PICKUP_LIVE )
		{
			CoF_PickupsList( slot, ent );
			if( copy ) ( *copies )++;
		}
		else if( slot->state == COF_PICKUP_WAITING && known )
		{
			VectorCopy( known->origin, slot->origin );
			VectorCopy( known->angles, slot->angles );
		}
		states[slot->state]++;
		found++;
	}
	return found;
}

// A map's pickups: its entity text's per-player blocks, the pickups lying in the world, paired, and the ledger.
static void CoF_PickupsFind( void )
{
	cof_pickup_spot_t blocks[COF_PICKUP_MAX], lying[COF_PICKUP_MAX];
	int lying_entity[COF_PICKUP_MAX], match[COF_PICKUP_MAX];
	int states[4] = { 0, 0, 0, 0 };
	char key[256], value[2048];
	char *text, *cursor;
	int block, nlying = 0, copies = 0, skipped = 0, bonus, e, i;

	CoF_PickupsForget();
	cof_pickups.generation = svs.spawncount;
	cof_pickups.map = CoF_PickupLedgerMap( &cof_pickups_ledger, sv.name, true );
	text = cursor = sv.worldmodel->entities;
	for( block = 0; cursor; block++ )
	{
		char classname[64], *start = cursor;
		vec3_t origin = { 0.0f, 0.0f, 0.0f };
		int row, named = 0, fires = 0, touch = 0, placed = 0;
		cof_pickups_slot_t *slot;
		if(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) == NULL || key[0] != '{' ) break;
		classname[0] = 0;
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
			if( !value[0] ) continue;
			if( !Q_strcmp( key, "classname" ))
			{
				if( !classname[0] ) Q_strncpy( classname, value, sizeof( classname ));
			}
			else if( !Q_strcmp( key, "origin" )) placed = CoF_PickupParseOrigin( value, origin );
			else if( !Q_strcmp( key, "targetname" )) named = 1;
			else if( !Q_strcmp( key, "target" ) || !Q_strcmp( key, "killtarget" )) fires = 1;
			else if( !Q_strcmp( key, "iuser4" )) touch = 1;
		}
		if( !cursor ) break;
		row = CoF_PickupClass( classname );
		if( !placed || !CoF_PickupEligible( row, named, fires, touch )) continue;
		if( cof_pickups.count >= COF_PICKUP_MAX )
		{
			skipped++;
			continue;
		}
		slot = &cof_pickups.slots[cof_pickups.count];
		slot->offset = (int)( start - text );
		slot->block = block;
		slot->row = row;
		slot->kind = cof_pickup_classes[row].kind;
		slot->entry = -1;
		Q_strncpy( slot->classname, classname, sizeof( slot->classname ));
		blocks[cof_pickups.count].classid = CoF_PickupCanonical( row );
		blocks[cof_pickups.count].copy = 0;
		VectorCopy( origin, blocks[cof_pickups.count].origin );
		cof_pickups.count++;
	}

	for( e = svs.maxclients + 1; e < svgame.numEntities && nlying < COF_PICKUP_MAX; e++ )
	{
		edict_t *ent = SV_EdictNum( e );
		int row;
		// the bonus kit's are no block's (CoF_PickupsFindBonus)
		if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ) || FBitSet( ent->v.spawnflags, COF_PICKUP_BONUS_FLAG ) ||
			CoF_PickupsHeld( ent )) continue;
		if(( row = CoF_PickupClass( SV_GetString( ent->v.classname ))) < 0 ) continue;
		lying[nlying].classid = CoF_PickupCanonical( row );
		lying[nlying].copy = FBitSet( ent->v.spawnflags, COF_PICKUP_COPY_FLAG ) != 0;
		VectorCopy( ent->v.origin, lying[nlying].origin );
		lying_entity[nlying] = e;
		nlying++;
	}
	CoF_PickupMatch( blocks, cof_pickups.count, lying, nlying, match );

	for( i = 0; i < cof_pickups.count; i++ )
	{
		cof_pickups_slot_t *slot = &cof_pickups.slots[i];
		cof_pickup_entry_t *entry;
		int l = match[i], forget = 0;
		slot->entry = CoF_PickupLedgerFind( &cof_pickups_ledger, cof_pickups.map, slot->block, false );
		entry = slot->entry >= 0 ? &cof_pickups_ledger.entries[slot->entry] : NULL;
		slot->state = CoF_PickupResolve( l >= 0, l >= 0 && lying[l].copy, entry ? entry->takers.count : 0, entry ? entry->retired : 0, &forget );
		if( forget && entry )
		{
			// the map's own pickup lies here, so this world never saw it taken: a ledger from another one
			entry->takers.count = 0;
			entry->retired = 0;
			entry->saved = -1;
		}
		if( slot->state == COF_PICKUP_LIVE )
		{
			CoF_PickupsList( slot, SV_EdictNum( lying_entity[l] ));
			if( lying[l].copy ) copies++;
		}
		else if( slot->state == COF_PICKUP_WAITING && entry )
		{
			VectorCopy( entry->origin, slot->origin );
			VectorCopy( entry->angles, slot->angles );
		}
		states[slot->state]++;
	}
	bonus = CoF_PickupsFindBonus( states, &copies, &skipped );
	Con_Printf( "cofstory phase=pickups map=%s found=%d lying=%d copies=%d waiting=%d none=%d skipped=%d ledger=%d bonus=%d\n", sv.name,
		cof_pickups.count, states[COF_PICKUP_LIVE], copies, states[COF_PICKUP_WAITING], states[COF_PICKUP_OFF], skipped, cof_pickups_ledger.count, bonus );
}

static qboolean CoF_PickupsOn( void )
{
	return cof_coop_pickups.value != 0.0f && sv.state == ss_active && sv.worldmodel && sv.worldmodel->entities && SV_CoF_StoryCoopEnabled();
}

void SV_CoF_PickupsFrame( void )
{
	unsigned int present[MAX_CLIENTS];
	int i, count = 0;
	if( !CoF_PickupsOn( ))
	{
		// off (the host's choice): the copies lying stay as ordinary pickups, the retail rules from now on
		if( cof_pickups.generation != -1 ) CoF_PickupsForget();
		return;
	}
	if( cof_pickups.generation != svs.spawncount ) CoF_PickupsFind();
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		const sv_client_t *cl = &svs.clients[i];
		if( cl->state == cs_spawned && !FBitSet( cl->flags, FCL_FAKECLIENT ) && SV_IsValidEdict( cl->edict ))
			present[count++] = CoF_PickupName( cl->name );
	}
	for( i = 0; i < cof_pickups.count; i++ )
	{
		cof_pickups_slot_t *slot = &cof_pickups.slots[i];
		if( slot->state == COF_PICKUP_LIVE && slot->taken )
			CoF_PickupsSettle( slot, slot->taker, slot->taker_bot, slot->taker_userid );
		else if( slot->state == COF_PICKUP_LIVE )
		{
			cof_pickup_look_t look;
			int taker = 0, seen;
			CoF_PickupsLook( slot, &look );
			seen = CoF_PickupAtFrame( &look, svs.maxclients, &taker );
			if( seen == COF_PICKUP_TAKEN && taker <= MAX_CLIENTS )
			{
				// on a player although no command of theirs was seen taking it
				const sv_client_t *owner = &svs.clients[taker - 1];
				CoF_PickupsSettle( slot, CoF_PickupName( owner->name ), FBitSet( owner->flags, FCL_FAKECLIENT ) != 0, owner->userid );
			}
			else if( seen != COF_PICKUP_LYING ) CoF_PickupsGone( slot, "removed" );
			else
			{
				edict_t *ent = SV_EdictNum( slot->entity );
				slot->look = look;
				VectorCopy( ent->v.origin, slot->origin );
				VectorCopy( ent->v.angles, slot->angles );
			}
		}
		if( slot->state == COF_PICKUP_WAITING && CoF_PickupWanted( CoF_PickupsTakers( slot ), present, count ))
			CoF_PickupsCopy( slot );
	}
}

void SV_CoF_PickupsCheckpointSaved( const char *slot )
{
	CoF_PickupLedgerSaved( &cof_pickups_ledger, slot );
	Con_Printf( "cofstory phase=pickups-checkpoint saved=%s entries=%d\n", slot ? slot : "", cof_pickups_ledger.count );
}

void SV_CoF_PickupsCheckpointLoaded( const char *slot )
{
	qboolean back = CoF_PickupLedgerLoaded( &cof_pickups_ledger, slot );
	CoF_PickupsForget(); // the loaded map's pickups are found again at its first story frame
	Con_Printf( "cofstory phase=pickups-checkpoint loaded=%s ledger=%s entries=%d\n", slot ? slot : "", back ? "back" : "forgotten", cof_pickups_ledger.count );
}
