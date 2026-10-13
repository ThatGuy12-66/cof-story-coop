#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_snapshot.h"

int main( void )
{
	unsigned char data[4096] = {0};
	cof_snapshot_header_t header = {0};
	cof_snapshot_entity_t *entities;
	size_t size;
	header.magic = COF_SNAPSHOT_MAGIC; header.version = 1;
	header.entities = 3; header.player = 1; header.tokens = 2; header.token_bytes = 3; header.data_bytes = 4;
	size = sizeof( header ) + 3 * sizeof( cof_snapshot_entity_t ) + 7;
	memcpy( data, &header, sizeof( header ));
	entities = (cof_snapshot_entity_t *)( data + sizeof( header ));
	entities[1].size = 2; strcpy( entities[1].classname, "player" );
	entities[2].location = 2; entities[2].size = 2; strcpy( entities[2].classname, "weapon_glock" );
	data[sizeof( header ) + 3 * sizeof( *entities )] = 'a';
	header.checksum = CoF_SnapshotChecksum( (const unsigned char *)&header, sizeof( header ) - 4 ) ^ CoF_SnapshotChecksum( data + sizeof( header ), size - sizeof( header ));
	memcpy( data, &header, sizeof( header ));
	assert( CoF_SnapshotValid( data, size ));
	header.weapons ^= 1; memcpy( data, &header, sizeof( header ));
	assert( !CoF_SnapshotValid( data, size ));
	header.weapons ^= 1; memcpy( data, &header, sizeof( header ));
	assert( !CoF_SnapshotValid( data, size - 1 ));
	data[size - 1] ^= 1; assert( !CoF_SnapshotValid( data, size )); data[size - 1] ^= 1;
	header.entities = 0xffffffffU; memcpy( data, &header, sizeof( header ));
	assert( !CoF_SnapshotValid( data, size ));
	header.entities = 3; header.player = 3; memcpy( data, &header, sizeof( header ));
	assert( !CoF_SnapshotValid( data, size ));
	header.player = 1; memcpy( data, &header, sizeof( header ));
	entities[2].size = 2000;
	header.checksum = CoF_SnapshotChecksum( (const unsigned char *)&header, sizeof( header ) - 4 ) ^ CoF_SnapshotChecksum( data + sizeof( header ), size - sizeof( header ));
	memcpy( data, &header, sizeof( header ));
	assert( !CoF_SnapshotValid( data, size ));
	puts( "snapshot validation scenarios passed" );
	return 0;
}
