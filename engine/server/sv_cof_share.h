/* Opt-in Cry of Fear story co-op: shared story items. GPL-3.0-or-later. */
#ifndef SV_COF_SHARE_H
#define SV_COF_SHARE_H
void SV_CoF_ShareInit( void );
void SV_CoF_ShareReset( void );
void SV_CoF_ShareFrame( void );
// Around the game DLL's ClientCommand, so its inventory changes are attributed exactly.
void SV_CoF_ShareCommandBegin( sv_client_t *cl );
void SV_CoF_ShareCommandEnd( sv_client_t *cl );
#endif
