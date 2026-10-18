/* cof_abi.h: the room for Cry of Fear's donator fields, and the level every command carries. */
#include "abi_under_test.h"

static void level( float cvar )
{
	printf( "level %g -> %d\n", cvar, CoF_DonatorLevel( cvar ));
}

int main( void )
{
	cof_usercmd_t cmd;
	usercmd_t plain;
	memset( &plain, 7, sizeof( plain ));
	CoF_DonatorCommand( &cmd, &plain, 1 );
	if( memcmp( &cmd.cmd, &plain, sizeof( plain )) || cmd.is_donator != 1 ) printf( "copy wrong\n" );
	printf( "usercmd %d is_donator at %d\n", (int)sizeof( cof_usercmd_t ), (int)offsetof( cof_usercmd_t, is_donator ));
	printf( "clientdata %d isdonator at %d\n", (int)sizeof( cof_clientdata_t ), (int)offsetof( cof_clientdata_t, isdonator ));
	level( 0.0f );  // retail's non-donor
	level( 1.0f );  // a donator
	level( 4.0f );  // never the developer level (debug commands)
	level( -1.0f );
	return 0;
}
