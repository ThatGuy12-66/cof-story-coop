/* Cutscene watchdog: the rules, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_scene_state.h"

static cof_watch_t w;

/* In a running take-control camera, as the retail start loop leaves everyone. */
static void watching( int camera )
{
	memset( &w, 0, sizeof( w ));
	w.camera = camera; w.camera_live = w.camera_freezes = 1;
	w.frozen = w.notarget = w.invulnerable = 1;
}

int main( void )
{
	cof_watch_t other;

	/* Ordinary play, and a camera running as retail set it up: nothing to do. */
	memset( &w, 0, sizeof( w ));
	assert( CoF_WatchDecide( &w ) == 0 && CoF_WatchSettle( &w ) == 0 );
	watching( 40 ); assert( CoF_WatchDecide( &w ) == 0 );

	/* Revived after the scene ended without them: still looking through it,
	 * frozen and invulnerable. Released, whatever their flags say. */
	watching( 40 ); w.camera_live = 0;
	assert( CoF_WatchDecide( &w ) == COF_WATCH_RELEASE );
	w.frozen = w.notarget = w.invulnerable = 0;
	assert( CoF_WatchDecide( &w ) == COF_WATCH_RELEASE );

	/* Respawned or rejoined mid-scene: InitHUD gave the view back but no freeze,
	 * or the no-target/no-damage writes went to the other player. Held like everyone. */
	watching( 40 ); w.frozen = 0; assert( CoF_WatchDecide( &w ) == COF_WATCH_HOLD );
	watching( 40 ); w.notarget = 0; assert( CoF_WatchDecide( &w ) == COF_WATCH_HOLD );
	watching( 40 ); w.invulnerable = 0; assert( CoF_WatchDecide( &w ) == COF_WATCH_HOLD );
	/* a camera that does not take control holds nobody */
	watching( 40 ); w.camera_freezes = 0; w.frozen = w.notarget = w.invulnerable = 0;
	assert( CoF_WatchDecide( &w ) == 0 );

	/* The telescope switched off under its user, and a late joiner's black wait ending. */
	memset( &w, 0, sizeof( w )); w.telescope_off = 1;
	assert( CoF_WatchDecide( &w ) == COF_WATCH_TELESCOPE );
	memset( &w, 0, sizeof( w )); w.was_cutscene_wait = 1;
	assert( CoF_WatchDecide( &w ) == COF_WATCH_FADE_IN );
	w.cutscene_wait = 1; w.model_cutscene = 1; assert( CoF_WatchDecide( &w ) == 0 ); /* still waiting */
	w.was_cutscene_wait = 0; assert( CoF_WatchDecide( &w ) == 0 ); /* just started */
	/* Review I-2: the wait was started by a ladder's or the strangle's camera
	 * model and no model cutscene will ever end it: let them go. */
	w.model_cutscene = 0; assert( CoF_WatchDecide( &w ) == COF_WATCH_UNWAIT );
	/* several at once */
	watching( 40 ); w.camera_live = 0; w.telescope_off = 1; w.was_cutscene_wait = 1; w.model_cutscene = 1;
	assert( CoF_WatchDecide( &w ) == ( COF_WATCH_RELEASE | COF_WATCH_TELESCOPE | COF_WATCH_FADE_IN ));

	/* A map change releases anyone looking through any camera, running or not. */
	watching( 40 ); assert( CoF_WatchSettle( &w ) == COF_WATCH_RELEASE );
	w.camera_live = 0; assert( CoF_WatchSettle( &w ) == COF_WATCH_RELEASE );

	/* Watching together: the same running camera. */
	watching( 40 ); other = w;
	assert( CoF_WatchTogether( &w, &other ) && CoF_WatchTogether( &other, &w ));
	other.camera = 41; assert( !CoF_WatchTogether( &w, &other ));
	other.camera = 40; w.camera_live = 0; assert( !CoF_WatchTogether( &w, &other ));
	memset( &w, 0, sizeof( w )); memset( &other, 0, sizeof( other ));
	assert( !CoF_WatchTogether( &w, &other )); /* nobody in a camera is not "together" */

	/* 1.1.8 (K1, K4): a running camera that is not theirs (the wardrobe's, another merged-map section's) is left at
	 * once, never held; a model cutscene of another section is left as its end would. */
	watching( 40 ); w.camera_elsewhere = 1; assert( CoF_WatchDecide( &w ) == COF_WATCH_RELEASE );
	w.frozen = 0; assert( CoF_WatchDecide( &w ) == COF_WATCH_RELEASE );
	memset( &w, 0, sizeof( w )); w.model_scene = 11; assert( CoF_WatchDecide( &w ) == 0 );
	w.model_elsewhere = 1; assert( CoF_WatchDecide( &w ) == COF_WATCH_MODEL_RELEASE );
	/* ...and is nobody's to watch together */
	watching( 40 ); other = w; other.camera_elsewhere = 1;
	assert( !CoF_WatchTogether( &w, &other ) && !CoF_WatchTogether( &other, &w ));

	/* 1.1.8 (M3 finding 2a): two frozen in one running model cutscene watch it together. */
	memset( &w, 0, sizeof( w )); w.model_scene = 11; w.frozen = 1; other = w;
	assert( CoF_WatchTogether( &w, &other ));
	other.frozen = 0; assert( !CoF_WatchTogether( &w, &other ) && !CoF_WatchTogether( &other, &w ));
	other.frozen = 1; other.model_scene = 12; assert( !CoF_WatchTogether( &w, &other ));
	other.model_scene = 11; other.model_elsewhere = 1; assert( !CoF_WatchTogether( &w, &other ));
	w.model_scene = other.model_scene = 0; other.model_elsewhere = 0; assert( !CoF_WatchTogether( &w, &other ));

	/* 1.1.8 (M1 finding 5): both watched one camera to its end, at most 2 s ago. */
	{
		cof_watch_seen_t a = { 40, 3, 10.0 }, b = { 40, 3, 10.0 };
		assert( CoF_WatchRecent( &a, &b, 0, 11.0 ) && CoF_WatchRecent( &a, &b, 0, 12.0 ));
		assert( !CoF_WatchRecent( &a, &b, 0, 12.1 )); /* too long ago */
		assert( !CoF_WatchRecent( &a, &b, 0, 1.0 )); /* a time from before it: another map's clock */
		assert( !CoF_WatchRecent( &a, &b, 1, 11.0 )); /* it still runs: one of them left it */
		b.seen = 7.0; assert( !CoF_WatchRecent( &a, &b, 0, 11.0 )); /* one stopped watching long before its end */
		b.seen = 10.0; b.serial = 4; assert( !CoF_WatchRecent( &a, &b, 0, 11.0 )); /* another entity in that slot */
		b.serial = 3; b.camera = 41; assert( !CoF_WatchRecent( &a, &b, 0, 11.0 ));
		memset( &a, 0, sizeof( a )); memset( &b, 0, sizeof( b )); assert( !CoF_WatchRecent( &a, &b, 0, 0.5 ));
	}

	/* Skipping. Held: frozen in a running camera, or in a model cutscene. */
	watching( 40 ); assert( CoF_SkipHeld( &w ));
	w.frozen = 0; assert( !CoF_SkipHeld( &w )); /* a camera that leaves them free: they play, never at speed */
	watching( 40 ); w.camera_live = 0; assert( !CoF_SkipHeld( &w ));
	memset( &w, 0, sizeof( w )); w.frozen = 1; assert( !CoF_SkipHeld( &w )); /* a puzzle, a ladder, an exit hold */
	w.model_cutscene = 1; assert( !CoF_SkipHeld( &w )); /* one runs, but not on them */
	w.camera_model = 1; assert( CoF_SkipHeld( &w ));
	w.animating = 1; assert( !CoF_SkipHeld( &w )); /* a struggle counts presses against the clock */
	watching( 40 ); w.animating = 1; assert( !CoF_SkipHeld( &w ));
	{
		cof_skip_t s; int over;
		memset( &s, 0, sizeof( s ));
		/* nobody asked: one step, however held */
		assert( CoF_SkipSteps( &s, 1, 10.0, 10, &over ) == 1 && !over );
		/* asked: the speed while held, never more than the most */
		s.wanted = 100;
		assert( CoF_SkipSteps( &s, 1, 10.0, 10, &over ) == 10 && !over && s.wanted );
		assert( CoF_SkipSteps( &s, 1, 10.1, 99, &over ) == COF_SKIP_SPEED_MAX );
		assert( CoF_SkipSteps( &s, 1, 10.2, 0, &over ) == 1 );
		/* between two cameras: normal speed, the skip still wanted */
		assert( CoF_SkipSteps( &s, 0, 11.0, 10, &over ) == 1 && !over && s.wanted );
		assert( CoF_SkipSteps( &s, 0, 12.9, 10, &over ) == 1 && !over && s.wanted );
		assert( CoF_SkipSteps( &s, 1, 13.0, 10, &over ) == 10 && s.free_since == 0.0 ); /* the next camera: on again */
		/* nobody held for two seconds: the scene is over, and so is the skip */
		assert( CoF_SkipSteps( &s, 0, 20.0, 10, &over ) == 1 && !over );
		assert( CoF_SkipSteps( &s, 0, 22.0, 10, &over ) == 1 && over && !s.wanted );
		assert( CoF_SkipSteps( &s, 1, 22.1, 10, &over ) == 1 && !over ); /* a later scene is not skipped unasked */
	}

	puts( "scene state scenarios passed" );
	return 0;
}
