/* Opt-in Cry of Fear story party coordination. GPL-3.0-or-later. */
#ifndef SV_COF_PARTY_H
#define SV_COF_PARTY_H
void SV_CoF_PartyPlayerReady( sv_client_t *cl, qboolean fresh );
void SV_CoF_PartyNoteOrigin( edict_t *ent, const vec3_t previous );
void SV_CoF_PartyTransitionCapture( unsigned int epoch, const vec3_t landmark, const int *exit_models, int exit_count );
void SV_CoF_PartyTransitionArrive( unsigned int epoch );
void SV_CoF_PartyFrame( void );
void SV_CoF_PartyShutdown( void );
void SV_CoF_PartyInit( void );
// Standing in one of these exit brush models, or in a trigger that fires one.
qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count );
// Something can still ask for this exit by touch: its own trigger_changelevel
// (not use-only), or a live trigger that fires it by name.
qboolean SV_CoF_PartyExitRefires( const int *models, int count );
// Place an AI companion out of the leader's sight, near exit_at (a door exit) or behind them,
// rank rows back, facing their way; false when no spot is out of sight (anywhere: the least seen).
qboolean SV_CoF_PartyPlaceUnseen( sv_client_t *mover, edict_t *leader, const vec3_t exit_at, int rank, qboolean anywhere );
// Going through an inter_door (the game holds the player meanwhile).
qboolean SV_CoF_PartyInDoor( const sv_client_t *cl );
// The ending door holds a partner (frozen, blacked out): saves wait for it.
qboolean SV_CoF_PartyHolding( void );
// cof_warp and other commands the story layer handles itself; true when handled.
qboolean SV_CoF_PartyClientCommand( sv_client_t *cl );
#endif
