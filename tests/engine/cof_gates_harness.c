/* sv_cof_gates.h: a gate written over its retail bytes, put back, and never over anything else. */
#include <stdio.h>
#include "gates_under_test.h"

static byte image[0x100000];
static int writes;
static int write_bytes( byte *p, const byte *value, int size ) { memcpy( p, value, size ); writes++; return 1; }

int main( void )
{
	const cof_gate_t *g = &cof_gates[COF_GATE_FRIENDLY_FIRE];
	byte *p = image + g->rva;
	int ok;
	printf( "%s rva %X size %d\n", g->name, g->rva, g->size );
	memcpy( p, g->original, g->size );
	ok = CoF_GateSet( image, g, 1, write_bytes ); printf( "on: %d bytes %02x %02x writes %d\n", ok, p[0], p[1], writes );
	ok = CoF_GateSet( image, g, 1, write_bytes ); printf( "on again: %d writes %d\n", ok, writes ); /* already there */
	ok = CoF_GateSet( image, g, 0, write_bytes ); printf( "off: %d bytes %02x %02x writes %d\n", ok, p[0], p[1], writes );
	p[0] = 0x12; p[1] = 0x34; /* not the DLL this gate was made for */
	ok = CoF_GateSet( image, g, 1, write_bytes ); printf( "foreign: %d bytes %02x %02x writes %d\n", ok, p[0], p[1], writes );
	for( ok = 0; ok < COF_GATES; ok++ ) /* the whole table, for the check against the retail DLL */
	{
		int i;
		printf( "gate %s %X", cof_gates[ok].name, cof_gates[ok].rva );
		printf( " " ); for( i = 0; i < cof_gates[ok].size; i++ ) printf( "%02x", cof_gates[ok].original[i] );
		printf( " " ); for( i = 0; i < cof_gates[ok].size; i++ ) printf( "%02x", cof_gates[ok].changed[i] );
		printf( "\n" );
	}
	return 0;
}
