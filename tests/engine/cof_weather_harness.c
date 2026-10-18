/* sv_cof_weather.h: player 1's rain (where the game's rain_modify writes) mirrored to another player. */
#include <stdio.h>
#include "weather_under_test.h"

static unsigned char host[0x2810], player[0x2810];

static void rain( unsigned char *pv, int drips, float wind, int ideal, float ideal_wind, float end )
{
	cof_rain_t r;
	memset( &r, 0, sizeof( r ));
	r.drips = drips; r.wind_x = wind; r.ideal_drips = ideal; r.ideal_wind_x = ideal_wind; r.end_fade = end;
	memcpy( pv + COF_RAIN_BLOCK, &r, sizeof( r ));
	memset( pv + COF_RAIN_NEEDS_UPDATE, 0, 4 );
}

static void mirror( const char *what )
{
	cof_rain_t r;
	int copied = CoF_RainMirror( host, player ), update;
	memcpy( &r, player + COF_RAIN_BLOCK, sizeof( r ));
	memcpy( &update, player + COF_RAIN_NEEDS_UPDATE, 4 );
	printf( "%s: copied %d drips %d ideal %d end %g update %d\n", what, copied, r.drips, r.ideal_drips, r.end_fade, update );
}

int main( void )
{
	printf( "block %X size %d update %X\n", COF_RAIN_BLOCK, (int)sizeof( cof_rain_t ), COF_RAIN_NEEDS_UPDATE );
	rain( host, 300, 1.0f, 0, 0.0f, 0.0f ); rain( player, 300, 1.0f, 0, 0.0f, 0.0f );
	mirror( "same rain" );
	rain( host, 900, 2.0f, 0, 0.0f, 0.0f ); rain( player, 300, 1.0f, 0, 0.0f, 0.0f );
	mirror( "scripted change" );
	rain( host, 400, 1.0f, 900, 2.0f, 60.0f ); rain( player, 300, 1.0f, 0, 0.0f, 0.0f );
	mirror( "scripted fade" );
	rain( host, 500, 1.2f, 900, 2.0f, 60.0f ); rain( player, 450, 1.1f, 900, 2.0f, 60.0f );
	mirror( "both fading, a step apart" );
	rain( host, 900, 2.0f, 900, 2.0f, 0.0f ); rain( player, 800, 1.9f, 900, 2.0f, 60.0f );
	mirror( "host's fade over a frame early" );
	rain( host, 900, 2.0f, 0, 0.0f, 0.0f ); rain( player, 0, 0.0f, 0, 0.0f, 0.0f );
	mirror( "respawned (Spawn zeroes the rain)" );
	return 0;
}
