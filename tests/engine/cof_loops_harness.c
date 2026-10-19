/* sv_cof_loops.h: a merged map's ambient loops, and what a player moving between its sections needs of each. */
#include <stdio.h>
#include <string.h>
#include "loops_under_test.h"

static cof_loops_t loops;

static cof_loop_t loop( int entnum, int section, const char *sample, int vol )
{
	cof_loop_t l;
	memset( &l, 0, sizeof( l ));
	l.entnum = entnum; l.serial = 7; l.section = section; l.vol = vol; l.pitch = 100; l.attn = 1.25f;
	strncpy( l.sample, sample, sizeof( l.sample ) - 1 );
	return l;
}

static void show( const char *what )
{
	int i;
	printf( "%s:", what );
	for( i = 0; i < loops.count; i++ )
		printf( " [%d %s s%d v%d p%d]", loops.loop[i].entnum, loops.loop[i].sample, loops.loop[i].section, loops.loop[i].vol, loops.loop[i].pitch );
	printf( "\n" );
}

int main( void )
{
	cof_loop_t l = loop( 40, 0, "ambience/rain45.wav", 255 ), dead;
	char longest[COF_LOOP_SAMPLE + 8];
	int i;

	printf( "start: %d\n", CoF_LoopStart( &loops, &l ));
	l = loop( 41, 1, "boss/sawer/chainsaw_loop.wav", 200 ); CoF_LoopStart( &loops, &l );
	l = loop( 40, 0, "ambience/rain45.wav", 100 ); CoF_LoopStart( &loops, &l ); /* the same entity's same sound again */
	l = loop( 40, 0, "ambience/wind.wav", 90 ); CoF_LoopStart( &loops, &l );    /* another sound of it */
	show( "started" );
	printf( "find 41: %d, find 41 other: %d\n", CoF_LoopFind( &loops, 41, "boss/sawer/chainsaw_loop.wav" ), CoF_LoopFind( &loops, 41, "ambience/wind.wav" ));
	printf( "change vol: %d\n", CoF_LoopChange( &loops, 41, "boss/sawer/chainsaw_loop.wav", 50, -1 ));
	printf( "change pitch: %d\n", CoF_LoopChange( &loops, 40, "ambience/wind.wav", -1, 120 ));
	printf( "change unknown: %d\n", CoF_LoopChange( &loops, 99, "ambience/wind.wav", 10, 10 ));
	show( "changed" );
	CoF_LoopStop( &loops, 40, "ambience/rain45.wav" );
	CoF_LoopStop( &loops, 99, "ambience/rain45.wav" ); /* not recorded: nothing */
	show( "stopped" );

	memset( longest, 'a', sizeof( longest ) - 1 ); longest[sizeof( longest ) - 1] = 0;
	l = loop( 42, 0, "", 255 ); memcpy( l.sample, longest, sizeof( l.sample )); /* not terminated */
	printf( "too long: %d\n", CoF_LoopStart( &loops, &l ));
	l = loop( 43, 0, "", 255 );
	printf( "no sample: %d\n", CoF_LoopStart( &loops, &l ));
	for( i = loops.count; i < COF_LOOP_MAX; i++ ) { l = loop( 100 + i, 2, "x.wav", 255 ); CoF_LoopStart( &loops, &l ); }
	l = loop( 999, 2, "x.wav", 255 );
	printf( "full: %d (%d)\n", CoF_LoopStart( &loops, &l ), loops.count );
	l = loop( 100 + COF_LOOP_MAX - 1, 2, "x.wav", 10 );
	printf( "full, again: %d\n", CoF_LoopStart( &loops, &l ));

	l = loop( 40, 0, "ambience/wind.wav", 90 ); dead = loop( 44, 1, "ambience/fire.wav", 90 );
	printf( "first frame in 0: own %d\n", CoF_LoopMove( &l, 1, COF_LOOP_UNSEEN, 0 ));
	printf( "first frame in 1: other's %d\n", CoF_LoopMove( &l, 1, COF_LOOP_UNSEEN, 1 ));
	printf( "0 to 1: left's %d, new's %d, new's removed %d\n", CoF_LoopMove( &l, 1, 0, 1 ), CoF_LoopMove( &dead, 1, 0, 1 ), CoF_LoopMove( &dead, 0, 0, 1 ));
	printf( "1 to 2: left's removed %d, another's %d\n", CoF_LoopMove( &dead, 0, 1, 2 ), CoF_LoopMove( &l, 1, 1, 2 ));
	printf( "stays: %d\n", CoF_LoopMove( &l, 1, 0, 0 ));
	return 0;
}
