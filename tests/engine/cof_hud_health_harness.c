/* cof_hud_health.h: the HealthCap type byte as the player's maximum, and the remade bar's fill. */
#include <stdio.h>
#include "hud_health_under_test.h"

int main( void )
{
	int types[] = { 0, 1, 2, 3, -1 };
	int i;
	for( i = 0; i < 5; i++ ) printf( "maximum %d: %d\n", types[i], CoF_HudHealthMaximum( types[i] ));
	printf( "fill 50 cap 0: %.2f\n", CoF_HudHealthFill( 50, 0 ));
	printf( "fill 120 cap 0: %.2f\n", CoF_HudHealthFill( 120, 0 ));
	printf( "fill 80 cap 1: %.2f\n", CoF_HudHealthFill( 80, 1 ));
	printf( "fill 90 cap 2: %.2f\n", CoF_HudHealthFill( 90, 2 )); /* never past the cap */
	printf( "fill -5 cap 0: %.2f\n", CoF_HudHealthFill( -5, 0 ));
	return 0;
}
