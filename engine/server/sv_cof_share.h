/* Opt-in Cry of Fear story co-op: shared story items. GPL-3.0-or-later. */
#ifndef SV_COF_SHARE_H
#define SV_COF_SHARE_H
#include "sv_cof_share_state.h"
void SV_CoF_ShareInit( void );
void SV_CoF_ShareReset( void );
void SV_CoF_ShareFrame( void );
void SV_CoF_ShareRecovered( sv_client_t *cl );
void SV_CoF_ShareRemember( void );
void SV_CoF_ShareExport( cof_share_archive_t *archive );
qboolean SV_CoF_ShareImport( const cof_share_archive_t *archive );
// Around the game DLL's ClientCommand, so its inventory changes are attributed exactly.
void SV_CoF_ShareCommandBegin( sv_client_t *cl );
void SV_CoF_ShareCommandEnd( sv_client_t *cl );
#endif
