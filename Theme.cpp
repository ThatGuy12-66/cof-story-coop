/*
Theme.cpp -- Source-style theme for the Cry of Fear unified UI (milestone 3a)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "Theme.h"
#include "menus/CryOfFear.h"
#include "utflib.h"

bool g_bThemeActive = false;

cvar_t *ui_theme = NULL;
static cvar_t *ui_cof_menu_scale = NULL;
static float s_cofMenuScaleApplied = 1.0f;

// marker for UI_ThemeApplyDeferredDefaults. It has to be *registered* here, in
// UI_Init, and only read later: UI_Init runs inside CL_Init, before Host_Init
// execs config.cfg, and a name config.cfg sets before the menu has registered
// it does not reach the menu at all. Registering early means config.cfg's own
// line lands on this cvar, which is the whole point of the marker.
static cvar_t *ui_cof_scene_defaults = NULL;

// the marker's generation. 0 = nothing applied yet, 1 = ui_renderworld and
// r_detailtextures, 2 = ui_sound_volume, 3 = hud_scale cleared, 4 = the
// MOUSE2/MOUSE3 swap (aim on the right button). Raise this when a new one-shot
// default is added; UI_ThemeApplyDeferredDefaults then applies only the steps
// between the recorded generation and this one.
// 5 = F5 quick save / F9 quick load (patches/cof-mainui-quicksave.patch)
#define COF_SCENE_DEFAULTS_GEN	5

// Called once per UI_VidInit after the aspect-ratio base scale is reset.
// Reduce all themed CoF controls/text together, keeping the virtual canvas
// centered. Sample until the next VidInit so cvar changes cannot split
// drawing from cached hit rectangles. A value of 1 restores prior sizing.
void UI_ThemeApplyMenuScale( void )
{
	s_cofMenuScaleApplied = 1.0f;
	if( !UI_ThemeActive() || !UI_IsCryOfFear() || !ui_cof_menu_scale )
		return; // preserve the stock scale/offset values exactly
	{
		const float requested = ui_cof_menu_scale->value;
		// Ordered comparisons also reject NaN before it reaches coordinates.
		if( requested >= 0.75f && requested <= 1.0f )
			s_cofMenuScaleApplied = requested;
		else if( requested < 0.75f ) s_cofMenuScaleApplied = 0.75f;
		else if( requested > 1.0f ) s_cofMenuScaleApplied = 1.0f;
		else s_cofMenuScaleApplied = 0.88f;
	}
	uiStatic.scaleX *= s_cofMenuScaleApplied;
	uiStatic.scaleY *= s_cofMenuScaleApplied;
	uiStatic.yOffset = ScreenHeight / ( 2.0f * uiStatic.scaleY ) - 384.0f;
}

float UI_ThemeMenuScaleApplied( void )
{
	return s_cofMenuScaleApplied;
}

/*
=================
UI_ThemeInit

Sampled once. Switching ui_theme at runtime would also have to re-pick the font
family and rebuild every handle, so the cvar is read at startup only and is
documented as needing a restart.
=================
*/
void UI_ThemeInit( void )
{
	ui_theme = EngFuncs::CvarRegister( "ui_theme", "1", FCVAR_ARCHIVE );
	ui_cof_menu_scale = EngFuncs::CvarRegister( "ui_cof_menu_scale", "0.88", FCVAR_ARCHIVE );

	g_bThemeActive = ui_theme && ui_theme->value != 0.0f;

	if( !g_bThemeActive )
		return;

	if( UI_IsCryOfFear( ))
		ui_cof_scene_defaults = EngFuncs::CvarRegister( "ui_cof_scene_defaults", "0", FCVAR_ARCHIVE );

	// Re-tint every control that already reads a global. colors.lst can still
	// override the seven keys it knows, because UI_ApplyCustomColors runs after
	// this - but it cannot set alpha (UI_ParseColor -> PackRGB), so the
	// translucent panels and fills below are only reachable from here.
	uiColorHelp        = THEME_TEXT_DIM;
	uiPromptBgColor    = THEME_PANEL;
	uiPromptTextColor  = THEME_TEXT;
	uiPromptFocusColor = THEME_TEXT_HI;
	uiInputTextColor   = THEME_ACCENT;
	uiInputBgColor     = THEME_INPUT_BG;
	uiInputFgColor     = THEME_INPUT_BORDER;
	uiColorDkGrey      = THEME_DISABLED;
	uiColorBlack       = THEME_INPUT_BG;

	// UI_ApplyCustomColors only pushes the console colour when a colors.lst
	// exists, and Cry of Fear ships none, so push it here. A colors.lst can
	// still override it afterwards.
	{
		int r, g, b;

		UnpackRGB( r, g, b, THEME_TEXT );
		EngFuncs::SetConsoleDefaultColor( r, g, b );
	}
}

/*
=================
UI_ThemeApplyDeferredDefaults

The translucent pause scrim (controls/BackgroundBitmap.cpp) only replaces the
opaque background fill while the engine is still drawing the world behind the
menu, which it does only when ui_renderworld is on - and FWGS registers that
cvar with a default of "0" (engine/client/cl_main.c:92), so in the deployed
build the paused scene was black behind the scrim.

ui_renderworld is FCVAR_ARCHIVE, so it cannot simply be forced every startup:
that would ignore a user who turned it off. Instead flip it once, the first
time Cry of Fear runs a build that knows about it, and record that in an
archived marker of the menu's own. Afterwards config.cfg carries whatever the
user chose and nothing here touches it again.

This cannot live in UI_ThemeInit: UI_Init runs from SCR_Init inside CL_Init,
and Host_Init execs config.cfg afterwards, so anything set there is overwritten
by the archived value a moment later. UI_UpdateMenu's one-time block is the
first point that is safely past it.
=================
*/
void UI_ThemeApplyDeferredDefaults( void )
{
	// The marker is a generation number, not a flag. A build that adds a new
	// deferred default raises COF_SCENE_DEFAULTS_GEN, so an installation that
	// already ran an older build gets only the defaults it has never seen -
	// every choice it made about the older ones still stands.
	int generation;

	if( !g_bThemeActive || !ui_cof_scene_defaults )
		return;

	generation = (int)ui_cof_scene_defaults->value;

	if( generation >= COF_SCENE_DEFAULTS_GEN )
		return; // nothing new here; the user's choices stand

	if( generation < 5 )
	{
		// Generation 5 (quick saves): F5 = cof_quicksave, F9 = cof_quickload,
		// the default keys of the quick save (off until "Quick saves" is
		// ticked in Options > Game; gfx/shell/kb_def.lst carries the same for
		// "Use defaults"). A key is taken only when it is free - F9 unbound,
		// F5 unbound or still holding the game's shipped "snapshot" (which
		// then moves to F12, if F12 is free); a key the player bound to
		// anything else is left alone. Offered exactly once.
		const char *f5 = EngFuncs::KEY_GetBinding( K_F5 );
		const char *f9 = EngFuncs::KEY_GetBinding( K_F9 );
		const char *f12 = EngFuncs::KEY_GetBinding( K_F12 );

		if( !f9 || !f9[0] )
		{
			EngFuncs::ClientCmd( false, "bind F9 \"cof_quickload\"\n" );
			Con_Printf( "Cry of Fear: default binds updated - F9 quick load (cof_quickload)\n" );
		}
		else
			Con_Printf( "Cry of Fear: F9 is bound to \"%s\", left as it is (quick load has no key)\n", f9 );

		if( !f5 || !f5[0] )
		{
			EngFuncs::ClientCmd( false, "bind F5 \"cof_quicksave\"\n" );
			Con_Printf( "Cry of Fear: default binds updated - F5 quick save (cof_quicksave)\n" );
		}
		else if( !stricmp( f5, "snapshot" ) && ( !f12 || !f12[0] ))
		{
			EngFuncs::ClientCmd( false, "bind F5 \"cof_quicksave\"\nbind F12 \"snapshot\"\n" );
			Con_Printf( "Cry of Fear: default binds updated - F5 quick save (cof_quicksave), screenshot moved from F5 to F12\n" );
		}
		else
			Con_Printf( "Cry of Fear: F5 is bound to \"%s\", left as it is (quick save has no key)\n", f5 );
	}

	if( generation < 4 )
	{
		// Generation 4 (milestone 5b): aim down sights on the right mouse
		// button. Cry of Fear ships MOUSE2 = "+attack2" (secondary attack,
		// the melee bash) and MOUSE3 = "+attack3" (ironsights) in its
		// config.cfg; the new defaults swap them, and gfx/shell/kb_def.lst
		// (gamedata overlay) carries the same so "Use defaults" agrees.
		// Only a config whose two binds are STILL the shipped pair is
		// touched: a player who bound either button to anything else keeps
		// their choice, and the swap is offered exactly once.
		const char *m2 = EngFuncs::KEY_GetBinding( K_MOUSE2 );
		const char *m3 = EngFuncs::KEY_GetBinding( K_MOUSE3 );

		if( m2 && m3 && !stricmp( m2, "+attack2" ) && !stricmp( m3, "+attack3" ))
		{
			EngFuncs::ClientCmd( false, "bind MOUSE2 \"+attack3\"\nbind MOUSE3 \"+attack2\"\n" );
			Con_Printf( "Cry of Fear: default binds updated - MOUSE2 aims down sights (+attack3), MOUSE3 is the secondary attack (+attack2)\n" );
		}
		else
		{
			Con_Printf( "Cry of Fear: MOUSE2/MOUSE3 are not the shipped binds (\"%s\" / \"%s\"), left as they are\n",
				m2 ? m2 : "", m3 ? m3 : "" );
		}
	}

	if( generation < 3 )
	{
		// Generation 3: clear a hud_scale this build's predecessors let the
		// user set. The Video page used to offer "Auto scale HUD", and for
		// Cry of Fear that is actively harmful: the client builds its own
		// renderer's viewport and projection out of the same screen size the
		// engine reports for the HUD, so a virtual screen size zooms and
		// shifts the 3D view, in game and on the menu background map alike.
		// The checkbox is gone for this game and the engine ignores the cvar
		// for it, but an archived value from an older build is still sitting
		// in config.cfg, so clear it once. HUD scale on the Game options page
		// is the supported control now (docs/cof-ui-m4-scaling.md).
		if( EngFuncs::GetCvarFloat( "hud_scale" ) != 0.0f )
		{
			EngFuncs::CvarSetValue( "hud_scale", 0.0f );
			Con_Printf( "Cry of Fear: hud_scale cleared, it breaks this game's own 3D view. Use HUD scale in Options > Game\n" );
		}
	}

	if( generation < 2 )
	{
		// Generation 2: the engine menu's own sounds. Cry of Fear's UI sounds
		// (ui/3d_click.wav and friends, mixed for the client's own panels) are
		// noticeably louder than the game, which the user reported. The engine
		// cvar is registered by the engine and applies to the menu library's
		// sounds only (engine/client/dll_int/cl_gameui.c, pfnPlaySound); its
		// stock default is 1.0 and every other game keeps it.
		EngFuncs::CvarSetValue( "ui_sound_volume", 0.5f );
		Con_Printf( "Cry of Fear: ui_sound_volume defaulted to 0.5 (the game's own UI sounds are loud)\n" );
	}

	if( generation >= 1 )
	{
		// the generation-1 defaults below have already been offered once
		EngFuncs::CvarSetValue( "ui_cof_scene_defaults", (float)COF_SCENE_DEFAULTS_GEN );
		return;
	}

	EngFuncs::CvarSetValue( "ui_renderworld", 1.0f );

	// Detail textures: Cry of Fear has no detail textures. It ships no
	// maps/<map>_detail.txt at all, and its own renderer has a separate cvar
	// of its own (gl_detailtex, registered by client.dll) for the thing it
	// does do. The engine's r_detailtextures is FCVAR_GLCONFIG and defaults to
	// 1 (ref/gl/gl_opengl.c), and the Video page's checkbox for it is removed
	// for this game, so default the cvar off through the same one-shot marker
	// - an explicit value the user put in opengl.cfg still wins, because the
	// marker is already 1 by then.
	EngFuncs::CvarSetValue( "r_detailtextures", 0.0f );

	EngFuncs::CvarSetValue( "ui_cof_scene_defaults", (float)COF_SCENE_DEFAULTS_GEN );

	Con_Printf( "Cry of Fear: ui_renderworld defaulted to 1 so the paused scene shows through the menu scrim\n" );
	Con_Printf( "Cry of Fear: r_detailtextures defaulted to 0 (this game has no detail textures)\n" );
}

// ---------------------------------------------------------------------------
// font family
// ---------------------------------------------------------------------------

// Inter (SIL Open Font License 1.1) is shipped with the game data under
// cryoffear/gfx/fonts and is read by the stb_truetype backend through
// CFontManager::FindFontDataFile. If that file is missing or unreadable, or if
// the build has no TTF backend, CFontBuilder falls through this chain on the
// Windows GDI backend instead of dropping the whole menu to the bitmap font,
// which is a visible catastrophe rather than a graceful degradation.
//
// "Inter" is first because the static faces may also be installed on the
// system; Tahoma is present on every Windows install.
static const char *s_szThemeFonts[] =
{
	"Inter",
	"Tahoma",
	"Verdana",
	"Microsoft Sans Serif",
	"Arial",
	NULL
};

const char *UI_ThemeFontFamily( void )
{
	return s_szThemeFonts[0];
}

const char *UI_ThemeFontFallback( int i )
{
	if( i < 0 || i >= (int)( sizeof( s_szThemeFonts ) / sizeof( s_szThemeFonts[0] )))
		return NULL;
	return s_szThemeFonts[i];
}

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

int UI_ThemeHairline( void )
{
	int w = (int)( uiStatic.scaleY + 0.5f );

	return w < 1 ? 1 : w;
}

void UI_DrawHairline( Point pos, Size size, unsigned int color )
{
	UI_DrawRectangleExt( pos.x, pos.y, size.w, size.h, color, UI_ThemeHairline( ));
}

void UI_ThemeSeparator( int x, int y, int w, unsigned int color )
{
	UI_FillRect( x, y, w, UI_ThemeHairline( ), color );
}

/*
=================
UI_ThemeDeathBacking

A faint dark strip behind one line of the death page. The page draws over the
live scene with no panel and no scrim, exactly like Cry of Fear's own GAME OVER
screen, so each line needs its own backing to stay readable whatever is behind
it. Coordinates are already scaled; the padding is not.
=================
*/
void UI_ThemeDeathBacking( int x, int y, int w, int h )
{
	const int px = (int)( THEME_DEATH_PAD_X * uiStatic.scaleX );
	const int py = (int)( THEME_DEATH_PAD_Y * uiStatic.scaleY );

	UI_FillRect( x - px, y - py, w + px * 2, h + py * 2, THEME_DEATH_BACKING );
}

static bool g_bThemeSceneUnveiled = false;

void UI_ThemeSetSceneUnveiled( bool on )
{
	g_bThemeSceneUnveiled = on;
}

bool UI_ThemeSceneUnveiled( void )
{
	return g_bThemeActive && g_bThemeSceneUnveiled;
}

void UI_ThemeDrawCross( Point pos, Size size, unsigned int color )
{
	const int t = UI_ThemeHairline() * 2;
	const int n = size.w < size.h ? size.w : size.h;

	if( n <= t )
		return;

	// two diagonals of small filled squares - there is no line primitive
	for( int i = 0; i <= n - t; i++ )
	{
		UI_FillRect( pos.x + i, pos.y + i, t, t, color );
		UI_FillRect( pos.x + ( n - t - i ), pos.y + i, t, t, color );
	}
}

void UI_ThemeDrawTriangle( Point pos, Size size, int dir, unsigned int color )
{
	if( size.w <= 0 || size.h <= 0 )
		return;

	if( dir == 0 || dir == 1 ) // left / right
	{
		for( int i = 0; i < size.w; i++ )
		{
			// the apex is the narrow end: column 0 is narrow for a left arrow,
			// wide for a right arrow
			int k = ( dir == 0 ) ? ( size.w - 1 - i ) : i;
			int h = size.h - ( size.h * k ) / size.w;

			if( h < 1 ) h = 1;
			UI_FillRect( pos.x + i, pos.y + ( size.h - h ) / 2, 1, h, color );
		}
	}
	else // up / down
	{
		for( int i = 0; i < size.h; i++ )
		{
			int k = ( dir == 2 ) ? ( size.h - 1 - i ) : i;
			int w = size.w - ( size.w * k ) / size.h;

			if( w < 1 ) w = 1;
			UI_FillRect( pos.x + ( size.w - w ) / 2, pos.y + i, w, 1, color );
		}
	}
}

void UI_ThemeDrawTick( Point pos, Size size, unsigned int color )
{
	const int t = UI_ThemeHairline() * 2;
	const int n = ( size.w < size.h ? size.w : size.h );
	const int shortArm = n / 3;
	const int longArm  = n - shortArm;

	// down-right stroke of the tick
	for( int i = 0; i < shortArm; i++ )
		UI_FillRect( pos.x + i, pos.y + n / 2 + i, t, t, color );

	// up-right stroke
	for( int i = 0; i < longArm; i++ )
		UI_FillRect( pos.x + shortArm + i, pos.y + n / 2 + shortArm - i, t, t, color );
}

int UI_ThemeButtonWidth( const char *name )
{
	if( uiStatic.scaleX <= 0.0f )
		return THEME_BTN_MIN_W;

	int px = g_FontMgr->GetTextWideScaled( uiStatic.hThemeButton, name ? name : "",
		(int)( THEME_BUTTON_TALL * uiStatic.scaleY ));
	int v = (int)( px / uiStatic.scaleX ) + 28;

	return v < THEME_BTN_MIN_W ? THEME_BTN_MIN_W : v;
}

void UI_ThemeDrawDialogPanel( Point pos, Size size, const char *title )
{
	const int bandH = (int)( THEME_TITLE_BAND_H * uiStatic.scaleY );

	UI_FillRect( pos, size, THEME_PANEL );
	UI_DrawHairline( pos, size, THEME_PANEL_BORDER );

	UI_FillRect( pos.x, pos.y, size.w, bandH, THEME_TITLE_BAND );
	UI_ThemeSeparator( pos.x, pos.y + bandH, size.w, THEME_SEPARATOR );

	if( title )
	{
		const int chSize = (int)( THEME_TITLE_TALL * uiStatic.scaleY );

		UI_DrawString( uiStatic.hThemeTitle,
			Point( pos.x + (int)( THEME_PAD_X * uiStatic.scaleX ), pos.y + ( bandH - chSize ) / 2 ),
			Size( size.w - (int)( THEME_PAD_X * 2 * uiStatic.scaleX ), chSize ),
			title, THEME_TITLE_TEXT, chSize, QM_LEFT,
			ETF_FORCECOL | ETF_NO_WRAP | ETF_NOSIZELIMIT );
	}
}

// ---------------------------------------------------------------------------
// message panels
//
// The stock confirmation box is a fixed 520x200 panel and the message is drawn
// into whatever is left after the title band and the button row. A two-sentence
// warning does not fit that, so UI_DrawString ran out of lines and ellipsised
// the text into the button row. Measure the wrapped message first and size the
// panel to it instead.
// ---------------------------------------------------------------------------

int UI_ThemeWrappedLines( int font, const char *str, int charH, int widthPx )
{
	int lines = 1;
	int wide = 0;
	int wordWide = 0;   // width of the word being accumulated
	bool haveBreak = false; // a space has been seen on this line

	if( !str || !str[0] || widthPx <= 0 )
		return 0;

	for( const char *p = str; *p; p++ )
	{
		if( IsColorString( p ))
		{
			p++; // the colour code is not drawn
			continue;
		}

		if( *p == '\n' )
		{
			lines++;
			wide = wordWide = 0;
			haveBreak = false;
			continue;
		}

		int cw = g_FontMgr->GetCharacterWidthScaled( font, (unsigned char)*p, charH );

		if( *p == ' ' )
		{
			wide += wordWide + cw;
			wordWide = 0;
			haveBreak = true;
			continue;
		}

		if( wide + wordWide + cw > widthPx )
		{
			lines++;

			if( haveBreak )
			{
				// the unfinished word moves down with the new line
				wide = 0;
				haveBreak = false;
			}
			else
			{
				// one word longer than the line: hard break
				wide = wordWide = 0;
			}
		}

		wordWide += cw;
	}

	return lines;
}

void UI_ThemeMessagePanelSize( const char *str, int *w, int *h )
{
	// three measures, narrowest first: a short confirmation keeps the compact
	// Source dialog, a long warning is allowed to get wider before it gets tall
	static const int widths[] = { 520, 660, 800 };
	const int charH = (int)( THEME_BODY_TALL * uiStatic.scaleY );
	const int chrome = THEME_TITLE_BAND_H + THEME_PAD_TOP + THEME_PAD_BOTTOM
		+ THEME_BTNROW_H + THEME_BTNROW_GAP;
	const int nWidths = (int)( sizeof( widths ) / sizeof( widths[0] ));
	const int maxH = 768 - 64;
	const float sy = uiStatic.scaleY > 0.0f ? uiStatic.scaleY : 1.0f;
	int best = nWidths - 1, bestH = 200;

	for( int i = 0; i < nWidths; i++ )
	{
		int textW = (int)(( widths[i] - THEME_PAD_X * 2 ) * uiStatic.scaleX );
		int lines = UI_ThemeWrappedLines( uiStatic.hThemeBody, str, charH, textW );

		// one line of headroom once the message wraps at all: this count and
		// UI_DrawString's own word wrapping agree on ordinary text but not
		// necessarily on every colour-coded or hard-broken edge case, and being
		// one line short is what ellipsised the text in the first place
		if( lines > 1 )
			lines++;

		int total = chrome + (int)( lines * charH / sy ) + 8;

		best = i;
		bestH = total;

		// the narrowest measure that still reads as a compact dialog wins;
		// only a genuinely long message is allowed to get wider
		if( total <= 300 )
			break;
	}

	if( bestH < 200 )
		bestH = 200;
	if( bestH > maxH )
		bestH = maxH;

	if( w ) *w = widths[best];
	if( h ) *h = bestH;
}

// ---------------------------------------------------------------------------
// letter-spaced text
//
// UI_DrawString advances strictly by DrawCharacter's return value, so tracking
// is not expressible through it. The logo is the one place the look wants it.
// ---------------------------------------------------------------------------

//
// Both go through the language pack's table (UI_LangText, MenuStrings.cpp) and
// decode UTF-8 the way UI_DrawString does, so a translated title ("KONIEC
// GRY", or Cyrillic) is measured and drawn letter by letter like the English.
int UI_ThemeDrawTracked( int font, int x, int y, const char *str,
	unsigned int color, int charH, int tracking )
{
	int start = x;
	utfstate_t state;

	if( !str )
		return 0;

	str = UI_LangText( str );

	for( const char *p = str; *p; p++ )
	{
		uint32_t ch = state.Decode( (uint8_t)*p );

		if( !ch )
			continue; // inside a multi-byte sequence

		if( ch == ' ' )
		{
			x += g_FontMgr->GetCharacterWidthScaled( font, ' ', charH ) + tracking;
			continue;
		}

		x += g_FontMgr->DrawCharacter( font, ch, Point( x, y ), charH, color );
		x += tracking;
	}

	return x - start;
}

int UI_ThemeTrackedWidth( int font, const char *str, int charH, int tracking )
{
	int w = 0;
	utfstate_t state;

	if( !str )
		return 0;

	str = UI_LangText( str );

	for( const char *p = str; *p; p++ )
	{
		uint32_t ch = state.Decode( (uint8_t)*p );

		if( ch )
			w += g_FontMgr->GetCharacterWidthScaled( font, ch, charH ) + tracking;
	}

	return w > 0 ? w - tracking : 0;
}
