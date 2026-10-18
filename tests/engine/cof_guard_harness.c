/* sv_cof_guard.h: client commands refused in story co-op. */
#include <stdio.h>
#include "guard_under_test.h"

static void refusal( const char *cmd, int story, int host )
{
	const char *reason = CoF_GuardRefusal( cmd, story, host );
	printf( "%s story=%d host=%d: %s\n", cmd, story, host, reason ? reason : "-" );
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
	return 0;
}
