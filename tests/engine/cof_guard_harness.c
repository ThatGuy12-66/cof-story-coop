/* sv_cof_guard.h: client commands refused in story co-op. */
#include <stdio.h>
#include <stdlib.h>
#include "guard_under_test.h"

static void refusal( const char *cmd, int story, int host )
{
	const char *reason = CoF_GuardRefusal( cmd, story, host );
	printf( "%s story=%d host=%d: %s\n", cmd, story, host, reason ? reason : "-" );
}

static void scene( const char *cmd, const char *arg, int held )
{
	const char *reason = CoF_GuardSceneRefusal( cmd, arg, held );
	printf( "%s %s held=%d: %s\n", cmd, arg, held, reason ? reason : "-" );
}

static void puzzle( const char *cmd, const char *arg, int solved )
{
	const char *reason = CoF_GuardPuzzleRefusal( cmd, arg, solved );
	printf( "%s %s solved=%d: %s\n", cmd, arg, solved, reason ? reason : "-" );
}

static void telephone( const char *cmd, int taken )
{
	const char *reason = CoF_GuardTelephoneRefusal( cmd, taken );
	printf( "%s taken=%d: %s\n", cmd, taken, reason ? reason : "-" );
}

int main( void )
{
	refusal( "vote", 1, 1 );        // nobody votes in story co-op, the host neither
	refusal( "votekick", 1, 0 );
	refusal( "vote", 0, 0 );        // not story co-op: the game's own rules
	refusal( "subtitleset", 1, 0 ); // the host's server-wide subtitle language
	refusal( "subtitleset", 1, 1 ); // the host's own choice
	refusal( "hinttoggle", 1, 0 );  // the host's server-wide hints
	refusal( "say", 1, 0 );
	refusal( "Vote", 1, 0 );        // the game compares case-sensitively: not its command
	refusal( "pause", 1, 0 );       // a guest's Backspace froze the host's game
	refusal( "pause", 1, 1 );       // the host pauses the party
	refusal( "pause", 0, 0 );
	refusal( "aitest", 1, 0 );     // a guest stopping every monster in the host's game
	refusal( "simonmode", 1, 0 );  // a guest killing c_ending4b's book Simon
	refusal( "aitest", 1, 1 );     // the host keeps the console's developer commands
	scene( "unfreeze", "1024", 1 );  // a viewport reset's unfreeze while a camera holds the player
	scene( "unfreeze", "1024", 0 );  // a panel closing (telephone, yes/no, save)
	scene( "unfreeze", "512", 1 );   // the computer's own
	scene( "unfreeze", "666", 1 );
	scene( "stopcomputer", "", 1 );  // clears only the computer state, which a scene never sets
	scene( "freeze", "1024", 1 );
	puzzle( "boilercheck", "2", 0 );   // 1.1.9 (K9): the bars before the solve
	puzzle( "boilercheck", "2", 1 );   // a bar closed again after it re-fired restore_power
	puzzle( "boilercheck", "5", 1 );   // leaving the puzzle stays
	puzzle( "puzzlebara", "", 1 );     // the console's bar commands too
	puzzle( "puzzlebarc", "", 0 );
	puzzle( "boilerchecks", "2", 1 );
	telephone( "telephoneright", 1 );  // 1.1.9: a dial for a telephone someone else opened since
	telephone( "telephonewrong", 1 );
	telephone( "telephoneright", 0 );  // its user dials
	telephone( "phonenumber", 1 );     // the mobile phone's own dialling is not the telephone's
	return 0;
}
