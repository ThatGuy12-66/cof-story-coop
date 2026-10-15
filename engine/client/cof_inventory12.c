/*
cof_inventory12.c - Cry of Fear story co-op: the 12-slot inventory, client side
GPL-3.0-or-later; docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md section 2.

client.dll knows six slots. When its file is the retail 1.6 one, its single
slot-string accessor is redirected to the engine's store of twelve
(cof_inventory12_patch.h), Inventory messages for slots 7..12 are kept from it
(cof_inventory12_state.h) and the UI library draws the grid
(3rdparty/freevgui/platform/xash3d-fwgs/cofinventory.cpp) through
vguiapi_t::CofInventory. With both in place the client advertises the userinfo
key _cofinv 12; the server says each map's slot count with cof_inv_slots.
*/
#include "common.h"
#include "client.h"
#include "library.h"
#include "vgui_api.h"
#include "sv_cof_descriptor.h"
#include "cof_inventory12_state.h"
// Code patching only where the rest of the story co-op does it (sv_cof_story.c)
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
#define COF_INV12_PATCH 1
#include "cof_inventory12_patch.h"
#else
#define COF_INV12_PATCH 0
#endif

#define COF_INV12_CLIENT_SHA256 "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00" // client.dll, Cry of Fear 1.6

static CVAR_DEFINE( cof_inv12_userinfo, "_cofinv", "", FCVAR_USERINFO, "story co-op: \"12\" when this client has the 12-slot inventory (set by the engine)" );

static cof_inv12_slots_t cof_inv12_store;
static struct
{
#if COF_INV12_PATCH
	cof_inv12_hook_t hook;
#endif
	qboolean registered;	// the _cofinv cvar exists (CL_CoF_Inventory12Init)
	qboolean patched;	// client.dll's accessor reads the store
	qboolean grid;		// the UI library has the grid
	int slots;		// the server's cof_inv_slots for this map: 12, 6, or 0 before it said
	struct { char descriptor[COF_INV12_TEXT]; cof_vgui_inv_item_t item; } cache[COF_INV12_SLOTS + 1];
} cof_inv12;

qboolean CL_CoF_Inventory12Ready( void )
{
	return cof_inv12.patched && cof_inv12.grid;
}

static void CL_CoF_Inv12Advertise( void )
{
	if( !cof_inv12.registered ) return; // the UI library may report before CL_CoF_Inventory12Init; Init advertises
	if( CL_CoF_Inventory12Ready( ) && Q_strcmp( cof_inv12_userinfo.string, "12" ))
		Cvar_DirectSet( &cof_inv12_userinfo, "12" ); // FCVAR_USERINFO: into the userinfo, and to the server when connected
}

#if COF_INV12_PATCH
static char * __stdcall CL_CoF_Inv12Accessor( int slot )
{
	return CoF_Inv12Slot( &cof_inv12_store, slot );
}
#endif

// cl_game.c CL_LoadProgs: after the module is loaded, before its Initialize/Init
void CL_CoF_Inventory12Load( void *module )
{
#if COF_INV12_PATCH
	dll_user_t *lib = (dll_user_t *)module;
	cof_inv12.patched = false;
	if( !lib || !lib->hInstance || lib->custom_loader ) return;
	if( !COM_CoF_ModuleSha256Is( lib, COF_INV12_CLIENT_SHA256 ))
	{
		Con_Reportf( "[cof-inv12] client.dll is not the retail Cry of Fear 1.6 one: six inventory slots\n" );
		return;
	}
	memset( &cof_inv12_store, 0, sizeof( cof_inv12_store ));
	if( !CoF_Inv12HookAccessor( &cof_inv12.hook, (unsigned char *)lib->hInstance, (const void *)CL_CoF_Inv12Accessor ))
	{
		Con_Printf( S_WARN "[cof-inv12] client.dll's slot accessor has unexpected bytes: six inventory slots\n" );
		return;
	}
	cof_inv12.patched = true;
	Con_Reportf( "[cof-inv12] client.dll's slot accessor reads the 12-slot store\n" );
	CL_CoF_Inv12Advertise( );
#endif
}

// cl_game.c CL_UnloadProgs: before the module is freed
void CL_CoF_Inventory12Unload( void )
{
#if COF_INV12_PATCH
	CoF_Inv12UnhookAccessor( &cof_inv12.hook );
#endif
	cof_inv12.patched = false;
}

void CL_CoF_Inventory12ClearState( void )
{
	cof_inv12.slots = 0; // the server says it again for the next map
}

static void CL_CoF_Inv12Pickup( const char *descriptor )
{
	char nice[64];
	byte line[192];
	byte *text;
	int length;
	if( !descriptor[0] || !CoF_DescriptorSafe( descriptor ) || !( text = FS_LoadFile( descriptor, NULL, false ))) return;
	CoF_DescriptorValue( (const char *)text, "nice_name", nice, sizeof( nice ));
	Mem_Free( text );
	length = CoF_Inv12PickupLine( nice, line, sizeof( line ));
	if( length ) CL_DispatchUserMessage( "ProFont", length, line );
}

// cl_parse.c CL_ParseUserMessage, before client.dll's hook: true when the
// engine kept the message from client.dll
qboolean CL_CoF_Inventory12Message( const char *name, int size, const byte *buf )
{
	cof_inv12_message_t m;
	if( !Q_strcmp( name, "ResetHUD" ))
	{
		CoF_Inv12ResetHigh( &cof_inv12_store );
		return false;
	}
	if( Q_strcmp( name, "Inventory" )) return false;
	if( !CoF_Inv12Parse( buf, size, &m ))
	{
		Con_Printf( S_WARN "[cof-inv12] a malformed Inventory message was dropped\n" );
		return true;
	}
	switch( CoF_Inv12Route( &cof_inv12_store, &m ))
	{
	case COF_INV12_FORWARD:
		return false;
	case COF_INV12_KEPT:
		if( !m.silent ) CL_CoF_Inv12Pickup( m.descriptor );
		return true;
	default:
		Con_Printf( S_WARN "[cof-inv12] an Inventory message for slot %d was dropped\n", m.slot );
		return true;
	}
}

static void CL_CoF_Inv12Slots_f( void )
{
	int n = Q_atoi( Cmd_Argv( 1 ));
	cof_inv12.slots = n == 12 || n == 6 ? n : 0;
}

static int CL_CoF_Inv12Item( int slot, cof_vgui_inv_item_t *out )
{
	const char *d;
	if( slot < 1 || slot > COF_INV12_SLOTS ) return -1;
	d = cof_inv12_store.slot[slot];
	if( Q_strcmp( cof_inv12.cache[slot].descriptor, d )) // read each descriptor file once
	{
		cof_vgui_inv_item_t *it = &cof_inv12.cache[slot].item;
		byte *text;
		memset( it, 0, sizeof( *it ));
		Q_strncpy( cof_inv12.cache[slot].descriptor, d, sizeof( cof_inv12.cache[slot].descriptor ));
		Q_strncpy( it->descriptor, d, sizeof( it->descriptor ));
		it->weapon = Q_strstr( d, "weapon_" ) != NULL;
		if( CoF_DescriptorSafe( d ) && ( text = FS_LoadFile( d, NULL, false )))
		{
			CoF_DescriptorValue( (const char *)text, "inventory_picture", it->picture, sizeof( it->picture ));
			CoF_DescriptorValue( (const char *)text, "check_picture", it->check, sizeof( it->check ));
			CoF_DescriptorValue( (const char *)text, "combined_result", it->combined, sizeof( it->combined ));
			CoF_DescriptorValue( (const char *)text, "classname", it->classname, sizeof( it->classname ));
			Mem_Free( text );
		}
	}
	*out = cof_inv12.cache[slot].item;
	return d[0] ? 1 : 0;
}

// Only what the grid's menu sends, the way client.dll's own menu sends it.
static qboolean CL_CoF_Inv12Command( const char *text )
{
	static const char *allowed[] = { "inventoryequip ", "inventorydrop ", "inventorycombine ", "inventorydualwield ", "quickselset1 ", "quickselset2 ", "quickselset3 " };
	const char *c;
	size_t i;
	for( c = text; *c; c++ )
		if( *c == ';' || *c == '"' || *c == '\n' || *c == '\r' ) return false;
	for( i = 0; i < ARRAYSIZE( allowed ); i++ )
		if( !Q_strncmp( text, allowed[i], Q_strlen( allowed[i] )))
		{
			CL_ServerCommand( true, "%s", text );
			return true;
		}
	return false;
}

static int CL_CoF_Inv12Image( cof_vgui_image_t *image )
{
	rgbdata_t *pic;
	int bytes;
	if( !image->path || !image->path[0] || Q_strstr( image->path, ".." )) return 0;
	pic = FS_LoadImage( image->path, NULL, 0 );
	if( !pic ) return 0;
	if( pic->type != PF_RGBA_32 ) // every TGA comes out RGBA (img_tga.c)
	{
		FS_FreeImage( pic );
		return 0;
	}
	image->wide = pic->width;
	image->tall = pic->height;
	bytes = pic->width * pic->height * 4;
	if( image->rgba )
	{
		if( image->size < bytes )
		{
			FS_FreeImage( pic );
			return 0;
		}
		memcpy( image->rgba, pic->buffer, bytes );
	}
	FS_FreeImage( pic );
	return 1;
}

// vguiapi_t::CofInventory (vgui_draw.c)
int CL_CoF_Inventory12Api( int op, int arg, void *buf, int size )
{
	switch( op )
	{
	case COF_INV_OP_GRID:
		return cof_inv12.slots == 12 && CL_CoF_Inventory12Ready( ) ? 12 : 0;
	case COF_INV_OP_THREE:
		return cof_inv12_store.three ? 1 : 0;
	case COF_INV_OP_SLOT:
		return buf && size == sizeof( cof_vgui_inv_item_t ) ? CL_CoF_Inv12Item( arg, (cof_vgui_inv_item_t *)buf ) : -1;
	case COF_INV_OP_COMMAND:
		return buf && CL_CoF_Inv12Command( (const char *)buf ) ? 1 : 0;
	case COF_INV_OP_SOUND:
		if( !buf || Q_strstr( (const char *)buf, ".." )) return 0;
		S_StartLocalSound( (const char *)buf, 1.0f, false );
		return 1;
	case COF_INV_OP_IMAGE:
		return buf && size == sizeof( cof_vgui_image_t ) ? CL_CoF_Inv12Image( (cof_vgui_image_t *)buf ) : 0;
	case COF_INV_OP_READY:
		cof_inv12.grid = arg != 0;
		CL_CoF_Inv12Advertise( );
		return 1;
	}
	return 0;
}

void CL_CoF_Inventory12Init( void )
{
	Cvar_RegisterVariable( &cof_inv12_userinfo );
	cof_inv12.registered = true;
	Cmd_AddCommand( "cof_inv_slots", CL_CoF_Inv12Slots_f, "story co-op: the server's inventory slot count for this map" );
	CL_CoF_Inv12Advertise( ); // the UI library may have reported before the cvar existed
}
