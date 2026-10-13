#ifndef SV_COF_SESSION_H
#define SV_COF_SESSION_H
void SV_CoF_SessionInit( void );
void SV_CoF_SessionReset( void );
void SV_CoF_SessionFrame( void );
qboolean SV_CoF_SessionSave( const char *slot );
qboolean SV_CoF_SessionLoad( const char *slot );
qboolean SV_CoF_SessionClientCommand( sv_client_t *cl );
qboolean SV_CoF_SessionWipe( const char *map, const char *landmark );
#endif
