/* Opt-in Cry of Fear story co-op adapter. GPL-3.0-or-later. */
#ifndef SV_COF_STORY_H
#define SV_COF_STORY_H
qboolean SV_CoF_StoryCoopEnabled( void );
void SV_CoF_StoryInit( void );
void SV_CoF_StoryShutdown( void );
void SV_CoF_StoryFrame( void );
void SV_CoF_StoryUserinfo( const sv_client_t *cl, char *info, size_t size );
#endif
