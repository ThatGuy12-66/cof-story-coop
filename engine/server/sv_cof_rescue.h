/* Cry of Fear story co-op: ways back from soft-locks the retail game makes with more than one player
 * (sv_cof_rescue.c). GPL-3.0-or-later. */
#ifndef SV_COF_RESCUE_H
#define SV_COF_RESCUE_H
void SV_CoF_RescueReset( void );
void SV_CoF_RescueFrame( void );
// SV_ExecuteClientCommand, before the game: observes (padlock turns), never consumes (false).
qboolean SV_CoF_RescueClientCommand( sv_client_t *cl );
// SV_RunThink: the player who is "the first player" while this entity thinks, or NULL (the usual one).
edict_t *SV_CoF_RescueThinkFirst( edict_t *thinker );
#endif
