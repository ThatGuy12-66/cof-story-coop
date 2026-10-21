/* Remote players' position history (engine/client/cl_frame.c: CL_UpdatePositions, CL_ResetPositions,
 * CL_FindInterpolationUpdates, CL_PureOrigin) against plain doubles: no renderer, network or game. */
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef int qboolean;
typedef unsigned int uint;
typedef float vec3_t[3];
typedef float vec4_t[4];
#define true 1
#define false 0
#define HISTORY_MAX 64
#define HISTORY_MASK ( HISTORY_MAX - 1 )
#define EQUAL_EPSILON 0.001f
#define Q_equal( a, b ) ((( a ) >= (( b ) - EQUAL_EPSILON )) && (( a ) <= (( b ) + EQUAL_EPSILON )))
#define Q_max( a, b ) ((( a ) > ( b )) ? ( a ) : ( b ))
#define bound( min, num, max ) (( num ) >= ( min ) ? (( num ) < ( max ) ? ( num ) : ( max )) : ( min ))
#define VectorCopy( a, b ) (( b )[0] = ( a )[0], ( b )[1] = ( a )[1], ( b )[2] = ( a )[2] )
#define VectorSubtract( a, b, c ) (( c )[0] = ( a )[0] - ( b )[0], ( c )[1] = ( a )[1] - ( b )[1], ( c )[2] = ( a )[2] - ( b )[2] )
#define VectorMA( a, s, b, c ) (( c )[0] = (float)(( a )[0] + ( s ) * ( b )[0] ), ( c )[1] = (float)(( a )[1] + ( s ) * ( b )[1] ), \
	( c )[2] = (float)(( a )[2] + ( s ) * ( b )[2] ))

typedef struct { float animtime; vec3_t origin; vec3_t angles; } position_history_t;
typedef struct { vec3_t origin, angles; float animtime, msg_time; } entity_state_t;
typedef struct { entity_state_t prevstate, curstate; int current_position; position_history_t ph[HISTORY_MAX]; } cl_entity_t;

/* Angles stand in for the quaternion: the slerp is a blend, enough to see which samples were used. */
static void AngleQuaternion( const vec3_t angles, vec4_t q, qboolean studio )
{
	(void)studio;
	q[0] = angles[0]; q[1] = angles[1]; q[2] = angles[2]; q[3] = 0.0f;
}
static void QuaternionSlerp( const vec4_t p, const vec4_t q, double t, vec4_t qt )
{
	int i;
	for( i = 0; i < 4; i++ ) qt[i] = (float)( p[i] + ( q[i] - p[i] ) * t );
}
static void QuaternionAngle( const vec4_t q, vec3_t angles ) { angles[0] = q[0]; angles[1] = q[1]; angles[2] = q[2]; }

#include "interp_under_test.c"

#define CHECK( expr ) do { if( !( expr )) { printf( "FAIL line %d: %s\n", __LINE__, #expr ); return 1; } } while( 0 )
#define SPEED 200.0 /* units a second along x */
#define INTERP 0.1  /* ex_interp */
#define STEP ( 1.0 / 60.0 ) /* a sample a server frame */

static cl_entity_t partner;

/* A packet: the partner at x = SPEED * at, stamped at. */
static void sample( double at )
{
	partner.prevstate = partner.curstate;
	partner.curstate.origin[0] = (float)( SPEED * at );
	partner.curstate.angles[1] = (float)( at * 10.0 );
	partner.curstate.animtime = (float)at;
	partner.curstate.msg_time = (float)at;
	CL_UpdatePositions( &partner );
}

static float drawn( double now )
{
	vec3_t origin, angles;
	CL_PureOrigin( &partner, now - INTERP, origin, angles );
	return origin[0];
}

int main( void )
{
	position_history_t *ph0, *ph1;
	double t, next, now;
	float last, x;
	int held = 0;

	/* A steady stream: drawn exactly where the partner was ex_interp ago, between the two samples around it. */
	memset( &partner, 0, sizeof( partner ));
	for( t = 1.0; t < 2.0; t += STEP ) sample( t );
	x = drawn( 1.905 );
	CHECK( fabs( x - SPEED * ( 1.905 - INTERP )) < 0.05 );
	CHECK( !CL_FindInterpolationUpdates( &partner, 1.905 - INTERP, &ph0, &ph1 ) && ph1->animtime < 1.805f && ph0->animtime > 1.805f );

	/* The partner comes into view (or is moved): the history is reset to the newest sample and new ones follow.
	 * The time drawn is older than all of them for ex_interp: the partner holds the oldest, then moves on from
	 * it. It never goes back (it was drawn at the newest but one, then jumped back about ex_interp). */
	CL_ResetPositions( &partner );
	CHECK( partner.current_position == 1 && partner.ph[0].animtime == partner.ph[1].animtime && partner.ph[2].animtime == 0.0f );
	last = -1.0f;
	next = t;
	for( now = t; now < t + 0.5; now += 1.0 / 144.0 )
	{
		while( next <= now ) { sample( next ); next += STEP; }
		x = drawn( now );
		CHECK( x >= last - 0.001f );
		if( x == partner.ph[0].origin[0] ) held++;
		last = x;
		if( now - INTERP > partner.ph[0].animtime + STEP )
			CHECK( fabs( x - SPEED * ( now - INTERP )) < 0.05 ); /* caught up: on the path again */
	}
	CHECK( held > 5 );
	/* while held, the pair is the oldest sample twice: no extrapolation from the newest two */
	CL_ResetPositions( &partner );
	sample( next ); sample( next + STEP );
	CHECK( CL_FindInterpolationUpdates( &partner, next - 1.0, &ph0, &ph1 ));
	CHECK( ph0 == ph1 && ph0 == &partner.ph[0] ); /* the reset left the newest sample in slots 0 and 1 */

	/* Nothing older than the newest sample to look at (its first packet): as before, the newest and the slot behind it. */
	memset( &partner, 0, sizeof( partner ));
	sample( 5.0 );
	CHECK( CL_FindInterpolationUpdates( &partner, 4.0, &ph0, &ph1 ));
	CHECK( ph0 == &partner.ph[1] && ph1 == &partner.ph[0] );
	puts( "interpolation history passed" );
	return 0;
}
