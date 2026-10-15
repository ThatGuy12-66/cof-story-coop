/* The AI companion's route graph reader against real Cry of Fear graphs
 * (embedded by the test: graph_bytes.h), checked against the test's own reading. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_nav.h"
#include "graph_bytes.h"

static cof_nav_t nav;

int main( void )
{
	static int path[COF_NAV_NODES];
	int g, a, b, k;
	for( g = 0; g < GRAPHS; g++ )
	{
		int doors = 0, routes = 0;
		assert( CoF_NavLoad( &nav, graph_data[g], graph_size[g] ));
		assert( nav.nodes == want_nodes[g] && nav.links == want_links[g] );
		for( k = 0; k < nav.links; k++ ) doors += nav.link[k].door != 0;
		assert( doors == want_doors[g] );
		/* a way between every two nodes the plain reading says connect */
		for( a = 0; a < nav.nodes; a++ )
			for( b = 0; b < nav.nodes; b++ )
			{
				int length = CoF_NavRoute( &nav, a, b, NULL, NULL, path, COF_NAV_NODES );
				if( length ) routes++;
				for( k = 1; k < length; k++ ) assert( CoF_NavLink( &nav, path[k - 1], path[k] ) >= 0 );
			}
		assert( routes == want_routes[g] );
		printf( "graph %d: %d nodes, %d links, %d through doors, %d routes\n", g, nav.nodes, nav.links, doors, routes );
	}
	puts( "real graphs passed" );
	return 0;
}
