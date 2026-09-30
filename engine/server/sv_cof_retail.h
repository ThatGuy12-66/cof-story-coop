/* Opt-in Cry of Fear story co-op: the retail game DLL's inventory, commands
 * and user messages. GPL-3.0-or-later. */
#ifndef SV_COF_RETAIL_H
#define SV_COF_RETAIL_H
#define COF_RETAIL_SLOTS 6 // usable inventory slots 1..6 (the game never uses index 0)
#define COF_RETAIL_ITEM 64 // descriptor path buffer

void SV_CoF_RetailInit( void );
// The descriptors in usable slots 1..6 ("" when empty); false without a verified, spawned player.
qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] );
// Empty usable slots (only 1..3 count in the game's three-slot mode), or -1 without a verified player.
int SV_CoF_RetailFreeSlots( sv_client_t *cl );
// Hand a story item over through the game's own pickup (KeyitemUse -> AddInventoryItem).
qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor );
// Remove one story item through the game's own inventorydeletenamed command.
qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *descriptor );
// The descriptor's nice_name ("Hall key"); "item" and false when unknown.
qboolean SV_CoF_RetailItemName( const char *descriptor, char *out, size_t size );
// A line in the game's own message strip: colour 0/3 yellow, 1 azure, 2/6 red, 4 green, 5 grey.
qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable );
// The game's ScreenFade: seconds go out as x4096 shorts; flags 0 fades in from the colour.
qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a );
#endif
