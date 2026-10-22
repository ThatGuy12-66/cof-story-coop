/* cof_coop_hud.h: client.dll's co-op HUD on every multiplayer map, not only on maps named cof_. */
#include <stdio.h>
#include "coop_hud_under_test.h"

static byte image[0x100000];
static int writes;
static int write_bytes( byte *p, const byte *value, int size ) { memcpy( p, value, size ); writes++; return 1; }

static void show( const cof_gate_t *g )
{
	int i;
	printf( "%s rva %X", g->name, g->rva );
	printf( " " ); for( i = 0; i < g->size; i++ ) printf( "%02x", g->original[i] );
	printf( " " ); for( i = 0; i < g->size; i++ ) printf( "%02x", g->changed[i] );
	printf( "\n" );
}

int main( void )
{
	const cof_gate_t *g = &cof_coop_hud_gate;
	byte *p = image + g->rva;
	int i;
	for( i = 0; i < COF_CLIENT_GATES; i++ ) show( cof_client_gates[i] );
	printf( "wanted on=1 maxclients=4: %d\n", CoF_CoopHudWanted( 1, 4 ));
	printf( "wanted on=1 maxclients=1: %d\n", CoF_CoopHudWanted( 1, 1 )); /* single player keeps client.dll's own HUD */
	printf( "wanted on=0 maxclients=4: %d\n", CoF_CoopHudWanted( 0, 4 ));
	/* the arms: Simon's on the story's maps, the police officer's on CoF's own co-op maps */
	printf( "arms c_start: %d, cm_park2: %d, c_cof_hospital: %d, single player: %d, off: %d, hud on cof_: %d\n",
		CoF_ClientGateWanted( COF_CLIENT_GATE_SIMON_ARMS, 1, 4, "c_start" ), CoF_ClientGateWanted( COF_CLIENT_GATE_SIMON_ARMS, 1, 2, "cm_park2" ),
		CoF_ClientGateWanted( COF_CLIENT_GATE_SIMON_ARMS, 1, 4, "c_cof_hospital" ), CoF_ClientGateWanted( COF_CLIENT_GATE_SIMON_ARMS, 1, 1, "c_start" ),
		CoF_ClientGateWanted( COF_CLIENT_GATE_SIMON_ARMS, 0, 4, "c_start" ), CoF_ClientGateWanted( 0, 1, 4, "c_cof_hospital" ));
	memcpy( p, g->original, g->size );
	i = CoF_GateSet( image, g, 1, write_bytes ); printf( "on: %d first %02x writes %d\n", i, p[0], writes );
	i = CoF_GateSet( image, g, 0, write_bytes ); printf( "off: %d first %02x writes %d\n", i, p[0], writes );
	return 0;
}
