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
	return 0;
}
