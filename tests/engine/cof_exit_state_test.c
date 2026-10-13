/* The exit hold's rules, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_exit_state.h"

static cof_exit_player_t p[COF_EXIT_PLAYERS];
static cof_exit_state_t s;
static const float spot[3] = { 0, 0, 0 };

static void one( int i, int userid, float x, int touching )
{
	memset( &p[i], 0, sizeof( p[i] ));
	p[i].userid = userid; p[i].spawned = p[i].alive = 1; p[i].origin[0] = x; p[i].touching = touching;
}
/* host (userid 1) at x0, guest (userid 2) at x1 */
static void two( float x0, int touch0, float x1, int touch1 )
{
	memset( p, 0, sizeof( p )); memset( &s, 0, sizeof( s ));
	one( 0, 1, x0, touch0 ); one( 1, 2, x1, touch1 );
}
static int request( double t, int *began ) { return CoF_ExitRequest( &s, p, 2, 5, "c_start2", "hahaland", spot, t, began ); }
/* the exit's trigger can ask for it again */
static int frame( double t ) { return CoF_ExitFrame( &s, p, 2, 5, t, 1 ); }
/* nothing can: a once-only trigger fired a use-only exit and removed itself */
static int frame_once_only( double t ) { return CoF_ExitFrame( &s, p, 2, 5, t, 0 ); }

int main( void )
{
	int began, left;

	/* alone: an exit goes at once */
	two( 0, 1, 0, 0 ); p[1].userid = 0;
	assert( request( 100, &began ) == COF_EXIT_GO && !s.active && !began );
	/* both in the exit, or the partner within 128 units of the one in it */
	two( 0, 1, 10, 1 ); assert( request( 100, &began ) == COF_EXIT_GO );
	two( 0, 1, 128, 0 ); assert( request( 100, &began ) == COF_EXIT_GO );
	/* both in the exit, however far apart (a long trigger) */
	two( 0, 1, 3000, 1 ); assert( request( 100, &began ) == COF_EXIT_GO && !s.active );

	/* the partner elsewhere: held, with the player in the exit as the waiter */
	two( 0, 1, 129, 0 );
	assert( request( 100, &began ) == COF_EXIT_HOLD && began && s.active && s.walkin && s.waiter == 1 );
	assert( CoF_ExitMissing( p, 2, 1 ) == 2 );
	/* asked again every frame: the same hold, the same clock */
	assert( request( 100.1, &began ) == COF_EXIT_HOLD && !began && s.began == 100 );
	assert( frame( 100.2 ) == COF_EXIT_WAIT );
	/* Review Focus 1: no further request; the partner warps over and the frame lets it go */
	p[1].origin[0] = 35;
	assert( frame( 101 ) == COF_EXIT_READY && !strcmp( s.level, "c_start2" ) && !strcmp( s.landmark, "hahaland" ));

	/* stepping out of the exit ends the hold */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].touching = 0; p[0].origin[0] = 200;
	assert( frame( 100.1 ) == COF_EXIT_CANCELLED && !s.active );
	/* Review Finding 1: a once-only trigger vanishes after firing; the waiter has not moved */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].touching = 0; p[0].origin[0] = 100;
	assert( frame( 100.1 ) == COF_EXIT_WAIT && s.active );
	p[1].origin[0] = 35; assert( frame( 100.2 ) == COF_EXIT_READY );

	/* Final review F1: nothing can ask for this exit again, so a walk-in hold is
	   never cancelled; it becomes a scripted hold. The waiter walks away: */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].touching = 0; p[0].origin[0] = 500;
	assert( frame_once_only( 100.1 ) == COF_EXIT_WAIT && s.active && !s.walkin && s.waiter == 1 );
	p[1].origin[0] = 535; assert( frame_once_only( 100.2 ) == COF_EXIT_READY );
	/* ...the waiter dies: with three players the one nearest the landmark waits */
	two( 0, 1, 3000, 0 ); one( 2, 3, 2000, 0 );
	assert( CoF_ExitRequest( &s, p, 3, 5, "c_start2", "hahaland", spot, 100, &began ) == COF_EXIT_HOLD && s.waiter == 1 && s.walkin );
	p[0].alive = p[0].touching = 0;
	assert( CoF_ExitFrame( &s, p, 3, 5, 100.1, 0 ) == COF_EXIT_WAIT && s.active && !s.walkin && s.waiter == 3 );
	/* ...with two players the partner waits, and goes once the dead one is back beside them */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].alive = p[0].touching = 0;
	assert( frame_once_only( 100.1 ) == COF_EXIT_WAIT && s.active && !s.walkin && s.waiter == 2 );
	p[0].alive = 1; p[0].origin[0] = -2000; assert( frame_once_only( 105 ) == COF_EXIT_WAIT );
	p[0].origin[0] = 2950; assert( frame_once_only( 106 ) == COF_EXIT_READY );
	/* ...the waiter disconnects: the one left goes */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].userid = 0;
	assert( frame_once_only( 100.1 ) == COF_EXIT_READY );

	/* a scripted exit (nobody in a trigger): the player nearest its landmark waits,
	   and walking about does not cancel it */
	two( 40, 0, 3000, 0 );
	assert( request( 100, &began ) == COF_EXIT_HOLD && !s.walkin && s.waiter == 1 );
	p[0].origin[0] = 300; assert( frame( 100.1 ) == COF_EXIT_WAIT && s.active );
	p[1].origin[0] = 335; assert( frame( 100.2 ) == COF_EXIT_READY );
	two( 3000, 0, 40, 0 ); request( 100, &began ); assert( s.waiter == 2 );

	/* a dead partner holds the exit, even lying in it */
	two( 0, 1, 10, 1 ); p[1].alive = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && frame( 100.1 ) == COF_EXIT_WAIT );
	p[1].alive = 1; assert( frame( 100.2 ) == COF_EXIT_READY );
	/* a partner who is still connecting holds it until spawned or dropped */
	two( 0, 1, 0, 0 ); p[1].spawned = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && frame( 100.1 ) == COF_EXIT_WAIT );
	/* Review Focus 2: the partner disconnects; the waiter in the exit goes */
	p[1].userid = 0; assert( frame( 100.2 ) == COF_EXIT_READY );
	/* ...but not a waiter who has stepped out meanwhile */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[1].userid = 0; p[0].touching = 0; p[0].origin[0] = 500;
	assert( frame( 100.1 ) == COF_EXIT_CANCELLED );
	/* Review Focus 2: a scripted exit whose waiter leaves still happens for the one left */
	two( 40, 0, 3000, 0 ); request( 100, &began ); p[0].userid = 0;
	assert( frame( 100.1 ) == COF_EXIT_READY );
	/* with three players someone else becomes the waiter */
	two( 40, 0, 3000, 0 ); one( 2, 3, 2000, 0 );
	assert( CoF_ExitRequest( &s, p, 3, 5, "c_start2", "hahaland", spot, 100, &began ) == COF_EXIT_HOLD && s.waiter == 1 );
	p[0].userid = 0;
	assert( CoF_ExitFrame( &s, p, 3, 5, 100.1, 1 ) == COF_EXIT_WAIT && s.waiter == 3 );
	/* everyone dead (live 2026-10-12: a corpse in the exit took two dead players to
	   c_nightmare): held with nobody waiting and no prompts; asked again, still held */
	two( 0, 1, 10, 1 ); p[0].alive = p[1].alive = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && began && s.active && !s.waiter && !s.walkin );
	assert( request( 100.1, &began ) == COF_EXIT_HOLD && !began && s.active && !s.waiter );
	assert( frame( 100.2 ) == COF_EXIT_WAIT && !CoF_ExitPromptDue( &s, 100.2 ));
	/* ...the first one back waits for the other, who goes once beside them */
	p[0].alive = 1; p[0].touching = 0; p[0].origin[0] = 3000;
	assert( frame( 110 ) == COF_EXIT_WAIT && s.waiter == 1 );
	p[1].alive = 1; p[1].touching = 0; p[1].origin[0] = 3030;
	assert( frame( 111 ) == COF_EXIT_READY );
	/* playing alone (a companion bot does not count) and dead in the exit: held too,
	   until the respawn */
	two( 0, 1, 0, 0 ); p[1].userid = 0; p[0].alive = 0;
	assert( request( 100, &began ) == COF_EXIT_HOLD && frame( 100.1 ) == COF_EXIT_WAIT );
	p[0].alive = 1; assert( frame( 112 ) == COF_EXIT_READY );
	/* the waiter dies as the partner leaves: asked again, still held, never the dead one going */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].alive = 0; p[1].userid = 0;
	assert( request( 100.1, &began ) == COF_EXIT_HOLD && s.active && !s.waiter );
	/* nobody who counts at all: nothing to hold for */
	two( 0, 1, 0, 0 ); p[0].userid = p[1].userid = 0;
	assert( request( 100, &began ) == COF_EXIT_GO && !s.active );

	/* the escape hatch: only the waiter, only after 10 s */
	two( 0, 1, 3000, 0 ); request( 100, &began );
	assert( CoF_ExitEscape( &s, 2, 150, &left ) == 0 );
	assert( CoF_ExitEscape( &s, 1, 103.2, &left ) == -1 && left == 7 );
	/* Review Focus 3: pressing early changes nothing */
	assert( s.began == 100 && CoF_ExitEscape( &s, 1, 105, &left ) == -1 && left == 5 );
	assert( CoF_ExitEscape( &s, 1, 110, &left ) == 1 );
	CoF_ExitClear( &s ); assert( CoF_ExitEscape( &s, 1, 200, &left ) == 0 );

	/* a newer request for another exit replaces the held one, once its waiter
	   no longer stands in it (Final review F3, below) */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[0].touching = 0;
	assert( CoF_ExitRequest( &s, p, 2, 5, "c_apartment1", "land1", spot, 104, &began ) == COF_EXIT_HOLD && began );
	assert( !strcmp( s.level, "c_apartment1" ) && s.began == 104 );
	/* the map changed another way: the hold is dropped */
	assert( CoF_ExitFrame( &s, p, 2, 6, 105, 1 ) == COF_EXIT_IDLE && !s.active );
	/* Final review F3: first come, first served. The waiter still stands in exit A,
	   so the partner standing in exit B is held and the state is left alone */
	two( 0, 1, 3000, 0 ); request( 100, &began ); p[1].touching = 1;
	assert( CoF_ExitRequest( &s, p, 2, 5, "c_apartment1", "land1", spot, 101, &began ) == COF_EXIT_HOLD && !began );
	assert( !strcmp( s.level, "c_start2" ) && s.began == 100 && s.waiter == 1 );
	/* the waiter walks away from A: that hold ends, and B's next request holds with its own waiter */
	p[0].touching = 0; p[0].origin[0] = 500;
	assert( frame( 102 ) == COF_EXIT_CANCELLED && !s.active );
	assert( CoF_ExitRequest( &s, p, 2, 5, "c_apartment1", "land1", spot, 103, &began ) == COF_EXIT_HOLD && began );
	assert( !strcmp( s.level, "c_apartment1" ) && s.waiter == 2 && s.walkin && s.began == 103 );

	/* prompts: at once, then every 2 s */
	two( 0, 1, 3000, 0 ); request( 100, &began );
	assert( CoF_ExitPromptDue( &s, 100 ) && !CoF_ExitPromptDue( &s, 101 ) && CoF_ExitPromptDue( &s, 102 ));
	CoF_ExitClear( &s ); assert( !CoF_ExitPromptDue( &s, 200 ));

	/* a name that cannot be stored is never held */
	two( 0, 1, 3000, 0 );
	assert( CoF_ExitRequest( &s, p, 2, 5, "0123456789012345678901234567890123456789012345678901234567890123", "x", spot, 100, &began ) == COF_EXIT_GO && !s.active );
	puts( "exit state scenarios passed" );
	return 0;
}
