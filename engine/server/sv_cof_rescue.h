/* Cry of Fear story co-op: ways back from soft-locks the retail game makes with more than one player
 * (sv_cof_rescue.c). GPL-3.0-or-later. */
#ifndef SV_COF_RESCUE_H
#define SV_COF_RESCUE_H
void SV_CoF_RescueReset( void );
void SV_CoF_RescueFrame( void );
// SV_ExecuteClientCommand, before the game: observes (padlock turns), never consumes (false).
qboolean SV_CoF_RescueClientCommand( sv_client_t *cl );
// While this entity thinks: the player who is "the first player", and the edict that answers INDEXENT( 1 ), or NULL
// (the usual one). SV_RunThink calls Begin before the game's think and End after it.
edict_t *SV_CoF_RescueThinkFirst( edict_t *thinker );
edict_t *SV_CoF_RescueThinkSlotOne( edict_t *thinker );
void SV_CoF_RescueThinkBegin( edict_t *thinker );
void SV_CoF_RescueThinkEnd( void );
// SV_DropClient: no boss keeps a raw pointer to the player who leaves.
void SV_CoF_RescueDrop( sv_client_t *cl );
#endif
