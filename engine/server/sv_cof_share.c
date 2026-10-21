/* Cry of Fear story co-op: shared story items. GPL-3.0-or-later.
 * The engine side of sv_cof_share_state.h. It reads inventories through the
 * retail layer, attributes changes to the game's own client commands, and
 * hands items over or takes used-up copies away through the game's own code. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_share_state.h"
#include "sv_cof_share.h"

#if COF_RETAIL_SLOTS != COF_SHARE_SLOTS || COF_RETAIL_ITEM != COF_SHARE_NAME
#error the retail inventory and the ledger must agree on slots and name size
#endif

static cof_share_state_t cof_share;
static cof_share_archive_t cof_share_saved;
static int cof_share_command_slot = -1, cof_share_command_kind, cof_share_generation = -1;

void SV_CoF_ShareReset( void )
{
	CoF_ShareReset( &cof_share );
	memset( &cof_share_saved, 0, sizeof( cof_share_saved ));
	cof_share_command_slot = -1;
	cof_share_generation = -1;
}

static int CoF_ShareClientSlot( const sv_client_t *cl )
{
	int slot = (int)( cl - svs.clients );
	return ( slot >= 0 && slot < svs.maxclients && slot < COF_SHARE_PLAYERS ) ? slot : -1;
}

static qboolean CoF_ShareRead( sv_client_t *cl, char slots[COF_SHARE_SLOTS][COF_SHARE_NAME], int *alive )
{
	if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_RetailReadSlots( cl, slots )) return false;
	*alive = cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
	return true;
}

// 1.1.9 (I2): the counted items this player set down whose copy still lies in this map, for the next look. The
// game's inventorydrop leaves an item_key owned by the dropper, its message the descriptor and armorvalue 666 until
// it is taken (CBasePlayer::DropKeyItem; CKeyitemModel::KeyitemUse zeroes it and removes the key). Searched only
// while such an item is set down.
static void CoF_ShareLying( const sv_client_t *cl, int slot )
{
	cof_share_player_t *p = &cof_share.players[slot];
	int i, index, bit;
	p->lying = 0;
	for( i = 0; i < p->set_down.count; i++ )
	{
		if( !( bit = CoF_ShareCountedBit( p->set_down.items[i].name ))) continue;
		for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
		{
			const edict_t *ent = SV_EdictNum( index );
			if( !SV_IsValidEdict( ent ) || FBitSet( ent->v.flags, FL_KILLME ) || ent->v.owner != cl->edict || ent->v.armorvalue != 666.0f ||
				Q_strcmp( SV_GetString( ent->v.classname ), "item_key" ) || Q_strcmp( SV_GetString( ent->v.message ), p->set_down.items[i].name )) continue;
			p->lying |= bit;
			break;
		}
	}
}

static const char *CoF_ShareName( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( svs.clients[i].state >= cs_connected && svs.clients[i].userid == userid ) return svs.clients[i].name;
	return "Your partner";
}

static void CoF_ShareDrain( void )
{
	// Indexed by the COF_SHARE_* event kinds in sv_cof_share_state.h.
	static const char *phases[] = { "item-found", "item-used", "item-dropped", "item-delivered", "item-removed", "item-combined", "item-stalled" };
	int i, j;
	for( i = 0; i < cof_share.event_count; i++ )
	{
		const cof_share_event_t *e = &cof_share.events[i];
		char nice[COF_SHARE_NAME], text[160];
		Con_Printf( "cofstory phase=%s userid=%d item=%s\n", phases[e->kind], e->userid, e->name );
		if( e->kind != COF_SHARE_FOUND ) continue;
		SV_CoF_RetailItemName( e->name, nice, sizeof( nice ));
		Q_snprintf( text, sizeof( text ), "%s found the %s", CoF_ShareName( e->userid ), nice );
		for( j = 0; j < svs.maxclients; j++ )
			if( j != e->slot && svs.clients[j].state == cs_spawned ) SV_CoF_RetailProFont( &svs.clients[j], 5, text, true );
	}
	cof_share.event_count = 0;
}

// Before the game's ClientCommand: bring this player's look up to date, so a
// pickup made earlier in the same packet is not credited to the command.
void SV_CoF_ShareCommandBegin( sv_client_t *cl )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int alive, slot = CoF_ShareClientSlot( cl );
	cof_share_command_slot = -1;
	if( slot < 0 || !SV_CoF_StoryCoopEnabled() || !CoF_ShareRead( cl, slots, &alive )) return;
	CoF_ShareLying( cl, slot );
	CoF_ShareObserve( &cof_share, slot, cl->userid, alive, slots );
	cof_share_command_kind = CoF_ShareCommandKind( Cmd_Argv( 0 ));
	cof_share_command_slot = slot;
}

void SV_CoF_ShareCommandEnd( sv_client_t *cl )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int alive, slot = CoF_ShareClientSlot( cl );
	if( slot < 0 || slot != cof_share_command_slot ) return;
	cof_share_command_slot = -1;
	if( !CoF_ShareRead( cl, slots, &alive )) return;
	CoF_ShareLying( cl, slot );
	CoF_ShareCommand( &cof_share, slot, cof_share_command_kind, slots );
}

void SV_CoF_ShareFrame( void )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME], item[COF_SHARE_NAME];
	int i, alive, action;
	if( svs.spawncount != cof_share_generation )
	{
		CoF_ShareNewMap( &cof_share ); // sv.time restarted with the new map
		cof_share_generation = svs.spawncount;
	}
	for( i = 0; i < svs.maxclients && i < COF_SHARE_PLAYERS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT )) CoF_ShareObserve( &cof_share, i, 0, 0, NULL );
		else if( CoF_ShareRead( cl, slots, &alive ))
		{
			int entry, fresh = cof_share.players[i].userid != cl->userid;
			for( entry = 0; fresh && entry < COF_SHARE_PLAYERS; entry++ )
				if( !Q_stricmp( cof_share_saved.names[entry], cl->name ))
				{
					SV_CoF_ShareRecovered( cl );
					CoF_ShareRead( cl, slots, &alive );
					break;
				}
			CoF_ShareLying( cl, i );
			CoF_ShareObserve( &cof_share, i, cl->userid, alive, slots );
		}
		// connected but not spawned (joining, or mid level change): keep what we know
	}
	CoF_ShareDrain();
	for( i = 0; i < svs.maxclients && i < COF_SHARE_PLAYERS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned ) continue;
		action = CoF_SharePlan( &cof_share, i, SV_CoF_RetailFreeSlots( cl ), sv.time, item );
		if( action == COF_SHARE_GIVE )
		{
			if( !SV_CoF_RetailGiveItem( cl, item )) Con_Printf( "cofstory phase=item-deferred userid=%d item=%s\n", cl->userid, item );
			CoF_ShareIssued( &cof_share, i, action, item, sv.time ); // confirmed only by the inventory
		}
		else if( action == COF_SHARE_TAKE )
		{
			SV_CoF_RetailRemoveItem( cl, item );
			CoF_ShareIssued( &cof_share, i, action, item, sv.time );
		}
		else if( action == COF_SHARE_ROOM )
		{
			char nice[COF_SHARE_NAME], text[160];
			SV_CoF_RetailItemName( item, nice, sizeof( nice ));
			Q_snprintf( text, sizeof( text ), "%s found the %s. Make room to carry it.", CoF_ShareName( CoF_ShareFinder( &cof_share, item )), nice );
			SV_CoF_RetailProFont( cl, 5, text, true );
			Con_Printf( "cofstory phase=item-deferred userid=%d item=%s reason=inventory-full\n", cl->userid, item );
		}
		// 1.1.9 (I1): the give and the take run the game's own code at once, so the inventory shows now whether they
		// landed. Settled by this look, a pickup made before the next one is a change of its own: left to that look,
		// a fuse found in the frame after a take of one read as no change, and the take, retried 5 s later as
		// stalled, removed the fuse found (the party never counted it).
		if(( action == COF_SHARE_GIVE || action == COF_SHARE_TAKE ) && CoF_ShareRead( cl, slots, &alive ))
		{
			CoF_ShareLying( cl, i );
			CoF_ShareObserve( &cof_share, i, cl->userid, alive, slots );
		}
	}
}

void SV_CoF_ShareRemember( void )
{
	SV_CoF_ShareExport( &cof_share_saved );
}

void SV_CoF_ShareRecovered( sv_client_t *cl )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME];
	int alive, index, slot = CoF_ShareClientSlot( cl );
	if( slot < 0 || !CoF_ShareRead( cl, slots, &alive )) return;
	for( index = 0; index < COF_SHARE_SLOTS; index++ )
		if( CoF_IsStoryItem( slots[index] ) && CoF_ShareSetFind( &cof_share.party, slots[index] ) < 0 )
			SV_CoF_RetailRemoveItem( cl, slots[index] );
	if( !CoF_ShareRead( cl, slots, &alive )) return;
	cof_share.players[slot].userid = cl->userid;
	cof_share.players[slot].alive = alive;
	// 1.1.8: the restore replaced the whole inventory, so a give or take issued before it (a rejoiner served while
	// a level change still held the restore back) can no longer land: left in flight, the next fuse picked up
	// read as that give, and the plan then took it away again.
	cof_share.players[slot].pending = COF_SHARE_NONE;
	CoF_ShareStoryOnly( cof_share.players[slot].held, slots );
	for( index = 0; index < COF_SHARE_PLAYERS; index++ )
		if( !Q_stricmp( cof_share_saved.names[index], cl->name )) cof_share.players[slot].set_down = cof_share_saved.dropped[index];
}

void SV_CoF_ShareExport( cof_share_archive_t *archive )
{
	int slot, entry;
	*archive = cof_share_saved;
	archive->party = cof_share.party;
	for( slot = 0; slot < svs.maxclients && slot < COF_SHARE_PLAYERS; slot++ )
	{
		sv_client_t *client = &svs.clients[slot];
		if( client->state < cs_connected || !client->name[0] || cof_share.players[slot].userid != client->userid ) continue;
		for( entry = 0; entry < COF_SHARE_PLAYERS; entry++ ) if( !Q_stricmp( archive->names[entry], client->name )) break;
		if( entry == COF_SHARE_PLAYERS )
			for( entry = 0; entry < COF_SHARE_PLAYERS; entry++ ) if( !archive->names[entry][0] ) break;
		if( entry == COF_SHARE_PLAYERS ) continue;
		Q_strncpy( archive->names[entry], client->name, sizeof( archive->names[entry] ));
		archive->dropped[entry] = cof_share.players[slot].set_down;
	}
	cof_share_saved = *archive;
}

qboolean SV_CoF_ShareImport( const cof_share_archive_t *archive )
{
	if( !archive || !CoF_ShareArchiveValid( archive )) return false;
	SV_CoF_ShareReset();
	cof_share_saved = *archive;
	cof_share.party = archive->party;
	return true;
}

static void CoF_ShareLedger_f( void )
{
	int i, j;
	Con_Printf( "cofstory phase=ledger items=%d\n", cof_share.party.count );
	for( i = 0; i < cof_share.party.count; i++ )
		Con_Printf( "cofstory phase=ledger-item item=%s finder=%d\n", cof_share.party.items[i].name, cof_share.party.items[i].finder );
	for( i = 0; i < COF_SHARE_PLAYERS; i++ )
		for( j = 0; j < cof_share.players[i].set_down.count; j++ )
			Con_Printf( "cofstory phase=ledger-set-down userid=%d item=%s\n", cof_share.players[i].userid, cof_share.players[i].set_down.items[j].name );
}

void SV_CoF_ShareInit( void )
{
	Cmd_AddCommand( "cof_story_ledger", CoF_ShareLedger_f, "story co-op: list the party's shared story items" );
}
