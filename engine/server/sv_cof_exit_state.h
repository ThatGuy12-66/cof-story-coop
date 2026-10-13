/* Exit hold for Cry of Fear story co-op: who must be at a level exit before it
 * goes. Pure state, no engine types, so the offline harness drives it
 * directly. GPL-3.0-or-later.
 * Rules: docs/superpowers/specs/2026-09-30-coop-split-play-design.md, part 1
 * (project repo). A held exit is stored once and released by the frame; it
 * never depends on the game asking again, because some exits are fired by a
 * script or a once-only trigger. */
#ifndef SV_COF_EXIT_STATE_H
#define SV_COF_EXIT_STATE_H
#include <string.h>

#define COF_EXIT_PLAYERS 32
#define COF_EXIT_NAME 64
#define COF_EXIT_NEAR 128.0f // "at the exit" also means this close to the waiting player
#define COF_EXIT_ESCAPE 10.0 // the waiter may leave alone after this long
#define COF_EXIT_PROMPT 2.0

enum { COF_EXIT_GO, COF_EXIT_HOLD };
enum { COF_EXIT_IDLE, COF_EXIT_WAIT, COF_EXIT_READY, COF_EXIT_CANCELLED };

// One client slot. userid 0: nobody who counts (a free slot or a fake client).
typedef struct { int userid, spawned, alive, touching; float origin[3]; } cof_exit_player_t;

typedef struct
{
	int active; // an exit is being held
	int walkin; // the waiter stood in the exit: walking away from it cancels
	int waiter; // userid; 0 while nobody is alive to wait
	int generation; // the map this hold belongs to
	char level[COF_EXIT_NAME], landmark[COF_EXIT_NAME];
	float spot[3]; // the exit's landmark: a scripted exit's waiter is the nearest player
	float where[3]; // a walk-in waiter's origin at the hold's start (or last touch)
	double began, prompt_at;
} cof_exit_state_t;

static void CoF_ExitClear( cof_exit_state_t *s )
{
	memset( s, 0, sizeof( *s ));
}

static void CoF_ExitCopy( char *dst, const char *src )
{
	size_t i;
	for( i = 0; i + 1 < COF_EXIT_NAME && src[i]; i++ ) dst[i] = src[i];
	dst[i] = 0;
}

static const cof_exit_player_t *CoF_ExitFind( const cof_exit_player_t *players, int n, int userid )
{
	int i;
	for( i = 0; userid && i < n; i++ )
		if( players[i].userid == userid ) return &players[i];
	return NULL;
}

static int CoF_ExitCounted( const cof_exit_player_t *players, int n )
{
	int i, count = 0;
	for( i = 0; i < n; i++ )
		if( players[i].userid ) count++;
	return count;
}

static float CoF_ExitDistanceSquared( const float *a, const float *b )
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

static int CoF_ExitCanWait( const cof_exit_player_t *p )
{
	return p && p->userid && p->spawned && p->alive;
}

// Standing where the exit can take them: in it, or beside the waiting player.
// A dead player, or one still joining, holds the exit.
static int CoF_ExitAt( const cof_exit_player_t *p, const cof_exit_player_t *waiter )
{
	if( !CoF_ExitCanWait( p ) || !CoF_ExitCanWait( waiter )) return 0;
	return p == waiter || p->touching || CoF_ExitDistanceSquared( p->origin, waiter->origin ) <= COF_EXIT_NEAR * COF_EXIT_NEAR;
}

// The userid of the first counted player who is not at the exit; 0 when everyone is.
static int CoF_ExitMissing( const cof_exit_player_t *players, int n, int waiter )
{
	const cof_exit_player_t *w = CoF_ExitFind( players, n, waiter );
	int i;
	for( i = 0; i < n; i++ )
		if( players[i].userid && !CoF_ExitAt( &players[i], w )) return players[i].userid;
	return 0;
}

// Who waits: a player standing in the exit, else (a scripted exit) the living
// player nearest its landmark. 0 when nobody can.
static int CoF_ExitPickWaiter( const cof_exit_player_t *players, int n, const float *spot, int *walkin )
{
	int i, best = -1;
	float d, nearest = 0.0f;
	*walkin = 0;
	for( i = 0; i < n; i++ )
	{
		if( !CoF_ExitCanWait( &players[i] )) continue;
		if( players[i].touching )
		{
			*walkin = 1;
			return players[i].userid;
		}
		d = CoF_ExitDistanceSquared( players[i].origin, spot );
		if( best < 0 || d < nearest ) { best = i; nearest = d; }
	}
	return best < 0 ? 0 : players[best].userid;
}

// First come, first served: a hold for another exit in this map whose waiter
// still stands in it. Without this, two players in two exits would replace
// each other's hold every frame.
static int CoF_ExitTaken( const cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation,
	const char *level, const char *landmark )
{
	const cof_exit_player_t *w;
	if( !s->active || s->generation != generation || ( !strcmp( s->level, level ) && !strcmp( s->landmark, landmark ))) return 0;
	w = CoF_ExitFind( players, n, s->waiter );
	return CoF_ExitCanWait( w ) && w->touching;
}

// The game asked to change level. GO: change now. HOLD: the exit is held and
// CoF_ExitFrame lets it go, or another exit's hold came first and this request
// is left for its trigger to make again. *began is set when this request
// started a new hold.
static int CoF_ExitRequest( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation,
	const char *level, const char *landmark, const float *spot, double time, int *began )
{
	int same = s->active && s->generation == generation && !strcmp( s->level, level ) && !strcmp( s->landmark, landmark );
	int waiter, walkin;
	*began = 0;
	if( strlen( level ) >= COF_EXIT_NAME || strlen( landmark ) >= COF_EXIT_NAME )
	{
		CoF_ExitClear( s );
		return COF_EXIT_GO;
	}
	if( CoF_ExitTaken( s, players, n, generation, level, landmark )) return COF_EXIT_HOLD;
	if( same && CoF_ExitCanWait( CoF_ExitFind( players, n, s->waiter )))
	{
		waiter = s->waiter;
		walkin = s->walkin;
	}
	else waiter = CoF_ExitPickWaiter( players, n, spot, &walkin );
	// Nobody alive (a corpse lying in the exit still asks for it): the dead are never
	// taken to the next map. Held for whoever is back first; a team-wipe recovery or
	// the game's own restart may come first and end the hold with the map.
	if( waiter ? CoF_ExitCounted( players, n ) <= 1 || !CoF_ExitMissing( players, n, waiter ) : !CoF_ExitCounted( players, n ))
	{
		CoF_ExitClear( s );
		return COF_EXIT_GO;
	}
	if( !same )
	{
		// a newer request replaces whatever was held
		CoF_ExitClear( s );
		s->active = 1;
		s->generation = generation;
		s->began = s->prompt_at = time;
		CoF_ExitCopy( s->level, level );
		CoF_ExitCopy( s->landmark, landmark );
		memcpy( s->spot, spot, sizeof( s->spot ));
		*began = 1;
	}
	s->waiter = waiter;
	s->walkin = walkin;
	{
		// A once-only trigger removes itself just after firing, so "touching"
		// drops while the waiter has not moved: remember where they stood.
		const cof_exit_player_t *w = CoF_ExitFind( players, n, waiter );
		if( w && ( *began || w->touching )) memcpy( s->where, w->origin, sizeof( s->where ));
	}
	return COF_EXIT_HOLD;
}

// Once per server frame. READY leaves the state for the caller to read the
// level and landmark; the caller clears it.
// refires: something can still ask for this exit by touch (its own
// trigger_changelevel, or a trigger that fires it). Only then may a walk-in
// hold be cancelled: a use-only exit fired by a once-only trigger is never
// asked for again, so cancelling it would strand the party for good.
static int CoF_ExitFrame( cof_exit_state_t *s, const cof_exit_player_t *players, int n, int generation, double time, int refires )
{
	const cof_exit_player_t *w;
	(void)time;
	if( !s->active ) return COF_EXIT_IDLE;
	if( s->generation != generation )
	{
		CoF_ExitClear( s ); // the map changed another way
		return COF_EXIT_IDLE;
	}
	w = CoF_ExitFind( players, n, s->waiter );
	if( s->walkin && ( !CoF_ExitCanWait( w ) ||
		( !w->touching && CoF_ExitDistanceSquared( w->origin, s->where ) > COF_EXIT_NEAR * COF_EXIT_NEAR )))
	{
		// walked away, died or left: the trigger asks again if someone returns.
		// Merely not touching is no reason: a once-only trigger vanishes after firing.
		if( refires )
		{
			CoF_ExitClear( s );
			return COF_EXIT_CANCELLED;
		}
		// Nothing will ask again: keep the exit as a scripted hold instead.
		s->walkin = 0;
	}
	if( !CoF_ExitCanWait( w ))
	{
		// A scripted exit already ran its script: it still has to happen, so
		// someone else waits for it.
		int walkin;
		s->waiter = CoF_ExitPickWaiter( players, n, s->spot, &walkin );
		if( !s->waiter ) return COF_EXIT_WAIT; // nobody alive: the game's own restart decides
	}
	if( CoF_ExitCounted( players, n ) <= 1 || !CoF_ExitMissing( players, n, s->waiter )) return COF_EXIT_READY;
	return COF_EXIT_WAIT;
}

// The waiter pressed the warp key. 1: leave now. 0: not the waiter.
// -1: too early, with *left seconds to go.
static int CoF_ExitEscape( const cof_exit_state_t *s, int userid, double time, int *left )
{
	*left = 0;
	if( !s->active || !userid || userid != s->waiter ) return 0;
	if( time - s->began >= COF_EXIT_ESCAPE ) return 1;
	*left = (int)( COF_EXIT_ESCAPE - ( time - s->began ) + 0.999 );
	return -1;
}

static int CoF_ExitPromptDue( cof_exit_state_t *s, double time )
{
	if( !s->active || !s->waiter || time < s->prompt_at ) return 0;
	s->prompt_at = time + COF_EXIT_PROMPT;
	return 1;
}
#endif
