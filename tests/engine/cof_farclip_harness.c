/* gl_cof_farclip.h: the world view's far distance from zmax. */
#include <stdio.h>
#include "farclip_under_test.h"

int main( void )
{
	printf( "1500 x 1: %g\n", CoF_FarClip( 1500.0f, 1.0f ));     // retail: exactly MaxRange
	printf( "1500 x 1.73: %g\n", CoF_FarClip( 1500.0f, 1.73f )); // Xash's own
	printf( "4096 x 0: %g\n", CoF_FarClip( 4096.0f, 0.0f ));     // nonsense scales fall back to retail
	printf( "4096 x -2: %g\n", CoF_FarClip( 4096.0f, -2.0f ));
	return 0;
}
