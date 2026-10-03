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

#define COF_RETAIL_INVENTORY   0x21FC // string_t inventorySlot[7], slots 1..6 used (AddInventoryItem 100DAD40)
#define COF_RETAIL_THREE_SLOTS 0x1E9C // bool m_bThreeSlots: only slots 1..3 usable
#define COF_RETAIL_PLAYER_NEED ( COF_RETAIL_INVENTORY + 7 * sizeof( string_t ))
#define COF_RETAIL_CACHE       128

// Documents (docs/research/2026-09-30-hl-documents-story-state-re.md, project repo)
#define COF_RETAIL_PLAYER_SIZE 0x281C // the whole CBasePlayer
#define COF_RETAIL_DOCUMENT    0x0630 // EHANDLE m_hDocument: the cof_document being read
#define COF_RETAIL_DOC_SLOTS   0x0688 // documentsstruct: entry i (1..16) at +0x688 + 0x120 * i
#define COF_RETAIL_DOC_SIZE    0x120
#define COF_RETAIL_DOC_COUNT   16
#define COF_RETAIL_DOC_NOTE1   0x40 // entry + 0x40: note1; non-empty marks a used slot
#define COF_RETAIL_DOC_ENTITY  0x210 // sizeof( CCoFDocument )
#define COF_RETAIL_ADDDOCUMENT 0xDA6D0 // CBasePlayer::AddDocument( CCoFDocument * ), thiscall, ret 4

typedef struct
{
	char descriptor[COF_RETAIL_ITEM];
	char classname[32];
	char nice[COF_RETAIL_ITEM];
	qboolean valid;
} cof_retail_item_t;

static cof_retail_item_t cof_retail_items[COF_RETAIL_CACHE];
static int cof_retail_item_count;

static byte *CoF_RetailPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_RETAIL_PLAYER_NEED );
}

// The same player, checked for the whole object: document and story fields sit past the inventory.
static byte *CoF_RetailStoryPlayer( sv_client_t *cl )
{
	if( !cl || cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return NULL;
	return SV_CoF_StoryEntityPrivate( cl->edict, COF_RETAIL_PLAYER_SIZE );
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
	usable = pv[COF_RETAIL_THREE_SLOTS] ? 3 : COF_RETAIL_SLOTS;
	for( i = 1; i <= usable; i++ )
	{
		string_t id;
		memcpy( &id, pv + COF_RETAIL_INVENTORY + i * sizeof( id ), sizeof( id ));
		if( !id ) count++;
	}
	return count;
}

qboolean SV_CoF_RetailGiveItem( sv_client_t *cl, const char *descriptor )
{
	const cof_retail_item_t *item = CoF_RetailItem( descriptor );
	if( !item || !CoF_RetailPlayer( cl )) return false;
	return SV_CoF_StoryGiveItemEx( cl, item->classname, descriptor );
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
	svgame.dllFuncs.pfnClientCommand( cl->edict );
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
	size_t length;
	if( !cl || cl->state != cs_spawned || !text || colour < 0 || colour > 6 ) return false;
	// The client's ProFont hook replaces any line containing DEADNOTIFY with its respawn prompt.
	if( Q_strstr( text, "DEADNOTIFY" )) return false;
	length = Q_strlen( text );
	if( length + 2 > sizeof( payload )) return false;
	payload[0] = (byte)colour;
	memcpy( payload + 1, text, length + 1 );
	return SV_CoF_SendUserMessage( cl->edict, "ProFont", payload, (int)length + 2, reliable );
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
	for( i = 1; i <= COF_RETAIL_DOC_COUNT; i++ )
	{
		entry = pv + COF_RETAIL_DOC_SLOTS + COF_RETAIL_DOC_SIZE * i;
		if( !entry[COF_RETAIL_DOC_NOTE1] ) break;
	}
	if( i > COF_RETAIL_DOC_COUNT ) return false; // every document slot is used
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
	if(( !Q_strcmp( what, "give" ) || !Q_strcmp( what, "remove" ) || !Q_strcmp( what, "doc" ) || !Q_strcmp( what, "codenote" )) &&
		!Cvar_VariableInteger( "sv_cheats" ))
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
