#ifndef SV_COF_SNAPSHOT_H
#define SV_COF_SNAPSHOT_H
#include <stdint.h>
#include <string.h>

#define COF_SNAPSHOT_MAGIC 0x31504643U
#define COF_SNAPSHOT_LIMIT (8U * 1024U * 1024U)
#define COF_SNAPSHOT_ENTITIES 32768U

typedef struct
{
	uint32_t magic, version, entities, player, tokens, token_bytes, data_bytes, weapons, checksum;
} cof_snapshot_header_t;

typedef struct
{
	uint32_t location, size;
	char classname[64];
} cof_snapshot_entity_t;

static uint32_t CoF_SnapshotChecksum( const unsigned char *data, size_t size )
{
	uint32_t value = 2166136261U;
	size_t index;
	for( index = 0; index < size; index++ ) value = ( value ^ data[index] ) * 16777619U;
	return value;
}

static int CoF_SnapshotValid( const void *data, size_t size )
{
	cof_snapshot_header_t header;
	const unsigned char *bytes = data, *tokens;
	size_t offset, position;
	uint32_t index;
	if( !data || size < sizeof( header ) || size > COF_SNAPSHOT_LIMIT ) return 0;
	memcpy( &header, data, sizeof( header ));
	if( header.magic != COF_SNAPSHOT_MAGIC || header.version != 1 || header.entities > COF_SNAPSHOT_ENTITIES ||
		!header.player || header.player >= header.entities || !header.tokens || header.tokens > 4095 ||
		header.token_bytes > COF_SNAPSHOT_LIMIT || header.data_bytes > COF_SNAPSHOT_LIMIT ) return 0;
	offset = sizeof( header ) + header.entities * sizeof( cof_snapshot_entity_t );
	if( offset > size || header.token_bytes > size - offset || header.data_bytes != size - offset - header.token_bytes ) return 0;
	if( header.checksum != ( CoF_SnapshotChecksum( bytes, sizeof( header ) - sizeof( header.checksum )) ^
		CoF_SnapshotChecksum( bytes + sizeof( header ), size - sizeof( header )))) return 0;
	tokens = bytes + offset;
	position = 0;
	for( index = 0; index < header.tokens; index++ )
	{
		while( position < header.token_bytes && tokens[position] ) position++;
		if( position == header.token_bytes ) return 0;
		position++;
	}
	if( position != header.token_bytes ) return 0;
	for( index = 0; index < header.entities; index++ )
	{
		cof_snapshot_entity_t entity;
		memcpy( &entity, bytes + sizeof( header ) + index * sizeof( entity ), sizeof( entity ));
		if( entity.location > header.data_bytes || entity.size > header.data_bytes - entity.location ||
			!memchr( entity.classname, 0, sizeof( entity.classname ))) return 0;
		if( index == header.player && ( !entity.size || strcmp( entity.classname, "player" ))) return 0;
		if( entity.size && index != header.player && strncmp( entity.classname, "weapon_", 7 )) return 0;
	}
	return 1;
}
#endif
