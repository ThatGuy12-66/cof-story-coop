/*
console.c - developer console
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "keydefs.h"
#include "protocol.h"		// get the protocol version
#include "con_nprint.h"
#include "qfont.h"
#include "wadfile.h"
#include "input.h"
#include "utflib.h"

// The "cofenhanced" build stamp. cof-fix\scripts\write-cof-version.ps1 writes
// engine/cof_version.h from cof-fix\VERSION and the repository's git short
// hash; a plain apply of the patch has no such file, so every value falls back
// to "dev" and the build still compiles.
#if defined( __has_include )
#if __has_include( "cof_version.h" )
#include "cof_version.h"
#endif
#endif
#ifndef COFE_VERSION
#define COFE_VERSION   "dev"
#endif
#ifndef COFE_MILESTONE
#define COFE_MILESTONE "dev"
#endif
#ifndef COFE_COMMIT
#define COFE_COMMIT    "dev"
#endif
#define COFE_BUILD_STRING "cofenhanced " COFE_VERSION " (" COFE_MILESTONE ", " COFE_COMMIT ")"

static CVAR_DEFINE_AUTO( scr_conspeed, "600", FCVAR_ARCHIVE, "console moving speed" );
static CVAR_DEFINE_AUTO( con_notifytime, "3", FCVAR_ARCHIVE, "notify time to live" );
// Cry of Fear: Enhanced, Options > Game > "Show console notifications": 0
// hides the notify lines (the newest console lines drawn over the top-left
// corner during play, Con_DrawNotify). The console itself, the chat input
// line and the developer overlays are unaffected. Saved in config.cfg.
static CVAR_DEFINE_AUTO( cof_notify, "1", FCVAR_ARCHIVE, "draw the console's notify lines in the top-left corner during play (0 = hide them; the console itself is unaffected)" );
CVAR_DEFINE_AUTO( con_fontsize, "1", FCVAR_ARCHIVE, "console font number (0, 1 or 2)" );
static CVAR_DEFINE_AUTO( con_fontrender, "2", FCVAR_ARCHIVE, "console font render mode (0: additive, 1: holes, 2: trans)" );
static CVAR_DEFINE_AUTO( con_charset, "cp1251", FCVAR_ARCHIVE, "console font charset (only cp1251 supported now)" );
static CVAR_DEFINE_AUTO( con_fontscale, "1.0", FCVAR_ARCHIVE, "scale font texture" );
static CVAR_DEFINE_AUTO( con_fontnum, "-1", FCVAR_ARCHIVE, "console font number (0, 1 or 2), -1 for autoselect" );
static CVAR_DEFINE_AUTO( con_color, "240 180 24", FCVAR_ARCHIVE, "set a custom console color" );
static CVAR_DEFINE_AUTO( scr_drawversion, "1", FCVAR_ARCHIVE, "draw version in menu or screenshots, doesn't affect console" );
static CVAR_DEFINE_AUTO( con_oldfont, "0", 0, "use legacy font from gfx.wad, might be missing or broken" );
static CVAR_DEFINE_AUTO( con_showcompletion, "1", FCVAR_ARCHIVE, "perform simplified autocompletion while typing" );

// Unified UI: "Enable console" as a setting instead of a launch switch.
//
// Measured: stock FWGS has no cvar for this at all. host.allow_console is a
// bitfield set once in Host_InitCommon from "-console", "-dev", a dedicated
// server or a "quake*" exe name (engine/common/host.c), and the only runtime
// route is the restricted, one-way command "ui_allowconsole"
// (engine/client/dll_int/cl_gameui.c), which MainUI's multiplayer-only Game
// Options page exposes as a push button. Nothing persists the choice and
// nothing can undo it, so a checkbox had nothing to bind to.
//
// con_enable is that missing setting: archived, so config.cfg carries it, and
// applied by Con_ApplyEnable() below on the first frame after config.cfg has
// run and again whenever it changes. Turning it off never takes away a console
// the command line asked for - that is what con_enable_boot remembers.
static CVAR_DEFINE_AUTO( con_enable, "0", FCVAR_ARCHIVE, "enable the developer console without the -console or -dev command line switch" );
static qboolean con_enable_boot; // the console was already unlocked at startup

// Cry of Fear unified UI, milestone 3b: the console as a Source-style window.
// Deliberately not FCVAR_ARCHIVE, like the rest of the cof_* stack, so a stale
// config.cfg cannot pin the look. cof_console_style 0 restores the stock
// full-width drawing byte for byte.
static CVAR_DEFINE_AUTO( cof_console_style, "1", 0, "draw the console as a centred Source-style window (0 = stock full-width drawing)" );
static CVAR_DEFINE_AUTO( cof_console_width, "0.70", 0, "styled console: panel width as a fraction of the screen" );
static CVAR_DEFINE_AUTO( cof_console_height, "0.60", 0, "styled console: panel height as a fraction of the screen" );
static CVAR_DEFINE_AUTO( cof_console_title, "CONSOLE", 0, "styled console: title band text" );
static CVAR_DEFINE_AUTO( cof_console_panel_color, "32 32 32 230", 0, "styled console: panel background colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_border_color, "150 150 150 200", 0, "styled console: panel border colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_band_color, "20 20 20 240", 0, "styled console: title band colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_title_color, "220 220 220 255", 0, "styled console: title and version text colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_text_color, "190 190 190 255", 0, "styled console: log and input text colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_input_bg_color, "12 12 12 200", 0, "styled console: input box background colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_input_border_color, "90 90 90 255", 0, "styled console: input box border colour, \"r g b a\"" );
static CVAR_DEFINE_AUTO( cof_console_accent_color, "240 180 24 255", 0, "styled console: caret and hovered close-button colour, \"r g b a\"" );
// Cry of Fear ships an amber CONCHARS atlas in gfx.wad, so every glyph is
// pre-tinted and no colour a draw call passes can make it neutral. Loading the
// console font as TF_LUMINANCE makes the glyphs white again, which is what lets
// cof_console_text_color land on the spec grey. Only honoured while
// cof_console_style is on, so "cof_console_style 0" is still the stock console
// down to the font texture.
static CVAR_DEFINE_AUTO( cof_console_font_grayscale, "1", 0, "styled console: load the console font as greyscale, so a pre-tinted game font stops forcing its own colour" );

// Cry of Fear unified UI, milestone 4: engine text that scales with the display.
//
// The console font is a bitmap atlas, so a glyph is a fixed number of *pixels*
// tall whatever the framebuffer is. The game's own gfx.wad CONCHARS atlas is
// 12 px tall, which is already small at 1920x1080 and half of that again at
// 3840x2160, where the console, the notify lines and every engine-drawn corner
// overlay become unreadable.
//
// The size is therefore not a factor and not a table of resolutions: it is one
// target glyph height, cof_text_height_pct percent of the render height, from
// which everything else follows. Everything that draws through a console font
// grows together, because the target is baked into the font's charHeight,
// charWidths and scale: the styled console and the stock console, the input
// line, the notify lines, Con_DrawDebug, cl_showfps, r_speeds, net_speeds and
// the net graph labels, and the version string.
//
// con_fontscale still multiplies it rather than being replaced by it, so a
// user who has set con_fontscale 1.5 keeps that 1.5 on top of the target.
//
// Archived, unlike the cof_console_* look cvars, because these are display
// preferences that have to survive a restart.
static CVAR_DEFINE_AUTO( cof_text_autoscale, "1", FCVAR_ARCHIVE, "size console and engine overlay text from the render height (0 = off, fixed bitmap glyph size)" );
static CVAR_DEFINE_AUTO( cof_text_height_pct, "1.7", FCVAR_ARCHIVE, "target console and overlay glyph height, as a percentage of the render height" );
// The stock atlas is a 12 px aliased bitmap; stretched to 37 px at 2160p it is
// mush. scripts/make-cof-console-font.py rasterises Inter into three .fnt
// atlases at 16, 24 and 34 px, shipped in the game's fonts/ folder, and
// Con_LoadConchars() picks whichever needs the least resampling to reach the
// target. 0 goes back to the game's own CONCHARS, which is the control.
static CVAR_DEFINE_AUTO( cof_text_font, "1", FCVAR_ARCHIVE, "load the generated Inter console atlases from fonts/cof_console*.fnt (0 = the game's own CONCHARS atlas)" );

// the target glyph height baked into the console fonts as they are loaded right
// now, in pixels; 0 means "autoscale off or nothing loaded yet", which never
// compares equal to a live target, so the first Con_CheckTextScale() loads
static float con_text_target_applied = 0.0f;

static int g_codepage = 0;

static qboolean g_messagemode_privileged = true;

#define CON_TIMES		4	// notify lines
#define CON_MAX_TIMES	64	// notify max lines
#define COLOR_DEFAULT	'7'
#define CON_HISTORY		64
#define MAX_DBG_NOTIFY	128
#if XASH_LOW_MEMORY
#define CON_NUMFONTS	1		// do not load different font textures
#define CON_TEXTSIZE	32768	// max scrollback buffer characters in console (32 kb)
#define CON_MAXLINES	2048	// max scrollback buffer lines in console
#else
#define CON_NUMFONTS	3	// maxfonts
#define CON_TEXTSIZE	1048576	// max scrollback buffer characters in console (1 Mb)
#define CON_MAXLINES	16384	// max scrollback buffer lines in console
#endif
// milestone 4: the unscaled glyph height of each loaded atlas, so the target
// height can be re-derived without reading the .fnt files again
static int con_font_base_height[CON_NUMFONTS];

#define CON_LINES( i )	(con.lines[(con.lines_first + (i)) % con.maxlines])
#define CON_LINES_COUNT	con.lines_count
#define CON_LINES_LAST()	CON_LINES( CON_LINES_COUNT - 1 )

// console color typeing
rgba_t g_color_table[8] =
{
{   0,   0,   0, 255 },	// black
{ 255,   0,   0, 255 },	// red
{   0, 255,   0, 255 },	// green
{ 255, 255,   0, 255 },	// yellow
{   0,   0, 255, 255 },	// blue
{   0, 255, 255, 255 },	// cyan
{ 255,   0, 255, 255 },	// magenta
{ 240, 180,  24, 255 },	// default color (can be changed by user)
};

typedef struct
{
	string		szNotify;
	float		expire;
	rgba_t		color;
	int		key_dest;
} notify_t;

typedef struct con_lineinfo_s
{
	char		*start;
	size_t		length;
	double		addtime;		// notify stuff
} con_lineinfo_t;

typedef struct history_line_s
{
	string buffer;
	int    cursor;
	int    scroll;
} history_line_t;

typedef struct con_history_s
{
	history_line_t lines[CON_HISTORY];
	history_line_t backup;
	int     line; // the line being displayed from history buffer will be <= nextHistoryLine
	int     next; // the last line in the history buffer, not masked
} con_history_t;

typedef struct
{
	qboolean		initialized;

	// conbuffer
	char		*buffer;		// common buffer for all console lines
	int		bufsize;		// CON_TEXSIZE
	con_lineinfo_t	*lines;		// console lines
	int		maxlines;		// CON_MAXLINES

	int		lines_first;	// cyclic buffer
	int		lines_count;
	int		num_times;	// overlay lines count

	// console scroll
	int		backscroll;	// lines up from bottom to display
	int 		linewidth;	// characters across screen

	// console animation
	float		showlines;	// how many lines we should display
	float		vislines;		// in scanlines

	// console images
	int		background;	// console background

	// console fonts
	cl_font_t		chars[CON_NUMFONTS];// fonts.wad/font1.fnt
	cl_font_t		*curFont;

	// console input
	field_t		input;
	field_t		input_completion;

	// chatfiled
	field_t		chat;
	string		chat_cmd;		// can be overrieded by user

	// console history
	con_history_t	history;
	qboolean		historyLoaded;

	notify_t		notify[MAX_DBG_NOTIFY]; // for Con_NXPrintf
	qboolean		draw_notify;	// true if we have NXPrint message
} console_t;

static console_t		con;

static void Con_ClearField( field_t *edit );
static void Field_CharEvent( field_t *edit, int ch );
static void Con_InvalidateFonts( void );

static void Con_LoadHistory( con_history_t *self );
static void Con_SaveHistory( con_history_t *self );

// milestone 3b, styled console window
static qboolean Con_StyleEnabled( void );
static qboolean Con_StyleCloseHit( int mx, int my );

// milestone 4, text autoscale: the wrap width has to know the styled panel, but
// constyle_t is only declared with the rest of the styled console below
static int Con_StyleLogWidth( void );

// last known mouse position, fed by IN_MouseMove()
static int con_style_mouse_x = -1;
static int con_style_mouse_y = -1;

/*
================
Con_BackgroundMapActive
================
*/
static qboolean Con_BackgroundMapActive( void )
{
	return sv_background.value != 0.0f || cl.background;
}

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void )
{
	con.lines_count = 0;
	con.backscroll = 0; // go to end
}

/*
================
Con_SetColor
================
*/
static void Con_SetColor( void )
{
	if( !FBitSet( con_color.flags, FCVAR_CHANGED ))
		return;

	int r, g, b;
	int num = sscanf( con_color.string, "%i %i %i", &r, &g, &b );

	switch( num )
	{
	case 1:
		Con_DefaultColor( r, r, r, false );
		break;
	case 3:
		Con_DefaultColor( r, g, b, false );
		break;
	default:
		Cvar_DirectSet( &con_color, con_color.def_string );
		break;
	}

	ClearBits( con_color.flags, FCVAR_CHANGED );
}

/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void )
{
	for( int i = 0; i < CON_LINES_COUNT; i++ )
		CON_LINES( i ).addtime = 0.0;
}

/*
================
Con_ClearTyping
================
*/
static void Con_ClearTyping( void )
{
	Con_ClearField( &con.input );
	con.input.widthInChars = con.linewidth;

	Cmd_AutoCompleteClear();
}

/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f( void )
{
	g_messagemode_privileged = Cmd_CurrentCommandIsPrivileged();

	if( Cmd_Argc() == 2 )
		Q_strncpy( con.chat_cmd, Cmd_Argv( 1 ), sizeof( con.chat_cmd ));
	else Q_strncpy( con.chat_cmd, "say", sizeof( con.chat_cmd ));

	Key_SetKeyDest( key_message );
}

/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f( void )
{
	g_messagemode_privileged = Cmd_CurrentCommandIsPrivileged();

	Q_strncpy( con.chat_cmd, "say_team", sizeof( con.chat_cmd ));
	Key_SetKeyDest( key_message );
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void )
{
	if( !host.allow_console || UI_CreditsActive( ))
		return; // disabled

	SCR_EndLoadingPlaque();

	// show console only in game, from menu, or to close console
	if( cls.state != ca_active && cls.key_dest != key_menu && cls.key_dest != key_console )
		return;

	Con_ClearTyping();
	Con_ClearNotify();

	if( cls.key_dest == key_console )
	{
		// closing console
		// Cry of Fear unified UI (cof_ui_death_menu): the death page is the
		// only interactive surface left in its session, so treat it like the
		// out-of-game case and go back to the menu, not to the game
		if( cls.state != ca_active || Con_BackgroundMapActive( ) || CL_CoF_DeathMenuActive( ))
		{
			// not in game or in background mode - return to menu
			// UI_SetActiveMenu(true) reactivates menu without resetting history
			UI_SetActiveMenu( true );
		}
		else
		{
			// in game - return to game
			UI_SetActiveMenu( false );
		}
	}
	else
	{
		// opening console - just switch key_dest, don't call UI_SetActiveMenu
		// which would reset menu state via on_menu_hide()
		Key_SetKeyDest( key_console );
	}
}

/*
================
Con_SetTimes_f
================
*/
static void Con_SetTimes_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "contimes <n lines>\n" );
		return;
	}

	int newtimes = Q_atoi( Cmd_Argv( 1 ) );
	con.num_times = bound( CON_TIMES, newtimes, CON_MAX_TIMES );
}

/*
================
Con_FixTimes

Notifies the console code about the current time
(and shifts back times of other entries when the time
went backwards)
================
*/
static void Con_FixTimes( void )
{
	if( con.lines_count <= 0 ) return;

	double diff = cl.time - CON_LINES_LAST().addtime;
	if( diff >= 0.0 ) return; // nothing to fix

	for( int i = 0; i < con.lines_count; i++ )
		CON_LINES( i ).addtime += diff;
}

/*
================
Con_DeleteLine

Deletes the first line from the console history.
================
*/
static void Con_DeleteLine( void )
{
	if( con.lines_count == 0 )
		return;
	con.lines_count--;
	con.lines_first = (con.lines_first + 1) % con.maxlines;
}

/*
================
Con_DeleteLastLine

Deletes the last line from the console history.
================
*/
static void Con_DeleteLastLine( void )
{
	if( con.lines_count == 0 )
		return;
	con.lines_count--;
}

/*
================
Con_BytesLeft

Checks if there is space for a line of the given length, and if yes, returns a
pointer to the start of such a space, and NULL otherwise.
================
*/
static char *Con_BytesLeft( int length )
{
	if( length > con.bufsize )
		return NULL;

	if( con.lines_count == 0 )
	{
		return con.buffer;
	}
	else
	{
		char	*firstline_start = con.lines[con.lines_first].start;
		char	*lastline_onepastend = CON_LINES_LAST().start + CON_LINES_LAST().length;

		// the buffer is cyclic, so we first have two cases...
		if( firstline_start < lastline_onepastend ) // buffer is contiguous
		{
			// put at end?
			if( length <= con.buffer + con.bufsize - lastline_onepastend )
				return lastline_onepastend;
			// put at beginning?
			else if( length <= firstline_start - con.buffer )
				return con.buffer;

			return NULL;
		}
		else
		{
			// buffer has a contiguous hole
			if( length <= firstline_start - lastline_onepastend )
				return lastline_onepastend;

			return NULL;
		}
	}
}

/*
================
Con_AddLine

Appends a given string as a new line to the console.
================
*/
static void Con_AddLine( const char *line, int length, qboolean newline )
{
	char *putpos;

	if( !con.initialized || !con.buffer )
		return;

	Con_FixTimes();
	length++;	// reserve space for term

	ASSERT( length < CON_TEXTSIZE );

	while( !( putpos = Con_BytesLeft( length )) || con.lines_count >= con.maxlines )
		Con_DeleteLine();

	con_lineinfo_t *p;
	if( newline )
	{
		memcpy( putpos, line, length );
		putpos[length - 1] = '\0';
		con.lines_count++;

		p = &CON_LINES_LAST();
		p->start = putpos;
		p->length = length;
		p->addtime = cl.time;
	}
	else
	{
		p = &CON_LINES_LAST();
		putpos = p->start + Q_strlen( p->start );
		memcpy( putpos, line, length - 1 );
		p->length = Q_strlen( p->start );
		putpos[p->length] = '\0';
		p->addtime = cl.time;
		p->length++;
	}
}

/*
================
Con_TextTargetHeight

The one number the whole of milestone 4 derives from: how tall a console glyph
should be, in pixels, on this render target. It is a plain fraction of the
render height - no integer steps, no per-resolution cases, nothing that has to
be extended when somebody plugs in a different monitor.

  cof_text_height_pct 1.7 gives 12 px at 720p, 18 at 1080p, 24 at 1440p,
  37 at 2160p, 73 at 4320p.

con_fontscale multiplies it, so a user who has already set con_fontscale keeps
that preference on top. Returns 0 when the feature is off, which every caller
reads as "size the fonts exactly the way the stock engine did".
================
*/
static float Con_TextTargetHeight( void )
{
	float	px;

	if( cof_text_autoscale.value == 0.0f )
		return 0.0f;

	if( !ref.initialized || refState.height <= 0 )
		return 0.0f;

	px = refState.height * bound( 0.4f, cof_text_height_pct.value, 12.0f ) * 0.01f;

	if( con_fontscale.value > 0.0f )
		px *= con_fontscale.value;

	return bound( 6.0f, px, 192.0f );
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
static void Con_CheckResize( void )
{
	int	charWidth = 8;
	int	logWidth;

	if( con.curFont && con.curFont->hFontTexture )
		charWidth = con.curFont->charWidths['O'] - 1;

	if( charWidth < 1 )
		charWidth = 1; // an autoscaled atlas never gets here, but do not divide by zero

	int width = ( refState.width / charWidth ) - 2;

	// milestone 4: Con_Print() wraps a line at con.linewidth when the line is
	// added, long before the styled panel exists, so the panel can only ever
	// show a long line whole if the wrap width is the panel's log area instead
	// of the whole screen. charWidth already carries the autoscale factor,
	// because the factor is baked into charWidths at load time.
	logWidth = Con_StyleLogWidth();

	if( logWidth > 0 )
	{
		int	w = ( logWidth / charWidth ) - 1;

		if( w >= 20 ) // never wrap to something unusably narrow
			width = w;
	}

	if( !ref.initialized ) width = (640 / 5);

	if( width == con.linewidth )
		return;

	Con_ClearNotify();
	con.linewidth = width;
	con.backscroll = 0;

	con.input.widthInChars = con.linewidth;
}

/*
================
Con_PageUp
================
*/
void Con_PageUp( int lines )
{
	con.backscroll += abs( lines );
}

/*
================
Con_PageDown
================
*/
void Con_PageDown( int lines )
{
	con.backscroll -= abs( lines );
}

/*
================
Con_Top
================
*/
static void Con_Top( void )
{
	con.backscroll = CON_MAXLINES;
}

/*
================
Con_Bottom
================
*/
void Con_Bottom( void )
{
	con.backscroll = 0;
}

/*
================
Con_Visible
================
*/
int GAME_EXPORT Con_Visible( void )
{
	return (con.vislines > 0);
}

/*
================
Con_FixedFont
================
*/
qboolean Con_FixedFont( void )
{
	return CL_FixedFont( con.curFont );
}


/*
================
Con_LoadConsoleFont

INTERNAL RESOURCE
================
*/
static void Con_LoadConsoleFont( int fontNumber, cl_font_t *font )
{
	qboolean success = false;
	// milestone 4: when a target height is in force the atlas is loaded at its
	// native size and Con_ScaleConsoleFont() resizes it afterwards, because the
	// base glyph height is only known once the .fnt header has been read.
	qboolean autoscale = con_text_target_applied > 0.0f;
	float scale = autoscale ? 1.0f : con_fontscale.value;
	uint texFlags = TF_FONT|TF_NEAREST;

	if( font->valid )
		return; // already loaded

	// milestone 4: a target height is almost never an integer multiple of the
	// atlas, and a nearest-filtered stretch turns a 1.15x glyph stem into an
	// alternating one and two pixels. Filter the atlas instead; CL_DrawCharacter
	// then insets half a texel per glyph rect when it magnifies (cl_font.c),
	// which is exactly the case that inset exists for, and the generated
	// atlases keep a two-pixel gutter between cells for the minifying case.
	if( autoscale )
		ClearBits( texFlags, TF_NEAREST );

	// milestone 3b: neutralise a pre-tinted game font so the styled console's
	// text colour means something (Cry of Fear's CONCHARS atlas is amber)
	if( Con_StyleEnabled() && cof_console_font_grayscale.value != 0.0f )
		SetBits( texFlags, TF_LUMINANCE );

	// milestone 4: the generated Inter atlases, before anything else. They are
	// the only console font in this stack that was drawn to be resized: three
	// base sizes, antialiased, with an alpha ramp instead of a colour key.
	// cof_text_font 0, or simply not shipping them, falls through to the exact
	// stock chain below, which is what keeps the game's own atlas as a control.
	if( !con_oldfont.value && cof_text_font.value != 0.0f )
	{
		string path;

		Q_snprintf( path, sizeof( path ), "fonts/cof_console%i.fnt", fontNumber );
		success = Con_LoadVariableWidthFont( path, font, scale, &con_fontrender, texFlags );
	}

	if( success )
	{
		// nothing else to try
	}
	else if( con_oldfont.value )
	{
		success = Con_LoadVariableWidthFont( "gfx/conchars.fnt", font, scale, &con_fontrender, texFlags );
	}
	else
	{
		string path;
		dword crc = 0;

		// replace default fonts.wad textures by current charset's font
		if( !CRC32_File( &crc, "fonts.wad" ) || crc == 0x3c0a0029 )
		{
			if( Q_snprintf( path, sizeof( path ),
				"font%i_%s.fnt", fontNumber, Cvar_VariableString( "con_charset" )) > 0 )
			{
				success = Con_LoadVariableWidthFont( path, font, scale, &con_fontrender, texFlags );
			}
		}

		if( !success )
		{
			Q_snprintf( path, sizeof( path ), "fonts/font%i", fontNumber );
			success = Con_LoadVariableWidthFont( path, font, scale, &con_fontrender, texFlags );
		}

		if( !success )
		{
			// Some mods provide a variable-width console font in gfx.wad.
			success = Con_LoadVariableWidthFont( "gfx/conchars.fnt", font, scale, &con_fontrender, texFlags );
		}
	}

	if( !success )
	{
		// quake fixed font as fallback
		// keep source to print directly into conback image
		if( !Con_LoadFixedWidthFont( "gfx/conchars", font, scale, &con_fontrender, texFlags|TF_KEEP_SOURCE ))
			Con_DPrintf( S_ERROR "failed to load console font\n" );
	}
}

/*
================
Con_LoadConchars
================
*/
static void Con_ScaleConsoleFont( cl_font_t *font, int base, float target )
{
	float	scale;

	if( !font->valid || base <= 0 || target <= 0.0f )
		return;

	scale = target / (float)base;

	// the atlas was loaded at scale 1, so charWidths are still the raw advances
	// and fontRc is still in texels; setting font->scale and rescaling the
	// metrics is exactly what loading at that scale would have produced
	font->scale = scale;
	font->charHeight = Q_max( 1, Q_rint( base * scale ));

	for( int i = 0; i < ARRAYSIZE( font->charWidths ); i++ )
	{
		if( font->charWidths[i] > 0 )
			font->charWidths[i] = Q_max( 1, Q_rint( font->charWidths[i] * scale ));
	}
}

static void Con_LoadConchars( void )
{
	qboolean fresh[CON_NUMFONTS];
	float	target;
	int	fontSize;

	// milestone 4: freeze the target height for this whole load, so every font
	// in the set carries the same one and Con_CheckTextScale() can tell when
	// the screen has moved away from it
	con_text_target_applied = Con_TextTargetHeight();
	target = con_text_target_applied;

	// load all the console fonts
	for( int i = 0; i < CON_NUMFONTS; i++ )
	{
		fresh[i] = !con.chars[i].valid;
		Con_LoadConsoleFont( i, con.chars + i );

		if( fresh[i] && con.chars[i].valid )
			con_font_base_height[i] = con.chars[i].charHeight;
	}

	// select properly fontsize
	if( con_fontnum.value >= 0 && con_fontnum.value <= CON_NUMFONTS - 1 )
	{
		fontSize = con_fontnum.value;
	}
	else if( target > 0.0f )
	{
		// milestone 4: not "how wide is the screen" but "which of the loaded
		// atlases is closest to the height we actually want", measured as the
		// resampling ratio, so magnifying 1.2x loses to minifying 1.1x
		float	best = 0.0f;

		fontSize = -1;

		for( int i = 0; i < CON_NUMFONTS; i++ )
		{
			float	ratio;

			if( !con.chars[i].valid || con_font_base_height[i] <= 0 )
				continue;

			ratio = target / (float)con_font_base_height[i];
			if( ratio < 1.0f )
				ratio = 1.0f / ratio;

			if( fontSize < 0 || ratio < best )
			{
				best = ratio;
				fontSize = i;
			}
		}

		if( fontSize < 0 )
			fontSize = 0;
	}
	else if( refState.width <= 640 )
		fontSize = 0;
	else if( refState.width >= 1280 )
		fontSize = 2;
	else fontSize = 1;

	if( fontSize > CON_NUMFONTS - 1 )
		fontSize = CON_NUMFONTS - 1;

	// milestone 4: bring every atlas to the same on-screen height, so whichever
	// one a consumer already picked for itself - con_fontsize for the client's
	// pfnDrawConsoleString, font 0 for the net graph - agrees with the console
	if( target > 0.0f )
	{
		for( int i = 0; i < CON_NUMFONTS; i++ )
		{
			if( fresh[i] )
				Con_ScaleConsoleFont( &con.chars[i], con_font_base_height[i], target );
		}
	}

	// sets the current font
	con.curFont = &con.chars[fontSize];
}

/*
================
Con_CheckTextScale

Re-derives the target glyph height and reloads the console fonts when it has
moved.

Two triggers: a video mode change, which reaches this through
SCR_VidInit() -> Con_VidInit(), and cof_text_autoscale / cof_text_height_pct /
cof_text_font being changed at runtime, which reach it through
Con_RunConsole()/Con_DrawConsole() on the next frame. Everything else is a
float comparison per frame.

The target is baked into the atlas' charWidths/charHeight/scale at load time,
so no drawing code has to know it exists; only the wrap width has to be redone,
because Con_Print() wraps at con.linewidth in glyph cells.
================
*/
static void Con_CheckTextScale( void )
{
	qboolean force = false;
	float	want;

	if( !ref.initialized )
		return;

	// which atlas is loaded is not a function of the target height
	if( FBitSet( cof_text_font.flags, FCVAR_CHANGED ))
	{
		ClearBits( cof_text_font.flags, FCVAR_CHANGED );
		force = true;
	}

	want = Con_TextTargetHeight();

	if( !force && want == con_text_target_applied )
		return;

	Con_InvalidateFonts();
	Con_LoadConchars();	// sets con_text_target_applied = want
	con.linewidth = 0;	// make Con_CheckResize() redo the wrap width

	Con_Reportf( "[cof-text] %ix%i: target %.1f px (cof_text_height_pct %.2f, con_fontscale %.2f) -> font %i base %i px, glyph scale %.3f, charH %i px\n",
		refState.width, refState.height, want,
		cof_text_height_pct.value, con_fontscale.value,
		(int)( con.curFont - con.chars ),
		con.curFont ? con_font_base_height[con.curFont - con.chars] : 0,
		con.curFont ? con.curFont->scale : 0.0f,
		con.curFont ? con.curFont->charHeight : 0 );
}

/*
============================
Con_UtfProcessChar

Convert utf char to current font's single-byte encoding
============================
*/
int Con_UtfProcessCharForce( int in )
{
	// TODO: get rid of global state where possible
	static utfstate_t state = { 0 };

	uint32_t ch = Q_DecodeUTF8( &state, in );

	if( g_codepage == 1251 )
		return Q_UnicodeToCP1251( ch );
	if( g_codepage == 1252 )
		return Q_UnicodeToCP1252( ch );

	return '?'; // not implemented yet
}

int GAME_EXPORT Con_UtfProcessChar( int in )
{
	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return in;

	// otherwise, decode it and convert to selected codepage
	return Con_UtfProcessCharForce( in );
}

/*
=================
Con_UtfMoveLeft

get position of previous printful char
=================
*/
int Con_UtfMoveLeft( char *str, int pos )
{
	utfstate_t state = { 0 };
	int k = 0;

	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return pos - 1;

	if( pos == 1 )
		return 0;

	for( int i = 0; i < pos - 1; i++ )
	{
		if( Q_DecodeUTF8( &state, (byte)str[i] ))
			k = i + 1;
	}

	return k;
}

/*
=================
Con_UtfMoveRight

get next of previous printful char
=================
*/
int Con_UtfMoveRight( char *str, int pos, int length )
{
	utfstate_t state = { 0 };

	if( !cls.accept_utf8 ) // incoming character is not a UTF-8 sequence
		return pos + 1;

	for( int i = pos; i <= length; i++ )
	{
		if( Q_DecodeUTF8( &state, (byte)str[i] ))
			return i + 1;
	}

	return pos + 1;
}

static void Con_DrawCharToConback( int num, const byte *conchars, byte *dest )
{
	int row = num >> 4;
	int col = num & 15;
	const byte *source = conchars + (row << 10) + (col << 3);

	int drawline = 8;

	while( drawline-- )
	{
		for( int x = 0; x < 8; x++ )
			if( source[x] != 255 )
				dest[x] = 0x60 + source[x];
		source += 128;
		dest += 320;
	}

}

/*
====================
Con_GetFont

====================
*/
cl_font_t *Con_GetFont( int num )
{
	num = bound( 0, num, CON_NUMFONTS - 1 );
	return &con.chars[num];
}

/*
====================
Con_GetCurFont

====================
*/
cl_font_t *Con_GetCurFont( void )
{
	return con.curFont;
}

/*
====================
Con_DrawStringLen

compute string width and height in screen pixels
====================
*/
void GAME_EXPORT Con_DrawStringLen( const char *pText, int *length, int *height )
{
	CL_DrawStringLen( con.curFont, pText, length, height, FONT_DRAW_UTF8 );
}

/*
====================
Con_DrawString

client version of routine
====================
*/
int Con_DrawString( int x, int y, const char *string, const rgba_t setColor )
{
	return CL_DrawString( x, y, string, setColor, con.curFont, FONT_DRAW_UTF8 );
}

/*
================
Con_Init
================
*/
void Con_Init( void )
{
	if( host.type == HOST_DEDICATED )
		return; // dedicated server already have console

	// must be init before startup video subsystem
	Cvar_RegisterVariable( &scr_conspeed );
	Cvar_RegisterVariable( &con_notifytime );
	Cvar_RegisterVariable( &cof_notify );
	Cvar_RegisterVariable( &con_fontsize );
	Cvar_RegisterVariable( &con_charset );
	Cvar_RegisterVariable( &con_fontscale );
	Cvar_RegisterVariable( &con_fontrender );
	Cvar_RegisterVariable( &con_fontnum );
	Cvar_RegisterVariable( &con_color );
	Cvar_RegisterVariable( &scr_drawversion );
	Cvar_RegisterVariable( &con_oldfont );
	Cvar_RegisterVariable( &con_showcompletion );

	// "Enable console" as a persistent setting. Con_Init runs inside
	// Host_InitCommon, long before Host_Init execs config.cfg, so a value
	// stored there really reaches this cvar; Con_ApplyEnable() picks it up on
	// the first frame afterwards.
	Cvar_RegisterVariable( &con_enable );
	con_enable_boot = host.allow_console_init;

	// milestone 3b: the Source-style console window
	Cvar_RegisterVariable( &cof_console_style );
	Cvar_RegisterVariable( &cof_console_width );
	Cvar_RegisterVariable( &cof_console_height );
	Cvar_RegisterVariable( &cof_console_title );
	Cvar_RegisterVariable( &cof_console_panel_color );
	Cvar_RegisterVariable( &cof_console_border_color );
	Cvar_RegisterVariable( &cof_console_band_color );
	Cvar_RegisterVariable( &cof_console_title_color );
	Cvar_RegisterVariable( &cof_console_text_color );
	Cvar_RegisterVariable( &cof_console_input_bg_color );
	Cvar_RegisterVariable( &cof_console_input_border_color );
	Cvar_RegisterVariable( &cof_console_accent_color );
	Cvar_RegisterVariable( &cof_console_font_grayscale );

	// milestone 4: console and overlay text sized from the render height
	Cvar_RegisterVariable( &cof_text_autoscale );
	Cvar_RegisterVariable( &cof_text_height_pct );
	Cvar_RegisterVariable( &cof_text_font );

	// init the console buffer
	con.bufsize = CON_TEXTSIZE;
	con.buffer = (char *)Z_Calloc( con.bufsize );
	con.maxlines = CON_MAXLINES;
	con.lines = (con_lineinfo_t *)Z_Calloc( con.maxlines * sizeof( *con.lines ));
	con.lines_first = con.lines_count = 0;
	con.num_times = CON_TIMES; // default as 4

	Con_CheckResize();

	Con_ClearField( &con.input );
	con.input.widthInChars = con.linewidth;

	Con_ClearField( &con.chat );
	con.chat.widthInChars = con.linewidth;

	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f, "opens or closes the console" );
	Cmd_AddRestrictedCommand( "clear", Con_Clear_f, "clear console history" );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f, "enable message mode \"say\"" );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f, "enable message mode \"say_team\"" );
	Cmd_AddCommand( "contimes", Con_SetTimes_f, "change number of console overlay lines (4-64)" );
	con.initialized = true;

	Con_Printf( "Console initialized.\n" );
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void )
{
	con.initialized = false;

	if( con.buffer )
		Mem_Free( con.buffer );

	if( con.lines )
		Mem_Free( con.lines );

	con.buffer = NULL;
	con.lines = NULL;
	Con_SaveHistory( &con.history );
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed
If no console is visible, the notify window will pop up.
================
*/
void Con_Print( const char *txt )
{
	static qboolean cr_pending = false;
	static qboolean colorstring = false;
	static char buf[MAX_PRINT_MSG];
	static int  lastlength = 0;
	static int  bufpos = 0;
	static int  charpos = 0;

	qboolean norefresh = false;
	int		c, mask = 0;

	// client not running
	if( !con.initialized || !con.buffer )
		return;

	if( txt[0] == 2 )
	{
		// go to colored text
		if( Con_FixedFont( ))
			mask = 128;
		txt++;
	}

	if( txt[0] == 3 )
	{
		norefresh = true;
		txt++;
	}

	for( ; *txt; txt++ )
	{
		if( cr_pending )
		{
			Con_DeleteLastLine();
			cr_pending = false;
		}
		c = *txt;

		switch( c )
		{
		case '\0':
			break;
		case '\r':
			if( txt[1] != '\n' )
			{
				Con_AddLine( buf, bufpos, true );
				lastlength = CON_LINES_LAST().length;
				cr_pending = true;
				bufpos = 0;
				charpos = 0;
			}
			break;
		case '\n':
			Con_AddLine( buf, bufpos, true );
			lastlength = CON_LINES_LAST().length;
			bufpos = 0;
			charpos = 0;
			break;
		default:
			buf[bufpos++] = c | mask;

			if( IsColorString( txt ))
			{
				// first color string character
				colorstring = true;
			}
			else if( colorstring )
			{
				// second color string character
				colorstring = false;
			}
			else
			{
				// not a color string, move char counter
				charpos++;
			}

			if(( bufpos >= sizeof( buf ) - 1 ) || charpos >= ( con.linewidth - 1 ))
			{
				Con_AddLine( buf, bufpos, true );
				lastlength = CON_LINES_LAST().length;
				bufpos = 0;
				charpos = 0;
			}
			break;
		}
	}

	if( norefresh ) return;

	// custom renderer cause problems while updates screen on-loading
	if( SV_Active() && cls.state < ca_active && !cl.video_prepped && !cls.disable_screen )
	{
		if( bufpos != 0 )
		{
			Con_AddLine( buf, bufpos, lastlength != 0 );
			lastlength = 0;
			bufpos = 0;
			charpos = 0;
		}

		// FIXME: disable updating screen, because when texture is bound any console print
		// can re-bound it to console font texture
#if 0
		if( !inupdate )
		{
			inupdate = true;
			SCR_UpdateScreen ();
			inupdate = false;
		}
#endif
	}
}

/*
================
Con_NXPrintfv

Draw a single debug line with specified height, color and time to live
================
*/
static void Con_NXPrintfv( keydest_t key_dest, const con_nprint_t *info, const char *fmt, va_list va )
{
	if( info->index < 0 || info->index >= ARRAYSIZE( con.notify ))
		return;

	Q_vsnprintf( con.notify[info->index].szNotify, sizeof( con.notify[info->index].szNotify ), fmt, va );

	// setup values
	con.notify[info->index].key_dest = key_dest;
	con.notify[info->index].expire = host.realtime + info->time_to_live;
	MakeRGBA( con.notify[info->index].color, (byte)(info->color[0] * 255), (byte)(info->color[1] * 255), (byte)(info->color[2] * 255), 255 );
	con.draw_notify = true;
}

/*
================
Con_NPrint

Draw a single debug line with specified height
================
*/
void GAME_EXPORT Con_NPrintf( int idx, const char *fmt, ... )
{
	va_list	args;
	con_nprint_t info =
	{
		.index = idx,
		.time_to_live = 4.0f,
		.color = { 1.0f, 1.0f, 1.0f },
	};

	va_start( args, fmt );
	Con_NXPrintfv( key_game, &info, fmt, args );
	va_end( args );
}

/*
================
Con_NXPrint

Draw a single debug line with specified height, color and time to live
================
*/
void GAME_EXPORT Con_NXPrintf( con_nprint_t *info, const char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	va_start( args, fmt );
	Con_NXPrintfv( key_game, info, fmt, args );
	va_end( args );
}

/*
================
UI_NPrint

Draw a single debug line with specified height (menu version)
================
*/
void GAME_EXPORT UI_NPrintf( int idx, const char *fmt, ... )
{
	va_list	args;
	con_nprint_t info =
	{
		.index = idx,
		.time_to_live = 4.0f,
		.color = { 1.0f, 1.0f, 1.0f },
	};

	va_start( args, fmt );
	Con_NXPrintfv( key_menu, &info, fmt, args );
	va_end( args );
}

/*
================
UI_NXPrint

Draw a single debug line with specified height, color and time to live (menu version)
================
*/
void GAME_EXPORT UI_NXPrintf( con_nprint_t *info, const char *fmt, ... )
{
	va_list	args;

	if( !info ) return;

	va_start( args, fmt );
	Con_NXPrintfv( key_menu, info, fmt, args );
	va_end( args );
}

/*
=============================================================================

EDIT FIELDS

=============================================================================
*/
/*
================
Con_ClearField
================
*/
static void Con_ClearField( field_t *edit )
{
	memset( edit->buffer, 0, sizeof( edit->buffer ));
	edit->cursor = 0;
	edit->scroll = 0;
}

/*
================
Field_Set
================
*/
MAYBE_UNUSED static void Field_Set( field_t *f, const char *string )
{
	f->scroll = 0;
	f->cursor = Q_strncpy( f->buffer, string, sizeof( f->buffer ));
}

/*
================
Field_Paste
================
*/
static void Field_Paste( field_t *edit )
{
	char *cbd = Sys_GetClipboardData();
	if( !cbd ) return;

	// send as if typed, so insert / overstrike works properly
	int pasteLen = Q_strlen( cbd );
	for( int i = 0; i < pasteLen; i++ )
		Field_CharEvent( edit, cbd[i] );
}

/*
=================
Field_GoTo

=================
*/
static void Field_GoTo( field_t *edit, int pos )
{
	edit->cursor = pos;
	edit->scroll = Q_max( 0, edit->cursor - edit->widthInChars );
}

/*
=================
Field_KeyDownEvent

Performs the basic line editing functions for the console,
in-game talk, and menu fields

Key events are used for non-printable characters, others are gotten from char events.
=================
*/
static void Field_KeyDownEvent( field_t *edit, int key )
{
	// shift-insert is paste
	if((( key == K_INS ) || ( key == K_KP_INS )) && Key_IsDown( K_SHIFT ))
	{
		Field_Paste( edit );
		return;
	}

	int len = Q_strlen( edit->buffer );

	if( key == K_DEL )
	{
		if( edit->cursor < len )
			memmove( edit->buffer + edit->cursor, edit->buffer + edit->cursor + 1, len - edit->cursor );
		return;
	}

	if( key == K_BACKSPACE || key == K_X_BUTTON )
	{
		if( edit->cursor > 0 )
		{
			int newcursor = Con_UtfMoveLeft( edit->buffer, edit->cursor );
			memmove( edit->buffer + newcursor, edit->buffer + edit->cursor, len - edit->cursor + 1 );
			edit->cursor = newcursor;
			if( edit->scroll ) edit->scroll--;
		}
		return;
	}

	if( key == K_RIGHTARROW || key == K_DPAD_RIGHT )
	{
		if( edit->cursor < len ) edit->cursor = Con_UtfMoveRight( edit->buffer, edit->cursor, edit->widthInChars );
		if( edit->cursor >= edit->scroll + edit->widthInChars && edit->cursor <= len )
			edit->scroll++;
		return;
	}

	if( key == K_LEFTARROW || key == K_DPAD_LEFT )
	{
		if( edit->cursor > 0 ) edit->cursor = Con_UtfMoveLeft( edit->buffer, edit->cursor );
		if( edit->cursor < edit->scroll ) edit->scroll--;
		return;
	}

	if( key == K_HOME || ( Q_tolower(key) == 'a' && Key_IsDown( K_CTRL )))
	{
		Field_GoTo( edit, 0 );
		return;
	}

	if( key == K_END || ( Q_tolower(key) == 'e' && Key_IsDown( K_CTRL )))
	{
		Field_GoTo( edit, len );
		return;
	}

	if( key == K_INS )
	{
		host.key_overstrike = !host.key_overstrike;
		return;
	}
}

/*
==================
Field_CharEvent
==================
*/
static void Field_CharEvent( field_t *edit, int ch )
{
	if( ch == 'v' - 'a' + 1 )
	{
		// ctrl-v is paste
		Field_Paste( edit );
		return;
	}

	if( ch == 'c' - 'a' + 1 )
	{
		// ctrl-c clears the field
		Con_ClearField( edit );
		return;
	}

	int len = Q_strlen( edit->buffer );

	if( ch == 'a' - 'a' + 1 )
	{
		// ctrl-a is home
		Field_GoTo( edit, 0 );
		return;
	}

	if( ch == 'e' - 'a' + 1 )
	{
		// ctrl-e is end
		Field_GoTo( edit, len );
		return;
	}

	// ignore any other non printable chars
	if( ch < 32 ) return;

	if( host.key_overstrike )
	{
		if ( edit->cursor == MAX_STRING - 1 ) return;
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}
	else
	{
		// insert mode
		if ( len == MAX_STRING - 1 ) return; // all full
		memmove( edit->buffer + edit->cursor + 1, edit->buffer + edit->cursor, len + 1 - edit->cursor );
		edit->buffer[edit->cursor] = ch;
		edit->cursor++;
	}

	if( edit->cursor >= edit->widthInChars ) edit->scroll++;
	if( edit->cursor == len + 1 ) edit->buffer[edit->cursor] = 0;
}

/*
==================
Field_DrawInputLine
==================
*/
static int Field_DrawInputLine( int x, int y, const field_t *edit, byte alpha, qboolean cursor )
{
	char str[MAX_SYSPATH];
	rgba_t colorDefault;
	const int prestep = bound( 0, edit->scroll, sizeof( edit->buffer ) - 1 );
	const int drawLen = bound( 0, edit->widthInChars, sizeof( str ));
	const int cursorCharPos = bound( 0, edit->cursor - prestep, sizeof( str ));

	memcpy( colorDefault, g_color_table[ColorIndex( COLOR_DEFAULT )], 3 * sizeof( colorDefault[0] ));
	colorDefault[3] = alpha;

	Q_strncpy( str, edit->buffer + prestep, drawLen );

	// draw it
	CL_DrawString( x, y, str, colorDefault, con.curFont, FONT_DRAW_UTF8 );

	// calc cursor position
	str[cursorCharPos] = 0;
	int curPos;
	CL_DrawStringLen( con.curFont, str, &curPos, NULL, FONT_DRAW_UTF8 );

	// draw the cursor
	if( !cursor || (int)( host.realtime * 4 ) & 1 )
		return curPos; // off blink

	CL_DrawCharacter( x + curPos, y, host.key_overstrike ? '|' : '_', colorDefault, con.curFont, 0 );

	return curPos;
}

/*
=============================================================================

CONSOLE HISTORY HANDLING

=============================================================================
*/
/*
===================
Con_HistoryFromField

===================
*/
static void Con_HistoryFromField( history_line_t *dst, const field_t *src )
{
	Q_strncpy( dst->buffer, src->buffer, sizeof( dst->buffer ));
	dst->cursor = src->cursor;
	dst->scroll = src->scroll;
}

/*
===================
Con_HistoryToField

===================
*/
static void Con_HistoryToField( field_t *dst, const history_line_t *src )
{
	Q_strncpy( dst->buffer, src->buffer, sizeof( dst->buffer ));
	dst->cursor = src->cursor;
	dst->scroll = src->scroll;
}

/*
===================
Con_HistoryUp

===================
*/
static void Con_HistoryUp( con_history_t *self, field_t *in )
{
	if( self->line == self->next )
		Con_HistoryFromField( &self->backup, in );
	else
		Con_HistoryFromField( &self->lines[self->line % CON_HISTORY], in );

	if(( self->next - self->line ) < CON_HISTORY )
		self->line = Q_max( 0, self->line - 1 );

	Con_HistoryToField( in, &self->lines[self->line % CON_HISTORY] );
}

/*
===================
Con_HistoryDown

===================
*/
static void Con_HistoryDown( con_history_t *self, field_t *in )
{
	Con_HistoryFromField( &self->lines[self->line % CON_HISTORY], in );

	self->line = Q_min( self->next, self->line + 1 );
	if( self->line == self->next )
		Con_HistoryToField( in, &self->backup );
	else
		Con_HistoryToField( in, &self->lines[self->line % CON_HISTORY] );
}

/*
===================
Con_HistoryAppend
===================
*/
static void Con_HistoryAppend( con_history_t *self, const field_t *from )
{
	int prevLine = Q_max( 0, self->line - 1 );
	const char *buf = from->buffer;

	// skip backslashes
	if( from->buffer[0] == '\\' || from->buffer[1] == '/' )
		buf++;

	// only if non-empty
	if( !from->buffer[0] )
		return;

	// skip empty commands
	if( Q_isspace( buf ))
		return;

	// if not copy (don't ignore backslashes)
	if( !Q_strcmp( from->buffer, self->lines[prevLine % CON_HISTORY].buffer ))
		return;

	Con_HistoryFromField( &self->lines[self->next % CON_HISTORY], from );
	self->line = ++self->next;
}

static void Con_LoadHistory( con_history_t *self )
{
	file_t *fd = FS_Open( "console_history.txt", "rb", true );

	if( !fd )
		return;

	while( !FS_Eof( fd ))
	{
		history_line_t *f = &self->lines[self->next % CON_HISTORY];

		FS_Gets( fd, f->buffer, sizeof( f->buffer ));
		f->cursor = Q_strlen( f->buffer );
		f->scroll = 0;

		// skip empty lines
		if( f->cursor == 0 )
			continue;

		// skip repeating lines
		if( self->next > 0 )
		{
			const history_line_t *prev = &self->lines[(self->next - 1) % CON_HISTORY];
			if( !Q_stricmp( prev->buffer, f->buffer ))
				continue;
		}

		self->next++;
	}

	FS_Close( fd );

	for( int i = self->next; i < CON_HISTORY; i++ )
	{
		history_line_t *f = &self->lines[i];

		memset( f, 0, sizeof( *f ));
	}

	self->line = self->next;
}

static void Con_SaveHistory( con_history_t *self )
{
	// do not save history if nothing was executed
	if( self->next == 0 )
		return;

	int historyStart = self->next - CON_HISTORY;
	if( historyStart < 0 )
		historyStart = 0;

	file_t *f = FS_Open( "console_history.txt", "wb", true );

	if( !f )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, "console_history.txt" );
		return;
	}

	for( int i = historyStart; i < self->next; i++ )
	{
		const char *s = self->lines[i % CON_HISTORY].buffer;

		// HACKHACK: don't save lines that have something that looks like a password
		if( Q_stristr( s, "password" ) || Q_stristr( s, "_pw" ))
			continue;

		FS_Printf( f, "%s\n", s );
	}

	FS_Close( f );
}


/*
=============================================================================

CONSOLE LINE EDITING

=============================================================================
*/
static void Con_InputCompletion( void )
{
	if( !con_showcompletion.value )
		return;

	// keep a copy of console input
	con.input_completion = con.input;

	Con_CompleteCommand( &con.input_completion, false );
}

/*
====================
Key_Console

Handles history and console scrollback
====================
*/
void Key_Console( int key )
{
	// exit the console by pressing MINUS on NSwitch
	// or both Back(Select)/Start buttons for everyone else
	if( key == K_BACK_BUTTON || key == K_START_BUTTON || key == K_ESCAPE )
	{
		if( cls.state == ca_active && !cl.background )
		{
			UI_SetActiveMenu( false ); // we are in game, prevent menu from drawing
			Key_SetKeyDest( key_game );
		}
		else
			UI_SetActiveMenu( true );
		return;
	}

	// milestone 3b: the close "X" of the styled console window. Only ever acts
	// inside that one box, and only while the styled window is the thing on
	// screen; otherwise the click falls through exactly as it used to.
	if( key == K_MOUSE1 && Con_StyleCloseHit( con_style_mouse_x, con_style_mouse_y ))
	{
		Con_ToggleConsole_f();
		return;
	}

	// ctrl-L clears screen
	if( key == 'l' && Key_IsDown( K_CTRL ))
	{
		Cbuf_AddText( "clear\n" );
		return;
	}

	// enter or A finish the line
	if( key == K_ENTER || key == K_KP_ENTER || key == K_A_BUTTON )
	{
		// backslash text are commands, else chat
		if( con.input.buffer[0] == '\\' || con.input.buffer[0] == '/' )
			Cbuf_AddText( con.input.buffer + 1 ); // skip backslash
		else Cbuf_AddText( con.input.buffer ); // valid command
		Cbuf_AddText( "\n" );

		// echo to console
		Con_Printf( ">%s\n", con.input.buffer );

		// copy line to history buffer
		// just in case, remove all CR and LF characters pushing it to the history
		// not sure how they get even added in the first place
		COM_RemoveLineFeed( con.input.buffer, sizeof( con.input.buffer ));
		Con_HistoryAppend( &con.history, &con.input );

		Con_ClearField( &con.input );
		con.input.widthInChars = con.linewidth;
		Con_Bottom();
		Con_ClearField( &con.input_completion );

		if( cls.state == ca_disconnected )
		{
			// force an update, because the command may take some time
			SCR_UpdateScreen ();
		}
		return;
	}

	// command completion
	if( key == K_TAB || key == K_L2_BUTTON || key == K_LTRIGGER )
	{
		Con_CompleteCommand( &con.input, true );
		Con_Bottom();
		Con_ClearField( &con.input_completion );
		return;
	}

	// command history (ctrl-p ctrl-n for unix style)
	if(( key == K_MWHEELUP && Key_IsDown( K_SHIFT )) || ( key == K_UPARROW ) || (( Q_tolower(key) == 'p' ) && Key_IsDown( K_CTRL )))
	{
		Con_HistoryUp( &con.history, &con.input );
		Con_InputCompletion();
		return;
	}

	if(( key == K_MWHEELDOWN && Key_IsDown( K_SHIFT )) || ( key == K_DOWNARROW ) || (( Q_tolower(key) == 'n' ) && Key_IsDown( K_CTRL )))
	{
		Con_HistoryDown( &con.history, &con.input );
		Con_InputCompletion();
		return;
	}

	// console scrolling
	if( key == K_PGUP || key == K_DPAD_UP )
	{
		Con_PageUp( 1 );
		return;
	}

	if( key == K_PGDN || key == K_DPAD_DOWN )
	{
		Con_PageDown( 1 );
		return;
	}

	if( key == K_MWHEELUP )
	{
		if( Key_IsDown( K_CTRL ))
			Con_PageUp( 8 );
		else Con_PageUp( 2 );
		return;
	}

	if( key == K_MWHEELDOWN )
	{
		if( Key_IsDown( K_CTRL ))
			Con_PageDown( 8 );
		else Con_PageDown( 2 );
		return;
	}

	// ctrl-home = top of console
	if( key == K_HOME && Key_IsDown( K_CTRL ))
	{
		Con_Top();
		return;
	}

	// ctrl-end = bottom of console
	if( key == K_END && Key_IsDown( K_CTRL ))
	{
		Con_Bottom();
		return;
	}

	// enable the OSK with button press
	if( key == K_Y_BUTTON )
	{
		Key_EnableTextInput( true, true );
		return;
	}

	// pass to the normal editline routine
	Field_KeyDownEvent( &con.input, key );
	Con_InputCompletion();
}

/*
================
Key_Message

In game talk message
================
*/
void Key_Message( int key )
{
	char	buffer[MAX_SYSPATH];

	if( key == K_ESCAPE || key == K_BACK_BUTTON )
	{
		Key_SetKeyDest( key_game );
		Con_ClearField( &con.chat );
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER || key == K_A_BUTTON )
	{
		if( con.chat.buffer[0] && cls.state == ca_active )
		{
			Q_snprintf( buffer, sizeof( buffer ), "%s \"%s\"\n", con.chat_cmd, con.chat.buffer );

			if( g_messagemode_privileged )
				Cbuf_AddText( buffer );
			else Cbuf_AddFilteredText( buffer );
		}

		Key_SetKeyDest( key_game );
		Con_ClearField( &con.chat );
		return;
	}

	Field_KeyDownEvent( &con.chat, key );
}

/*
==============================================================================

DRAWING

==============================================================================
*/
/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
static void Con_DrawInput( int lines )
{
	// don't draw anything (always draw if not active)
	if( cls.key_dest != key_console || !con.curFont )
		return;

	int y = lines - ( con.curFont->charHeight * 2 );
	CL_DrawCharacter( con.curFont->charWidths[' '], y, ']', g_color_table[7], con.curFont, 0 );
	int x = Field_DrawInputLine( con.curFont->charWidths[' ']*2, y, &con.input, 255, true );

	// HACKHACK: avoid rendering issues when scroll != 0
	if( con_showcompletion.value && con.input.scroll == 0 )
	{
		int len = Q_strlen( con.input.buffer );

		if( FBitSet( con_showcompletion.flags, FCVAR_CHANGED ))
		{
			Con_InputCompletion();
			ClearBits( con_showcompletion.flags, FCVAR_CHANGED );
		}

		// Con_CompleteCommand destroys the buffer. Need to figure out how to make it append only
		if( Q_strlen(con.input_completion.buffer) > len && !Q_strncmp( con.input.buffer, con.input_completion.buffer, len ))
		{
			con.input_completion.scroll = len;
			Field_DrawInputLine( con.curFont->charWidths[' ']*2 + x, y, &con.input_completion, 128, false );
		}
	}
}

/*
================
Con_DrawDebugLines

Custom debug messages
================
*/
static int Con_DrawDebugLines( void )
{
	notify_t *notify = con.notify;
	int	count = 0;
	int	y = 20;

	if( !con.curFont || !con.curFont->valid )
		return 0;

	int defaultX = refState.width / 4;
	int fontTall = con.curFont->charHeight + 1;

	for( int i = 0; i < ARRAYSIZE( con.notify ); i++, notify++ )
	{
		int x, len;

		if( host.realtime > notify->expire )
			continue;

		if( notify->key_dest != cls.key_dest )
			continue;

		Con_DrawStringLen( notify->szNotify, &len, NULL );
		x = refState.width - Q_max( defaultX, len ) - 10;

		if( y + fontTall > refState.height - 20 )
			return count;

		count++;
		y += fontTall;
		CL_DrawString( x, y, notify->szNotify, notify->color, con.curFont, FONT_DRAW_UTF8 | FONT_DRAW_NOLF );
	}

	return count;
}

/*
================
Con_DrawDebug

Draws the debug messages (not passed to console history)
================
*/
void Con_DrawDebug( void )
{
	static double	timeStart;

	if( scr_download.value != -1.0f )
	{
		string dlstring;
		int length;
		Q_snprintf( dlstring, sizeof( dlstring ), "Downloading [%d remaining]: ^2%s^7 %5.1f%% time %.f secs",
			host.downloadcount, host.downloadfile, scr_download.value, Platform_DoubleTime() - timeStart );

		Con_DrawStringLen( dlstring, &length, NULL );
		length = Q_max( length, 300 );
		int x = refState.width - length * 1.05f;
		int y = con.curFont->charHeight * 1.05f;
		Con_DrawString( x, y, dlstring, g_color_table[7] );
	}
	else
	{
		timeStart = host.realtime;
	}

	if( !host.allow_console || Con_BackgroundMapActive( ))
		return;

	if( con.draw_notify && !Con_Visible( ))
	{
		if( Con_DrawDebugLines() == 0 )
			con.draw_notify = false;
	}
}

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify( void )
{
	double	time = cl.time;
	int	y = 0;

	if( !con.curFont ) return;

	int x = con.curFont->charWidths[' ']; // offset one space at left screen side

	// cof_notify 0: no notify lines (the chat input below still draws)
	if( host.allow_console && !Con_BackgroundMapActive( ) && cof_notify.value != 0.0f )
	{
		for( int i = Q_max( 0, CON_LINES_COUNT - con.num_times ); i < CON_LINES_COUNT; i++ )
		{
			con_lineinfo_t	*l = &CON_LINES( i );

			if( l->addtime < ( time - con_notifytime.value ))
				continue;

			Con_DrawString( x, y, l->start, g_color_table[7] );
			y += con.curFont->charHeight;
		}
	}

	if( cls.key_dest == key_message )
	{
		string	buf;
		int	len;

		// update chatline position from client.dll
		if( clgame.dllFuncs.pfnChatInputPosition )
			clgame.dllFuncs.pfnChatInputPosition( &x, &y );

		Q_snprintf( buf, sizeof( buf ), "%s: ", con.chat_cmd );

		Con_DrawStringLen( buf, &len, NULL );
		Con_DrawString( x, y, buf, g_color_table[7] );

		Field_DrawInputLine( x + len, y, &con.chat, 255, true );
	}

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
================
Con_DrawConsoleLine

Draws a line of the console; returns its height in lines.
If alpha is 0, the line is not drawn, but still wrapped and its height
returned.
================
*/
static int Con_DrawConsoleLine( int y, int lineno )
{
	con_lineinfo_t	*li = &CON_LINES( lineno );

	if( !li || !li->start || *li->start == '\1' )
		return 0;	// this string will be shown only at notify

	if( y >= con.curFont->charHeight )
	{
		float x = con.curFont->charWidths[' '];

		CL_DrawString( x, y, li->start, g_color_table[7], con.curFont, FONT_DRAW_UTF8 );
	}

	return con.curFont->charHeight;
}

/*
================
Con_LastVisibleLine

Calculates the last visible line index and how much to show
of it based on con.backscroll.
================
*/
static void Con_LastVisibleLine( int *lastline )
{
	int lines_seen = 0;

	con.backscroll = Q_max( 0, con.backscroll );
	*lastline = 0;

	// now count until we saw con_backscroll actual lines
	for( int i = CON_LINES_COUNT - 1; i >= 0; i-- )
	{
		// line is the last visible line?
		*lastline = i;

		if( lines_seen + 1 > con.backscroll && lines_seen <= con.backscroll )
			return;

		lines_seen += 1;
	}

	// if we get here, no line was on screen - scroll so that one line is visible then.
	con.backscroll = lines_seen - 1;
}

/*
==============================================================================

CRY OF FEAR UNIFIED UI, MILESTONE 3B: THE CONSOLE AS A SOURCE-STYLE WINDOW

A centred panel of cof_console_width x cof_console_height of the screen, with a
translucent dark-grey body, a hairline border, a title band carrying the title
and the build string, and a separate input box at the bottom. Nothing about the
console's *behaviour* changes: the tilde toggle, the key handling, the history,
the notify lines, the backscroll and the completion ghost are all the stock
code. Only Con_DrawSolidConsole() forks, and only when cof_console_style is on,
so "cof_console_style 0" reaches the original drawing unchanged.

Geometry is derived from refState.width/height and the loaded bitmap font, so
there is no 4:3 assumption anywhere in here.

==============================================================================
*/
typedef struct
{
	int	x, y, w, h;			// the panel
	int	border;				// hairline width, pixels
	int	padx, pady;
	int	band_h;				// title band height
	int	log_x, log_y, log_w, log_h;	// log area, log_y is its top
	int	in_x, in_y, in_w, in_h;		// input box, outer edge
	int	close_x, close_y, close_s;	// close button box
} constyle_t;

static qboolean Con_StyleEnabled( void )
{
	return cof_console_style.value != 0.0f;
}

/*
================
Con_StyleColor

"r g b" or "r g b a"; anything else falls back to the cvar's own default
================
*/
static void Con_StyleColor( convar_t *cv, rgba_t out, byte alphaScale )
{
	int	r = 255, g = 255, b = 255, a = 255;
	int	num;

	num = sscanf( cv->string, "%i %i %i %i", &r, &g, &b, &a );

	if( num < 3 )
	{
		r = 255; g = 255; b = 255; a = 255;
		if( sscanf( cv->def_string, "%i %i %i %i", &r, &g, &b, &a ) < 3 )
			r = g = b = a = 255;
	}
	else if( num == 3 )
	{
		a = 255;
	}

	out[0] = bound( 0, r, 255 );
	out[1] = bound( 0, g, 255 );
	out[2] = bound( 0, b, 255 );
	out[3] = ( bound( 0, a, 255 ) * alphaScale ) / 255;
}

static void Con_StyleFill( int x, int y, int w, int h, const rgba_t color )
{
	if( w <= 0 || h <= 0 || color[3] == 0 )
		return;

	ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, color[0], color[1], color[2], color[3] );
}

// a hairline frame drawn *inside* the given rect, unlike mainui's UI_DrawRectangle
static void Con_StyleFrame( int t, int x, int y, int w, int h, const rgba_t color )
{
	if( t < 1 ) t = 1;

	if( w <= t * 2 || h <= t * 2 )
		return;

	Con_StyleFill( x,         y,         w, t,         color );
	Con_StyleFill( x,         y + h - t, w, t,         color );
	Con_StyleFill( x,         y + t,     t, h - t * 2, color );
	Con_StyleFill( x + w - t, y + t,     t, h - t * 2, color );
}

/*
================
Con_StyleLayout

Pure: no side effects, so Con_GetInputRect() can ask for the same numbers the
drawing code uses.
================
*/
static qboolean Con_StyleLayout( constyle_t *r )
{
	int	charH, minw, minh, gap;
	float	fw, fh;

	if( !con.curFont || !con.curFont->valid )
		return false;

	if( refState.width < 320 || refState.height < 200 )
		return false;

	memset( r, 0, sizeof( *r ));

	charH = con.curFont->charHeight;
	if( charH < 4 ) charH = 4;

	r->border = Q_max( 1, refState.height / 720 );
	r->padx   = Q_max( 8, refState.height / 72 );
	r->pady   = Q_max( 5, refState.height / 144 );
	gap       = r->pady;

	r->band_h = charH + r->pady * 2;
	r->in_h   = charH + r->pady * 2;

	fw = bound( 0.25f, cof_console_width.value, 1.0f );
	fh = bound( 0.25f, cof_console_height.value, 1.0f );

	// the panel must at least hold band + one log line + input box + padding
	minw = r->padx * 6 + charH * 8;
	minh = r->band_h + gap + charH + gap + r->in_h + r->pady;

	r->w = bound( minw, (int)( refState.width  * fw ), refState.width  );
	r->h = bound( minh, (int)( refState.height * fh ), refState.height );

	if( r->w > refState.width || r->h > refState.height )
		return false; // screen too small for a window, caller falls back

	r->x = ( refState.width  - r->w ) / 2;
	r->y = ( refState.height - r->h ) / 2;

	r->in_x = r->x + r->padx;
	r->in_w = r->w - r->padx * 2;
	r->in_y = r->y + r->h - r->pady - r->in_h;

	r->log_x = r->x + r->padx;
	r->log_w = r->w - r->padx * 2;
	r->log_y = r->y + r->band_h + gap;
	r->log_h = ( r->in_y - gap ) - r->log_y;

	if( r->log_h < charH )
		r->log_h = charH;

	r->close_s = r->band_h - r->pady;
	r->close_x = r->x + r->w - r->padx - r->close_s;
	r->close_y = r->y + ( r->band_h - r->close_s ) / 2;

	return true;
}

/*
================
Con_StyleLogWidth

Width in pixels of the styled panel's log area, or 0 when the styled console is
off or cannot be laid out. Con_CheckResize() uses it as the wrap width so that
a long line is rewrapped inside the panel instead of being clipped at its edge.
================
*/
static int Con_StyleLogWidth( void )
{
	constyle_t	r;

	if( !Con_StyleEnabled( ))
		return 0;

	if( !Con_StyleLayout( &r ))
		return 0;

	return r.log_w;
}

/*
================
Con_StyleCloseHit

The close "X" is clickable only if a mouse position ever reached us and the
styled console is the thing on screen. Called from Key_Console().
================
*/
static qboolean Con_StyleCloseHit( int mx, int my )
{
	constyle_t	r;

	if( !Con_StyleEnabled() || !Con_Visible() )
		return false;

	if( mx < 0 || my < 0 )
		return false;

	if( !Con_StyleLayout( &r ))
		return false;

	return mx >= r.close_x && mx < r.close_x + r.close_s
		&& my >= r.close_y && my < r.close_y + r.close_s;
}

/*
================
Con_MouseMove

Called from IN_MouseMove() next to UI_MouseMove(). Only remembers where the
cursor is; nothing else in the console reads it unless the styled window is up.
================
*/
void Con_MouseMove( int x, int y )
{
	con_style_mouse_x = x;
	con_style_mouse_y = y;
}

/*
================
Con_DrawStringClipped

Like Con_DrawString(), but stops before it would draw past maxWidth, so a long
log line cannot spill out of the panel. Colour codes and the UTF-8 state machine
behave exactly as in CL_DrawString().
================
*/
static int Con_DrawStringClipped( int x, int y, int maxWidth, const char *s, const rgba_t color )
{
	cl_font_t	*font = con.curFont;
	rgba_t		cur;
	int		draw_len = 0;
	int		widest;

	if( !font || !font->valid || !s )
		return 0;

	widest = font->charWidths['W'];
	if( widest <= 0 ) widest = font->charHeight;

	Con_UtfProcessChar( 0 ); // clear utf state
	CL_SetFontRendermode( font );

	memcpy( cur, color, sizeof( cur ));

	while( *s )
	{
		if( *s == '\n' )
		{
			s++;
			continue; // console lines never carry one, but never wrap here either
		}

		if( IsColorString( s ))
		{
			const byte *c = g_color_table[ColorIndex( *( s + 1 ))];

			MakeRGBA( cur, c[0], c[1], c[2], color[3] );
			s += 2;
			continue;
		}

		if( draw_len + widest > maxWidth )
			break;

		draw_len += CL_DrawCharacter( x + draw_len, y, (byte)*s, cur, font,
			FONT_DRAW_UTF8 | FONT_DRAW_NORENDERMODE );
		s++;
	}

	return draw_len;
}

/*
================
Con_DrawStyledField

The stock Field_DrawInputLine() hardcodes the default console colour and has no
width limit; this is the same logic inside the panel's input box.
================
*/
static int Con_DrawStyledField( int x, int y, int maxWidth, const field_t *edit, const rgba_t color, const rgba_t caret, byte alpha, qboolean cursor )
{
	char	str[MAX_SYSPATH];
	rgba_t	base, tip;
	const int prestep = bound( 0, edit->scroll, (int)sizeof( edit->buffer ) - 1 );
	const int drawLen = bound( 0, edit->widthInChars, (int)sizeof( str ));
	const int cursorCharPos = bound( 0, edit->cursor - prestep, (int)sizeof( str ) - 1 );
	int	curPos = 0;

	MakeRGBA( base, color[0], color[1], color[2], alpha );
	Q_strncpy( str, edit->buffer + prestep, drawLen );

	Con_DrawStringClipped( x, y, maxWidth, str, base );

	// where the caret goes
	str[cursorCharPos] = 0;
	CL_DrawStringLen( con.curFont, str, &curPos, NULL, FONT_DRAW_UTF8 );

	if( !cursor || (int)( host.realtime * 4 ) & 1 )
		return curPos; // off blink

	if( curPos < maxWidth )
	{
		MakeRGBA( tip, caret[0], caret[1], caret[2], alpha );
		CL_DrawCharacter( x + curPos, y, host.key_overstrike ? '|' : '_', tip, con.curFont, 0 );
	}

	return curPos;
}

/*
================
Con_DrawStyledInput
================
*/
static void Con_DrawStyledInput( const constyle_t *r, const rgba_t text, const rgba_t accent, byte alpha )
{
	rgba_t	bg, frame, prompt;
	int	tx, ty, tw, x, chars, charW;

	if( cls.key_dest != key_console || !con.curFont )
		return;

	Con_StyleColor( &cof_console_input_bg_color, bg, alpha );
	Con_StyleColor( &cof_console_input_border_color, frame, alpha );

	Con_StyleFill( r->in_x, r->in_y, r->in_w, r->in_h, bg );
	Con_StyleFrame( r->border, r->in_x, r->in_y, r->in_w, r->in_h, frame );

	tx = r->in_x + r->border + r->pady;
	ty = r->in_y + r->pady;
	tw = r->in_w - ( tx - r->in_x ) - r->border - r->pady;

	if( tw <= 0 )
		return;

	// keep the field's horizontal scrolling in step with the box, not the screen
	charW = con.curFont->charWidths['O'] - 1;
	if( charW < 1 ) charW = 1;
	chars = Q_max( 8, ( tw / charW ) - 2 );
	if( con.input.widthInChars != chars )
		con.input.widthInChars = chars;

	MakeRGBA( prompt, accent[0], accent[1], accent[2], alpha );
	x = CL_DrawCharacter( tx, ty, ']', prompt, con.curFont, 0 );

	x += Con_DrawStyledField( tx + x, ty, tw - x, &con.input, text, accent, alpha, true );

	// completion ghost, same guard as the stock path
	if( con_showcompletion.value && con.input.scroll == 0 )
	{
		int len = Q_strlen( con.input.buffer );

		if( FBitSet( con_showcompletion.flags, FCVAR_CHANGED ))
		{
			Con_InputCompletion();
			ClearBits( con_showcompletion.flags, FCVAR_CHANGED );
		}

		if( Q_strlen( con.input_completion.buffer ) > len && !Q_strncmp( con.input.buffer, con.input_completion.buffer, len ))
		{
			con.input_completion.scroll = len;
			con.input_completion.widthInChars = con.input.widthInChars;
			Con_DrawStyledField( tx + x, ty, tw - x, &con.input_completion, text, text, alpha / 2, false );
		}
	}
}

/*
================
Con_DrawStyledConsole

lines is the stock slide-in height. The styled path does not slide and does not
fade: Con_RunConsole() snaps con.vislines to Con_DestHeight() while this style
is on, so lines is either the full render height or half of it and the alpha
below is always 255. The expression is kept because it is what makes the window
vanish on the frame the console is closed - lines becomes 0, alpha becomes 0,
and nothing is drawn.
================
*/
static qboolean Con_DrawStyledConsole( int lines )
{
	constyle_t	r;
	rgba_t		panel, border, band, title, text, accent, xcol;
	string		curbuild;
	byte		alpha;
	float		frac;
	int		y, ymin, lastline, stringLen, charH, tx;
	const char	*titleText;

	// no font or no console at all: let the stock path handle it
	if( !con.curFont || !host.allow_console )
		return false;

	// screen too small for a window: fall back to the stock drawing
	if( !Con_StyleLayout( &r ))
		return false;

	frac  = lines / (float)Q_max( 1, refState.height );
	alpha = (byte)( Q_min( frac * 2.0f, 1.0f ) * 255 );
	if( alpha == 0 )
		return true;

	Con_StyleColor( &cof_console_panel_color,  panel,  alpha );
	Con_StyleColor( &cof_console_border_color, border, alpha );
	Con_StyleColor( &cof_console_band_color,   band,   alpha );
	Con_StyleColor( &cof_console_title_color,  title,  alpha );
	Con_StyleColor( &cof_console_text_color,   text,   alpha );
	Con_StyleColor( &cof_console_accent_color, accent, alpha );

	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 ); // don't inherit a screenfade colour

	// body, title band, hairline under the band, outer border
	Con_StyleFill( r.x, r.y, r.w, r.h, panel );
	Con_StyleFill( r.x, r.y, r.w, r.band_h, band );
	Con_StyleFill( r.x, r.y + r.band_h, r.w, r.border, border );
	Con_StyleFrame( r.border, r.x, r.y, r.w, r.h, border );

	// title, small and uppercase
	titleText = cof_console_title.string;
	if( COM_StringEmpty( titleText ))
		titleText = "CONSOLE";

	tx = r.x + r.padx;
	Con_DrawStringClipped( tx, r.y + r.pady, r.w - r.padx * 2, titleText, title );

	// the build string lives in the band now, not across the top of the screen,
	// with the cofenhanced stamp appended after the engine's own build
	Q_snprintf( curbuild, sizeof( curbuild ), XASH_ENGINE_NAME " %i/" XASH_VERSION " (%s-%s build %i)  %s",
		PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ), COFE_BUILD_STRING );
	Con_DrawStringLen( curbuild, &stringLen, &charH );

	{
		rgba_t dim;
		int bx = r.close_x - r.padx - stringLen;

		MakeRGBA( dim, title[0], title[1], title[2], alpha / 2 );
		// milestone 4: Con_DrawStringClipped() keeps a whole 'W' of headroom
		// before it stops, so an exact maxWidth drops the last glyphs. The
		// string was measured, it cannot draw past stringLen, so hand it that
		// headroom back - otherwise the build string loses its ")" as soon as
		// the glyphs get big.
		if( bx > tx + r.padx * 4 )
			Con_DrawStringClipped( bx, r.y + r.pady,
				stringLen + con.curFont->charWidths['W'] + 1, curbuild, dim );
	}

	// close button: an "X" in the band, highlighted while the cursor is over it
	if( Con_StyleCloseHit( con_style_mouse_x, con_style_mouse_y ))
		memcpy( xcol, accent, sizeof( xcol ));
	else memcpy( xcol, title, sizeof( xcol ));

	{
		int cw = con.curFont->charWidths['X'];

		CL_DrawCharacter( r.close_x + ( r.close_s - cw ) / 2,
			r.close_y + ( r.close_s - con.curFont->charHeight ) / 2,
			'X', xcol, con.curFont, 0 );
	}

	// the log
	charH = con.curFont->charHeight;
	ymin  = r.log_y;
	y     = r.log_y + r.log_h - charH;

	if( CON_LINES_COUNT > 0 )
	{
		int i;

		Con_LastVisibleLine( &lastline );

		if( con.backscroll )
		{
			int step = con.curFont->charWidths[' '];
			int x;

			// red arrows, as in the stock console, but inside the panel
			CL_SetFontColor( con.curFont, g_color_table[1] );
			for( x = 0; ( x + 1 ) * step < r.log_w; x += 4 )
				CL_DrawCharacter( r.log_x + ( x + 1 ) * step, y, '^', NULL, con.curFont, FONT_DRAW_NOCOLOR );
			y -= charH;
		}

		i = lastline;

		while( y >= ymin )
		{
			con_lineinfo_t *li = &CON_LINES( i );

			if( li && li->start && *li->start != '\1' )
			{
				Con_DrawStringClipped( r.log_x, y, r.log_w, li->start, text );
				y -= charH;
			}

			if( i == 0 )
				break;
			i--;
		}
	}

	Con_DrawStyledInput( &r, text, accent, alpha );

	SCR_DrawFPS( 4 );

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );

	return true;
}

/*
================
Con_DrawConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole( int lines )
{
	int	x, y;
	float	fraction;
	int	start;
	int	stringLen, charH;
	string	curbuild;
	byte	color[4];

	if( lines <= 0 ) return;

	// milestone 3b: the Source-style window. Everything below this point is the
	// stock drawing and is reached unchanged with "cof_console_style 0".
	if( Con_StyleEnabled() && Con_DrawStyledConsole( lines ))
		return;

	// draw the background
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 ); // to prevent grab color from screenfade
	if( refState.width * 3 / 4 < refState.height && lines >= refState.height )
		ref.dllFuncs.R_DrawStretchPic( 0, lines - refState.height, refState.width, refState.height - refState.width * 3 / 4, 0, 0, 1, 1, R_GetBuiltinTexture( REF_BLACK_TEXTURE ));
	ref.dllFuncs.R_DrawStretchPic( 0, lines - refState.width * 3 / 4, refState.width, refState.width * 3 / 4, 0, 0, 1, 1, con.background );

	if( !con.curFont || !host.allow_console )
		return; // nothing to draw

	// draw current version
	memcpy( color, g_color_table[7], sizeof( color ));

	Q_snprintf( curbuild, MAX_STRING, XASH_ENGINE_NAME " %i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));

	Con_DrawStringLen( curbuild, &stringLen, &charH );

	start = refState.width - stringLen;
	fraction = lines / (float)refState.height;
	color[3] = Q_min( fraction * 2.0f, 1.0f ) * 255; // fadeout version number

#if XASH_MOBILE_PLATFORM
	// the top of the screen might be hidden by the on-screen keyboard panning
	Con_DrawString( start, lines - charH, curbuild, color );
#else
	Con_DrawString( start, 0, curbuild, color );
#endif

	// draw the text
	if( CON_LINES_COUNT > 0 )
	{
		int	ymax = lines - (con.curFont->charHeight * 2.0f);
		int	lastline;

		Con_LastVisibleLine( &lastline );
		y = ymax - con.curFont->charHeight;

		if( con.backscroll )
		{
			start = con.curFont->charWidths[' ']; // offset one space at left screen side

			// draw red arrows to show the buffer is backscrolled
			CL_SetFontColor( con.curFont, g_color_table[1] );
			for( x = 0; x < con.linewidth; x += 4 )
				CL_DrawCharacter( ( x + 1 ) * start, y, '^', NULL, con.curFont, FONT_DRAW_NOCOLOR );
			y -= con.curFont->charHeight;
		}
		x = lastline;

		while( 1 )
		{
			y -= Con_DrawConsoleLine( y, x );

			// top of console buffer or console window
			if( x == 0 || y < con.curFont->charHeight )
				break;
			x--;
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( lines );

	y = lines - ( con.curFont->charHeight * 1.2f );
	SCR_DrawFPS( Q_max( y, 4 )); // to avoid to hide fps counter

	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void )
{
	// never draw console when changelevel in-progress
	if( cls.state != ca_disconnected && ( cls.changelevel || cls.changedemo ))
		return;

	// milestone 3b: the styled input box narrows con.input.widthInChars, so when
	// the style is switched off Con_CheckResize() has to recompute it from the
	// screen; and the greyscale font flag is baked into the texture, so it needs
	// the fonts reloaded, exactly as a video mode change does.
	{
		static qboolean styled_last = false;
		static qboolean gray_last = false;
		qboolean styled_now = Con_StyleEnabled();
		qboolean gray_now = styled_now && cof_console_font_grayscale.value != 0.0f;

		if( styled_now != styled_last )
		{
			styled_last = styled_now;
			con.linewidth = 0; // force Con_CheckResize() to redo the width
		}

		if( gray_now != gray_last )
		{
			gray_last = gray_now;
			Con_InvalidateFonts();
			Con_LoadConchars();
			con.linewidth = 0;
		}
	}

	// milestone 4: Con_RunConsole() is not reached in the connecting states, so
	// the autoscale factor is re-checked here too before anything is measured
	Con_CheckTextScale();

	// check for console width changes from a vid mode change
	Con_CheckResize ();

	if( cls.state == ca_connecting || cls.state == ca_connected )
	{
		if( !cl_allow_levelshots.value && !cls.timedemo )
		{
			if( cls.key_dest != key_console && Con_BackgroundMapActive( ))
				con.vislines = con.showlines = 0;
			else con.vislines = con.showlines = refState.height;
		}
		else
		{
			con.showlines = 0;

			if( host_developer.value >= DEV_EXTENDED && !cls.timedemo )
				Con_DrawNotify(); // draw notify lines
		}
	}

	// if disconnected, render console full screen
	switch( cls.state )
	{
	case ca_disconnected:
		if( cls.key_dest != key_menu )
		{
			Con_DrawSolidConsole( refState.height );
			Key_SetKeyDest( key_console );
		}
		break;
	case ca_connecting:
	case ca_connected:
	case ca_validate:
		// force to show console always for -dev 3 and higher
		Con_DrawSolidConsole( con.vislines );
		break;
	case ca_active:
	case ca_cinematic:
		if( Con_BackgroundMapActive( ))
		{
			if( cls.key_dest == key_console )
				Con_DrawSolidConsole( refState.height );
		}
		else
		{
			if( con.vislines )
				Con_DrawSolidConsole( con.vislines );
			else if( cls.state == ca_active && ( cls.key_dest == key_game || cls.key_dest == key_message ) && !cls.timedemo )
				Con_DrawNotify(); // draw notify lines
		}
		break;
	}

	if( !Con_Visible( )) SCR_DrawFPS( 4 );
}

/*
==================
Con_DrawVersion

Used by menu
==================
*/
void Con_DrawVersion( void )
{
	// draws the current build
	byte	*color = g_color_table[7];
	string	curbuild;

	if( !scr_drawversion.value )
		return;

	if( cls.key_dest == key_menu )
	{
		Q_snprintf( curbuild, sizeof( curbuild ),
			"v%i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
	}
	else
	{
		qboolean draw_version;

		if( CL_IsDevOverviewMode() == 2 || net_graph.value )
			return;

		draw_version = cls.scrshot_action == scrshot_normal
			|| cls.scrshot_action == scrshot_snapshot
			|| host.force_draw_version_time > host.realtime;

		if( !draw_version )
			return;

		Q_snprintf( curbuild, sizeof( curbuild ),
			XASH_ENGINE_NAME " v%i/" XASH_VERSION " (%s-%s build %i)", PROTOCOL_VERSION, Q_buildos(), Q_buildarch(), Q_buildnum( ));
	}

	int stringLen, charH = 0;
	Con_DrawStringLen( curbuild, &stringLen, &charH );
	int start = refState.width - stringLen * 1.05f;
	int height = refState.height - charH * 1.05f;

	// cofenhanced build stamp, for bug reports: the TOP-right corner, so it
	// never covers the HUD at the bottom of the screen. Same autoscaled font
	// (Con_DrawString/Con_DrawStringLen already carry the cof_text_autoscale
	// sizing), same colour and alpha as the engine line, and the same inset
	// from the two edges as that line keeps in the bottom-right corner (a
	// twentieth of its own width and height). It is drawn exactly when the
	// engine line is (scr_drawversion, the menu, a screenshot, a few seconds
	// after a window event); there is no switch for the stamp alone. While the
	// fps counter is up (cl_showfps in a running game: SCR_DrawFPS, y 4,
	// right-aligned) the stamp moves down below it.
	{
		int	cofLen, cofH = 0;
		int	cofY;

		Con_DrawStringLen( COFE_BUILD_STRING, &cofLen, &cofH );

		cofY = cofH * 0.05f;
		if( cls.state == ca_active && !cl.background && Cvar_VariableValue( "cl_showfps" ) != 0.0f )
		{
			int	fpsLen, fpsH = 0;

			Con_DrawStringLen( "0 fps", &fpsLen, &fpsH );
			cofY = 4 + fpsH + cofH * 0.05f;
		}

		Con_DrawString( refState.width - cofLen * 1.05f, cofY, COFE_BUILD_STRING, color );
	}

	Con_DrawString( start, height, curbuild, color );
}

/*
==================
Con_DestHeight

==================
*/
static int Con_DestHeight( void )
{
	if( !host.allow_console || cls.key_dest != key_console )
		return 0; // none visible

	if( cls.state < ca_active || cl.first_frame )
		return refState.height;	// full screen

	return refState.height >> 1;	// half screen
}

/*
==================
Con_GetInputRect

Where the currently edited line is drawn, in render coordinates
==================
*/
qboolean Con_GetInputRect( int *x, int *y, int *w, int *h )
{
	if( !con.curFont )
		return false;

	if( cls.key_dest == key_console )
	{
		int lines = Con_DestHeight();
		constyle_t r;

		if( lines <= 0 )
			return false;

		// milestone 3b: report the styled window's input box, so the on-screen
		// keyboard and IME candidate window land on it and not on the old
		// full-width line
		if( Con_StyleEnabled() && Con_StyleLayout( &r ))
		{
			*x = r.in_x + r.border + r.pady;
			*y = r.in_y + r.pady;
			*w = r.in_w - ( r.border + r.pady ) * 2;
			*h = con.curFont->charHeight;
			return true;
		}

		*x = con.curFont->charWidths[' '];
		*y = lines - con.curFont->charHeight * 2;
		*w = refState.width - *x;
		*h = con.curFont->charHeight;
		return true;
	}

	if( cls.key_dest == key_message )
	{
		*x = con.curFont->charWidths[' '];
		*y = 0;

		if( clgame.dllFuncs.pfnChatInputPosition )
		{
			clgame.dllFuncs.pfnChatInputPosition( x, y );
			*h = con.curFont->charHeight;
		}
		else
		{
			// chat line is pushed down by the notify lines drawn above it
			*h = con.curFont->charHeight * ( con.num_times + 1 );
		}

		*w = refState.width - *x;
		return true;
	}

	return false;
}

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
/*
================
Con_ApplyEnable

Mirror the archived con_enable onto host.allow_console, the bitfield every
console entry point tests. Runs from Con_RunConsole, which is called every
frame in ca_disconnected (the menu) and ca_active (a live session or a
background map), so the first application happens on the frame after
Host_Init's "exec config.cfg" and every later change lands immediately.

host.allow_console_init has to move with it: CL_Disconnect restores
host.allow_console from it (engine/client/cl_main.c), so setting only the
former would lose the console on the next disconnect. That is exactly what the
stock one-way "ui_allowconsole" command does too.

A console the command line unlocked is never taken away again: con_enable 0 is
"do not unlock it", not "lock it".
================
*/
static void Con_ApplyEnable( void )
{
	int	want = ( con_enable.value != 0.0f ) ? 1 : 0;
	static int applied = -1;

	if( applied == want && !FBitSet( con_enable.flags, FCVAR_CHANGED ))
		return;

	ClearBits( con_enable.flags, FCVAR_CHANGED );
	applied = want;

	if( want )
	{
		if( !host.allow_console )
			Con_Reportf( "[cof-ui] con_enable 1: developer console unlocked\n" );

		host.allow_console = host.allow_console_init = true;
	}
	else if( !con_enable_boot )
	{
		if( host.allow_console )
			Con_Reportf( "[cof-ui] con_enable 0: developer console locked\n" );

		host.allow_console = host.allow_console_init = false;
	}

	// the menu library reads this as its "developer" global
	if( gameui.globals )
		gameui.globals->developer = host.allow_console;
}

void Con_RunConsole( void )
{
	Con_ApplyEnable();

	// milestone 4: pick up a cof_text_autoscale change (a mode change is caught
	// by Con_VidInit instead, and the connecting states by Con_DrawConsole)
	Con_CheckTextScale();

	Con_SetColor( );

	con.showlines = Con_DestHeight();

	// The styled console is a WINDOW, not a GoldSrc drop-down: it does not
	// slide, and con.vislines only ever reached it as the fade alpha
	// (Con_DrawStyledConsole). Animating it meant the panel faded up over
	// scr_conspeed on open and, worse, lingered on the way out - the user asked
	// for both edges to be instant. So the styled path snaps: the window is
	// there on the frame the toggle is pressed and gone on the frame it is
	// pressed again. scr_conspeed keeps driving the stock drop-down exactly as
	// before, which is the only thing it was ever for.
	if( Con_StyleEnabled( ))
	{
		con.vislines = con.showlines;
	}
	else
	{
		float lines_per_frame = fabs( scr_conspeed.value ) * host.realframetime;

		if( con.showlines < con.vislines )
		{
			con.vislines -= lines_per_frame;
			if( con.showlines > con.vislines )
				con.vislines = con.showlines;
		}
		else if( con.showlines > con.vislines )
		{
			con.vislines += lines_per_frame;
			if( con.showlines < con.vislines )
				con.vislines = con.showlines;
		}
	}

	if( FBitSet( con_charset.flags|con_fontscale.flags|con_fontnum.flags|cl_charset.flags|con_oldfont.flags, FCVAR_CHANGED ))
	{
		if( con_fontscale.value < 1.0f )
			Cvar_DirectSet( &con_fontscale, "1" );
		
		// update codepage parameters
		if( !Q_stricmp( con_charset.string, "cp1251" ))
		{
			g_codepage = 1251;
		}
		else if( !Q_stricmp( con_charset.string, "cp1252" ))
		{
			g_codepage = 1252;
		}
		else
		{
			g_codepage = 0;
		}

		cls.accept_utf8 = !Q_stricmp( cl_charset.string, "utf-8" );
		Con_InvalidateFonts();
		Con_LoadConchars();
		ClearBits( con_charset.flags,   FCVAR_CHANGED );
		ClearBits( con_fontnum.flags,   FCVAR_CHANGED );
		ClearBits( con_fontscale.flags, FCVAR_CHANGED );
		ClearBits( cl_charset.flags,    FCVAR_CHANGED );
		ClearBits( con_oldfont.flags,   FCVAR_CHANGED );
	}
}

/*
==============================================================================

CONSOLE INTERFACE

==============================================================================
*/
/*
================
Con_CharEvent

Console input
================
*/
void Con_CharEvent( int key )
{
	// distribute the key down event to the apropriate handler
	if( cls.key_dest == key_console )
	{
		// no tilda or backtick in console is supported
		if( key == '`' || key == '~' )
		{
			Con_ToggleConsole_f();
			return;
		}

		Field_CharEvent( &con.input, key );
		Con_InputCompletion();
	}
	else if( cls.key_dest == key_message )
	{
		Field_CharEvent( &con.chat, key );
	}
}

static int Con_LoadSimpleConback( const char *name, int flags )
{
	for( int i = 0; i < 5; i++ )
	{
		string path;

		switch( i )
		{
		case 0:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.dds", name );
			break;
		case 1:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.bmp", name );
			break;
		case 2:
			Q_snprintf( path, sizeof( path ), "gfx/shell/%s.tga", name );
			break;
		case 3:
			Q_snprintf( path, sizeof( path ), "cached/%s640", name );
			break;
		case 4:
			Q_snprintf( path, sizeof( path ), "cached/%s", name );
			break;
		}

		if( g_fsapi.FileExists( path, false ))
		{
			int gl_texturenum = ref.dllFuncs.GL_LoadTexture( path, NULL, 0, flags );

			if( gl_texturenum )
				return gl_texturenum;
		}
	}

	return 0;
}

/*
=========
Con_VidInit

INTERNAL RESOURCE
=========
*/
void Con_VidInit( void )
{
	const uint flags = TF_IMAGE|TF_ALLOW_NEAREST;

	if( !con.historyLoaded )
	{
		Con_LoadHistory( &con.history );
		con.historyLoaded = true;
	}

	if( Sys_CheckParm( "-oldfont" ))
		Cvar_DirectSet( &con_oldfont, "1" );

	// milestone 4: the render size may have changed under us, so re-derive the
	// text autoscale factor before the fonts are (re)loaded
	Con_CheckTextScale();

	Con_LoadConchars();
	Con_CheckResize();

#if XASH_LOW_MEMORY
	con.background = R_GetBuiltinTexture( REF_GRAY_TEXTURE );
#else
	// loading console image
	con.background = Con_LoadSimpleConback( host.allow_console ? "conback" : "loading", flags );

	if( !con.background ) // last chance - quake conback image
	{
		fs_offset_t length = 0;
		const byte *buf;

		// quake games always use fixed width fonts, hl games only use variable width
		// and we want to store buildnumber only in quake
		if( con.curFont && con.curFont->type == FONT_FIXED && ( buf = ref.dllFuncs.R_GetTextureOriginalBuffer( con.curFont->hFontTexture )) != NULL )
		{
			lmp_t	*cb = (lmp_t *)FS_LoadFile( "gfx/conback.lmp", &length, false );

			// another sanity test, quake background is always 320x200
			if( cb && cb->width == 320 && cb->height == 200 )
			{
				char ver[64];
				int len = Q_snprintf( ver, 64, "%i", Q_buildnum( )); // can store only buildnum
				byte *dest = (byte *)(cb + 1) + 320 * 186 + 320 - 11 - 8 * len;
				int y = len;
				for( int x = 0; x < y; x++ )
					Con_DrawCharToConback( ver[x], buf, dest + (x << 3));
				con.background = ref.dllFuncs.GL_LoadTexture( "#gfx/conback.lmp", (byte *)cb, length, TF_IMAGE );
			}
			if( cb ) Mem_Free( cb );
		}

		if( !con.background ) // trying the load unmodified conback
			con.background = ref.dllFuncs.GL_LoadTexture( "gfx/conback.lmp", NULL, 0, TF_IMAGE );
	}

	// missed console image will be replaced as gray background like X-Ray or Crysis
	if( con.background == R_GetBuiltinTexture( REF_DEFAULT_TEXTURE ) || con.background == 0 )
		con.background = R_GetBuiltinTexture( REF_GRAY_TEXTURE );
#endif
}

/*
=========
Con_InvalidateFonts

=========
*/
void Con_InvalidateFonts( void )
{
	for( int i = 0; i < ARRAYSIZE( con.chars ); i++ )
		CL_FreeFont( &con.chars[i] );
	con.curFont = NULL;
}

/*
=========
Con_FastClose

immediately close the console
=========
*/
void Con_FastClose( void )
{
	Con_ClearField( &con.input );
	Con_ClearNotify();
	con.showlines = 0;
	con.vislines = 0;
}

/*
=========
Con_DefaultColor

called from MainUI
=========
*/
void Con_DefaultColor( int r, int g, int b, qboolean gameui )
{
	r = bound( 0, r, 255 );
	g = bound( 0, g, 255 );
	b = bound( 0, b, 255 );

	// gameui wants to override console color... check if it's not default
	if( gameui && ( g_color_table[7][0] != r || g_color_table[7][1] != g || g_color_table[7][2] != b ))
	{
		// yes, different from default orange, disable con_color
		SetBits( con_color.flags, FCVAR_READ_ONLY );
		ClearBits( con_color.flags, FCVAR_CHANGED );
	}

	MakeRGBA( g_color_table[7], r, g, b, 255 );
}

#if XASH_ENGINE_TESTS
#include "tests.h"

static void Test_RunConHistory( void )
{
	con_history_t hist = { 0 };
	field_t input = { 0 };
	const char *strs1[] = { "map t0a0", "quit", "wtf", "wtf", "", "nyan" };
	const char *strs2[] = { "nyan", "wtf", "quit", "map t0a0" };
	const char *testbackup = "unfinished_edit";

	for( int i = 0; i < ARRAYSIZE( strs1 ); i++ )
	{
		Field_Set( &input, strs1[i] );
		Con_HistoryAppend( &hist, &input );
	}

	Field_Set( &input, testbackup );

	for( int i = 0; i < ARRAYSIZE( strs2 ); i++ )
	{
		Con_HistoryUp( &hist, &input );
		TASSERT_STR( input.buffer, strs2[i] );
	}

	// check for overrun
	Con_HistoryUp( &hist, &input );

	for( int i = ARRAYSIZE( strs2 ) - 1; i >= 0; i-- )
	{
		TASSERT_STR( input.buffer, strs2[i] );
		Con_HistoryDown( &hist, &input );
	}

	TASSERT_STR( input.buffer, testbackup );
}

void Test_RunCon( void )
{
	TRUN( Test_RunConHistory() );
}

#endif /* XASH_ENGINE_TESTS */
