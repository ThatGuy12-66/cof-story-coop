/*
vgui_draw.c - vgui draw methods
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <string.h>
#include "common.h"
#include "client.h"
#include "vgui_draw.h"
#include "vgui_api.h"
#include "library.h"
#include "keydefs.h"
#include "ref_common.h"
#include "input.h"
#include "platform/platform.h"
#include "cof_language.h"
#include "cof_glyphs.h"

#define VGUI_MAX_TEXTURES 1024

typedef struct vgui_reusable_texture_s
{
	int gl_texturenum;
	byte hash[16];
	int cof_hud_kind; // exact decoded Classic health/stamina image
} vgui_reusable_texture_t;

typedef struct vgui_static_s
{
	qboolean initialized;
	VGUI_DefaultCursor cursor;
	vguiapi_t dllFuncs;
	qboolean from_client; // vgui_support API is provided by the client library

	vgui_reusable_texture_t *textures;
	int texture_id;
	int max_textures;
	int bound_texture;
	byte color[4];
	qboolean enable_texture;
	int paint_offset[2]; // in vgui coordinates, the renderer applies it to everything, including our own quads

	HINSTANCE hInstance;

	poolhandle_t mempool;

	enum VGUI_KeyCode virtualKeyTrans[256];
} vgui_static_t;

static vgui_static_t vgui = {
	false, -1
};
static CVAR_DEFINE_AUTO( vgui_utf8, "0", FCVAR_ARCHIVE, "enable utf-8 support for vgui text" );
static CVAR_DEFINE_AUTO( vgui_key_layout, "1", FCVAR_ARCHIVE, "translate keys sent to vgui through the keyboard layout, like GoldSrc does, so text entries type what's on the keycap" );

// Cry of Fear unified UI: remembers which keys and mouse buttons the support
// library was actually told about, so a release is only ever delivered for a
// press it received. Gating presses alone would leak the matching release into
// a CoF panel (measured: the 3D menu reacted to the escape release and
// re-issued "map c_game_menu1"), and gating releases alone would leave a key
// pressed in game stuck down inside VGUI after focus moved to the menu.
static byte vgui_key_delivered[256];

static qboolean VGui_CoF_DeliverKey( int key, int down )
{
	if( key < 0 || key >= (int)ARRAYSIZE( vgui_key_delivered ))
		return !CL_CoF_UIGateActive();

	if( down )
	{
		if( CL_CoF_UIGateActive( ))
			return false;

		vgui_key_delivered[key] = 1;
		return true;
	}

	if( !vgui_key_delivered[key] )
		return false;

	vgui_key_delivered[key] = 0;
	return true;
}

void VGui_CoF_ForgetKey( int key )
{
	if( key >= 0 && key < (int)ARRAYSIZE( vgui_key_delivered ))
		vgui_key_delivered[key] = 0;
}

/*
====================
VGui_CoF_OskCall

Cry of Fear on-screen keyboard (cof_osk.c): the support library's text entry
operations, vguiapi_t::CofOskCall. 0 when it has none (an older vgui.dll, no
text entry).
====================
*/
int VGui_CoF_OskCall( int op, char *buf, int size )
{
	if( !vgui.initialized || !vgui.dllFuncs.CofOskCall )
		return 0;

	return vgui.dllFuncs.CofOskCall( op, buf, size );
}

static void GAME_EXPORT VGUI_DrawInit( void )
{
	if( vgui.mempool )
		Mem_EmptyPool( vgui.mempool );
	else vgui.mempool = Mem_AllocPool( "VGui Support Pool" );

	vgui.textures = NULL;

	memset( vgui.color, 0, sizeof( vgui.color ));
	vgui.texture_id = 0;
	vgui.bound_texture = 0;
	vgui.max_textures = 0;
	vgui.enable_texture = true;
}

static void GAME_EXPORT VGUI_DrawShutdown( void )
{
	for( int i = 1; i < vgui.texture_id; i++ )
		ref.dllFuncs.GL_FreeTexture( vgui.textures[i].gl_texturenum );

	Mem_FreePool( &vgui.mempool );
	vgui.textures = NULL;

	memset( vgui.color, 0, sizeof( vgui.color ));
	vgui.texture_id = 0;
	vgui.bound_texture = 0;
	vgui.max_textures = 0;
}

static int GAME_EXPORT VGUI_GenerateTexture( void )
{
	// allocate new
	if( vgui.texture_id + 1 >= vgui.max_textures )
	{
		if( vgui.max_textures + VGUI_MAX_TEXTURES >= VGUI_MAX_TEXTURES * VGUI_MAX_TEXTURES )
		{
			// in theory it might look up texture that hasn't been bound for a while and
			// reuse that but it will eventually overwrite some important textures anyway
			Con_Printf( S_ERROR "%s: Refusing resizing VGUI textures array due to memory leak\n", __func__ );
			return vgui.texture_id;
		}

		vgui.max_textures += VGUI_MAX_TEXTURES;

		// this potentially might leak memory if VGUI is used incorrectly!
		// (like in Cry of Fear)
		vgui.textures = Mem_Realloc( vgui.mempool, vgui.textures, sizeof( *vgui.textures ) * vgui.max_textures );

		// warn mod developer
		if( vgui.max_textures >= VGUI_MAX_TEXTURES * 4 )
			Con_Printf( S_ERROR "%s: Potential memory leak in VGUI code is detected!\n", __func__ );
	}

	return ++vgui.texture_id;
}

static void GAME_EXPORT VGUI_UploadTexture( int id, const char *buffer, int width, int height )
{
	if( id <= 0 || id >= vgui.max_textures || width <= 0 || height <= 0 )
	{
		Con_DPrintf( S_ERROR "%s: bad texture %i. Ignored\n", __func__, id );
		return;
	}

	// need to do this as some mods tend to upload same texture over and over
	// exhausing engine-wide limit on textures and leaking vram
	MD5Context_t ctx;
	byte hash[16];

	MD5Init( &ctx );
	MD5Update( &ctx, buffer, width * height * 4 );
	MD5Final( hash, &ctx );
	vgui.textures[id].cof_hud_kind = CL_CoF_HudRemakeTexture( hash );

	// it's a new texture, try to find a copy
	if( vgui.textures[id].gl_texturenum == 0 )
	{
		for( int i = 1; i < vgui.texture_id; i++ )
		{
			if( vgui.textures[i].gl_texturenum != 0 && !memcmp( vgui.textures[i].hash, hash, sizeof( hash )))
			{
				// copy data to new texture id
				vgui.textures[id] = vgui.textures[i];
				return;
			}
		}
	}

	char texName[32];
	Q_snprintf( texName, sizeof( texName ), "*vgui%i", id );

	rgbdata_t r_image = {
		.width = width,
		.height = height,
		.type = PF_RGBA_32,
		.size = width * height * 4,
		.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
		.buffer = (byte*)buffer,
	};

	vgui.textures[id].gl_texturenum = GL_LoadTextureInternal( texName, &r_image, TF_IMAGE );
	memcpy( vgui.textures[id].hash, hash, sizeof( hash ));
}

static void GAME_EXPORT VGUI_CreateTexture( int id, int width, int height )
{
	// nothing uses it, it can be removed
	Host_Error( "%s: deprecated\n", __func__ );

}

static void GAME_EXPORT VGUI_UploadTextureBlock( int id, int drawX, int drawY, const byte *rgba, int blockWidth, int blockHeight )
{
	// nothing uses it, it can be removed
	Host_Error( "%s: deprecated\n", __func__ );
}

static void GAME_EXPORT VGUI_BindTexture( int id )
{
	if( id <= 0 || id >= vgui.max_textures || !vgui.textures[id].gl_texturenum )
		id = 1; // NOTE: same as bogus index 2700 in GoldSrc

	ref.dllFuncs.GL_Bind( XASH_TEXTURE0, vgui.textures[id].gl_texturenum );
	vgui.bound_texture = id;
}

static void GAME_EXPORT VGUI_GetTextureSizes( int *w, int *h )
{
	int texnum;

	if( vgui.bound_texture )
		texnum = vgui.textures[vgui.bound_texture].gl_texturenum;
	else
		texnum = R_GetBuiltinTexture( REF_DEFAULT_TEXTURE );

	R_GetTextureParms( w, h, texnum );
}

static void GAME_EXPORT VGUI_SetupDrawingRect( int *pColor )
{
	ref.dllFuncs.VGUI_SetupDrawing( true );
	Vector4Set( vgui.color, pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

static void GAME_EXPORT VGUI_SetupDrawingText( int *pColor )
{
	ref.dllFuncs.VGUI_SetupDrawing( false );
	Vector4Set( vgui.color, pColor[0], pColor[1], pColor[2], 255 - pColor[3] );
}

static void GAME_EXPORT VGUI_DrawQuad( const vpoint_t *ul, const vpoint_t *lr )
{
	if( !ul || !lr )
		return;

	float x = ul->point[0] - vgui.paint_offset[0];
	float y = ul->point[1] - vgui.paint_offset[1];
	float w = lr->point[0] - ul->point[0];
	float h = lr->point[1] - ul->point[1];

	// Cry of Fear in-game UI scaling: the support library hands us the client's
	// own layout pixels, this is where they become device pixels. It is applied
	// before SPR_AdjustSize so a hand-set hud_scale still composes with it
	CL_CoF_UIScaleRect( &x, &y, &w, &h );

	SPR_AdjustSize( &x, &y, &w, &h );

	if( CL_CoF_HudRemakeVguiQuad( vgui.enable_texture ? vgui.textures[vgui.bound_texture].cof_hud_kind : 0,
		vgui.enable_texture, x, y, w, h, vgui.color )) return;
	if( vgui.enable_texture )
	{
		float s1 = ul->coord[0];
		float t1 = ul->coord[1];
		float s2 = lr->coord[0];
		float t2 = lr->coord[1];

		ref.dllFuncs.Color4ub( vgui.color[0], vgui.color[1], vgui.color[2], vgui.color[3] );
		ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, vgui.textures[vgui.bound_texture].gl_texturenum );
	}
	else
	{
		ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, vgui.color[0], vgui.color[1], vgui.color[2], vgui.color[3] );
	}
}

static void GAME_EXPORT VGUI_EnableTexture( qboolean enable )
{
	vgui.enable_texture = enable;
}

static int GAME_EXPORT VGUI_CofUITransform( float *scale, int *layoutWide, int *layoutTall, int *screenWide, int *screenTall )
{
	return CL_CoF_UITransform( scale, layoutWide, layoutTall, screenWide, screenTall ) ? 1 : 0;
}

static int GAME_EXPORT VGUI_CofFontRoleLatch( char *buf, int size )
{
	return CL_CoF_FontRoleLatch( buf, size );
}

static int GAME_EXPORT VGUI_CofFontDesc( const char *role, cof_vgui_font_t *out )
{
	return CL_CoF_FontDesc( role, out ) ? 1 : 0;
}

static const unsigned char *GAME_EXPORT VGUI_CofFontFile( int semibold, int *size )
{
	return CL_CoF_FontFile( semibold, size );
}

/*
================
VGUI_CofPrint

milestone 4c: the support library's developer trace, into the engine console.
================
*/
static void GAME_EXPORT VGUI_CofPrint( const char *text )
{
	if( COM_StringEmptyOrNULL( text ))
		return;

	// Con_Printf is gated on host.allow_console, and an unlocked console is
	// also the configuration in which the client's own VGUI layer does NOT
	// paint (it owns key_dest, and XashPaint returns early) - so the one
	// configuration this trace exists for is the one Con_Printf drops. Write
	// straight to the log file when the console is locked.
	if( host.allow_console )
		Con_Printf( "%s", text );
	else Sys_PrintLog( text );
}

static const char *GAME_EXPORT VGUI_CofLangString( const char *str )
{
	return CoF_Lang_String( str );
}

static const unsigned char *GAME_EXPORT VGUI_CofFontFileCJK( int bold, int *size )
{
	return CoF_Glyphs_CJKFile( bold, size );
}

static int GAME_EXPORT VGUI_CofTextGlyphs( const unsigned int **out, int *generation )
{
	const uint32_t *list = NULL;
	int count = CoF_Lang_Glyphs( &list, generation );

	if( out )
		*out = (const unsigned int *)list;
	return count;
}

// a glyph page that gained glyphs: the same id, its pixels again (VGUI_UploadTexture keeps an id's first upload)
static void GAME_EXPORT VGUI_CofUpdateTexture( int id, const char *rgba, int width, int height )
{
	char texName[32];

	if( id <= 0 || id >= vgui.max_textures || width <= 0 || height <= 0 || !rgba )
		return;

	Q_snprintf( texName, sizeof( texName ), "*vgui%i", id );

	rgbdata_t r_image = {
		.width = width,
		.height = height,
		.type = PF_RGBA_32,
		.size = width * height * 4,
		.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
		.buffer = (byte*)rgba,
	};

	vgui.textures[id].gl_texturenum = ref.dllFuncs.GL_LoadTextureFromBuffer( texName, &r_image, TF_IMAGE, vgui.textures[id].gl_texturenum != 0 );
	memset( vgui.textures[id].hash, 0, sizeof( vgui.textures[id].hash )); // never a copy source: it changes
	vgui.textures[id].cof_hud_kind = 0;
}

static int GAME_EXPORT VGUI_CofFontProbe( char *text, int size, char *role, int roleSize )
{
	return CL_CoF_FontProbe( text, size, role, roleSize );
}

static void GAME_EXPORT VGUI_SetPaintOffset( int x, int y )
{
	float fx = x, fy = y, fw = 0.0f, fh = 0.0f;

	Vector2Set( vgui.paint_offset, x, y );

	// the offset is in the same layout pixels VGUI_DrawQuad() subtracts it from,
	// so it goes through exactly the same scale
	CL_CoF_UIScaleRect( &fx, &fy, &fw, &fh );

	SPR_AdjustSize( &fx, &fy, &fw, &fh );
	ref.dllFuncs.R_Set2DOffset( fx, fy );
}

static void GAME_EXPORT *VGUI_EngineMalloc( size_t size )
{
	return Z_Malloc( size );
}

static qboolean GAME_EXPORT VGUI_IsInGame( void )
{
	// Cry of Fear on-screen keyboard: the support library paints nothing
	// unless the game is "in game"; while the menu's keyboard types into a
	// game panel, that panel must keep painting (cof_osk.c)
	return cls.state == ca_active && ( cls.key_dest == key_game || CL_CoF_OskOverGame( ));
}

static void GAME_EXPORT VGUI_GetMousePos( int *_x, int *_y )
{
	// Cry of Fear in-game UI scaling: the exact inverse of the scale
	// VGUI_DrawQuad() applies, so the cursor lands on the same layout pixel the
	// panel under it was drawn from. The anchor half of the transform moved the
	// panels themselves (CofUI_SolvePanel), so it needs no inverse here
	float cof_scale = CL_CoF_UIScale();
	float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth * cof_scale;
	float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight * cof_scale;
	int x, y;

	// Cry of Fear gamepad: while the stick drives the panel cursor, FreeVGUI's
	// per-frame poll reads that position (cof_gamepad.c)
	if( !CL_CoF_VCursorPos( &x, &y ))
		Platform_GetMousePos( &x, &y );
	*_x = x / xscale;
	*_y = y / yscale;
}

static void GAME_EXPORT VGUI_CursorSelect( VGUI_DefaultCursor cursor )
{
	if( vgui.cursor != cursor )
		Platform_SetCursorType( cursor );
}

static byte GAME_EXPORT VGUI_GetColor( int i, int j )
{
	return g_color_table[i][j];
}

static int GAME_EXPORT VGUI_UtfProcessChar( int in )
{
	if( vgui_utf8.value )
		return Con_UtfProcessCharForce( in );
	return in;
}

qboolean VGui_IsActive( void )
{
	return vgui.initialized;
}

qboolean VGui_IsProvidedByClientDll( void )
{
	return vgui.from_client;
}

void VGui_RegisterCvars( void )
{
	Cvar_RegisterVariable( &vgui_utf8 );
	Cvar_RegisterVariable( &vgui_key_layout );
}

int CL_CoF_PanelReport( unsigned int visible ); // cof_panel_pause.c
int CL_CoF_PanelConfig( cof_vgui_panel_cfg_t *out ); // cof_panel_transparency.c
int CL_CoF_PanelImageLatch( char *buf, int size ); // cof_panel_transparency.c
int CL_CoF_Inventory12Api( int op, int arg, void *buf, int size ); // cof_inventory12.c

static const vguiapi_t gEngfuncs =
{
	false, // Not initialized yet
	VGUI_DrawInit, // VGUI_DrawInit,
	VGUI_DrawShutdown, // VGUI_DrawShutdown,
	VGUI_SetupDrawingText, // VGUI_SetupDrawingText,
	VGUI_SetupDrawingRect, // VGUI_SetupDrawingRect,
	VGUI_SetupDrawingText, // VGUI_SetupDrawingImage, (same as text)
	VGUI_BindTexture, // VGUI_BindTexture,
	VGUI_EnableTexture, // VGUI_EnableTexture,
	VGUI_CreateTexture, // VGUI_CreateTexture,
	VGUI_UploadTexture, // VGUI_UploadTexture,
	VGUI_UploadTextureBlock, // VGUI_UploadTextureBlock,
	VGUI_DrawQuad, // VGUI_DrawQuad,
	VGUI_GetTextureSizes, // VGUI_GetTextureSizes,
	VGUI_GenerateTexture, // VGUI_GenerateTexture,
	VGUI_EngineMalloc,
	VGUI_CursorSelect,
	VGUI_GetColor,
	VGUI_IsInGame,
	Key_EnableTextInput,
	VGUI_GetMousePos,
	VGUI_UtfProcessChar,
	Platform_GetClipboardText,
	Platform_SetClipboardText,
	Platform_GetKeyModifiers,
	NULL, // Startup
	NULL, // Shutdown
	NULL, // GetPanel
	NULL, // Paint
	NULL, // Mouse
	NULL, // Key
	NULL, // MouseMove
	NULL, // TextInput
	VGUI_SetPaintOffset,
	CL_CoF_OskEntry, // CofOskEntry (cof_osk.c)
	NULL, // CofOskCall, filled in by the support library
	VGUI_CofUITransform,
	VGUI_CofFontRoleLatch,
	VGUI_CofFontDesc,
	VGUI_CofFontFile,
	VGUI_CofFontProbe,
	VGUI_CofPrint,
	VGUI_CofLangString,
	CL_CoF_PanelReport,
	CL_CoF_PanelConfig,
	CL_CoF_PanelImageLatch,
	NULL, // CofPanelAction, filled in by the support library
	CL_CoF_Inventory12Api, // CofInventory (cof_inventory12.c)
	VGUI_CofFontFileCJK,
	VGUI_CofTextGlyphs,
	VGUI_CofUpdateTexture,
};

qboolean VGui_LoadProgs( HINSTANCE hInstance )
{
	qboolean client = hInstance != NULL;

	vgui.dllFuncs = gEngfuncs;

	// not loading interface from client.dll, load vgui_support.dll instead
	if( !client )
	{
		string vguiloader, vguilib;

		// HACKHACK: try to load path from custom path
		// to support having different versions of VGUI
		if( Sys_GetParmFromCmdLine( "-vguilib", vguilib ) && !COM_LoadLibrary( vguilib, false, false ))
		{
			Con_Reportf( S_WARN "VGUI preloading failed. Default library will be used! Reason: %s", COM_GetLibraryError());
		}

		if( !Sys_GetParmFromCmdLine( "-vguiloader", vguiloader ))
		{
			Q_strncpy( vguiloader, "vgui." OS_LIB_EXT, sizeof( vguiloader ));
		}

		hInstance = vgui.hInstance = COM_LoadLibrary( vguiloader, false, false );

		if( !vgui.hInstance )
		{
			Con_Reportf( S_ERROR "Failed to load vgui_support library: %s\n", COM_GetLibraryError() );
			return false;
		}
	}

	// try legacy API first
	void (*F)( vguiapi_t* ) = COM_GetProcAddress( hInstance, client ? "InitVGUISupportAPI" : "InitAPI" );

	if( F )
	{
		F( &vgui.dllFuncs );

		vgui.initialized = vgui.dllFuncs.initialized = true;
		vgui.from_client = client;
		Con_Reportf( "%s: initialized legacy API in %s module\n", __func__, client ? "client" : "support" );

		return true;
	}

	Con_Reportf( S_ERROR "%s: Failed to find VGUI support API entry point in %s module\n", __func__, client ? "client" : "support" );
	return false;
}

/*
================
VGui_Startup

================
*/
void VGui_Startup( int width, int height )
{
	// vgui not initialized from both support and client modules, skip
	if( !vgui.initialized )
		return;

	// Cry of Fear in-game UI scaling: the root panel has to cover everything the
	// anchor pass may move, which at the client's first VidInit is exactly the
	// render size it is called with here
	CL_CoF_UIRootSize( &width, &height );

	height = Q_max( 480, height );

	if( width <= 640 ) width = 640;
	else if( width <= 800 ) width = 800;
	else if( width <= 1024 ) width = 1024;
	else if( width <= 1152 ) width = 1152;
	else if( width <= 1280 ) width = 1280;
	else if( width <= 1600 ) width = 1600;

	if( vgui.dllFuncs.Startup )
		vgui.dllFuncs.Startup( width, height );
}



/*
================
VGui_Shutdown

Unload vgui_support library and call VGui_Shutdown
================
*/
void VGui_Shutdown( void )
{
	if( vgui.dllFuncs.Shutdown )
		vgui.dllFuncs.Shutdown();

	if( vgui.hInstance )
		COM_FreeLibrary( vgui.hInstance );

	// drop pointers to now unloaded vgui_support
	vgui.dllFuncs = gEngfuncs;
	vgui.hInstance = NULL;
	vgui.from_client = false;
}


static void VGUI_InitKeyTranslationTable( void )
{
	static qboolean initialized = false;

	if( initialized ) return;

	initialized = true;

	// set virtual key translation table
	memset( vgui.virtualKeyTrans, -1, sizeof( vgui.virtualKeyTrans ) );

	// TODO: engine keys are not enough here!
	// make crossplatform way to pass SDL keys here

	vgui.virtualKeyTrans['0'] = KEY_0;
	vgui.virtualKeyTrans['1'] = KEY_1;
	vgui.virtualKeyTrans['2'] = KEY_2;
	vgui.virtualKeyTrans['3'] = KEY_3;
	vgui.virtualKeyTrans['4'] = KEY_4;
	vgui.virtualKeyTrans['5'] = KEY_5;
	vgui.virtualKeyTrans['6'] = KEY_6;
	vgui.virtualKeyTrans['7'] = KEY_7;
	vgui.virtualKeyTrans['8'] = KEY_8;
	vgui.virtualKeyTrans['9'] = KEY_9;
	vgui.virtualKeyTrans['A'] = vgui.virtualKeyTrans['a'] = KEY_A;
	vgui.virtualKeyTrans['B'] = vgui.virtualKeyTrans['b'] = KEY_B;
	vgui.virtualKeyTrans['C'] = vgui.virtualKeyTrans['c'] = KEY_C;
	vgui.virtualKeyTrans['D'] = vgui.virtualKeyTrans['d'] = KEY_D;
	vgui.virtualKeyTrans['E'] = vgui.virtualKeyTrans['e'] = KEY_E;
	vgui.virtualKeyTrans['F'] = vgui.virtualKeyTrans['f'] = KEY_F;
	vgui.virtualKeyTrans['G'] = vgui.virtualKeyTrans['g'] = KEY_G;
	vgui.virtualKeyTrans['H'] = vgui.virtualKeyTrans['h'] = KEY_H;
	vgui.virtualKeyTrans['I'] = vgui.virtualKeyTrans['i'] = KEY_I;
	vgui.virtualKeyTrans['J'] = vgui.virtualKeyTrans['j'] = KEY_J;
	vgui.virtualKeyTrans['K'] = vgui.virtualKeyTrans['k'] = KEY_K;
	vgui.virtualKeyTrans['L'] = vgui.virtualKeyTrans['l'] = KEY_L;
	vgui.virtualKeyTrans['M'] = vgui.virtualKeyTrans['m'] = KEY_M;
	vgui.virtualKeyTrans['N'] = vgui.virtualKeyTrans['n'] = KEY_N;
	vgui.virtualKeyTrans['O'] = vgui.virtualKeyTrans['o'] = KEY_O;
	vgui.virtualKeyTrans['P'] = vgui.virtualKeyTrans['p'] = KEY_P;
	vgui.virtualKeyTrans['Q'] = vgui.virtualKeyTrans['q'] = KEY_Q;
	vgui.virtualKeyTrans['R'] = vgui.virtualKeyTrans['r'] = KEY_R;
	vgui.virtualKeyTrans['S'] = vgui.virtualKeyTrans['s'] = KEY_S;
	vgui.virtualKeyTrans['T'] = vgui.virtualKeyTrans['t'] = KEY_T;
	vgui.virtualKeyTrans['U'] = vgui.virtualKeyTrans['u'] = KEY_U;
	vgui.virtualKeyTrans['V'] = vgui.virtualKeyTrans['v'] = KEY_V;
	vgui.virtualKeyTrans['W'] = vgui.virtualKeyTrans['w'] = KEY_W;
	vgui.virtualKeyTrans['X'] = vgui.virtualKeyTrans['x'] = KEY_X;
	vgui.virtualKeyTrans['Y'] = vgui.virtualKeyTrans['y'] = KEY_Y;
	vgui.virtualKeyTrans['Z'] = vgui.virtualKeyTrans['z'] = KEY_Z;

	vgui.virtualKeyTrans[K_KP_5 - 5] = KEY_PAD_0;
	vgui.virtualKeyTrans[K_KP_5 - 4] = KEY_PAD_1;
	vgui.virtualKeyTrans[K_KP_5 - 3] = KEY_PAD_2;
	vgui.virtualKeyTrans[K_KP_5 - 2] = KEY_PAD_3;
	vgui.virtualKeyTrans[K_KP_5 - 1] = KEY_PAD_4;
	vgui.virtualKeyTrans[K_KP_5 - 0] = KEY_PAD_5;
	vgui.virtualKeyTrans[K_KP_5 + 1] = KEY_PAD_6;
	vgui.virtualKeyTrans[K_KP_5 + 2] = KEY_PAD_7;
	vgui.virtualKeyTrans[K_KP_5 + 3] = KEY_PAD_8;
	vgui.virtualKeyTrans[K_KP_5 + 4] = KEY_PAD_9;
	vgui.virtualKeyTrans[K_KP_SLASH] = KEY_PAD_DIVIDE;
	vgui.virtualKeyTrans['*']        = KEY_PAD_MULTIPLY;
	vgui.virtualKeyTrans[K_KP_MINUS] = KEY_PAD_MINUS;
	vgui.virtualKeyTrans[K_KP_PLUS]  = KEY_PAD_PLUS;
	vgui.virtualKeyTrans[K_KP_ENTER] = KEY_PAD_ENTER;
	vgui.virtualKeyTrans[K_KP_NUMLOCK] = KEY_NUMLOCK;
	vgui.virtualKeyTrans['['] = KEY_LBRACKET;
	vgui.virtualKeyTrans[']'] = KEY_RBRACKET;
	vgui.virtualKeyTrans[';'] = KEY_SEMICOLON;
	vgui.virtualKeyTrans['`'] = KEY_BACKQUOTE;
	vgui.virtualKeyTrans[','] = KEY_COMMA;
	vgui.virtualKeyTrans['.'] = KEY_PERIOD;
	vgui.virtualKeyTrans['-'] = KEY_MINUS;
	vgui.virtualKeyTrans['='] = KEY_EQUAL;
	vgui.virtualKeyTrans['/'] = KEY_SLASH;
	vgui.virtualKeyTrans['\\'] = KEY_BACKSLASH;
	vgui.virtualKeyTrans['\''] = KEY_APOSTROPHE;
	vgui.virtualKeyTrans[K_TAB] = KEY_TAB;
	vgui.virtualKeyTrans[K_ENTER] = KEY_ENTER;
	vgui.virtualKeyTrans[K_SPACE] = KEY_SPACE;
	vgui.virtualKeyTrans[K_CAPSLOCK] = KEY_CAPSLOCK;
	vgui.virtualKeyTrans[K_BACKSPACE] = KEY_BACKSPACE;
	vgui.virtualKeyTrans[K_ESCAPE]	= KEY_ESCAPE;
	vgui.virtualKeyTrans[K_INS] = KEY_INSERT;
	vgui.virtualKeyTrans[K_DEL] = KEY_DELETE;
	vgui.virtualKeyTrans[K_HOME] = KEY_HOME;
	vgui.virtualKeyTrans[K_END] = KEY_END;
	vgui.virtualKeyTrans[K_PGUP] = KEY_PAGEUP;
	vgui.virtualKeyTrans[K_PGDN] = KEY_PAGEDOWN;
	vgui.virtualKeyTrans[K_PAUSE] = KEY_BREAK;
	vgui.virtualKeyTrans[K_SHIFT] = KEY_LSHIFT;	// SHIFT -> left SHIFT
	vgui.virtualKeyTrans[K_ALT] = KEY_LALT;		// ALT -> left ALT
	vgui.virtualKeyTrans[K_CTRL] = KEY_LCONTROL;	// CTRL -> left CTRL
	vgui.virtualKeyTrans[K_WIN] = KEY_LWIN;
	vgui.virtualKeyTrans[K_UPARROW] = KEY_UP;
	vgui.virtualKeyTrans[K_LEFTARROW] = KEY_LEFT;
	vgui.virtualKeyTrans[K_DOWNARROW] = KEY_DOWN;
	vgui.virtualKeyTrans[K_RIGHTARROW] = KEY_RIGHT;
	vgui.virtualKeyTrans[K_F1] = KEY_F1;
	vgui.virtualKeyTrans[K_F2] = KEY_F2;
	vgui.virtualKeyTrans[K_F3] = KEY_F3;
	vgui.virtualKeyTrans[K_F4] = KEY_F4;
	vgui.virtualKeyTrans[K_F5] = KEY_F5;
	vgui.virtualKeyTrans[K_F6] = KEY_F6;
	vgui.virtualKeyTrans[K_F7] = KEY_F7;
	vgui.virtualKeyTrans[K_F8] = KEY_F8;
	vgui.virtualKeyTrans[K_F9] = KEY_F9;
	vgui.virtualKeyTrans[K_F10] = KEY_F10;
	vgui.virtualKeyTrans[K_F11] = KEY_F11;
	vgui.virtualKeyTrans[K_F12] = KEY_F12;
}

static enum VGUI_KeyCode VGUI_MapKey( int keyCode )
{
	VGUI_InitKeyTranslationTable();

	if( keyCode >= 0 && keyCode < ARRAYSIZE( vgui.virtualKeyTrans ))
		return vgui.virtualKeyTrans[keyCode];

	return (enum VGUI_KeyCode)-1;
}

void VGui_MouseEvent( int key, int clicks )
{
	enum VGUI_MouseAction mact;
	enum VGUI_MouseCode   code;

	if( !vgui.dllFuncs.Mouse )
		return;

	// see VGui_KeyEvent: a release is only delivered for a press VGUI received
	if( !VGui_CoF_DeliverKey( key, clicks >= 1 ))
		return;

	switch( key )
	{
	case K_MOUSE1: code = MOUSE_LEFT; break;
	case K_MOUSE2: code = MOUSE_RIGHT; break;
	case K_MOUSE3: code = MOUSE_MIDDLE; break;
	default: return;
	}

	if( clicks >= 2 )
		mact = MA_DOUBLE;
	else if( clicks == 1 )
		mact = MA_PRESSED;
	else
		mact = MA_RELEASED;

	vgui.dllFuncs.Mouse( mact, code );
}

void VGui_MWheelEvent( int y )
{
	if( !vgui.dllFuncs.Mouse )
		return;

	if( CL_CoF_UIGateActive( ))
		return;

	vgui.dllFuncs.Mouse( MA_WHEEL, y );
}

void VGui_KeyEvent( int key, int down )
{
	enum VGUI_KeyCode code;

	if( !vgui.dllFuncs.Key )
		return;

	if( !VGui_CoF_DeliverKey( key, down ))
		return;

	// VGUI wants the character printed on the keycap, not the physical key position, so text entries work on non-QWERTY layouts
	// in the future we might want to partially (at least we can control TextEntry and friends implementation now in freevgui)
	// to handle this through SDL's text mode.
	if( vgui_key_layout.value )
		key = Platform_TranslateKeyLayout( key );

	if(( code = VGUI_MapKey( key )) < 0 )
		return;

	if( down )
	{
		vgui.dllFuncs.Key( KA_PRESSED, code );
		vgui.dllFuncs.Key( KA_TYPED, code );
	}
	else vgui.dllFuncs.Key( KA_RELEASED, code );
}

void VGui_MouseMove( int x, int y )
{
	// Cry of Fear unified UI: only one cursor may be live at a time. While the
	// engine menu or the console owns input, UI_MouseMove drives the menu cursor
	// and the client's VGUI cursor is frozen where it was.
	if( CL_CoF_UIGateActive( ))
		return;

	if( vgui.dllFuncs.MouseMove )
	{
		// same inverse as VGUI_GetMousePos() above
		float cof_scale = CL_CoF_UIScale();
		float xscale = (float)refState.width / (float)clgame.scrInfo.iWidth * cof_scale;
		float yscale = (float)refState.height / (float)clgame.scrInfo.iHeight * cof_scale;
		vgui.dllFuncs.MouseMove( x / xscale, y / yscale );
	}
}

void VGui_Paint( void )
{
	// Cry of Fear unified UI: nothing from the client's VGUI1 layer may be drawn
	// underneath the engine menu or the console. This is what keeps the CoF 3D
	// menu overlay off the screen while mainui owns it, and what will keep the
	// CoF HUD from bleeding through a translucent pause scrim later.
	// The one exception is the menu's on-screen keyboard typing into one of
	// these panels (the computer): that panel must stay visible. The same
	// call runs the keyboard's once-a-frame work (cof_osk.c).
	const qboolean osk_over_game = CL_CoF_OskFrame( );

	if( CL_CoF_UIGateActive( ) && !osk_over_game )
		return;

	CL_CoF_HudRemakeVguiBegin();
	if( vgui.dllFuncs.Paint )
		vgui.dllFuncs.Paint();

	// don't trust the support library to leave it clean, everything drawn afterwards is in screen space
	VGUI_SetPaintOffset( 0, 0 );

	// Cry of Fear gamepad: the ring cursor over the panels (cof_gamepad.c)
	CL_CoF_HudRemakeBars();
	CL_CoF_VCursorDraw( );
}

/*
================
VGui_CoF_PanelAction

Cry of Fear gamepad (cof_gamepad.c): an action on the client's open panel
through its own buttons (vguiapi_t::CofPanelAction, COF_VGUI_PANEL_*). 0 when
the support library does not offer it.
================
*/
int VGui_CoF_PanelAction( int action, char *info, int infoSize )
{
	if( info && infoSize > 0 && action != COF_VGUI_PANEL_LOCATE )
		info[0] = 0;

	if( !vgui.initialized || !vgui.dllFuncs.CofPanelAction )
		return 0;

	return vgui.dllFuncs.CofPanelAction( action, info, infoSize );
}

void VGui_UpdateInternalCursorState( VGUI_DefaultCursor cursorType )
{
	vgui.cursor = cursorType;
}

void *GAME_EXPORT VGui_GetPanel( void )
{
	if( vgui.dllFuncs.GetPanel )
		return vgui.dllFuncs.GetPanel();
	return NULL;
}

void VGui_ReportTextInput( const char *text )
{
	// Cry of Fear unified UI: characters typed into the console or a menu field
	// must not also land in a CoF text entry (the computer, the phone keypad).
	if( CL_CoF_UIGateActive( ))
		return;

	if( vgui.dllFuncs.TextInput )
		vgui.dllFuncs.TextInput( text );
}

