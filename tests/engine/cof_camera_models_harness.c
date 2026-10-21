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
	left( "the player left (no object)", 3, 0, NULL, &model_a );
	left( "a player who left or is loading, whose object still holds it", 3, 1, &model_a, &model_a );
	left( "an owner object we cannot read", 3, -1, NULL, &model_a );
	left( "placed by the map (no owner)", 0, 0, NULL, &model_a );
	left( "owned by another entity", 9, 0, NULL, &model_a );
	/* 1.1.9 (ladders): a ladder's hands model goes to its owner only; the climb holds the owner while frozen with it */
	printf( "a ladder's hands, to its climber: %d\n", CoF_CameraModelHidden( 2, 4, 1, 2 ));
	printf( "a ladder's hands, to a partner: %d\n", CoF_CameraModelHidden( 2, 4, 1, 3 ));
	printf( "another camera model (the strangle, a cutscene), to a partner: %d\n", CoF_CameraModelHidden( 2, 4, 0, 3 ));
	printf( "a ladder model placed by the map: %d\n", CoF_CameraModelHidden( 0, 4, 1, 3 ));
	printf( "owned by another entity: %d\n", CoF_CameraModelHidden( 9, 4, 1, 3 ));
	printf( "climbing: %d\n", CoF_CameraModelClimbing( 1, 1, &model_a, &model_a ));
	printf( "the climb let go (unfrozen): %d\n", CoF_CameraModelClimbing( 1, 0, &model_a, &model_a ));
	printf( "a newer scene: %d\n", CoF_CameraModelClimbing( 1, 1, &model_b, &model_a ));
	printf( "not a ladder: %d\n", CoF_CameraModelClimbing( 0, 1, &model_a, &model_a ));
	return 0;
}
