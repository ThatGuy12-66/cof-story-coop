/*
vgui_api.h - vgui_support library interface
Copyright (C) 2015 Mittorn

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
#ifndef VGUI_API_H
#define VGUI_API_H

#include <stddef.h>
#include "key_modifiers.h"
#include "cursor_type.h"

// VGUI generic vertex

typedef struct
{
	float	point[2];
	float	coord[2];
} vpoint_t;

// C-Style VGUI enums

enum VGUI_MouseCode
{
	MOUSE_LEFT=0,
	MOUSE_RIGHT,
	MOUSE_MIDDLE,
	MOUSE_LAST
};

enum VGUI_KeyCode
{
	KEY_0=0,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_A,
	KEY_B,
	KEY_C,
	KEY_D,
	KEY_E,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_I,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_M,
	KEY_N,
	KEY_O,
	KEY_P,
	KEY_Q,
	KEY_R,
	KEY_S,
	KEY_T,
	KEY_U,
	KEY_V,
	KEY_W,
	KEY_X,
	KEY_Y,
	KEY_Z,
	KEY_PAD_0,
	KEY_PAD_1,
	KEY_PAD_2,
	KEY_PAD_3,
	KEY_PAD_4,
	KEY_PAD_5,
	KEY_PAD_6,
	KEY_PAD_7,
	KEY_PAD_8,
	KEY_PAD_9,
	KEY_PAD_DIVIDE,
	KEY_PAD_MULTIPLY,
	KEY_PAD_MINUS,
	KEY_PAD_PLUS,
	KEY_PAD_ENTER,
	KEY_PAD_DECIMAL,
	KEY_LBRACKET,
	KEY_RBRACKET,
	KEY_SEMICOLON,
	KEY_APOSTROPHE,
	KEY_BACKQUOTE,
	KEY_COMMA,
	KEY_PERIOD,
	KEY_SLASH,
	KEY_BACKSLASH,
	KEY_MINUS,
	KEY_EQUAL,
	KEY_ENTER,
	KEY_SPACE,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_CAPSLOCK,
	KEY_NUMLOCK,
	KEY_ESCAPE,
	KEY_SCROLLLOCK,
	KEY_INSERT,
	KEY_DELETE,
	KEY_HOME,
	KEY_END,
	KEY_PAGEUP,
	KEY_PAGEDOWN,
	KEY_BREAK,
	KEY_LSHIFT,
	KEY_RSHIFT,
	KEY_LALT,
	KEY_RALT,
	KEY_LCONTROL,
	KEY_RCONTROL,
	KEY_LWIN,
	KEY_RWIN,
	KEY_APP,
	KEY_UP,
	KEY_LEFT,
	KEY_DOWN,
	KEY_RIGHT,
	KEY_F1,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	KEY_F6,
	KEY_F7,
	KEY_F8,
	KEY_F9,
	KEY_F10,
	KEY_F11,
	KEY_F12,
	KEY_LAST
};

enum VGUI_KeyAction
{
	KA_TYPED=0,
	KA_PRESSED,
	KA_RELEASED
};
enum VGUI_MouseAction
{
	MA_PRESSED=0,
	MA_RELEASED,
	MA_DOUBLE,
	MA_WHEEL
};

// Cry of Fear engine-rasterised VGUI fonts (milestone 4): what the engine
// tells the support library about one font role.
typedef struct
{
	int	devicePx;	// rasterise the glyphs this many device pixels tall
	int	logicalPx;	// and report every metric at this height, so that the
				// cof_ui_scale surface transform maps the atlas onto
				// exactly the device pixels it was rasterised for
	int	codepage;	// 1250, 1251 or 1252: how a text byte becomes a glyph
	int	semibold;	// face: 0 Regular, 1 SemiBold, 2 Bold transient style (same ABI)
	int	ytarget;	// milestone 4c: where this role's text belongs, as a
				// DEVICE y for the first line of a run; the support
				// library shifts the whole run so that line lands
				// there and keeps the spacing of the rest. -1 leaves
				// the client's own placement alone, which is every
				// role but the in-game message text
	int	trace;		// developer: log this role's text lines
	int	backing;	// milestone 4b: alpha 0-255 of the translucent strip to
				// draw behind each line of this role's text, 0 = none.
				// Only the notes/document reader ("pager") asks for one,
				// so no other client label, and not the boss bar or the
				// inventory, is touched by it
	int	overflow;	// cof-coop-bridge: a collected line wider than its own
				// label on BOTH sides (a centred caption sized for the
				// game's narrower bitmap font, e.g. the co-op lobby
				// hints) may use its parent panel's width instead of
				// being cut at the label's edges. Global, like trace
} cof_vgui_font_t;

// Cry of Fear panel pause (cof_panel_pause): the client's in-play panels the
// support library recognises by their class (the MSVC run-time type
// information client.dll carries), one bit each, reported every painted frame
// through CofPanelReport below. See engine/client/cof_panel_pause.c.
enum
{
	COF_PANEL_INVENTORY	= 1 << 0,	// CRECBInventory
	COF_PANEL_DOCUMENTS	= 1 << 1,	// CDocuments, the notes list
	COF_PANEL_NOTE		= 1 << 2,	// CNoteDocument
	COF_PANEL_USERDOC	= 1 << 3,	// CUserDocument
	COF_PANEL_BILLBOARD	= 1 << 4,	// CVGUIBillboard
	COF_PANEL_YESNO		= 1 << 5,	// CYesno
	COF_PANEL_SAVELOAD	= 1 << 6,	// CSaveLoad, the tape recorder's SAVED GAMES page
	COF_PANEL_SAVECONFIRM	= 1 << 7,	// CSave, its confirm box
	COF_PANEL_PHONE		= 1 << 8,	// CPhone
	COF_PANEL_TELEPHONE	= 1 << 9,	// CTelephone, the landline
	COF_PANEL_COMPUTER	= 1 << 10,	// CComputer
	COF_PANEL_PADLOCK	= 1 << 11,	// CPadlock
	COF_PANEL_PUZZLEBAR	= 1 << 12,	// CPuzzleBar, the boiler puzzle
	COF_PANEL_KEYPAD	= 1 << 13,	// CKeypad
	COF_PANEL_STATUE	= 1 << 14,	// CAnimalStatue
	COF_PANEL_TEXTWINDOW	= 1 << 15,	// CParanoiaTextPanel
	COF_PANEL_OTHER		= 1 << 16,	// any other modal client panel (menu-map pages, game over, gallery)
	COF_PANEL_HUD		= 1 << 17,	// CHUDControl, the HUD container (not modal)
};

// Cry of Fear world behind the panels (cof_panel_transparent): what the
// engine tells the support library, once per painted frame, through
// CofPanelConfig below. See engine/client/cof_panel_transparency.c.
typedef struct
{
	int	transparent;	// skip the client's black backdrops behind its in-play panels
	int	plateAlpha;	// 0-255 opacity of the backgroundtoall.tga plate while they are skipped
	int	hideHud;	// 0 keep the HUD container, 1 hide it, 2 hide the pieces that overlap the panel
	int	trace;		// cof_panel_trace level
} cof_vgui_panel_cfg_t;

typedef struct  vguiapi_s
{
	int	initialized;
	// called from vgui_support
	void	(*DrawInit)( void );
	void	(*DrawShutdown)( void );
	void	(*SetupDrawingText)( int *pColor );
	void	(*SetupDrawingRect)( int *pColor );
	void	(*SetupDrawingImage)( int *pColor );
	void	(*BindTexture)( int id );
	void	(*EnableTexture)( int enable );
	void	(*Reserved0)( int id, int width, int height );
	void	(*UploadTexture)( int id, const char *buffer, int width, int height );
	void	(*Reserved1)( int id, int drawX, int drawY, const unsigned char *rgba, int blockWidth, int blockHeight );
	void	(*DrawQuad)( const vpoint_t *ul, const vpoint_t *lr );
	void	(*GetTextureSizes)( int *width, int *height );
	int		(*GenerateTexture)( void );
	void	*(*EngineMalloc)( size_t size );
	void	(*CursorSelect)( VGUI_DefaultCursor cursor );
	unsigned char	(*GetColor)( int i, int j );
	int		(*IsInGame)( void );
	void	(*EnableTextInput)( int enable, int force );
	void	(*GetCursorPos)( int *x, int *y );
	int		(*ProcessUtfChar)( int ch );
	int		(*GetClipboardText)( char *buffer, size_t bufferSize );
	void	(*SetClipboardText)( const char *text );
	key_modifier_t (*GetKeyModifiers)( void );
	// called from engine side
	void	(*Startup)( int width, int height );
	void	(*Shutdown)( void );
	void	*(*GetPanel)( void );
	void	(*Paint)( void );
	void	(*Mouse)( enum VGUI_MouseAction action, int code );
	void	(*Key)( enum VGUI_KeyAction action, enum VGUI_KeyCode code );
	void	(*MouseMove)( int x, int y );
	void	(*TextInput)( const char *text );

	// called from vgui_support, appended later so older support libraries keep working
	void	(*SetPaintOffset)( int x, int y );	// translates 2D drawing for the everything that's not VGUI, used to simulate GoldSrc behavior which installs matrix in push/popMakeCurrent

	// Cry of Fear on-screen keyboard (engine/client/cof_osk.c). CofOskEntry,
	// called by the support library: a TextEntry lost (0) or got (1) the key
	// focus, or was clicked (2); hidden = it shows asterisks (a password);
	// text = what it holds. CofOskCall, filled in by the support library the
	// first time it reports: op 0 puts buf into the focused TextEntry, op 1
	// (test hook) focuses the visible TextEntry number atoi(buf), op 2 copies
	// the focused TextEntry's text into buf. Returns 0 when there is no such
	// entry. xash.dll and vgui.dll must come from the same build.
	void	(*CofOskEntry)( int event, int hidden, const char *text );
	int	(*CofOskCall)( int op, char *buf, int size );

	// Cry of Fear in-game UI scaling (cof_ui_scale, milestone 4). Fills the
	// current transform and returns non-zero while it is active: `scale` is the
	// layout-pixel to device-pixel factor the engine applies to every VGUI quad,
	// `layout*` is the screen size the client's frozen panel layout was built
	// for, `screen*` is the render size now. The support library uses the three
	// to anchor each top-level client panel to the screen edge it was laid out
	// against, which is what keeps a magnified HUD in its corner and a magnified
	// inventory centred. See engine/client/cl_main.c.
	int	(*CofUITransform)( float *scale, int *layoutWide, int *layoutTall, int *screenWide, int *screenTall );

	// Cry of Fear engine-rasterised VGUI fonts (cof_ui_inter_fonts, milestone 4).
	//
	// The game's own VGUI text comes from bitmap strips whose sizes stopped
	// changing at the 1024 resolution bucket, so the engine substitutes a font
	// of its own, rasterised from Inter at a size derived from the render
	// height. These three entries are everything the support library needs.
	//
	// CofFontRoleLatch consumes the scheme name the client last asked for
	// through COM_LoadFile( "gfx/vgui/fonts/<res>/<scheme>.tga" ), which is how
	// a font object built immediately afterwards is identified: the client
	// constructs every one of them as Font( "Arial", ..., tall = 20 ), so
	// neither the name nor the size tells them apart. Returns 0 when nothing
	// is latched.
	//
	// CofFontDesc answers the size and code page for a role, and 0 when the
	// client's own strip should be used instead.
	//
	// CofFontFile returns the engine-owned TTF bytes, valid until shutdown.
	int	(*CofFontRoleLatch)( char *buf, int size );
	int	(*CofFontDesc)( const char *role, cof_vgui_font_t *out );
	const unsigned char *(*CofFontFile)( int semibold, int *size );

	// cof_font_probe: draw this string in the top-left corner with the engine
	// font for `role`, so a language pack's code page can be checked without a
	// map, a save, or a mouse. Returns 0 when the cvar is empty.
	int	(*CofFontProbe)( char *text, int size, char *role, int roleSize );

	// milestone 4c: the support library's own developer trace, routed into the
	// engine console and the log. vgui_dprintf goes to stderr, which the game
	// log never sees, so the one line cof_hud_text_trace wants needs its own
	// way out.
	void	(*CofPrint)( const char *text );

	// Cry of Fear language packs (cof_language). The captions compiled
	// into client.dll have no file slot at all, so they are swapped at
	// draw time from the active pack's table. TextImage::setText is the
	// chokepoint: it runs before measurement and wrapping, so the layout
	// is computed from the text that is actually drawn. Returns the
	// argument unchanged when no pack is active or nothing matches, so
	// the result is always safe to use. Valid until the next call.
	const char *(*CofLangString)( const char *str );

	// Cry of Fear panel pause (cof_panel_pause): the COF_PANEL_* bits of the
	// client panels that are visible right now, sent once per painted frame.
	// Returns the cof_panel_trace level: at 2 and above the support library
	// also names every panel class the first time it sees one on screen.
	int	(*CofPanelReport)( unsigned int visible );

	// Cry of Fear world behind the panels (cof_panel_transparent). The
	// settings, and 0 while the feature is off; and the base name of the
	// gfx/vgui TGA the client loaded through COM_LoadFile just before the
	// BitmapTGA being built now (one shot, 0 when nothing is latched), which
	// is how the backgroundtoall.tga plate is recognised.
	int	(*CofPanelConfig)( cof_vgui_panel_cfg_t *out );
	int	(*CofImageLatch)( char *buf, int size );

	// Cry of Fear gamepad (cof-gamepad-input), called from the engine side and
	// filled in by the support library: an action on the client's open panel
	// through the panel's own buttons, exactly as a click would do it (the
	// button's ActionSignals). Returns 1 when something was done; `info` gets
	// a one-line description for the log. Left null by older libraries.
	//   COF_VGUI_PANEL_BACK     click the open panel's Back/Close/Cancel/No
	//   COF_VGUI_PANEL_CONFIRM  after a gamepad left click: when it landed on a
	//                           tape recorder slot, click it once more (the
	//                           page acts on a double click)
	//   COF_VGUI_PANEL_DUMP     developer: list the open panels' buttons
	//   COF_VGUI_PANEL_LOCATE   developer: `info` holds "<handler class> <command>"
	//                           on the way in and the centre "x y" of that
	//                           visible button (VGUI coordinates) on the way out
	int	(*CofPanelAction)( int action, char *info, int infoSize );
} vguiapi_t;

enum
{
	COF_VGUI_PANEL_BACK	= 1,
	COF_VGUI_PANEL_CONFIRM	= 2,
	COF_VGUI_PANEL_DUMP	= 3,
	COF_VGUI_PANEL_LOCATE	= 4,
};
#endif // VGUI_API_H
