/* sv_cof_signon.h: the size of a sign-on fragment and how many cl_dlmax pieces it goes out in. */
#include <stdio.h>
#include "signon_under_test.h"

#define MAX_FRAGMENT ( 64000 - ( 8 + 2 * 13 ))

static void show( const char *label, int split, int spawned )
{
	int size = CoF_SignonFragmentSize( split, spawned, COF_SIGNON_WINDOW, MAX_FRAGMENT );
	if( size > 0 )
		printf( "%s: %d bytes in %d pieces of at most %d\n", label, size, CoF_SignonPieces( split, size ), split );
	else
		printf( "%s: the engine's\n", label );
}

int main( void )
{
	show( "co-op cl_dlmax 1400 signing on", 1400, 0 );
	show( "co-op cl_dlmax 1400 in game", 1400, 1 );
	show( "no split size", 0, 0 );
	show( "cl_dlmax 508", 508, 0 );
	show( "cl_dlmax 1200", 1200, 0 );
	show( "cl_dlmax 64000", 64000, 0 );
	printf( "window 1: %d\n", CoF_SignonFragmentSize( 1400, 0, 1, MAX_FRAGMENT ));
	/* 1.1.9 (N1): the host's cof_signon_window, as the server reads it */
	printf( "cvar 10: %d, 1: %d, 0: %d, -3: %d, 2.9: %d, 1000: %d\n", CoF_SignonWindow( 10.0f ), CoF_SignonWindow( 1.0f ),
		CoF_SignonWindow( 0.0f ), CoF_SignonWindow( -3.0f ), CoF_SignonWindow( 2.9f ), CoF_SignonWindow( 1000.0f ));
	printf( "cvar 1 signing on: %d\n", CoF_SignonFragmentSize( 1400, 0, CoF_SignonWindow( 1.0f ), MAX_FRAGMENT ));
	printf( "cvar 1000 signing on: %d\n", CoF_SignonFragmentSize( 1400, 0, CoF_SignonWindow( 1000.0f ), MAX_FRAGMENT ));
	return 0;
}
