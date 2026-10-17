/* The AI companion's mind for Cry of Fear story co-op: what an AI-driven
 * player does this frame. Pure state and maths, no engine types, so the
 * offline harness drives it directly. GPL-3.0-or-later.
 *
 * The companion is an ordinary player entity (an engine fake client, or an
 * idle second client) that the engine drives only through usercmds and the
 * game's own player code, so everything it does goes through the retail
 * rules: movement, weapons, CPR, the syringe. The engine fills a perception
 * (where everyone is, what it can see and carry); this decides, in priority:
 *   1. dead: press jump so the game respawns it, unless someone revives it;
 *   2. grabbed by a monster that turns its gun on it: press attack over and
 *      over (the game's suicide struggle), or it shoots itself;
 *   3. reviving the leader: keep +use held while the game's CPR runs;
 *   4. nothing while held by the game (cutscene, ladder, door);
 *   5. the leader is down: go to them and start CPR;
 *   6. fight the nearest threat: a gun with rounds (sights up for a far
 *      target, one press per shot, reload when empty), else the knife;
 *   7. hurt and safe: inject a syringe;
 *   8. the leader is hurt and it is safe: walk up and give them a syringe;
 *   9. follow the leader along the path the leader walked (a breadcrumb
 *      trail: the leader's own path is walkable), with slack (it sets off
 *      beyond COF_BRAIN_START and stops inside COF_BRAIN_STOP). Wherever it is
 *      going (the leader, a trail point, its post, a downed leader) it says
 *      so (out->goal); when the straight way there is blocked the engine finds
 *      a way round on the map's route graph (sv_cof_nav.h) and hands it the
 *      next point (in->nav). A leader out of sight with no trail is followed
 *      only along such a route. On the way it opens a door the engine finds
 *      right ahead, hops a low obstacle the engine says a jump clears (never a
 *      blind hop) and steps sideways to line up with an opening the engine
 *      probed (in->side_shift: corners and door jambs). When the game moves the
 *      leader (an inter_door, a script) the trail is kept: it walks to where
 *      they left and comes out after them (out->place). Stuck (no progress, not
 *      merely no motion) on a route, it asks for another first (out->replan); it
 *      teleports out of the player's sight when lost or stuck, never during a
 *      cutscene, and says why (out->why, for the log). Several bots behind one
 *      person keep their own distance (in->rank), and a partner in the way
 *      counts as arrived;
 *  10. otherwise stand by them, looking where they look, gun in hand; out of
 *      their body and out of their face.
 * Told to wait, it holds its spot instead of following (still fighting what
 * comes near, still reviving the leader wherever they fall, and giving them a
 * syringe when they come to it).
 * Weapon facts: docs/research/2026-10-12-companion-weapons-items-re.md. */
#ifndef SV_COF_BRAIN_H
#define SV_COF_BRAIN_H
#include <math.h>
#include <string.h>

#define COF_BRAIN_TRAIL 64
#define COF_BRAIN_CRUMB 48.0f // a trail point every this far the leader walks
#define COF_BRAIN_JUMP 200.0f // a leader move this long between samples is a teleport (door, ladder, script)
#define COF_BRAIN_REACHED 32.0f
#define COF_BRAIN_START 128.0f // farther from the leader than this: set off after them...
#define COF_BRAIN_STOP 80.0f // ...and walk until this close (one threshold made it start and stop every frame)
#define COF_BRAIN_RUN 260.0f // farther than this: run
#define COF_BRAIN_FAR 900.0f // farther than this: teleport
#define COF_BRAIN_LOST 6.0 // leader out of sight and over 300 units away this long: teleport...
#define COF_BRAIN_LOST_ROUTE 15.0 // ...or this long while a route leads on
#define COF_BRAIN_STUCK 2.5 // trying to move but not moving this long: teleport
#define COF_BRAIN_TELEPORT_COOLDOWN 3.0
#define COF_BRAIN_REVIVE_REACH 56.0f
#define COF_BRAIN_DANGER 250.0f // a threat this close comes before reviving
#define COF_BRAIN_MELEE_REACH 30.0f // melee weapons reach 32-40 units from the eye: swing from this close to the body
#define COF_BRAIN_BACK_OFF 100.0f // a gun this close to a body: step back while shooting
#define COF_BRAIN_SIGHTS 200.0f // farther than this, raise the sights
#define COF_BRAIN_AIM_TOLERANCE 6.0f // degrees
#define COF_BRAIN_TURN_RATE 540.0f // degrees a second
#define COF_BRAIN_HEAL_BELOW 35
#define COF_BRAIN_MEDIC_BELOW 50 // the leader at or under this health: give them a syringe
#define COF_BRAIN_MEDIC_REACH 40.0f // the syringe reaches 48 units from the eye
#define COF_BRAIN_WALK 150.0f
#define COF_BRAIN_RUN_SPEED 300.0f
#define COF_BRAIN_WARP_STEP 64.0f // the leader moved this far in one frame...
#define COF_BRAIN_WARP_SPEED 1000.0f // ...faster than anyone walks: the game moved them (a door, a script)
#define COF_BRAIN_DOOR_REACH 40.0f // this close to where the leader left: come out after them
#define COF_BRAIN_DOOR_HEIGHT 48.0f
#define COF_BRAIN_DOOR_WAIT 10.0 // no way to that spot in this long: the ordinary teleport
#define COF_BRAIN_SETTLE 0.5 // after a cutscene no teleport this long: the script's own moves land first
#define COF_BRAIN_PROGRESS 16.0f // stuck: not this much nearer the goal (along the trail) within COF_BRAIN_STUCK
#define COF_BRAIN_CLIMB 40.0f // a trail point this far above or below is not reached by standing under it
#define COF_BRAIN_PROBE_TOLERANCE 15.0f // the engine's side probe was made for this heading, give or take
#define COF_BRAIN_PROBE_AHEAD 6.0f // stepping sideways to an opening, this much forward
#define COF_BRAIN_PERSONAL 40.0f // closer to the leader (or a partner) than this: step out...
#define COF_BRAIN_PERSONAL_OUT 64.0f // ...until this far
#define COF_BRAIN_FACE 96.0f // this close in front of the leader...
#define COF_BRAIN_FACE_ANGLE 25.0f // ...within this angle of where they look...
#define COF_BRAIN_FACE_TIME 1.0 // ...this long: step aside...
#define COF_BRAIN_FACE_CLEAR 35.0f // ...until this angle
#define COF_BRAIN_RANK_GAP 48.0f // each further bot behind the same person keeps this much more distance

// usercmd buttons (HLSDK in_buttons.h; the Cry of Fear meaning in the comment)
#define COF_IN_ATTACK ( 1 << 0 ) // fire, swing, inject
#define COF_IN_JUMP ( 1 << 1 )
#define COF_IN_DUCK ( 1 << 2 )
#define COF_IN_USE ( 1 << 5 )
#define COF_IN_CANCEL ( 1 << 6 ) // +attack3: sights (one press toggles them)
#define COF_IN_ATTACK2 ( 1 << 11 ) // a bash with a gun; the syringe on a partner
#define COF_IN_RELOAD ( 1 << 13 )
// Buttons the game reads as a press: a driven person's client gets each for one usercmd, then a release.
#define COF_BRAIN_TAPS ( COF_IN_ATTACK | COF_IN_CANCEL | COF_IN_ATTACK2 | COF_IN_RELOAD )

enum { COF_BRAIN_IDLE, COF_BRAIN_FOLLOW, COF_BRAIN_FIGHT, COF_BRAIN_REVIVE, COF_BRAIN_HEAL, COF_BRAIN_HELD, COF_BRAIN_DEAD, COF_BRAIN_MEDIC, COF_BRAIN_STRUGGLE };
enum { COF_WEAPON_NONE, COF_WEAPON_MELEE, COF_WEAPON_GUN, COF_WEAPON_HEAL };
// What the engine must do besides the usercmd.
enum
{
	COF_BRAIN_ACT_NONE,
	COF_BRAIN_ACT_TELEPORT, // place me out of sight behind the leader (or by their body when reviving)
	COF_BRAIN_ACT_EQUIP_GUN,
	COF_BRAIN_ACT_EQUIP_MELEE,
	COF_BRAIN_ACT_EQUIP_HEAL, // the syringe in hand
	COF_BRAIN_ACT_REVIVE, // start the game's CPR on the leader's body
	COF_BRAIN_ACT_OPEN_DOOR // open the door the engine found right ahead
};

typedef struct
{
	double now;
	float frametime;
	// the companion
	float self[3], eye[3], view[3]; // origin, eye, current view angles (pitch down positive, yaw, roll)
	int alive, held, onground, health;
	int revived; // dead, and someone is reviving me: do not respawn
	int cpr; // I am reviving someone: the game's CPR runs while +use stays down
	int struggle; // a monster turned my gun on me: every fresh press of attack fights it
	int weapon; // COF_WEAPON_*: what it holds now
	int ready; // the held weapon can act now (not switching, not between shots)
	int sights; // the held gun is aimed down its sights
	int reloading;
	int clip, reserve; // the held gun: rounds loaded and in reserve
	int has_gun, has_melee, has_heal; // carries a gun with rounds (loaded or in reserve), a melee weapon, a syringe
	// the leader
	int leader; // a leader exists
	float leader_pos[3], leader_eye[3], leader_view[3];
	int leader_alive, leader_down; // down: dead and ready for CPR
	int leader_health;
	int leader_visible; // a clear line from my eye to theirs
	int leader_walkable; // a clear hull path straight to them
	// the nearest threat the engine chose (alive, hostile, visible, near the leader)
	int threat;
	float threat_pos[3], threat_dist; // the body's centre, and how far it is from my eye
	float threat_gap; // how far my eye is from the body's box (what a swing must reach)
	int line_clear; // the leader is not in the line of fire
	// told to wait here
	int stay;
	float stay_pos[3];
	float medic; // seconds between syringes for the leader (0: never)
	// just ahead of the feet, the way it walked: a door it may open, or a low
	// obstacle a jump clears (a stair step is not one: the walk climbs it)
	int door_ahead, ledge_ahead;
	// the straight way to last frame's goal is blocked and the route graph
	// knows a way round: the next point on it (a standing body's origin there)
	int nav;
	float nav_point[3];
	// the leader is held by the game (a cutscene, a camera, a door)
	int leader_held;
	// the engine's probe along last frame's way (out->move_to): blocked ahead, but this many
	// units to the left (+) or right (-) the way is clear; made for heading probe_yaw
	float side_shift, probe_yaw;
	int rank; // other bots ahead of it behind the same person (0: the first)
	int partner_block; // another player stands right in its way to the goal...
	float partner_pace; // ...or walks on that way at this speed (0: stands): then it keeps that pace
	int crowd; // another player (not the leader) is closer than COF_BRAIN_PERSONAL...
	float crowd_pos[3]; // ...here
} cof_brain_input_t;

typedef struct
{
	float angles[3]; // view angles to send
	float forward, side, up; // usercmd moves
	int buttons;
	int action; // COF_BRAIN_ACT_*
	int mode; // COF_BRAIN_* for the log
	const char *why; // why the teleport, the door or the jump, for the log (NULL otherwise)
	int going; // on the way to goal (the engine looks for a route there when the straight way is blocked)
	float goal[3];
	int replan; // stuck on the route: leave out the leg it was on and find another
	float move_to[3]; // the point it walked at this frame (the engine probes that way next frame)
	int place; // with the teleport: come out near place_at (where the game put the leader), not behind them
	float place_at[3];
	int aiming; // far enough to follow: aim is the point it would walk to (set off for or not), where
	float aim[3]; // the engine looks for a partner in its way next frame (in->partner_block)
} cof_brain_output_t;

typedef struct
{
	float trail[COF_BRAIN_TRAIL][3];
	int head, count; // ring: oldest at head - count
	int cursor; // trail points walked so far (0..count)
	float last_sample[3];
	int have_sample, broken; // the leader teleported since: the trail ends where the jump began
	double stuck_since, lost_since, teleport_after, fire_after, use_after, jump_after, sights_after;
	double heal_after, heal_since, heal_until; // a syringe for myself: next allowed, trying since, injecting until
	double medic_after, medic_since, medic_check; // a syringe for the leader: next allowed, trying since, see whether it took
	int medic_health; // the leader's health when the syringe went in
	float stuck_origin[3];
	int mode;
	int fire_held; // the attack button was down last frame (semi-automatic guns need a release)
	int mash; // the struggle: attack on every other frame
	int following; // on the way to the leader (the slack between START and STOP)
	double door_after; // next try at a door
	int door_grace; // the stuck time restarted for a door since the body last moved
	int replanned; // asked for another route since the body last moved
	float leader_last[3]; // the leader last frame (a jump faster than walking is the game moving them)
	int have_leader_last;
	int door; // where the game took the leader: trail point door - 1 (0: none)
	float door_to[3]; // and where it put them
	double door_since;
	double held_until; // a cutscene (its or the leader's) ended: no teleport before
	float stuck_progress; // how far along it was when the stuck time last started
	int dropped; // trail points fallen off the ring (progress counts them)
	double face_since; // in front of the leader's eyes since
	int stepping; // stepping aside out of the leader's view
} cof_brain_t;

static void CoF_BrainReset( cof_brain_t *b )
{
	memset( b, 0, sizeof( *b ));
}

static float CoF_BrainDist2D( const float *a, const float *b )
{
	float x = a[0] - b[0], y = a[1] - b[1];
	return sqrtf( x * x + y * y );
}

static float CoF_BrainDist( const float *a, const float *b )
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return sqrtf( x * x + y * y + z * z );
}

static float CoF_BrainNormalize( float angle )
{
	while( angle > 180.0f ) angle -= 360.0f;
	while( angle < -180.0f ) angle += 360.0f;
	return angle;
}

// View angles that look from eye to point (GoldSrc: pitch positive looks down).
static void CoF_BrainLookAt( const float *eye, const float *point, float *angles )
{
	float x = point[0] - eye[0], y = point[1] - eye[1], z = point[2] - eye[2];
	float flat = sqrtf( x * x + y * y );
	angles[0] = flat > 0.001f || fabsf( z ) > 0.001f ? -atan2f( z, flat ) * 57.2957795f : 0.0f;
	angles[1] = flat > 0.001f ? atan2f( y, x ) * 57.2957795f : 0.0f;
	angles[2] = 0.0f;
}

// Turn from current toward wanted at most rate degrees this frame.
static void CoF_BrainTurn( const float *current, const float *wanted, float rate, float *out )
{
	int i;
	for( i = 0; i < 2; i++ )
	{
		float delta = CoF_BrainNormalize( wanted[i] - current[i] );
		if( delta > rate ) delta = rate;
		if( delta < -rate ) delta = -rate;
		out[i] = CoF_BrainNormalize( current[i] + delta );
	}
	if( out[0] > 89.0f ) out[0] = 89.0f;
	if( out[0] < -89.0f ) out[0] = -89.0f;
	out[2] = 0.0f;
}

static float CoF_BrainAimError( const float *current, const float *wanted )
{
	float pitch = fabsf( CoF_BrainNormalize( wanted[0] - current[0] ));
	float yaw = fabsf( CoF_BrainNormalize( wanted[1] - current[1] ));
	return pitch > yaw ? pitch : yaw;
}

// Turn toward a point this frame; the wanted angles are left in wanted.
static void CoF_BrainFace( const cof_brain_input_t *in, const float *point, float *wanted, cof_brain_output_t *out )
{
	CoF_BrainLookAt( in->eye, point, wanted );
	CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * in->frametime, out->angles );
}

// usercmd moves that walk toward point at speed while looking along yaw.
static void CoF_BrainMoveToward( const float *self, const float *point, float yaw, float speed, cof_brain_output_t *out )
{
	float x = point[0] - self[0], y = point[1] - self[1], length = sqrtf( x * x + y * y );
	float c = cosf( yaw / 57.2957795f ), s = sinf( yaw / 57.2957795f );
	if( length < 0.001f ) return;
	x /= length; y /= length;
	out->forward = speed * ( x * c + y * s ); // forward = ( cos yaw, sin yaw )
	out->side = speed * ( x * s - y * c ); // right = ( sin yaw, -cos yaw )
}

// The trail point index k (0 = oldest still kept).
static const float *CoF_BrainCrumb( const cof_brain_t *b, int k )
{
	return b->trail[( b->head - b->count + k + COF_BRAIN_TRAIL * 2 ) % COF_BRAIN_TRAIL];
}

static void CoF_BrainPushCrumb( cof_brain_t *b, const float *pos )
{
	memcpy( b->trail[b->head], pos, sizeof( b->trail[b->head] ));
	b->head = ( b->head + 1 ) % COF_BRAIN_TRAIL;
	if( b->count < COF_BRAIN_TRAIL ) { b->count++; return; }
	// the oldest point fell off
	b->dropped++;
	if( b->cursor > 0 ) b->cursor--;
	if( b->door > 0 && --b->door == 0 ) b->broken = 1; // the way to the door is gone
}

// The leader's position this frame: a breadcrumb every COF_BRAIN_CRUMB units.
// When the game moves them (a door, a script: farther in a frame than anyone
// walks) the trail is kept: the spot they left becomes its last point, the door.
static void CoF_BrainNoteLeader( cof_brain_t *b, const cof_brain_input_t *in )
{
	const float *pos = in->leader_pos;
	float moved;
	int warp = 0;
	if( b->have_leader_last )
	{
		float step = CoF_BrainDist2D( pos, b->leader_last ) + fabsf( pos[2] - b->leader_last[2] );
		warp = step > COF_BRAIN_WARP_STEP && step > COF_BRAIN_WARP_SPEED * ( in->frametime > 0.001f ? in->frametime : 0.001f );
	}
	if( warp || ( b->have_sample && CoF_BrainDist( pos, b->last_sample ) > COF_BRAIN_JUMP ))
	{
		const float *from = warp ? b->leader_last : b->last_sample;
		if( !b->count || CoF_BrainDist( CoF_BrainCrumb( b, b->count - 1 ), from ) > 16.0f ) CoF_BrainPushCrumb( b, from );
		b->door = b->count;
		memcpy( b->door_to, pos, sizeof( b->door_to ));
		b->door_since = in->now;
		memcpy( b->last_sample, pos, sizeof( b->last_sample ));
		b->have_sample = 1;
	}
	else if( !b->have_sample )
	{
		memcpy( b->last_sample, pos, sizeof( b->last_sample ));
		b->have_sample = 1;
	}
	else if(( moved = CoF_BrainDist( pos, b->last_sample )) >= COF_BRAIN_CRUMB )
	{
		CoF_BrainPushCrumb( b, pos );
		memcpy( b->last_sample, pos, sizeof( b->last_sample ));
	}
	memcpy( b->leader_last, pos, sizeof( b->leader_last ));
	b->have_leader_last = 1;
}

// A trail point counts as reached this close, at about the same height.
static int CoF_BrainReached( const cof_brain_input_t *in, const float *point )
{
	return CoF_BrainDist2D( in->self, point ) < COF_BRAIN_REACHED && fabsf( point[2] - in->self[2] ) < COF_BRAIN_CLIMB;
}

// Where to walk to follow: the leader when the way is clear, else the next
// trail point, else the leader in sight: 1. With nothing of those the leader
// still: 2 (walked to only along a route the engine finds).
static int CoF_BrainFollowPoint( cof_brain_t *b, const cof_brain_input_t *in, float *point )
{
	int last = b->door ? b->door - 1 : b->count; // never past the door: the trail beyond it is the far side
	if( in->leader_walkable && in->leader_visible )
	{
		// The way is open: the trail behind us no longer matters.
		b->cursor = b->count;
		b->door = 0;
		memcpy( point, in->leader_pos, sizeof( float ) * 3 );
		return 1;
	}
	while( b->cursor < last && CoF_BrainReached( in, CoF_BrainCrumb( b, b->cursor ))) b->cursor++;
	// Skip ahead to the newest point we are already close to (the leader doubled back).
	{
		int k;
		for( k = last - 1; k > b->cursor; k-- )
			if( CoF_BrainReached( in, CoF_BrainCrumb( b, k ))) { b->cursor = k + 1; break; }
	}
	if( b->door )
	{
		memcpy( point, CoF_BrainCrumb( b, b->cursor < last ? b->cursor : last ), sizeof( float ) * 3 );
		return 1;
	}
	if( b->cursor < b->count )
	{
		memcpy( point, CoF_BrainCrumb( b, b->cursor ), sizeof( float ) * 3 );
		return 1;
	}
	memcpy( point, in->leader_pos, sizeof( float ) * 3 );
	return in->leader_visible ? 1 : 2;
}

// Head for target: straight at it, or at the next point of the route the
// engine found round whatever is in the way; looking where it walks.
static void CoF_BrainGo( const cof_brain_input_t *in, const float *target, float speed, cof_brain_output_t *out )
{
	const float *point = in->nav ? in->nav_point : target;
	float wanted[3], walk[3];
	memcpy( out->goal, target, sizeof( out->goal ));
	out->going = 1;
	memcpy( walk, point, sizeof( walk ));
	if( in->side_shift != 0.0f )
	{
		// The engine found the way blocked ahead and an opening to one side (a door jamb,
		// a corner): step over to line up with it, a little forward.
		float heading = atan2f( point[1] - in->self[1], point[0] - in->self[0] ) * 57.2957795f;
		if( fabsf( CoF_BrainNormalize( heading - in->probe_yaw )) <= COF_BRAIN_PROBE_TOLERANCE )
		{
			float c = cosf( heading / 57.2957795f ), s = sinf( heading / 57.2957795f );
			walk[0] = in->self[0] - s * in->side_shift + c * COF_BRAIN_PROBE_AHEAD;
			walk[1] = in->self[1] + c * in->side_shift + s * COF_BRAIN_PROBE_AHEAD;
			walk[2] = in->self[2];
		}
	}
	memcpy( out->move_to, walk, sizeof( out->move_to ));
	CoF_BrainLookAt( in->eye, point, wanted );
	wanted[0] = 0.0f;
	CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * in->frametime, out->angles );
	CoF_BrainMoveToward( in->self, walk, out->angles[1], speed, out );
}

// why: "far", "lost", "stuck", "trail" (the leader went through a door or was
// moved by a script), "revive" (to the leader's body).
static int CoF_BrainTeleport( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out, const char *why )
{
	if( in->now < b->teleport_after ) return 0;
	b->teleport_after = in->now + COF_BRAIN_TELEPORT_COOLDOWN;
	b->stuck_since = b->lost_since = 0.0;
	b->broken = 0;
	b->count = b->cursor = b->door = 0;
	out->action = COF_BRAIN_ACT_TELEPORT;
	out->why = why;
	return 1;
}

// Through the door after the leader: come out where the game put them.
static int CoF_BrainDoorTeleport( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float to[3];
	memcpy( to, b->door_to, sizeof( to ));
	if( !CoF_BrainTeleport( b, in, out, "door" )) return 0;
	out->place = 1;
	memcpy( out->place_at, to, sizeof( out->place_at ));
	return 1;
}

// Moving with intent toward target: notice when it gets nowhere. Progress is how
// far along the trail it is (points passed, those fallen off the ring included)
// less how far the next point still is, so jittering back and forth is no
// progress. On a route the planner takes its own detours: there, any motion counts.
static int CoF_BrainStuck( cof_brain_t *b, const cof_brain_input_t *in, int moving, const float *target )
{
	float progress = (float)( b->dropped + b->cursor ) * COF_BRAIN_CRUMB - CoF_BrainDist2D( in->self, target );
	int restart;
	if( !moving )
	{
		b->stuck_since = 0.0;
		return 0;
	}
	if( in->nav ) restart = CoF_BrainDist( in->self, b->stuck_origin ) > 24.0f;
	else restart = progress > b->stuck_progress + COF_BRAIN_PROGRESS;
	if( b->stuck_since == 0.0 || restart )
	{
		b->stuck_since = in->now;
		b->stuck_progress = progress;
		b->door_grace = b->replanned = 0;
		memcpy( b->stuck_origin, in->self, sizeof( b->stuck_origin ));
		return 0;
	}
	return in->now - b->stuck_since > COF_BRAIN_STUCK;
}

// On the way somewhere: open a door the engine found right ahead (once a second;
// the first try since the body last moved restarts the stuck time, so the door
// has time to swing, and a locked one still ends in a teleport), or hop a low
// obstacle the engine says a jump clears. Never a blind hop: bumping a wall, a
// door frame or a person is no reason to jump.
static void CoF_BrainObstacle( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	if( in->door_ahead && out->action == COF_BRAIN_ACT_NONE && in->now >= b->door_after )
	{
		b->door_after = in->now + 1.0;
		if( !b->door_grace && b->stuck_since != 0.0 )
		{
			b->stuck_since = in->now;
			b->door_grace = 1;
		}
		out->action = COF_BRAIN_ACT_OPEN_DOOR;
		out->why = "door";
	}
	else if( in->ledge_ahead && !in->door_ahead && in->onground && in->now >= b->jump_after )
	{
		b->jump_after = in->now + 1.0;
		out->buttons |= COF_IN_JUMP;
		out->why = "ledge";
	}
}

// The weapon to hold: a gun with rounds (switching costs a second or more each
// time, so it stays out even up close), else the knife.
static int CoF_BrainWeapon( const cof_brain_input_t *in )
{
	if( in->has_gun ) return COF_WEAPON_GUN;
	return in->has_melee ? COF_WEAPON_MELEE : COF_WEAPON_NONE;
}

static void CoF_BrainFight( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float wanted[3];
	int want = CoF_BrainWeapon( in );
	out->mode = COF_BRAIN_FIGHT;
	CoF_BrainFace( in, in->threat_pos, wanted, out );
	if( want != COF_WEAPON_NONE && in->weapon != want )
	{
		out->action = want == COF_WEAPON_GUN ? COF_BRAIN_ACT_EQUIP_GUN : COF_BRAIN_ACT_EQUIP_MELEE;
		return;
	}
	if( in->weapon == COF_WEAPON_GUN )
	{
		// Too close for comfort: step back while shooting.
		if( in->threat_gap < COF_BRAIN_BACK_OFF )
		{
			float away[3];
			away[0] = in->self[0] * 2.0f - in->threat_pos[0];
			away[1] = in->self[1] * 2.0f - in->threat_pos[1];
			away[2] = in->self[2];
			CoF_BrainMoveToward( in->self, away, out->angles[1], COF_BRAIN_WALK, out );
		}
		if( in->reloading ) return;
		if( in->clip <= 0 )
		{
			// one press starts the reload
			if( in->reserve > 0 && in->ready && in->now >= b->use_after )
			{
				out->buttons |= COF_IN_RELOAD;
				b->use_after = in->now + 1.0;
			}
			return;
		}
		// A far target: sights up first (a press toggles them).
		if( !in->sights && in->ready && in->threat_gap > COF_BRAIN_SIGHTS && in->now >= b->sights_after )
		{
			out->buttons |= COF_IN_CANCEL;
			b->sights_after = in->now + 1.0;
			return;
		}
		// The guns fire once per press: the trigger goes up again the next frame.
		if( CoF_BrainAimError( out->angles, wanted ) <= COF_BRAIN_AIM_TOLERANCE && in->line_clear && in->ready &&
			!b->fire_held && in->now >= b->fire_after )
		{
			out->buttons |= COF_IN_ATTACK;
			b->fire_after = in->now + 0.2;
		}
	}
	else if( in->weapon == COF_WEAPON_MELEE )
	{
		// Melee swings again for as long as the button stays down.
		if( in->threat_gap > COF_BRAIN_MELEE_REACH )
			CoF_BrainMoveToward( in->self, in->threat_pos, out->angles[1], COF_BRAIN_RUN_SPEED, out );
		if( in->threat_gap <= COF_BRAIN_MELEE_REACH + 8.0f && in->line_clear &&
			CoF_BrainAimError( out->angles, wanted ) <= COF_BRAIN_AIM_TOLERANCE * 3.0f )
			out->buttons |= COF_IN_ATTACK;
	}
}

// Hurt and nothing in sight: the syringe in hand, one press, then stand still
// while it goes in. True while busy with it.
static int CoF_BrainHeal( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	if( in->now < b->heal_until ) { out->mode = COF_BRAIN_HEAL; return 1; }
	if( !in->has_heal || in->health >= COF_BRAIN_HEAL_BELOW || in->now < b->heal_after ) { b->heal_since = 0.0; return 0; }
	if( b->heal_since == 0.0 ) b->heal_since = in->now;
	if( in->now - b->heal_since > 6.0 ) { b->heal_after = in->now + 30.0; b->heal_since = 0.0; return 0; } // the game would not have it
	out->mode = COF_BRAIN_HEAL;
	if( in->weapon != COF_WEAPON_HEAL ) out->action = COF_BRAIN_ACT_EQUIP_HEAL;
	else if( in->ready )
	{
		out->buttons |= COF_IN_ATTACK;
		b->heal_until = in->now + 3.5;
		b->heal_after = in->now + 15.0;
		b->heal_since = 0.0;
	}
	return 1;
}

// The leader is hurt and it is quiet: walk up, face them and press the second
// button with the syringe in hand (the game's own partner heal). True while busy.
static int CoF_BrainMedic( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float wanted[3], chest[3], distance;
	if( b->medic_check != 0.0 )
	{
		// The syringe went in (both of us are held for a moment): see whether it took.
		if( in->now < b->medic_check ) { out->mode = COF_BRAIN_MEDIC; return 1; }
		b->medic_after = in->now + ( in->leader_health > b->medic_health ? in->medic : 6.0f );
		b->medic_check = 0.0;
		return 0;
	}
	if( in->medic <= 0.0f || !in->has_heal || !in->leader || !in->leader_alive || in->leader_health > COF_BRAIN_MEDIC_BELOW ||
		in->now < b->medic_after || !in->leader_visible || CoF_BrainDist2D( in->self, in->leader_pos ) > ( in->stay ? 120.0f : 400.0f ))
	{
		b->medic_since = 0.0;
		return 0;
	}
	if( b->medic_since == 0.0 ) b->medic_since = in->now;
	if( in->now - b->medic_since > 10.0 ) { b->medic_after = in->now + 10.0; b->medic_since = 0.0; return 0; } // they keep moving
	out->mode = COF_BRAIN_MEDIC;
	memcpy( chest, in->leader_eye, sizeof( chest ));
	chest[2] -= 20.0f;
	CoF_BrainFace( in, chest, wanted, out );
	distance = CoF_BrainDist2D( in->self, in->leader_pos );
	if( distance > COF_BRAIN_MEDIC_REACH ) CoF_BrainMoveToward( in->self, in->leader_pos, out->angles[1], COF_BRAIN_WALK, out );
	if( in->weapon != COF_WEAPON_HEAL ) out->action = COF_BRAIN_ACT_EQUIP_HEAL;
	else if( distance <= COF_BRAIN_MEDIC_REACH + 4.0f && in->ready && CoF_BrainAimError( out->angles, wanted ) <= COF_BRAIN_AIM_TOLERANCE )
	{
		out->buttons |= COF_IN_ATTACK2;
		b->medic_check = in->now + 3.0;
		b->medic_health = in->leader_health;
		b->medic_since = 0.0;
	}
	return 1;
}

// Out of a fight: sights down, and the gun (or the knife) back in hand.
static void CoF_BrainCalm( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	int want = CoF_BrainWeapon( in );
	if( out->action != COF_BRAIN_ACT_NONE ) return;
	if( in->sights && in->ready && in->now >= b->sights_after )
	{
		out->buttons |= COF_IN_CANCEL;
		b->sights_after = in->now + 1.0;
	}
	else if( want != COF_WEAPON_NONE && in->weapon != want ) out->action = want == COF_WEAPON_GUN ? COF_BRAIN_ACT_EQUIP_GUN : COF_BRAIN_ACT_EQUIP_MELEE;
	else if( in->weapon == COF_WEAPON_GUN && in->clip <= 0 && in->reserve > 0 && in->ready && !in->reloading && in->now >= b->use_after )
	{
		out->buttons |= COF_IN_RELOAD;
		b->use_after = in->now + 2.0;
	}
}

// Standing by: out of the leader's body (and a partner's), and out of the leader's
// face: right in front of their eyes for a second, step aside.
static void CoF_BrainSpace( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float flat = CoF_BrainDist2D( in->self, in->leader_pos ), away[3], look, angle;
	if( flat < COF_BRAIN_PERSONAL || ( in->crowd && CoF_BrainDist2D( in->self, in->crowd_pos ) < COF_BRAIN_PERSONAL ))
	{
		const float *from = flat < COF_BRAIN_PERSONAL ? in->leader_pos : in->crowd_pos;
		float x = in->self[0] - from[0], y = in->self[1] - from[1];
		if( x * x + y * y < 1.0f )
		{
			// right on top: toward the leader's back
			x = -cosf( in->leader_view[1] / 57.2957795f );
			y = -sinf( in->leader_view[1] / 57.2957795f );
		}
		away[0] = in->self[0] + x * COF_BRAIN_PERSONAL_OUT;
		away[1] = in->self[1] + y * COF_BRAIN_PERSONAL_OUT;
		away[2] = in->self[2];
		CoF_BrainMoveToward( in->self, away, out->angles[1], COF_BRAIN_WALK, out );
		b->face_since = 0.0;
		return;
	}
	look = atan2f( in->self[1] - in->leader_pos[1], in->self[0] - in->leader_pos[0] ) * 57.2957795f;
	angle = CoF_BrainNormalize( look - in->leader_view[1] );
	if( b->stepping ? ( fabsf( angle ) < COF_BRAIN_FACE_CLEAR && flat < 128.0f ) : ( flat < COF_BRAIN_FACE && fabsf( angle ) < COF_BRAIN_FACE_ANGLE ))
	{
		if( b->face_since == 0.0 ) b->face_since = in->now;
		if( b->stepping || in->now - b->face_since > COF_BRAIN_FACE_TIME )
		{
			// sideways out of their view, away from its axis
			float side = ( angle >= 0.0f ? 90.0f : -90.0f ) + in->leader_view[1];
			b->stepping = 1;
			away[0] = in->self[0] + cosf( side / 57.2957795f ) * 64.0f;
			away[1] = in->self[1] + sinf( side / 57.2957795f ) * 64.0f;
			away[2] = in->self[2];
			CoF_BrainMoveToward( in->self, away, out->angles[1], COF_BRAIN_WALK, out );
		}
		return;
	}
	b->face_since = 0.0;
	b->stepping = 0;
}

// One frame of thought. The engine keeps b between frames; in is this frame's
// perception; out receives the usercmd and an optional action.
static void CoF_BrainThink( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float target[3], wanted[3], distance, gap = COF_BRAIN_RANK_GAP * (float)( in->rank > 0 ? in->rank : 0 );
	int danger = in->threat && in->threat_dist < COF_BRAIN_DANGER, calm;
	memset( out, 0, sizeof( *out ));
	memcpy( out->angles, in->view, sizeof( out->angles ));
	if( in->leader && in->leader_alive ) CoF_BrainNoteLeader( b, in );
	if( in->held || in->leader_held )
	{
		// a cutscene: nobody is lost meanwhile, and no teleport until it has settled
		b->lost_since = 0.0;
		b->held_until = in->now + COF_BRAIN_SETTLE;
	}
	if( !in->alive )
	{
		// Press jump on and off: the retail respawn waits for it (PreThink 100EF5F4).
		// Someone reviving me: lie still (a respawn would end their CPR).
		out->mode = b->mode = COF_BRAIN_DEAD;
		b->fire_held = 0;
		b->heal_until = 0.0;
		b->medic_check = 0.0;
		if( !in->revived && ((long)( in->now * 2.0 ) & 1 )) out->buttons |= COF_IN_JUMP;
		return;
	}
	if( in->struggle )
	{
		// A fresh press every other frame; a held button counts once.
		out->mode = b->mode = COF_BRAIN_STRUGGLE;
		b->mash = !b->mash;
		if( b->mash ) out->buttons |= COF_IN_ATTACK;
		b->fire_held = b->mash;
		return;
	}
	if( in->cpr && !danger )
	{
		// The game's CPR runs while +use stays down; letting go would end it.
		out->mode = b->mode = COF_BRAIN_REVIVE;
		out->buttons |= COF_IN_USE;
		b->stuck_since = 0.0;
		b->fire_held = 0;
		return;
	}
	if( in->held ) { out->mode = b->mode = COF_BRAIN_HELD; b->stuck_since = 0.0; b->fire_held = 0; b->face_since = 0.0; return; }

	// The leader is down: get to them and start the CPR (a threat right here comes first).
	if( in->leader && in->leader_down && !danger )
	{
		out->mode = COF_BRAIN_REVIVE;
		b->fire_held = 0;
		distance = CoF_BrainDist2D( in->self, in->leader_pos );
		if( distance > COF_BRAIN_REVIVE_REACH )
		{
			if(( !in->leader_visible && distance > 300.0f ) || distance > COF_BRAIN_FAR || CoF_BrainStuck( b, in, 1, in->leader_pos ))
				CoF_BrainTeleport( b, in, out, "revive" );
			CoF_BrainGo( in, in->leader_pos, distance > COF_BRAIN_RUN ? COF_BRAIN_RUN_SPEED : COF_BRAIN_WALK, out );
			CoF_BrainObstacle( b, in, out );
		}
		else
		{
			// beside the body: look down at it
			CoF_BrainFace( in, in->leader_pos, wanted, out );
			b->stuck_since = 0.0;
			out->action = COF_BRAIN_ACT_REVIVE;
		}
		b->mode = out->mode;
		return;
	}

	// A threat: fight it.
	if( in->threat )
	{
		CoF_BrainFight( b, in, out );
		b->fire_held = ( out->buttons & COF_IN_ATTACK ) != 0;
		b->stuck_since = 0.0;
		b->heal_until = 0.0;
		b->medic_check = 0.0;
		b->mode = out->mode;
		return;
	}
	b->fire_held = 0;

	// Quiet: see to my wounds, then the leader's.
	if( CoF_BrainHeal( b, in, out ) || CoF_BrainMedic( b, in, out ))
	{
		b->stuck_since = 0.0;
		b->mode = out->mode;
		return;
	}

	// Told to wait: walk back to the spot if pushed off it, otherwise stand guard.
	if( in->stay )
	{
		out->mode = b->mode = COF_BRAIN_IDLE;
		b->lost_since = 0.0;
		if( CoF_BrainDist2D( in->self, in->stay_pos ) > COF_BRAIN_REACHED * 2.0f )
		{
			CoF_BrainGo( in, in->stay_pos, COF_BRAIN_WALK, out );
			if( CoF_BrainStuck( b, in, 1, in->stay_pos )) b->stuck_since = 0.0; // stay put rather than teleport
		}
		CoF_BrainCalm( b, in, out );
		return;
	}

	if( !in->leader || !in->leader_alive )
	{
		out->mode = b->mode = COF_BRAIN_IDLE;
		CoF_BrainCalm( b, in, out );
		return;
	}

	// Follow.
	distance = CoF_BrainDist( in->self, in->leader_pos );
	calm = !in->leader_held && in->now >= b->held_until; // teleports wait until a cutscene has settled
	if( b->door && in->leader_visible && in->leader_walkable ) b->door = 0; // moved within the room
	if( b->door )
	{
		// The game took the leader through a door: walk their trail to where they left,
		// then come out after them. Neither far nor lost meanwhile.
		const float *at = CoF_BrainCrumb( b, b->door - 1 );
		b->lost_since = 0.0;
		out->mode = b->mode = COF_BRAIN_FOLLOW;
		if( in->now - b->door_since > COF_BRAIN_DOOR_WAIT )
		{
			if( calm ) CoF_BrainTeleport( b, in, out, "trail" );
			return;
		}
		if( CoF_BrainDist2D( in->self, at ) < COF_BRAIN_DOOR_REACH && fabsf( at[2] - in->self[2] ) < COF_BRAIN_DOOR_HEIGHT )
		{
			if( calm ) CoF_BrainDoorTeleport( b, in, out );
			return;
		}
		CoF_BrainFollowPoint( b, in, target );
		b->following = 1;
		CoF_BrainGo( in, target, CoF_BrainDist( in->self, target ) > COF_BRAIN_RUN ? COF_BRAIN_RUN_SPEED : COF_BRAIN_WALK, out );
		if( CoF_BrainStuck( b, in, 1, target ) && calm ) CoF_BrainTeleport( b, in, out, "stuck" );
		else
		{
			CoF_BrainCalm( b, in, out );
			CoF_BrainObstacle( b, in, out );
		}
		return;
	}
	if( !in->leader_visible && distance > 300.0f )
	{
		if( b->lost_since == 0.0 ) b->lost_since = in->now;
	}
	else b->lost_since = 0.0;
	{
		int lost = b->lost_since != 0.0 && in->now - b->lost_since > ( in->nav ? COF_BRAIN_LOST_ROUTE : COF_BRAIN_LOST );
		if( calm && ( distance > COF_BRAIN_FAR || lost || ( b->broken && b->cursor >= b->count && !in->leader_visible )))
		{
			out->mode = b->mode = COF_BRAIN_FOLLOW;
			CoF_BrainTeleport( b, in, out, distance > COF_BRAIN_FAR ? "far" : lost ? "lost" : "trail" );
			return;
		}
	}
	// a partner standing in the way: as near as this bot gets for now; one walking on that way: its pace
	if( b->following ? distance > COF_BRAIN_STOP + gap : distance > COF_BRAIN_START + gap )
	{
		int found = CoF_BrainFollowPoint( b, in, target ), blocked = in->partner_block && in->partner_pace <= 0.0f;
		float speed = distance > COF_BRAIN_RUN ? COF_BRAIN_RUN_SPEED : COF_BRAIN_WALK;
		// The way it would set off, whether it does or not: the engine looks for a partner that way. (It
		// looked toward the leader while standing and toward the trail while walking, so with a partner on
		// one line and not the other it set off and stopped every frame; live 2026-10-16.)
		memcpy( out->aim, target, sizeof( out->aim ));
		out->aiming = 1;
		if( in->partner_block && in->partner_pace > 0.0f && in->partner_pace < speed ) speed = in->partner_pace;
		if(( found == 1 || in->nav ) && !blocked )
		{
			out->mode = b->mode = COF_BRAIN_FOLLOW;
			b->following = 1;
			CoF_BrainGo( in, target, speed, out );
			if( CoF_BrainStuck( b, in, 1, target ))
			{
				if( !calm ) b->stuck_since = in->now;
				else if( !in->nav || b->replanned )
				{
					CoF_BrainTeleport( b, in, out, "stuck" );
					return;
				}
				// another way round before giving up
				b->replanned = 1;
				b->stuck_since = in->now;
				out->replan = 1;
			}
			CoF_BrainCalm( b, in, out );
			CoF_BrainObstacle( b, in, out );
			return;
		}
		// The leader out of sight and no trail: wait here, but say where to,
		// so the engine looks for a route.
		if( !blocked )
		{
			memcpy( out->goal, target, sizeof( out->goal ));
			out->going = 1;
		}
	}

	// Beside the leader: stand, look where they look, weapon ready.
	b->following = 0;
	b->stuck_since = 0.0;
	out->mode = b->mode = COF_BRAIN_IDLE;
	wanted[0] = 0.0f; wanted[1] = in->leader_view[1]; wanted[2] = 0.0f;
	CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * 0.25f * in->frametime, out->angles );
	CoF_BrainSpace( b, in, out );
	CoF_BrainCalm( b, in, out );
}
#endif
