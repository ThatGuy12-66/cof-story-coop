/* Cry of Fear story co-op: per-player map pickups (sv_cof_pickups.c; the rules: sv_cof_pickups_state.h).
 * GPL-3.0-or-later. */
#ifndef SV_COF_PICKUPS_H
#define SV_COF_PICKUPS_H
void SV_CoF_PickupsInit( void );
// A new session (SV_CoF_TransitionReset clearing its history) and the story layer's shutdown: nobody took anything.
void SV_CoF_PickupsReset( void );
// The story frame, after the world moved and before the AI companions' commands: takes settled, copies made.
void SV_CoF_PickupsFrame( void );
// SV_RunCmd, after the game's PostThink, for every player (AI companions too): what this command took.
void SV_CoF_PickupsCommand( sv_client_t *cl );
// SV_AddEntitiesToPacket: false for a copy the viewing player has taken theirs of (it is not sent to them).
qboolean SV_CoF_PickupsVisible( const edict_t *viewer, const edict_t *ent );
// pfnFindEntityInSphere while a player's packet or an AI companion's command runs: true for a map pickup that player
// must not +use (they took theirs, or it is an AI companion).
qboolean SV_CoF_PickupsBlocked( const edict_t *user, const edict_t *ent );
// A co-op checkpoint was written to, or loaded from, this slot (sv_cof_session.inc).
void SV_CoF_PickupsCheckpointSaved( const char *slot );
void SV_CoF_PickupsCheckpointLoaded( const char *slot );
#endif
