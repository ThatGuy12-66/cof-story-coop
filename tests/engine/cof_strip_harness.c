/* cof_strip.h: the engine's own message-strip lines, shown although the player turned subtitles off. */
#include <stdio.h>
#include "strip_under_test.h"

static void own( const char *what, const unsigned char *payload, int size )
{
	printf( "%s: %d\n", what, CoF_StripOwnLine( payload, size ));
}

int main( void )
{
	unsigned char payload[192];
	static const unsigned char game[] = { 5, 'H', 'e', 'l', 'p', 0 };
	static const unsigned char trailing[] = { 5, 'H', 'i', 0, 0xC0, 0xF5, 0 };
	int n = CoF_StripPayload( payload, sizeof( payload ), 4, "Simon found the hall key" ), i;
	printf( "payload %d:", n );
	for( i = 0; i < 2; i++ ) printf( " %02x", payload[i] );
	printf( " ... %02x %02x %02x\n", payload[n - 3], payload[n - 2], payload[n - 1] );
	printf( "too long: %d\n", CoF_StripPayload( payload, 8, 4, "Simon found the hall key" ));
	own( "ours", payload, n );
	own( "the game's", game, sizeof( game ));
	own( "a byte after the mark", trailing, sizeof( trailing ));
	own( "cut short", payload, n - 1 );
	printf( "cl_subtitles off, ours on the strip: %g\n", CoF_StripSubtitles( "cl_subtitles", 0.0f, 10.0, 12.0 ));
	printf( "cl_subtitles off, ours gone: %g\n", CoF_StripSubtitles( "cl_subtitles", 0.0f, 13.0, 12.0 ));
	printf( "cl_subtitles on: %g\n", CoF_StripSubtitles( "cl_subtitles", 1.0f, 13.0, 12.0 ));
	printf( "another cvar: %g\n", CoF_StripSubtitles( "fps_max", 0.0f, 10.0, 12.0 ));
	return 0;
}
