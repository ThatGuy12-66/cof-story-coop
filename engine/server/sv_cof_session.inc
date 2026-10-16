#include "sv_cof_session_data.h"
#include "sv_cof_exit.h"
#include "sv_cof_retail.h"

static byte *cof_session_pending;
static size_t cof_session_pending_size;
static cof_session_data_t *cof_session_data;
static int cof_session_slots[32];
static int cof_session_generation = -1;
static double cof_session_save_after;
static qboolean cof_session_auto_needed;
static char cof_session_latest[25];
static char cof_session_pending_slot[25];
static double cof_session_wipe_after;
static int cof_session_deferred; // the reason last logged (1 cutscene, 2 ending door): once per deferral
static convar_t *cof_continue; // the tester app's Continue: load the latest checkpoint once the host has joined
static qboolean CoF_SessionCurrentNames( char names[32][32] );
static qboolean CoF_SessionPreflight( const byte *archive, size_t size, cof_session_data_t *metadata );

static byte *CoF_SessionReadFile( const char *path, size_t *size )
{
	file_t *file = FS_Open( path, "rb", true );
	fs_offset_t length;
	byte *data;
	*size = 0;
	if( !file ) return NULL;
	length = FS_FileLength( file );
	if( length <= 0 || length > COF_ARCHIVE_LIMIT ) { FS_Close( file ); return NULL; }
	data = Mem_Calloc( host.mempool, (size_t)length );
	if( FS_Read( file, data, (size_t)length ) != length ) { FS_Close( file ); Mem_Free( data ); return NULL; }
	FS_Close( file );
	*size = (size_t)length;
	return data;
}

static qboolean CoF_SessionWriteFile( const char *path, const void *data, size_t size )
{
	file_t *file = FS_Open( path, "wb", true );
	qboolean written;
	if( !file ) return false;
	written = FS_Write( file, data, size ) == (fs_offset_t)size;
	return FS_Close( file ) == 0 && written;
}

static qboolean CoF_SessionReplace( const char *path, const byte *data, size_t size )
{
	char temporary[MAX_OSPATH], backup[MAX_OSPATH];
	byte *check;
	size_t length;
	qboolean equal;
	Q_snprintf( temporary, sizeof( temporary ), "%s.next", path );
	Q_snprintf( backup, sizeof( backup ), "%s.bak", path );
	if( !CoF_SessionWriteFile( temporary, data, size )) return false;
	check = CoF_SessionReadFile( temporary, &length );
	if( !check ) return false;
	equal = length == size && !memcmp( check, data, size ); Mem_Free( check );
	if( !equal ) return false;
	if( FS_FileExists( path, true ))
	{
		if( FS_FileExists( backup, true ) && !FS_Delete( backup )) return false;
		if( !FS_Rename( path, backup )) return false;
	}
	if( FS_Rename( temporary, path )) return true;
	if( FS_FileExists( backup, true ) && !FS_Rename( backup, path ))
		Con_Printf( "cofstory phase=session-rollback-failed file=%s\n", path );
	return false;
}

static qboolean CoF_SessionAppend( byte **archive, size_t *size, const char *name, const void *data, size_t length )
{
	cof_archive_header_t header;
	cof_archive_entry_t entry;
	size_t total;
	if( !CoF_ArchiveName( name ) || length > COF_ARCHIVE_LIMIT || sizeof( entry ) + length > COF_ARCHIVE_LIMIT - *size ) return false;
	memcpy( &header, *archive, sizeof( header ));
	if( header.count >= COF_ARCHIVE_FILES ) return false;
	total = *size + sizeof( entry ) + length;
	*archive = Mem_Realloc( host.mempool, *archive, total );
	memset( &entry, 0, sizeof( entry ));
	Q_strncpy( entry.name, name, sizeof( entry.name )); entry.size = (uint32_t)length;
	memcpy( *archive + *size, &entry, sizeof( entry ));
	memcpy( *archive + *size + sizeof( entry ), data, length );
	header.count++; header.size = (uint32_t)total;
	memcpy( *archive, &header, sizeof( header ));
	*size = total;
	return true;
}

static byte *CoF_SessionGlobalCapture( size_t *size )
{
	SAVERESTOREDATA *saved = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS );
	uint32_t header[3];
	int index;
	size_t tokens = 0;
	byte *result, *cursor;
	svgame.dllFuncs.pfnSaveGlobalState( saved );
	for( index = 0; index < saved->tokenCount; index++ ) tokens += saved->pTokens[index] ? Q_strlen( saved->pTokens[index] ) + 1 : 1;
	*size = sizeof( header ) + tokens + saved->size;
	result = Mem_Calloc( host.mempool, *size );
	header[0] = saved->tokenCount; header[1] = (uint32_t)tokens; header[2] = saved->size;
	memcpy( result, header, sizeof( header )); cursor = result + sizeof( header );
	for( index = 0; index < saved->tokenCount; index++ )
	{
		const char *token = saved->pTokens[index] ? saved->pTokens[index] : "";
		size_t length = Q_strlen( token ) + 1;
		memcpy( cursor, token, length ); cursor += length;
	}
	memcpy( cursor, saved->pBaseData, saved->size );
	SaveFinish( saved );
	return result;
}

static qboolean CoF_SessionGlobalValid( const byte *data, size_t size )
{
	uint32_t header[3], index;
	size_t position = 0;
	if( !data || size < sizeof( header )) return false;
	memcpy( header, data, sizeof( header ));
	if( !header[0] || header[0] > SAVE_HASHSTRINGS || header[1] > size - sizeof( header ) || header[2] != size - sizeof( header ) - header[1] ) return false;
	for( index = 0; index < header[0]; index++ )
	{
		while( position < header[1] && data[sizeof( header ) + position] ) position++;
		if( position == header[1] ) return false;
		position++;
	}
	return position == header[1];
}

static void CoF_SessionGlobalRestore( const byte *data )
{
	uint32_t header[3], index;
	SAVERESTOREDATA *saved;
	char *cursor;
	memcpy( header, data, sizeof( header ));
	saved = SaveInit( header[1] + header[2], header[0] );
	memcpy( saved->pBaseData, data + sizeof( header ) + header[1], header[2] );
	memcpy( saved->pBaseData + header[2], data + sizeof( header ), header[1] );
	cursor = saved->pBaseData + header[2];
	for( index = 0; index < header[0]; index++ )
	{
		saved->pTokens[index] = *cursor ? cursor : NULL;
		cursor += Q_strlen( cursor ) + 1;
	}
	svgame.dllFuncs.pfnResetGlobalState();
	svgame.dllFuncs.pfnRestoreGlobalState( saved );
	SaveFinish( saved );
}

static void CoF_SessionSavedEntity( ENTITYTABLE *entry, int index )
{
	int slot;
	if( !cof_session_saving || !cof_session_data || index <= svs.maxclients ) return;
	for( slot = 0; slot < svs.maxclients; slot++ )
		if( cof_session_data->players[slot][0] && SV_CoF_RetailOwnsWeapon( svs.clients[slot].edict, entry->pent ))
		{
			cof_session_data->owners[index] = slot + 1;
			SetBits( entry->flags, FENTTABLE_COF_INVENTORY );
			break;
		}
}

qboolean SV_CoF_SessionSave( const char *slot )
{
	cof_archive_header_t header = { COF_ARCHIVE_MAGIC, 1, 0, sizeof( header ), 0 };
	byte *archive = NULL, *content = NULL;
	size_t size = sizeof( header ), length;
	char path[MAX_OSPATH], name[64];
	char names[32][32];
	SAVERESTOREDATA *saved = NULL;
	int index, extension, spawned = 0;
	qboolean success = false;
	if( !CoF_SessionSlotValid( slot ) || !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ||
		SV_CoF_TransitionActive() || cof_session_pending || cof_session_saving || svgame.globals->pSaveData ) return false;
	// A running cutscene or the ending door freezes players; a save must never hold that freeze.
	if( SV_CoF_SceneActive() || SV_CoF_PartyHolding() )
	{
		int reason = SV_CoF_SceneActive() ? 1 : 2;
		if( cof_session_deferred != reason )
			Con_Printf( "cofstory phase=session-save-deferred slot=%s reason=%s\n", slot, reason == 1 ? "cutscene" : "ending-door" );
		cof_session_deferred = reason;
		return false;
	}
	cof_session_deferred = 0;
	for( index = 0; index < svs.maxclients; index++ )
	{
		sv_client_t *client = &svs.clients[index];
		if( client->state < cs_connected || FBitSet( client->flags, FCL_FAKECLIENT )) continue;
		if( client->state != cs_spawned || !SV_IsValidEdict( client->edict ) || client->edict->v.health <= 0 || client->edict->v.deadflag != DEAD_NO ) return false;
		spawned++;
	}
	if( !spawned || svgame.numEntities > COF_SNAPSHOT_ENTITIES || !CoF_SessionCurrentNames( names )) return false;
	SV_CoF_StateFrame(); SV_CoF_ShareFrame();
	cof_session_data = Mem_Calloc( host.mempool, sizeof( *cof_session_data ));
	cof_session_data->version = COF_SESSION_VERSION; cof_session_data->clients = svs.maxclients; cof_session_data->entities = svgame.numEntities;
	cof_session_data->maps = cof_story_map_count;
	memcpy( cof_session_data->history, cof_story_maps, sizeof( cof_story_maps ));
	Q_strncpy( cof_session_data->map, sv.name, sizeof( cof_session_data->map ));
	for( index = 0; index < svs.maxclients; index++ )
		if( svs.clients[index].state == cs_spawned && !FBitSet( svs.clients[index].flags, FCL_FAKECLIENT ))
			Q_strncpy( cof_session_data->players[index], svs.clients[index].name, 32 );
	SV_CoF_StateExport( &cof_session_data->story ); SV_CoF_ShareExport( &cof_session_data->share );
	cof_session_saving = true;
	cofMenuSaveTracking = true; cofMenuSaveIOFailed = false;
	saved = SaveGameState( false );
	cofMenuSaveTracking = false; cof_session_saving = false;
	if( !saved || cofMenuSaveIOFailed ) goto done;
	SaveFinish( saved ); saved = NULL;
	archive = Mem_Calloc( host.mempool, size ); memcpy( archive, &header, size );
	for( index = 0; index <= cof_story_map_count; index++ )
	{
		const char *map = index == cof_story_map_count ? sv.name : cof_story_maps[index];
		if( index < cof_story_map_count && !Q_stricmp( map, sv.name )) continue;
		for( extension = 1; extension <= 3; extension++ )
		{
			Q_snprintf( name, sizeof( name ), "%s.HL%d", map, extension );
			Q_snprintf( path, sizeof( path ), "save/%s", name );
			content = CoF_SessionReadFile( path, &length );
			if( !content || !CoF_SessionAppend( &archive, &size, name, content, length )) goto done;
			Mem_Free( content ); content = NULL;
		}
	}
	for( index = 0; index < 32; index++ )
	{
		const byte *parked;
		if( index < svs.maxclients && cof_session_data->players[index][0] )
		{
			if( !SV_CoF_CaptureInventory( &svs.clients[index], &content, &length )) goto done;
			Q_snprintf( name, sizeof( name ), "player%02d.inv", index );
			if( !CoF_SessionAppend( &archive, &size, name, content, length )) goto done;
			Mem_Free( content ); content = NULL;
		}
		if( SV_CoF_RecoveryExport( index, cof_session_data->parked[index], &parked, &length ))
		{
			Q_snprintf( name, sizeof( name ), "parked%02d.inv", index );
			if( !CoF_SessionAppend( &archive, &size, name, parked, length )) goto done;
		}
	}
	content = CoF_SessionGlobalCapture( &length );
	if( !CoF_SessionAppend( &archive, &size, "global.dat", content, length ) ||
		!CoF_SessionAppend( &archive, &size, "session.dat", cof_session_data, sizeof( *cof_session_data ))) goto done;
	Mem_Free( content ); content = NULL;
	memcpy( &header, archive, sizeof( header )); header.checksum = CoF_ArchiveChecksum( archive, size ); memcpy( archive, &header, sizeof( header ));
	if( !CoF_SessionPreflight( archive, size, cof_session_data )) goto done;
	Q_snprintf( path, sizeof( path ), "save/cofcoop/%s.cof", slot );
	if( !CoF_SessionReplace( path, archive, size )) goto done;
	Q_strncpy( cof_session_latest, slot, sizeof( cof_session_latest ));
	if( !CoF_SessionReplace( "save/cofcoop/latest.txt", (const byte *)slot, Q_strlen( slot )))
		Con_Printf( "cofstory phase=session-index-failed slot=%s\n", slot );
	Con_Printf( "cofstory phase=session-saved slot=%s map=%s bytes=%u\n", slot, sv.name, (unsigned int)size );
	success = true;
done:
	if( saved ) SaveFinish( saved );
	if( content ) Mem_Free( content );
	if( archive ) Mem_Free( archive );
	if( cof_session_data ) Mem_Free( cof_session_data ); cof_session_data = NULL;
	if( !success ) Con_Printf( "cofstory phase=session-save-refused slot=%s reason=io-or-snapshot\n", slot );
	return success;
}

static qboolean CoF_SessionPreflight( const byte *archive, size_t size, cof_session_data_t *metadata )
{
	const byte *content;
	size_t length;
	char name[96];
	uint32_t index;
	int kind;
	if( !CoF_ArchiveValid( archive, size )) return false;
	content = CoF_ArchiveFind( archive, "session.dat", &length );
	if( !content || length != sizeof( *metadata )) return false;
	memcpy( metadata, content, length );
	if( !CoF_SessionDataValid( metadata ) || metadata->clients != (uint32_t)svs.maxclients ) return false;
	Q_snprintf( name, sizeof( name ), "maps/%s.bsp", metadata->map );
	if( !FS_FileExists( name, false )) return false;
	content = CoF_ArchiveFind( archive, "global.dat", &length );
	if( !CoF_SessionGlobalValid( content, length )) return false;
	for( index = 0; index <= metadata->maps; index++ )
	{
		const char *map = index == metadata->maps ? metadata->map : metadata->history[index];
		uint32_t entities;
		Q_snprintf( name, sizeof( name ), "%s.HL1", map );
		content = CoF_ArchiveFind( archive, name, &length );
		if( !CoF_SessionNativeValid( content, length, 1, 0 )) return false;
		memcpy( &entities, content + 12, 4 );
		if( index == metadata->maps && entities != metadata->entities ) return false;
		for( kind = 2; kind <= 3; kind++ )
		{
			Q_snprintf( name, sizeof( name ), "%s.HL%d", map, kind );
			content = CoF_ArchiveFind( archive, name, &length );
			if( !CoF_SessionNativeValid( content, length, kind, entities )) return false;
		}
	}
	for( index = 0; index < 32; index++ )
	{
		if( metadata->players[index][0] )
		{
			Q_snprintf( name, sizeof( name ), "player%02u.inv", index );
			content = CoF_ArchiveFind( archive, name, &length );
			if( !CoF_SnapshotValid( content, length )) return false;
		}
		if( metadata->parked[index][0] )
		{
			Q_snprintf( name, sizeof( name ), "parked%02u.inv", index );
			content = CoF_ArchiveFind( archive, name, &length );
			if( !CoF_SnapshotValid( content, length )) return false;
		}
	}
	return true;
}

static qboolean CoF_SessionStage( const byte *archive, const cof_session_data_t *metadata )
{
	struct { char name[64]; int backed, installed; } files[771];
	char path[MAX_OSPATH], temporary[MAX_OSPATH], backup[MAX_OSPATH];
	const byte *content;
	byte *check;
	size_t length, checked;
	int index, count = 0, kind;
	memset( files, 0, sizeof( files ));
	for( index = 0; index <= (int)metadata->maps; index++ )
	{
		const char *map = index == (int)metadata->maps ? metadata->map : metadata->history[index];
		if( index < (int)metadata->maps && !Q_stricmp( map, metadata->map )) continue;
		for( kind = 1; kind <= 3; kind++ )
		{
			Q_snprintf( files[count].name, sizeof( files[count].name ), "%s.HL%d", map, kind );
			content = CoF_ArchiveFind( archive, files[count].name, &length );
			Q_snprintf( temporary, sizeof( temporary ), "save/cofcoop/load-next-%s", files[count].name );
			if( !content || !CoF_SessionWriteFile( temporary, content, length )) goto rollback;
			check = CoF_SessionReadFile( temporary, &checked );
			if( !check ) goto rollback;
			if( checked != length || memcmp( content, check, length )) { Mem_Free( check ); goto rollback; }
			Mem_Free( check ); count++;
		}
	}
	for( index = 0; index < count; index++ )
	{
		Q_snprintf( path, sizeof( path ), "save/%s", files[index].name );
		Q_snprintf( temporary, sizeof( temporary ), "save/cofcoop/load-next-%s", files[index].name );
		Q_snprintf( backup, sizeof( backup ), "save/cofcoop/load-before-%s", files[index].name );
		if( FS_FileExists( path, true ))
		{
			if( FS_FileExists( backup, true ) && !FS_Delete( backup )) goto rollback;
			if( !FS_Rename( path, backup )) goto rollback;
			files[index].backed = true;
		}
		if( !FS_Rename( temporary, path )) goto rollback;
		files[index].installed = true;
	}
	return true;
rollback:
	for( index = count - 1; index >= 0; index-- )
	{
		Q_snprintf( path, sizeof( path ), "save/%s", files[index].name );
		Q_snprintf( backup, sizeof( backup ), "save/cofcoop/load-before-%s", files[index].name );
		if( files[index].installed ) FS_Delete( path );
		if( files[index].backed && !FS_Rename( backup, path ))
			Con_Printf( "cofstory phase=session-rollback-failed file=%s\n", files[index].name );
	}
	return false;
}

static qboolean CoF_SessionCurrentNames( char names[32][32] )
{
	int slot, unused[32];
	memset( names, 0, 32 * 32 );
	for( slot = 0; slot < svs.maxclients; slot++ )
		if( svs.clients[slot].state >= cs_connected && !FBitSet( svs.clients[slot].flags, FCL_FAKECLIENT ))
			Q_strncpy( names[slot], svs.clients[slot].name, 32 );
	return CoF_SessionMatch( names, names, unused );
}

static void CoF_SessionLatest( void )
{
	byte *content;
	size_t size;
	if( cof_session_latest[0] ) return;
	content = CoF_SessionReadFile( "save/cofcoop/latest.txt", &size );
	if( content )
	{
		if( size < sizeof( cof_session_latest ))
		{
			memcpy( cof_session_latest, content, size ); cof_session_latest[size] = 0;
			if( !CoF_SessionSlotValid( cof_session_latest )) cof_session_latest[0] = 0;
		}
		Mem_Free( content );
	}
}

// A well-formed archive whose session record is another format version.
static qboolean CoF_SessionOlder( const byte *archive, size_t size )
{
	const byte *content;
	size_t length;
	uint32_t version;
	if( !CoF_ArchiveValid( archive, size )) return false;
	content = CoF_ArchiveFind( archive, "session.dat", &length );
	if( !content || length < sizeof( version )) return false;
	memcpy( &version, content, sizeof( version ));
	return version != COF_SESSION_VERSION || length != sizeof( cof_session_data_t );
}

// The checkpoint in this slot would pass every load check right now.
static qboolean CoF_SessionUsable( const char *slot )
{
	char path[MAX_OSPATH];
	byte *archive;
	size_t size;
	cof_session_data_t *metadata;
	qboolean usable;
	if( !slot || !CoF_SessionSlotValid( slot )) return false;
	Q_snprintf( path, sizeof( path ), "save/cofcoop/%s.cof", slot );
	if( !( archive = CoF_SessionReadFile( path, &size ))) return false;
	metadata = Mem_Calloc( host.mempool, sizeof( *metadata ));
	usable = CoF_SessionPreflight( archive, size, metadata );
	Mem_Free( metadata );
	Mem_Free( archive );
	return usable;
}

qboolean SV_CoF_SessionLoad( const char *slot )
{
	byte *archive;
	size_t size;
	char path[MAX_OSPATH], names[32][32];
	cof_session_data_t *metadata;
	qboolean valid;
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active || cof_session_pending ||
		SV_CoF_TransitionActive() || cof_session_saving || svgame.globals->pSaveData ) return false;
	if( GameState->nextstate != STATE_RUNFRAME || UI_CreditsActive() ) return false;
	CoF_SessionLatest();
	if( !slot || !slot[0] ) slot = cof_session_latest;
	if( !CoF_SessionSlotValid( slot ) || !CoF_SessionCurrentNames( names )) return false;
	Q_snprintf( path, sizeof( path ), "save/cofcoop/%s.cof", slot );
	archive = CoF_SessionReadFile( path, &size );
	metadata = Mem_Calloc( host.mempool, sizeof( *metadata ));
	valid = archive && CoF_SessionPreflight( archive, size, metadata );
	if( valid )
	{
		cof_session_pending = archive; cof_session_pending_size = size;
		Q_strncpy( cof_session_pending_slot, slot, sizeof( cof_session_pending_slot ));
		COM_ChangeLevel( metadata->map, NULL, false );
		Con_Printf( "cofstory phase=session-load-queued slot=%s map=%s\n", slot, metadata->map );
	}
	else
	{
		qboolean older = archive && CoF_SessionOlder( archive, size );
		if( archive ) Mem_Free( archive );
		Con_Printf( "cofstory phase=session-load-refused slot=%s reason=%s\n", slot, older ? "older-build" : "invalid-or-missing" );
		if( older ) Con_Printf( "Checkpoint %s was saved by an older build of the story engine and cannot be loaded.\n", slot );
	}
	Mem_Free( metadata );
	return valid;
}

static void CoF_SessionCreatePlayers( SAVERESTOREDATA *saved )
{
	int index, target;
	for( index = 1; index < saved->tableCount; index++ )
	{
		ENTITYTABLE *entry = &saved->pTable[index];
		if( index <= (int)cof_session_data->clients )
		{
			target = cof_session_slots[index - 1];
			if( !target ) continue;
			if( !entry->size || Q_strcmp( SV_GetString( entry->classname ), "player" )) Host_Error( "Invalid co-op player snapshot\n" );
			entry->pent = SV_EdictNum( target );
			SV_InitEdict( entry->pent );
			entry->pent = SV_CreateNamedEntity( entry->pent, entry->classname );
		}
		else if( cof_session_data->owners[index] && cof_session_slots[cof_session_data->owners[index] - 1] )
			entry->pent = SV_CreateNamedEntity( NULL, entry->classname );
	}
}

static void CoF_SessionPlayersRestored( SAVERESTOREDATA *saved )
{
	int index;
	for( index = 0; index < (int)cof_session_data->clients; index++ )
	{
		int target = cof_session_slots[index];
		sv_client_t *client;
		if( !target ) continue;
		client = &svs.clients[target - 1];
		client->edict = saved->pTable[index + 1].pent;
		if( !SV_IsValidEdict( client->edict )) Host_Error( "Co-op player restore failed\n" );
		client->edict->v.netname = SV_MakeString( client->name );
		client->edict->v.colormap = target;
		client->edict->v.fixangle = 1;
		SV_CoF_RetailInventoryReady( client );
		SV_CoF_RetailSessionReady( client, &cof_session_data->story.values );
		CoF_TransitionRestored( &cof_transition.tickets, svs.spawncount, target - 1, client->userid );
		Con_Printf( "cofstory phase=session-player-restored userid=%d saved-slot=%d slot=%d\n", client->userid, index + 1, target );
	}
}

static qboolean CoF_SessionRunLoad( void )
{
	char names[32][32], name[64];
	const byte *content;
	size_t length;
	int index;
	if( !cof_session_pending ) return false;
	cof_session_data = Mem_Calloc( host.mempool, sizeof( *cof_session_data ));
	if( !CoF_SessionPreflight( cof_session_pending, cof_session_pending_size, cof_session_data ) ||
		!CoF_SessionCurrentNames( names ) || !CoF_SessionMatch( cof_session_data->players, names, cof_session_slots ) ||
		!CoF_SessionStage( cof_session_pending, cof_session_data ))
	{
		Con_Printf( "cofstory phase=session-load-refused reason=preflight-or-stage\n" );
		SV_CoF_SessionReset();
		return true;
	}
	SV_CoF_TransitionReset( false );
	CoF_TransitionBegin( &cof_transition.tickets, svs.spawncount + 1 );
	for( index = 0; index < 32; index++ )
		if( cof_session_slots[index] )
		{
			int target = cof_session_slots[index] - 1;
			CoF_TransitionCapture( &cof_transition.tickets, target, svs.clients[target].userid );
		}
	SV_CoF_ExitReset();
	SV_InactivateClients(); SV_FinalMessage( "", true ); SV_DeactivateServer();
	for( index = 0; index < svs.maxclients; index++ )
	{
		sv_client_t *client = &svs.clients[index];
		if( client->state >= cs_connected && !FBitSet( client->flags, FCL_FAKECLIENT ) && !client->frames )
			client->frames = Mem_Calloc( host.mempool, sizeof( client_frame_t ) * SV_UPDATE_BACKUP );
	}
	if( !SV_SpawnServer( cof_session_data->map, NULL, false )) Host_Error( "Co-op checkpoint map failed to start\n" );
	content = CoF_ArchiveFind( cof_session_pending, "global.dat", &length );
	CoF_SessionGlobalRestore( content );
	cof_story_map_count = cof_session_data->maps;
	memcpy( cof_story_maps, cof_session_data->history, sizeof( cof_story_maps ));
	cof_session_loading = true;
	if( !LoadGameState( cof_session_data->map, false )) Host_Error( "Co-op checkpoint became unreadable\n" );
	cof_session_loading = false;
	svgame.globals->time = sv.time;
	SV_CoF_StateImport( &cof_session_data->story );
	SV_CoF_ShareImport( &cof_session_data->share );
	SV_CoF_RecoveryReset();
	for( index = 0; index < 32; index++ )
	{
		if( cof_session_data->players[index][0] && !cof_session_slots[index] )
		{
			Q_snprintf( name, sizeof( name ), "player%02d.inv", index );
			content = CoF_ArchiveFind( cof_session_pending, name, &length );
			SV_CoF_RecoveryImport( cof_session_data->players[index], content, length );
		}
		if( cof_session_data->parked[index][0] )
		{
			Q_snprintf( name, sizeof( name ), "parked%02d.inv", index );
			content = CoF_ArchiveFind( cof_session_pending, name, &length );
			SV_CoF_RecoveryImport( cof_session_data->parked[index], content, length );
		}
	}
	SV_ActivateServer( false );
	CoF_TransitionResume();
	Q_strncpy( cof_session_latest, cof_session_pending_slot, sizeof( cof_session_latest ));
	Con_Printf( "cofstory phase=session-loaded slot=%s map=%s generation=%d\n", cof_session_pending_slot, sv.name, svs.spawncount );
	Mem_Free( cof_session_data ); cof_session_data = NULL;
	Mem_Free( cof_session_pending ); cof_session_pending = NULL; cof_session_pending_size = 0;
	cof_session_generation = svs.spawncount; cof_session_auto_needed = false;
	return true;
}

void SV_CoF_SessionReset( void )
{
	if( cof_session_pending ) Mem_Free( cof_session_pending );
	if( cof_session_data ) Mem_Free( cof_session_data );
	cof_session_pending = NULL; cof_session_data = NULL; cof_session_pending_size = 0;
	cof_session_generation = -1; cof_session_auto_needed = false;
	cof_session_saving = cof_session_loading = false;
	cof_session_latest[0] = cof_session_pending_slot[0] = 0;
}

void SV_CoF_SessionFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	if( cof_session_generation != svs.spawncount )
	{
		qboolean first = cof_session_generation < 0;
		cof_session_generation = svs.spawncount;
		cof_session_auto_needed = true; cof_session_save_after = sv.time + 2;
		// Not on the new map's first frames: a load then swaps the world under a
		// client that has not drawn it yet (live 2026-10-12: the retail client.dll
		// crashed on the reloaded world's sky, which its renderer never set up).
		cof_session_wipe_after = sv.time + 2;
		if( first )
		{
			CoF_SessionLatest();
			// Keep the last checkpoint for the host to load, but only one that
			// can be loaded: otherwise a team wipe here would find nothing.
			if( cof_session_latest[0] && CoF_SessionUsable( cof_session_latest ))
			{
				cof_session_auto_needed = false;
				Con_Printf( "cofstory phase=session-checkpoint-preserved slot=%s\n", cof_session_latest );
			}
			else if( cof_session_latest[0] )
				Con_Printf( "cofstory phase=session-checkpoint-unusable slot=%s\n", cof_session_latest );
		}
	}
	// Continue (the tester app's start option): the latest checkpoint, once,
	// when the listen host has joined; after the same two-second settle.
	if( cof_continue && cof_continue->value && sv.time >= cof_session_wipe_after && !cof_session_pending &&
		!SV_CoF_TransitionActive() && svs.clients[0].state == cs_spawned )
	{
		qboolean queued;
		Cvar_DirectSet( cof_continue, "0" );
		queued = SV_CoF_SessionLoad( NULL );
		Con_Printf( "cofstory phase=session-continue result=%s\n", queued ? "queued" : "refused" );
		if( !queued ) SV_CoF_RetailProFont( &svs.clients[0], 5, "The checkpoint could not be loaded: this map starts from its beginning.", true );
		return;
	}
	if( sv.time >= cof_session_wipe_after && !SV_CoF_TransitionActive() && !cof_session_pending )
	{
		cof_session_wipe_after = sv.time + 1;
		if( SV_CoF_SessionWipe( sv.name, NULL )) return;
	}
	if( cof_session_auto_needed && sv.time >= cof_session_save_after && !SV_CoF_TransitionActive() )
	{
		cof_session_save_after = sv.time + 5;
		if( SV_CoF_SessionSave( "autosave" )) cof_session_auto_needed = false;
	}
}

qboolean SV_CoF_SessionWipe( const char *map, const char *landmark )
{
	static qboolean waiting; // said once: the recovery waits for a player still joining
	int slot, players = 0;
	if( !SV_CoF_StoryCoopEnabled() || !map || Q_stricmp( map, sv.name ) || ( landmark && landmark[0] )) return false;
	for( slot = 0; slot < svs.maxclients; slot++ )
	{
		sv_client_t *client = &svs.clients[slot];
		if( client->state < cs_connected ) continue;
		if( FBitSet( client->flags, FCL_FAKECLIENT ))
		{
			// A living AI companion can still revive the person (its CPR takes
			// seconds), who can also respawn beside it: no wipe yet.
			if( client->state == cs_spawned && SV_IsValidEdict( client->edict ) &&
				client->edict->v.health > 0 && client->edict->v.deadflag == DEAD_NO ) return false;
			continue;
		}
		if( client->state != cs_spawned )
		{
			// Still joining: never load under a signon. The game's own restart
			// (pfnChangeLevel, no checkpoint) may come first meanwhile.
			if( !waiting ) Con_Printf( "cofstory phase=session-wipe-waiting reason=joining\n" );
			waiting = true;
			return false;
		}
		players++;
		if( !SV_IsValidEdict( client->edict ) || client->edict->v.fuser3 == 3 ||
			( client->edict->v.health > 0 && client->edict->v.deadflag == DEAD_NO ) || SV_CoF_RetailRevived( client )) return false;
	}
	waiting = false;
	if( !players ) return false;
	if( cof_session_pending ) return true;
	if( !SV_CoF_SessionLoad( NULL )) return false;
	Con_Printf( "cofstory phase=session-wipe-recovery map=%s\n", map );
	return true;
}

static void CoF_SessionSave_f( void )
{
	char slot[25];
	if( Cmd_Argc() > 2 || ( Cmd_Argc() == 2 && Q_strlen( Cmd_Argv( 1 )) >= sizeof( slot )))
	{
		Con_Printf( "Usage: cof_save [slot of 1-24 characters]\n" ); return;
	}
	Q_strncpy( slot, Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "manual", sizeof( slot ));
	if( SV_CoF_SessionSave( slot )) return;
	if( SV_CoF_SceneActive() || SV_CoF_PartyHolding() ) Con_Printf( "Co-op save waits: a cutscene is running. Save again when it ends.\n" );
	else Con_Printf( "Co-op save refused: use a unique-name, living, fully joined story party and a slot of 1-24 lowercase letters, digits, _ or -.\n" );
}

static void CoF_SessionLoad_f( void )
{
	char slot[25];
	if( Cmd_Argc() > 2 || ( Cmd_Argc() == 2 && Q_strlen( Cmd_Argv( 1 )) >= sizeof( slot )))
	{
		Con_Printf( "Usage: cof_load [slot of 1-24 characters]\n" ); return;
	}
	Q_strncpy( slot, Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "", sizeof( slot ));
	if( !SV_CoF_SessionLoad( slot )) Con_Printf( "Co-op load refused: start story co-op with the saved player limit and unique player names; check the checkpoint slot.\n" );
}

qboolean SV_CoF_SessionClientCommand( sv_client_t *client )
{
	if( SV_CoF_StoryCoopEnabled() && Cmd_Argc() == 2 )
	{
		qboolean tape_save = !Q_stricmp( Cmd_Argv( 0 ), "savehack" ) && Q_strlen( Cmd_Argv( 1 )) == 8 &&
			!Q_strncmp( Cmd_Argv( 1 ), "cofsave", 7 ) && Cmd_Argv( 1 )[7] >= '1' && Cmd_Argv( 1 )[7] <= '5';
		qboolean tape_load = !Q_stricmp( Cmd_Argv( 0 ), "cofload" ) && Q_strlen( Cmd_Argv( 1 )) == 1 &&
			Cmd_Argv( 1 )[0] >= '1' && Cmd_Argv( 1 )[0] <= '5';
		if( tape_save || tape_load )
		{
			char slot[25];
			qboolean success;
			Q_snprintf( slot, sizeof( slot ), "tape%c", Cmd_Argv( 1 )[tape_save ? 7 : 0] );
			if( tape_load && client != svs.clients )
			{
				SV_CoF_RetailProFont( client, 5, "Ask the host to load the co-op checkpoint.", true );
				return true;
			}
			success = tape_save ? SV_CoF_SessionSave( slot ) : SV_CoF_SessionLoad( slot );
			SV_CoF_RetailProFont( client, success ? 4 : 5, success ? "Co-op checkpoint accepted." : "Co-op checkpoint failed. See the host console.", true );
			return true;
		}
	}
	if( Q_stricmp( Cmd_Argv( 0 ), "cof_save" ) && Q_stricmp( Cmd_Argv( 0 ), "cof_load" )) return false;
	if( !SV_CoF_StoryCoopEnabled() || client != svs.clients )
	{
		SV_ClientPrintf( client, "Only the story co-op host can save or load the session.\n" );
		return true;
	}
	if( !Q_stricmp( Cmd_Argv( 0 ), "cof_save" )) CoF_SessionSave_f();
	else CoF_SessionLoad_f();
	return true;
}

void SV_CoF_SessionInit( void )
{
	Cmd_AddCommand( "cof_save", CoF_SessionSave_f, "story co-op: save the living party to a separate checkpoint slot" );
	Cmd_AddCommand( "cof_load", CoF_SessionLoad_f, "story co-op: restore a checkpoint without disconnecting the party" );
	cof_continue = Cvar_Get( "cof_continue", "0", 0, "story co-op: load the latest checkpoint once the host has joined (the tester app's Continue)" );
}
