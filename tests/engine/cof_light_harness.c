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

static void night( int state, float next, double now )
{
	printf( "night state=%d next=%g now=%g: %d\n", state, next, now, CoF_NightVisionRequest( state, next, now ));
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
	night( 0, 4.0f, 5.0 );           // off: ask PostThink to switch it on (101)
	night( 1, 4.0f, 5.0 );           // on: ask it to switch off (100)
	night( 101, 4.0f, 5.0 );         // a switch under way
	night( 100, 4.0f, 5.0 );
	night( 0, 5.0f, 5.0 );           // the game's own delay: only after it
	night( 0, 666.0f, 700.0 );       // a scene's lock
	return 0;
}
