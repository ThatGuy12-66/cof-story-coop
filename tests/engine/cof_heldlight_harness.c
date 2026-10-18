/* sv_cof_heldlight.h: the light of a held lantern, flare or book laser, as the other players see it. */
#include <stdio.h>
#include "heldlight_under_test.h"

static const char *names[] = { "none", "lantern", "flare", "booklaser" };

static void held( const char *what, int id, int right, int left, int flags )
{
	printf( "%s: %s\n", what, names[CoF_HeldLight( id, right, left, flags )] );
}

static void look( int kind, int r1, int r2, int r3 )
{
	int amt, rgb[3];
	CoF_HeldLightLook( kind, r1, r2, r3, &amt, rgb );
	printf( "%s rolls %d %d %d: renderamt %d colour %d %d %d\n", names[kind], r1, r2, r3, amt, rgb[0], rgb[1], rgb[2] );
}

int main( void )
{
	float origin[3] = { 100, 200, 300 }, ofs[3] = { 0, 0, 28 }, fwd[3] = { 1, 0, 0 }, right[3] = { 0, -1, 0 }, out[3];
	held( "lantern", 10, 0, 0, 0 );
	held( "lantern in a dual pair", 15, 10, 6, 0 );
	held( "flare, not lit", 13, 0, 0, 0 );
	held( "flare, lit", 13, 0, 0, 16 );
	held( "lit flare in a dual pair", 15, 2, 13, 16 );
	held( "book laser, lit", 25, 0, 0, 16 );
	held( "a pistol with the phone light", 2, 0, 0, 1 | 16 );
	held( "the flashlight weapon", 9, 0, 0, 8 );
	look( 1, 0, 0, 0 );
	look( 1, 999, 999, 999 );
	look( 2, 0, 0, 0 );
	look( 2, 999, 999, 999 );
	look( 3, 500, 500, 500 );
	CoF_HeldLightOrigin( origin, ofs, fwd, right, out );
	printf( "origin %g %g %g\n", out[0], out[1], out[2] );
	return 0;
}
