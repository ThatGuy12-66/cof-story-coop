/* Offline checks of the black-room rules (sv_cof_doorroom.h, 1.2.3) on the rooms of the 2026-10-21 host log. */
#include <assert.h>
#include <stdio.h>
#include "doorroom_under_test.h"

/* a brush entity's absbox as the engine links it: the model's bounds padded by a unit */
static void pad( const float *mins, const float *maxs, float *absmin, float *absmax )
{
	int k;
	for( k = 0; k < 3; k++ ) { absmin[k] = mins[k] - 1.0f; absmax[k] = maxs[k] + 1.0f; }
}

int main( void )
{
	const float hull_min[3] = { -16, -16, -36 }, hull_max[3] = { 16, 16, 36 };
	/* c_buscity (cm_buscity's first section, not moved): landmark landbc, the walk-in *41 (teleportbc2), the exit
	 * back to c_city *40 */
	const float landbc[3] = { -507, -1184, -54 };
	const float walkin_min[3] = { -578, -1219, -121 }, walkin_max[3] = { -520, -1147, 0 };
	const float back_min[3] = { -494, -1219, -121 }, back_max[3] = { -437, -1147, 0 };
	/* c_city's own room is taller: its walk-in *43 lies under the landmark, 93 units down */
	const float city_min[3] = { -495, -1219, -272 }, city_max[3] = { -437, -1147, -146 };
	float wmin[3], wmax[3], bmin[3], bmax[3], cmin[3], cmax[3], spot[3], at[3];

	pad( walkin_min, walkin_max, wmin, wmax );
	pad( back_min, back_max, bmin, bmax );
	pad( city_min, city_max, cmin, cmax );

	/* the room's walk-in is beside the landmark; c_city's is no black-room walk-in of its landmark */
	assert( CoF_DoorRoomWalkInNear( landbc, wmin, wmax ));
	assert( CoF_DoorRoomBoxDistance2( landbc, wmin, wmax ) == 144.0f ); /* 12 units */
	assert( !CoF_DoorRoomWalkInNear( landbc, cmin, cmax ));
	assert( CoF_DoorRoomBoxDistance2( landbc, cmin, cmax ) > 80.0f * 80.0f );

	/* where the log's arrivals stood: the leader on the landmark (arrival-landmark -507,-1184,-92.969) and the partner
	 * beside him (arrival-placed -472,-1184,-92.969, in the exit back): neither well inside the walk-in */
	at[0] = -507; at[1] = -1184; at[2] = -92.969f;
	assert( !CoF_DoorRoomInside( at, hull_min, hull_max, wmin, wmax ));
	at[0] = -472;
	assert( !CoF_DoorRoomInside( at, hull_min, hull_max, wmin, wmax ) && CoF_DoorRoomInside( at, hull_min, hull_max, bmin, bmax ));
	/* the retail arrival from c_city's door (d1_1_exit1 at x -546) stands in it */
	at[0] = -546; at[1] = -1185;
	assert( CoF_DoorRoomInside( at, hull_min, hull_max, wmin, wmax ));

	/* the spots: its middle, then the landmark brought in by half a hull and a unit */
	assert( CoF_DoorRoomSpot( landbc, wmin, wmax, hull_min, hull_max, 0, spot ));
	assert( spot[0] == -549.0f && spot[1] == -1183.0f && spot[2] == -54.0f );
	assert( CoF_DoorRoomSpot( landbc, wmin, wmax, hull_min, hull_max, 1, spot ));
	assert( spot[0] == -537.0f && spot[1] == -1184.0f && spot[2] == -54.0f );
	at[0] = spot[0]; at[1] = spot[1]; at[2] = -92.969f;
	assert( CoF_DoorRoomInside( at, hull_min, hull_max, wmin, wmax ) && !CoF_DoorRoomInside( at, hull_min, hull_max, bmin, bmax ));
	assert( !CoF_DoorRoomSpot( landbc, wmin, wmax, hull_min, hull_max, 2, spot ));
	/* a trigger narrower than a hull: its middle either way */
	{
		const float narrow_min[3] = { -530, -1200, -121 }, narrow_max[3] = { -520, -1170, 0 };
		float nmin[3], nmax[3];
		pad( narrow_min, narrow_max, nmin, nmax );
		assert( CoF_DoorRoomSpot( landbc, nmin, nmax, hull_min, hull_max, 1, spot ) && spot[0] == -525.0f && spot[1] == -1185.0f );
	}
	/* a box too thin to stand in has no spot */
	{
		const float thin_min[3] = { -530, -1200, -121 }, thin_max[3] = { -531, -1170, 0 };
		assert( !CoF_DoorRoomSpot( landbc, thin_min, thin_max, hull_min, hull_max, 0, spot ));
	}

	/* teleports stay home when their destination is there */
	assert( CoF_DoorRoomTeleportStays( 1, 1, 1 ));
	assert( !CoF_DoorRoomTeleportStays( 1, 1, 0 )); /* destination elsewhere: crosses, as in single player */
	assert( !CoF_DoorRoomTeleportStays( 1, 0, 0 ) && !CoF_DoorRoomTeleportStays( 0, 1, 1 ));
	puts( "doorroom scenarios passed" );
	return 0;
}
