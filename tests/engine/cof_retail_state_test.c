#define main retail_fixture_main
#include "retail_fixture.c"
#undef main

int main( void )
{
	cof_story_values_t values, actual;
	unsigned char note[] = { 1, 1, 0xD2, 4, 3, 'n','e','r','d','s',0,0,0,0,0,0,0,0,0,0,0 };
	unsigned char document[] = { 1, 0, 0, 0, 0, 'a','.','t','x','t',0,0,0,0,0,0,0,0,0,'A',0,0 };
	retail_fixture_main();
	gate = 1;
	memset( private_data, 0, sizeof( private_data ));
	memset( &values, 0, sizeof( values ));
	values.code = 5247; values.code_type = 2;
	values.digits[0] = 1; values.digits[1] = 2; values.digits[2] = 3; values.digits[3] = 4;
	values.ending[2] = 1;
	assert( SV_CoF_RetailStoryWrite( &clients[0], &values ));
	assert( SV_CoF_RetailStoryRead( &clients[0], &actual ));
	assert( actual.code == 5247 && actual.code_type == 2 && actual.digits[3] == 4 && actual.ending[2] );
	assert( SV_CoF_RetailArchiveNote( &clients[0], 3, note, sizeof( note )));
	assert( last_msg[0] == 3 && last_msg[1] == 1 && last_msg[2] == 0xD2 && last_msg_reliable );
	assert( !strcmp( (char *)private_data[0] + 0x688 + 0x120 * 3 + 0x40, "nerds" ));
	assert( !SV_CoF_RetailArchiveNote( &clients[0], 0, note, sizeof( note )));
	assert( !SV_CoF_RetailArchiveNote( &clients[0], 17, note, sizeof( note )));
	assert( !SV_CoF_RetailArchiveNote( &clients[0], 3, document, sizeof( document )));
	assert( SV_CoF_RetailArchiveNote( &clients[0], 2, document, sizeof( document )));
	assert( !strcmp( (char *)private_data[0] + 0x688 + 0x120 * 2 + 0xB0, "A" ));
	clients[0].state = 2;
	assert( !SV_CoF_RetailStoryWrite( &clients[0], &values ));
	assert( !SV_CoF_RetailArchiveNote( &clients[0], 4, note, sizeof( note )));
	puts( "retail state scenarios passed" );
	return 0;
}
