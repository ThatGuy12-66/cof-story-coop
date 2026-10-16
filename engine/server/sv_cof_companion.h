/* Cry of Fear story co-op: the AI companion. Decisions in sv_cof_brain.h.
 * GPL-3.0-or-later. */
#ifndef SV_COF_COMPANION_H
#define SV_COF_COMPANION_H
void SV_CoF_CompanionInit( void );
void SV_CoF_CompanionFrame( void );
void SV_CoF_CompanionReset( void );
// SV_RunCmd, for a human client: hands the controls to the AI while nobody touches them.
void SV_CoF_CompanionUsercmd( sv_client_t *cl, usercmd_t *cmd );
// A person wants to join a full server: let a companion bot go. True when a slot was freed.
qboolean SV_CoF_CompanionMakeRoom( void );
void SV_CoF_CompanionDrop( sv_client_t *cl );
// cof_companion_order (bind h): every companion waits here, or follows again. True when handled.
qboolean SV_CoF_CompanionClientCommand( sv_client_t *cl );
// This client is a companion the AI is driving right now (a bot, or an idle second player).
qboolean SV_CoF_CompanionDriven( const sv_client_t *cl );
// The person this companion bot looks after (userid; 0: none, or not a bot).
int SV_CoF_CompanionLeaderUserid( const sv_client_t *cl );
// A person's client the AI may drive (solo testing, cof_companion_drive): not the
// one the companions follow, and nobody at its controls for the idle time. Such
// a client does not hold a level exit; it comes along like a companion bot.
qboolean SV_CoF_CompanionAway( const sv_client_t *cl );
// SV_TouchLinks: true when a companion bot must not touch this trigger (anything
// but push, gravity and a hurt volume that fires nothing: story scripts, exits
// and items are the people's).
qboolean SV_CoF_CompanionTriggerTouch( edict_t *trigger, edict_t *ent );
#endif
