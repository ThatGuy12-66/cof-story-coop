/* 1.3.4: sv_cof_chairs_state.h, everyone in a wheelchair at the end of Ending 4 (c_ending4b). */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "chairs_state_under_test.h"

static cof_chairs_person_t on_foot( void )
{
	cof_chairs_person_t p;
	memset( &p, 0, sizeof( p ));
	p.spawned = p.alive = 1;
	return p;
}

int main( void )
{
	cof_chairs_person_t p = on_foot(), q;
	double since = 0.0;
	int i;

	/* a person on foot is seated half a second after they are first free, while the ride is on */
	assert( !CoF_ChairsSeat( 1, &p, &since, 10.0, 0.0 ) && since == 10.0 );
	assert( !CoF_ChairsSeat( 1, &p, &since, 10.4, 0.0 ) && since == 10.0 );
	assert( CoF_ChairsSeat( 1, &p, &since, 10.5, 0.0 ));
	/* not before a failed seat's retry */
	assert( !CoF_ChairsSeat( 1, &p, &since, 11.0, 12.0 ) && CoF_ChairsSeat( 1, &p, &since, 12.0, 12.0 ));
	/* nobody rides, or the map ended the ride: nobody is seated, and the wait starts again */
	since = 10.0; assert( !CoF_ChairsSeat( 0, &p, &since, 20.0, 0.0 ) && since == 0.0 );
	/* anything that holds them waits it out, from the start */
	for( i = 0; i < 9; i++ )
	{
		q = on_foot();
		switch( i )
		{
		case 0: q.spawned = 0; break;  /* still joining */
		case 1: q.fake = 1; break;     /* an AI companion: never */
		case 2: q.alive = 0; break;    /* dead: after the respawn */
		case 3: q.riding = 1; break;   /* in a chair already */
		case 4: q.frozen = 1; break;   /* a scene, a door, the strangle */
		case 5: q.camera = 1; break;   /* watching a camera */
		case 6: q.busy = 1; break;     /* a boat, a call */
		case 7: q.moving = 1; break;   /* a level change or everyone's cutscene */
		default: break;
		}
		since = 5.0;
		if( i < 8 ) assert( !CoF_ChairsSeat( 1, &q, &since, 30.0, 0.0 ) && since == 0.0 );
		else assert( CoF_ChairsSeat( 1, &q, &since, 30.0, 0.0 ));
	}

	/* the map ended the ride: a chair that carried someone last frame, the same one, standing, alive and empty */
	assert( CoF_ChairsStopped( 1, 1, 0, 0, 0 ));
	assert( !CoF_ChairsStopped( 0, 1, 0, 0, 0 ));  /* nobody rode it */
	assert( !CoF_ChairsStopped( 1, 0, 0, 0, 0 ));  /* another entity in its slot now */
	assert( !CoF_ChairsStopped( 1, 1, 1, 0, 0 ));  /* removed: a leaver's (SV_CoF_MomentsDrop) */
	assert( !CoF_ChairsStopped( 1, 1, 0, 1, 0 ));  /* dead: the team wipe's (sv_cof_rescue.c) */
	assert( !CoF_ChairsStopped( 1, 1, 0, 0, 1 ));  /* still carrying */

	/* the rider whose chair is nearest; one not usable is passed over; none: -1 */
	{
		const float chairs[3][3] = { { 0, 0, 0 }, { 100, 0, 0 }, { 30, 40, 0 } };
		const float simon[3] = { 90, 10, 0 }, far_side[3] = { -10, 0, 0 };
		int usable[3] = { 1, 1, 1 };
		assert( CoF_ChairsNearest( chairs, usable, 3, simon ) == 1 );
		assert( CoF_ChairsNearest( chairs, usable, 3, far_side ) == 0 );
		usable[1] = 0; assert( CoF_ChairsNearest( chairs, usable, 3, simon ) == 2 );
		usable[0] = usable[2] = 0; assert( CoF_ChairsNearest( chairs, usable, 3, simon ) == -1 );
		assert( CoF_ChairsNearest( chairs, usable, 0, simon ) == -1 );
	}
	puts( "wheelchair scenarios passed" );
	return 0;
}
