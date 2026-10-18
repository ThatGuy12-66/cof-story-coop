/* sv_cof_camera_models.h: the camera models a finished model cutscene leaves behind. */
#include <stdio.h>
#include "camera_models_under_test.h"

static int model_a, model_b;

static void left( const char *what, int owner_slot, int present, const void *scene, const void *model )
{
	printf( "%s: %d\n", what, CoF_CameraModelLeft( owner_slot, 4, present, scene, model ));
}

int main( void )
{
	left( "the player's scene", 1, 1, &model_a, &model_a );
	left( "the scene ended (pDeathScene cleared)", 1, 1, NULL, &model_a );
	left( "the player has a newer scene", 2, 1, &model_b, &model_a );
	left( "the player left", 3, 0, NULL, &model_a );
	left( "placed by the map (no owner)", 0, 0, NULL, &model_a );
	left( "owned by another entity", 9, 0, NULL, &model_a );
	return 0;
}
