/* Opt-in Cry of Fear story co-op: what a door's +use met (story log, cvar cof_use_probe, default 0).
 * GPL-3.0-or-later. Observes only; nothing here changes what the game sees. */
#ifndef SV_COF_USEPROBE_H
#define SV_COF_USEPROBE_H
// Once per server frame, before anything moves a player.
void SV_CoF_UseProbeFrame( void );
#endif
