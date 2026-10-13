#define main fixture_main
#include "map_fixture.c"
#undef main
#undef SV_CoF_MapExitModels
#include "map_cases.h"

int main( void )
{
	int index, models[16], count;
	qboolean fires, scripted;
	for( index = 0; index < (int)( sizeof( cases ) / sizeof( cases[0] )); index++ )
	{
		FILE *input = fopen( cases[index].entities, "rb" );
		long length;
		char *contents;
		assert( input );
		fseek( input, 0, SEEK_END ); length = ftell( input ); rewind( input );
		assert( length > 0 );
		contents = malloc( (size_t)length + 1 ); assert( contents );
		assert( fread( contents, 1, (size_t)length, input ) == (size_t)length );
		contents[length] = 0; fclose( input );
		map_text = contents;
		count = SV_CoF_MapExitModels( cases[index].source, cases[index].destination, cases[index].landmark, models, 16, &fires, &scripted );
		assert( count > 0 && scripted == cases[index].scripted );
		free( contents );
	}
	printf( "shipped exit cases passed: %d\n", index );
	return 0;
}
