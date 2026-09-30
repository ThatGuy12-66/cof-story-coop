/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
------------------------------------------------------------------------------

In theme mode this page is the top-level "Game" options page, the first entry
of Options. It carries what used to be hidden behind Controls > Advanced (the
advanced control options), plus the Cry of Fear pause-menu-saves switch that
used to live in the Save/Load menu and the subtitle language selector. Without
the theme it is still the stock Advanced Controls dialog reached from Controls,
so the WON path is unchanged.

The Language selector is the one language option for Cry of Fear: English
and every pack installed under <gamedir>/languages/<code>/ that has a
manifest.txt (the Polish translation, and one minimal pack per subtitle
language the game ships: Dutch ... Swedish, which translate the menu and pick
the game's own subtitles). It sets the engine's cof_language and the client's
own subtitle language together; the client's separate "Subtitle language" row
is gone. See docs/cof-language-packs.md section 7.
*/

#include "q_client.h"
#include "Framework.h"
#include "MenuStrings.h"
#include "Bitmap.h"
#include "PicButton.h"
#include "CheckBox.h"
#include "Slider.h"
#include "SpinControl.h"
#include "Action.h"
#include "StringArrayModel.h"
#include "CryOfFear.h"
#include "CoFOptions.h"

#define ART_BANNER			"gfx/shell/head_advanced"

// the subtitle languages, wrapped as a spin-control model
class CMenuCoFLanguageModel : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return UI_CoFSubtitleLanguageCount(); }
	const char *GetText( int line ) override { return UI_CoFSubtitleLanguageName( line ); }
};

// The in-game UI scale, as a spin control over the two engine cvars that drive
// it (see engine/client/cl_main.c and docs/cof-ui-m4-scaling.md):
//
//   Auto   cof_ui_scale 0    the automatic law - the UI keeps the fraction of
//                            the screen it has at 1080p, at every resolution
//   100 %  cof_ui_scale 100  no automatic scaling, the client's native pixels
//   125,
//   150,
//   200 %  cof_ui_scale 0 with cof_ui_scale_user as the multiplier on top
//
// so all five entries mean something different. The multiplier also drives the
// engine HUD text height, so the bars, the panels and the text grow together.
static const struct { const char *label; float scale; float user; } g_cofHudScales[] =
{
	{ "Auto",  0.0f,   1.00f },
	{ "100 %", 100.0f, 1.00f },
	{ "125 %", 0.0f,   1.25f },
	{ "150 %", 0.0f,   1.50f },
	{ "200 %", 0.0f,   2.00f },
};

class CMenuCoFHudScaleModel : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return (int)( sizeof( g_cofHudScales ) / sizeof( g_cofHudScales[0] )); }
	const char *GetText( int line ) override { return g_cofHudScales[line].label; }
};

// Renderer contract: archived cof_hud_style, 0 Classic (default), 1 Remake.
// Labels are translated at display time, not stored in the cvar.
class CMenuCoFHudStyleModel : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return 2; }
	const char *GetText( int line ) override
	{
		return line == 1 ? L( "Remake" ) : L( "Classic" );
	}
};

// "Aim down sights: Hold / Toggle" (milestone 5b). Both modes are the game's
// own: hl.dll's server cvar ironsights_toggle (1 = a press toggles, the
// game's default; 0 = aim while held). The game does not save it, so the
// engine keeps the archived cof_ads_toggle (engine/client/cl_main.c) and
// copies it into ironsights_toggle whenever the game DLL loads.
static const char *g_cofAdsModes[] = { "Hold", "Toggle" };

class CMenuCoFAdsModel : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return (int)( sizeof( g_cofAdsModes ) / sizeof( g_cofAdsModes[0] )); }
	const char *GetText( int line ) override { return L( g_cofAdsModes[line] ); }
};

// The game language, one option for everything (docs/cof-language-packs.md
// section 7). Rows, in this order:
//
//   English    cof_language ""      cof_subtitlelanguage 1
//   packs      cof_language <code>  cof_subtitlelanguage <manifest subtitle_language>
//              (sorted by display name)
//
// A pack is every folder under <gamedir>/languages that holds a manifest.txt,
// found the way the engine's own cof_language_list finds them (a
// game-directory-only search of languages/*) and named by the manifest's
// display_name (UTF-8; the folder name when the key is missing). Its
// subtitle_language key is the client subtitle slot it wants (1 = English,
// the client default, when the key is missing): the engine serves the pack's
// own txtfiles/notes over whatever slot the client asks for, so 1 means that
// anything the pack does not translate falls back to English; the packs for
// the game's own six languages (languages/dutch ... swedish) name slots 2..7
// and carry only the menu's strings. A cof_language naming a pack that is not
// installed, or a client slot 2..7 with no pack (an old configuration), is
// shown as its own row, so opening the page never rewrites anything.
#define COF_LANG_MAX_ROWS	40

// cof_subtitlelanguage 2..7, in the client's own order (CryOfFear.cpp); only
// to name an old configuration's slot when no pack is selected
static const char *g_cofBuiltinLanguages[] =
{
	"Nederlands", "Fran\xC3\xA7" "ais", "Deutsch", "Norsk", "Espa\xC3\xB1" "ol", "Svenska"
};

class CMenuCoFLangPackModel : public CMenuBaseArrayModel
{
public:
	CMenuCoFLangPackModel() : m_iRows( 1 ) { memset( m_rows, 0, sizeof( m_rows )); m_rows[0].subtitle = 1; }

	void Update() override;
	int GetRows() const override { return m_iRows; }
	const char *GetText( int line ) override
	{
		if( line <= 0 || line >= m_iRows )
			return L( "English" );
		return m_rows[line].name;
	}

	const char *Code( int line ) const { return ( line <= 0 || line >= m_iRows ) ? "" : m_rows[line].code; }
	int Subtitle( int line ) const { return ( line <= 0 || line >= m_iRows ) ? 1 : m_rows[line].subtitle; }
	int Row( const char *code, int subtitle );	// adds a "not installed" row for an unknown code

private:
	struct row_t
	{
		char code[32];
		char name[96];
		int  subtitle;
	};

	bool ReadManifest( const char *code, char *name, size_t size, int *subtitle );

	int m_iRows;
	row_t m_rows[COF_LANG_MAX_ROWS];
};

bool CMenuCoFLangPackModel::ReadManifest( const char *code, char *name, size_t size, int *subtitle )
{
	char path[128];
	int length = 0;

	snprintf( path, sizeof( path ), "languages/%s/manifest.txt", code );
	byte *raw = EngFuncs::COM_LoadFile( path, &length );

	if( !raw )
		return false;

	Q_strncpy( name, code, size );
	*subtitle = 1;

	const char *p = (const char *)raw, *end = p + length;

	while( p < end )
	{
		const char *line = p, *eol;

		while( p < end && *p != '\n' )
			p++;
		eol = p;
		if( p < end )
			p++;
		if( eol > line && eol[-1] == '\r' )
			eol--;

		static const char key[] = "display_name=";
		const size_t keylen = sizeof( key ) - 1;
		static const char subkey[] = "subtitle_language=";
		const size_t subkeylen = sizeof( subkey ) - 1;

		if( (size_t)( eol - line ) > keylen && !strnicmp( line, key, keylen ))
		{
			size_t n = eol - line - keylen;

			if( n >= size )
				n = size - 1;
			memcpy( name, line + keylen, n );
			name[n] = 0;
		}
		else if( (size_t)( eol - line ) > subkeylen && !strnicmp( line, subkey, subkeylen ))
		{
			int n = atoi( line + subkeylen );

			// the client knows 1..7 (1 English, 2..7 its six folders)
			*subtitle = ( n >= 1 && n <= 7 ) ? n : 1;
		}
	}

	EngFuncs::COM_FreeFile( raw );
	return true;
}

void CMenuCoFLangPackModel::Update()
{
	int numFiles = 0;
	char **files = EngFuncs::GetFilesList( "languages/*", &numFiles, 1 );
	m_iRows = 1;

	for( int i = 0; files && i < numFiles && m_iRows < COF_LANG_MAX_ROWS - 1; i++ )
	{
		const char *code = strrchr( files[i], '/' );
		code = code ? code + 1 : files[i];

		// a pack code is a plain folder name, the rule the engine applies
		if( !code[0] || strlen( code ) >= sizeof( m_rows[0].code ) || strpbrk( code, ".:\\/" ))
			continue;

		bool dup = false;
		for( int j = 1; j < m_iRows && !dup; j++ )
			dup = !stricmp( m_rows[j].code, code );
		if( dup )
			continue;

		row_t &r = m_rows[m_iRows];
		if( !ReadManifest( code, r.name, sizeof( r.name ), &r.subtitle ))
			continue;

		Q_strncpy( r.code, code, sizeof( r.code ));
		m_iRows++;
	}

	// English stays first; the packs by display name (byte order, case
	// folded for ASCII - every name starts with a Latin letter today)
	for( int i = 2; i < m_iRows; i++ )
	{
		row_t t = m_rows[i];
		int j = i - 1;

		while( j >= 1 && stricmp( m_rows[j].name, t.name ) > 0 )
		{
			m_rows[j + 1] = m_rows[j];
			j--;
		}
		m_rows[j + 1] = t;
	}
}

int CMenuCoFLangPackModel::Row( const char *code, int subtitle )
{
	if( !code || !code[0] )
	{
		const int numBuiltin = (int)( sizeof( g_cofBuiltinLanguages ) / sizeof( g_cofBuiltinLanguages[0] ));

		if( subtitle < 2 || subtitle > numBuiltin + 1 || m_iRows >= COF_LANG_MAX_ROWS )
			return 0;

		// an old configuration: the client's own slot without a pack. Shown
		// as it is ("%s (subtitles)" is a key of the menu string table) and
		// not written back until the player picks a row.
		row_t &r = m_rows[m_iRows];
		r.code[0] = 0;
		r.subtitle = subtitle;
		snprintf( r.name, sizeof( r.name ), "%s (subtitles)", g_cofBuiltinLanguages[subtitle - 2] );
		return m_iRows++;
	}

	for( int i = 1; i < m_iRows; i++ )
	{
		if( !stricmp( m_rows[i].code, code ))
			return i;
	}

	if( m_iRows >= COF_LANG_MAX_ROWS || strlen( code ) >= sizeof( m_rows[0].code ))
		return 0;

	row_t &r = m_rows[m_iRows];
	Q_strncpy( r.code, code, sizeof( r.code ));
	r.subtitle = subtitle;
	snprintf( r.name, sizeof( r.name ), "%s (%s)", code, L( "not installed" ));
	return m_iRows++;
}

class CAdvancedControls : public CMenuFramework
{
public:
	typedef CMenuFramework BaseClass;
	CAdvancedControls() : CMenuFramework("CAdvancedControls") { m_bCoF = false; m_bHudStyleLoading = false; }

	void ToggleLookCheckboxes( bool write );
	void CoFLanguageProbe( const char *arg );
	void OptionsProbe( void );

private:
	void _Init( void ) override;
	void _VidInit( void ) override;
	void SaveAndPopMenu() override;

	void GetConfig( void );
	void PitchInvert( void );
	void MenuSavesChanged( void );
	void ConsoleEnableChanged( void );
	void LanguageChanged( void );
	void HudScaleChanged( void );
	void HudStyleChanged( void );
	void ReloadHudStyle( void );
	void AdsModeChanged( void );
	void LangPackChanged( void );
	void NotifyChanged( void );
	void PanelPauseChanged( void );
	void PanelTransparentChanged( void );
	void SubBackingChanged( void );

	CMenuPicButton done, inputDev;

	CMenuCheckBox	crosshair;
	CMenuCheckBox	invertMouse;
	CMenuCheckBox	mouseLook;
	CMenuCheckBox	lookSpring;
	CMenuCheckBox	lookStrafe;
	CMenuCheckBox	lookFilter;
	CMenuCheckBox	autoaim;
	CMenuCheckBox	rawinput;
	CMenuSlider	sensitivity;

	// "Enable console". Bound to the engine cvar con_enable, which this
	// project's engine patch adds: stock FWGS has no cvar for the console at
	// all, only the "-console"/"-dev" launch switches and the one-way,
	// restricted "ui_allowconsole" command that upstream's multiplayer-only
	// Game Options page exposes as a push button. con_enable is archived and
	// the engine mirrors it onto host.allow_console, so this is a real
	// two-way, persistent setting. See docs/cof-ui-death-flow.md.
	CMenuCheckBox	conEnable;

	// Cry of Fear game options
	CMenuCheckBox	menuSaves;
	// "Show console notifications": the engine's archived cof_notify, whether
	// the newest console lines are drawn over the top-left corner during play
	CMenuCheckBox	notifyLines;
	// "Pause on Inventory": the saved engine cvar cof_panel_pause (0/1), the
	// engine's world pause while a Cry of Fear item panel is open
	CMenuCheckBox	panelPause;
	// "Transparent UI background": the saved engine cvar cof_panel_transparent
	// (0/1, default 1), the world instead of black behind those panels
	CMenuCheckBox	panelTransparent;
	// "Quick saves": the saved engine cvar cof_quick_saves (0/1): F5 / F9,
	// the pause menu's Quick save and the Load list's quick row
	CMenuCheckBox	quickSaves;
	CMenuSpinControl	language;
	CMenuSpinControl	hudScale;
	CMenuSpinControl	hudStyle;
	CMenuCoFHudStyleModel hudStyleModel;
	bool m_bHudStyleLoading;
	CMenuSpinControl	adsMode;
	// "Subtitle background": the engine's saved cof_hud_text_backing, the
	// alpha of the dark strip under subtitles and messages (0 = none); on =
	// the engine default 120 (patches/cof-hud-text-backing.patch)
	CMenuCheckBox	subBacking;
	CMenuCoFLanguageModel	languageModel;
	CMenuCoFHudScaleModel	hudScaleModel;
	CMenuCoFAdsModel	adsModel;
	CMenuSpinControl	langPack;
	CMenuCoFLangPackModel	langPackModel;
	CMenuAction	langNote;
	bool		m_bCoF;
};

/*
=================
UI_AdvControls_GetConfig
=================
*/
void CAdvancedControls::GetConfig( )
{
	kbutton_t	*mlook;

	if( EngFuncs::GetCvarFloat( "m_pitch" ) < 0 )
		invertMouse.bChecked = true;

	mlook = (kbutton_t *)EngFuncs::KEY_GetState( "in_mlook" );
	if( mlook )
	{
		if( mlook && mlook->state & 1 )
			mouseLook.bChecked = true;
		else
			mouseLook.bChecked = false;
	}
	else
	{
		mouseLook.SetGrayed( true );
		mouseLook.bChecked = true;
	}

	crosshair.LinkCvar( "crosshair" );
	lookSpring.LinkCvar( "lookspring" );
	lookStrafe.LinkCvar( "lookstrafe" );
	lookFilter.LinkCvar( "look_filter" );

	autoaim.LinkCvar( "sv_aim" );
	rawinput.LinkCvar( "m_rawinput" );
	sensitivity.LinkCvar( "sensitivity" );

	if( UI_ThemeActive( ))
	{
		conEnable.LinkCvar( "con_enable" );
		conEnable.UpdateEditable();
	}

	if( m_bCoF )
	{
		menuSaves.LinkCvar( "cof_pause_menu_saves" );
		menuSaves.UpdateEditable();
		notifyLines.LinkCvar( "cof_notify" );
		notifyLines.UpdateEditable();
		quickSaves.LinkCvar( "cof_quick_saves" );
		quickSaves.UpdateEditable();
		// the two panel switches link only to a cvar that exists: an engine
		// without them shows their engine defaults (pause 0, transparent 1),
		// and the page's Reload cannot turn a missing cvar into a 0
		if( EngFuncs::GetCvarString( "cof_panel_pause" )[0] )
		{
			panelPause.LinkCvar( "cof_panel_pause" );
			panelPause.UpdateEditable();
		}
		else
			panelPause.bChecked = false;

		if( EngFuncs::GetCvarString( "cof_panel_transparent" )[0] )
		{
			panelTransparent.LinkCvar( "cof_panel_transparent" );
			panelTransparent.UpdateEditable();
		}
		else
			panelTransparent.bChecked = true;
		subBacking.bChecked = EngFuncs::GetCvarFloat( "cof_hud_text_backing" ) > 0.0f;
		if( UI_CoFOptionsLayout() ) ReloadHudStyle();
		// the client's own "Subtitle language" control is not on the page any
		// more (the Language row sets that slot); moving a spinner that has
		// no parent would raise its change event on no page at all

		// pick the row the two cvars currently describe; anything hand-edited
		// that matches no row shows as Auto without being written back
		{
			float scale = EngFuncs::GetCvarFloat( "cof_ui_scale" );
			float user = EngFuncs::GetCvarFloat( "cof_ui_scale_user" );
			int row = 0;

			for( int i = 0; i < hudScaleModel.GetRows( ); i++ )
			{
				if( fabs( g_cofHudScales[i].scale - scale ) < 0.5f &&
					fabs( g_cofHudScales[i].user - user ) < 0.005f )
				{
					row = i;
					break;
				}
			}

			hudScale.SetCurrentValue( (float)row );
		}

		// not on this page in the options layout (the Controls page has it); a
		// spinner without a parent must not raise its change event
		if( !UI_CoFOptionsLayout( ))
			adsMode.SetCurrentValue( EngFuncs::GetCvarFloat( "cof_ads_toggle" ) != 0.0f ? 1.0f : 0.0f );

		// the packs are listed again every time the page opens, so a pack
		// copied in while the game runs shows up without a restart
		langPackModel.Update();
		int langRow = langPackModel.Row( EngFuncs::GetCvarString( "cof_language" ), UI_CoFGetSubtitleLanguage( ));
		langPack.Setup( &langPackModel );
		langPack.SetCurrentValue( (float)langRow );
	}

	ToggleLookCheckboxes( false );
}

/*
=================
CAdvancedControls::MenuSavesChanged

Opt-in feature: make the change durable as soon as it is selected, exactly as
the Save/Load menu did before this switch moved here.
=================
*/
void CAdvancedControls::MenuSavesChanged()
{
	menuSaves.WriteCvar();
}

/*
=================
CAdvancedControls::NotifyChanged

Written at once, like the two switches above: the engine reads cof_notify
every frame (Con_DrawNotify), so the top-left lines go or come back while the
page is still open, and the archived value survives a restart.
=================
*/
void CAdvancedControls::NotifyChanged()
{
	notifyLines.WriteCvar();
}

/*
=================
CAdvancedControls::PanelPauseChanged

Written at once too. The engine owns cof_panel_pause (saved, default 0); an
engine without it gets a plain console variable, so the switch neither fails
nor pretends: the value is there for the session and is not saved.
=================
*/
void CAdvancedControls::PanelPauseChanged()
{
	if( EngFuncs::GetCvarString( "cof_panel_pause" )[0] )
		panelPause.WriteCvar();
	else
		EngFuncs::ClientCmdF( false, "set cof_panel_pause %d\n", panelPause.bChecked ? 1 : 0 );
}

// the subtitle strip: on = the engine default alpha, off = 0; saved
void CAdvancedControls::SubBackingChanged()
{
	EngFuncs::CvarSetValue( "cof_hud_text_backing", subBacking.bChecked ? 120.0f : 0.0f );
}

// the same, for cof_panel_transparent (engine and FreeVGUI side)
void CAdvancedControls::PanelTransparentChanged()
{
	if( EngFuncs::GetCvarString( "cof_panel_transparent" )[0] )
		panelTransparent.WriteCvar();
	else
		EngFuncs::ClientCmdF( false, "set cof_panel_transparent %d\n", panelTransparent.bChecked ? 1 : 0 );
}

/*
=================
CAdvancedControls::ConsoleEnableChanged

Write it immediately, like the pause-saves switch: the engine picks the new
value up on its next frame (Con_ApplyEnable, engine/client/console.c) and the
archived cvar is what makes the choice survive a restart.
=================
*/
void CAdvancedControls::ConsoleEnableChanged()
{
	conEnable.WriteCvar();
}

void CAdvancedControls::LanguageChanged()
{
	UI_CoFSetSubtitleLanguage( (int)language.GetCurrentValue() + 1 );
}

/*
=================
CAdvancedControls::HudScaleChanged

Both cvars are archived and both are live - the engine re-derives the transform
and reloads the HUD font on the next frame (CL_CoF_CheckUIScaleChanged), so
there is nothing to restart and nothing to warn about.
=================
*/
// Programmatic selection also emits QM_CHANGED, so guard reload/reset from
// rewriting saved values. Missing renderer support stays disabled/Classic.
void CAdvancedControls::ReloadHudStyle( void )
{
	const bool available = EngFuncs::GetCvarString( "cof_hud_style" )[0] != 0;
	m_bHudStyleLoading = true;
	hudStyle.SetGrayed( !available );
	hudStyle.SetCurrentValue( available && EngFuncs::GetCvarFloat( "cof_hud_style" ) == 1.0f ? 1.0f : 0.0f );
	m_bHudStyleLoading = false;
}

void CAdvancedControls::HudStyleChanged( void )
{
	if( m_bHudStyleLoading || !m_bCoF || !UI_CoFOptionsLayout() ||
		!EngFuncs::GetCvarString( "cof_hud_style" )[0] ) return;
	const float value = hudStyle.GetCurrentValue();
	if( value != 0.0f && value != 1.0f ) return;
	EngFuncs::CvarSetValue( "cof_hud_style", value );
}

void CAdvancedControls::HudScaleChanged()
{
	int row = (int)hudScale.GetCurrentValue();

	if( row < 0 || row >= hudScaleModel.GetRows( ))
		return;

	EngFuncs::CvarSetValue( "cof_ui_scale", g_cofHudScales[row].scale );
	EngFuncs::CvarSetValue( "cof_ui_scale_user", g_cofHudScales[row].user );
}

/*
=================
CAdvancedControls::AdsModeChanged

Live and archived, like the HUD scale: the engine copies cof_ads_toggle into
the game's own ironsights_toggle on its next frame (CL_CoF_ADSSync), and the
weapons read that on the next press.
=================
*/
void CAdvancedControls::AdsModeChanged()
{
	EngFuncs::CvarSetValue( "cof_ads_toggle", adsMode.GetCurrentValue() >= 1.0f ? 1.0f : 0.0f );
}

/*
=================
CAdvancedControls::LangPackChanged

One option for both language settings. cof_language is archived and live:
the engine applies a change on its next frame (CoF_Lang_Frame) - the string
table, the code page and every text file the game opens from then on switch
at once, and so do this menu's own strings (menu-strings.tsv, MenuStrings.cpp);
repainted world textures and entity strings follow on the next map load, and
the client's VGUI art, which it loads once per session, after a restart (the
note under the row). The client's own subtitle language (cof_subtitlelanguage,
read by the game DLLs at every file open) is set to the row's slot: the pack's
subtitle_language, 1 for English, 2..7 for the game's own languages. Inside a
running game the client is also told through its own "subtitleset" command,
so the player's cached copy follows. The configuration is written straight
away so the choice survives even a session that does not end with a clean
quit. Opening the page selects the current row without writing anything.
=================
*/
void CAdvancedControls::LangPackChanged()
{
	const int row = (int)langPack.GetCurrentValue( );
	const char *code = langPackModel.Code( row );
	const int subtitle = langPackModel.Subtitle( row );
	bool changed = false;

	if( stricmp( code, EngFuncs::GetCvarString( "cof_language" )))
	{
		EngFuncs::CvarSetString( "cof_language", code );
		changed = true;
	}

	const bool coopCopy = EngFuncs::GetCvarString( "cl_coop_language" )[0] != 0;

	if( subtitle != (int)EngFuncs::GetCvarFloat( "cof_subtitlelanguage" ) ||
		( coopCopy && subtitle != (int)EngFuncs::GetCvarFloat( "cl_coop_language" )))
	{
		// "cmd subtitleset N" in a running game, the cvar otherwise
		UI_CoFSetSubtitleLanguage( subtitle );

		// the cvar itself at once too, so the config written below has it;
		// the game creates it on its first subtitleset, so make sure it exists
		if( EngFuncs::GetCvarString( "cof_subtitlelanguage" )[0] )
			EngFuncs::CvarSetValue( "cof_subtitlelanguage", (float)subtitle );
		else
			EngFuncs::ClientCmdF( false, "set cof_subtitlelanguage %d\n", subtitle );

		// the client's co-op intro card reads its own copy of the slot
		if( coopCopy )
			EngFuncs::CvarSetValue( "cl_coop_language", (float)subtitle );

		changed = true;
	}

	if( changed )
		EngFuncs::ClientCmd( FALSE, "host_writeconfig\n" );
}

void CAdvancedControls::PitchInvert()
{
	bool invert = invertMouse.bChecked;
	float m_pitch = EngFuncs::GetCvarFloat( "m_pitch" );
	if( ( invert && (m_pitch > 0) ) ||
		( !invert && (m_pitch < 0) ) )
	{
		EngFuncs::CvarSetValue( "m_pitch", -m_pitch );
	}
}

void CAdvancedControls::ToggleLookCheckboxes( bool write )
{
	lookSpring.SetGrayed( mouseLook.bChecked );
	lookStrafe.SetGrayed( mouseLook.bChecked );

	if( write )
	{
		if( mouseLook.bChecked )
			EngFuncs::ClientCmd( false, "+mlook\nbind _force_write\n" );
		else
			EngFuncs::ClientCmd( false, "-mlook\nbind _force_write\n" );
	}
}

void CAdvancedControls::SaveAndPopMenu()
{
	if( UI_CoFOptionsLayout( ))
	{
		crosshair.WriteCvar();
		lookSpring.WriteCvar();
		conEnable.WriteCvar();
		CMenuFramework::SaveAndPopMenu();
		return;
	}

	crosshair.WriteCvar();
	lookSpring.WriteCvar();
	lookStrafe.WriteCvar();
	lookFilter.WriteCvar();
	if( EngFuncs::GetCvarString("m_filter")[0] )
		EngFuncs::CvarSetValue( "m_filter", lookFilter.bChecked );
	autoaim.WriteCvar();
	rawinput.WriteCvar();
	sensitivity.WriteCvar();
	if( UI_ThemeActive( ))
		conEnable.WriteCvar();

	ToggleLookCheckboxes( true );

	CMenuFramework::SaveAndPopMenu();
}

/*
=================
UI_AdvControls_Init
=================
*/
void CAdvancedControls::_Init( void )
{
	banner.SetPicture( ART_BANNER );

	m_bCoF = UI_ThemeActive() && UI_IsCryOfFear();

	// the Cry of Fear options layout moves the mouse and aim rows to the
	// Controls page, so the page is shorter (CoFOptions.cpp)
	if( UI_ThemeActive( ))
		SetPanel( "GAME", 760, m_bCoF ? ( UI_CoFOptionsLayout( ) ? 442 : 580 + THEME_ROW_PITCH ) : 520 );	// + the notifications row

	done.szName = L( "Done" );
	done.SetPicture( PC_DONE );
	done.onReleased = VoidCb( &CAdvancedControls::SaveAndPopMenu );
	done.SetCoord( 72, 710 );

	crosshair.SetNameAndStatus( L( "Crosshair" ), L( "Enable the weapon aiming crosshair" ) );
	crosshair.iFlags |= QMF_NOTIFY;
	crosshair.SetCoord( 72, 260 );

	invertMouse.SetNameAndStatus( L( "GameUI_ReverseMouse" ), L( "GameUI_ReverseMouseLabel" ) );
	invertMouse.iFlags |= QMF_NOTIFY;
	invertMouse.onChanged = VoidCb( &CAdvancedControls::PitchInvert );
	invertMouse.SetCoord( 72, 310 );

	mouseLook.SetNameAndStatus( L( "GameUI_MouseLook" ), L( "GameUI_MouseLookLabel" ) );
	mouseLook.iFlags |= QMF_NOTIFY;
	SET_EVENT( mouseLook.onChanged,
		((CAdvancedControls*)pSelf->Parent())->ToggleLookCheckboxes( true ) );
	mouseLook.SetCoord( 72, 360 );

	lookSpring.SetNameAndStatus( L( "Look spring" ), L( "Causes the screen to 'spring' back to looking straight ahead when you move forward" ) );
	lookSpring.iFlags |= QMF_NOTIFY;
	lookSpring.SetCoord( 72, 410 );

	lookStrafe.SetNameAndStatus( L( "Look strafe" ), L( "In combination with your mouse look modifier, causes left-right movements to strafe instead of turn" ) );
	lookStrafe.iFlags |= QMF_NOTIFY;
	lookStrafe.SetCoord( 72, 460 );

	lookFilter.SetNameAndStatus( L( "GameUI_MouseFilter" ), L( "GameUI_MouseFilterLabel" ) );
	lookFilter.iFlags |= QMF_NOTIFY;
	lookFilter.SetCoord( 72, 510 );

	autoaim.SetNameAndStatus( L( "GameUI_AutoAim" ), L( "GameUI_AutoaimLabel" ) );
	autoaim.iFlags |= QMF_NOTIFY;
	autoaim.SetCoord( 72, 560 );
	
	rawinput.SetNameAndStatus( L( "GameUI_RawInput" ), L( "GameUI_RawInputLabel" ) );
	rawinput.iFlags |= QMF_NOTIFY;
	rawinput.SetCoord( 72, 610 );

	sensitivity.szName = L( "GameUI_MouseSensitivity" );
	sensitivity.Setup( 0.0, 20.0f, 0.1 );
	sensitivity.SetCoord( 72, 690 );

	inputDev.SetNameAndStatus( L( "Input devices" ), L( "Toggle mouse, touch controls" ) );
	inputDev.onReleased = UI_InputDevices_Menu;
	inputDev.iFlags |= QMF_NOTIFY;
	if( CL_IsActive() && !EngFuncs::GetCvarFloat( "host_serverstate" ))
		inputDev.SetGrayed( true );
	inputDev.SetCoord( 72, 210 );

	conEnable.SetNameAndStatus( L( "Enable console" ),
		L( "Allow the developer console without the -console or -dev command line switch" ));
	conEnable.iFlags |= QMF_NOTIFY;
	conEnable.onChanged = VoidCb( &CAdvancedControls::ConsoleEnableChanged );

	menuSaves.SetNameAndStatus( L( "Pause menu saves" ),
		L( "Allow saving from the pause menu into the five shared Cry of Fear slots" ));
	menuSaves.iFlags |= QMF_NOTIFY;
	menuSaves.onChanged = VoidCb( &CAdvancedControls::MenuSavesChanged );

	notifyLines.SetNameAndStatus( L( "Show console notifications" ),
		L( "Show the newest console lines in the top-left corner during play" ));
	notifyLines.iFlags |= QMF_NOTIFY;
	notifyLines.onChanged = VoidCb( &CAdvancedControls::NotifyChanged );

	subBacking.SetNameAndStatus( L( "Subtitle background" ),
		L( "A dark strip under the subtitles and game messages, so they stay readable" ));
	subBacking.iFlags |= QMF_NOTIFY;
	subBacking.onChanged = VoidCb( &CAdvancedControls::SubBackingChanged );

	panelPause.SetNameAndStatus( L( "Pause on Inventory" ),
		L( "Pause the game while the inventory, notes and other item pages are open" ));
	panelPause.iFlags |= QMF_NOTIFY;
	panelPause.onChanged = VoidCb( &CAdvancedControls::PanelPauseChanged );

	panelTransparent.SetNameAndStatus( L( "Transparent UI background" ),
		L( "Show the game world behind the inventory, notes and the tape recorder page instead of black" ));
	panelTransparent.iFlags |= QMF_NOTIFY;
	panelTransparent.onChanged = VoidCb( &CAdvancedControls::PanelTransparentChanged );

	// written at once, like Pause menu saves: the engine reads it on every
	// cof_quicksave / cof_quickload, the pause list lays itself out again
	quickSaves.SetNameAndStatus( L( "Quick saves" ),
		L( "F5 quick save, F9 quick load; Quick save also heads the pause menu and the Load list" ));
	quickSaves.iFlags |= QMF_NOTIFY;
	SET_EVENT( quickSaves.onChanged, ((CMenuCheckBox *)pSelf)->WriteCvar( ));

	language.szName = L( "Subtitle language" );
	language.Setup( &languageModel );
	language.SetCharSize( QM_SMALLFONT );
	language.iFlags |= QMF_NOTIFY;
	language.szStatusText = L( "Language of the subtitles and the in-game documents" );
	language.onChanged = VoidCb( &CAdvancedControls::LanguageChanged );

	hudStyle.szName = L( "HUD" );
	hudStyle.Setup( &hudStyleModel );
	hudStyle.SetCharSize( QM_SMALLFONT );
	hudStyle.iFlags |= QMF_NOTIFY;
	hudStyle.onChanged = VoidCb( &CAdvancedControls::HudStyleChanged );

	hudScale.szName = L( "HUD scale" );
	hudScale.Setup( &hudScaleModel );
	hudScale.SetCharSize( QM_SMALLFONT );
	hudScale.iFlags |= QMF_NOTIFY;
	hudScale.szStatusText = L( "Size of the in-game HUD, inventory and subtitles. Auto keeps them the same fraction of the screen at every resolution" );
	hudScale.onChanged = VoidCb( &CAdvancedControls::HudScaleChanged );

	adsMode.szName = L( "Aim down sights" );
	adsMode.Setup( &adsModel );
	adsMode.SetCharSize( QM_SMALLFONT );
	adsMode.iFlags |= QMF_NOTIFY;
	adsMode.szStatusText = L( "Hold: aim while the aim key is held. Toggle: press once to aim, press again to stop" );
	adsMode.onChanged = VoidCb( &CAdvancedControls::AdsModeChanged );

	langPack.szName = L( "Language" );
	langPack.Setup( &langPackModel );
	langPack.SetCharSize( QM_SMALLFONT );
	langPack.iFlags |= QMF_NOTIFY;
	langPack.szStatusText = L( "Language of the menus and of the game's subtitles, documents and signs" );
	langPack.onChanged = VoidCb( &CAdvancedControls::LangPackChanged );

	langNote.iFlags = QMF_INACTIVE;
	langNote.colorBase = uiColorHelp;
	langNote.SetCharSize( QM_SMALLFONT );
	langNote.szName = L( "Text switches at once, signs on the next map; restart the game for the remaining art." );

	if( !UI_ThemeActive( ))
		AddItem( banner );
	if( !UI_CoFOptionsLayout( ))
		AddItem( done );
	AddItem( inputDev );
	AddItem( crosshair );
	if( !UI_CoFOptionsLayout( ))
	{
		AddItem( invertMouse );
		AddItem( mouseLook );
	}
	AddItem( lookSpring );
	if( !UI_CoFOptionsLayout( ))
	{
		AddItem( lookStrafe );
		AddItem( lookFilter );
		AddItem( autoaim );
		AddItem( rawinput );
		AddItem( sensitivity );
	}

	if( UI_ThemeActive( ))
		AddItem( conEnable );

	if( m_bCoF )
	{
		// the client's own "Subtitle language" row ("language") is not added:
		// the Language row sets that slot too, one option for both
		AddItem( menuSaves );
		AddItem( notifyLines );
		if( UI_CoFOptionsLayout() ) AddItem( hudStyle );
		AddItem( hudScale );
		// Aim down sights moved to the Controls page with the options layout
		if( UI_CoFOptionsLayout( ))
		{
			AddItem( panelPause );
			AddItem( panelTransparent );
			AddItem( quickSaves );
		}
		else
			AddItem( adsMode );
		if( UI_CoFOptionsLayout( ))
			AddItem( subBacking );	// Game tab: placed by _VidInit after the other boxes
		AddItem( langPack );
		AddItem( langNote );
	}

	// options layout: Close is the last stop of the pad/arrow-key order
	if( UI_CoFOptionsLayout( ))
		AddItem( done );
}


void CAdvancedControls::_VidInit()
{
	if( IsPanel( ) && m_bCoF && UI_CoFOptionsLayout( ))
	{
		// Cry of Fear options layout: the game switches only. Two checkbox
		// columns, the Input devices row, then Language / HUD scale and the
		// language note, as before.
		Rect left = ColumnRect( 0, 2 );
		Rect right = ColumnRect( 1, 2 );
		CMenuCheckBox *boxes[] = { &crosshair, &lookSpring, &conEnable, &menuSaves, &notifyLines, &panelPause, &panelTransparent, &quickSaves };
		const int numBoxes = (int)V_ARRAYSIZE( boxes );
		int y = left.pt.y;

		for( int i = 0; i < numBoxes; i++ )
		{
			boxes[i]->SetCoord(( i & 1 ) ? right.pt.x : left.pt.x, y );
			if(( i & 1 ) || i == numBoxes - 1 )
				y += THEME_ROW_PITCH;
		}

		// Subtitle background (menufix round): the next free place in the
		// two columns, whatever the list above holds
		if( numBoxes & 1 )
			subBacking.SetCoord( right.pt.x, y - THEME_ROW_PITCH );
		else
		{
			subBacking.SetCoord( left.pt.x, y );
			y += THEME_ROW_PITCH;
		}

		y += 6;
		inputDev.SetRect( left.pt.x, y, left.sz.w, THEME_ROW_H );
		inputDev.eThemeStyle = CMenuPicButton::THEME_ROW;
		inputDev.eTextAlignment = QM_LEFT;

		// the spin controls draw their caption above the box
		y += THEME_ROW_H + THEME_LABEL_TALL + 24;
		langPack.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
		// Style and scale share the old HUD-scale row; no new vertical crowding.
		const int hudGap = 16;
		const int hudHalf = ( right.sz.w - hudGap ) / 2;
		hudStyle.SetRect( right.pt.x, y, hudHalf, THEME_CTRL_H );
		hudScale.SetRect( right.pt.x + hudHalf + hudGap, y, right.sz.w - hudHalf - hudGap, THEME_CTRL_H );

		y += THEME_CTRL_H + 14;
		Rect c = ContentRect();
		langNote.SetRect( c.pt.x, y, c.sz.w, 2 * THEME_LABEL_TALL + 8 );

		done.szName = L( "Close" );
		CMenuPicButton *btns[] = { &done };
		LayoutButtonRow( btns, 1 );

		GetConfig();
		return;
	}

	if( IsPanel( ))
	{
		Rect left = ColumnRect( 0, 2 );
		Rect right = ColumnRect( 1, 2 );
		CMenuCheckBox *col1[] = { &crosshair, &invertMouse, &mouseLook, &lookSpring };
		CMenuCheckBox *col2[] = { &lookStrafe, &lookFilter, &autoaim, &rawinput };
		int y = left.pt.y;

		for( int i = 0; i < 4; i++ )
		{
			col1[i]->SetCoord( left.pt.x, y );
			col2[i]->SetCoord( right.pt.x, y );
			y += THEME_ROW_PITCH;
		}

		// the extra game switches keep the two checkbox columns going
		CMenuCheckBox *extra[3];
		int n = 0;

		extra[n++] = &conEnable;
		if( m_bCoF )
		{
			extra[n++] = &menuSaves;
			extra[n++] = &notifyLines;
		}

		for( int i = 0; i < n; i++ )
		{
			extra[i]->SetCoord(( i & 1 ) ? right.pt.x : left.pt.x, y );
			if( i & 1 )
				y += THEME_ROW_PITCH;
		}

		if( n & 1 )
			y += THEME_ROW_PITCH;

		// the slider draws its own caption 1.5 char heights above its track, so
		// the next row has to clear the checkbox block by more than a label
		y += THEME_LABEL_TALL + 40;
		sensitivity.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );
		inputDev.SetRect( right.pt.x, y, right.sz.w, THEME_ROW_H );
		inputDev.eThemeStyle = CMenuPicButton::THEME_ROW;
		inputDev.eTextAlignment = QM_LEFT;

		if( m_bCoF )
		{
			y += THEME_CTRL_PITCH;
			langPack.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
			hudScale.SetRect( right.pt.x, y, right.sz.w, THEME_CTRL_H );

			y += THEME_CTRL_PITCH;
			adsMode.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );

			// under the rows, the full content width, room for two lines
			// (a translation may be longer than the English)
			y += THEME_CTRL_H + 14;
			Rect c = ContentRect();
			langNote.SetRect( c.pt.x, y, c.sz.w, 2 * THEME_LABEL_TALL + 8 );
		}

		done.szName = L( "Close" );
		CMenuPicButton *btns[] = { &done };
		LayoutButtonRow( btns, 1 );
	}

	GetConfig();
}

ADD_MENU( menu_advcontrols, CAdvancedControls, UI_AdvControls_Menu );

/*
=================
menu_cof_language_select [row | code | english]

This project's tests may not inject a click or a key, so this cfg-only hook
opens the Game page and moves its Language spinner exactly the way an arrow
click does: SetCurrentValue on the control, which redraws it and raises the
same QM_CHANGED event, so LangPackChanged runs unchanged. Without an argument
it only lists the rows. Cry of Fear with the theme only.
=================
*/
void CAdvancedControls::CoFLanguageProbe( const char *arg )
{
	if( !m_bCoF )
	{
		Con_Printf( "menu_cof_language_select: Cry of Fear with ui_theme 1 only\n" );
		return;
	}

	for( int i = 0; i < langPackModel.GetRows( ); i++ )
		Con_Printf( "menu_cof_language_select: row %d \"%s\" = cof_language \"%s\", cof_subtitlelanguage %d%s\n", i,
			langPackModel.GetText( i ), langPackModel.Code( i ), langPackModel.Subtitle( i ),
			i == (int)langPack.GetCurrentValue( ) ? " (selected)" : "" );

	if( !arg || !arg[0] )
		return;

	int row = !stricmp( arg, "english" ) ? 0 : -1;

	for( int i = 1; i < langPackModel.GetRows( ) && row < 0; i++ )
	{
		if( langPackModel.Code( i )[0] && !stricmp( arg, langPackModel.Code( i )))
			row = i;
	}

	if( row < 0 && arg[0] >= '0' && arg[0] <= '9' )
		row = atoi( arg );

	if( row < 0 || row >= langPackModel.GetRows( ))
	{
		Con_Printf( "menu_cof_language_select: no row \"%s\"\n", arg );
		return;
	}

	langPack.SetCurrentValue( (float)row );
	Con_Printf( "menu_cof_language_select: row %d, cof_language is now \"%s\", cof_subtitlelanguage \"%s\"\n",
		row, EngFuncs::GetCvarString( "cof_language" ), EngFuncs::GetCvarString( "cof_subtitlelanguage" ));
}

/*
=================
CAdvancedControls::OptionsProbe

menu_cof_options_select game [<item> <value>] (menus/CoFOptions.cpp): lists
the page's switches with their cvars, or sets one the way a click does -
the control's value, then the same QM_CHANGED event - and "close" presses
Close (SaveAndPopMenu).
=================
*/
void CAdvancedControls::OptionsProbe( void )
{
	char item[64], value[64];
	Q_strncpy( item, EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "", sizeof( item ));
	Q_strncpy( value, EngFuncs::CmdArgc() > 3 ? EngFuncs::CmdArgv( 3 ) : "", sizeof( value ));
	struct { const char *name; CMenuCheckBox *box; const char *cvar; } boxes[] =
	{
		{ "crosshair", &crosshair, "crosshair" },
		{ "subbacking", &subBacking, "cof_hud_text_backing" },
		{ "lookspring", &lookSpring, "lookspring" },
		{ "console", &conEnable, "con_enable" },
		{ "pausesaves", &menuSaves, "cof_pause_menu_saves" },
		{ "notify", &notifyLines, "cof_notify" },
		{ "panelpause", &panelPause, "cof_panel_pause" },
		{ "transparent", &panelTransparent, "cof_panel_transparent" },
		{ "quicksaves", &quickSaves, "cof_quick_saves" },
	};

	if( !stricmp( item, "close" ))
	{
		Con_Printf( "menu_cof_options_select game: Close\n" );
		SaveAndPopMenu();
		return;
	}

	if( !stricmp( item, "hudstyle" ))
	{
		if( !EngFuncs::GetCvarString( "cof_hud_style" )[0] )
			ReloadHudStyle();
		else if( !strcmp( value, "0" ) || !strcmp( value, "1" ))
		{
			const float requested = (float)atoi( value );
			if( hudStyle.GetCurrentValue() == requested ) HudStyleChanged();
			else hudStyle.SetCurrentValue( requested );
		}
		else if( value[0] )
			Con_Printf( "menu_cof_options_select game: hudstyle expects 0 or 1\n" );
		Con_Printf( "menu_cof_options_select game: HUD %s, cof_hud_style %s\n",
			hudStyle.GetCurrentValue() == 1.0f ? "Remake" : "Classic", EngFuncs::GetCvarString( "cof_hud_style" ));
		return;
	}

	if( !stricmp( item, "hudscale" ) && value[0] )
	{
		hudScale.SetCurrentValue( (float)atoi( value ));
		Con_Printf( "menu_cof_options_select game: hudscale row %d, cof_ui_scale %s, cof_ui_scale_user %s\n",
			atoi( value ), EngFuncs::GetCvarString( "cof_ui_scale" ), EngFuncs::GetCvarString( "cof_ui_scale_user" ));
		return;
	}

	for( size_t i = 0; i < V_ARRAYSIZE( boxes ); i++ )
	{
		if( item[0] && stricmp( item, boxes[i].name ))
			continue;

		if( item[0] && value[0] )
			UI_CoFProbeCheck( *boxes[i].box, value );

		Con_Printf( "menu_cof_options_select game: %s = %d (%s \"%s\")%s\n", boxes[i].name, boxes[i].box->bChecked ? 1 : 0,
			boxes[i].cvar, EngFuncs::GetCvarString( boxes[i].cvar ), FBitSet( boxes[i].box->iFlags, QMF_GRAYED ) ? " grayed" : "" );
	}
}

void UI_AdvControls_OptionsProbe( void )
{
	// the page on top keeps its unsaved state; one lower in the stack is
	// brought back to the top
	if( menu_advcontrols->WindowStack()->Current() != menu_advcontrols )
		UI_AdvControls_Menu();
	menu_advcontrols->OptionsProbe();
}

static void UI_CoFLanguageSelect( void )
{
	UI_AdvControls_Menu();
	menu_advcontrols->CoFLanguageProbe( EngFuncs::CmdArgc() > 1 ? EngFuncs::CmdArgv( 1 ) : NULL );
}

ADD_COMMAND( menu_cof_language_select, UI_CoFLanguageSelect );
