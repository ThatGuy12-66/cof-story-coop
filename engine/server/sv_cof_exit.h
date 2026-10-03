/* Opt-in Cry of Fear story co-op: hold a level exit until the party is at it.
 * GPL-3.0-or-later. */
#ifndef SV_COF_EXIT_H
#define SV_COF_EXIT_H
void SV_CoF_ExitReset( void );
// pfnChangeLevel hook. true: change level now. false: held; the frame lets it go.
qboolean SV_CoF_ExitRequest( const char *level, const char *landmark );
void SV_CoF_ExitFrame( void );
// The warp key during a hold. true: it was the waiter's and has been dealt with.
qboolean SV_CoF_ExitWarpKey( sv_client_t *cl );
// The userid waiting at a held exit, or 0.
int SV_CoF_ExitWaiter( void );
// The game's "CHANGE LEVEL:" console line, repeated every frame of a hold.
qboolean SV_CoF_ExitQuietAlert( const char *text );
#endif
