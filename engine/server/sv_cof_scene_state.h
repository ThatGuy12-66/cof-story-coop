/* Cutscene watchdog for Cry of Fear story co-op: what the retail cutscene code
 * leaves wrong with two players. Pure state, no engine types, so the offline
 * harness drives it directly. GPL-3.0-or-later.
 *
 * The retail trigger_camera (Use 10113B60) already gives its view to EVERY
 * player: the start loop (1011420B) calls SET_VIEW, sets FL_NOTARGET,
 * takedamage 0 and, with spawnflag 4, FL_FROZEN on each of them. Its end
 * (FollowTarget 1010AF30, 1010B267) undoes that only for LIVING players, and a
 * CPR revive (100E9AF1) is not a respawn: a partner who was down during a
 * scene comes back frozen, invulnerable and looking through a dead camera.
 * A player who respawns or rejoins mid-scene gets the view back from InitHUD
 * (100F72A2) but is never frozen, and the no-target/no-damage writes land on
 * the other player (100F74B7). Research: docs/research/2026-10-12-story-moments-re.md. */
#ifndef SV_COF_SCENE_STATE_H
#define SV_COF_SCENE_STATE_H

// One living player as the engine sees them this frame.
typedef struct
{
	int camera; // viewing a trigger_camera (edict index, 0 = no camera)
	int camera_live; // that camera exists and is running (its iuser1 != 0)
	int camera_freezes; // its spawnflags have 4 (take control)
	int frozen, notarget, invulnerable; // FL_FROZEN, FL_NOTARGET, takedamage == 0
	int telescope_off; // their telescope handle (P+370) points at a cof_telescope that is off (health 0)
	int cutscene_wait, was_cutscene_wait; // P+3C8 now and last frame: a late joiner held in black by a model cutscene
	int model_cutscene; // a cof_mdlcutscene is running (the only thing that ends that wait)
	int camera_model; // P+388 names their camera model (a model cutscene, a ladder or the strangle attaches one)
	int animating; // fuser3 != 0: CPR, phone, struggle, syringe or a fall
} cof_watch_t;

enum
{
	COF_WATCH_RELEASE = 1 << 0, // camera over: own view, no freeze, no-target or invulnerability
	COF_WATCH_HOLD = 1 << 1, // camera running: freeze, no-target and invulnerability like everyone else
	COF_WATCH_TELESCOPE = 1 << 2, // someone else switched their telescope off: take them out of it
	COF_WATCH_FADE_IN = 1 << 3, // the model cutscene they waited out in black is over
	// Held in that black wait with no model cutscene to end it: MP PlayerSpawn
	// (100C948D) starts the wait for ANY player's camera model, a ladder's or the
	// strangle's too, and only cof_mdlcutscene's end (1009F584) clears it.
	COF_WATCH_UNWAIT = 1 << 4
};

static int CoF_WatchDecide( const cof_watch_t *w )
{
	int act = 0;
	if( w->camera && !w->camera_live ) act |= COF_WATCH_RELEASE;
	else if( w->camera && w->camera_freezes && ( !w->frozen || !w->notarget || !w->invulnerable )) act |= COF_WATCH_HOLD;
	if( w->telescope_off ) act |= COF_WATCH_TELESCOPE;
	if( w->was_cutscene_wait && !w->cutscene_wait ) act |= COF_WATCH_FADE_IN;
	if( w->cutscene_wait && !w->model_cutscene ) act |= COF_WATCH_UNWAIT;
	return act;
}

// A map change: no camera survives it, so nobody may carry its freeze along.
static int CoF_WatchSettle( const cof_watch_t *w )
{
	return w->camera ? COF_WATCH_RELEASE : 0;
}

// Two players watch the same running camera: in the story they are one
// character, so a scripted move of one takes the other along.
static int CoF_WatchTogether( const cof_watch_t *a, const cof_watch_t *b )
{
	return a->camera && a->camera_live && a->camera == b->camera;
}

// Cutscene skip (cof_skip, Enter). The retail single-player skip runs the game
// faster (its client sets host_framerate, FWGS issue 2706); multiplayer rules
// switch it off (FollowTarget 1010AF30). Here the server runs several physics
// steps a frame instead, so every scripted event still happens, in order, only
// sooner; and only while every living person is held by a scene, so nobody
// ever plays at that speed (the three cameras that leave players free never).
#define COF_SKIP_GAP 2.0 // seconds with nobody held before a skip is over: two cameras of one scene are not that far apart
#define COF_SKIP_SPEED_MAX 20

typedef struct
{
	int wanted; // userid who asked; 0: no skip
	double free_since; // nobody held since; 0 while someone is
} cof_skip_t;

// One living person: held by a running scene, frozen in a running camera or in
// a running model cutscene through their own camera model; never in a player
// animation state (a struggle counts its key presses against the clock).
static int CoF_SkipHeld( const cof_watch_t *w )
{
	return w->frozen && !w->animating && (( w->camera && w->camera_live ) || ( w->model_cutscene && w->camera_model ));
}

// Physics steps for this frame. held: every living person is held, and there is
// one. *over: the skip ended here (the scene is over).
static int CoF_SkipSteps( cof_skip_t *s, int held, double time, int speed, int *over )
{
	*over = 0;
	if( !s->wanted ) return 1;
	if( held )
	{
		s->free_since = 0.0;
		return speed < 1 ? 1 : speed > COF_SKIP_SPEED_MAX ? COF_SKIP_SPEED_MAX : speed;
	}
	if( s->free_since == 0.0 ) s->free_since = time;
	else if( time - s->free_since >= COF_SKIP_GAP )
	{
		s->wanted = 0;
		s->free_since = 0.0;
		*over = 1;
	}
	return 1;
}
#endif
