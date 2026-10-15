/*
sv_cof_inventory12.h - Cry of Fear story co-op: the 12-slot inventory, server side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md
*/
#ifndef SV_COF_INVENTORY12_H
#define SV_COF_INVENTORY12_H

#define COF_INV12_USERINFO "_cofinv" // a client with the 12-slot screen sends "12"

void SV_CoF_Inventory12Init( void );
// sv_init.c SV_ActivateServer, before the game's ServerActivate: this map's
// slot count, and the hl.dll bytes that go with it.
void SV_CoF_Inventory12Activate( void );
// The story gate closes (story co-op off, the game DLL going away): stock bytes.
void SV_CoF_Inventory12Close( void );
// Story frame: tells every person connected the map's slot count, once per map.
void SV_CoF_Inventory12Frame( void );
// 12 while this map runs the 12-slot inventory, else 6 (the retail inventory).
int SV_CoF_InventorySlots( void );
// SV_ConnectClient: true when this client must be turned away (a map at 12 and
// no 12-slot screen: its client.dll would crash on the first slot 7 message).
qboolean SV_CoF_Inventory12Refuse( const char *userinfo, qboolean local );
// SV_ExecuteClientCommand: an inventory command whose slot number is out of
// range is dropped (the retail handlers do not check). True when consumed.
qboolean SV_CoF_Inventory12ClientCommand( sv_client_t *cl );

#endif // SV_COF_INVENTORY12_H
