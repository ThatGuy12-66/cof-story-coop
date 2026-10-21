/* gl_cof_texnames.h: the name an engine texture takes when the driver's can't be a slot. */
#include <stdio.h>
#include <string.h>
#include "texnames_under_test.h"

#define SLOTS 8192 // MAX_TEXTURES
static unsigned char used[SLOTS];
static unsigned int asked_past;

static int taken( unsigned int name, void *ctx )
{
	if( name >= SLOTS )
		asked_past++;
	return name < SLOTS && used[name];
}

static void show( const char *label )
{
	printf( "%s: %u\n", label, CoF_SlotTextureName( SLOTS, 5800, 5806, taken, NULL )); // SKYBOX_BASE_NUM .. + SKYBOX_MAX_SIDES
}

int main( void )
{
	show( "nothing loaded" );                 // 1, never 0 (GL's default texture)
	used[1] = used[2] = used[4] = 1;
	show( "1 2 4 in use" );                   // the lowest gap
	memset( used, 1, 5800 );
	show( "every slot below the sky in use" ); // past 5800 .. 5806, the names the skybox hack uses
	memset( used, 1, SLOTS );
	show( "every slot in use" );              // 0: the driver's own name stands
	printf( "names asked past the slots: %u\n", asked_past );
	return 0;
}
