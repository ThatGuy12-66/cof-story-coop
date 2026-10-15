/* The AI companion's route graph (sv_cof_nav.h), driven directly with graphs
 * written in the game's own .nod layout. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sv_cof_nav.h"

typedef struct { float x, y, z; int land; } tnode;
typedef struct { int src, dest, bits; const char *model; } tlink;
#define WALK ( COF_NAV_HUMAN | 1 | 4 | 8 ) /* every hull, as the game marks an open corridor */

static unsigned char file[400000];
static cof_nav_t nav;

static void put_int( size_t at, int v ) { memcpy( file + at, &v, 4 ); }
static void put_float( size_t at, float v ) { memcpy( file + at, &v, 4 ); }

/* A graph file as hl.dll saves it: links grouped by their source node. */
static size_t build( const tnode *nodes, int n, const tlink *links, int l, int routes, int hashes )
{
	size_t node_at = 4 + COF_NAV_GRAPH, link_at = node_at + (size_t)n * COF_NAV_NODE, size;
	int i, k, written = 0;
	size = link_at + (size_t)l * COF_NAV_LINK + (size_t)n * COF_NAV_DIST + (size_t)routes + (size_t)hashes * 2;
	assert( size <= sizeof( file ));
	memset( file, 0xCD, size );
	put_int( 0, COF_NAV_VERSION );
	put_int( 4 + 24, n ); put_int( 4 + 28, l ); put_int( 4 + 32, routes ); put_int( 4 + 8384, hashes );
	for( i = 0; i < n; i++ )
	{
		size_t p = node_at + (size_t)i * COF_NAV_NODE;
		int first = written;
		put_float( p, nodes[i].x ); put_float( p + 4, nodes[i].y ); put_float( p + 8, nodes[i].z );
		put_int( p + 28, nodes[i].land ? COF_NAV_LAND : 2 );
		for( k = 0; k < l; k++ )
		{
			size_t q = link_at + (size_t)written * COF_NAV_LINK;
			if( links[k].src != i ) continue;
			put_int( q, links[k].src ); put_int( q + 4, links[k].dest ); put_int( q + 8, 0x12345678 );
			memset( file + q + 12, 0, 4 );
			if( links[k].model ) memcpy( file + q + 12, links[k].model, strlen( links[k].model ) < 4 ? strlen( links[k].model ) : 4 );
			put_int( q + 16, links[k].bits ); put_float( q + 20, 1.0f ); /* the saved length is not trusted */
			written++;
		}
		put_int( p + 32, written - first ); put_int( p + 36, first );
	}
	assert( written == l );
	return size;
}

static int door_blocked, door_cost;
static float doors( void *context, int link )
{
	(void)context;
	if( nav.link[link].door != 12 ) return 0.0f;
	return door_blocked ? -1.0f : (float)door_cost;
}

/* both ways between a and b */
static void pair( tlink *lines, int *l, int a, int b )
{
	lines[*l].src = a; lines[*l].dest = b; lines[*l].bits = WALK; lines[*l].model = NULL; ( *l )++;
	lines[*l].src = b; lines[*l].dest = a; lines[*l].bits = WALK; lines[*l].model = NULL; ( *l )++;
}

static int same( const int *path, int length, const int *want, int count )
{
	return length == count && !memcmp( path, want, sizeof( int ) * (size_t)count );
}

int main( void )
{
	/* A square room: A (0,0) B (100,0) C (100,100) D (0,100), and E, an air node, over it. */
	static const tnode square[] = { { 0, 0, 8, 1 }, { 100, 0, 8, 1 }, { 100, 100, 8, 1 }, { 0, 100, 8, 1 }, { 50, 50, 200, 0 } };
	tlink walls[] = {
		{ 0, 1, WALK, NULL }, { 1, 0, WALK, NULL }, { 1, 2, WALK, NULL }, { 2, 1, WALK, NULL },
		{ 2, 3, WALK, NULL }, { 3, 2, WALK, NULL }, { 3, 0, WALK, NULL }, { 0, 3, WALK, NULL },
		{ 0, 4, WALK, NULL }, { 4, 2, WALK, NULL } };
	int path[COF_NAV_NODES], found[16], length, i;
	size_t size;
	float point[3];

	/* Read: every walkable link between land nodes; lengths from the origins. */
	size = build( square, 5, walls, 10, 37, 19 );
	assert( CoF_NavLoad( &nav, file, size ) && nav.nodes == 5 && nav.links == 8 );
	assert( nav.node[4].count == 0 && !nav.node[4].land ); /* nothing to or from the air */
	assert( nav.link[nav.node[0].first].length > 99.9f && nav.link[nav.node[0].first].length < 100.1f );
	CoF_NavPoint( &nav, 2, point ); assert( point[0] == 100 && point[1] == 100 && point[2] == 36 );
	assert( CoF_NavLink( &nav, 0, 1 ) >= 0 && CoF_NavLink( &nav, 0, 2 ) < 0 && CoF_NavLink( &nav, 0, 4 ) < 0 );

	/* A route: start and goal included, in walking order. */
	length = CoF_NavRoute( &nav, 0, 2, NULL, NULL, path, 64 );
	assert( length == 3 && path[0] == 0 && path[2] == 2 && ( path[1] == 1 || path[1] == 3 ));
	assert( CoF_NavRoute( &nav, 2, 2, NULL, NULL, path, 64 ) == 1 && path[0] == 2 );
	assert( CoF_NavRoute( &nav, 0, 2, NULL, NULL, path, 2 ) == 0 ); /* longer than asked */
	assert( CoF_NavRoute( &nav, 0, 4, NULL, NULL, path, 64 ) == 0 && CoF_NavRoute( &nav, 0, 9, NULL, NULL, path, 64 ) == 0 );

	/* Only links the player hull passes and that are not switched off. */
	walls[0].bits = 1 | 4; /* A to B: small and large hulls only */
	walls[7].bits = WALK | COF_NAV_DISABLED; /* A to D: off */
	size = build( square, 5, walls, 10, 0, 0 );
	assert( CoF_NavLoad( &nav, file, size ) && nav.links == 6 );
	assert( CoF_NavRoute( &nav, 0, 2, NULL, NULL, path, 64 ) == 0 );
	/* one way: D to A walks (a drop), A to D does not */
	walls[0].bits = WALK;
	size = build( square, 5, walls, 10, 0, 0 );
	assert( CoF_NavLoad( &nav, file, size ));
	{ static const int want[] = { 0, 1, 2, 3 }; assert( same( path, CoF_NavRoute( &nav, 0, 3, NULL, NULL, path, 64 ), want, 4 )); }
	{ static const int want[] = { 3, 0 }; assert( same( path, CoF_NavRoute( &nav, 3, 0, NULL, NULL, path, 64 ), want, 2 )); }

	/* A door across B to C ("*12"): the cost says how it stands now. */
	walls[2].model = "*12"; walls[3].model = "*12"; walls[7].bits = WALK;
	size = build( square, 5, walls, 10, 0, 0 );
	assert( CoF_NavLoad( &nav, file, size ));
	assert( nav.link[CoF_NavLink( &nav, 1, 2 )].door == 12 && nav.link[CoF_NavLink( &nav, 0, 1 )].door == 0 );
	door_blocked = 1;
	{ static const int want[] = { 1, 0, 3, 2 }; assert( same( path, CoF_NavRoute( &nav, 1, 2, doors, NULL, path, 64 ), want, 4 )); }
	door_blocked = 0; door_cost = 0;
	{ static const int want[] = { 1, 2 }; assert( same( path, CoF_NavRoute( &nav, 1, 2, doors, NULL, path, 64 ), want, 2 )); }
	door_cost = 1000; /* shut but it may open it: the long way round is cheaper */
	{ static const int want[] = { 1, 0, 3, 2 }; assert( same( path, CoF_NavRoute( &nav, 1, 2, doors, NULL, path, 64 ), want, 4 )); }
	/* the door's model number in all four characters */
	walls[2].model = "*105";
	size = build( square, 5, walls, 10, 0, 0 );
	assert( CoF_NavLoad( &nav, file, size ) && nav.link[CoF_NavLink( &nav, 1, 2 )].door == 105 );

	/* Nearest land nodes to a body (its origin 36 over the floor), nearest first, within reach. */
	{
		float body[3] = { 90, 10, 36 };
		assert( CoF_NavNearest( &nav, body, 500, found, 16 ) == 4 && found[0] == 1 && found[3] == 3 );
		assert( CoF_NavNearest( &nav, body, 500, found, 2 ) == 2 && found[0] == 1 && ( found[1] == 0 || found[1] == 2 ));
		assert( CoF_NavNearest( &nav, body, 50, found, 16 ) == 1 && found[0] == 1 );
		assert( CoF_NavNearest( &nav, body, 500, found, 0 ) == 0 );
	}

	/* Anything but exactly the game's layout: refused, and the graph left empty. */
	size = build( square, 5, walls, 10, 0, 0 );
	assert( !CoF_NavLoad( &nav, file, size - 1 ) && nav.nodes == 0 );
	assert( !CoF_NavLoad( &nav, file, size + 1 ) && nav.nodes == 0 );
	assert( !CoF_NavLoad( &nav, file, 100 ) && !CoF_NavLoad( &nav, NULL, 0 ));
	put_int( 0, 15 ); assert( !CoF_NavLoad( &nav, file, size ));
	size = build( square, 5, walls, 10, 0, 0 );
	put_int( 4 + COF_NAV_GRAPH + 1 * COF_NAV_NODE + 36, 9 ); assert( !CoF_NavLoad( &nav, file, size ) && nav.nodes == 0 ); /* links past the end */
	size = build( square, 5, walls, 10, 0, 0 );
	put_int( 4 + COF_NAV_GRAPH + 5 * COF_NAV_NODE, 3 ); assert( !CoF_NavLoad( &nav, file, size )); /* a link that is not its node's */
	size = build( square, 5, walls, 10, 0, 0 );
	put_int( 4 + COF_NAV_GRAPH + 5 * COF_NAV_NODE + 4, 77 ); assert( !CoF_NavLoad( &nav, file, size )); /* to a node that does not exist */
	size = build( square, 5, walls, 10, 0, 0 );
	put_float( 4 + COF_NAV_GRAPH, 1e30f ); assert( !CoF_NavLoad( &nav, file, size ));
	size = build( square, 5, walls, 10, 0, 0 );
	put_int( 4 + 24, 0 ); assert( !CoF_NavLoad( &nav, file, size ));
	assert( CoF_NavRoute( &nav, 0, 1, NULL, NULL, path, 64 ) == 0 ); /* an empty graph has no routes */

	/* A big map: a 30 by 30 grid (900 nodes, 3480 links) corner to corner, and the
	 * shortest way out of a maze of random walls agrees with a plain search. */
	{
		static tnode grid[900];
		static tlink lines[3480];
		int x, y, l = 0, trial;
		for( y = 0; y < 30; y++ )
			for( x = 0; x < 30; x++ )
			{
				int me = y * 30 + x;
				grid[me].x = x * 64.0f; grid[me].y = y * 64.0f; grid[me].z = 8; grid[me].land = 1;
				if( x < 29 ) { pair( lines, &l, me, me + 1 ); }
				if( y < 29 ) { pair( lines, &l, me, me + 30 ); }
			}
		assert( l == 3480 );
		size = build( grid, 900, lines, l, 0, 0 );
		assert( CoF_NavLoad( &nav, file, size ) && nav.nodes == 900 && nav.links == 3480 );
		length = CoF_NavRoute( &nav, 0, 899, NULL, NULL, path, COF_NAV_NODES );
		assert( length == 59 && path[0] == 0 && path[58] == 899 );
		for( i = 1; i < length; i++ ) assert( CoF_NavLink( &nav, path[i - 1], path[i] ) >= 0 );
		srand( 7 );
		for( trial = 0; trial < 20; trial++ )
		{
			static float best[900];
			static int done[900];
			float route_cost = 0.0f;
			int k;
			for( k = 0; k < l; k++ ) lines[k].bits = ( rand() % 4 ) ? WALK : 1; /* a quarter of the links shut */
			size = build( grid, 900, lines, l, 0, 0 );
			assert( CoF_NavLoad( &nav, file, size ));
			/* the plain search: Dijkstra by scanning */
			for( k = 0; k < 900; k++ ) { best[k] = 1e30f; done[k] = 0; }
			best[0] = 0.0f;
			for( ;; )
			{
				int node = -1, j;
				for( k = 0; k < 900; k++ ) if( !done[k] && best[k] < 1e29f && ( node < 0 || best[k] < best[node] )) node = k;
				if( node < 0 ) break;
				done[node] = 1;
				for( j = nav.node[node].first; j < nav.node[node].first + nav.node[node].count; j++ )
					if( best[node] + nav.link[j].length < best[nav.link[j].dest] ) best[nav.link[j].dest] = best[node] + nav.link[j].length;
			}
			length = CoF_NavRoute( &nav, 0, 899, NULL, NULL, path, COF_NAV_NODES );
			if( best[899] > 1e29f ) { assert( length == 0 ); continue; }
			assert( length > 0 && path[0] == 0 && path[length - 1] == 899 );
			for( i = 1; i < length; i++ ) route_cost += nav.link[CoF_NavLink( &nav, path[i - 1], path[i] )].length;
			assert( route_cost < best[899] + 0.5f );
		}
	}

	puts( "nav scenarios passed" );
	return 0;
}
