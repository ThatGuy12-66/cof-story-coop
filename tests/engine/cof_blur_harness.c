/* sv_cof_blur.h: the story's screen blur is one for the whole party. The game's side is played here: a flip is
 * cof_blur's Use on one player, a trip is MorphineTrip/StopMorphineTrip, and what the rule sets is written back the
 * way the engine writes m_iBlurActive. */
#include <stdio.h>
#include "blur_under_test.h"

#define PLAYERS 3

static cof_blur_state_t state;
static cof_blur_now_t game[PLAYERS];

static const char *why_name( int why )
{
	switch( why )
	{
	case COF_BLUR_FLIP: return "flip";
	case COF_BLUR_JOIN: return "join";
	case COF_BLUR_TRIP_END: return "trip-end";
	default: return "-";
	}
}

static void frame( const char *what )
{
	int set[PLAYERS], why[PLAYERS], i, source = CoF_BlurShare( &state, game, PLAYERS, set, why );
	printf( "%s: source=%d", what, source );
	for( i = 0; i < PLAYERS; i++ )
	{
		if( !game[i].present ) { printf( " -" ); continue; }
		if( set[i] >= 0 ) game[i].blur = set[i];
		printf( " %d%s%s", game[i].blur, set[i] >= 0 ? ":" : "", set[i] >= 0 ? why_name( why[i] ) : "" );
	}
	printf( "\n" );
}

static void arrive( int i, int blur ) { game[i].present = 1; game[i].blur = blur; game[i].trip = 0; }
static void leave( int i ) { game[i].present = 0; }
static void flip( int i ) { game[i].blur = !game[i].blur; }
static void trip_start( int i ) { game[i].trip = 1; game[i].blur = 1; }
static void trip_end( int i ) { game[i].trip = 0; game[i].blur = 0; }
static void new_map( void ) { CoF_BlurReset( &state, 0 ); }
static void checkpoint( void ) { CoF_BlurReset( &state, 1 ); }

static void carried( const char *map ) { printf( "carried %s: %d\n", map, CoF_BlurCarried( map )); }

int main( void )
{
	/* The 2026-10-20 player log, c_nightmare: the host alone, nightmulti turns the blur on for them, Wolf joins,
	 * Wolf reaches the exit first and multispotea2's flip is Wolf's. Retail leaves both blurred into c_start. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); frame( "nightmare host" );
	flip( 0 ); frame( "nightmulti flips the host" );
	arrive( 1, 0 ); frame( "the guest joins" );
	frame( "a quiet frame" );
	flip( 1 ); frame( "the exit flips the guest" );
	new_map(); frame( "c_start" );

	/* Alone, c_start's slower_eyes_mm flips four times: the game's own, nothing to share. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); frame( "solo" );
	flip( 0 ); frame( "solo flip 1" ); flip( 0 ); frame( "solo flip 2" );
	flip( 0 ); frame( "solo flip 3" ); flip( 0 ); frame( "solo flip 4" );

	/* A new map whose players disagree (a save from before): the first player seen sets the party. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 1 ); arrive( 1, 0 ); arrive( 2, 1 ); frame( "carried in apart" );

	/* A morphine trip is the tripper's own; when it ends they take the party's again. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); arrive( 1, 0 ); frame( "two players" );
	trip_start( 1 ); frame( "the guest trips" );
	flip( 0 ); frame( "the story flips the host" );
	trip_end( 1 ); frame( "the trip ends" );

	/* A flip that lands on a tripper still flips the party. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); arrive( 1, 0 ); frame( "two again" );
	trip_start( 1 ); frame( "the guest trips again" );
	flip( 1 ); frame( "the story flips the tripper" );
	trip_end( 1 ); frame( "that trip ends" );

	/* Gone and back (a reconnect): the returning player is a joiner. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); arrive( 1, 0 ); frame( "both here" );
	leave( 1 ); frame( "the guest drops" );
	flip( 0 ); frame( "a flip while they are gone" );
	arrive( 1, 0 ); frame( "the guest is back" );

	/* Two flips in one frame (two triggers at once): the lowest slot's decides, nobody is left apart. */
	new_map(); memset( game, 0, sizeof( game ));
	arrive( 0, 0 ); arrive( 1, 0 ); arrive( 2, 0 ); frame( "three players" );
	flip( 0 ); flip( 2 ); frame( "two flips at once" );

	/* A checkpoint saved while both were stuck (the player's cm_start autosave): the party starts off. */
	checkpoint(); memset( game, 0, sizeof( game ));
	arrive( 0, 1 ); frame( "checkpoint host" );
	arrive( 1, 1 ); frame( "checkpoint guest" );
	flip( 0 ); frame( "a flip after it" );

	/* Which maps hand their blur on to the next: the endings (merged ones too), the suicide maps, the game's own
	 * co-op maps. Every other map's flips come in pairs. */
	carried( "c_nightmare" ); carried( "cm_start" ); carried( "c_tricky" ); carried( "c_ending1b" );
	carried( "cm_c_ending1" ); carried( "cof_suicide2" ); carried( "c_cof_cabin" ); carried( "" );
	return 0;
}
