/* gl_cof_depthrange.h: the far end of each depth range the engine sets, after Cry of Fear's opengl32.dll rule. */
#include <stdio.h>
#include "depthrange_under_test.h"

static void show( const char *label, double znear, double zfar )
{
	printf( "%s %g..%g: %g\n", label, znear, zfar, CoF_DepthRangeFar( znear, zfar ));
}

int main( void )
{
	show( "world", 0.0, 1.0 );           // R_Clear: the world goes to 0..0.8
	show( "half", 0.0, 0.5 );            // the dll's other case
	show( "view model", 0.0, 0.3 );      // R_DrawViewModel's own range stands
	show( "overview", 1.0, 0.0 );        // the reversed overview range stands
	show( "already", 0.0, 0.8 );         // through the dll a second time: unchanged
	show( "sky", 0.9, 1.0 );             // client.dll's own ranges stand
	return 0;
}
