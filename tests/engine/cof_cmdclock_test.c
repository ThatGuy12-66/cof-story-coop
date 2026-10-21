/* The command clock (sv_cof_cmdclock.h): when a person's position belongs, for the clients' interpolation. */
#include <stdio.h>
#include <math.h>
#include "sv_cof_cmdclock.h"

#define CHECK( expr ) do { if( !( expr )) { printf( "FAIL line %d: %s\n", __LINE__, #expr ); return 1; } } while( 0 )
#define FRAME ( 1.0 / 60.0 )
#define COMMANDS 2400 /* 40 s */

static unsigned int seed = 12345u;
static double jitter( double amount ) /* -amount..amount, the same every run */
{
	seed = seed * 1103515245u + 12345u;
	return ((double)(( seed >> 8 ) & 0xFFFF ) / 65535.0 * 2.0 - 1.0 ) * amount;
}

/* A person at 60 fps (commands of 16 or 17 ms, as the client carries the leftover over) sent over a link with this much
 * jitter to a 60 fps host; drift: how much slower their clock runs. Returns how much, in seconds, the stamps (clock, or
 * the old one: the frame's end) wander against when the person really was where the position says (the same offset
 * all the time is smooth). */
static double run( double spread, double drift, int old_stamps, double *worst_lag )
{
	double sent[COMMANDS], length[COMMANDS], arrive[COMMANDS], stamp = 0.0, last = 0.0, worst = 0.0, carry = 0.0;
	double first = 0.0;
	int k, next = 0, made = 0, frame, have_first = 0;
	*worst_lag = 0.0;
	for( k = 0; k < COMMANDS; k++ )
	{
		int ms;
		carry += FRAME * 1000.0 * ( 1.0 - drift );
		ms = (int)carry;
		carry -= ms;
		length[k] = ms / 1000.0;
		sent[k] = k * FRAME;
		arrive[k] = sent[k] + 0.04 + jitter( spread );
		if( k && arrive[k] < arrive[k - 1] ) arrive[k] = arrive[k - 1]; /* one link: in order */
	}
	for( frame = 0; frame < COMMANDS - 6; frame++ )
	{
		double start = 0.05 + frame * FRAME, end = start + FRAME;
		int ran = 0;
		while( next < COMMANDS && arrive[next] <= start )
		{
			stamp = CoF_CmdClockAdvance( stamp, length[next], end );
			next++;
			ran = 1;
		}
		if( !ran ) continue;
		made++;
		if( stamp < last || stamp > end + 1e-9 || stamp < end - COF_CMDCLOCK_WINDOW - 1e-9 ) return 1.0; /* never */
		last = stamp;
		if( end - stamp > *worst_lag ) *worst_lag = end - stamp;
		if( frame < 60 ) continue; /* settled */
		/* the stamp against when the person was there (the last command's own time): the same offset every time is smooth */
		{
			double offset = ( old_stamps ? end : stamp ) - sent[next - 1];
			if( !have_first ) { first = offset; have_first = 1; }
			if( fabs( offset - first ) > worst ) worst = fabs( offset - first );
		}
	}
	return made > COMMANDS / 2 ? worst : 1.0;
}

int main( void )
{
	double lag, old, now;

	/* the first command, and a command that moves nobody (frozen, paused): the frame's end, as the game's stamp */
	CHECK( CoF_CmdClockAdvance( 0.0, 0.016, 10.0 ) == 10.0 );
	CHECK( CoF_CmdClockAdvance( 9.99, 0.0, 10.0 ) == 10.0 );
	/* ahead of a frame's end only when a frame came out shorter than the last (paused): held, never back */
	CHECK( CoF_CmdClockAdvance( 10.002, 0.016, 10.0 ) == 10.002 );
	/* a new map: its time starts again */
	CHECK( CoF_CmdClockAdvance( 500.0, 0.016, 1.016 ) == 1.016 );
	/* never past the frame's end, never more than the window behind it */
	CHECK( CoF_CmdClockAdvance( 9.999, 0.016, 10.0 ) == 10.0 );
	CHECK( CoF_CmdClockAdvance( 5.0, 0.016, 10.0 ) == 10.0 - COF_CMDCLOCK_WINDOW );
	/* in between: the command's length, and a part of the gap left */
	now = CoF_CmdClockAdvance( 9.95, 0.016, 10.0 );
	CHECK( fabs( now - ( 9.966 + 0.034 * COF_CMDCLOCK_PULL )) < 1e-9 );

	/* Over a link with 12 ms of jitter the old stamps (the frame each packet arrived in) were off by up to a frame
	 * or more; the clock's stay within a few milliseconds of the person's own time, and well inside ex_interp. */
	old = run( 0.012, 0.0, 1, &lag );
	now = run( 0.012, 0.0, 0, &lag );
	printf( "jitter 12 ms: stamps off by %.1f ms before, %.1f ms now, %.1f ms behind at most\n", old * 1000.0, now * 1000.0, lag * 1000.0 );
	CHECK( old > 0.012 && now < 0.004 && now < old / 4.0 && lag < 0.04 );
	/* a steady link: as smooth as before, and right at the frame's end */
	now = run( 0.0, 0.0, 0, &lag );
	CHECK( now < 0.001 && lag < 0.001 );
	/* a person whose clock runs 0.1% slow does not fall behind over 40 s (34 ms without the pull) */
	now = run( 0.006, 0.001, 0, &lag );
	printf( "clock 0.1%% slow: %.1f ms behind at most\n", lag * 1000.0 );
	CHECK( lag < 0.02 && now < 0.01 );
	/* one running fast is held at the frame's end (run checks it every frame) */
	CHECK( run( 0.006, -0.001, 0, &lag ) < 1.0 );
	/* lost commands left it the window behind: it closes the gap in seconds, while staying inside ex_interp */
	{
		double at = 10.0 - COF_CMDCLOCK_WINDOW, end = 10.0;
		int k;
		for( k = 0; k < 300; k++ )
		{
			end += FRAME;
			at = CoF_CmdClockAdvance( at, FRAME, end );
		}
		CHECK( end - at < COF_CMDCLOCK_WINDOW / 2.0 && end - at > 0.0 );
	}
	puts( "command clock passed" );
	return 0;
}
