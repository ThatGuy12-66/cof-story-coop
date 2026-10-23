/* CL_CoF_AxisPushed (cof_gamepad.c, 1.2.8): a stick or trigger takes the pointer for the gamepad when pushed from
   rest, once; a drifting or off-centre axis does not keep taking it back from the mouse. */
#include <stdio.h>
#include <stdlib.h>

typedef int qboolean;
#define true 1
#define false 0
#define ARRAYSIZE( x ) ( sizeof( x ) / sizeof(( x )[0] ))

static struct
{
	qboolean axis_held[2][8];
} cof_pad;

#include "axis_under_test.c"

#define CHECK( cond, what ) do { if( !( cond )) { printf( "FAIL: %s\n", what ); return 1; } } while( 0 )

int main( void )
{
	int i, counts;

	CHECK( !CL_CoF_AxisPushed( 0, 0, 3000 ), "an axis at rest is no push" );
	CHECK( CL_CoF_AxisPushed( 0, 0, 20000 ), "a push from rest counts" );
	CHECK( !CL_CoF_AxisPushed( 0, 0, 25000 ), "holding it does not count again" );
	CHECK( !CL_CoF_AxisPushed( 0, 0, 10000 ), "easing off, not at rest yet" );
	CHECK( !CL_CoF_AxisPushed( 0, 0, 20000 ), "pushed again without coming back to rest" );
	CHECK( !CL_CoF_AxisPushed( 0, 0, 7000 ), "back at rest" );
	CHECK( CL_CoF_AxisPushed( 0, 0, -20000 ), "the other way after rest counts" );

	// a drifting stick hovering around the threshold sends events all the time: one count
	for( i = counts = 0; i < 300; i++ )
		counts += CL_CoF_AxisPushed( 0, 1, 11700 + ( i % 3 ) * 600 );
	CHECK( counts == 1, "a drifting stick takes the pointer once" );

	// an axis resting off centre (a misread trigger at -32768) with noise: one count
	for( i = counts = 0; i < 300; i++ )
		counts += CL_CoF_AxisPushed( 0, 5, -32768 + ( i % 7 ) * 40 );
	CHECK( counts == 1, "an off-centre axis takes the pointer once" );

	// the SDL controller axes and the engine axes are kept apart
	CHECK( CL_CoF_AxisPushed( 1, 1, 30000 ), "the engine axis has its own state" );
	CHECK( CL_CoF_AxisPushed( 1, 0, -30000 ), "every engine axis has its own state" );

	// outside the table: the plain threshold, as before
	CHECK( CL_CoF_AxisPushed( 0, 99, 13000 ) && CL_CoF_AxisPushed( 0, 99, 13000 ), "an unknown axis uses the threshold" );
	CHECK( !CL_CoF_AxisPushed( 2, 0, 11000 ) && CL_CoF_AxisPushed( 2, 0, 13000 ), "an unknown source uses the threshold" );
	CHECK( !CL_CoF_AxisPushed( 0, -1, 12000 ), "the threshold itself is no push" );

	printf( "axis push scenarios passed\n" );
	return 0;
}
