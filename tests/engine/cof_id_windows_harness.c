/* identification.c's Windows sources (1.2.8): read in this process, no child process; each read gives the same
   answer twice; a buffer too small for the value gives nothing. The values are machine identifiers: never printed. */
#include <windows.h>
#pragma comment( lib, "advapi32.lib" )
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef int qboolean;
typedef unsigned int uint;
#define true 1
#define false 0
#define Q_tolower( c ) ((char)tolower( (unsigned char)( c )))
#define Q_snprintf _snprintf

#include "id_under_test.c"

#define CHECK( cond, what ) do { if( !( cond )) { printf( "FAIL: %s\n", what ); return 1; } } while( 0 )

int main( void )
{
	char a[256], b[256], tiny[8];  /* not "small": rpcndr.h defines it */
	int i, found = 0;

	for( i = 0; i < ID_WINDOWS_SOURCES; i++ )
	{
		qboolean first = ID_WindowsSource( i, a, sizeof( a ));
		qboolean second = ID_WindowsSource( i, b, sizeof( b ));

		CHECK( first == second && ( !first || !strcmp( a, b )), "a source gives the same answer twice" );
		CHECK( !first || strlen( a ) >= 8, "a source read in full" );
		printf( "source %d: %s\n", i, first ? "read" : "none" );
		found += first;
	}
	CHECK( found == ID_WINDOWS_SOURCES, "both sources read on this machine" );

	// MachineGuid is 36 characters: it does not fit, and nothing half-read is used
	CHECK( !ID_WindowsSource( 0, tiny, sizeof( tiny )) && !tiny[0], "a value too long for the buffer gives nothing" );
	CHECK( !ID_WindowsSource( 7, a, sizeof( a )), "an unknown source gives nothing" );

	printf( "windows id sources passed\n" );
	return 0;
}
