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
 *      trail: no navigation mesh exists, but the leader's own path is
 *      walkable), and teleport out of sight behind them when lost or stuck;
 *  10. otherwise stand by them, looking where they look, gun in hand.
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
#define COF_BRAIN_NEAR 96.0f // close enough to the leader: stop
#define COF_BRAIN_RUN 260.0f // farther than this: run
#define COF_BRAIN_FAR 900.0f // farther than this: teleport
#define COF_BRAIN_LOST 6.0 // leader out of sight and over 300 units away this long: teleport
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
	COF_BRAIN_ACT_REVIVE // start the game's CPR on the leader's body
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
} cof_brain_input_t;

typedef struct
{
	float angles[3]; // view angles to send
	float forward, side, up; // usercmd moves
	int buttons;
	int action; // COF_BRAIN_ACT_*
	int mode; // COF_BRAIN_* for the log
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

// The leader's position this frame: a breadcrumb every COF_BRAIN_CRUMB units;
// a teleport (door, ladder, scripted move) breaks the trail there.
static void CoF_BrainNoteLeader( cof_brain_t *b, const float *pos )
{
	float moved;
	int slot;
	if( !b->have_sample )
	{
		memcpy( b->last_sample, pos, sizeof( b->last_sample ));
		b->have_sample = 1;
		return;
	}
	moved = CoF_BrainDist( pos, b->last_sample );
	if( moved < COF_BRAIN_CRUMB ) return;
	if( moved > COF_BRAIN_JUMP )
	{
		b->broken = 1;
		b->count = b->cursor = 0;
		memcpy( b->last_sample, pos, sizeof( b->last_sample ));
		return;
	}
	slot = b->head;
	memcpy( b->trail[slot], pos, sizeof( b->trail[slot] ));
	b->head = ( b->head + 1 ) % COF_BRAIN_TRAIL;
	if( b->count < COF_BRAIN_TRAIL ) b->count++;
	else if( b->cursor > 0 ) b->cursor--; // the oldest point fell off
	memcpy( b->last_sample, pos, sizeof( b->last_sample ));
}

// The trail point index k (0 = oldest still kept).
static const float *CoF_BrainCrumb( const cof_brain_t *b, int k )
{
	return b->trail[( b->head - b->count + k + COF_BRAIN_TRAIL * 2 ) % COF_BRAIN_TRAIL];
}

// Where to walk to follow: the leader when the way is clear, else the next
// trail point. Returns 0 when there is nothing to walk to.
static int CoF_BrainFollowPoint( cof_brain_t *b, const cof_brain_input_t *in, float *point )
{
	if( in->leader_walkable && in->leader_visible )
	{
		// The way is open: the trail behind us no longer matters.
		b->cursor = b->count;
		memcpy( point, in->leader_pos, sizeof( float ) * 3 );
		return 1;
	}
	while( b->cursor < b->count && CoF_BrainDist2D( in->self, CoF_BrainCrumb( b, b->cursor )) < COF_BRAIN_REACHED ) b->cursor++;
	// Skip ahead to the newest point we are already close to (the leader doubled back).
	{
		int k;
		for( k = b->count - 1; k > b->cursor; k-- )
			if( CoF_BrainDist2D( in->self, CoF_BrainCrumb( b, k )) < COF_BRAIN_REACHED ) { b->cursor = k + 1; break; }
	}
	if( b->cursor < b->count )
	{
		memcpy( point, CoF_BrainCrumb( b, b->cursor ), sizeof( float ) * 3 );
		return 1;
	}
	if( in->leader_visible )
	{
		memcpy( point, in->leader_pos, sizeof( float ) * 3 );
		return 1;
	}
	return 0;
}

static int CoF_BrainTeleport( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	if( in->now < b->teleport_after ) return 0;
	b->teleport_after = in->now + COF_BRAIN_TELEPORT_COOLDOWN;
	b->stuck_since = b->lost_since = 0.0;
	b->broken = 0;
	b->count = b->cursor = 0;
	out->action = COF_BRAIN_ACT_TELEPORT;
	return 1;
}

// Moving with intent: notice when the body does not follow.
static int CoF_BrainStuck( cof_brain_t *b, const cof_brain_input_t *in, int moving )
{
	if( !moving )
	{
		b->stuck_since = 0.0;
		return 0;
	}
	if( b->stuck_since == 0.0 || CoF_BrainDist( in->self, b->stuck_origin ) > 24.0f )
	{
		b->stuck_since = in->now;
		memcpy( b->stuck_origin, in->self, sizeof( b->stuck_origin ));
		return 0;
	}
	return in->now - b->stuck_since > COF_BRAIN_STUCK;
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

// One frame of thought. The engine keeps b between frames; in is this frame's
// perception; out receives the usercmd and an optional action.
static void CoF_BrainThink( cof_brain_t *b, const cof_brain_input_t *in, cof_brain_output_t *out )
{
	float target[3], wanted[3], distance;
	int danger = in->threat && in->threat_dist < COF_BRAIN_DANGER;
	memset( out, 0, sizeof( *out ));
	memcpy( out->angles, in->view, sizeof( out->angles ));
	if( in->leader && in->leader_alive ) CoF_BrainNoteLeader( b, in->leader_pos );
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
	if( in->held ) { out->mode = b->mode = COF_BRAIN_HELD; b->stuck_since = 0.0; b->fire_held = 0; return; }

	// The leader is down: get to them and start the CPR (a threat right here comes first).
	if( in->leader && in->leader_down && !danger )
	{
		out->mode = COF_BRAIN_REVIVE;
		b->fire_held = 0;
		distance = CoF_BrainDist2D( in->self, in->leader_pos );
		CoF_BrainFace( in, in->leader_pos, wanted, out );
		if( distance > COF_BRAIN_REVIVE_REACH )
		{
			if(( !in->leader_visible && distance > 300.0f ) || distance > COF_BRAIN_FAR || CoF_BrainStuck( b, in, 1 ))
				CoF_BrainTeleport( b, in, out );
			CoF_BrainMoveToward( in->self, in->leader_pos, out->angles[1], distance > COF_BRAIN_RUN ? COF_BRAIN_RUN_SPEED : COF_BRAIN_WALK, out );
		}
		else
		{
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
			CoF_BrainLookAt( in->eye, in->stay_pos, wanted );
			wanted[0] = 0.0f;
			CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * in->frametime, out->angles );
			CoF_BrainMoveToward( in->self, in->stay_pos, out->angles[1], COF_BRAIN_WALK, out );
			if( CoF_BrainStuck( b, in, 1 )) b->stuck_since = 0.0; // stay put rather than teleport
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
	if( !in->leader_visible && distance > 300.0f )
	{
		if( b->lost_since == 0.0 ) b->lost_since = in->now;
	}
	else b->lost_since = 0.0;
	if( distance > COF_BRAIN_FAR || ( b->lost_since != 0.0 && in->now - b->lost_since > COF_BRAIN_LOST ) ||
		( b->broken && b->cursor >= b->count && !in->leader_visible ))
	{
		out->mode = b->mode = COF_BRAIN_FOLLOW;
		CoF_BrainTeleport( b, in, out );
		return;
	}
	if( distance > COF_BRAIN_NEAR && CoF_BrainFollowPoint( b, in, target ))
	{
		float speed = distance > COF_BRAIN_RUN ? COF_BRAIN_RUN_SPEED : COF_BRAIN_WALK;
		out->mode = b->mode = COF_BRAIN_FOLLOW;
		CoF_BrainLookAt( in->eye, target, wanted );
		wanted[0] = 0.0f;
		CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * in->frametime, out->angles );
		CoF_BrainMoveToward( in->self, target, out->angles[1], speed, out );
		if( CoF_BrainStuck( b, in, 1 ))
		{
			CoF_BrainTeleport( b, in, out );
			return;
		}
		// Bumped into something low: hop once (the stuck timer teleports if that fails).
		if( b->stuck_since != 0.0 && in->now - b->stuck_since > 0.6 && in->onground && in->now >= b->jump_after )
		{
			out->buttons |= COF_IN_JUMP;
			b->jump_after = in->now + 1.5;
		}
		CoF_BrainCalm( b, in, out );
		return;
	}

	// Beside the leader: stand, look where they look, weapon ready.
	b->stuck_since = 0.0;
	out->mode = b->mode = COF_BRAIN_IDLE;
	wanted[0] = 0.0f; wanted[1] = in->leader_view[1]; wanted[2] = 0.0f;
	CoF_BrainTurn( in->view, wanted, COF_BRAIN_TURN_RATE * 0.25f * in->frametime, out->angles );
	CoF_BrainCalm( b, in, out );
}
#endif
