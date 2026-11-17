/* Cry of Fear story co-op: the one place that knows the retail game DLL's
 * inventory layout, commands and user messages. GPL-3.0-or-later.
 * Everything here is behind the retail DLL hash gate; the layout and paths are
 * documented in docs/research/2026-09-30-hl-inventory-re.md (project repo). */
#include "common.h"
#include "server.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_descriptor.h"
#include "sv_cof_retail.h"
#include "sv_cof_light.h"
#include "cof_strip.h"
#include "sv_cof_inventory12.h"

#define COF_RETAIL_INVENTORY   0x21FC // string_t inventorySlot[], slots 1..12 in place (AddInventoryItem 100DAD40; sv_cof_inventory12_patch.h)
#define COF_RETAIL_THREE_SLOTS 0x1E9C // bool m_bThreeSlots: only slots 1..3 usable
#define COF_RETAIL_PLAYER_NEED ( COF_RETAIL_INVENTORY + ( COF_RETAIL_SLOTS + 1 ) * sizeof( string_t ))
#define COF_RETAIL_CACHE       128
#define COF_RETAIL_PRESSED     0x22B0 // int m_afButtonPressed: the buttons newly down in this PreThink (player.h order, docs/research/2026-10-12-companion-weapons-items-re.md)
#define COF_RETAIL_PRESSED_USE ( 1 << 5 ) // IN_USE

// Documents (docs/research/2026-09-30-hl-documents-story-state-re.md, project repo)
#define COF_RETAIL_PLAYER_SIZE 0x281C // the whole CBasePlayer
#define COF_RETAIL_DOCUMENT    0x0630 // EHANDLE m_hDocument: the cof_document being read
#define COF_RETAIL_DOC_SLOTS   0x0688 // documentsstruct: entry i (1..16) at +0x688 + 0x120 * i
#define COF_RETAIL_DOC_SIZE    0x120
#define COF_RETAIL_DOC_COUNT   16
// The last slot the engine fills itself: client.dll's notes array (gHUD) holds slots 1-15, and an AddNote for 16
// overwrites the members after it (iron-sight offset, fade, clothes; the game's own 16th document does it too).
#define COF_RETAIL_DOC_SAFE    15
#define COF_RETAIL_DOC_NOTE1   0x40 // entry + 0x40: note1; non-empty marks a used slot
#define COF_RETAIL_DOC_ENTITY  0x210 // sizeof( CCoFDocument )
#define COF_RETAIL_ADDDOCUMENT 0xDA6D0 // CBasePlayer::AddDocument( CCoFDocument * ), thiscall, ret 4

// The AI companion: weapons, CPR, monsters (docs/research/2026-10-12-companion-weapons-items-re.md
// and 2026-10-12-companion-bots-cpr-monsters-re.md, project repo)
#define COF_RETAIL_NEXT_ATTACK    0x0308 // float m_flNextAttack: a holster or deploy under way
#define COF_RETAIL_WHEELCHAIR     0x0390 // EHANDLE m_hWheelchair: riding the c_ending4b chair (player-state RE)
#define COF_RETAIL_ATTACHED_CAMERA 0x0388 // EHANDLE m_pAttachedCamera
#define COF_RETAIL_BOAT           0x0380 // EHANDLE m_pBoat: rowing the c_lake boat (player.h order: m_hTelescope +0x370, m_pPuzzleBar, m_pBoat, m_pAttachedCamera +0x388)
#define COF_RETAIL_FLASH_FLAGS    0x05D0 // int m_iFlashlightFlags: 1 phone light, 8 flashlight, 16 lit flare/book laser...
#define COF_RETAIL_NV_NEXT        0x03EC // float m_flNextNightVisionTime
#define COF_RETAIL_HAS_NV         0x0434 // bool m_bHasNightVision: picked up the bonus room's item_nightvision
#define COF_RETAIL_OUTFIT         0x0424 // int m_iUnlockedClothes: the outfit worn (setclothes, client.cpp:1566)
#define COF_RETAIL_VIEW_ENT       0x2038 // int m_iViewEntIndex
#define COF_RETAIL_HEADSHIELD     0x2300 // int m_iHeadShieldOn: the night vision (sv_cof_light.h)
#define COF_RETAIL_BLUR           0x1EBC // int m_iBlurActive: the story's screen blur, flipped by cof_blur (CCofBlur::Use 100A5BB0)
#define COF_RETAIL_TRIPPING       0x2278 // bool m_bIsTripping: a morphine trip, the blur on for its 30 s (MorphineTrip 100E8D70)
#define COF_RETAIL_CAMERA         0x2348 // EHANDLE m_hCurrentCamera
#define COF_RETAIL_RESTART        0x04E8 // a team-wipe restart is pending
#define COF_RETAIL_SEQUENCE_END   0x04F4 // float m_flSequenceOverrideTime: the game's own body animation runs until then
#define COF_RETAIL_GETUP_END      0x04FC // float: when the CPR's get-up animation ends
#define COF_RETAIL_GIBBED         0x0501
#define COF_RETAIL_FALL_FREEZE    0x1ED0 // bool m_bShouldFallFreeze: held down after a hard landing
#define COF_RETAIL_FALL_TIME      0x1ECC // float m_fFallingTime: the get-up comes after it
#define COF_RETAIL_WAITING_FALL   0x050C // bool m_bWaitingToFall: a taller's knock-down until its get-up (RevivePlayer clears +0x50C)
#define COF_RETAIL_TRIP_TIME      0x2274 // float m_fMorphineTripTime: another syringe before it starts the trip (CSyringe::Inject 1007AADC)
#define COF_RETAIL_NIGHTMARE      0x1EE0 // bool m_bNightmare: the hardest difficulty's stamina (skillset 4 sets it, 10019E02)
#define COF_RETAIL_IN_CALL        0x1EE8 // bool m_bInPhoneCall: equip and quick-select refused
#define COF_RETAIL_DEFER_DEPLOY   0x1EF9 // bool m_bWeaponWaitingToDeploy: a switch under way
#define COF_RETAIL_INFINITE_AMMO  0x21EA // bool m_bInfiniteAmmo: every firing path skips the clip and syringe count
#define COF_RETAIL_ACTIVE_ITEM    0x252C // CBasePlayerItem *m_pActiveItem
#define COF_RETAIL_AMMO           0x2540 // int m_rgAmmo[32]
#define COF_RETAIL_AMMO_TYPES     32
#define COF_RETAIL_TAPES          0x1EF4 // int m_iTapes: tape saves left (the hardest difficulty)
#define COF_RETAIL_HUD_INITIALIZED 0x24D0 // BOOL m_fGameHUDInitialized: UpdateClientData runs the rules' InitHUD while 0 (100F76A4)
#define COF_RETAIL_CPR_TARGET     0x2698 // EHANDLE: the body I am reviving
#define COF_RETAIL_REVIVER        0x26A0 // EHANDLE: who is reviving me
#define COF_RETAIL_SELECT_ITEM    0xF1A50 // CBasePlayer::SelectItem( const char * ), thiscall, ret 4
#define COF_RETAIL_NEXT_ITEM      0x2538 // CBasePlayerItem *m_pNextItem: a switch under way
#define COF_RETAIL_DUAL_LEFT      0x1E70 // int m_iDualWeaponLeftID, set by StartDualWield
#define COF_RETAIL_DUAL_RIGHT     0x1E74 // int m_iDualWeaponRightID
#define COF_RETAIL_SWITCH_WEAPON  0xF5E50 // CBasePlayer::SwitchWeapon( CBasePlayerItem * ), thiscall, ret 4
#define COF_RETAIL_DUAL_ITEM_LEFT  0x204 // weapon_dualweapon: m_iLeftWeaponID
#define COF_RETAIL_DUAL_ITEM_RIGHT 0x200 // m_iRightWeaponID
#define COF_RETAIL_DUAL_ITEM_NEED  0x208
#define COF_RETAIL_DUAL_BUCKET    0x2518 // m_rgpPlayerItems[1] (+0x2514 + 4): weapon_dualweapon's iItemSlot is 1 (10037D10); AddPlayerItem puts the newest first (100DAFD3)
#define COF_RETAIL_CARRIED        24
#define COF_RETAIL_WEAPON_SCOPE   0x106 // rifle: bool m_bRaised
#define COF_RETAIL_WEAPON_SUICIDE 0x140 // int m_iSuicideState: 2 = the struggle
#define COF_RETAIL_WEAPON_PLAYER  0x160 // CBasePlayer *m_pPlayer
#define COF_RETAIL_WEAPON_ID      0x168 // int m_iId
#define COF_RETAIL_WEAPON_SIGHTS  0x178 // bool m_bIronSights
#define COF_RETAIL_WEAPON_NEXT_PRIMARY   0x1CC // float, absolute time
#define COF_RETAIL_WEAPON_NEXT_SECONDARY 0x1D0
#define COF_RETAIL_WEAPON_AMMO_TYPE 0x1DC // int m_iPrimaryAmmoType: index into m_rgAmmo
#define COF_RETAIL_WEAPON_CLIP    0x1E4 // int m_iClip
#define COF_RETAIL_WEAPON_IN_RELOAD 0x1F0 // int m_fInReload
#define COF_RETAIL_WEAPON_NEED    0x1F4
#define COF_RETAIL_MONSTER_ENEMY  0x184 // EHANDLE m_hEnemy
#define COF_RETAIL_MONSTER_MEMORY 0x2EC // int m_afMemory: bit 0 provoked
#define COF_RETAIL_MONSTER_CLASS  0x350 // int m_iClass (0: the class's own)
#define COF_RETAIL_MONSTER_REACT  0x354 // int m_iPlayerReact: 1 never, 4 always, other: only provoked
#define COF_RETAIL_MONSTER_NEED   0x358
#define COF_RETAIL_RELATIONS      0x217118 // int iEnemy[19][19]: row the monster's class, column the target's
#define COF_RETAIL_CLASSES        19
#define COF_RETAIL_CLASS_PLAYER   2
#define COF_RETAIL_SF_PRISONER    16
#define COF_RETAIL_SF_WAIT_SCRIPT 128

typedef struct
{
	char descriptor[COF_RETAIL_ITEM];
	char classname[32];
	char nice[COF_RETAIL_ITEM];
	qboolean valid;
} cof_retail_item_t;

static cof_retail_item_t cof_retail_items[COF_RETAIL_CACHE];
static int cof_retail_item_count;

// The allocation check behind SV_CoF_StoryEntityPrivate walks the whole game
// pool, and the companion asks about players, weapons and monsters every
// frame: each answer is kept while the edict, its allocation and the map stay
// the same (a freed edict gets a new serial number).
#define COF_RETAIL_PV_CACHE 4096
static struct
{
	const void *pv;
	size_t need;
	int serial, generation;
	qboolean ok;
} cof_retail_pv[COF_RETAIL_PV_CACHE];

static byte *CoF_RetailPrivate( edict_t *ent, size_t need )
{
	int index;
	byte *pv;
	if( !SV_IsValidEdict( ent ) || !ent->pvPrivateData ) return NULL;
	index = NUM_FOR_EDICT( ent );
	if( index < 0 || index >= COF_RETAIL_PV_CACHE ) return SV_CoF_StoryEntityPrivate( ent, need );
	if( cof_retail_pv[index].pv == ent->pvPrivateData && cof_retail_pv[index].need == need &&
		cof_retail_pv[index].serial == ent->serialnumber && cof_retail_pv[index].generation == svs.spawncount )
		return cof_retail_pv[index].ok ? (byte *)ent->pvPrivateData : NULL;
	pv = SV_CoF_StoryEntityPrivate( ent, need );
	cof_retail_pv[index].pv = ent->pvPrivateData;
	cof_retail_pv[index].need = need;
	cof_retail_pv[index].serial = ent->serialnumber;
	cof_retail_pv[index].generation = svs.spawncount;
	cof_retail_pv[index].ok = pv != NULL;
	return pv;
}

static byte *CoF_RetailPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_RETAIL_PLAYER_NEED );
}

// The same player, checked for the whole object: document and story fields sit past the inventory.
static byte *CoF_RetailStoryPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return CoF_RetailPrivate( cl->edict, COF_RETAIL_PLAYER_SIZE );
}

// A pointer read out of game memory, checked against the edict array before any use.
static edict_t *CoF_RetailEdict( edict_t *ent )
{
	size_t offset;
	if( !ent || !svgame.edicts || ent < svgame.edicts || ent >= svgame.edicts + svgame.numEntities ) return NULL;
	offset = (size_t)((byte *)ent - (byte *)svgame.edicts );
	return offset % sizeof( edict_t ) ? NULL : ent;
}

static qboolean CoF_RetailIsDocument( edict_t *ent )
{
	return SV_IsValidEdict( ent ) && !FBitSet( ent->v.flags, FL_KILLME ) &&
		!Q_strcmp( SV_GetString( ent->v.classname ), "cof_document" ) &&
		SV_CoF_StoryEntityPrivate( ent, COF_RETAIL_DOC_ENTITY ) != NULL;
}

// The map's n-th cof_document (from 1), for the probe.
static edict_t *CoF_RetailNthDocument( int n )
{
	int i;
	for( i = svs.maxclients + 1; n > 0 && i < svgame.numEntities; i++ )
		if( CoF_RetailIsDocument( SV_EdictNum( i )) && !--n ) return SV_EdictNum( i );
	return NULL;
}

// The same safe-name rule the ledger applies (sv_cof_descriptor.h), plus our buffer size.
static qboolean CoF_RetailSafeDescriptor( const char *d )
{
	return CoF_DescriptorSafe( d ) && Q_strlen( d ) < COF_RETAIL_ITEM;
}

// The descriptor file's classname and nice_name, loaded once per session.
static const cof_retail_item_t *CoF_RetailItem( const char *descriptor )
{
	cof_retail_item_t *item;
	byte *text;
	fs_offset_t size = 0;
	int i;
	if( !CoF_RetailSafeDescriptor( descriptor )) return NULL;
	for( i = 0; i < cof_retail_item_count; i++ )
		if( !Q_strcmp( cof_retail_items[i].descriptor, descriptor ))
			return cof_retail_items[i].valid ? &cof_retail_items[i] : NULL;
	if( cof_retail_item_count >= COF_RETAIL_CACHE ) return NULL;
	item = &cof_retail_items[cof_retail_item_count++];
	memset( item, 0, sizeof( *item ));
	Q_strncpy( item->descriptor, descriptor, sizeof( item->descriptor ));
	text = FS_LoadFile( descriptor, &size, false ); // zero-terminated by the filesystem
	if( !text ) return NULL;
	item->valid = CoF_DescriptorValue( (const char *)text, "classname", item->classname, sizeof( item->classname )) &&
		!Q_strncmp( item->classname, "item_", 5 );
	if( !CoF_DescriptorValue( (const char *)text, "nice_name", item->nice, sizeof( item->nice )))
		Q_strncpy( item->nice, "item", sizeof( item->nice ));
	Mem_Free( text );
	return item->valid ? item : NULL;
}

qboolean SV_CoF_RetailReadSlots( sv_client_t *cl, char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM] )
{
	byte *pv = CoF_RetailPlayer( cl );
	int i;
	if( !pv ) return false;
	for( i = 0; i < COF_RETAIL_SLOTS; i++ )
	{
		string_t id;
		memcpy( &id, pv + COF_RETAIL_INVENTORY + ( i + 1 ) * sizeof( id ), sizeof( id ));
		Q_strncpy( slots[i], id ? SV_GetString( id ) : "", COF_RETAIL_ITEM );
	}
	return true;
}

int SV_CoF_RetailFreeSlots( sv_client_t *cl )
{
	byte *pv = CoF_RetailPlayer( cl );
	int i, usable, count = 0;
	if( !pv ) return -1;
	usable = pv[COF_RETAIL_THREE_SLOTS] ? 3 : SV_CoF_InventorySlots();
	for( i = 1; i <= usable; i++ )
	{
		string_t id;
		memcpy( &id, pv + COF_RETAIL_INVENTORY + i * sizeof( id ), sizeof( id ));
		if( !id ) count++;
	}
	return count;
}

qboolean SV_CoF_RetailInThreeSlots( sv_client_t *cl )
{
	byte *pv = CoF_RetailPlayer( cl );
	return pv && pv[COF_RETAIL_THREE_SLOTS];
}

qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor )
{
	const cof_retail_item_t *item = CoF_RetailItem( descriptor );
	byte *player;
	int pressed, press;
	qboolean taken;
	if( !item || !CoF_RetailPlayer( cl )) return false;
	// 1.1.8: the taclight and the phone battery are CItems (item_glocktaclight, item_phonebattery): their MyTouch
	// takes the item only on a fresh +use press (m_afButtonPressed & IN_USE, items.cpp CGlockTacLight/CPhoneBattery),
	// which a give never was, so a partner never got either. The press is made for the give and put back after it.
	if( !( player = CoF_RetailStoryPlayer( cl ))) return SV_CoF_StoryGiveItemEx( cl, item->classname, descriptor );
	memcpy( &pressed, player + COF_RETAIL_PRESSED, sizeof( pressed ));
	press = pressed | COF_RETAIL_PRESSED_USE;
	memcpy( player + COF_RETAIL_PRESSED, &press, sizeof( press ));
	taken = SV_CoF_StoryGiveItemEx( cl, item->classname, descriptor );
	if(( player = CoF_RetailStoryPlayer( cl )) != NULL ) memcpy( player + COF_RETAIL_PRESSED, &pressed, sizeof( pressed ));
	return taken;
}

// The game's ClientCommand for the tokenized command, as this player even in a phone call or on the computer or
// phone screen, where the game drops every command (100190BE: m_bInPhoneCall +0x1EE8, m_iUsingComputerOrPhone
// +0x1EA0): both are let go around the call and put back unless the command changed them.
#define COF_RETAIL_SCREEN 0x1EA0
void SV_CoF_RetailGameCommand( sv_client_t *cl )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	byte call = 0;
	int screen = 0, none = 0, after;
	if( !svgame.dllFuncs.pfnClientCommand || !cl || !SV_IsValidEdict( cl->edict )) return;
	if( pv )
	{
		call = pv[COF_RETAIL_IN_CALL];
		memcpy( &screen, pv + COF_RETAIL_SCREEN, sizeof( screen ));
		pv[COF_RETAIL_IN_CALL] = 0;
		memcpy( pv + COF_RETAIL_SCREEN, &none, sizeof( none ));
	}
	SV_CoF_GlobalsEnter( cl );
	svgame.dllFuncs.pfnClientCommand( cl->edict );
	SV_CoF_GlobalsLeave( cl );
	if( !pv ) return;
	if( !pv[COF_RETAIL_IN_CALL] ) pv[COF_RETAIL_IN_CALL] = call;
	memcpy( &after, pv + COF_RETAIL_SCREEN, sizeof( after ));
	if( !after ) memcpy( pv + COF_RETAIL_SCREEN, &screen, sizeof( screen ));
}

qboolean SV_CoF_RetailRemoveItem( sv_client_t *cl, const char *descriptor )
{
	char text[COF_RETAIL_ITEM + 32];
	if( !CoF_RetailSafeDescriptor( descriptor ) || !CoF_RetailPlayer( cl ) || !svgame.dllFuncs.pfnClientCommand ) return false;
	// The game's own removal: ClientCommand -> DeleteInventoryItemNamed
	// (100DDA70) clears the first exact match and sends Inventory, so the
	// client's copy of the slot stays in sync. A direct slot write would not.
	Q_snprintf( text, sizeof( text ), "inventorydeletenamed \"%s\" 0", descriptor );
	Cmd_TokenizeString( text );
	SV_CoF_RetailGameCommand( cl );
	return true;
}

qboolean SV_CoF_RetailItemName( const char *descriptor, char *out, size_t size )
{
	const cof_retail_item_t *item = CoF_RetailItem( descriptor );
	Q_strncpy( out, item ? item->nice : "item", size );
	return item != NULL;
}

qboolean SV_CoF_RetailProFont( sv_client_t *cl, int colour, const char *text, qboolean reliable )
{
	byte payload[192]; // the largest GoldSrc user message
	int length;
	if( !cl || cl->state != cs_spawned || !text || colour < 0 || colour > 6 ) return false;
	// The client's ProFont hook replaces any line containing DEADNOTIFY with its respawn prompt.
	if( Q_strstr( text, "DEADNOTIFY" )) return false;
	// marked as the engine's own: shown although the player turned subtitles off (cof_strip.h)
	if( !( length = CoF_StripPayload( payload, sizeof( payload ), colour, text ))) return false;
	return SV_CoF_SendUserMessage( cl->edict, "ProFont", payload, length, reliable );
}

qboolean SV_CoF_RetailScreenFade( sv_client_t *cl, float duration, float hold, int flags, int r, int g, int b, int a )
{
	byte payload[10]; // "ScreenFade" is registered with size 10 (0x10226720)
	int d = (int)( duration * 4096.0f ), h = (int)( hold * 4096.0f );
	if( !cl || cl->state != cs_spawned ) return false;
	d = bound( 0, d, 0xFFFF );
	h = bound( 0, h, 0xFFFF );
	payload[0] = (byte)( d & 0xFF ); payload[1] = (byte)(( d >> 8 ) & 0xFF );
	payload[2] = (byte)( h & 0xFF ); payload[3] = (byte)(( h >> 8 ) & 0xFF );
	payload[4] = (byte)( flags & 0xFF ); payload[5] = (byte)(( flags >> 8 ) & 0xFF );
	payload[6] = (byte)r; payload[7] = (byte)g; payload[8] = (byte)b; payload[9] = (byte)a;
	return SV_CoF_SendUserMessage( cl->edict, "ScreenFade", payload, sizeof( payload ), true );
}

edict_t *SV_CoF_RetailOpenDocument( sv_client_t *cl )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	edict_t *ent;
	int serial;
	if( !pv ) return NULL;
	memcpy( &ent, pv + COF_RETAIL_DOCUMENT, sizeof( ent ));
	memcpy( &serial, pv + COF_RETAIL_DOCUMENT + sizeof( ent ), sizeof( serial ));
	ent = CoF_RetailEdict( ent );
	if( !ent || !CoF_RetailIsDocument( ent ) || ent->serialnumber != serial ) return NULL;
	return ent;
}

qboolean SV_CoF_RetailAddDocument( sv_client_t *cl, edict_t *doc )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	if( !pv || !doc || !CoF_RetailIsDocument( doc )) return false;
	return SV_CoF_StoryCallMember( COF_RETAIL_ADDDOCUMENT, pv, SV_CoF_StoryEntityPrivate( doc, COF_RETAIL_DOC_ENTITY ));
}

int SV_CoF_RetailDocumentCount( sv_client_t *cl )
{
	byte *pv = CoF_RetailStoryPlayer( cl );
	int i, count = 0;
	if( !pv ) return -1;
	for( i = 1; i <= COF_RETAIL_DOC_COUNT; i++ )
		if( pv[COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * i + COF_RETAIL_DOC_NOTE1] ) count++;
	return count;
}

// AddNote for a code note: byte slot, byte isCode = 1, short code, byte style,
// then eleven strings (note1-4, page1-4, turnsound, title, label).
// cof_addcodenote (100A6790) marks the slot with the "nerds" placeholder; the
// code itself travels only in the message.
qboolean SV_CoF_RetailCodeNote( sv_client_t *cl, int code, int style )
{
	byte payload[32], *pv = CoF_RetailStoryPlayer( cl ), *entry = NULL;
	int i, n = 0;
	if( !pv || code < 0 || code > 0xFFFF || style < 0 || style > 0xFF ) return false;
	for( i = 1; i <= COF_RETAIL_DOC_SAFE; i++ )
	{
		entry = pv + COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * i;
		if( !entry[COF_RETAIL_DOC_NOTE1] ) break;
	}
	if( i > COF_RETAIL_DOC_SAFE ) return false; // every slot the engine may fill is used
	memcpy( entry + COF_RETAIL_DOC_NOTE1, "nerds", 6 );
	payload[n++] = (byte)i;
	payload[n++] = 1;
	payload[n++] = (byte)( code & 0xFF );
	payload[n++] = (byte)( code >> 8 );
	payload[n++] = (byte)style;
	memcpy( payload + n, "nerds", 6 ); n += 6;
	memset( payload + n, 0, 10 ); n += 10; // the other ten strings are empty
	return SV_CoF_SendUserMessage( cl->edict, "AddNote", payload, n, true );
}

qboolean SV_CoF_RetailStoryRead( sv_client_t *cl, cof_story_values_t *values )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	string_t objective, objective_file;
	int index;
	if( !player || !values ) return false;
	memset( values, 0, sizeof( *values ));
	memcpy( &values->code, player + 0x2040, sizeof( int ));
	memcpy( &values->code_type, player + 0x2044, sizeof( int ));
	memcpy( values->digits, player + 0x235C, sizeof( values->digits ));
	memcpy( &objective, player + 0x1E90, sizeof( objective ));
	memcpy( &objective_file, player + 0x420, sizeof( objective_file ));
	Q_strncpy( values->objective, SV_GetString( objective ), sizeof( values->objective ));
	Q_strncpy( values->objective_file, SV_GetString( objective_file ), sizeof( values->objective_file ));
	for( index = 0; index < 3; index++ ) values->ending[index] = !!player[0x4CD + index];
	values->three_slots = !!player[COF_RETAIL_THREE_SLOTS];
	memcpy( &values->doctor_weapon, player + 0x5B0, sizeof( int ));
	memcpy( values->begin, player + 0x678, sizeof( values->begin ));
	memcpy( &values->tapes, player + COF_RETAIL_TAPES, sizeof( int ));
	return true;
}

qboolean SV_CoF_RetailStoryWrite( sv_client_t *cl, const cof_story_values_t *values )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	string_t current, replacement;
	int index;
	if( !player || !values ) return false;
	memcpy( player + 0x2040, &values->code, sizeof( int ));
	memcpy( player + 0x2044, &values->code_type, sizeof( int ));
	if( values->digits[0] ) memcpy( player + 0x235C, values->digits, sizeof( values->digits ));
	for( index = 0; index < 3; index++ ) player[0x4CD + index] = (byte)!!values->ending[index];
	if( values->doctor_weapon == 1 || values->doctor_weapon == 2 ) memcpy( player + 0x5B0, &values->doctor_weapon, sizeof( int ));
	if( values->begin[0] == 1 && player[0x678] != 1 ) memcpy( player + 0x678, values->begin, sizeof( values->begin ));
	if( values->tapes >= 0 ) memcpy( player + COF_RETAIL_TAPES, &values->tapes, sizeof( int ));
	memcpy( &current, player + 0x1E90, sizeof( current ));
	if( Q_strcmp( SV_GetString( current ), values->objective ))
	{
		replacement = SV_AllocString( values->objective );
		memcpy( player + 0x1E90, &replacement, sizeof( replacement ));
	}
	memcpy( &current, player + 0x420, sizeof( current ));
	if( Q_strcmp( SV_GetString( current ), values->objective_file ))
	{
		replacement = SV_AllocString( values->objective_file );
		memcpy( player + 0x420, &replacement, sizeof( replacement ));
		memset( player + 0x1E8C, 0xFF, sizeof( string_t ));
	}
	return true;
}

// An EHANDLE in game memory ({ edict_t *, serial number }): the entity it still names, or NULL.
static edict_t *CoF_RetailHandle( const byte *field )
{
	edict_t *ent;
	int serial;
	memcpy( &ent, field, sizeof( ent ));
	memcpy( &serial, field + sizeof( ent ), sizeof( serial ));
	if( !( ent = CoF_RetailEdict( ent )) || ent->free || ent->serialnumber != serial ) return NULL;
	return ent;
}

static qboolean CoF_RetailAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO;
}

// The weapons the companion knows how to use, by m_iId (GetItemInfo of each);
// the classname is the main one (aliases such as weapon_beretta share the id).
typedef struct
{
	const char *classname;
	int id, kind, rank, magazine;
} cof_retail_weapon_t;

static const cof_retail_weapon_t cof_retail_weapons[] =
{
	{ "weapon_m16", 9, COF_ARMS_GUN, 8, 20 },
	{ "weapon_shotgun", 4, COF_ARMS_GUN, 7, 5 },
	{ "weapon_tmp", 14, COF_ARMS_GUN, 6, 30 },
	{ "weapon_vp70", 19, COF_ARMS_GUN, 5, 18 },
	{ "weapon_revolver", 21, COF_ARMS_GUN, 4, 5 },
	{ "weapon_rifle", 11, COF_ARMS_GUN, 3, 5 }, // its sights are a scope
	{ "weapon_p345", 7, COF_ARMS_GUN, 2, 8 },
	{ "weapon_glock", 3, COF_ARMS_GUN, 1, 15 },
	{ "weapon_axe", 24, COF_ARMS_MELEE, 5, 0 },
	{ "weapon_nightstick", 12, COF_ARMS_MELEE, 4, 0 },
	{ "weapon_branch", 16, COF_ARMS_MELEE, 3, 0 },
	{ "weapon_switchblade", 6, COF_ARMS_MELEE, 2, 0 },
	{ "weapon_sledgehammer", 18, COF_ARMS_MELEE, 1, 0 }, // slow, and it needs stamina
	{ "weapon_syringe", 8, COF_ARMS_HEAL, 1, 1 }, // the count is its ammo
};

static const cof_retail_weapon_t *CoF_RetailWeaponKind( int id )
{
	int i;
	for( i = 0; i < (int)ARRAYSIZE( cof_retail_weapons ); i++ )
		if( cof_retail_weapons[i].id == id ) return &cof_retail_weapons[i];
	return NULL;
}

static const cof_retail_weapon_t *CoF_RetailWeaponNamed( const char *classname )
{
	int i;
	for( i = 0; classname && i < (int)ARRAYSIZE( cof_retail_weapons ); i++ )
		if( !Q_strcmp( cof_retail_weapons[i].classname, classname )) return &cof_retail_weapons[i];
	return NULL;
}

typedef struct
{
	edict_t *ent;
	byte *pv;
	const cof_retail_weapon_t *kind;
	int id, clip, type, reserve;
} cof_retail_carried_t;

// The weapons this player carries: every weapon entity whose m_pPlayer is this
// player's object (compared as a value, never followed).
static int CoF_RetailCarried( byte *player, cof_retail_carried_t *out, int max )
{
	int i, n = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && n < max; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		cof_retail_carried_t *w = &out[n];
		byte *owner;
		if( !SV_IsValidEdict( e ) || FBitSet( e->v.flags, FL_KILLME ) || Q_strncmp( SV_GetString( e->v.classname ), "weapon_", 7 )) continue;
		if( !( w->pv = CoF_RetailPrivate( e, COF_RETAIL_WEAPON_NEED ))) continue;
		memcpy( &owner, w->pv + COF_RETAIL_WEAPON_PLAYER, sizeof( owner ));
		if( owner != player ) continue;
		w->ent = e;
		memcpy( &w->id, w->pv + COF_RETAIL_WEAPON_ID, sizeof( w->id ));
		memcpy( &w->clip, w->pv + COF_RETAIL_WEAPON_CLIP, sizeof( w->clip ));
		memcpy( &w->type, w->pv + COF_RETAIL_WEAPON_AMMO_TYPE, sizeof( w->type ));
		w->reserve = 0;
		if( w->type > 0 && w->type < COF_RETAIL_AMMO_TYPES )
			memcpy( &w->reserve, player + COF_RETAIL_AMMO + w->type * sizeof( int ), sizeof( int ));
		w->kind = CoF_RetailWeaponKind( w->id );
		n++;
	}
	return n;
}

static float CoF_RetailFloat( const byte *field )
{
	float value;
	memcpy( &value, field, sizeof( value ));
	return value;
}

qboolean SV_CoF_RetailArms( sv_client_t *cl, cof_retail_arms_t *arms )
{
	cof_retail_carried_t carried[COF_RETAIL_CARRIED];
	byte *player = CoF_RetailStoryPlayer( cl ), *active;
	int i, n, best[4] = { 0, 0, 0, 0 };
	memset( arms, 0, sizeof( *arms ));
	if( !player ) return false;
	memcpy( &active, player + COF_RETAIL_ACTIVE_ITEM, sizeof( active ));
	n = CoF_RetailCarried( player, carried, COF_RETAIL_CARRIED );
	for( i = 0; i < n; i++ )
	{
		const cof_retail_carried_t *w = &carried[i];
		int kind = w->kind ? w->kind->kind : COF_ARMS_NONE, rounds = w->clip > 0 || w->reserve > 0;
		if( w->pv == active )
		{
			int suicide, reload;
			arms->weapon = kind == COF_ARMS_GUN && !rounds ? COF_ARMS_NONE : kind;
			arms->clip = w->clip;
			arms->reserve = w->reserve;
			arms->sights = w->pv[COF_RETAIL_WEAPON_SIGHTS] || ( w->id == 11 && w->pv[COF_RETAIL_WEAPON_SCOPE] );
			memcpy( &reload, w->pv + COF_RETAIL_WEAPON_IN_RELOAD, sizeof( reload ));
			memcpy( &suicide, w->pv + COF_RETAIL_WEAPON_SUICIDE, sizeof( suicide ));
			arms->reloading = reload != 0;
			arms->struggle = suicide == 2;
			arms->ready = !player[COF_RETAIL_DEFER_DEPLOY] && CoF_RetailFloat( player + COF_RETAIL_NEXT_ATTACK ) <= sv.time &&
				CoF_RetailFloat( w->pv + COF_RETAIL_WEAPON_NEXT_PRIMARY ) <= sv.time &&
				CoF_RetailFloat( w->pv + COF_RETAIL_WEAPON_NEXT_SECONDARY ) <= sv.time;
		}
		if( kind == COF_ARMS_NONE || ( kind != COF_ARMS_MELEE && !rounds ) || w->kind->rank <= best[kind] ) continue;
		best[kind] = w->kind->rank;
		Q_strncpy( kind == COF_ARMS_GUN ? arms->gun : kind == COF_ARMS_MELEE ? arms->melee : arms->heal,
			SV_GetString( w->ent->v.classname ), sizeof( arms->gun ));
	}
	arms->has_gun = best[COF_ARMS_GUN] > 0;
	arms->has_melee = best[COF_ARMS_MELEE] > 0;
	arms->has_heal = best[COF_ARMS_HEAL] > 0;
	return true;
}

qboolean SV_CoF_RetailEquip( sv_client_t *cl, const char *classname )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	if( !player || !classname || Q_strncmp( classname, "weapon_", 7 )) return false;
	qboolean done;
	// What inventoryequip ends in, without its menu sound: the swap happens a moment later in PostThink.
	SV_CoF_GlobalsEnter( cl ); // a dual weapon's Holster runs on the game's one dual-weapon object
	done = SV_CoF_StoryCallMember( COF_RETAIL_SELECT_ITEM, player, (void *)classname );
	SV_CoF_GlobalsLeave( cl );
	return done;
}

qboolean SV_CoF_RetailDualCommand( const char *cmd )
{
	return cmd && ( !Q_strcmp( cmd, "inventorydualwield" ) || !Q_strcmp( cmd, "quicksel" ));
}

// The player's own dual weapons that are not on their way out (FL_KILLME): the game's m_pPlayer.
static qboolean CoF_RetailOwnDual( edict_t *e, byte *player, byte **pv )
{
	byte *owner;
	if( !SV_IsValidEdict( e ) || FBitSet( e->v.flags, FL_KILLME ) || Q_strcmp( SV_GetString( e->v.classname ), "weapon_dualweapon" )) return false;
	if( !( *pv = CoF_RetailPrivate( e, COF_RETAIL_DUAL_ITEM_NEED ))) return false;
	memcpy( &owner, *pv + COF_RETAIL_WEAPON_PLAYER, sizeof( owner ));
	return owner == player;
}

// The player's own live dual weapon whose object is pv, or NULL (pv is only compared, never followed).
static byte *CoF_RetailOwnDualAt( byte *player, const byte *pv )
{
	int i;
	byte *own;
	if( !pv ) return NULL;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
		if( SV_EdictNum( i )->pvPrivateData == pv && CoF_RetailOwnDual( SV_EdictNum( i ), player, &own )) return own;
	return NULL;
}

void SV_CoF_RetailDualSeen( sv_client_t *cl, cof_retail_dual_seen_t *seen )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	memset( seen, 0, sizeof( *seen ));
	if( !player ) return;
	memcpy( &seen->head, player + COF_RETAIL_DUAL_BUCKET, sizeof( seen->head ));
	seen->valid = true;
}

// Dual wield in co-op (sv_cof_story.c opens the multiplayer refusals of the command
// and of the quick-slot double tap): the game builds weapon_dualweapon, but its
// multiplayer rules switch only to a heavier weapon (dual 1, a pistol 5), so it is
// never deployed. Switch to it here, as single player's AddPlayerItem does. Only
// one this command built: AddPlayerItem puts it first in its bucket, and the one it
// replaced (killed, still the player's and with the same pair for 0.1 s) is unlinked.
qboolean SV_CoF_RetailDualDeploy( sv_client_t *cl, const cof_retail_dual_seen_t *seen )
{
	byte *player = CoF_RetailStoryPlayer( cl ), *active, *next, *head, *pv;
	int left, right, l, r;
	if( !player || !seen || !seen->valid ) return false;
	memcpy( &head, player + COF_RETAIL_DUAL_BUCKET, sizeof( head ));
	if( !head || head == seen->head || !( pv = CoF_RetailOwnDualAt( player, head ))) return false; // none built
	memcpy( &left, player + COF_RETAIL_DUAL_LEFT, sizeof( left ));
	memcpy( &right, player + COF_RETAIL_DUAL_RIGHT, sizeof( right ));
	memcpy( &l, pv + COF_RETAIL_DUAL_ITEM_LEFT, sizeof( l ));
	memcpy( &r, pv + COF_RETAIL_DUAL_ITEM_RIGHT, sizeof( r ));
	if( l != left || r != right ) return false;
	memcpy( &active, player + COF_RETAIL_ACTIVE_ITEM, sizeof( active ));
	memcpy( &next, player + COF_RETAIL_NEXT_ITEM, sizeof( next ));
	if( pv == active || pv == next ) return false;
	SV_CoF_GlobalsEnter( cl ); // the dual weapon's Deploy runs on the game's one dual-weapon object
	SV_CoF_StoryCallMember( COF_RETAIL_SWITCH_WEAPON, player, pv );
	SV_CoF_GlobalsLeave( cl );
	// SwitchWeapon refuses a weapon that cannot deploy (on a ladder) and keeps the switch under way. When that was to
	// the dual weapon this command replaced, freed 0.1 s later, the new one takes its place (single player has the
	// same hole; here a repeated command could reach it).
	memcpy( &next, player + COF_RETAIL_NEXT_ITEM, sizeof( next ));
	if( next != pv && CoF_RetailOwnDualAt( player, next ))
	{
		memcpy( player + COF_RETAIL_NEXT_ITEM, &pv, sizeof( pv ));
		next = pv;
	}
	if( next != pv ) return false;
	Con_Printf( "cofstory phase=dualwield-switch userid=%d left=%d right=%d\n", cl->userid, left, right );
	return true;
}

// Every frame, for each player:
// - The hardest difficulty's stamina (it drains twice as fast, comes back a third as fast, and running out of breath
//   lasts 10 s, 100F5411..100F55AA): skillset sets m_bNightmare only in single player (client.cpp:1346 in the
//   decompilation), so a co-op game on difficulty 4 ran the easier stamina. Every player's follows the difficulty.
// - An AI companion never takes the morphine trip (the cof_sp_morphine_trip gate gives it back to players): it heals
//   itself every 15 s while hurt, with syringes that never run out. Its trip timer stays 0.
void SV_CoF_RetailPlayersFrame( void )
{
	byte want = (byte)( Cvar_VariableInteger( "difficulty" ) == 4 );
	float none = 0.0f;
	int i;
	if( !SV_CoF_StoryCoopEnabled()) return;
	for( i = 0; i < svs.maxclients; i++ )
	{
		byte *player = CoF_RetailStoryPlayer( &svs.clients[i] );
		if( !player ) continue;
		if( player[COF_RETAIL_NIGHTMARE] != want ) player[COF_RETAIL_NIGHTMARE] = want;
		if( FBitSet( svs.clients[i].flags, FCL_FAKECLIENT ) && !player[COF_RETAIL_TRIPPING] )
			memcpy( player + COF_RETAIL_TRIP_TIME, &none, sizeof( none ));
	}
}

qboolean SV_CoF_RetailHeld( sv_client_t *cl, int *id, int *right_hand, int *left_hand )
{
	byte *player = CoF_RetailStoryPlayer( cl ), *active, *pv;
	int i, first, second;
	if( !player || !id ) return false;
	memcpy( &active, player + COF_RETAIL_ACTIVE_ITEM, sizeof( active ));
	memcpy( &first, player + COF_RETAIL_DUAL_LEFT, sizeof( first ));
	memcpy( &second, player + COF_RETAIL_DUAL_RIGHT, sizeof( second ));
	if( !active ) return false;
	// the item's own edict, by comparing pointers: the game's pointer is never followed
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		if( !SV_IsValidEdict( e ) || e->pvPrivateData != active ) continue;
		if( Q_strncmp( SV_GetString( e->v.classname ), "weapon_", 7 ) || !( pv = CoF_RetailPrivate( e, COF_RETAIL_WEAPON_NEED ))) return false;
		memcpy( id, pv + COF_RETAIL_WEAPON_ID, sizeof( *id ));
		// StartDualWield puts a light (or the second pistol) second: in first person
		// Simon holds that one in his right hand, the other in his left (mirrored)
		if( right_hand ) *right_hand = second;
		if( left_hand ) *left_hand = first;
		return true;
	}
	return false;
}

void SV_CoF_RetailSupply( sv_client_t *cl )
{
	cof_retail_carried_t carried[COF_RETAIL_CARRIED];
	byte *player = CoF_RetailStoryPlayer( cl );
	int i, n;
	if( !player ) return;
	player[COF_RETAIL_INFINITE_AMMO] = 1; // shots and syringes are not spent
	n = CoF_RetailCarried( player, carried, COF_RETAIL_CARRIED );
	for( i = 0; i < n; i++ )
	{
		const cof_retail_carried_t *w = &carried[i];
		if( !w->kind || w->kind->kind == COF_ARMS_MELEE || w->type <= 0 || w->type >= COF_RETAIL_AMMO_TYPES ) continue;
		if( w->reserve < w->kind->magazine )
			memcpy( player + COF_RETAIL_AMMO + w->type * sizeof( int ), &w->kind->magazine, sizeof( int ));
	}
}

int SV_CoF_RetailInfiniteAmmo( sv_client_t *cl, qboolean on )
{
	byte *player = CoF_RetailStoryPlayer( cl ), was;
	if( !player || player[COF_RETAIL_INFINITE_AMMO] > 1 ) return -1; // a bool the game wrote is 0 or 1
	was = player[COF_RETAIL_INFINITE_AMMO];
	player[COF_RETAIL_INFINITE_AMMO] = on ? 1 : 0;
	return was;
}

int SV_CoF_RetailCarrying( sv_client_t *cl, const char *classname )
{
	cof_retail_carried_t carried[COF_RETAIL_CARRIED];
	const cof_retail_weapon_t *kind = CoF_RetailWeaponNamed( classname );
	byte *player = CoF_RetailStoryPlayer( cl ), *active;
	int i, n, found = 0;
	if( !player || !kind ) return -1;
	memcpy( &active, player + COF_RETAIL_ACTIVE_ITEM, sizeof( active ));
	n = CoF_RetailCarried( player, carried, COF_RETAIL_CARRIED );
	for( i = 0; i < n; i++ )
		if( carried[i].kind == kind ) found = Q_max( found, carried[i].pv == active ? 2 : 1 );
	return found;
}

qboolean SV_CoF_RetailInWheelchair( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && CoF_RetailHandle( player + COF_RETAIL_WHEELCHAIR ) != NULL;
}

qboolean SV_CoF_RetailInBoat( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	edict_t *boat;
	return player && ( boat = CoF_RetailHandle( player + COF_RETAIL_BOAT )) != NULL && !FBitSet( boat->v.flags, FL_KILLME ) &&
		!Q_strcmp( SV_GetString( boat->v.classname ), "boat" );
}

qboolean SV_CoF_RetailHudInitialized( edict_t *ent )
{
	byte *pv = CoF_RetailPrivate( ent, COF_RETAIL_PLAYER_SIZE );
	if( !pv ) return false;
	*(int *)( pv + COF_RETAIL_HUD_INITIALIZED ) = 1;
	return true;
}

int SV_CoF_RetailFlashFlags( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int flags;
	if( !player ) return -1;
	memcpy( &flags, player + COF_RETAIL_FLASH_FLAGS, sizeof( flags ));
	return flags;
}

// A trigger_camera holds the player's view: the first half of the game's IsInCutscene (100E4E00).
qboolean SV_CoF_RetailCameraHolds( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int view;
	if( !player || !CoF_RetailHandle( player + COF_RETAIL_CAMERA )) return false;
	memcpy( &view, player + COF_RETAIL_VIEW_ENT, sizeof( view ));
	return view && view != NUM_FOR_EDICT( cl->edict );
}

qboolean SV_CoF_RetailAttachedCamera( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && CoF_RetailHandle( player + COF_RETAIL_ATTACHED_CAMERA ) != NULL;
}

int SV_CoF_RetailOutfit( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int outfit;
	if( !player ) return -1;
	memcpy( &outfit, player + COF_RETAIL_OUTFIT, sizeof( outfit ));
	return outfit;
}

// The game's single-player F (hl.dll 1001C1FB): refused in a cutscene (IsInCutscene 100E4E00: a camera holds the
// view), on an attached camera or in the wheelchair; then the request in m_iHeadShieldOn.
int SV_CoF_RetailNightVision( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int view, state;
	float next;
	if( !player || !player[COF_RETAIL_HAS_NV] ) return -1;
	memcpy( &view, player + COF_RETAIL_VIEW_ENT, sizeof( view ));
	if( CoF_RetailHandle( player + COF_RETAIL_ATTACHED_CAMERA ) || CoF_RetailHandle( player + COF_RETAIL_WHEELCHAIR ) ||
		( CoF_RetailHandle( player + COF_RETAIL_CAMERA ) && view && view != NUM_FOR_EDICT( cl->edict )))
		return 0;
	memcpy( &state, player + COF_RETAIL_HEADSHIELD, sizeof( state ));
	memcpy( &next, player + COF_RETAIL_NV_NEXT, sizeof( next ));
	if(( state = CoF_NightVisionRequest( state, next, sv.time )) < 0 ) return 0;
	memcpy( player + COF_RETAIL_HEADSHIELD, &state, sizeof( state ));
	return 1;
}

qboolean SV_CoF_RetailInPhoneCall( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && player[COF_RETAIL_IN_CALL];
}

// 1.1.9 (M1 9): c_subway1st1's cof_phonedisable breaks the phone of the first player only (CCofPhoneDisable::Use,
// effects.cpp:8563-8600; docs/research/2026-10-12-endings-phone-re.md "cof_phonedisable"). The same writes for
// another player: their weapon_mobile broken, m_iPhoneDisabled (PostThink then refuses the phone light and
// UpdateClientData sends PhoneOff) and the broken body, which the phone shows at its next Deploy or ItemPostFrame.
#define COF_RETAIL_PHONE_BODY     0x05CC // int m_iPhoneMessage: the phone's body (its SMS screen, the broken parts)
#define COF_RETAIL_PHONE_DISABLED 0x1F14 // int m_iPhoneDisabled
#define COF_RETAIL_MOBILE_ID      2      // weapon_mobile's m_iId (CMobile::GetItemInfo)
#define COF_RETAIL_MOBILE_BROKEN  0x1F9  // bool CMobile::m_bDisabled: no SMS (trigger_cofmobile), no light switch
#define COF_RETAIL_MOBILE_NEED    0x1FC

int SV_CoF_RetailPhoneBroken( sv_client_t *cl, int *body )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int broken;
	if( !player ) return -1;
	memcpy( &broken, player + COF_RETAIL_PHONE_DISABLED, sizeof( broken ));
	if( body ) memcpy( body, player + COF_RETAIL_PHONE_BODY, sizeof( *body ));
	return broken != 0;
}

qboolean SV_CoF_RetailBreakPhone( sv_client_t *cl, int body )
{
	cof_retail_carried_t carried[COF_RETAIL_CARRIED];
	byte *player = CoF_RetailStoryPlayer( cl ), *mobile;
	int i, n, broken = 1;
	if( !player ) return false;
	n = CoF_RetailCarried( player, carried, COF_RETAIL_CARRIED );
	for( i = 0; i < n; i++ )
	{
		if( carried[i].id != COF_RETAIL_MOBILE_ID || Q_strcmp( SV_GetString( carried[i].ent->v.classname ), "weapon_mobile" ) ||
			!( mobile = SV_CoF_StoryEntityPrivate( carried[i].ent, COF_RETAIL_MOBILE_NEED ))) continue;
		mobile[COF_RETAIL_MOBILE_BROKEN] = 1;
		memcpy( player + COF_RETAIL_PHONE_DISABLED, &broken, sizeof( broken ));
		memcpy( player + COF_RETAIL_PHONE_BODY, &body, sizeof( body ));
		return true;
	}
	return false;
}

// 1.1.9 (M2 F10, K): m_iHealthCap, which PostThink enforces (1 holds health at 80, 2 at 66); cof_maxhealthchange
// sets it on its activator alone (effects.cpp:8997-9033), the strangle and the wheelchair clear it on one player.
#define COF_RETAIL_HEALTH_CAP 0x060C

int SV_CoF_RetailHealthCap( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int cap;
	if( !player ) return -1;
	memcpy( &cap, player + COF_RETAIL_HEALTH_CAP, sizeof( cap ));
	return cap;
}

// Set as cof_maxhealthchange does: the field, then HealthCap( byte ) to that player for their health bar.
qboolean SV_CoF_RetailSetHealthCap( sv_client_t *cl, int cap )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	byte value;
	if( !player || cap < 0 || cap > 2 ) return false;
	value = (byte)cap;
	memcpy( player + COF_RETAIL_HEALTH_CAP, &cap, sizeof( cap ));
	SV_CoF_SendUserMessage( cl->edict, "HealthCap", &value, sizeof( value ), true );
	return true;
}

// 1.1.9 (M2 F10, K): a cof_telephone keeps one user handle, its last opener (CCofTelephone::Use); a dial
// (telephoneright/wrong) acts on the dialer's own m_pTelephone but answers that user: a partner who opened it later
// got the replies, a dial after the right number fired its target again, and a user gone leaves TelephoneRight a
// handle it uses unchecked (CoF_Telephone.cpp:112-127; docs/research/2026-10-12-endings-phone-re.md "cof_telephone").
#define COF_RETAIL_TELEPHONE      0x0368 // EHANDLE m_pTelephone: the cof_telephone this player last opened
#define COF_RETAIL_TELEPHONE_USER 0x00DC // cof_telephone: EHANDLE m_pPlayer, the one its replies go to
#define COF_RETAIL_TELEPHONE_NEED 0x00E4

qboolean SV_CoF_RetailTelephoneTaken( sv_client_t *cl, edict_t **user )
{
	byte *player = CoF_RetailStoryPlayer( cl ), *phone;
	edict_t *ent, *by;
	if( user ) *user = NULL;
	if( !player || !( ent = CoF_RetailHandle( player + COF_RETAIL_TELEPHONE )) || FBitSet( ent->v.flags, FL_KILLME ) ||
		Q_strcmp( SV_GetString( ent->v.classname ), "cof_telephone" ) || !( phone = CoF_RetailPrivate( ent, COF_RETAIL_TELEPHONE_NEED )))
		return false;
	by = CoF_RetailHandle( phone + COF_RETAIL_TELEPHONE_USER );
	if( user ) *user = by;
	return by != cl->edict;
}

// 1.1.9 (K9): c_asylumb's bar puzzle. Its solving think (CPuzzleBar::BarCheck, CoF_PuzzleBars.cpp:198-219) marks the
// cof_puzzlebarbutton solved (iuser1 1) and lets only the first player out of the puzzle; a partner who had it open
// kept its view and panel. Let such a player out as the game's own "boilercheck 5" does (SET_VIEW to themselves,
// m_pPuzzleBar cleared) and close the panel (Puzzlebar 0). A handle a use after the solve set (the button only says
// it is done then, no view) is just cleared. True when a view was given back.
#define COF_RETAIL_PUZZLE_BAR 0x0378 // EHANDLE m_pPuzzleBar, between m_hTelescope +0x370 and m_pBoat +0x380

qboolean SV_CoF_RetailPuzzleRelease( sv_client_t *cl )
{
	static const byte off = 0; // Puzzlebar( byte on )
	byte *player = CoF_RetailStoryPlayer( cl );
	edict_t *button, *view;
	int self;
	if( !player || !( button = CoF_RetailHandle( player + COF_RETAIL_PUZZLE_BAR )) || !button->v.iuser1 ||
		Q_strcmp( SV_GetString( button->v.classname ), "cof_puzzlebarbutton" )) return false;
	memset( player + COF_RETAIL_PUZZLE_BAR, 0, sizeof( edict_t * ) + sizeof( int ));
	SV_CoF_SendUserMessage( cl->edict, "Puzzlebar", &off, sizeof( off ), true );
	view = cl->pViewEntity;
	// the panel's camera: the entity the button's message names (CPuzzleBarButton::Use)
	if( !SV_IsValidEdict( view ) || !button->v.message || Q_strcmp( SV_GetString( view->v.targetname ), SV_GetString( button->v.message )))
		return false;
	self = NUM_FOR_EDICT( cl->edict );
	cl->pViewEntity = NULL;
	MSG_BeginServerCmd( &cl->netchan.message, svc_setview );
	MSG_WriteWord( &cl->netchan.message, self );
	memcpy( player + COF_RETAIL_VIEW_ENT, &self, sizeof( self ));
	return true;
}

qboolean SV_CoF_RetailBlur( sv_client_t *cl, int *blur, int *trip )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	if( !player ) return false;
	memcpy( blur, player + COF_RETAIL_BLUR, sizeof( *blur ));
	*trip = player[COF_RETAIL_TRIPPING] != 0;
	return true;
}

qboolean SV_CoF_RetailSetBlur( sv_client_t *cl, int blur )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	if( !player ) return false;
	memcpy( player + COF_RETAIL_BLUR, &blur, sizeof( blur ));
	return true;
}

qboolean SV_CoF_RetailReviving( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && CoF_RetailHandle( player + COF_RETAIL_CPR_TARGET ) != NULL;
}

// 1.1.9: m_fStamina (P+0x21F0, 0..100: a sprint spends it), for the AI companion's sprint; 0 when unknown.
float SV_CoF_RetailStamina( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	float stamina = 0.0f;
	if( player ) memcpy( &stamina, player + 0x21F0, sizeof( stamina ));
	return stamina >= 0.0f && stamina <= 100.0f ? stamina : 0.0f;
}

qboolean SV_CoF_RetailRevived( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && ( CoF_RetailHandle( player + COF_RETAIL_REVIVER ) != NULL || cl->edict->v.fuser3 == 3.0f );
}

// The guards of the player's Use (100F88C0), which starts CPR: dead, on the
// ground, not gibbed, no restart pending, nobody already at it. Plus one the
// Use lacks: a finished CPR clears both links while the body, still dead, plays
// its get-up (fuser3 3) for a moment; a second CPR then would lay it down again.
qboolean SV_CoF_RetailRevivable( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	if( !player || CoF_RetailAlive( cl->edict ) || !FBitSet( cl->edict->v.flags, FL_ONGROUND ) || cl->edict->v.fuser3 == 3.0f ) return false;
	return !player[COF_RETAIL_GIBBED] && !player[COF_RETAIL_RESTART] && !CoF_RetailHandle( player + COF_RETAIL_REVIVER );
}

qboolean SV_CoF_RetailRevive( sv_client_t *reviver, sv_client_t *body )
{
	byte *player = CoF_RetailStoryPlayer( reviver );
	// The reviver free: not in CPR, its get-up, a phone call, a syringe or a struggle (fuser3).
	if( !player || !CoF_RetailAlive( reviver->edict ) || reviver->edict->v.fuser3 != 0.0f || CoF_RetailHandle( player + COF_RETAIL_CPR_TARGET ) ||
		!SV_CoF_RetailRevivable( body ) || !svgame.dllFuncs.pfnUse ) return false;
	// Use the body as +use would (DispatchUse: the reviver is the activator). Done
	// directly, because a pressed +use picks whatever wins the game's own search,
	// and an item, door or keypad near the body would win it.
	svgame.dllFuncs.pfnUse( body->edict, reviver->edict );
	return SV_CoF_RetailReviving( reviver );
}

// A syringe heal holds healer and partner at fuser3 25, the view locked, until its BladeRetract 1.5 s later
// (CSyringe::SecondaryAttack 1007B110); a healer killed in between left the partner held for good, and Spawn
// keeps fuser3. Held longer than this: let go, as BladeRetract does.
#define COF_RETAIL_SYRINGE_HOLD 25.0f
#define COF_RETAIL_SYRINGE_MAX 3.0
static double cof_retail_syringe_since[MAX_CLIENTS];

static void CoF_RetailSyringeRepair( int i, sv_client_t *cl )
{
	static const byte unlock[3] = { 0, 0, 0 }; // ViewLock( byte 0, coord 0 )
	if( i >= MAX_CLIENTS ) return;
	if( cl->edict->v.fuser3 != COF_RETAIL_SYRINGE_HOLD || cof_retail_syringe_since[i] > sv.time )
		cof_retail_syringe_since[i] = 0.0;
	if( cl->edict->v.fuser3 != COF_RETAIL_SYRINGE_HOLD ) return;
	if( !cof_retail_syringe_since[i] )
	{
		cof_retail_syringe_since[i] = sv.time;
		return;
	}
	if( sv.time - cof_retail_syringe_since[i] <= COF_RETAIL_SYRINGE_MAX ) return;
	cl->edict->v.fuser3 = 0.0f;
	SV_CoF_SendUserMessage( cl->edict, "ViewLock", unlock, sizeof( unlock ), true );
	cof_retail_syringe_since[i] = 0.0;
	Con_Printf( "cofstory phase=syringe-unstuck userid=%d\n", cl->userid );
}

// c_roofs2's cof_yesno "ryesno" roots the player it asks (fuser3 1, no fall flag: CCofYesNo::Use, effects.cpp:7565)
// until they answer; its trigger_multiple fires it while they stand in it. Such a player is not down.
static qboolean CoF_RetailAskedYesNo( const edict_t *player )
{
	int i;
	qboolean prompt = false;
	for( i = svs.maxclients + 1; i < svgame.numEntities && !prompt; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		prompt = SV_IsValidEdict( ent ) && !Q_strcmp( SV_GetString( ent->v.classname ), "cof_yesno" ) &&
			!Q_strcmp( SV_GetString( ent->v.targetname ), "ryesno" );
	}
	for( i = svs.maxclients + 1; prompt && i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.target ), "ryesno" )) continue;
		if( player->v.absmin[0] <= ent->v.absmax[0] && player->v.absmax[0] >= ent->v.absmin[0] &&
			player->v.absmin[1] <= ent->v.absmax[1] && player->v.absmax[1] >= ent->v.absmin[1] &&
			player->v.absmin[2] <= ent->v.absmax[2] && player->v.absmax[2] >= ent->v.absmin[2] )
			return true;
	}
	return false;
}

// 1.1.8 (K2): a trip (fuser3 1) or a taller's knock-down (fuser3 2) gets up only through PostThink's get-up, which
// needs m_bShouldFallFreeze; every EnableControl (each camera and model cutscene start and end, for all players)
// clears it, and the player could never walk again. Held down so for a moment, not frozen, not in CPR, not asked by
// a yes/no: set the flag and its time back, and the game's own get-up runs on the next PostThink.
#define COF_RETAIL_FALL_GRACE 1.0
#define COF_RETAIL_FALL_LONGEST 10.0f // the longest get-up time the game sets (a death's fall, Killed)
static double cof_retail_fall_since[MAX_CLIENTS];
// 1.1.9 (K2 residual): the get-up time the game last set for this fall (m_fFallingTime, never the 0 EnableControl
// writes; a tapgetup's "now" counts), so the repair keeps it: a taller's 3 s knock-down cleared 0.2 s in got up
// after the 1 s grace.
static float cof_retail_fall_until[MAX_CLIENTS];

static void CoF_RetailFallRepair( int i, sv_client_t *cl, byte *body )
{
	float fuser3 = cl->edict->v.fuser3, now = (float)sv.time, until;
	qboolean down = fuser3 == 1.0f || fuser3 == 2.0f;
	if( i >= MAX_CLIENTS ) return;
	if( !down || cof_retail_fall_since[i] > sv.time ) cof_retail_fall_until[i] = 0.0f; // standing, or another map's
	memcpy( &until, body + COF_RETAIL_FALL_TIME, sizeof( until ));
	if( down && until > 0.0f ) cof_retail_fall_until[i] = until;
	if( !down || body[COF_RETAIL_FALL_FREEZE] || cof_retail_fall_since[i] > sv.time ||
		CoF_RetailHandle( body + COF_RETAIL_CPR_TARGET ) || CoF_RetailHandle( body + COF_RETAIL_REVIVER ))
	{
		cof_retail_fall_since[i] = 0.0; // standing, a fall the game ends itself, or a CPR under way
		return;
	}
	if( fuser3 == 2.0f && !body[COF_RETAIL_WAITING_FALL] )
	{
		// A CPR the game cancelled because its body came alive (a respawn) or left clears only the link, leaving
		// the reviver's CPR state (fuser3 2) to block every attack: end it the way a cancel does, by its get-up.
		// 1.1.8 (E2-2): only this case; the taller's knock-down is fuser3 2 too (with m_bWaitingToFall), and
		// forcing its get-up made it land again every frame.
		cl->edict->v.fuser3 = 4.0f;
		memcpy( body + COF_RETAIL_GETUP_END, &now, sizeof( now ));
		cof_retail_fall_since[i] = 0.0;
		Con_Printf( "cofstory phase=cpr-unstuck userid=%d\n", cl->userid );
		return;
	}
	if( FBitSet( cl->edict->v.flags, FL_FROZEN ) || ( fuser3 == 1.0f && CoF_RetailAskedYesNo( cl->edict )))
	{
		cof_retail_fall_since[i] = 0.0; // a scene holds them (its end calls EnableControl again), or a question
		return;
	}
	if( !cof_retail_fall_since[i] )
	{
		cof_retail_fall_since[i] = sv.time;
		return;
	}
	if( sv.time - cof_retail_fall_since[i] <= COF_RETAIL_FALL_GRACE ) return;
	until = cof_retail_fall_until[i];
	if( until < now || until > now + COF_RETAIL_FALL_LONGEST ) until = now;
	body[COF_RETAIL_FALL_FREEZE] = 1;
	memcpy( body + COF_RETAIL_FALL_TIME, &until, sizeof( until ));
	cof_retail_fall_since[i] = 0.0;
	Con_Printf( "cofstory phase=fall-unstuck userid=%d fuser3=%d\n", cl->userid, (int)fuser3 );
}

void SV_CoF_RetailCprRepair( void )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		byte *body = CoF_RetailStoryPlayer( cl ), *reviver;
		edict_t *by;
		if( body && CoF_RetailAlive( cl->edict )) CoF_RetailSyringeRepair( i, cl );
		if( body && CoF_RetailAlive( cl->edict ))
		{
			CoF_RetailFallRepair( i, cl, body );
			continue;
		}
		if( i < MAX_CLIENTS ) { cof_retail_fall_since[i] = 0.0; cof_retail_fall_until[i] = 0.0f; }
		if( !body || !( by = CoF_RetailHandle( body + COF_RETAIL_REVIVER ))) continue;
		reviver = CoF_RetailPrivate( by, COF_RETAIL_PLAYER_SIZE );
		if( reviver && CoF_RetailAlive( by ) && CoF_RetailHandle( reviver + COF_RETAIL_CPR_TARGET ) == cl->edict ) continue;
		memset( body + COF_RETAIL_REVIVER, 0, sizeof( edict_t * ) + sizeof( int ));
		Con_Printf( "cofstory phase=cpr-unlinked userid=%d\n", cl->userid );
	}
}

/*
1.3.9: a player's weapons after a revive (a player, in Cry of Fear's own co-op: "After a player is revived, they are
sometimes unable to use their weapons if they drop them and pick them up again, Cry of Fear crashes"). Two retail
leftovers, read in the decompilation (player.cpp; evidence/release-1.3.9/revive-weapons-re.md on the dev PC):
- A revive's get-up plays while the body is still dead, at fuser3 3 (RevivePlayer). A respawn then (PreThink: jump
  after 60 s, or sv_auto_respawn_time, which RevivePlayer does not clear) runs Spawn, which keeps fuser3: every attack
  wants fuser3 <= 0 (CBasePlayerWeapon::ItemPostFrame) and monsters do not see a player at 3, for that whole life.
  Spawn keeps the other leftovers of a death too (10 a gun's suicide struggle, 25 a syringe hold, 5 a phone call).
- RemovePlayerItem (a drop) clears m_pActiveItem, or else m_pLastItem, and never m_pNextItem (the last switch's
  weapon), m_pStoredActionItem or m_pStoredNightVisionWeapon; a later switch (a get-up's queued deploy, PostThink
  6389; SelectLastItem; the action or night-vision restore) then Deploys a weapon lying on the floor with m_pPlayer
  NULL (CGlock::Deploy reads through it) or one already freed. A get-up from a knock-down (fuser3 2) also queues a
  deploy of m_pNextItem whatever it holds: NULL (a fresh life) left the player with nothing in hand.
Before each PostThink (SV_RunCmd): a respawned player's fuser3 back to 0, a living one's 3 too; each of those weapon
pointers that is not on the player's inventory chain (m_rgpPlayerItems through m_pNext: a weapon leaving it keeps its
m_pPlayer until removed 0.1 s later, RemoveAllItems at a respawn, DestroyItem; independent review) is cleared as
RemovePlayerItem would; a queued deploy of nothing deploys the weapon in hand, as it is after every switch.
*/
#define COF_RETAIL_LAST_ITEM      0x2534 // CBasePlayerItem *m_pLastItem
#define COF_RETAIL_STORED_ACTION  0x253C // CBasePlayerItem *m_pStoredActionItem: back in hand after a weapon_action
#define COF_RETAIL_STORED_NV      0x03F0 // CBasePlayerWeapon *m_pStoredNightVisionWeapon: back after the night vision
#define COF_RETAIL_ITEMS          0x2514 // CBasePlayerItem *m_rgpPlayerItems[6]: each slot's chain, through m_pNext
#define COF_RETAIL_ITEM_SLOTS     6
#define COF_RETAIL_ITEM_NEXT      0x0164 // CBasePlayerItem *m_pNext (after m_pPlayer +0x160, before m_iId +0x168)
#define COF_RETAIL_ITEM_NEED      0x0168 // every CBasePlayerItem holds m_pPlayer and m_pNext
#define COF_RETAIL_CHAIN_MOST     48     // the inventory chain read: more is not a sane inventory (a loop)
static const struct { int offset; const char *name; } cof_retail_item_fields[] =
{
	{ COF_RETAIL_ACTIVE_ITEM, "active" }, // first: a queued deploy of nothing takes the weapon in hand
	{ COF_RETAIL_LAST_ITEM, "last" },
	{ COF_RETAIL_STORED_ACTION, "stored-action" },
	{ COF_RETAIL_STORED_NV, "stored-nightvision" },
	{ COF_RETAIL_NEXT_ITEM, "next" },
};
#define COF_RETAIL_ITEM_FIELDS ( (int)ARRAYSIZE( cof_retail_item_fields ))
static int cof_retail_item_hint[MAX_CLIENTS][COF_RETAIL_ITEM_FIELDS];
static int cof_retail_chain_hint[MAX_CLIENTS][COF_RETAIL_CHAIN_MOST];

// The game is 32-bit: these fields are four-byte pointers four bytes apart, read and written as such (an offline
// 64-bit test build compares their low four bytes); never followed, only compared with the edicts' game objects.
typedef uint32_t cof_retail_ptr_t;
#define COF_RETAIL_PTR( p ) ( (cof_retail_ptr_t)(uintptr_t)( p ))

// The edict whose game object this is, or NULL: none any more.
static edict_t *CoF_RetailObjectEdict( cof_retail_ptr_t object, int *hint )
{
	int i;
	if( *hint > svs.maxclients && *hint < svgame.numEntities )
	{
		edict_t *e = SV_EdictNum( *hint );
		if( !e->free && e->pvPrivateData && COF_RETAIL_PTR( e->pvPrivateData ) == object ) return e;
	}
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		if( e->free || !e->pvPrivateData || COF_RETAIL_PTR( e->pvPrivateData ) != object ) continue;
		*hint = i;
		return e;
	}
	return NULL;
}

// The player's inventory chain: each slot's items through m_pNext, every pointer looked up as an edict first (a value
// never followed); stops at an item that is gone, at a loop, at COF_RETAIL_CHAIN_MOST.
static int CoF_RetailItemChain( int client, const byte *player, cof_retail_ptr_t *chain )
{
	int n = 0, slot, seen;
	for( slot = 0; slot < COF_RETAIL_ITEM_SLOTS; slot++ )
	{
		cof_retail_ptr_t item;
		memcpy( &item, player + COF_RETAIL_ITEMS + slot * sizeof( item ), sizeof( item ));
		while( item && n < COF_RETAIL_CHAIN_MOST )
		{
			edict_t *e;
			byte *pv;
			for( seen = 0; seen < n && chain[seen] != item; seen++ );
			if( seen < n ) break;
			e = CoF_RetailObjectEdict( item, &cof_retail_chain_hint[client][n] );
			if( !e || !( pv = CoF_RetailPrivate( e, COF_RETAIL_ITEM_NEED + sizeof( item )))) break;
			chain[n++] = item;
			memcpy( &item, pv + COF_RETAIL_ITEM_NEXT, sizeof( item ));
		}
	}
	return n;
}

// Why this weapon pointer of the player's is not theirs any more, or NULL: on their inventory chain.
static const char *CoF_RetailItemStale( const byte *player, cof_retail_ptr_t object, int *hint, const cof_retail_ptr_t *chain, int count )
{
	edict_t *e = CoF_RetailObjectEdict( object, hint );
	cof_retail_ptr_t owner = 0;
	byte *pv;
	int i;
	if( !e ) return "gone";
	if( FBitSet( e->v.flags, FL_KILLME )) return "removed";
	for( i = 0; i < count; i++ )
		if( chain[i] == object ) return NULL;
	if(( pv = CoF_RetailPrivate( e, COF_RETAIL_ITEM_NEED ))) memcpy( &owner, pv + COF_RETAIL_WEAPON_PLAYER, sizeof( owner ));
	if( owner == COF_RETAIL_PTR( player )) return "leaving"; // out of the inventory, removed in a moment
	return owner ? "another's" : "dropped";
}

void SV_CoF_RetailItemsRepair( sv_client_t *cl, qboolean respawned )
{
	static const byte unlock[3] = { 0, 0, 0 }; // ViewLock( byte 0, coord 0 )
	byte *player = CoF_RetailStoryPlayer( cl );
	cof_retail_ptr_t object, active, chain[COF_RETAIL_CHAIN_MOST];
	int i, k, count = 0;
	qboolean chained = false;
	if( !player || ( i = (int)( cl - svs.clients )) < 0 || i >= MAX_CLIENTS ) return;
	if( CoF_RetailAlive( cl->edict ) && cl->edict->v.fuser3 != 0.0f && ( respawned || cl->edict->v.fuser3 == 3.0f ))
	{
		float was = cl->edict->v.fuser3;
		cl->edict->v.fuser3 = 0.0f;
		if( was == 25.0f || was == 5.0f ) SV_CoF_SendUserMessage( cl->edict, "ViewLock", unlock, sizeof( unlock ), true );
		Con_Printf( "cofstory phase=weapons-unstuck userid=%d fuser3=%d respawned=%d\n", cl->userid, (int)was, respawned ? 1 : 0 );
	}
	for( k = 0; k < COF_RETAIL_ITEM_FIELDS; k++ )
	{
		const int offset = cof_retail_item_fields[k].offset;
		const char *why;
		memcpy( &object, player + offset, sizeof( object ));
		if( offset == COF_RETAIL_NEXT_ITEM && !object && player[COF_RETAIL_DEFER_DEPLOY] )
		{
			// a queued deploy of nothing (a knock-down's get-up): the weapon in hand, as after every switch
			memcpy( &active, player + COF_RETAIL_ACTIVE_ITEM, sizeof( active ));
			if( !active ) player[COF_RETAIL_DEFER_DEPLOY] = 0;
			memcpy( player + offset, &active, sizeof( active ));
			if( active ) Con_Printf( "cofstory phase=item-unlinked userid=%d field=next why=empty-switch\n", cl->userid );
			continue;
		}
		if( !object ) continue;
		if( !chained ) { count = CoF_RetailItemChain( i, player, chain ); chained = true; } // once, when a pointer is set
		if( !( why = CoF_RetailItemStale( player, object, &cof_retail_item_hint[i][k], chain, count ))) continue;
		object = 0;
		if( offset == COF_RETAIL_NEXT_ITEM )
		{
			// a switch under way to it: the weapon in hand instead (checked above), or none
			memcpy( &object, player + COF_RETAIL_ACTIVE_ITEM, sizeof( object ));
			if( !object ) player[COF_RETAIL_DEFER_DEPLOY] = 0;
		}
		memcpy( player + offset, &object, sizeof( object ));
		if( offset == COF_RETAIL_ACTIVE_ITEM )
		{
			cl->edict->v.viewmodel = 0; // as RemovePlayerItem does
			cl->edict->v.weaponmodel = 0;
		}
		Con_Printf( "cofstory phase=item-unlinked userid=%d field=%s why=%s\n", cl->userid, cof_retail_item_fields[k].name, why );
	}
}

qboolean SV_CoF_RetailFallen( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	return player && player[COF_RETAIL_FALL_FREEZE] && cl->edict->v.fuser3 != 0.0f;
}

qboolean SV_CoF_RetailOwnAnimation( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	float end;
	if( !player ) return false;
	memcpy( &end, player + COF_RETAIL_SEQUENCE_END, sizeof( end ));
	return end > (float)sv.time;
}

qboolean SV_CoF_RetailCommand( sv_client_t *cl, const char *command )
{
	if( !CoF_RetailPlayer( cl ) || !command || !svgame.dllFuncs.pfnClientCommand ) return false;
	Cmd_TokenizeString( command );
	SV_CoF_RetailGameCommand( cl );
	return true;
}

// A monster's class when its m_iClass key is unset (each Classify), and the
// ones that ignore the key.
static int CoF_RetailMonsterClass( const char *name, int keyed )
{
	if( !Q_strcmp( name, "monster_doctorboss" )) return 4;
	if( !Q_strcmp( name, "monster_nerd" )) return 5;
	if( !Q_strncmp( name, "monster_twitcher", 16 )) return 7;
	if( keyed ) return keyed;
	return Q_strcmp( name, "monster_sawrunner" ) ? 7 : 18;
}

edict_t *SV_CoF_RetailMonsterEnemy( edict_t *monster )
{
	byte *pv = CoF_RetailPrivate( monster, COF_RETAIL_MONSTER_NEED );
	return pv ? CoF_RetailHandle( pv + COF_RETAIL_MONSTER_ENEMY ) : NULL;
}

int SV_CoF_RetailHostile( edict_t *monster )
{
	static const char *never[] = { "monster_generic", "monster_furniture", "monster_target" };
	const char *name;
	edict_t *enemy;
	uintptr_t base;
	byte *pv;
	int i, react, keyed, provoked, relation, engaged;
	if( !monster ) return 0;
	name = SV_GetString( monster->v.classname );
	// Allies, props and scripted actors: monster_generic carries c_ending4b's boss health and the strangler.
	if( Q_strncmp( name, "monster_", 8 ) || !Q_strncmp( name, "monster_cine", 12 ) || Q_strstr( name, "_dead" )) return 0;
	for( i = 0; i < (int)ARRAYSIZE( never ); i++ )
		if( !Q_strcmp( name, never[i] )) return 0;
	if( FBitSet( monster->v.spawnflags, COF_RETAIL_SF_PRISONER )) return 0;
	if( !( pv = CoF_RetailPrivate( monster, COF_RETAIL_MONSTER_NEED ))) return 0;
	memcpy( &react, pv + COF_RETAIL_MONSTER_REACT, sizeof( react ));
	memcpy( &keyed, pv + COF_RETAIL_MONSTER_CLASS, sizeof( keyed ));
	memcpy( &provoked, pv + COF_RETAIL_MONSTER_MEMORY, sizeof( provoked ));
	enemy = CoF_RetailHandle( pv + COF_RETAIL_MONSTER_ENEMY );
	engaged = enemy && NUM_FOR_EDICT( enemy ) >= 1 && NUM_FOR_EDICT( enemy ) <= svs.maxclients;
	// IRelationship (100BF7F0): m_iPlayerReact overrides the class table for players.
	if( react == 1 ) return 0;
	if( react != 0 && react != 4 && !( provoked & 1 )) return 0;
	if( react == 0 )
	{
		int cls = CoF_RetailMonsterClass( name, keyed );
		if( !( base = SV_CoF_StoryBase()) || cls < 0 || cls >= COF_RETAIL_CLASSES ) return 0;
		memcpy( &relation, (const byte *)base + COF_RETAIL_RELATIONS + ( cls * COF_RETAIL_CLASSES + COF_RETAIL_CLASS_PLAYER ) * sizeof( int ), sizeof( relation ));
		if( relation <= 0 ) return 0;
	}
	// Waiting for its script (a boss before its entrance): leave it until it goes for someone.
	if( FBitSet( monster->v.spawnflags, COF_RETAIL_SF_WAIT_SCRIPT ) && !engaged ) return 0;
	return engaged ? 2 : 1;
}

int SV_CoF_RetailNoteSlot( sv_client_t *cl, const char *note, int start )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	int slot;
	if( !player || !note ) return 0;
	// a free slot (an empty note) only where the engine may write one
	for( slot = start < 1 ? 1 : start; slot <= ( note[0] ? COF_RETAIL_DOC_COUNT : COF_RETAIL_DOC_SAFE ); slot++ )
	{
		const char *key = (const char *)player + COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * slot + COF_RETAIL_DOC_NOTE1;
		if( memchr( key, 0, 16 ) && !Q_strcmp( key, note )) return slot;
	}
	return 0;
}

qboolean SV_CoF_RetailArchiveNote( sv_client_t *cl, int slot, const byte *payload, int size )
{
	static const int offsets[11] = { 0x40, 0x50, 0x60, 0x70, 0, 0x10, 0x20, 0x30, 0x80, 0xB0, 0xF0 };
	byte message[COF_STATE_MESSAGE], storage[COF_RETAIL_DOC_SIZE], *entry;
	byte *player = CoF_RetailStoryPlayer( cl );
	int field, position = 5;
	if( !player || slot < 1 || slot > COF_RETAIL_DOC_SAFE || !CoF_StateNoteValid( payload, size )) return false;
	entry = player + COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * slot;
	if( entry[COF_RETAIL_DOC_NOTE1] && ( !memchr( entry + COF_RETAIL_DOC_NOTE1, 0, 16 ) ||
		Q_strcmp( (const char *)entry + COF_RETAIL_DOC_NOTE1, (const char *)payload + 5 ))) return false;
	memset( storage, 0, sizeof( storage ));
	for( field = 0; field < 11; field++ )
	{
		size_t length = Q_strlen( (const char *)payload + position ) + 1;
		memcpy( storage + offsets[field], payload + position, length );
		position += (int)length;
	}
	memcpy( message, payload, size );
	message[0] = (byte)slot;
	if( !SV_CoF_SendUserMessage( cl->edict, "AddNote", message, size, true )) return false;
	memcpy( entry, storage, sizeof( storage ));
	return true;
}

qboolean SV_CoF_RetailThreeSlots( sv_client_t *cl )
{
	byte *player = CoF_RetailStoryPlayer( cl );
	edict_t *entity;
	if( !player ) return false;
	if( player[COF_RETAIL_THREE_SLOTS] ) return true;
	if( !svgame.dllFuncs.pfnSpawn || !svgame.dllFuncs.pfnUse ) return false;
	entity = SV_CreateNamedEntity( NULL, SV_AllocString( "cof_clearitems" ));
	if( !SV_IsValidEdict( entity )) return false;
	entity->v.spawnflags = 4;
	if( svgame.dllFuncs.pfnSpawn( entity ) != -1 && SV_IsValidEdict( entity ) && !FBitSet( entity->v.flags, FL_KILLME ))
		svgame.dllFuncs.pfnUse( entity, cl->edict );
	if( !entity->free ) SV_FreeEdict( entity );
	player = CoF_RetailStoryPlayer( cl );
	return player && player[COF_RETAIL_THREE_SLOTS];
}

static sv_client_t *CoF_RetailClientByUserid( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients; i++ )
		if( svs.clients[i].state == cs_spawned && svs.clients[i].userid == userid ) return &svs.clients[i];
	return NULL;
}

// Developer probe for the live checks, from the server console or RCON:
// cof_story_probe <slots|give|remove|msg|fade|doc|codenote> <userid> [args]
static void CoF_RetailProbe_f( void )
{
	char what[16], arg[COF_RETAIL_ITEM], text[128];
	sv_client_t *cl;
	int result = 0, i;
	if( Cmd_Argc() < 3 )
	{
		Con_Printf( S_USAGE "cof_story_probe <slots|give|remove|msg|fade|doc|codenote> <userid> [descriptor | colour text | hold | n | code style]\n" );
		return;
	}
	// Copy the arguments first: a removal re-tokenizes the command buffer.
	Q_strncpy( what, Cmd_Argv( 1 ), sizeof( what ));
	cl = CoF_RetailClientByUserid( Q_atoi( Cmd_Argv( 2 )));
	Q_strncpy( arg, Cmd_Argc() > 3 ? Cmd_Argv( 3 ) : "", sizeof( arg ));
	Q_strncpy( text, Cmd_Argc() > 4 ? Cmd_Argv( 4 ) : "", sizeof( text ));
	if( !cl || !SV_CoF_StoryCoopEnabled() )
	{
		Con_Printf( "cofstory phase=probe what=%s result=0 reason=%s\n", what, cl ? "story-off" : "no-such-player" );
		return;
	}
	if( !Q_strcmp( what, "slots" ))
	{
		char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM];
		result = SV_CoF_RetailReadSlots( cl, slots );
		for( i = 0; result && i < COF_RETAIL_SLOTS; i++ )
			Con_Printf( "cofstory phase=probe-slot userid=%d index=%d descriptor=\"%s\"\n", cl->userid, i + 1, slots[i] );
		Con_Printf( "cofstory phase=probe what=slots userid=%d result=%d free=%d\n", cl->userid, result, SV_CoF_RetailFreeSlots( cl ));
		return;
	}
	// Everything but reading the slots changes what a player has or sees (1.1.8: msg and fade too; the console is open
	// in co-op).
	if( !Cvar_VariableInteger( "sv_cheats" ))
	{
		Con_Printf( "cofstory phase=probe what=%s result=0 reason=needs-sv_cheats\n", what );
		return;
	}
	if( !Q_strcmp( what, "doc" ) || !Q_strcmp( what, "codenote" ))
	{
		int before = SV_CoF_RetailDocumentCount( cl );
		if( !Q_strcmp( what, "doc" )) result = SV_CoF_RetailAddDocument( cl, CoF_RetailNthDocument( arg[0] ? Q_atoi( arg ) : 1 ));
		else result = SV_CoF_RetailCodeNote( cl, Q_atoi( arg ), Q_atoi( text ));
		Con_Printf( "cofstory phase=probe what=%s userid=%d result=%d documents=%d->%d\n", what, cl->userid, result, before, SV_CoF_RetailDocumentCount( cl ));
		return;
	}
	if( !Q_strcmp( what, "give" )) result = SV_CoF_RetailGiveItem( cl, arg );
	else if( !Q_strcmp( what, "remove" )) result = SV_CoF_RetailRemoveItem( cl, arg );
	else if( !Q_strcmp( what, "msg" )) result = SV_CoF_RetailProFont( cl, Q_atoi( arg ), text, true );
	else if( !Q_strcmp( what, "fade" )) result = SV_CoF_RetailScreenFade( cl, 0.5f, arg[0] ? Q_atof( arg ) : 0.1f, 0, 0, 0, 0, 255 );
	Con_Printf( "cofstory phase=probe what=%s userid=%d result=%d\n", what, cl->userid, result );
}

void SV_CoF_RetailInit( void )
{
	Cmd_AddCommand( "cof_story_probe", CoF_RetailProbe_f, "story co-op developer probe: slots, give, remove, msg, fade, doc, codenote" );
}
