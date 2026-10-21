/* The bonus kit at a chapter start (sv_cof_bonus_kit.h): which chapter starts get it, where the party starts in a
 * merged map, and where its items lie, against a synthetic world: solid boxes (floors, walls, crates), water, the
 * triggers kept clear of and a merged map's sections. The probes answer as the engine's do (sv_cof_bonus.c): a line
 * between two points, the floor below a point, a standing player's box, a player's box against a trigger's. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bonus_under_test.h"
#include "bonus_kit_under_test.h"

typedef struct { float mins[3], maxs[3]; } box_t;
static box_t solids[32], water[8], dangers[8];
static int nsolids, nwater, ndangers;
static float band_lo[4], band_hi[4];
static int nbands;

static void reset( void ) { nsolids = nwater = ndangers = nbands = 0; }
static void add( box_t *list, int *n, float x0, float y0, float z0, float x1, float y1, float z1 )
{
	box_t *b = &list[( *n )++];
	b->mins[0] = x0; b->mins[1] = y0; b->mins[2] = z0; b->maxs[0] = x1; b->maxs[1] = y1; b->maxs[2] = z1;
}
static void solid( float x0, float y0, float z0, float x1, float y1, float z1 ) { add( solids, &nsolids, x0, y0, z0, x1, y1, z1 ); }
static void floor_at( float x0, float y0, float x1, float y1, float z ) { solid( x0, y0, z - 16.0f, x1, y1, z ); }

/* a segment against a box (the slab test): it enters the box's inside between its ends */
static int segment_hits( const float *a, const float *b, const box_t *box )
{
	float t0 = 0.0f, t1 = 1.0f;
	int k;
	for( k = 0; k < 3; k++ )
	{
		float d = b[k] - a[k];
		if( d == 0.0f )
		{
			if( a[k] <= box->mins[k] || a[k] >= box->maxs[k] ) return 0;
			continue;
		}
		{
			float u = ( box->mins[k] - a[k] ) / d, v = ( box->maxs[k] - a[k] ) / d;
			if( u > v ) { float w = u; u = v; v = w; }
			if( u > t0 ) t0 = u;
			if( v < t1 ) t1 = v;
			if( t0 >= t1 ) return 0;
		}
	}
	return 1;
}
static int inside( const float *p, const box_t *box )
{
	return p[0] > box->mins[0] && p[0] < box->maxs[0] && p[1] > box->mins[1] && p[1] < box->maxs[1] && p[2] > box->mins[2] && p[2] < box->maxs[2];
}
static int overlaps( const float *lo, const float *hi, const box_t *box )
{
	int k;
	for( k = 0; k < 3; k++ ) if( lo[k] >= box->maxs[k] || hi[k] <= box->mins[k] ) return 0;
	return 1;
}

static int calls_reach;
static int probe_reach( void *ctx, const float *from, const float *to )
{
	int i;
	(void)ctx; calls_reach++;
	for( i = 0; i < nsolids; i++ ) if( segment_hits( from, to, &solids[i] )) return 0;
	return 1;
}
/* the engine traces a point down COF_BONUS_KIT_EYE + 2 * COF_BONUS_KIT_STEP + 32 units */
static int probe_ground( void *ctx, const float *point, float *z )
{
	float best = -1e9f, reach = COF_BONUS_KIT_EYE + 2.0f * COF_BONUS_KIT_STEP + 32.0f;
	int i;
	(void)ctx;
	for( i = 0; i < nsolids; i++ )
	{
		if( inside( point, &solids[i] )) return 0;
		if( point[0] <= solids[i].mins[0] || point[0] >= solids[i].maxs[0] || point[1] <= solids[i].mins[1] || point[1] >= solids[i].maxs[1] ) continue;
		if( solids[i].maxs[2] <= point[2] && solids[i].maxs[2] >= point[2] - reach && solids[i].maxs[2] > best ) best = solids[i].maxs[2];
	}
	if( best < -1e8f ) return 0;
	*z = best;
	return 1;
}
static int probe_stand( void *ctx, const float *feet )
{
	float lo[3], hi[3], centre[3];
	int i;
	(void)ctx;
	lo[0] = feet[0] - 16; lo[1] = feet[1] - 16; lo[2] = feet[2];
	hi[0] = feet[0] + 16; hi[1] = feet[1] + 16; hi[2] = feet[2] + 2 * COF_BONUS_KIT_EYE;
	centre[0] = feet[0]; centre[1] = feet[1]; centre[2] = feet[2] + COF_BONUS_KIT_EYE;
	for( i = 0; i < nsolids; i++ ) if( overlaps( lo, hi, &solids[i] )) return 0;
	for( i = 0; i < nwater; i++ ) if( inside( feet, &water[i] ) || inside( centre, &water[i] )) return 0;
	return 1;
}
static int probe_danger( void *ctx, const float *feet )
{
	int i;
	(void)ctx;
	for( i = 0; i < ndangers; i++ ) if( CoF_BonusKitTouches( feet, dangers[i].mins, dangers[i].maxs )) return 1;
	return 0;
}
static int probe_section( void *ctx, const float *point )
{
	int i, best = 0;
	(void)ctx;
	if( !nbands ) return -1;
	for( i = 0; i < nbands; i++ ) if( point[2] >= band_lo[i] && point[2] < band_hi[i] ) best = i;
	return best;
}
static const cof_bonus_kit_probe_t probe = { NULL, probe_reach, probe_ground, probe_stand, probe_danger, probe_section };

#define MOST 24
static float out[MOST][3];
static int placed[MOST];
static const float ahead_x[3] = { 1.0f, 0.0f, 0.0f }, ahead_y[3] = { 0.0f, 1.0f, 0.0f };

static float dist2( const float *a, const float *b ) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

/* every spot the kit chose passes every rule, whatever the world: on a floor (lifted), a player fits there, out of
 * the dangers and the water, in the start's section, a straight line from the start (or the start's own spot) */
static void every_spot_safe( const float *start, int count )
{
	int i;
	for( i = 0; i < count; i++ )
	{
		float feet[3], floor_z, point[3];
		if( !placed[i] ) continue;
		feet[0] = out[i][0]; feet[1] = out[i][1]; feet[2] = out[i][2] - COF_BONUS_KIT_LIFT + 1.0f;
		point[0] = feet[0]; point[1] = feet[1]; point[2] = start[2];
		assert( probe_ground( NULL, point, &floor_z ) && feet[2] == floor_z + 1.0f );
		assert( probe_stand( NULL, feet ) && !probe_danger( NULL, feet ));
		assert( probe_section( NULL, feet ) == probe_section( NULL, start ));
		assert(( feet[0] == start[0] && feet[1] == start[1] ) || probe_reach( NULL, start, point ));
	}
}

static void spread( int count, int want )
{
	int i, j;
	for( i = 0; i < count; i++ )
		for( j = 0; j < i; j++ )
			if( placed[i] == 1 && placed[j] == 1 && want ) assert( dist2( out[i], out[j] ) >= COF_BONUS_KIT_SPREAD * COF_BONUS_KIT_SPREAD );
}

static void an_open_room( void )
{
	static const float start[3] = { 0.0f, 0.0f, 37.0f };
	int i;
	reset();
	floor_at( -2000, -2000, 2000, 2000, 0.0f );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	every_spot_safe( start, 6 ); spread( 6, 1 );
	for( i = 0; i < 6; i++ )
	{
		assert( placed[i] == 1 );
		assert( out[i][2] == 0.0f + COF_BONUS_KIT_LIFT ); /* laid a little over the floor, to fall onto it */
		assert( dist2( out[i], start ) <= 64.0f * 64.0f + 37.0f * 37.0f + 1.0f ); /* all on the nearest ring */
	}
	/* the first straight ahead of the start, as it faces; the rest out to both sides, the last behind */
	assert( out[0][0] == 64.0f && out[0][1] == 0.0f );
	assert( out[1][1] > 0.0f && out[2][1] < 0.0f && out[5][0] < 0.0f );
	/* facing +y: the first ahead that way */
	assert( CoF_BonusKitPlace( &probe, start, ahead_y, 6, out, placed ) == 6 );
	assert( out[0][0] > -0.01f && out[0][0] < 0.01f && out[0][1] == 64.0f );
	/* four sets (the pickups off) spread out over the next rings */
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 24, out, placed ) == 24 );
	every_spot_safe( start, 24 ); spread( 24, 1 );
	for( i = 0; i < 24; i++ ) assert( placed[i] == 1 );
	/* nothing asked: nothing */
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 0, out, placed ) == 0 );
}

static void walls_triggers_pits_and_water( void )
{
	static const float start[3] = { 0.0f, 0.0f, 37.0f };
	int i;
	/* a wall just ahead: nothing beyond it, nothing whose player would stand in it */
	reset();
	floor_at( -2000, -2000, 2000, 2000, 0.0f );
	solid( 40, -500, 0, 48, 500, 200 );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	every_spot_safe( start, 6 );
	for( i = 0; i < 6; i++ ) assert( out[i][0] <= 40.0f - 16.0f );
	/* a level exit and a hurt to the sides, a crate behind: none lies where a player taking it would touch them */
	reset();
	floor_at( -2000, -2000, 2000, 2000, 0.0f );
	add( dangers, &ndangers, 40, -400, 0, 400, 400, 100 );   /* ahead: the exit */
	add( dangers, &ndangers, -400, 60, 0, 400, 400, 100 );   /* left: a hurt */
	solid( -120, -80, 0, -60, 80, 64 );                       /* behind: a crate */
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	every_spot_safe( start, 6 ); spread( 6, 1 );
	for( i = 0; i < 6; i++ )
	{
		float feet[3];
		feet[0] = out[i][0]; feet[1] = out[i][1]; feet[2] = 1.0f;
		assert( !CoF_BonusKitTouches( feet, dangers[0].mins, dangers[0].maxs ) && !CoF_BonusKitTouches( feet, dangers[1].mins, dangers[1].maxs ));
		assert( feet[0] + 32.0f < 40.0f && feet[1] + 32.0f < 60.0f );
	}
	/* a pit ahead (no floor within reach), a step down within a step, a drop of more than a step, water to the right */
	reset();
	floor_at( -2000, -2000, 50, 2000, 0.0f );                 /* the start's floor ends ahead... */
	floor_at( 50, -2000, 2000, -100, -400.0f );               /* ...a pit to the right of the way ahead */
	floor_at( 50, -100, 2000, 100, -20.0f );                  /* a step down straight ahead */
	floor_at( 50, 100, 2000, 2000, -60.0f );                  /* a drop to the left */
	add( water, &nwater, -2000, -2000, -100, 2000, -40, 100 ); /* the right half under water */
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	every_spot_safe( start, 6 );
	for( i = 0; i < 6; i++ )
	{
		float z = out[i][2] - COF_BONUS_KIT_LIFT;
		assert( z == 0.0f || z == -20.0f );                  /* the start's floor or the step, never the pit or the drop */
		assert( out[i][1] >= -40.0f );                         /* out of the water */
	}
	/* a gap of no floor at all straight ahead: the start's floor only */
	reset();
	floor_at( -2000, -2000, 30, 2000, 0.0f );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	for( i = 0; i < 6; i++ ) assert( out[i][0] < 30.0f );
}

static void tight_places( void )
{
	static const float start[3] = { 0.0f, 0.0f, 37.0f };
	int i, close = 0;
	/* a corridor a player wide: the spots ahead and behind, then the free ones, then closer */
	reset();
	floor_at( -2000, -2000, 2000, 2000, 0.0f );
	solid( -2000, 17, 0, 2000, 40, 200 ); solid( -2000, -40, 0, 2000, -17, 200 );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 24, out, placed ) == 24 );
	every_spot_safe( start, 24 );
	for( i = 0; i < 24; i++ ) { assert( out[i][1] == 0.0f ); if( placed[i] == 2 ) close++; }
	assert( close == 24 - 13 ); /* six spots each way and the start's own, all 32 apart; the others on them */
	/* a closet: the start's own spot for everything */
	reset();
	floor_at( -2000, -2000, 2000, 2000, 0.0f );
	solid( 17, -40, 0, 40, 40, 200 ); solid( -40, -40, 0, -17, 40, 200 ); solid( -40, 17, 0, 40, 40, 200 ); solid( -40, -40, 0, 40, -17, 200 );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 6 );
	every_spot_safe( start, 6 );
	assert( placed[0] == 1 );
	for( i = 0; i < 6; i++ ) assert( out[i][0] == 0.0f && out[i][1] == 0.0f && out[i][2] == COF_BONUS_KIT_LIFT );
	for( i = 1; i < 6; i++ ) assert( placed[i] == 2 );
	/* ...and the closet's floor a hurt: nowhere safe, nothing laid out */
	add( dangers, &ndangers, -40, -40, -20, 40, 40, 4 );
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 6, out, placed ) == 0 );
	for( i = 0; i < 6; i++ ) assert( !placed[i] );
}

static void merged_sections( void )
{
	/* a merged map: section 1 (the chapter's) from z 995 up, section 0 below; the start in section 1 */
	static const float start[3] = { 0.0f, 0.0f, 1037.0f };
	static const int sections[] = { 0, 0, 1, 1 };
	int i;
	reset();
	nbands = 2; band_lo[0] = -5000; band_hi[0] = 995; band_lo[1] = 995; band_hi[1] = 5000;
	floor_at( -2000, -2000, 2000, 2000, -1000.0f );           /* the other map's floor far below */
	floor_at( -2000, -2000, 100, 2000, 1000.0f );             /* the chapter's room */
	floor_at( 100, -2000, 2000, 2000, 980.0f );               /* a step down that is in the other section's band */
	assert( CoF_BonusKitPlace( &probe, start, ahead_x, 24, out, placed ) == 24 );
	every_spot_safe( start, 24 );
	for( i = 0; i < 24; i++ ) assert( out[i][2] - COF_BONUS_KIT_LIFT == 1000.0f && probe_section( NULL, out[i] ) == 1 );
	/* where the party starts: the first info_player_start of the chapter's section; not merged: the first */
	assert( CoF_BonusKitStart( sections, 4, 1 ) == 2 && CoF_BonusKitStart( sections, 4, 0 ) == 0 );
	assert( CoF_BonusKitStart( sections, 4, -1 ) == 0 && CoF_BonusKitStart( sections, 4, 3 ) == 0 );
	assert( CoF_BonusKitStart( sections, 0, 1 ) == -1 );
}

static void the_table( void )
{
	static const char *chapters[] = { "c_basement", "c_sewer3", "c_park3", "c_subwaysick2", "c_attic", "c_ending1b", "c_ending2b",
		"c_ending3b", "c_ending1", "c_ending2", "c_ending3", "c_ending4", "c_ending5" };
	int i, items = 0;
	for( i = 0; i < (int)( sizeof( chapters ) / sizeof( chapters[0] )); i++ ) assert( CoF_BonusKitWanted( chapters[i] ));
	assert( !CoF_BonusKitWanted( "c_bridge" ) && !CoF_BonusKitWanted( "C_BRIDGE" ) && !CoF_BonusKitWanted( "c_start" ));
	assert( !CoF_BonusKitWanted( "c_nightmare" ) && !CoF_BonusKitWanted( "" ) && !CoF_BonusKitWanted( NULL ));
	/* the room's items but its key; the unlock each checks lying (FAMAS 0x40, axe 0x80000, camera 0x100000, book laser
	 * 0x200000, night vision 0x1000000: lines 39, 70, 71, 72, 75); the MP5's rule is the donator level's */
	assert( CoF_BonusKitItem( "weapon_famas" ) == 0 && CoF_BonusKitItem( "weapon_mp5" ) == 5 && CoF_BonusKitItem( "item_key" ) == -1 );
	assert( CoF_BonusKitItem( NULL ) == -1 && CoF_BonusKitItem( "weapon_glock" ) == -1 );
	assert( cof_bonus_kit[0].gate == 39 && cof_bonus_kit[1].gate == 70 && cof_bonus_kit[2].gate == 72 && cof_bonus_kit[3].gate == 71 );
	assert( cof_bonus_kit[4].gate == 75 && cof_bonus_kit[5].gate == 0 );
	for( i = 0; i < COF_BONUS_KIT_ITEMS; i++ )
	{
		int k;
		if( cof_bonus_kit[i].gate ) assert( CoF_BonusLine( cof_bonus_kit[i].gate ) != NULL );
		for( k = 0; k < COF_BONUS_KIT_KEYS && cof_bonus_kit[i].keys[k]; k++ ) assert( strcmp( cof_bonus_kit[i].keys[k], "origin" ) && strcmp( cof_bonus_kit[i].keys[k], "angles" ));
		assert( k < COF_BONUS_KIT_KEYS && k % 2 == 0 );
		items += cof_bonus_kit[i].item;
	}
	assert( items == 1 && cof_bonus_kit[4].item && !strcmp( cof_bonus_kit[4].classname, "item_nightvision" ));
	/* the triggers kept clear of */
	assert( CoF_BonusKitDanger( "trigger_changelevel", 0 ) && CoF_BonusKitDanger( "trigger_hurt", 0 ));
	assert( CoF_BonusKitDanger( "trigger_teleport", 0 ) && CoF_BonusKitDanger( "trigger_push", 0 ));
	assert( !CoF_BonusKitDanger( "trigger_multiple", 0 ) && CoF_BonusKitDanger( "trigger_multiple", 1 ) && CoF_BonusKitDanger( "trigger_once", 1 ));
	assert( !CoF_BonusKitDanger( "trigger_once", 0 ) && !CoF_BonusKitDanger( "func_door", 1 ) && !CoF_BonusKitDanger( NULL, 1 ));
	/* a player's box padded by the walk up: touching, just clear */
	{
		static const float lo[3] = { 100, -50, 0 }, hi[3] = { 200, 50, 100 };
		float feet[3] = { 100 - 32, 0, 1 };
		assert( CoF_BonusKitTouches( feet, lo, hi ));
		feet[0] = 100 - 32.5f; assert( !CoF_BonusKitTouches( feet, lo, hi ));
		feet[0] = 150; feet[2] = 101.5f; assert( !CoF_BonusKitTouches( feet, lo, hi ));
		feet[2] = 100; assert( CoF_BonusKitTouches( feet, lo, hi ));
		feet[2] = -73; assert( CoF_BonusKitTouches( feet, lo, hi ));
		feet[2] = -73.5f; assert( !CoF_BonusKitTouches( feet, lo, hi ));
	}
}

int main( void )
{
	an_open_room();
	walls_triggers_pits_and_water();
	tight_places();
	merged_sections();
	the_table();
	assert( calls_reach > 0 );
	puts( "bonus kit placement scenarios passed" );
	return 0;
}
