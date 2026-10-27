/* Cry of Fear story co-op: the decisions of sv_cof_chairs.c (everyone in a wheelchair at the end of Ending 4), kept
 * free of the engine so tests/engine/cof_chairs_harness.c runs them. GPL-3.0-or-later. */
#ifndef SV_COF_CHAIRS_STATE_H
#define SV_COF_CHAIRS_STATE_H

#define COF_CHAIRS_SETTLE 0.5 // seconds a person stands free before they are seated (a section move, a respawn settle)
#define COF_CHAIRS_RETRY 2.0 // after a seat that did not take

typedef struct
{
	int spawned, fake, alive, riding; // in the game, an AI companion, alive, in a wheelchair already
	int frozen, camera, busy; // held by the game (a scene, a door), viewing through a camera, in a boat or a call
	int moving; // a level change or a scene for everyone (SV_CoF_TransitionActive, SV_CoF_SceneActive)
} cof_chairs_person_t;

// A person to seat now. on: someone rides a living chair the game seated them in, and the map has not stopped the
// ride. *free_since: when they last became free to seat (0: not now), kept up to date here.
static int CoF_ChairsSeat( int on, const cof_chairs_person_t *p, double *free_since, double now, double retry_after )
{
	int ready = on && p->spawned && !p->fake && p->alive && !p->riding && !p->frozen && !p->camera && !p->busy && !p->moving;
	if( !ready )
	{
		*free_since = 0.0;
		return 0;
	}
	if( *free_since <= 0.0 ) *free_since = now;
	return now - *free_since >= COF_CHAIRS_SETTLE && now >= retry_after;
}

// A chair that carried a rider last frame, the same chair still standing, alive and empty now: the map let its rider
// out (cof_stopwheelchair at the boss's death; a wheelchair_exit). A removed one is a leaver's (SV_CoF_MomentsDrop), a
// dead one the team wipe's (sv_cof_rescue.c): neither ends the ride. The wipe's own let-out of the living chairs
// (SV_CoF_ChairsReleaseAll) does end it, and the checkpoint it loads starts the map's ride state again.
static int CoF_ChairsStopped( int carried_before, int same, int removed, int dead, int carries )
{
	return carried_before && same && !removed && !dead && !carries;
}

// The one of count spots nearest to from (squared distance; ties: the first), -1 for none usable.
static int CoF_ChairsNearest( const float (*spots)[3], const int *usable, int count, const float *from )
{
	int i, best = -1;
	float best_d = 0.0f;
	for( i = 0; i < count; i++ )
	{
		float dx, dy, dz, d;
		if( !usable[i] ) continue;
		dx = spots[i][0] - from[0]; dy = spots[i][1] - from[1]; dz = spots[i][2] - from[2];
		d = dx * dx + dy * dy + dz * dz;
		if( best < 0 || d < best_d ) { best = i; best_d = d; }
	}
	return best;
}
#endif
