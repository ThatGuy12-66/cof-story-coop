/* Cry of Fear story co-op: cutscene watchdog. Rules in sv_cof_scene_state.h.
 * GPL-3.0-or-later. */
#ifndef SV_COF_SCENE_H
#define SV_COF_SCENE_H
void SV_CoF_SceneFrame( void );
// A map change: release everyone from cameras that will not exist there.
void SV_CoF_SceneSettle( const char *reason );
// A running camera holds the party (saves wait: its freeze must never be saved).
qboolean SV_CoF_SceneActive( void );
// userid is watching the same running camera as owner_userid: a scripted move
// of the owner takes them along, however far apart their bodies are.
qboolean SV_CoF_SceneWatching( int owner_userid, int userid );
void SV_CoF_SceneReset( void );
void SV_CoF_SceneInit( void );
// cof_skip (Enter): any person skips the running cutscene for the party, again: watch it.
qboolean SV_CoF_SceneClientCommand( sv_client_t *cl );
// Physics steps the server runs this frame: cof_skip_speed while a skipped scene
// holds every living person, else 1.
int SV_CoF_SceneSkipSteps( void );
#endif
