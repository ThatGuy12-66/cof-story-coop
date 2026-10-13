#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_cheats.h"
#include "sv_cof_recovery.h"

static TYPEDESCRIPTION cof_inventory_fields[] =
{
	{ FIELD_STRING, "inventorySlot", 0x21FC, 7, 0 },
	{ FIELD_STRING, "inventorySlotModels", 0x2218, 7, 0 },
	{ FIELD_STRING, "inventorySlotClassname", 0x2234, 7, 0 },
	{ FIELD_CLASSPTR, "m_rgpPlayerItems", 0x2514, 6, 0 },
	{ FIELD_CLASSPTR, "m_pActiveItem", 0x252C, 1, 0 },
	{ FIELD_CLASSPTR, "m_pLastItem", 0x2534, 1, 0 },
	{ FIELD_CLASSPTR, "m_pNextItem", 0x2538, 1, 0 },
	{ FIELD_CLASSPTR, "m_pStoredActionItem", 0x253C, 1, 0 },
	{ FIELD_CLASSPTR, "m_pStoredNightVisionWeapon", 0x3F0, 1, 0 },
	{ FIELD_INTEGER, "m_rgAmmo", 0x2540, 32, 0 },
	{ FIELD_INTEGER, "m_iQuickSlotSlot", 0x3D8, 4, 0 },
	{ FIELD_CHARACTER, "m_sQuickSel1", 0x204C, 128, 0 },
	{ FIELD_CHARACTER, "m_sQuickSel2", 0x20CC, 128, 0 },
	{ FIELD_CHARACTER, "m_sQuickSel3", 0x214C, 128, 0 },
	{ FIELD_INTEGER, "m_iTapes", 0x1EF4, 1, 0 },
	{ FIELD_BOOLEAN, "m_bThreeSlots", 0x1E9C, 1, 0 },
	{ FIELD_BOOLEAN, "m_bHasSaved", 0x21D8, 1, 0 },
	{ FIELD_CHARACTER, "documents", 0x7A8, 4608, 0 },
	{ FIELD_INTEGER, "m_iGoodPoints", 0x5F0, 1, 0 },
	{ FIELD_STRING, "m_sObjectiveText", 0x1E90, 1, 0 },
	{ FIELD_STRING, "m_sCustomObjectives", 0x420, 1, 0 },
	{ FIELD_INTEGER, "m_iKeypadNumber", 0x2040, 1, 0 },
	{ FIELD_INTEGER, "m_iKeypadType", 0x2044, 1, 0 },
	{ FIELD_INTEGER, "m_iPadlockNumber", 0x2358, 5, 0 },
	{ FIELD_BOOLEAN, "m_b_Ending_RoofBossKilled", 0x4CD, 1, 0 },
	{ FIELD_BOOLEAN, "m_b_Ending_GivenP345", 0x4CE, 1, 0 },
	{ FIELD_BOOLEAN, "m_b_Ending_DeliveredPackage", 0x4CF, 1, 0 },
	{ FIELD_INTEGER, "m_iDualWeaponLeftAmmo", 0x5DC, 1, 0 },
	{ FIELD_INTEGER, "m_iDualWeaponRightAmmo", 0x5E0, 1, 0 },
	{ FIELD_INTEGER, "m_iDualWeaponLeftID", 0x1E70, 1, 0 },
	{ FIELD_INTEGER, "m_iDualWeaponRightID", 0x1E74, 1, 0 },
	{ FIELD_STRING, "m_sDualWieldViewModel", 0x624, 1, 0 },
	{ FIELD_INTEGER, "m_bMirroredDualWield", 0x62C, 1, 0 },
	{ FIELD_INTEGER, "m_iOwnedWeaponsList", 0x444, 32, 0 },
	{ FIELD_BOOLEAN, "m_bFirstDeploy", 0x3F4, 33, 0 },
	{ FIELD_BOOLEAN, "m_bHasNightVision", 0x434, 1, 0 },
	{ FIELD_INTEGER, "m_iFlashBattery", 0x22A8, 1, 0 },
	{ FIELD_FLOAT, "m_fStamina", 0x21F0, 1, 0 },
	{ FIELD_INTEGER, "m_iPhoneDisabled", 0x1F14, 1, 0 },
	{ FIELD_STRING, "transferredentity_message", 0x19A8, 51, 0 },
	{ FIELD_VECTOR, "transferredentity_origin", 0x1A74, 51, 0 },
	{ FIELD_STRING, "transferredentity_classname", 0x1CD8, 51, 0 },
	{ FIELD_INTEGER, "transferredentity_clip", 0x1DA4, 51, 0 },
	{ FIELD_CHARACTER, "playerstats", 0x638, 80, 0 },
};

qboolean SV_CoF_RetailInventoryIO( sv_client_t *cl, SAVERESTOREDATA *data, qboolean restore )
{
	byte *player;
	if( !cl || !data || !SV_CoF_StoryCoopEnabled() || !SV_IsValidEdict( cl->edict )) return false;
	player = SV_CoF_StoryEntityPrivate( cl->edict, 0x281C );
	if( !player ) return false;
	if( restore ) svgame.dllFuncs.pfnSaveReadFields( data, "COFINVENTORY", player, cof_inventory_fields, ARRAYSIZE( cof_inventory_fields ));
	else svgame.dllFuncs.pfnSaveWriteFields( data, "COFINVENTORY", player, cof_inventory_fields, ARRAYSIZE( cof_inventory_fields ));
	return true;
}

qboolean SV_CoF_RetailOwnsWeapon( edict_t *player, edict_t *entity )
{
	byte *weapon, *owner;
	if( !SV_IsValidEdict( player ) || !SV_IsValidEdict( entity ) || FBitSet( entity->v.flags, FL_KILLME ) ||
		Q_strncmp( SV_GetString( entity->v.classname ), "weapon_", 7 )) return false;
	weapon = SV_CoF_StoryEntityPrivate( entity, 0x168 );
	if( !weapon ) return false;
	memcpy( &owner, weapon + 0x160, sizeof( owner ));
	return owner && owner == player->pvPrivateData;
}

void SV_CoF_RetailInventoryReady( sv_client_t *cl )
{
	byte *player = SV_CoF_StoryEntityPrivate( cl->edict, 0x281C );
	void *active;
	int index;
	if( !player ) return;
	player[0x24CC] = 1;
	cl->edict->v.weaponanim = 0;
	if( cl->state != cs_spawned ) return;
	memcpy( &active, player + 0x252C, sizeof( active ));
	for( index = svs.maxclients + 1; active && index < svgame.numEntities; index++ )
	{
		edict_t *weapon = SV_EdictNum( index );
		if( SV_IsValidEdict( weapon ) && weapon->pvPrivateData == active && SV_CoF_RetailOwnsWeapon( cl->edict, weapon ))
		{
			SV_CoF_StoryDeployWeapon( active );
			break;
		}
	}
}

void SV_CoF_RetailSessionReady( sv_client_t *cl, const cof_story_values_t *values )
{
	byte *player = SV_CoF_StoryEntityPrivate( cl->edict, 0x281C );
	string_t text;
	if( !player || !values ) return;
	text = SV_AllocString( values->objective_file );
	memcpy( player + 0x420, &text, sizeof( text ));
	memset( player + 0x1E8C, 0xFF, sizeof( string_t ));
	player[0x4E8] = 0;
	memset( player + 0x4F0, 0, sizeof( int ));
	memset( player + 0x1F24, 0, sizeof( float ));
}
