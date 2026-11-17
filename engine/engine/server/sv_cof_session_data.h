#ifndef SV_COF_SESSION_DATA_H
#define SV_COF_SESSION_DATA_H
#include "sv_cof_state_data.h"
#include "sv_cof_share_state.h"
#include "sv_cof_archive.h"

// 2: story values gained the doctor weapon and cof_begingame's stats (2026-10-12).
#define COF_SESSION_VERSION 2

typedef struct
{
	uint32_t version, clients, entities, maps;
	char map[64], history[256][64];
	char players[32][32], parked[32][32];
	int owners[COF_SNAPSHOT_ENTITIES];
	cof_state_data_t story;
	cof_share_archive_t share;
} cof_session_data_t;

static int CoF_SessionSlotValid( const char *slot )
{
	size_t index, length;
	if( !slot ) return 0;
	length = strlen( slot );
	if( !length || length > 24 || !CoF_ArchiveName( slot )) return 0;
	for( index = 0; index < length; index++ )
		if( !( ( slot[index] >= 'a' && slot[index] <= 'z' ) || ( slot[index] >= '0' && slot[index] <= '9' ) || slot[index] == '_' || slot[index] == '-' )) return 0;
	return 1;
}

static int CoF_SessionDataValid( const cof_session_data_t *data )
{
	uint32_t index, previous;
	int identities = 0;
	if( !data || data->version != COF_SESSION_VERSION || data->clients < 2 || data->clients > 32 ||
		data->entities <= data->clients || data->entities > COF_SNAPSHOT_ENTITIES || data->maps > 256 ||
		!CoF_ArchiveName( data->map ) || strchr( data->map, '.' ) || !CoF_ShareArchiveValid( &data->share )) return 0;
	for( index = 0; index < data->maps; index++ )
	{
		if( !CoF_ArchiveName( data->history[index] ) || strchr( data->history[index], '.' )) return 0;
		for( previous = 0; previous < index; previous++ )
			if( CoF_ArchiveSameName( data->history[index], data->history[previous] )) return 0;
	}
	for( index = 0; index < 32; index++ )
	{
		if( !memchr( data->players[index], 0, 32 ) || !memchr( data->parked[index], 0, 32 )) return 0;
		for( previous = 0; data->players[index][0] && previous < index; previous++ )
			if( CoF_ArchiveSameName( data->players[index], data->players[previous] )) return 0;
		if( index >= data->clients && data->players[index][0] ) return 0;
		identities += !!data->players[index][0] + !!data->parked[index][0];
		for( previous = 0; data->parked[index][0] && previous < index; previous++ )
			if( CoF_ArchiveSameName( data->parked[index], data->parked[previous] )) return 0;
	}
	if( identities > 32 ) return 0;
	for( index = 0; index < 32; index++ )
		for( previous = 0; data->parked[index][0] && previous < 32; previous++ )
			if( CoF_ArchiveSameName( data->parked[index], data->players[previous] )) return 0;
	for( index = 0; index < data->entities; index++ )
		if( data->owners[index] < 0 || data->owners[index] > (int)data->clients ||
			( data->owners[index] && ( index <= data->clients || !data->players[data->owners[index] - 1][0] ))) return 0;
	if( data->story.note_count < 0 || data->story.note_count > COF_STATE_NOTES ||
		!memchr( data->story.values.objective, 0, COF_STATE_TEXT ) || !memchr( data->story.values.objective_file, 0, COF_STATE_TEXT )) return 0;
	for( index = 0; index < (uint32_t)data->story.note_count; index++ )
		if( !CoF_StateNoteValid( data->story.notes[index].message, data->story.notes[index].size )) return 0;
	return 1;
}

static int CoF_SessionNativeValid( const unsigned char *data, size_t size, int kind, uint32_t entities )
{
	uint32_t header[6], count, token_count, token_bytes, index;
	size_t header_size, position, end;
	if( !data || size < 4 || size > COF_ARCHIVE_LIMIT ) return 0;
	if( kind == 3 )
	{
		memcpy( &count, data, 4 );
		if( count > entities || size != 4 + (size_t)count * 4 ) return 0;
		for( index = 0; index < count; index++ )
		{
			uint32_t entity;
			memcpy( &entity, data + 4 + index * 4, 4 );
			if( entity >= entities ) return 0;
		}
		return 1;
	}
	header_size = kind == 1 ? 24 : 20;
	if( size < header_size ) return 0;
	memcpy( header, data, header_size );
	if( header[0] != ( kind == 1 ? 0x564c4156U : 0x5641534aU ) ||
		header[1] != ( kind == 1 ? 0x71U : 0x67U )) return 0;
	if( kind == 1 && ( !header[3] || header[3] > COF_SNAPSHOT_ENTITIES )) return 0;
	token_count = header[kind == 1 ? 4 : 3]; token_bytes = header[kind == 1 ? 5 : 4];
	if( !token_count || token_count > 4095 || token_bytes > size - header_size ||
		header[2] != size - header_size - token_bytes ) return 0;
	position = header_size; end = position + token_bytes;
	for( index = 0; index < token_count; index++ )
	{
		while( position < end && data[position] ) position++;
		if( position == end ) return 0;
		position++;
	}
	if( position != end ) return 0;
	while( position < size )
	{
		uint16_t field[2];
		if( size - position < sizeof( field )) return 0;
		memcpy( field, data + position, sizeof( field )); position += sizeof( field );
		if( field[0] > size - position || field[1] >= token_count ) return 0;
		position += field[0];
	}
	return position == size;
}

static int CoF_SessionMatch( const char saved[32][32], const char current[32][32], int slots[32] )
{
	int source, target, previous;
	memset( slots, 0, sizeof( int ) * 32 );
	for( target = 0; target < 32; target++ )
		for( previous = 0; current[target][0] && previous < target; previous++ )
			if( CoF_ArchiveSameName( current[target], current[previous] )) return 0;
	for( source = 0; source < 32; source++ )
		for( target = 0; saved[source][0] && target < 32; target++ )
			if( CoF_ArchiveSameName( saved[source], current[target] )) slots[source] = target + 1;
	return 1;
}
#endif
