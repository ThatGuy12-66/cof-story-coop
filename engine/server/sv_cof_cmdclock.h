/* Cry of Fear story co-op (1.1.9, movement): the time a person's position belongs to. GPL-3.0-or-later.
 *
 * Clients draw a partner ex_interp in the past, between samples stamped with the player's animtime
 * (engine/client/cl_frame.c). The game stamps a player with the time of their last command, and a
 * packet's commands run with a time base that ends at the server frame it arrived in
 * (SV_EstablishTimeBase): a position was stamped with when it arrived, not with when the person was
 * there. Commands that slipped across a server frame (two in one frame, none in the next) drew the
 * partner speeding up and slowing down. Each person has a command clock instead: it moves on by each
 * command's own length, so the stamps are as even as the movement; it never runs past the end of the
 * server frame (nothing is stamped in the future) and never goes back; it closes a part of any gap to
 * that end with every command (a slower clock, lost commands) and is never more than
 * COF_CMDCLOCK_WINDOW behind it, so it keeps well inside ex_interp. A command that moves nobody
 * (frozen, paused: no length) puts it at the frame's end, the stamp it had before. Pure maths, no
 * engine types: tests/engine/cof_cmdclock_test.c drives it. */
#ifndef SV_COF_CMDCLOCK_H
#define SV_COF_CMDCLOCK_H

#define COF_CMDCLOCK_WINDOW 0.05 // seconds behind the server frame's end at most (ex_interp is 0.1)
#define COF_CMDCLOCK_PULL ( 1.0 / 256.0 ) // each command closes this part of the gap to the frame's end: enough for a
// clock 0.1% slow (4 ms behind), little enough not to bring the jitter back (under a millisecond at 12 ms)
#define COF_CMDCLOCK_RESTART 1.0 // a clock this far past the frame's end belongs to an earlier map: start again

// running: the person's command clock (0: none yet); seconds: this command's length (msec / 1000); end: the end of
// the server frame running it (sv.time + sv.frametime). Returns the clock after the command.
static inline double CoF_CmdClockAdvance( double running, double seconds, double end )
{
	if( running > end )
		return running - end > COF_CMDCLOCK_RESTART ? end : running; // a frame shorter than the last one (paused): hold
	if( running <= 0.0 || seconds <= 0.0 )
		return end;
	running += seconds;
	if( running < end )
		running += ( end - running ) * COF_CMDCLOCK_PULL;
	if( running > end )
		running = end;
	if( running < end - COF_CMDCLOCK_WINDOW )
		running = end - COF_CMDCLOCK_WINDOW;
	return running;
}

#ifdef SERVER_H
// SV_RunCmd: a person's command ran for this long (sv_pmove.c).
void SV_CoF_CmdClockRun( const sv_client_t *cl, double seconds );
// SV_AddEntitiesToPacket: the animtime player e is sent with (theirs from the game when they have no clock).
float SV_CoF_CmdClockStamp( int e, float animtime );
#endif
#endif
