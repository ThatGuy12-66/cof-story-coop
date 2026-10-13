#ifndef SV_COF_STATE_H
#define SV_COF_STATE_H
#include "sv_cof_state_data.h"
void SV_CoF_StateReset( void );
void SV_CoF_StateActivate( void );
void SV_CoF_StateFrame( void );
void SV_CoF_StateCommandBegin( sv_client_t *cl );
void SV_CoF_StateCommandEnd( sv_client_t *cl );
void SV_CoF_StateMessage( edict_t *entity, const char *name, byte *payload, int size );
void SV_CoF_StateExport( cof_state_data_t *data );
qboolean SV_CoF_StateImport( const cof_state_data_t *data );
#endif
