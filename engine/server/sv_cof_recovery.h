#ifndef SV_COF_RECOVERY_H
#define SV_COF_RECOVERY_H
#include "sv_cof_state_data.h"
void SV_CoF_RetailSessionReady( sv_client_t *cl, const cof_story_values_t *values );
qboolean SV_CoF_RetailInventoryIO( sv_client_t *cl, SAVERESTOREDATA *data, qboolean restore );
qboolean SV_CoF_RetailOwnsWeapon( edict_t *player, edict_t *entity );
void SV_CoF_RetailInventoryReady( sv_client_t *cl );
qboolean SV_CoF_CaptureInventory( sv_client_t *cl, byte **output, size_t *size );
qboolean SV_CoF_RestoreInventory( sv_client_t *cl, const byte *snapshot, size_t size );
void SV_CoF_RecoveryInit( void );
void SV_CoF_RecoveryReset( void );
void SV_CoF_RecoveryFrame( void );
void SV_CoF_RecoveryDrop( sv_client_t *cl );
void SV_CoF_RecoveryDropDone( sv_client_t *cl );
void SV_CoF_RecoveryJoin( sv_client_t *cl );
// The inventory a death or a rejoin took is still owed to this player.
qboolean SV_CoF_RecoveryPending( sv_client_t *cl );
qboolean SV_CoF_RecoveryExport( int index, char name[32], const byte **snapshot, size_t *size );
qboolean SV_CoF_RecoveryImport( const char *name, const byte *snapshot, size_t size );
#endif
