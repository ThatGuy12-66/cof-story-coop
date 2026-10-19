/* sv_story_transition_state.h: which rows of a hub snapshot a revisit leaves out. */
#include <stdio.h>
#include <string.h>
#include "transition_under_test.h"

static void skip( const char *what, int world, int incoming, int player, int inventory, int companion )
{
	printf( "%s: %d\n", what, CoF_TransitionSkipSavedEntity( world, incoming, player, inventory, companion ));
}

int main( void )
{
	skip( "a player row, revisit", 1, 0, 1, 0, 0 );
	skip( "a captured player's item, revisit", 1, 0, 0, 1, 0 );
	skip( "a companion's item, revisit", 1, 0, 0, 0, 1 );       /* it would come back with no owner */
	skip( "a companion's item, the arriving transition", 0, 1, 0, 0, 1 );  /* the engine adds the companions again with their own kit */
	skip( "a map entity, revisit", 1, 0, 0, 0, 0 );
	skip( "a captured player's item, arriving", 0, 1, 0, 1, 0 );  /* travels with its player */
	printf( "companion item: owner %d, attached %d, neither %d\n", CoF_TransitionCompanionItem( 1, 0 ),
		CoF_TransitionCompanionItem( 0, 1 ), CoF_TransitionCompanionItem( 0, 0 ));
	return 0;
}
