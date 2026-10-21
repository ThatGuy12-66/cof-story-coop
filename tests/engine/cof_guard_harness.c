/* sv_cof_guard.h: client commands refused in story co-op. */
#include <stdio.h>
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
	scene( "unfreeze", "1024", 1 );  // a viewport reset's unfreeze while a camera holds the player
	scene( "unfreeze", "1024", 0 );  // a panel closing (telephone, yes/no, save)
	scene( "unfreeze", "512", 1 );   // the computer's own
	scene( "unfreeze", "666", 1 );
	scene( "stopcomputer", "", 1 );  // clears only the computer state, which a scene never sets
	scene( "freeze", "1024", 1 );
	return 0;
}
