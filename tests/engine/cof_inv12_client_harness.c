/* The client's 12-slot store (cof_inventory12_state.h): which Inventory messages
 * reach client.dll, what the engine keeps, the tac-glock rename and the pickup line. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "state_under_test.h"

/* byte slot, byte reset, string descriptor, byte three, byte silent, byte tac (hl.dll 100E4360) */
static int Message( unsigned char *b, int slot, const char *d, int three, int silent, int tac )
{
	int n = 0;
	b[n++] = (unsigned char)slot; b[n++] = 0;
	strcpy( (char *)b + n, d ); n += (int)strlen( d ) + 1;
	b[n++] = (unsigned char)three; b[n++] = (unsigned char)silent; b[n++] = (unsigned char)tac;
	return n;
}

int main( void )
{
	static cof_inv12_slots_t s;
	cof_inv12_message_t m;
	unsigned char b[400], line[200];
	char long_name[200];
	int n;

	n = Message( b, 3, "inventoryitems/apartmentkey.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && m.slot == 3 && !strcmp( m.descriptor, "inventoryitems/apartmentkey.txt" ));
	assert( CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD && !strcmp( s.slot[3], m.descriptor ));

	/* 7..12 are kept from client.dll, which would write them into read-only memory */
	n = Message( b, 9, "inventoryitems/weapons/weapon_glock.txt", 0, 1, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && m.silent == 1 );
	assert( CoF_Inv12Route( &s, &m ) == COF_INV12_KEPT && !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glock.txt" ));
	n = Message( b, 12, "", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_KEPT && !s.slot[12][0] );

	/* slot 0 and 13+: dropped, nothing stored */
	n = Message( b, 0, "inventoryitems/x.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_DROPPED );
	n = Message( b, 13, "inventoryitems/x.txt", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_DROPPED );

	/* malformed: too short, or no terminator, or the trailing bytes missing */
	assert( !CoF_Inv12Parse( b, 5, &m ));
	memset( b, 'a', 40 ); b[0] = 3; b[1] = 0;
	assert( !CoF_Inv12Parse( b, 40, &m ));
	n = Message( b, 3, "inventoryitems/x.txt", 0, 0, 0 );
	assert( !CoF_Inv12Parse( b, n - 1, &m ));

	/* a descriptor longer than 127 characters is cut like client.dll's strncpy */
	memset( long_name, 'q', 150 ); long_name[150] = 0;
	n = Message( b, 8, long_name, 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && strlen( m.descriptor ) == 127 );

	/* the tac glock: client.dll renames slots 1..6, the engine 7..12, both ways */
	n = Message( b, 4, "", 0, 0, 1 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD );
	assert( !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glocktac.txt" ));
	n = Message( b, 4, "", 0, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD );
	assert( !strcmp( s.slot[9], "inventoryitems/weapons/weapon_glock.txt" ));

	/* the three-slot flag of the last message */
	n = Message( b, 1, "", 1, 0, 0 );
	assert( CoF_Inv12Parse( b, n, &m ) && CoF_Inv12Route( &s, &m ) == COF_INV12_FORWARD && s.three == 1 );

	/* ResetHUD: client.dll clears 1..6 itself, the engine 7..12 */
	CoF_Inv12ResetHigh( &s );
	assert( !s.slot[9][0] && !strcmp( s.slot[3], "inventoryitems/apartmentkey.txt" ));

	/* the accessor: 1..12 are the store, anything else a writable scratch buffer */
	assert( CoF_Inv12Slot( &s, 1 ) == s.slot[1] && CoF_Inv12Slot( &s, 12 ) == s.slot[12] );
	assert( CoF_Inv12Slot( &s, 13 ) == s.scratch && CoF_Inv12Slot( &s, 0 ) == s.scratch && CoF_Inv12Slot( &s, -5 ) == s.scratch );

	/* the pickup line, as client.dll writes it (ProFont: colour 5, text) */
	n = CoF_Inv12PickupLine( "Glock 19 9mm", line, sizeof( line ));
	assert( n == (int)strlen( "You got the Glock 19 9mm" ) + 2 && line[0] == 5 && !strcmp( (char *)line + 1, "You got the Glock 19 9mm" ));
	n = CoF_Inv12PickupLine( "Simon's Book", line, sizeof( line ));
	assert( n > 0 && !strcmp( (char *)line + 1, "You got Simon's Book" ));
	assert( !CoF_Inv12PickupLine( "", line, sizeof( line )));
	assert( !CoF_Inv12PickupLine( "Glock", line, 10 ));
	puts( "inventory12 client store passed" );
	return 0;
}
