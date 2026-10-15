/* The AI companion's map knowledge for Cry of Fear story co-op: the game's own
 * monster route graph, and the shortest way along it. Pure data and maths, no
 * engine types, so the offline harness drives it directly. GPL-3.0-or-later.
 *
 * hl.dll builds a graph from each map's info_node points (125 of the 226 maps
 * have them) and saves it as maps/graphs/<map>.nod: the HLSDK CGraph written
 * as is (nodes.cpp FSaveGraph, 32-bit). Layout, checked against all 117 Cry of
 * Fear graphs (2026-10-14):
 *   int version (16); the CGraph object, 8396 bytes (node count at 24, link
 *   count at 28, route table size at 32, hash link count at 8384);
 *   the nodes, 88 bytes each (CNode: origin at 0, node bits at 28, link count
 *   at 32, first link at 36); the links, 24 bytes each (CLink: source node at
 *   0, destination at 4, the blocking brush model "*N" in 4 chars at 12, hull
 *   bits at 16, length at 20); 16 bytes a node of sorted distances; the route
 *   table; 2 bytes a hash link.
 * A land node lies 8 units over the floor (NODE_HEIGHT). A link the player
 * sized hull (32x32x72, walking, 18-unit steps) passes has the human hull bit;
 * one through a door names the door's brush model. Links are one way (a drop
 * walks down, not up). */
#ifndef SV_COF_NAV_H
#define SV_COF_NAV_H
#include <math.h>
#include <string.h>

#define COF_NAV_VERSION 16
#define COF_NAV_GRAPH 8396 // sizeof( CGraph )
#define COF_NAV_NODE 88 // sizeof( CNode )
#define COF_NAV_LINK 24 // sizeof( CLink )
#define COF_NAV_DIST 16 // sizeof( DIST_INFO )
#define COF_NAV_NODES 1024 // the HLSDK's MAX_NODES
#define COF_NAV_LINKS 16384 // the largest Cry of Fear graph has 3483
#define COF_NAV_LAND ( 1 << 0 ) // bits_NODE_LAND
#define COF_NAV_HUMAN ( 1 << 1 ) // bits_LINK_HUMAN_HULL
#define COF_NAV_DISABLED ( 1 << 4 ) // bits_LINK_DISABLED
#define COF_NAV_RAISE 28.0f // a land node is 8 over the floor, a standing body's origin 36

typedef struct
{
	float origin[3];
	int land;
	int first, count; // its walkable links
} cof_nav_node_t;

typedef struct
{
	int src, dest;
	int door; // the brush model across it ("*N": N), 0 none
	float length;
} cof_nav_link_t;

typedef struct
{
	int nodes, links;
	cof_nav_node_t node[COF_NAV_NODES];
	cof_nav_link_t link[COF_NAV_LINKS];
	// the search's scratch
	float cost[COF_NAV_NODES], score[COF_NAV_NODES];
	int from[COF_NAV_NODES], heap[COF_NAV_NODES], place[COF_NAV_NODES]; // place: in the heap at, -1 unseen, -2 done
	int heaped;
} cof_nav_t;

// The extra cost of walking a link now (0: none), or below 0 when it cannot be walked.
typedef float (*cof_nav_cost_t)( void *context, int link );

static int CoF_NavInt( const unsigned char *p )
{
	int value;
	memcpy( &value, p, sizeof( value ));
	return value;
}

static float CoF_NavFloat( const unsigned char *p )
{
	float value;
	memcpy( &value, p, sizeof( value ));
	return value;
}

static float CoF_NavDistance( const float *a, const float *b )
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return sqrtf( x * x + y * y + z * z );
}

// Where a standing body's origin is over node i.
static void CoF_NavPoint( const cof_nav_t *nav, int i, float *point )
{
	point[0] = nav->node[i].origin[0];
	point[1] = nav->node[i].origin[1];
	point[2] = nav->node[i].origin[2] + COF_NAV_RAISE;
}

// Reads a saved graph, keeping the links a player can walk between land nodes.
// Returns 0 (and an empty graph) for anything that is not exactly that layout.
static int CoF_NavLoad( cof_nav_t *nav, const unsigned char *data, size_t size )
{
	const unsigned char *nodes, *links;
	int n, l, routes, hashes, i, k;
	nav->nodes = nav->links = 0;
	if( !data || size < 4 + COF_NAV_GRAPH || CoF_NavInt( data ) != COF_NAV_VERSION ) return 0;
	n = CoF_NavInt( data + 4 + 24 );
	l = CoF_NavInt( data + 4 + 28 );
	routes = CoF_NavInt( data + 4 + 32 );
	hashes = CoF_NavInt( data + 4 + 8384 );
	if( n <= 0 || n > COF_NAV_NODES || l < 0 || l > COF_NAV_LINKS || routes < 0 || routes > ( 1 << 24 ) || hashes < 0 || hashes > ( 1 << 24 ))
		return 0;
	if( size != 4 + (size_t)COF_NAV_GRAPH + (size_t)n * ( COF_NAV_NODE + COF_NAV_DIST ) + (size_t)l * COF_NAV_LINK + (size_t)routes + (size_t)hashes * 2 )
		return 0;
	nodes = data + 4 + COF_NAV_GRAPH;
	links = nodes + (size_t)n * COF_NAV_NODE;
	for( i = 0; i < n; i++ )
	{
		const unsigned char *p = nodes + (size_t)i * COF_NAV_NODE;
		cof_nav_node_t *node = &nav->node[i];
		int count = CoF_NavInt( p + 32 ), first = CoF_NavInt( p + 36 );
		node->origin[0] = CoF_NavFloat( p );
		node->origin[1] = CoF_NavFloat( p + 4 );
		node->origin[2] = CoF_NavFloat( p + 8 );
		node->land = ( CoF_NavInt( p + 28 ) & COF_NAV_LAND ) != 0;
		node->first = nav->links;
		node->count = 0;
		if( !( fabsf( node->origin[0] ) < 65536.0f && fabsf( node->origin[1] ) < 65536.0f && fabsf( node->origin[2] ) < 65536.0f ))
			break;
		if( count < 0 || first < 0 || first > l - count ) break;
		if( !node->land ) continue;
		for( k = first; k < first + count; k++ )
		{
			const unsigned char *q = links + (size_t)k * COF_NAV_LINK;
			int dest = CoF_NavInt( q + 4 ), j, door = 0;
			if( CoF_NavInt( q ) != i || dest < 0 || dest >= n ) break;
			if(( CoF_NavInt( q + 16 ) & ( COF_NAV_HUMAN | COF_NAV_DISABLED )) != COF_NAV_HUMAN || dest == i ) continue;
			if( !( CoF_NavInt( nodes + (size_t)dest * COF_NAV_NODE + 28 ) & COF_NAV_LAND )) continue;
			if( q[12] == '*' )
				for( j = 13; j < 16 && q[j] >= '0' && q[j] <= '9'; j++ ) door = door * 10 + ( q[j] - '0' );
			nav->link[nav->links].src = i;
			nav->link[nav->links].dest = dest;
			nav->link[nav->links].door = door;
			nav->links++;
			node->count++;
		}
		if( k < first + count ) break;
	}
	if( i < n )
	{
		nav->links = 0;
		return 0;
	}
	// lengths once every origin is known
	for( i = 0; i < n; i++ )
		for( k = nav->node[i].first; k < nav->node[i].first + nav->node[i].count; k++ )
			nav->link[k].length = CoF_NavDistance( nav->node[i].origin, nav->node[nav->link[k].dest].origin );
	nav->nodes = n;
	return 1;
}

// Up to max land nodes whose point lies within reach of pos, nearest first.
static int CoF_NavNearest( const cof_nav_t *nav, const float *pos, float reach, int *out, int max )
{
	float distance[16], point[3];
	int i, found = 0;
	if( max <= 0 ) return 0;
	if( max > 16 ) max = 16;
	for( i = 0; i < nav->nodes; i++ )
	{
		float d;
		int at;
		if( !nav->node[i].land ) continue;
		CoF_NavPoint( nav, i, point );
		if(( d = CoF_NavDistance( pos, point )) > reach ) continue;
		if( found == max && d >= distance[found - 1] ) continue;
		at = found < max ? found++ : found - 1;
		while( at > 0 && distance[at - 1] > d )
		{
			distance[at] = distance[at - 1];
			out[at] = out[at - 1];
			at--;
		}
		distance[at] = d;
		out[at] = i;
	}
	return found;
}

static void CoF_NavSwap( cof_nav_t *nav, int a, int b )
{
	int t = nav->heap[a];
	nav->heap[a] = nav->heap[b];
	nav->heap[b] = t;
	nav->place[nav->heap[a]] = a;
	nav->place[nav->heap[b]] = b;
}

static void CoF_NavUp( cof_nav_t *nav, int at )
{
	while( at > 0 && nav->score[nav->heap[( at - 1 ) / 2]] > nav->score[nav->heap[at]] )
	{
		CoF_NavSwap( nav, at, ( at - 1 ) / 2 );
		at = ( at - 1 ) / 2;
	}
}

static int CoF_NavPop( cof_nav_t *nav )
{
	int top = nav->heap[0], at = 0;
	if( --nav->heaped > 0 )
	{
		nav->heap[0] = nav->heap[nav->heaped];
		nav->place[nav->heap[0]] = 0;
	}
	for( ;; )
	{
		int left = at * 2 + 1, right = left + 1, best = at;
		if( left < nav->heaped && nav->score[nav->heap[left]] < nav->score[nav->heap[best]] ) best = left;
		if( right < nav->heaped && nav->score[nav->heap[right]] < nav->score[nav->heap[best]] ) best = right;
		if( best == at ) break;
		CoF_NavSwap( nav, at, best );
		at = best;
	}
	nav->place[top] = -2;
	return top;
}

// The shortest way from node start to node goal (A*, straight-line guesses),
// each link costing its length plus what extra says. Fills path with the nodes
// in walking order, start and goal included, and returns how many (0: no way,
// or longer than max).
static int CoF_NavRoute( cof_nav_t *nav, int start, int goal, cof_nav_cost_t extra, void *context, int *path, int max )
{
	int i, node, length;
	if( start < 0 || goal < 0 || start >= nav->nodes || goal >= nav->nodes || max <= 0 ) return 0;
	for( i = 0; i < nav->nodes; i++ ) nav->place[i] = -1;
	nav->cost[start] = 0.0f;
	nav->from[start] = -1;
	nav->score[start] = CoF_NavDistance( nav->node[start].origin, nav->node[goal].origin );
	nav->heap[0] = start;
	nav->place[start] = 0;
	nav->heaped = 1;
	while( nav->heaped > 0 )
	{
		int k;
		if(( node = CoF_NavPop( nav )) == goal ) break;
		for( k = nav->node[node].first; k < nav->node[node].first + nav->node[node].count; k++ )
		{
			const cof_nav_link_t *link = &nav->link[k];
			float add, cost;
			if( nav->place[link->dest] == -2 ) continue;
			if(( add = extra ? extra( context, k ) : 0.0f ) < 0.0f ) continue;
			cost = nav->cost[node] + link->length + add;
			if( nav->place[link->dest] >= 0 && cost >= nav->cost[link->dest] ) continue;
			nav->cost[link->dest] = cost;
			nav->from[link->dest] = node;
			nav->score[link->dest] = cost + CoF_NavDistance( nav->node[link->dest].origin, nav->node[goal].origin );
			if( nav->place[link->dest] < 0 )
			{
				nav->heap[nav->heaped] = link->dest;
				nav->place[link->dest] = nav->heaped++;
			}
			CoF_NavUp( nav, nav->place[link->dest] );
		}
	}
	if( nav->place[goal] != -2 ) return 0;
	for( length = 0, node = goal; node >= 0; node = nav->from[node] ) length++;
	if( length > max ) return 0;
	for( i = length - 1, node = goal; node >= 0; node = nav->from[node] ) path[i--] = node;
	return length;
}

// The link from node a to node b, or -1.
static int CoF_NavLink( const cof_nav_t *nav, int a, int b )
{
	int k;
	if( a < 0 || a >= nav->nodes ) return -1;
	for( k = nav->node[a].first; k < nav->node[a].first + nav->node[a].count; k++ )
		if( nav->link[k].dest == b ) return k;
	return -1;
}
#endif
