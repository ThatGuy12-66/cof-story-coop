#include "sv_cof_snapshot.h"
#include "sv_cof_recovery.h"

qboolean SV_CoF_CaptureInventory( sv_client_t *cl, byte **output, size_t *size )
{
	SAVERESTOREDATA *saved;
	cof_snapshot_header_t header;
	cof_snapshot_entity_t *rows;
	byte *result, *cursor;
	size_t token_bytes = 0, total;
	int index, player_id;
	if( !cl || !output || !size || !SV_CoF_StoryCoopEnabled() || !SV_IsValidEdict( cl->edict ) ||
		svgame.globals->pSaveData || svgame.numEntities > COF_SNAPSHOT_ENTITIES ) return false;
	player_id = NUM_FOR_EDICT( cl->edict );
	saved = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS );
	InitEntityTable( saved, svgame.numEntities );
	saved->currentIndex = player_id;
	if( !SV_CoF_RetailInventoryIO( cl, saved, false )) { SaveFinish( saved ); return false; }
	saved->pTable[player_id].classname = SV_AllocString( "player" );
	saved->pTable[player_id].size = saved->size;
	for( index = svs.maxclients + 1; index < saved->tableCount; index++ )
	{
		ENTITYTABLE *entry = &saved->pTable[index];
		if( !SV_CoF_RetailOwnsWeapon( cl->edict, entry->pent )) continue;
		entry->location = saved->size;
		saved->currentIndex = index;
		svgame.dllFuncs.pfnSave( entry->pent, saved );
		if( !entry->size ) { SaveFinish( saved ); return false; }
	}
	for( index = 0; index < saved->tokenCount; index++ ) token_bytes += saved->pTokens[index] ? Q_strlen( saved->pTokens[index] ) + 1 : 1;
	total = sizeof( header ) + saved->tableCount * sizeof( *rows ) + token_bytes + saved->size;
	if( total > COF_SNAPSHOT_LIMIT || saved->size <= 0 ) { SaveFinish( saved ); return false; }
	result = Mem_Calloc( host.mempool, total );
	memset( &header, 0, sizeof( header ));
	header.magic = COF_SNAPSHOT_MAGIC; header.version = 1;
	header.entities = saved->tableCount; header.player = player_id;
	header.tokens = saved->tokenCount; header.token_bytes = (uint32_t)token_bytes;
	header.data_bytes = saved->size; header.weapons = cl->edict->v.weapons;
	rows = (cof_snapshot_entity_t *)( result + sizeof( header ));
	for( index = 0; index < saved->tableCount; index++ )
	{
		ENTITYTABLE *entry = &saved->pTable[index];
		if( !entry->size ) continue;
		rows[index].location = entry->location; rows[index].size = entry->size;
		Q_strncpy( rows[index].classname, SV_GetString( entry->classname ), sizeof( rows[index].classname ));
	}
	cursor = result + sizeof( header ) + saved->tableCount * sizeof( *rows );
	for( index = 0; index < saved->tokenCount; index++ )
	{
		const char *token = saved->pTokens[index] ? saved->pTokens[index] : "";
		size_t length = Q_strlen( token ) + 1;
		memcpy( cursor, token, length ); cursor += length;
	}
	memcpy( cursor, saved->pBaseData, saved->size );
	header.checksum = CoF_SnapshotChecksum( (const byte *)&header, sizeof( header ) - sizeof( header.checksum )) ^
		CoF_SnapshotChecksum( result + sizeof( header ), total - sizeof( header ));
	memcpy( result, &header, sizeof( header ));
	SaveFinish( saved );
	if( !CoF_SnapshotValid( result, total )) { Mem_Free( result ); return false; }
	*output = result; *size = total;
	return true;
}

qboolean SV_CoF_RestoreInventory( sv_client_t *cl, const byte *snapshot, size_t size )
{
	cof_snapshot_header_t header;
	const cof_snapshot_entity_t *rows;
	const char *tokens, *data;
	SAVERESTOREDATA *saved;
	edict_t **originals;
	int index, original_count = 0;
	qboolean restored = true;
	if( !cl || !SV_CoF_StoryCoopEnabled() || !SV_IsValidEdict( cl->edict ) || svgame.globals->pSaveData || !CoF_SnapshotValid( snapshot, size )) return false;
	memcpy( &header, snapshot, sizeof( header ));
	rows = (const cof_snapshot_entity_t *)( snapshot + sizeof( header ));
	tokens = (const char *)( rows + header.entities ); data = tokens + header.token_bytes;
	originals = Mem_Calloc( host.mempool, sizeof( edict_t * ) * svgame.numEntities );
	for( index = svs.maxclients + 1; index < svgame.numEntities; index++ )
		if( SV_CoF_RetailOwnsWeapon( cl->edict, SV_EdictNum( index ))) originals[original_count++] = SV_EdictNum( index );
	saved = SaveInit( header.data_bytes + header.token_bytes, header.tokens );
	memcpy( saved->pBaseData, data, header.data_bytes );
	memcpy( saved->pBaseData + header.data_bytes, tokens, header.token_bytes );
	tokens = saved->pBaseData + header.data_bytes;
	for( index = 0; index < (int)header.tokens; index++ )
	{
		saved->pTokens[index] = *tokens ? (char *)tokens : NULL;
		tokens += Q_strlen( tokens ) + 1;
	}
	saved->tableCount = header.entities;
	saved->pTable = Mem_Calloc( host.mempool, sizeof( ENTITYTABLE ) * header.entities );
	saved->fUseLandmark = true;
	saved->time = sv.time;
	for( index = 0; index < (int)header.entities; index++ )
	{
		ENTITYTABLE *entry = &saved->pTable[index];
		entry->id = index; entry->location = rows[index].location; entry->size = rows[index].size;
		if( !entry->size ) continue;
		entry->classname = SV_AllocString( rows[index].classname );
		entry->pent = index == (int)header.player ? cl->edict : SV_CreateNamedEntity( NULL, entry->classname );
		if( !SV_IsValidEdict( entry->pent )) { restored = false; break; }
	}
	for( index = 0; restored && index < (int)header.entities; index++ )
	{
		ENTITYTABLE *entry = &saved->pTable[index];
		if( !entry->size || index == (int)header.player ) continue;
		saved->currentIndex = index; saved->size = entry->location;
		saved->pCurrentData = saved->pBaseData + entry->location;
		if( svgame.dllFuncs.pfnRestore( entry->pent, saved, 0 ) < 0 ) restored = false;
	}
	if( restored )
	{
		saved->currentIndex = header.player; saved->size = rows[header.player].location;
		saved->pCurrentData = saved->pBaseData + saved->size;
		restored = SV_CoF_RetailInventoryIO( cl, saved, true );
	}
	if( restored )
	{
		for( index = 0; index < original_count; index++ ) if( SV_IsValidEdict( originals[index] )) SV_FreeEdict( originals[index] );
		cl->edict->v.weapons = header.weapons;
		SV_CoF_RetailInventoryReady( cl );
	}
	else
	{
		for( index = 0; index < (int)header.entities; index++ )
			if( index != (int)header.player && SV_IsValidEdict( saved->pTable[index].pent )) SV_FreeEdict( saved->pTable[index].pent );
	}
	SaveFinish( saved );
	Mem_Free( originals );
	return restored;
}
