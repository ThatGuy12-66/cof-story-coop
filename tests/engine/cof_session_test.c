#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_session_data.h"
#include "session_fixture_paths.h"

int main( void )
{
	cof_session_data_t *metadata = calloc( 1, sizeof( *metadata ));
	char saved[32][32] = {{0}}, current[32][32] = {{0}};
	int slots[32], index;
	metadata->version = COF_SESSION_VERSION; metadata->clients = 2; metadata->entities = 3;
	strcpy( metadata->map, "c_start" );
	strcpy( metadata->players[0], "Host" ); strcpy( metadata->players[1], "Partner" );
	assert( CoF_SessionDataValid( metadata ));
	/* a checkpoint written before the story values grew (format 1) is refused */
	metadata->version = 1; assert( !CoF_SessionDataValid( metadata )); metadata->version = COF_SESSION_VERSION;
	strcpy( metadata->players[1], "HOST" ); assert( !CoF_SessionDataValid( metadata ));
	strcpy( metadata->players[1], "Partner" );
	strcpy( metadata->parked[0], "HOST" ); assert( !CoF_SessionDataValid( metadata )); metadata->parked[0][0] = 0;
	metadata->owners[2] = 1; assert( !CoF_SessionDataValid( metadata )); metadata->owners[2] = 0;
	strcpy( metadata->map, "../c_start" ); assert( !CoF_SessionDataValid( metadata ));
	assert( CoF_SessionSlotValid( "chapter_1-2" ) && !CoF_SessionSlotValid( "../auto" ));
	strcpy( saved[0], "Host" ); strcpy( saved[1], "Partner" );
	strcpy( current[0], "Partner" ); strcpy( current[1], "Host" );
	assert( CoF_SessionMatch( saved, current, slots ) && slots[0] == 2 && slots[1] == 1 );
	strcpy( current[1], "partner" ); assert( !CoF_SessionMatch( saved, current, slots ));
	strcpy( current[1], "Newcomer" ); assert( CoF_SessionMatch( saved, current, slots ) && !slots[0] );
	for( index = 0; fixture_paths[index]; index++ )
	{
		FILE *file = fopen( fixture_paths[index], "rb" );
		long length;
		unsigned char *data;
		int kind = fixture_paths[index][strlen( fixture_paths[index] ) - 1] - '0';
		assert( file );
		fseek( file, 0, SEEK_END ); length = ftell( file ); rewind( file );
		assert( length > 0 ); data = malloc( (size_t)length );
		assert( fread( data, 1, (size_t)length, file ) == (size_t)length ); fclose( file );
		assert( CoF_SessionNativeValid( data, (size_t)length, kind, COF_SNAPSHOT_ENTITIES ));
		assert( !CoF_SessionNativeValid( data, (size_t)length - 1, kind, COF_SNAPSHOT_ENTITIES ));
		memset( data, 0xff, 4 ); assert( !CoF_SessionNativeValid( data, (size_t)length, kind, COF_SNAPSHOT_ENTITIES ));
		free( data );
	}
	free( metadata );
	puts( "session validation scenarios passed" );
	return 0;
}
