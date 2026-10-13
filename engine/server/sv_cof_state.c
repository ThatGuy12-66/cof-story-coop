#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_state.h"

static cof_state_data_t cof_state;
static struct
{
	int userid, known, alive;
	cof_story_values_t previous;
	int notes[COF_STATE_NOTES];
} cof_state_players[MAX_CLIENTS];
static qboolean cof_state_sending;
static edict_t *cof_state_document;
static int cof_state_document_serial, cof_state_reader;

void SV_CoF_StateReset( void )
{
	memset( &cof_state, 0, sizeof( cof_state ));
	memset( cof_state_players, 0, sizeof( cof_state_players ));
	cof_state_document = NULL;
	cof_state_sending = false;
}

void SV_CoF_StateActivate( void )
{
	int index;
	if( !SV_CoF_StoryCoopEnabled()) return;
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
	{
		edict_t *entity = SV_EdictNum( index );
		if( SV_IsValidEdict( entity ) && !Q_strcmp( SV_GetString( entity->v.classname ), "cof_clearitems" ) &&
			( entity->v.spawnflags & 3 ) == 1 ) entity->v.spawnflags |= 2;
	}
}

static int CoF_StatePlayer( sv_client_t *cl )
{
	int slot = (int)( cl - svs.clients );
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS ) return -1;
	if( cof_state_players[slot].userid != cl->userid )
	{
		memset( &cof_state_players[slot], 0, sizeof( cof_state_players[slot] ));
		cof_state_players[slot].userid = cl->userid;
	}
	return slot;
}

void SV_CoF_StateMessage( edict_t *entity, const char *name, byte *payload, int size )
{
	sv_client_t *client;
	int slot, entry, note;
	if( cof_state_sending || !name || Q_strcmp( name, "AddNote" ) || !CoF_StateNoteValid( payload, size ) || !SV_CoF_StoryCoopEnabled()) return;
	client = SV_ClientFromEdict( entity, true );
	if( !client || ( slot = CoF_StatePlayer( client )) < 0 ) return;
	entry = payload[0] - 1;
	note = cof_state_players[slot].notes[entry] - 1;
	if( !payload[1] && !Q_strcmp( (const char *)payload + 5, "nerds" ))
	{
		if( note >= 0 && note < cof_state.note_count && cof_state.notes[note].message[1] )
			memcpy( payload + 1, cof_state.notes[note].message + 1, 4 );
		return;
	}
	note = CoF_StateNote( &cof_state, payload, size );
	if( note >= 0 ) cof_state_players[slot].notes[entry] = note + 1;
}

static void CoF_StateDeliverNotes( sv_client_t *client, int slot )
{
	int note, entry, chosen, used;
	for( note = 0; note < cof_state.note_count; note++ )
	{
		cof_story_note_t *item = &cof_state.notes[note];
		chosen = 0;
		for( entry = 0; entry < COF_STATE_NOTES; entry++ )
			if( cof_state_players[slot].notes[entry] == note + 1 &&
				SV_CoF_RetailNoteSlot( client, (const char *)item->message + 5, entry + 1 ) == entry + 1 ) break;
		if( entry < COF_STATE_NOTES ) continue;
		entry = 1;
		while(( entry = SV_CoF_RetailNoteSlot( client, (const char *)item->message + 5, entry )) != 0 )
		{
			used = cof_state_players[slot].notes[entry - 1];
			if( !used || used == note + 1 ) { chosen = entry; break; }
			entry++;
		}
		if( !chosen ) chosen = SV_CoF_RetailNoteSlot( client, "", 1 );
		if( !chosen ) continue;
		cof_state_sending = true;
		used = SV_CoF_RetailArchiveNote( client, chosen, item->message, item->size );
		cof_state_sending = false;
		if( used )
		{
			cof_state_players[slot].notes[chosen - 1] = note + 1;
			Con_Printf( "cofstory phase=note-shared userid=%d slot=%d code=%d\n", client->userid, chosen, item->message[1] );
		}
	}
}

void SV_CoF_StateFrame( void )
{
	cof_story_values_t observed[MAX_CLIENTS];
	int valid[MAX_CLIENTS] = {0}, slot;
	if( !SV_CoF_StoryCoopEnabled()) return;
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *client = &svs.clients[slot];
		int alive;
		if( client->state != cs_spawned || FBitSet( client->flags, FCL_FAKECLIENT ) || !SV_CoF_RetailStoryRead( client, &observed[slot] )) continue;
		CoF_StatePlayer( client );
		alive = client->edict->v.health > 0 && client->edict->v.deadflag == DEAD_NO;
		CoF_StateObserve( &cof_state, &observed[slot], cof_state_players[slot].known &&
			( !alive || cof_state_players[slot].alive ) ? &cof_state_players[slot].previous : NULL );
		cof_state_players[slot].alive = alive;
		valid[slot] = 1;
	}
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *client = &svs.clients[slot];
		if( !valid[slot] ) continue;
		SV_CoF_RetailStoryWrite( client, &cof_state.values );
		if( cof_state.values.three_slots && !observed[slot].three_slots && cof_state_players[slot].alive ) SV_CoF_RetailThreeSlots( client );
		if( SV_CoF_RetailStoryRead( client, &cof_state_players[slot].previous )) cof_state_players[slot].known = 1;
		CoF_StateDeliverNotes( client, slot );
	}
}

void SV_CoF_StateCommandBegin( sv_client_t *cl )
{
	cof_state_document = NULL;
	SV_CoF_StateFrame();
	if( !SV_CoF_StoryCoopEnabled() || Q_strcmp( Cmd_Argv( 0 ), "closedocument" )) return;
	cof_state_document = SV_CoF_RetailOpenDocument( cl );
	if( cof_state_document ) cof_state_document_serial = cof_state_document->serialnumber;
	cof_state_reader = cl->userid;
}

void SV_CoF_StateCommandEnd( sv_client_t *cl )
{
	int slot;
	if( cof_state_document && cl->userid == cof_state_reader && SV_IsValidEdict( cof_state_document ) && cof_state_document->serialnumber == cof_state_document_serial )
		for( slot = 0; slot < svs.maxclients; slot++ )
			if( &svs.clients[slot] != cl ) SV_CoF_RetailAddDocument( &svs.clients[slot], cof_state_document );
	cof_state_document = NULL;
	SV_CoF_StateFrame();
}

void SV_CoF_StateExport( cof_state_data_t *data )
{
	memcpy( data, &cof_state, sizeof( *data ));
}

qboolean SV_CoF_StateImport( const cof_state_data_t *data )
{
	int index;
	if( !data || data->note_count < 0 || data->note_count > COF_STATE_NOTES ||
		!memchr( data->values.objective, 0, COF_STATE_TEXT ) || !memchr( data->values.objective_file, 0, COF_STATE_TEXT )) return false;
	for( index = 0; index < data->note_count; index++ )
		if( !CoF_StateNoteValid( data->notes[index].message, data->notes[index].size )) return false;
	memcpy( &cof_state, data, sizeof( cof_state ));
	memset( cof_state_players, 0, sizeof( cof_state_players ));
	return true;
}
