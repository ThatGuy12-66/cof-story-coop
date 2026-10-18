/* The AI companion's mind, driven directly. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sv_cof_brain.h"

static cof_brain_t b;
static cof_brain_input_t in;
static cof_brain_output_t out;

static void set3( float *v, float x, float y, float z ) { v[0] = x; v[1] = y; v[2] = z; }
/* Companion at the origin facing +x, leader somewhere, everyone alive, no threat, a knife in hand. */
static void scene( float lx, float ly )
{
	CoF_BrainReset( &b );
	memset( &in, 0, sizeof( in ));
	in.now = 100.0; in.frametime = 0.05f;
	in.alive = in.onground = 1; in.health = 100;
	set3( in.self, 0, 0, 0 ); set3( in.eye, 0, 0, 28 );
	in.leader = in.leader_alive = in.leader_visible = in.leader_walkable = 1; in.leader_health = 100;
	set3( in.leader_pos, lx, ly, 0 ); set3( in.leader_eye, lx, ly, 28 );
	in.has_melee = 1; in.weapon = COF_WEAPON_MELEE; in.ready = 1;
	in.medic = 90.0f;
}
static void threat( float x, float gap )
{
	in.threat = 9; set3( in.threat_pos, x, 0, 28 ); in.threat_dist = x; in.threat_gap = gap; in.line_clear = 1;
}
static void think( double dt ) { in.now += dt; CoF_BrainThink( &b, &in, &out ); }
static int near( float a, float c, float tolerance ) { return a > c - tolerance && a < c + tolerance; }

int main( void )
{
	float angles[3];
	int i;

	/* Angles: GoldSrc pitch is positive looking down. */
	{
		float eye[3] = { 0, 0, 0 }, below[3] = { 100, 0, -100 }, left[3] = { 0, 100, 0 };
		CoF_BrainLookAt( eye, below, angles ); assert( near( angles[0], 45, 0.1f ) && near( angles[1], 0, 0.1f ));
		CoF_BrainLookAt( eye, left, angles ); assert( near( angles[0], 0, 0.1f ) && near( angles[1], 90, 0.1f ));
	}

	/* Dead: press jump on and off so the game respawns it; nothing else. Someone reviving
	 * it: lie still. Held by the game (a cutscene, a ladder, a door): nothing. */
	scene( 400, 0 ); in.alive = 0; in.now = 100.0; think( 0 ); assert( out.mode == COF_BRAIN_DEAD && !out.forward && !out.action );
	{
		int pressed = 0, released = 0;
		for( i = 0; i < 8; i++ ) { think( 0.25 ); if( out.buttons & COF_IN_JUMP ) pressed++; else released++; assert( !( out.buttons & ~COF_IN_JUMP )); }
		assert( pressed && released );
		in.revived = 1;
		for( i = 0; i < 8; i++ ) { think( 0.25 ); assert( !out.buttons ); }
	}
	scene( 400, 0 ); in.held = 1; think( 0 ); assert( out.mode == COF_BRAIN_HELD && !out.forward && !out.buttons );

	/* Grabbed, the gun turned on it: a fresh attack press every other frame, whatever else is going on. */
	scene( 400, 0 ); in.struggle = 1; in.held = 1; threat( 50, 30 );
	{
		int presses = 0;
		for( i = 0; i < 10; i++ )
		{
			int before = out.buttons & COF_IN_ATTACK;
			think( 0.016 );
			assert( out.mode == COF_BRAIN_STRUGGLE && !out.forward && !( out.buttons & ~COF_IN_ATTACK ));
			if(( out.buttons & COF_IN_ATTACK ) && !before ) presses++;
		}
		assert( presses == 5 );
	}

	/* Follow: the leader is 400 away straight ahead, in the open: run to them. */
	scene( 400, 0 ); think( 0 );
	assert( out.mode == COF_BRAIN_FOLLOW && near( out.forward, COF_BRAIN_RUN_SPEED, 1 ) && near( out.side, 0, 1 ) && !out.action );
	/* 200 away: walk. To the left (+y) while facing +x: the turn starts and the move goes left (negative side = left). */
	scene( 200, 0 ); think( 0 ); assert( near( out.forward, COF_BRAIN_WALK, 1 ));
	scene( 0, 200 ); think( 0 ); assert( out.angles[1] > 0 && out.side < 0 );
	/* Beside them: stand still and slowly look where they look. */
	scene( 60, 0 ); set3( in.leader_view, 0, 90, 0 ); think( 0 );
	assert( out.mode == COF_BRAIN_IDLE && !out.forward && !out.side && out.angles[1] > 0 && out.angles[1] < 90 );

	/* Follow with slack: set off beyond 128, stop inside 80 (one threshold made a slowly
	 * walking leader start and stop it every frame: 540 mode changes in 30 live minutes). */
	scene( 100, 0 ); think( 0.1 ); assert( out.mode == COF_BRAIN_IDLE && !out.forward );
	set3( in.leader_pos, 140, 0, 0 ); think( 0.1 ); assert( out.mode == COF_BRAIN_FOLLOW && out.forward > 0 );
	set3( in.leader_pos, 100, 0, 0 ); think( 0.1 ); assert( out.mode == COF_BRAIN_FOLLOW && out.forward > 0 );
	set3( in.leader_pos, 70, 0, 0 ); think( 0.1 ); assert( out.mode == COF_BRAIN_IDLE && !out.forward );
	/* walking behind a leader who sneaks at 60 units a second for 20 seconds */
	{
		int flips = 0, last = -1;
		float x = -150.0f;
		scene( 0, 0 );
		for( i = 0; i < 1250; i++ )
		{
			set3( in.leader_pos, (float)i * 0.016f * 60.0f, 0, 0 ); set3( in.leader_eye, in.leader_pos[0], 0, 28 );
			set3( in.self, x, 0, 0 ); set3( in.eye, x, 0, 28 );
			think( 0.016 ); memcpy( in.view, out.angles, sizeof( in.view ));
			x += out.forward * 0.016f;
			if( last >= 0 && out.mode != last ) flips++;
			last = out.mode;
		}
		assert( flips <= 40 );
	}

	/* Far away: teleport (the engine places me out of sight behind them), once per cooldown. */
	scene( 2000, 0 ); think( 0 ); assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "far" ));
	think( 1.0 ); assert( out.action != COF_BRAIN_ACT_TELEPORT );
	think( 2.5 ); assert( out.action == COF_BRAIN_ACT_TELEPORT );

	/* The trail: the leader walked round a corner and is out of sight. Walk their path in order. */
	scene( 0, 0 ); in.leader_visible = in.leader_walkable = 0;
	for( i = 1; i <= 6; i++ ) { set3( in.leader_pos, (float)i * 50, 0, 0 ); think( 0.1 ); } /* east 300 */
	for( i = 1; i <= 4; i++ ) { set3( in.leader_pos, 300, (float)i * 50, 0 ); think( 0.1 ); } /* then north 200 */
	assert( b.count >= 8 );
	set3( in.self, 0, 0, 0 ); think( 0.1 );
	assert( out.mode == COF_BRAIN_FOLLOW && out.forward > 0 && near( out.angles[1], 0, 30 )); /* first crumb is east */
	/* standing on the corner, the next crumbs are north */
	set3( in.self, 300, 0, 0 ); in.view[1] = 0; think( 0.1 ); think( 0.1 );
	assert( out.angles[1] > 0 && b.cursor == 5 ); /* the corner crumb is behind; the first north one is next */

	/* Out of sight over 300 units for 6 seconds: teleport. */
	scene( 500, 0 ); in.leader_visible = in.leader_walkable = 0; think( 0.1 );
	for( i = 0; i < 55; i++ ) { think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }
	for( i = 0; i < 10 && out.action != COF_BRAIN_ACT_TELEPORT; i++ ) think( 0.1 );
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "lost" ));

	/* Stuck: trying to walk, not moving: no blind hop (bumping a wall, a door frame or a
	 * person is no reason to jump; live 2026-10-13 it hopped all the time), a teleport
	 * after the stuck time, and the log says why. */
	scene( 400, 0 ); think( 0.1 );
	{
		int jumped = 0, teleported = 0;
		for( i = 0; i < 40 && !teleported; i++ )
		{
			think( 0.1 );
			jumped |= ( out.buttons & COF_IN_JUMP ) != 0;
			teleported = out.action == COF_BRAIN_ACT_TELEPORT;
		}
		assert( !jumped && teleported && !strcmp( out.why, "stuck" ));
	}
	/* The engine sees a low obstacle right ahead that a jump clears: hop, then not again
	 * for a second; never in the air. */
	scene( 400, 0 ); in.ledge_ahead = 1; think( 0.1 );
	assert( out.mode == COF_BRAIN_FOLLOW && ( out.buttons & COF_IN_JUMP ) && !strcmp( out.why, "ledge" ));
	think( 0.5 ); assert( !( out.buttons & COF_IN_JUMP ));
	think( 0.6 ); assert( out.buttons & COF_IN_JUMP );
	in.onground = 0; think( 1.5 ); assert( !( out.buttons & COF_IN_JUMP ));
	/* A door it may open right ahead: ask the engine to open it, once a second, still
	 * walking at it. */
	scene( 400, 0 ); in.door_ahead = 1; think( 0.1 );
	assert( out.action == COF_BRAIN_ACT_OPEN_DOOR && !strcmp( out.why, "door" ) && out.forward > 0 );
	think( 0.5 ); assert( out.action != COF_BRAIN_ACT_OPEN_DOOR );
	think( 0.6 ); assert( out.action == COF_BRAIN_ACT_OPEN_DOOR );
	/* a locked door never opens: the first try restarts the stuck time (a door needs time
	 * to swing), later tries do not, so it still ends in a teleport */
	scene( 400, 0 ); in.door_ahead = 1; think( 0.1 );
	{
		int teleported = 0;
		for( i = 0; i < 80 && !teleported; i++ ) { think( 0.1 ); teleported = out.action == COF_BRAIN_ACT_TELEPORT; }
		assert( teleported && i > 20 );
	}
	/* standing beside the leader: no door, no hop */
	scene( 60, 0 ); in.door_ahead = in.ledge_ahead = 1; think( 0.1 );
	assert( out.mode == COF_BRAIN_IDLE && out.action == COF_BRAIN_ACT_NONE && !out.buttons );

	/* Routes: wherever it goes it says so (the engine looks for a way round when the straight
	 * way is blocked); with a route point it walks at that instead, still naming its goal. */
	scene( 400, 0 ); think( 0.1 );
	assert( out.going && out.goal[0] == 400 && out.goal[1] == 0 && !out.replan );
	scene( 400, 0 ); in.leader_walkable = 0; in.nav = 1; set3( in.nav_point, 0, 200, 0 ); think( 0.1 );
	assert( out.mode == COF_BRAIN_FOLLOW && out.going && out.goal[0] == 400 && out.angles[1] > 0 && out.side < 0 );
	/* the leader out of sight, no trail: it waits, naming them as its goal; a route there and it goes */
	scene( 500, 0 ); in.leader_visible = in.leader_walkable = 0; think( 0.1 );
	assert( out.mode == COF_BRAIN_IDLE && !out.forward && !out.side && out.going && out.goal[0] == 500 );
	in.nav = 1; set3( in.nav_point, 0, 200, 0 ); think( 0.1 );
	assert( out.mode == COF_BRAIN_FOLLOW && out.side < 0 && out.goal[0] == 500 );
	/* beside the leader it goes nowhere */
	scene( 60, 0 ); think( 0.1 ); assert( !out.going );
	/* while a route leads on (and it moves), a leader out of sight is not lost for 15 seconds */
	scene( 500, 0 ); in.leader_visible = in.leader_walkable = 0; in.nav = 1; set3( in.nav_point, 0, 200, 0 );
	for( i = 0; i < 140; i++ ) { set3( in.self, 0, ( i & 1 ) ? 30.0f : 0.0f, 0 ); think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }
	for( i = 0; i < 20 && out.action != COF_BRAIN_ACT_TELEPORT; i++ ) { set3( in.self, 0, ( i & 1 ) ? 30.0f : 0.0f, 0 ); think( 0.1 ); }
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "lost" ));
	/* stuck on a route: ask for another route once (no teleport yet), then teleport */
	scene( 400, 0 ); in.leader_walkable = 0; in.nav = 1; set3( in.nav_point, 0, 200, 0 ); think( 0.1 );
	{
		int replans = 0, teleported = 0;
		for( i = 0; i < 80 && !teleported; i++ )
		{
			think( 0.1 );
			replans += out.replan;
			teleported = out.action == COF_BRAIN_ACT_TELEPORT;
			assert( !( out.replan && teleported ));
		}
		assert( replans == 1 && teleported && !strcmp( out.why, "stuck" ) && i > 45 );
	}
	/* a downed leader and its post are goals too, walked to along a route */
	scene( 200, 0 ); in.leader_alive = 0; in.leader_down = 1; in.nav = 1; set3( in.nav_point, 0, 200, 0 ); think( 0.1 );
	assert( out.mode == COF_BRAIN_REVIVE && out.going && out.goal[0] == 200 && out.side < 0 );
	scene( 3000, 0 ); in.stay = 1; set3( in.stay_pos, 0, 0, 0 ); set3( in.self, -200, 0, 0 ); set3( in.eye, -200, 0, 28 );
	in.nav = 1; set3( in.nav_point, -200, 200, 0 ); think( 0.1 );
	assert( out.going && out.goal[0] == 0 && out.side < 0 );

	/* The leader went through a door (a 600-unit jump) and is out of sight: walk to where they
	 * stood, then come out of the door after them (live 2026-10-15: 40 of 72 teleports were doors,
	 * most of them in front of the player). */
	scene( 100, 0 ); in.leader_visible = in.leader_walkable = 0; think( 0.1 );
	set3( in.leader_pos, 100, 600, 0 ); think( 0.1 );
	assert( out.action != COF_BRAIN_ACT_TELEPORT && b.door > 0 && out.mode == COF_BRAIN_FOLLOW && out.forward > 0 );
	set3( in.self, 90, 0, 0 ); set3( in.eye, 90, 0, 28 ); think( 0.1 );
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "door" ) && out.place );
	assert( out.place_at[0] == 100 && out.place_at[1] == 600 && !b.door );

	/* Revive: the leader is down 200 units away: go there (no +use on the way: a press
	 * would use whatever the game finds first); beside the body, ask the engine to start the
	 * game's CPR, looking down at them. */
	scene( 200, 0 ); in.leader_alive = 0; in.leader_down = 1; think( 0.1 );
	assert( out.mode == COF_BRAIN_REVIVE && out.forward > 0 && !out.buttons && out.action != COF_BRAIN_ACT_REVIVE );
	set3( in.self, 160, 0, 0 ); set3( in.eye, 160, 0, 28 ); set3( in.view, 30, 0, 0 ); think( 0.1 );
	assert( out.mode == COF_BRAIN_REVIVE && out.action == COF_BRAIN_ACT_REVIVE && !out.buttons && !out.forward && out.angles[0] > 0 );
	/* the CPR runs: +use stays down, even through a cutscene */
	in.cpr = 1; in.leader_down = 0; think( 0.1 ); assert( out.mode == COF_BRAIN_REVIVE && out.buttons == COF_IN_USE && !out.forward );
	in.held = 1; think( 0.1 ); assert( out.buttons == COF_IN_USE ); in.held = 0;
	/* ...unless something is attacking right here */
	threat( 100, 80 ); think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT && !( out.buttons & COF_IN_USE ));
	in.cpr = 0; in.leader_down = 1; think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT );

	/* Fight with a gun: draw it first (the knife in hand: no rounds are counted for it). */
	scene( 50, 0 ); in.has_gun = 1; threat( 500, 480 );
	think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT && out.action == COF_BRAIN_ACT_EQUIP_GUN && !out.buttons );
	/* gun out, far target: sights up first (one press of the sights button, never the bash button) */
	in.weapon = COF_WEAPON_GUN; in.clip = 5; in.reserve = 10; think( 0.1 ); assert( out.buttons == COF_IN_CANCEL );
	in.sights = 1;
	/* aimed: one press per shot; the trigger is up again the next frame; again once ready */
	think( 0.1 ); assert( out.buttons == COF_IN_ATTACK );
	think( 0.05 ); assert( !( out.buttons & COF_IN_ATTACK ));
	think( 0.3 ); assert( out.buttons & COF_IN_ATTACK );
	think( 0.3 ); assert( !( out.buttons & COF_IN_ATTACK )); /* a long frame: still released once */
	think( 0.3 ); assert( out.buttons & COF_IN_ATTACK );
	think( 0.05 ); in.ready = 0; think( 0.3 ); assert( !( out.buttons & COF_IN_ATTACK )); /* between shots */
	in.ready = 1;
	/* not aimed yet, or the leader in the line of fire: no shot */
	think( 1.0 ); set3( in.view, 0, 90, 0 ); think( 0.01 ); assert( !( out.buttons & COF_IN_ATTACK ));
	/* the leader in the line: no shot while players hurt each other (friendly fire on)... */
	set3( in.view, 0, 0, 0 ); in.line_clear = 0; think( 1.0 ); assert( !( out.buttons & COF_IN_ATTACK ));
	/* ...but a bullet passes through players (FireBulletsPlayer) and, with friendly fire off, hurts none: it fires */
	in.pass_through = 1; think( 1.0 ); assert( out.buttons & COF_IN_ATTACK ); in.pass_through = 0;
	/* empty: one press of reload; during the reload nothing */
	in.line_clear = 1; in.clip = 0; think( 1.0 ); assert( out.buttons == COF_IN_RELOAD );
	think( 0.05 ); assert( !( out.buttons & COF_IN_RELOAD ));
	in.reloading = 1; think( 2.0 ); assert( !out.buttons ); in.reloading = 0;
	/* close: keep the gun, step back while shooting */
	in.clip = 5; threat( 80, 60 ); think( 1.0 );
	assert( out.forward < 0 && ( out.buttons & COF_IN_ATTACK ) && out.action == COF_BRAIN_ACT_NONE );
	/* no gun with rounds left: the knife */
	in.has_gun = 0; think( 1.0 ); assert( out.action == COF_BRAIN_ACT_EQUIP_MELEE );

	/* Fight with the knife: close in, then swing (held: melee repeats while it is down). */
	scene( 50, 0 ); threat( 300, 280 );
	think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT && out.forward > 0 && !( out.buttons & COF_IN_ATTACK ));
	threat( 40, 20 ); think( 1.0 ); assert( out.buttons & COF_IN_ATTACK );
	think( 0.05 ); assert( out.buttons & COF_IN_ATTACK );
	assert( !( out.buttons & COF_IN_ATTACK2 )); /* the second button would change the switchblade's stance */
	/* a swing stops on the first body it meets: none with the leader in the way */
	in.line_clear = 0; think( 0.05 ); assert( !( out.buttons & COF_IN_ATTACK )); in.line_clear = 1;

	/* The chainsaw boss kills whoever its swing meets (and then leaves the fight for good): never in its reach. */
	scene( 50, 0 ); threat( 200, 180 ); in.threat_keep = 150.0f;
	think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT && out.forward == 0 && !( out.buttons & COF_IN_ATTACK )); /* knife only: no closing in */
	threat( 100, 80 ); in.threat_keep = 150.0f; think( 0.1 ); assert( out.forward < 0 && !( out.buttons & COF_IN_ATTACK )); /* back away */
	in.has_gun = 1; in.weapon = COF_WEAPON_GUN; in.clip = 5; in.reserve = 5; in.sights = 1;
	threat( 130, 120 ); in.threat_keep = 150.0f; think( 1.0 );
	assert( out.forward < 0 && ( out.buttons & COF_IN_ATTACK )); /* shoot it (it cowers) from out of reach */

	/* A taller stamps anyone lying, ducking or busy near it: no CPR, no syringe while one is close. */
	scene( 40, 0 ); in.leader_alive = 0; in.leader_down = 1; in.stamp = 1;
	think( 0.1 ); assert( out.action != COF_BRAIN_ACT_REVIVE && out.mode != COF_BRAIN_REVIVE );
	in.stamp = 0; think( 0.1 ); assert( out.action == COF_BRAIN_ACT_REVIVE );
	in.cpr = 1; think( 0.1 ); assert( out.buttons & COF_IN_USE );
	in.stamp = 1; think( 0.1 ); assert( !( out.buttons & COF_IN_USE )); /* let go: the CPR ends */
	scene( 50, 0 ); in.health = 20; in.has_heal = 1; in.stamp = 1;
	think( 0.1 ); assert( out.mode != COF_BRAIN_HEAL && out.action != COF_BRAIN_ACT_EQUIP_HEAL );
	scene( 30, 0 ); in.leader_health = 30; in.has_heal = 1; in.stamp = 1;
	think( 0.1 ); assert( out.mode != COF_BRAIN_MEDIC && out.action != COF_BRAIN_ACT_EQUIP_HEAL );

	/* The game halves forwardmove whenever sidemove is set too (PM_CheckParamters): the usercmd doubles it first. */
	assert( CoF_BrainUsercmdForward( 100.0f, 50.0f ) == 200.0f );
	assert( CoF_BrainUsercmdForward( 100.0f, 0.0f ) == 100.0f );
	assert( CoF_BrainUsercmdForward( 0.0f, 50.0f ) == 0.0f );
	assert( CoF_BrainUsercmdForward( -80.0f, -10.0f ) == -160.0f );

	/* Hurt, no threat, a syringe: take it in hand, one press once ready, stand still while it
	 * goes in, then not again for a while. */
	scene( 60, 0 ); in.health = 20; in.has_heal = 1; think( 0.1 );
	assert( out.mode == COF_BRAIN_HEAL && out.action == COF_BRAIN_ACT_EQUIP_HEAL );
	in.weapon = COF_WEAPON_HEAL; in.ready = 0; think( 0.5 ); assert( out.mode == COF_BRAIN_HEAL && !out.buttons );
	in.ready = 1; think( 0.1 ); assert( out.buttons == COF_IN_ATTACK );
	in.ready = 0; set3( in.leader_pos, 400, 0, 0 ); think( 1.0 ); assert( out.mode == COF_BRAIN_HEAL && !out.forward && !out.buttons );
	in.health = 100; think( 3.0 ); assert( out.mode == COF_BRAIN_FOLLOW );
	in.health = 20; in.ready = 1; set3( in.leader_pos, 60, 0, 0 ); think( 1.0 ); assert( out.mode != COF_BRAIN_HEAL );
	/* the game never takes the syringe out: give up for a while instead of standing there */
	scene( 60, 0 ); in.health = 20; in.has_heal = 1;
	for( i = 0; i < 70; i++ ) think( 0.1 );
	assert( out.mode != COF_BRAIN_HEAL );

	/* The leader is hurt and it is quiet: walk up with a syringe, face them, the second button. */
	scene( 200, 0 ); in.has_heal = 1; in.leader_health = 40; think( 0.1 );
	assert( out.mode == COF_BRAIN_MEDIC && out.forward > 0 && out.action == COF_BRAIN_ACT_EQUIP_HEAL );
	in.weapon = COF_WEAPON_HEAL; set3( in.self, 165, 0, 0 ); set3( in.eye, 165, 0, 28 ); set3( in.view, 0, 0, 0 );
	for( i = 0; i < 5 && !( out.buttons & COF_IN_ATTACK2 ); i++ ) { think( 0.05 ); memcpy( in.view, out.angles, sizeof( in.view )); }
	assert( out.buttons == COF_IN_ATTACK2 && !out.forward );
	/* held a moment while it goes in; it took: not again until the medic time has passed */
	think( 1.0 ); assert( out.mode == COF_BRAIN_MEDIC && !out.buttons );
	in.leader_health = 100; think( 2.5 ); assert( out.mode != COF_BRAIN_MEDIC );
	in.leader_health = 30; think( 60.0 ); assert( out.mode != COF_BRAIN_MEDIC );
	think( 31.0 ); assert( out.mode == COF_BRAIN_MEDIC );
	/* never with a threat in sight, never with the medic off, never without a syringe */
	threat( 400, 380 ); think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT ); in.threat = 0;
	in.medic = 0; think( 0.1 ); assert( out.mode != COF_BRAIN_MEDIC ); in.medic = 90;
	in.has_heal = 0; think( 0.1 ); assert( out.mode != COF_BRAIN_MEDIC );

	/* Calm again: sights down, the gun back in hand instead of the syringe; an empty gun reloaded. */
	scene( 60, 0 ); in.has_gun = 1; in.weapon = COF_WEAPON_GUN; in.clip = 5; in.sights = 1; think( 0.1 );
	assert( out.buttons == COF_IN_CANCEL );
	in.sights = 0; in.weapon = COF_WEAPON_HEAL; think( 1.0 ); assert( out.action == COF_BRAIN_ACT_EQUIP_GUN );
	in.weapon = COF_WEAPON_GUN; in.clip = 0; in.reserve = 8; think( 0.1 );
	assert( out.mode == COF_BRAIN_IDLE && out.buttons == COF_IN_RELOAD );

	/* Told to wait: no following or teleporting however far the leader goes; pushed off
	 * the spot, walk back; still fights what comes and still revives the leader. */
	scene( 3000, 0 ); in.stay = 1; set3( in.stay_pos, 0, 0, 0 ); in.leader_visible = 0;
	for( i = 0; i < 100; i++ ) { think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT && !out.forward ); }
	set3( in.self, -200, 0, 0 ); think( 0.1 ); assert( out.forward > 0 || out.side != 0 );
	threat( 300, 280 ); think( 0.1 ); assert( out.mode == COF_BRAIN_FIGHT );
	in.threat = 0; in.leader_alive = 0; in.leader_down = 1; set3( in.leader_pos, 100, 0, 0 ); think( 0.1 ); assert( out.mode == COF_BRAIN_REVIVE );

	/* Doors, step by step. A thin wall: the leader walks east to (300,0), stands, and the door
	 * puts them at (300,80) in one frame. The trail stays: the bot walks it to the door crumb. */
	scene( 0, 0 ); in.leader_visible = in.leader_walkable = 0; set3( in.leader_pos, 0, 0, 0 ); think( 0.05 );
	for( i = 1; i <= 6; i++ ) { set3( in.leader_pos, (float)i * 50, 0, 0 ); think( 0.1 ); }
	set3( in.leader_pos, 300, 80, 0 ); think( 0.05 );
	assert( out.action != COF_BRAIN_ACT_TELEPORT && b.door > 0 && out.goal[1] == 0 && out.goal[0] <= 300 );
	set3( in.self, 280, 0, 0 ); set3( in.eye, 280, 0, 28 ); think( 0.05 );
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "door" ) && out.place &&
		out.place_at[0] == 300 && out.place_at[1] == 80 );
	/* a hitch (30 units in a tenth of a second) is no door */
	scene( 0, 0 ); set3( in.leader_pos, 200, 0, 0 ); think( 0.1 ); set3( in.leader_pos, 230, 0, 0 ); think( 0.1 ); assert( !b.door );
	/* a far exit: walking to the door crumb is never "far" or "lost"; the door on arrival */
	scene( 0, 0 ); in.leader_visible = in.leader_walkable = 0;
	set3( in.leader_pos, 500, 0, 0 ); think( 0.05 ); set3( in.leader_pos, 2500, 0, 0 ); think( 0.05 );
	for( i = 0; i < 90; i++ )
	{
		set3( in.self, (float)i * 5.5f, 0, 0 ); set3( in.eye, (float)i * 5.5f, 0, 28 ); think( 0.05 );
		assert( out.action != COF_BRAIN_ACT_TELEPORT || !strcmp( out.why, "door" ));
		if( out.action == COF_BRAIN_ACT_TELEPORT ) break;
	}
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "door" ));
	/* a door it cannot reach: stuck, at the latest the ordinary trail teleport after the wait */
	scene( 0, 0 ); in.leader_visible = in.leader_walkable = 0;
	set3( in.leader_pos, 300, 0, 0 ); think( 0.05 ); set3( in.leader_pos, 300, 900, 0 ); think( 0.05 );
	for( i = 0; i < 220 && out.action != COF_BRAIN_ACT_TELEPORT; i++ ) think( 0.05 );
	assert( out.action == COF_BRAIN_ACT_TELEPORT && ( !strcmp( out.why, "stuck" ) || !strcmp( out.why, "trail" )) && !out.place );
	/* moved inside the room, still in sight and walkable: no door, no teleport */
	scene( 100, 0 ); think( 0.05 ); set3( in.leader_pos, 100, 150, 0 ); think( 0.05 );
	assert( !b.door && out.action != COF_BRAIN_ACT_TELEPORT );

	/* Cutscenes: while the leader is held (or just after) nothing teleports; "lost" counts from the release. */
	scene( 500, 0 ); in.leader_visible = in.leader_walkable = 0; in.leader_held = 1;
	for( i = 0; i < 100; i++ ) { think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }
	in.leader_held = 0;
	for( i = 0; i < 55; i++ ) { think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }
	for( i = 0; i < 15 && out.action != COF_BRAIN_ACT_TELEPORT; i++ ) think( 0.1 );
	assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "lost" ));
	/* released 2000 away: half a second for the script's own moves to land, then "far" */
	scene( 2000, 0 ); in.leader_held = 1; think( 0.1 ); in.leader_held = 0;
	think( 0.2 ); assert( out.action != COF_BRAIN_ACT_TELEPORT );
	think( 0.2 ); assert( out.action != COF_BRAIN_ACT_TELEPORT );
	think( 0.2 ); assert( out.action == COF_BRAIN_ACT_TELEPORT && !strcmp( out.why, "far" ));
	/* the bot itself held 8 s with the leader out of sight: not "lost" the moment it is free */
	scene( 500, 0 ); in.leader_visible = in.leader_walkable = 0; in.held = 1;
	for( i = 0; i < 80; i++ ) think( 0.1 );
	in.held = 0; think( 0.1 ); think( 0.6 ); assert( out.action != COF_BRAIN_ACT_TELEPORT );

	/* Stuck means no progress: jittering back and forth over 24 units is not getting anywhere
	 * (the old rule never caught it); a steady walk, or a receding leader, is never stuck. */
	scene( 400, 0 ); in.leader_walkable = 0;
	{
		int teleported = 0;
		for( i = 0; i < 60 && !teleported; i++ )
		{
			set3( in.self, ( i & 1 ) ? 30.0f : 0.0f, 0, 0 ); think( 0.1 );
			teleported = out.action == COF_BRAIN_ACT_TELEPORT;
		}
		assert( teleported && !strcmp( out.why, "stuck" ) && i < 40 );
	}
	scene( 900, 0 );
	for( i = 0; i < 100; i++ ) { set3( in.self, (float)i * 5.0f, 0, 0 ); think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }
	scene( 400, 0 );
	for( i = 0; i < 100; i++ ) { set3( in.self, (float)i * 15.0f, 0, 0 ); set3( in.leader_pos, 400 + (float)i * 15.0f, 0, 0 ); think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT ); }

	/* Corners and door jambs: the engine found an opening to one side; step over to line up with it. */
	scene( 400, 0 ); in.side_shift = 16; in.probe_yaw = 0; think( 0.05 );
	assert( out.side < 0 && out.forward > 0 && out.forward < 0.5f * COF_BRAIN_RUN_SPEED );
	assert( out.move_to[1] > 0 && out.move_to[0] > 0 && out.move_to[0] < 20 );
	scene( 400, 0 ); in.side_shift = -16; in.probe_yaw = 0; think( 0.05 ); assert( out.side > 0 );
	scene( 400, 0 ); in.side_shift = 0; think( 0.05 ); assert( near( out.side, 0, 1 ) && out.move_to[0] == 400 );
	scene( 400, 0 ); in.side_shift = 16; in.probe_yaw = 20; think( 0.05 ); assert( near( out.side, 0, 1 ));

	/* Personal space: inside the leader, step out; in their face, step aside after a second. */
	scene( 10, 0 ); set3( in.leader_view, 0, 180, 0 ); think( 0.1 ); assert( out.forward < 0 && out.mode == COF_BRAIN_IDLE );
	scene( 60, 0 ); set3( in.leader_view, 0, 180, 0 ); think( 0.1 ); assert( !out.forward && !out.side );
	for( i = 0; i < 9; i++ ) think( 0.1 );
	think( 0.2 ); assert( out.side != 0 );
	scene( 60, 0 ); set3( in.leader_view, 0, 0, 0 ); for( i = 0; i < 20; i++ ) think( 0.1 ); assert( !out.forward && !out.side );
	/* another bot (or person) right on top of it: step away from them */
	scene( 60, 0 ); in.crowd = 1; set3( in.crowd_pos, 0, 10, 0 ); think( 0.1 ); assert( out.side > 0 );

	/* Several bots behind one person: each keeps its own distance (rank), and a partner in the
	 * way counts as arrived, not as stuck. */
	scene( 170, 0 ); in.rank = 2; think( 0.1 ); assert( out.mode == COF_BRAIN_IDLE );
	scene( 240, 0 ); in.rank = 2; think( 0.1 ); assert( out.mode == COF_BRAIN_FOLLOW );
	scene( 400, 0 ); in.partner_block = 1;
	for( i = 0; i < 100; i++ ) { think( 0.1 ); assert( out.action != COF_BRAIN_ACT_TELEPORT && !out.forward ); }

	/* A trail point above or below (stairs, a switchback) is not reached by standing under it. */
	scene( 0, 0 ); in.leader_visible = in.leader_walkable = 0;
	set3( in.leader_pos, -40, 0, 0 ); think( 0.1 );
	set3( in.leader_pos, 0, 0, 60 ); think( 0.1 ); /* a crumb right above the bot */
	set3( in.leader_pos, 60, 0, 60 ); think( 0.1 );
	set3( in.leader_pos, 120, 0, 60 ); think( 0.1 );
	think( 0.1 ); assert( b.count >= 2 && b.cursor == 0 );

	puts( "brain scenarios passed" );
	return 0;
}
