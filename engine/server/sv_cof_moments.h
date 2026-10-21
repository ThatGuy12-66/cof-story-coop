/* Cry of Fear story co-op: story moments the retail game gets wrong with two
 * players. GPL-3.0-or-later. Research: docs/research/2026-10-12-story-moments-re.md. */
#ifndef SV_COF_MOMENTS_H
#define SV_COF_MOMENTS_H
void SV_CoF_MomentsFrame( void );
void SV_CoF_MomentsReset( void );
// SV_RunCmd, before the game sees the command: may take +use away.
void SV_CoF_MomentsCommand( sv_client_t *cl, usercmd_t *cmd );
// SV_TouchLinks: true when the story layer handled this trigger touch itself.
qboolean SV_CoF_MomentsTriggerTouch( edict_t *trigger, edict_t *ent );
// SV_LinkEdict: a living player non-solid only because it stands stacked on a
// living partner still touches the triggers it stands in.
qboolean SV_CoF_MomentsStackedTouch( const edict_t *ent );
// SV_DropClient: nothing may keep a pointer to the player who leaves.
void SV_CoF_MomentsDrop( sv_client_t *cl );
// Client commands the story layer answers or refuses before the game: true when handled.
qboolean SV_CoF_MomentsClientCommand( sv_client_t *cl );
// A user message the game sent (variable-size ones, as SV_CoF_StateMessage).
void SV_CoF_MomentsMessage( edict_t *to, const char *name, const byte *payload, int size );
// Before a level change is captured: every boat rider is let go as Disembark does, without the move (1.1.8).
void SV_CoF_MomentsReleaseRiders( void );
#endif
