/* sv_cof_sky.h: a merged map's 3D skies, each section its own. */
#include <stdio.h>
#include "sky_under_test.h"

static void hidden( int fx, int viewer, int model )
{
	printf( "hidden fx %d viewer %d model %d: %d\n", fx, viewer, model, CoF_SkyHidden( fx, viewer, model ));
}

static void height( const char *what, const unsigned char *payload, int size, int large )
{
	float z = 0.0f;
	int ok = CoF_SkyMarkerHeight( payload, size, large, &z );
	printf( "%s: %d z %g\n", what, ok, z );
}

int main( void )
{
	static const unsigned char small[] = { 0x10, 0x00, 0x20, 0x00, 0xC0, 0xF9, 0x03, 7, 9 }; /* z -1600/8, flags 3, amb, shade */
	static const unsigned char large[] = { 0x10, 0x00, 0x20, 0x00, 0x38, 0xFF, 0x00 };          /* z -200 */
	hidden( 70, 0, 1 );   /* another section's sky model */
	hidden( 137, 2, 1 );
	hidden( 184, 1, 0 );
	hidden( 70, 1, 1 );   /* its own */
	hidden( 70, -1, 1 );  /* not merged */
	hidden( 70, 0, -1 );  /* outside every section */
	hidden( 0, 0, 1 );    /* not a sky model */
	printf( "marker skymark_sky %d skymark_w %d SetFog %d\n", CoF_SkyMarker( "skymark_sky" ), CoF_SkyMarker( "skymark_w" ), CoF_SkyMarker( "SetFog" ));
	height( "eighths", small, sizeof( small ), 0 );
	height( "large coords", large, sizeof( large ), 1 );
	height( "too short", small, 5, 0 );
	return 0;
}
