#include "common.h"
#include "server.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_state.h"

// CEnvGlobal : CPointEntity (0xDC): string_t m_globalstate, int m_triggermode, int m_initialstate
#define COF_GLOBAL_SIZE 0xE8
#define COF_GLOBAL_STATE 0xDC
#define COF_GLOBAL_MODE 0xE0
#define COF_GLOBAL_INITIAL 0xE4

static cof_state_data_t cof_state;
static struct
{
	int userid, known, alive;
	cof_story_values_t previous;
	int notes[COF_STATE_NOTES];
	char delivered[COF_STATE_TEXT]; // the objective the game last sent this player (Objectives)
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

// 1.1.8 (M2 finding 6, M1 finding 4): every story env_global toggles (triggermode 3) a global that starts off, and
// single player fires each one once, to switch it on. A second fire in co-op (c_asylumb's restore_power from a
// puzzle left open, c_roofs's scene from its other entrance) switched it back off for good: c_forest7's exit,
// c_park3's gate. Such a toggle now only sets its global on (triggermode 1).
static void CoF_StateGlobalOnce( edict_t *entity, int index )
{
	byte *pv = SV_CoF_StoryEntityPrivate( entity, COF_GLOBAL_SIZE );
	string_t state;
	int mode, initial, on = 1;
	if( !pv ) return;
	memcpy( &state, pv + COF_GLOBAL_STATE, sizeof( state ));
	memcpy( &mode, pv + COF_GLOBAL_MODE, sizeof( mode ));
	memcpy( &initial, pv + COF_GLOBAL_INITIAL, sizeof( initial ));
	if( !state || mode != 3 || initial != 0 ) return;
	memcpy( pv + COF_GLOBAL_MODE, &on, sizeof( on ));
	Con_Printf( "cofstory phase=global-set-only entity=%d state=%s\n", index, SV_GetString( state ));
}

void SV_CoF_StateActivate( void )
{
	int index;
	if( !SV_CoF_StoryCoopEnabled()) return;
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
	{
		edict_t *entity = SV_EdictNum( index );
		const char *classname;
		if( !SV_IsValidEdict( entity )) continue;
		classname = SV_GetString( entity->v.classname );
		if( !Q_strcmp( classname, "cof_clearitems" ) && ( entity->v.spawnflags & 3 ) == 1 ) entity->v.spawnflags |= 2;
		// 1.1.8 (K3): a permanent fade (8, FFADE_STAYOUT; c_roofs/c_roofs2 fadefall when one player falls) blacked out
		// every player for good: it is its activator's alone (4), as single player sees it.
		else if( !Q_strcmp( classname, "env_fade" ) && ( entity->v.spawnflags & 12 ) == 8 ) entity->v.spawnflags |= 4;
		else if( !Q_strcmp( classname, "env_global" )) CoF_StateGlobalOnce( entity, index );
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

// Objectives( string key, string file, byte isNew ): what the game sent this player. A file other than a language's
// hints.txt is the custom one cof_objective set (the game zeroes it right after sending).
static void CoF_StateObjectiveSent( edict_t *entity, const byte *payload, int size )
{
	const char *key = (const char *)payload, *file;
	sv_client_t *client;
	int slot, key_length, file_length;
	for( key_length = 0; key_length < size && payload[key_length]; key_length++ );
	if( key_length >= size || key_length >= COF_STATE_TEXT ) return;
	file = key + key_length + 1;
	for( file_length = 0; key_length + 1 + file_length < size && file[file_length]; file_length++ );
	if( key_length + 1 + file_length >= size || file_length >= COF_STATE_TEXT ) return;
	if(( client = SV_ClientFromEdict( entity, true )) != NULL && ( slot = CoF_StatePlayer( client )) >= 0 )
		Q_strncpy( cof_state_players[slot].delivered, key, sizeof( cof_state_players[slot].delivered ));
	if( !file_length || ( !Q_strncmp( file, "txtfiles/", 9 ) && file_length >= 9 && !Q_strcmp( file + file_length - 9, "hints.txt" ))) return;
	Q_strncpy( cof_state.sent_key, key, sizeof( cof_state.sent_key ));
	Q_strncpy( cof_state.sent_file, file, sizeof( cof_state.sent_file ));
}

void SV_CoF_StateMessage( edict_t *entity, const char *name, byte *payload, int size )
{
	sv_client_t *client;
	int slot, entry, note;
	if( name && payload && !Q_strcmp( name, "Objectives" ) && SV_CoF_StoryCoopEnabled()) { CoF_StateObjectiveSent( entity, payload, size ); return; }
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
		cof_story_values_t mine;
		if( !valid[slot] ) continue;
		// The objective's file goes with its text, once: a player the game already sent it keeps the file it has
		// (zeroed by its PostThink; the game saves that field as a time).
		mine = cof_state.values;
		if( !Q_strcmp( cof_state_players[slot].delivered, mine.objective ))
			Q_strncpy( mine.objective_file, observed[slot].objective_file, sizeof( mine.objective_file ));
		SV_CoF_RetailStoryWrite( client, &mine );
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

// 1.1.8 (chapter starts): a party value no player has yet. The first look at the players keeps both: a fresh
// player's 0 never replaces a doctor weapon the party has, and three slots, once set, stay set.
void SV_CoF_StatePreset( int doctor_weapon, int three_slots )
{
	if( doctor_weapon == 1 || doctor_weapon == 2 ) cof_state.values.doctor_weapon = doctor_weapon;
	if( three_slots ) cof_state.values.three_slots = 1;
	Con_Printf( "cofstory phase=chapter-state doctor_weapon=%d three_slots=%d\n", cof_state.values.doctor_weapon, cof_state.values.three_slots );
}
