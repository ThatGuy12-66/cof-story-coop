/* Opt-in Cry of Fear story party coordination. GPL-3.0-or-later. */
#ifndef SV_COF_PARTY_H
#define SV_COF_PARTY_H
void SV_CoF_PartyPlayerReady( sv_client_t *cl, qboolean fresh );
void SV_CoF_PartyNoteOrigin( edict_t *ent, const vec3_t previous );
void SV_CoF_PartyTransitionCapture( unsigned int epoch, const vec3_t landmark, const int *exit_models, int exit_count );
void SV_CoF_PartyTransitionArrive( unsigned int epoch );
void SV_CoF_PartyFrame( void );
void SV_CoF_PartyShutdown( void );
// Standing in one of these exit brush models, or in a trigger that fires one.
qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count );
// cof_warp and other commands the story layer handles itself; true when handled.
qboolean SV_CoF_PartyClientCommand( sv_client_t *cl );
#endif
