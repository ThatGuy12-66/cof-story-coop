/* sv_cof_weather.h: the rain of a merged map's section, and the RainData that carries it. */
#include <stdio.h>
#include "weather_under_test.h"

static const cof_rain_settings_t settings[] = { { 0, 600.0f, 500.0f, 0 }, { 2, 800.0f, -200.0f, 1 }, { 2, 999.0f, 0.0f, 0 } };
static const cof_rain_constant_t constants[] = { { 0, 300, 1.0f, 2.0f, 3.0f, 4.0f }, { 0, 999, 0, 0, 0, 0 } };

static void section( int s, const cof_rain_t *own )
{
	cof_rain_msg_t m;
	CoF_RainForSection( settings, 3, constants, 2, s, own, &m );
	printf( "section %d: drips %d distance %g wind %g %g rand %g %g mode %d height %g\n", s, m.drips, m.distance,
		m.wind_x, m.wind_y, m.rand_x, m.rand_y, m.mode, m.height );
}

static void payload( int s, int large, const cof_rain_t *own )
{
	cof_rain_msg_t m;
	unsigned char out[COF_RAIN_PAYLOAD];
	int i;
	CoF_RainForSection( settings, 3, constants, 2, s, own, &m );
	CoF_RainPayload( &m, large, out );
	printf( "payload section %d large %d:", s, large );
	for( i = 0; i < COF_RAIN_PAYLOAD; i++ ) printf( " %02x", out[i] );
	printf( "\n" );
}

int main( void )
{
	cof_rain_t own;
	memset( &own, 0, sizeof( own ));
	own.drips = 50; own.wind_x = 5.0f; own.wind_y = 6.0f; own.rand_x = 7.0f; own.rand_y = 8.0f;
	section( 0, &own );  /* its own settings and constant rain_modify (the first of each) */
	section( 1, &own );  /* no rain_settings: no rain */
	section( 2, &own );  /* settings, no constant: the player's own (scripted) rain */
	payload( 0, 0, &own );
	payload( 2, 1, &own );
	return 0;
}
