/* cof_coop_hud.h: client.dll's co-op HUD on every multiplayer map, not only on maps named cof_. */
#include <stdio.h>
#include "coop_hud_under_test.h"

static byte image[0x100000];
static int writes;
static int write_bytes( byte *p, const byte *value, int size ) { memcpy( p, value, size ); writes++; return 1; }

int main( void )
{
	const cof_gate_t *g = &cof_coop_hud_gate;
	byte *p = image + g->rva;
	int i;
	printf( "%s rva %X", g->name, g->rva );
	printf( " " ); for( i = 0; i < g->size; i++ ) printf( "%02x", g->original[i] );
	printf( " " ); for( i = 0; i < g->size; i++ ) printf( "%02x", g->changed[i] );
	printf( "\n" );
	printf( "wanted on=1 maxclients=4: %d\n", CoF_CoopHudWanted( 1, 4 ));
	printf( "wanted on=1 maxclients=1: %d\n", CoF_CoopHudWanted( 1, 1 )); /* single player keeps client.dll's own HUD */
	printf( "wanted on=0 maxclients=4: %d\n", CoF_CoopHudWanted( 0, 4 ));
	memcpy( p, g->original, g->size );
	i = CoF_GateSet( image, g, 1, write_bytes ); printf( "on: %d first %02x writes %d\n", i, p[0], writes );
	i = CoF_GateSet( image, g, 0, write_bytes ); printf( "off: %d first %02x writes %d\n", i, p[0], writes );
	return 0;
}
