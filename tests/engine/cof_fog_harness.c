/* sv_cof_fog.h: which SetFog a player should get, merged or not. */
#include <stdio.h>
#include <string.h>
#include "fog_under_test.h"

static void show( const char *what, int result, const byte *p )
{
	printf( "%s: %d [%d %d %d %d %d %d]\n", what, result, p[0], p[1], p[2], (short)( p[3] | p[4] << 8 ), (short)( p[5] | p[6] << 8 ), (short)( p[7] | p[8] << 8 ));
}

int main( void )
{
	/* section 0's fog is on, section 1 has a fog off, section 2 none */
	cof_fog_t fogs[2] = {
		{ 0, 1, 0, { 40, 50, 60 }, 100, 900 },
		{ 1, 0, 0, { 200, 10, 10 }, 50, 400 },
	};
	byte p[9], got[9];
	CoF_FogPayload( p, fogs[0].color, 5, 100, 900 ); show( "payload", 1, p );
	/* the game's own messages, by the receiver's section */
	printf( "fits 0 own: %d\n", CoF_FogFits( fogs, 2, 0, p ));
	printf( "fits 2 other's: %d\n", CoF_FogFits( fogs, 2, 2, p ));
	CoF_FogPayload( p, fogs[1].color, -3, 50, 400 );
	printf( "fits 1 its fade out: %d\n", CoF_FogFits( fogs, 2, 1, p ));
	memset( p, 0, sizeof( p ));
	printf( "fits 1 off: %d\n", CoF_FogFits( fogs, 2, 1, p ));
	printf( "fits 0 off: %d\n", CoF_FogFits( fogs, 2, 0, p ));  /* its fog is on: another's off is not for it */
	printf( "fits all off: %d\n", CoF_FogFits( fogs, 2, -1, p )); /* not merged: as the game says */
	/* what a player should have now (respawn, join, section move): instant */
	show( "wanted 0", CoF_FogWanted( fogs, 2, 0, got ), got );
	show( "wanted 1", CoF_FogWanted( fogs, 2, 1, got ), got );
	show( "wanted all", CoF_FogWanted( fogs, 2, -1, got ), got );
	fogs[0].active = 0; fogs[0].fading = 1; /* fading out: off */
	show( "wanted 0 fading out", CoF_FogWanted( fogs, 2, 0, got ), got );
	/* what a frame sends: after a reset only a fog that is on; a section move and a new map's first frame either */
	printf( "send reset on: %d, off: %d\n", CoF_FogSend( 1, 0, 0, 1 ), CoF_FogSend( 1, 0, 0, 0 ));
	printf( "send section on: %d, off: %d\n", CoF_FogSend( 0, 1, 0, 1 ), CoF_FogSend( 0, 1, 0, 0 ));
	printf( "send arrival on: %d, off: %d\n", CoF_FogSend( 0, 0, 1, 1 ), CoF_FogSend( 0, 0, 1, 0 ));
	printf( "send nothing new: %d\n", CoF_FogSend( 0, 0, 0, 1 ));
	return 0;
}
