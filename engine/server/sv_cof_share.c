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
static int cof_share_command_slot = -1, cof_share_command_kind;

void SV_CoF_ShareReset( void )
{
	CoF_ShareReset( &cof_share );
	cof_share_command_slot = -1;
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

static const char *CoF_ShareName( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( svs.clients[i].state >= cs_connected && svs.clients[i].userid == userid ) return svs.clients[i].name;
	return "Your partner";
}

static void CoF_ShareDrain( void )
{
	static const char *phases[] = { "item-found", "item-used", "item-dropped", "item-delivered", "item-removed" };
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
	if( CoF_ShareRead( cl, slots, &alive )) CoF_ShareCommand( &cof_share, slot, cof_share_command_kind, slots );
}

void SV_CoF_ShareFrame( void )
{
	char slots[COF_SHARE_SLOTS][COF_SHARE_NAME], item[COF_SHARE_NAME];
	int i, alive, action;
	for( i = 0; i < svs.maxclients && i < COF_SHARE_PLAYERS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state < cs_connected || FBitSet( cl->flags, FCL_FAKECLIENT )) CoF_ShareObserve( &cof_share, i, 0, 0, NULL );
		else if( CoF_ShareRead( cl, slots, &alive )) CoF_ShareObserve( &cof_share, i, cl->userid, alive, slots );
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
	}
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
