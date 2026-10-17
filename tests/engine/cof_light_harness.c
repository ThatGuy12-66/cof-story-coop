/* sv_cof_light.h: when the co-op flashlight command is refused, and when a torch that is on gets switched off. */
#include <stdio.h>
#include "light_under_test.h"

static void refused( const char *cmd, int story, int allowed )
{
	printf( "refused %s story=%d allowed=%d: %d\n", cmd, story, allowed, CoF_LightRefused( cmd, story, allowed ));
}

static void off( float frags, int allowed, double now, double next_try )
{
	printf( "off frags=%g allowed=%d now=%g next=%g: %d\n", frags, allowed, now, next_try,
		CoF_LightSwitchOff( frags, allowed, now, next_try ));
}

int main( void )
{
	refused( "flashlight", 1, 0 );   // story co-op, the host left it off: refused
	refused( "flashlight", 1, 1 );   // the host allows it
	refused( "flashlight", 0, 0 );   // not story co-op: the game's own rules
	refused( "flashlights", 1, 0 );  // other commands pass
	refused( "weapontoggle", 1, 0 ); // the Glock's taclight is the game's own light
	off( 32.0f, 0, 5.0, 4.0 );       // the co-op torch is on, not allowed: switch it off
	off( 32.0f, 1, 5.0, 4.0 );       // allowed: leave it
	off( 32.0f, 0, 3.0, 4.0 );       // tried a moment ago (refused in a camera): wait
	off( 1.0f, 0, 5.0, 4.0 );        // the phone's light
	off( 8.0f, 0, 5.0, 4.0 );        // the flashlight weapon
	off( 0.0f, 0, 5.0, 4.0 );        // no light
	return 0;
}
