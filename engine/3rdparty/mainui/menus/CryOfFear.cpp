/*
CryOfFear.cpp -- Cry of Fear specific menu pages

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

These pages replace the Cry of Fear client's own VGUI panels with engine menus.
Every one of them emits exactly the console command the original panel emitted,
so the game DLL (cl_dlls/hl.dll) keeps doing the work:

  difficulty      cmd skillset 1..4        (Easy / Medium / Difficult / Nightmare)
  custom campaign cmd campaign <firstmap>  sent before the skillset, exactly as
                                           the original CCampaignSlider does
  language        cmd subtitleset 1..7     (English + the six txtfiles/languages)
  unlockables     (no command)             an engine-menu page that reads the
                                           real state out of
                                           cryoffear/scriptsettings.dat;
                                           "Open original gallery" still runs
                                           the client's own unlockablescmd,
                                           which loads c_unlockables
  extras          ShellExecute(<url>)      the original used the Steam overlay,
                                           which never appears under FWGS

Nothing here is reachable unless the running game directory is "cryoffear".
*/

#include "Framework.h"
#include "Action.h"
#include "Bitmap.h"
#include "CheckBox.h"
#include "PicButton.h"
#include "Table.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "MenuStrings.h"
#include "Utils.h"
#include "utlvector.h"
#include "CryOfFear.h"
#include "FontManager.h"

// ---------------------------------------------------------------------------
// game detection
// ---------------------------------------------------------------------------

bool UI_IsCryOfFear( void )
{
	char gameDir[256];
	char *base;

	EngFuncs::GetGameDir( gameDir );
	base = strrchr( gameDir, '/' );
	if( !base ) base = strrchr( gameDir, '\\' );
	return !stricmp( base ? base + 1 : gameDir, "cryoffear" ) ? true : false;
}

/*
=================
UI_CoFMapNameIsSane

Every map name this file can put on the command line comes from a data file
(maps/*.custom), so treat it as untrusted. An empty or malformed name would
reach the engine's map/campaign handling and, once a level load is aborted,
CL_LevelShot_f dereferences cl.worldmodel unconditionally
(engine/client/cl_cmds.c:322) and takes the process down. Refuse here instead.
=================
*/
static bool UI_CoFMapNameIsSane( const char *name )
{
	if( !name || !name[0] )
		return false;

	for( const char *p = name; *p; p++ )
	{
		if( *p >= 'a' && *p <= 'z' ) continue;
		if( *p >= 'A' && *p <= 'Z' ) continue;
		if( *p >= '0' && *p <= '9' ) continue;
		if( *p == '_' || *p == '-' || *p == '.' ) continue;
		return false;
	}
	return strstr( name, ".." ) == NULL;
}

// ---------------------------------------------------------------------------
// difficulty
// ---------------------------------------------------------------------------

#define COF_NUM_SKILLSETS 4

/*
=================
Skip prologue

c_intro is a ~3-minute non-interactive title, credits and car-crash sequence.
Measured from the entity lumps of the shipped maps, read-only:

* c_intro has exactly one trigger_changelevel, "*141", "map" "c_nightmare",
  landmark "landakukej" (info_landmark at 212 -408 -2523); the player is
  teleported into it by cof_teleport "kunitei2" at t+133, itself fired through
  multiintroduct -> multikattej;
* every one of c_intro's ten trigger_camera entities carries spawnflags 116,
  which includes SF_CAMERA_PLAYER_TAKECONTROL, so the player never has control
  on that map at all;
* c_intro grants and sets NOTHING: no game_player_equip, no weapon_*, no
  item_*, no cof_giveitems, no cof_clearitems, no env_global, no cof_chapter,
  no cof_begingame, no trigger_auto. Its only effect outside itself is the
  level change;
* c_nightmare's worldspawn has "newunit" "1", so the engine wipes the save
  transition directory on entry - it is a clean unit boundary by design - and
  its first info_player_start (211 -412 -2545) is exactly the landmark arrival
  point, inside the trigger_once that starts its own opening sequence;
* c_nightmare carries the campaign's only cof_begingame ("begin", fired at t=0
  of its nightmulti), and it grants weapon_camera through cof_giveitems
  "kakkaej". Starting one map later, at c_start, would skip both.

So c_nightmare is the first map of the game proper, and arriving there directly
is indistinguishable from arriving there through the prologue.

Difficulty survives the map change by itself: "difficulty" is an engine cvar
registered by the game DLL with FCVAR_ARCHIVE|FCVAR_SERVER (hl.dll cvar_t at
10214750, default "2"), skillset writes it synchronously with pfnCVarSetFloat
(VA 10019E09 / 10019E47), and RefreshSkillData (VA 100AF810) re-reads it from
the cvar on every level load through CWorld::Precache -> InstallGameRules. The
skillset handler also arms a five-second player think (VA 100DFC10) that issues
"map <campaign>" or, with no campaign set, "map c_intro" - which is why the
skip is spelled as the game's own "campaign" override rather than as our own
"map" command on top of it: otherwise that think would load c_intro five
seconds after we loaded c_nightmare.
=================
*/
#define COF_FIRST_MAP          "c_intro"
#define COF_SKIP_PROLOGUE_MAP  "c_nightmare"
#define COF_SKIP_PROLOGUE_CVAR "cof_skip_prologue"

// The four rows of gfx/vgui/640_difficulty.tga, in the order the original
// panel draws them. The fourth is replaced by "LOCKED" in
// gfx/vgui/640_difficultylocked.tga until the Nightmare mode unlockable is
// earned. That flag is line 83 of cryoffear/scriptsettings.dat and is read
// here through UI_CoFNightmareStatus (see the unlockables section below); the
// milestone-2 stand-in cvar cof_nightmare_unlocked is gone.
static const char *s_cofSkillName[COF_NUM_SKILLSETS] =
{
	"Easy", "Medium", "Difficult", "Nightmare"
};

static const char *s_cofSkillStatus[COF_NUM_SKILLSETS] =
{
	"Start a new game on Easy",
	"Start a new game on Medium",
	"Start a new game on Difficult",
	"Start a new game on Nightmare"
};

static const EDefaultBtns s_cofSkillPic[COF_NUM_SKILLSETS] =
{
	PC_EASY, PC_MEDIUM, PC_DIFFICULT, PC_DIFFICULT
};

class CMenuCoFDifficulty : public CMenuFramework
{
public:
	CMenuCoFDifficulty() : CMenuFramework( "CMenuCoFDifficulty" )
	{
		memset( skillBtn, 0, sizeof( skillBtn ));
		m_iSelected = 0;
		m_szCampaign[0] = m_szTitle[0] = m_szLabel[0] = m_szCommentaryLabel[0] = 0;
	}

	void SetCampaign( const char *firstmap, const char *title );
	void Show() override;

private:
	void _Init() override;
	void _VidInit() override;

	static void StartGameCb( CMenuBaseItem *pSelf, void *pExtra );
	void StartGame( int skillset );
	void DevCommentaryChanged();
	void CommentaryLabel();
	void Refresh();

	CEventCallback  skillCallback[COF_NUM_SKILLSETS];
	CMenuPicButton *skillBtn[COF_NUM_SKILLSETS];
	CMenuPicButton  skillRow[COF_NUM_SKILLSETS];
	CMenuAction     campaignLabel;
	CMenuAction     lockedNote;
	CMenuCheckBox   devCommentary;
	CMenuCheckBox   skipPrologue;

	int  m_iSelected;
	void SelectRow( int index );
	void StartSelected();
	static void SelectRowCb( CMenuBaseItem *pSelf, void *pExtra );

	char m_szCampaign[64];
	char m_szTitle[128];
	char m_szLabel[224];
	char m_szCommentaryLabel[CS_SIZE];
};

void CMenuCoFDifficulty::SelectRow( int index )
{
	if( index < 0 || index >= COF_NUM_SKILLSETS )
		return;

	m_iSelected = index;

	for( int i = 0; i < COF_NUM_SKILLSETS; i++ )
		skillRow[i].bThemeSelected = ( i == index );
}

void CMenuCoFDifficulty::SelectRowCb( CMenuBaseItem *pSelf, void *pExtra )
{
	CMenuCoFDifficulty *ui = (CMenuCoFDifficulty *)pSelf->Parent();

	ui->SelectRow( (int)(size_t)pExtra - 1 );
}

void CMenuCoFDifficulty::StartSelected()
{
	StartGame( m_iSelected + 1 );
}

void CMenuCoFDifficulty::SetCampaign( const char *firstmap, const char *title )
{
	if( firstmap && firstmap[0] )
	{
		Q_strncpy( m_szCampaign, firstmap, sizeof( m_szCampaign ));
		Q_strncpy( m_szTitle, title ? title : firstmap, sizeof( m_szTitle ));
	}
	else
	{
		m_szCampaign[0] = 0;
		m_szTitle[0] = 0;
	}
}

/*
=================
CMenuCoFDifficulty::StartGame

The original client panel does, in this order and only in this order:

    if( a custom campaign is selected ) ServerCmd( "campaign <firstmap>" );
    ServerCmd( "skillset N" );

ServerCmd is "cmd <text>" from a console point of view. Because the engine menu
is drawn over a live background map (scripts/chapterbackgrounds.txt), there is
a real local server to forward them to, so the game DLL runs its own handler:
it sets the "difficulty" cvar, plays inventory/game_start.wav, fades to white
for five seconds and only then changes level - the authentic sequence.

Without a server (for example right after Quit to menu, before the background
map is back) there is nothing to forward to, so set the cvar the handler would
have set and change level here instead.
=================
*/
void CMenuCoFDifficulty::StartGame( int skillset )
{
	if( skillset < 1 || skillset > COF_NUM_SKILLSETS )
		return;

	// Nightmare needs the real flag, not a menu guess: unknown is not unlocked
	if( skillset == COF_NUM_SKILLSETS && UI_CoFNightmareStatus() != 1 )
		return;

	// Never emit a level change with an empty map name: the engine's own
	// levelshot handler dereferences the world model unconditionally
	// (engine/client/cl_cmds.c:322) and an aborted load crashes it.
	if( m_szCampaign[0] && !UI_CoFMapNameIsSane( m_szCampaign ))
	{
		Con_Printf( "Cry of Fear: refusing to start campaign, bad first map \"%s\"\n", m_szCampaign );
		return;
	}

	EngFuncs::CvarSetValue( "deathmatch", 0.0f );
	EngFuncs::CvarSetValue( "teamplay", 0.0f );
	EngFuncs::CvarSetValue( "pausable", 1.0f );
	EngFuncs::CvarSetValue( "maxplayers", 1.0f );
	EngFuncs::CvarSetValue( "coop", 0.0f );
	EngFuncs::PlayBackgroundTrack( NULL, NULL );

	// The original difficulty panel stopped the client's MP3 player before it
	// changed level. "stopmp3" is the client's own console command (no
	// arguments, unconditional stop) and is a different player from the
	// engine's PlayBackgroundTrack above; without it the background map's music
	// carries into the new game.
	if( UI_IsCryOfFear( ))
		EngFuncs::ClientCmd( false, "stopmp3\n" );

	// Skip prologue: start the main campaign at the map the prologue leads to
	// instead of at c_intro. Main campaign only - a custom campaign has its own
	// first map and the checkbox is not shown for one. See COF_SKIP_PROLOGUE_MAP
	// for why that map is c_nightmare and why it is safe.
	const char *firstmap = m_szCampaign[0] ? m_szCampaign : COF_FIRST_MAP;

	if( !m_szCampaign[0] && EngFuncs::GetCvarFloat( COF_SKIP_PROLOGUE_CVAR ) != 0.0f )
	{
		firstmap = COF_SKIP_PROLOGUE_MAP;
		Con_Printf( "Cry of Fear: skip prologue is on, starting at \"%s\" instead of \"%s\"\n",
			COF_SKIP_PROLOGUE_MAP, COF_FIRST_MAP );
	}

	if( EngFuncs::GetCvarFloat( "host_serverstate" ) != 0.0f )
	{
		// "campaign <map>" is the game DLL's own override for the map its
		// skillset think loads (hl.dll ClientCommand at VA 10019C91 stores the
		// name, the think at VA 100DFC10 formats "map %s\n" with it and falls
		// back to "map c_intro\n"), so the skip rides the stock rails: the
		// authentic white fade, inventory/game_start.wav and the difficulty
		// write all still happen, only the destination differs.
		if( strcmp( firstmap, COF_FIRST_MAP ))
			EngFuncs::ClientCmdF( false, "cmd campaign %s\n", firstmap );
		EngFuncs::ClientCmdF( false, "cmd skillset %d\n", skillset );
	}
	else
	{
		EngFuncs::CvarSetValue( "difficulty", (float)skillset );
		EngFuncs::ClientCmdF( false, "map %s\n", firstmap );
	}

	UI_CloseMenu();
}

void CMenuCoFDifficulty::StartGameCb( CMenuBaseItem *pSelf, void *pExtra )
{
	CMenuCoFDifficulty *ui = (CMenuCoFDifficulty *)pSelf->Parent();

	ui->StartGame( (int)(size_t)pExtra );
}

/*
=================
CMenuCoFDifficulty::CommentaryLabel

The [ON ]/[OFF] text marker existed only because Cry of Fear ships no
gfx/shell/cb_* artwork and the stock checkbox therefore drew nothing at all.
The theme draws the box from primitives, so the marker is not needed.
=================
*/
void CMenuCoFDifficulty::CommentaryLabel()
{
	if( UI_ThemeActive( ))
		Q_strncpy( m_szCommentaryLabel, L( "Enable developer commentary" ), sizeof( m_szCommentaryLabel ));
	else
		snprintf( m_szCommentaryLabel, sizeof( m_szCommentaryLabel ), "%s %s",
			devCommentary.bChecked ? "[ON ]" : "[OFF]", L( "Enable developer commentary" ));

	devCommentary.szName = m_szCommentaryLabel;
}

void CMenuCoFDifficulty::DevCommentaryChanged()
{
	devCommentary.WriteCvar();
	CommentaryLabel();
}

void CMenuCoFDifficulty::Refresh()
{
	// _Init() creates the buttons; Show() and _VidInit() can both be reached
	// before that has happened (a menu_* console command, or a global VidInit),
	// so never dereference them blind.
	if( !skillBtn[COF_NUM_SKILLSETS - 1] )
		return;

	if( m_szCampaign[0] )
		snprintf( m_szLabel, sizeof( m_szLabel ), "Custom campaign: %s (first map %s)",
			m_szTitle, m_szCampaign );
	else
		snprintf( m_szLabel, sizeof( m_szLabel ), "%s", L( "Cry of Fear" ));
	campaignLabel.szName = m_szLabel;

	const int  nightmareState = UI_CoFNightmareStatus();
	const bool nightmare = ( nightmareState == 1 );
	const char *nightmareWhy = ( nightmareState == 0 ) ?
		"Nightmare has to be unlocked in game (scriptsettings.dat line 83)." :
		"Nightmare state is unknown: cryoffear/scriptsettings.dat is missing or line 83 is unreadable.";

	skillBtn[COF_NUM_SKILLSETS - 1]->szName = nightmare ? s_cofSkillName[COF_NUM_SKILLSETS - 1] : "Locked";
	skillBtn[COF_NUM_SKILLSETS - 1]->szStatusText = nightmare ? s_cofSkillStatus[COF_NUM_SKILLSETS - 1] : nightmareWhy;
	skillBtn[COF_NUM_SKILLSETS - 1]->SetGrayed( !nightmare );

	if( UI_ThemeActive( ))
	{
		// the locked row's explanation is a dim caption under the list instead
		// of a status string only visible on hover
		lockedNote.szName = nightmare ? "" : nightmareWhy;
		lockedNote.SetVisibility( !nightmare );

		if( m_iSelected == COF_NUM_SKILLSETS - 1 && !nightmare )
			m_iSelected = 0;
		SelectRow( m_iSelected );
	}

	devCommentary.UpdateEditable();
	CommentaryLabel();

	// Skip prologue is about the main campaign's intro map only; a custom
	// campaign starts at its own first map and has no prologue to skip
	skipPrologue.SetVisibility( m_szCampaign[0] == 0 );
	skipPrologue.UpdateEditable();
}

void CMenuCoFDifficulty::Show()
{
	// the Nightmare row is the one thing on this page that depends on the
	// player's progress, so re-read scriptsettings.dat before it is drawn
	UI_CoFUnlockablesReload();

	// base Show() runs Init() the first time, so the items only exist after it
	CMenuFramework::Show();
	Refresh();
}

void CMenuCoFDifficulty::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Choose a difficulty" );

	if( theme )
		// 440, not 400: the page grew a "Skip prologue" row in the
		// 2026-09-22 round and at 400 the framework's status line at the
		// bottom of the panel ran under the Cancel/Start row.
		SetPanel( "NEW GAME", 560, 440 );

	for( int i = 0; i < COF_NUM_SKILLSETS; i++ )
	{
		skillCallback[i] = StartGameCb;
		skillCallback[i].pExtra = (void *)(size_t)( i + 1 );

		if( theme )
		{
			// the four difficulties are full-width selectable rows, not four
			// WON pic buttons in a column
			skillRow[i].SetNameAndStatus( s_cofSkillName[i], s_cofSkillStatus[i] );
			skillRow[i].iFlags |= QMF_NOTIFY;
			skillRow[i].eThemeStyle = CMenuPicButton::THEME_ROW;
			skillRow[i].eTextAlignment = QM_LEFT;
			skillRow[i].onReleased = skillCallback[i];
			skillRow[i].onGotFocus = SelectRowCb;
			skillRow[i].onGotFocus.pExtra = (void *)(size_t)( i + 1 );
			AddItem( skillRow[i] );
			skillBtn[i] = &skillRow[i];
		}
		else
		{
			skillBtn[i] = AddButton( s_cofSkillName[i], s_cofSkillStatus[i],
				s_cofSkillPic[i], skillCallback[i], QMF_NOTIFY );
		}
	}

	campaignLabel.iFlags = QMF_INACTIVE;
	campaignLabel.colorBase = uiColorHelp;
	campaignLabel.SetCharSize( QM_SMALLFONT );
	campaignLabel.szName = m_szLabel;
	campaignLabel.SetCoord( 72, 200 );

	lockedNote.iFlags = QMF_INACTIVE;
	lockedNote.colorBase = uiColorHelp;
	lockedNote.SetCharSize( QM_SMALLFONT );
	lockedNote.szName = "";
	lockedNote.SetVisibility( false );

	devCommentary.SetCoord( 72, 230 + ( COF_NUM_SKILLSETS + 1 ) * 50 );
	devCommentary.LinkCvar( "dev_commentary" );
	devCommentary.UpdateEditable();
	devCommentary.szStatusText = L( "Play with the developer commentary track" );
	devCommentary.onChanged = VoidCb( &CMenuCoFDifficulty::DevCommentaryChanged );

	skipPrologue.szName = L( "Skip prologue" );
	skipPrologue.szStatusText = L( "Start at c_nightmare, past the three-minute intro sequence. The intro grants nothing, so nothing is lost" );
	skipPrologue.SetCoord( 72, 230 + ( COF_NUM_SKILLSETS + 2 ) * 50 );
	skipPrologue.LinkCvar( COF_SKIP_PROLOGUE_CVAR );
	skipPrologue.UpdateEditable();
	skipPrologue.onChanged = CMenuEditable::WriteCvarCb;

	if( !theme )
		AddItem( banner );
	AddItem( campaignLabel );
	AddItem( devCommentary );
	AddItem( skipPrologue );

	if( theme )
		AddItem( lockedNote );

	// button row, secondary first: Cancel then Start
	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFDifficulty::Hide ), QMF_NOTIFY );

	if( theme )
		AddButton( L( "Start" ), L( "Start a new game on the selected difficulty" ),
			PC_NEW_GAME, VoidCb( &CMenuCoFDifficulty::StartSelected ), QMF_NOTIFY );

	Refresh();
}

void CMenuCoFDifficulty::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect c = ContentRect();

		campaignLabel.SetRect( c.pt.x, c.pt.y, c.sz.w, THEME_LABEL_TALL );

		int y = c.pt.y + THEME_LABEL_TALL + 14;

		for( int i = 0; i < COF_NUM_SKILLSETS; i++ )
		{
			skillRow[i].SetRect( c.pt.x, y, c.sz.w, THEME_ROW_H );
			y += THEME_ROW_PITCH;
		}

		y += 6;
		lockedNote.SetRect( c.pt.x, y, c.sz.w, THEME_LABEL_TALL * 2 );

		// the two checkboxes sit at the bottom of the panel, Skip prologue
		// under the commentary box; when Skip prologue is hidden (a custom
		// campaign) the commentary box keeps the bottom line to itself
		if( skipPrologue.IsVisible( ))
		{
			devCommentary.SetCoord( c.pt.x, c.pt.y + c.sz.h - THEME_CHECK_SIZE - THEME_ROW_PITCH );
			skipPrologue.SetCoord( c.pt.x, c.pt.y + c.sz.h - THEME_CHECK_SIZE );
		}
		else
		{
			devCommentary.SetCoord( c.pt.x, c.pt.y + c.sz.h - THEME_CHECK_SIZE );
		}
	}

	Refresh();
}

static void UI_CoFDifficulty_Precache( void );
static void UI_CoFDifficulty_Shutdown( void );
static CMenuCoFDifficulty *menu_cofdifficulty = NULL;

static void UI_CoFDifficulty_Precache( void )
{
	menu_cofdifficulty = new CMenuCoFDifficulty();
	// cof_nightmare_unlocked is gone: the flag is read from
	// cryoffear/scriptsettings.dat line 83 (UI_CoFNightmareStatus)
	EngFuncs::CvarRegister( "dev_commentary", "0", FCVAR_ARCHIVE );
	// archived, default off; read in CMenuCoFDifficulty::StartGame
	EngFuncs::CvarRegister( COF_SKIP_PROLOGUE_CVAR, "0", FCVAR_ARCHIVE );
}

static void UI_CoFDifficulty_Shutdown( void )
{
	delete menu_cofdifficulty;
	menu_cofdifficulty = NULL;
}

void UI_CoFDifficulty_Menu( void )
{
	menu_cofdifficulty->SetCampaign( NULL, NULL );
	menu_cofdifficulty->Show();
}

void UI_CoFDifficultyForCampaign( const char *firstmap, const char *title )
{
	menu_cofdifficulty->SetCampaign( firstmap, title );
	menu_cofdifficulty->Show();
}

ADD_MENU4( menu_cofdifficulty, UI_CoFDifficulty_Precache, UI_CoFDifficulty_Menu, UI_CoFDifficulty_Shutdown );

// ---------------------------------------------------------------------------
// custom campaign
// ---------------------------------------------------------------------------

struct cofCampaign_t
{
	char file[64];
	char firstmap[64];
	char description[128];
	char author[128];
};

class CMenuCoFCampaign;

class CMenuCoFCampaignModel : public CMenuBaseModel, public CUtlVector<cofCampaign_t>
{
public:
	CMenuCoFCampaignModel( CMenuCoFCampaign *parent ) : parent( parent ) { }

	void Update() override;
	int GetColumns() const override { return 3; }
	int GetRows() const override { return Count(); }
	const char *GetCellText( int line, int column ) override
	{
		switch( column )
		{
		case 0: return Element( line ).description;
		case 1: return Element( line ).author;
		case 2: return Element( line ).firstmap;
		}
		return NULL;
	}
	void OnActivateEntry( int line ) override;

private:
	CMenuCoFCampaign *parent;
};

class CMenuCoFCampaign : public CMenuFramework
{
public:
	CMenuCoFCampaign() : CMenuFramework( "CMenuCoFCampaign" ), model( this ) { }

	void Show() override
	{
		// base Show() runs Init(), which is what creates play/list
		CMenuFramework::Show();
		model.Update();
		UpdateDetail();
	}

	void Play( int line );

private:
	void _Init() override;
	void _VidInit() override;
	void PlaySelected();
	void UpdateDetail();

	CMenuPicButton play;
	CMenuPicButton cancel;
	CMenuTable     list;
	CMenuAction    hint;
	CMenuAction    detail;
	char           m_szDetail[384];
	CMenuCoFCampaignModel model;

	friend class CMenuCoFCampaignModel;
};

/*
=================
CMenuCoFCampaignModel::Update

maps/*.custom is a flat three-line file of quoted key/value pairs, keys
firstmap / description / author, with no braces - not a KeyValues block. The
client's own parser only knows those three keys. Values are used verbatim,
trailing spaces included, so the list reads exactly like the original.
=================
*/
void CMenuCoFCampaignModel::Update( void )
{
	char **filenames;
	int numFiles = 0;

	RemoveAll();

	filenames = EngFuncs::GetFilesList( "maps/*.custom", &numFiles, 1 );
	for( int i = 0; i < numFiles; i++ )
	{
		cofCampaign_t entry;
		char token[256];
		char *pfile;
		byte *raw;
		int length = 0;

		memset( &entry, 0, sizeof( entry ));
		COM_FileBase( filenames[i], entry.file, sizeof( entry.file ));

		raw = EngFuncs::COM_LoadFile( filenames[i], &length );
		if( !raw )
			continue;

		pfile = (char *)raw;
		while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			char *dest = NULL;
			size_t destSize = 0;

			if( !stricmp( token, "firstmap" ))        { dest = entry.firstmap;    destSize = sizeof( entry.firstmap ); }
			else if( !stricmp( token, "description" )) { dest = entry.description; destSize = sizeof( entry.description ); }
			else if( !stricmp( token, "author" ))      { dest = entry.author;      destSize = sizeof( entry.author ); }

			pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile )
				break;
			if( dest )
				Q_strncpy( dest, token, destSize );
		}
		EngFuncs::COM_FreeFile( raw );

		if( !UI_CoFMapNameIsSane( entry.firstmap ))
		{
			// the client rejects a campaign file without firstmap too
			Con_Printf( "Cry of Fear: skipping %s, bad or missing firstmap\n", filenames[i] );
			continue;
		}
		if( !EngFuncs::IsMapValid( entry.firstmap ))
		{
			Con_Printf( "Cry of Fear: skipping %s, map %s is not installed\n",
				filenames[i], entry.firstmap );
			continue;
		}
		if( !entry.description[0] )
			Q_strncpy( entry.description, entry.file, sizeof( entry.description ));

		AddToTail( entry );
	}

	parent->play.SetGrayed( !IsValidIndex( 0 ));
}

void CMenuCoFCampaignModel::OnActivateEntry( int line )
{
	parent->Play( line );
}

void CMenuCoFCampaign::Play( int line )
{
	if( !model.IsValidIndex( line ))
		return;

	if( !UI_CoFMapNameIsSane( model[line].firstmap ))
	{
		Con_Printf( "Cry of Fear: campaign %s has no usable first map\n", model[line].file );
		return;
	}

	// Same two steps as the original: remember the campaign, then let the
	// difficulty page send "campaign <firstmap>" followed by "skillset N".
	UI_CoFDifficultyForCampaign( model[line].firstmap, model[line].description );
}

void CMenuCoFCampaign::PlaySelected()
{
	Play( list.GetCurrentIndex( ));
}

void CMenuCoFCampaign::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Custom Campaign" );

	if( theme )
		SetPanel( "CUSTOM CAMPAIGN", 860, 520 );

	cancel.szName = L( "GameUI_Cancel" );
	cancel.SetPicture( PC_CANCEL );
	cancel.iFlags |= QMF_NOTIFY;
	cancel.onReleased = VoidCb( &CMenuCoFCampaign::Hide );
	cancel.SetCoord( 72, 280 );

	play.szName = L( "Play" );
	play.SetPicture( PC_NEW_GAME );
	play.iFlags |= QMF_NOTIFY;
	play.onReleased = VoidCb( &CMenuCoFCampaign::PlaySelected );
	play.SetCoord( 72, 230 );

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = L( "From maps/*.custom" );
	hint.SetCoord( 72, 340 );

	detail.iFlags = QMF_INACTIVE;
	detail.colorBase = uiColorHelp;
	detail.SetCharSize( QM_SMALLFONT );
	detail.szName = m_szDetail;
	m_szDetail[0] = 0;

	list.SetupColumn( 0, L( "Campaign" ), 0.45f );
	list.SetupColumn( 1, L( "Author" ), 0.35f );
	list.SetupColumn( 2, L( "First map" ), 0.20f );
	list.SetModel( &model );
	list.SetCharSize( QM_SMALLFONT );
	list.SetRect( 360, 230, -20, 465 );

	if( theme )
	{
		list.onChanged = VoidCb( &CMenuCoFCampaign::UpdateDetail );

		AddItem( list );
		AddItem( hint );
		AddItem( detail );
		AddItem( cancel );
		AddItem( play );
		return;
	}

	AddItem( banner );
	AddItem( play );
	AddItem( cancel );
	AddItem( hint );
	AddItem( list );
}

void CMenuCoFCampaign::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect left = ColumnRect2( 0, 0.70f );
		Rect right = ColumnRect2( 1, 0.70f );

		list.SetRect( left.pt.x, left.pt.y, left.sz.w, left.sz.h - THEME_LABEL_TALL - 10 );
		hint.SetRect( left.pt.x, left.pt.y + left.sz.h - THEME_LABEL_TALL, left.sz.w, THEME_LABEL_TALL );
		detail.SetRect( right.pt.x, right.pt.y, right.sz.w, right.sz.h );

		CMenuPicButton *row[] = { &cancel, &play };
		LayoutButtonRow( row, 2 );
	}
}

/*
=================
CMenuCoFCampaign::UpdateDetail

The panel's right-hand column repeats the selected campaign's description,
author and first map as running text.
=================
*/
void CMenuCoFCampaign::UpdateDetail( void )
{
	int line = list.GetCurrentIndex();

	if( !model.IsValidIndex( line ))
	{
		m_szDetail[0] = 0;
		return;
	}

	snprintf( m_szDetail, sizeof( m_szDetail ), "%s\n\n%s\n\nFirst map: %s",
		model[line].description, model[line].author, model[line].firstmap );
}

ADD_MENU( menu_cofcampaign, CMenuCoFCampaign, UI_CoFCampaign_Menu );

// ---------------------------------------------------------------------------
// language
// ---------------------------------------------------------------------------

#define COF_NUM_LANGUAGES 7

// subtitleset 1 is English, whose text lives directly in cryoffear/txtfiles;
// 2..7 are the six cryoffear/txtfiles/languages/<name> folders, in the same
// alphabetical order as the client's own path table.
static const char *s_cofLanguage[COF_NUM_LANGUAGES] =
{
	"English", "Dutch", "French", "German", "Norwegian", "Spanish", "Swedish"
};

// ---------------------------------------------------------------------------
// Subtitle language, shared with the Game options page
//
// The original panel sends the bare game-DLL command "subtitleset N"; the
// client answers it by writing the archived cvar cof_subtitlelanguage, which is
// how the choice survives a restart. Without a server there is nothing to
// forward to, so write the cvar directly - the same split the Language page
// itself uses.
// ---------------------------------------------------------------------------

int UI_CoFSubtitleLanguageCount( void )
{
	return COF_NUM_LANGUAGES;
}

const char *UI_CoFSubtitleLanguageName( int index )
{
	if( index < 0 || index >= COF_NUM_LANGUAGES )
		return "";
	return s_cofLanguage[index];
}

int UI_CoFGetSubtitleLanguage( void )
{
	int index = (int)EngFuncs::GetCvarFloat( "cof_subtitlelanguage" );

	if( index < 1 || index > COF_NUM_LANGUAGES )
		index = 1;
	return index;
}

void UI_CoFSetSubtitleLanguage( int index )
{
	if( index < 1 || index > COF_NUM_LANGUAGES )
		return;

	if( EngFuncs::GetCvarFloat( "host_serverstate" ) != 0.0f )
		EngFuncs::ClientCmdF( false, "cmd subtitleset %d\n", index );
	else
		EngFuncs::CvarSetValue( "cof_subtitlelanguage", (float)index );
}

class CMenuCoFLanguage : public CMenuFramework
{
public:
	CMenuCoFLanguage() : CMenuFramework( "CMenuCoFLanguage" ) { m_szCurrent[0] = 0; }

	void Show() override
	{
		// base Show() runs Init(), which is what creates the items
		CMenuFramework::Show();
		Refresh();
	}

private:
	void _Init() override;
	void _VidInit() override
	{
		if( IsPanel( ))
		{
			for( int i = 0; i < COF_NUM_LANGUAGES; i++ )
			{
				Rect r = RowRect( i );

				langRow[i].SetRect( r.pt.x, r.pt.y, r.sz.w, r.sz.h );
			}

			CMenuPicButton *row[] = { &close };
			LayoutButtonRow( row, 1 );
		}
		Refresh();
	}
	void Refresh();

	static void SetLanguageCb( CMenuBaseItem *pSelf, void *pExtra );
	void SetLanguage( int index );

	CEventCallback  langCallback[COF_NUM_LANGUAGES];
	CMenuPicButton *langBtn[COF_NUM_LANGUAGES];
	CMenuPicButton  langRow[COF_NUM_LANGUAGES];
	CMenuPicButton  close;
	CMenuAction     current;
	char            m_szCurrent[128];
};

/*
=================
CMenuCoFLanguage::SetLanguage

The original panel sends the bare game-DLL command "subtitleset N"; the client
answers it by writing the archived cvar cof_subtitlelanguage, which is how the
choice survives a restart (it is saved in cryoffear/config.cfg). Measured:
"cmd subtitleset 4" changes cof_subtitlelanguage from 1 to 4.
=================
*/
void CMenuCoFLanguage::SetLanguage( int index )
{
	UI_CoFSetSubtitleLanguage( index );
	Refresh();
}

void CMenuCoFLanguage::SetLanguageCb( CMenuBaseItem *pSelf, void *pExtra )
{
	CMenuCoFLanguage *ui = (CMenuCoFLanguage *)pSelf->Parent();

	ui->SetLanguage( (int)(size_t)pExtra );
}

void CMenuCoFLanguage::Refresh()
{
	int index = UI_CoFGetSubtitleLanguage();

	snprintf( m_szCurrent, sizeof( m_szCurrent ), "Subtitle language: %s", s_cofLanguage[index - 1] );
	current.szName = m_szCurrent;

	// the theme marks the current choice on the row itself, so the caption is
	// redundant and is not shown
	if( UI_ThemeActive( ))
	{
		for( int i = 0; i < COF_NUM_LANGUAGES; i++ )
			langRow[i].bThemeSelected = ( i == index - 1 );
	}
}

void CMenuCoFLanguage::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Language" );

	if( theme )
		SetPanel( "LANGUAGE", 420, 420 );

	for( int i = 0; i < COF_NUM_LANGUAGES; i++ )
	{
		langCallback[i] = SetLanguageCb;
		langCallback[i].pExtra = (void *)(size_t)( i + 1 );

		if( theme )
		{
			langRow[i].SetNameAndStatus( s_cofLanguage[i], L( "Set the subtitle and document language" ));
			langRow[i].iFlags |= QMF_NOTIFY;
			langRow[i].eThemeStyle = CMenuPicButton::THEME_ROW;
			langRow[i].eTextAlignment = QM_LEFT;
			langRow[i].onReleased = langCallback[i];
			AddItem( langRow[i] );
			langBtn[i] = &langRow[i];
		}
		else
		{
			langBtn[i] = AddButton( s_cofLanguage[i], L( "Set the subtitle and document language" ),
				PC_CUSTOMIZE, langCallback[i], QMF_NOTIFY );
		}
	}

	current.iFlags = QMF_INACTIVE;
	current.colorBase = uiColorHelp;
	current.SetCharSize( QM_SMALLFONT );
	current.szName = m_szCurrent;
	current.SetCoord( 72, 200 );

	if( theme )
	{
		close.szName = L( "Close" );
		close.SetPicture( PC_DONE );
		close.iFlags |= QMF_NOTIFY;
		close.onReleased = VoidCb( &CMenuCoFLanguage::Hide );
		AddItem( close );
	}
	else
	{
		AddButton( L( "Done" ), L( "Go back to the Main menu" ), PC_DONE,
			VoidCb( &CMenuCoFLanguage::Hide ), QMF_NOTIFY );
		AddItem( banner );
		AddItem( current );
	}

	Refresh();
}

ADD_MENU( menu_coflanguage, CMenuCoFLanguage, UI_CoFLanguage_Menu );

// ---------------------------------------------------------------------------
// extras
// ---------------------------------------------------------------------------

#define COF_NUM_EXTRAS 7

// The six links of the original Extras panel, in its own order, with this
// project's own page added first. The original opened them with
// SteamFriends()->ActivateGameOverlayToWebPage and had no fallback, so under
// FWGS every one of them was a silent no-op; ShellExecute hands them to the
// system browser instead.
static const char *s_cofExtraName[COF_NUM_EXTRAS] =
{
	"Cry of Fear: Enhanced",
	"Donate",
	"Soundtrack 1",
	"Soundtrack 2",
	"Clothes",
	"Afraid of Monsters DC",
	"Facebook"
};

static const char *s_cofExtraUrl[COF_NUM_EXTRAS] =
{
	"https://cofenhanced.haej.pl",
	"http://www.cry-of-fear.com/donate.php",
	"http://www.cdbaby.com/cd/andreasronnberg",
	"http://www.cdbaby.com/cd/andreasronnberg2",
	"http://teampsykskallar.spreadshirt.se/",
	"http://www.moddb.com/mods/afraid-of-monsters-dc",
	"http://www.facebook.com/CryOfFearOfficial"
};

class CMenuCoFExtras : public CMenuFramework
{
public:
	CMenuCoFExtras() : CMenuFramework( "CMenuCoFExtras" ) { }

private:
	void _Init() override;
	void _VidInit() override;

	static void OpenCb( CMenuBaseItem *pSelf, void *pExtra );

	CEventCallback urlCallback[COF_NUM_EXTRAS];
	CMenuPicButton extraRow[COF_NUM_EXTRAS];
	CMenuAction    extraUrl[COF_NUM_EXTRAS];
	CMenuPicButton close;
};

void CMenuCoFExtras::OpenCb( CMenuBaseItem *pSelf, void *pExtra )
{
	const int index = (int)(size_t)pExtra;

	if( index < 0 || index >= COF_NUM_EXTRAS )
		return;

	EngFuncs::ShellExecute( s_cofExtraUrl[index], NULL, false );
}

void CMenuCoFExtras::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Extras" );

	if( theme )
		SetPanel( "EXTRAS", 560, 440 );

	for( int i = 0; i < COF_NUM_EXTRAS; i++ )
	{
		urlCallback[i] = OpenCb;
		urlCallback[i].pExtra = (void *)(size_t)i;

		if( theme )
		{
			extraRow[i].SetNameAndStatus( s_cofExtraName[i], s_cofExtraUrl[i] );
			extraRow[i].iFlags |= QMF_NOTIFY;
			extraRow[i].eThemeStyle = CMenuPicButton::THEME_ROW;
			extraRow[i].eTextAlignment = QM_LEFT;
			extraRow[i].onReleased = urlCallback[i];
			AddItem( extraRow[i] );

			// the URL was only a hover status string; show it as a dim second
			// line under each row instead
			extraUrl[i].iFlags = QMF_INACTIVE;
			extraUrl[i].colorBase = uiColorHelp;
			extraUrl[i].SetCharSize( QM_SMALLFONT );
			extraUrl[i].szName = s_cofExtraUrl[i];
			AddItem( extraUrl[i] );
		}
		else
		{
			AddButton( s_cofExtraName[i], s_cofExtraUrl[i], PC_VISIT_WEB_SITE,
				urlCallback[i], QMF_NOTIFY );
		}
	}

	if( theme )
	{
		close.szName = L( "Close" );
		close.SetPicture( PC_DONE );
		close.iFlags |= QMF_NOTIFY;
		close.onReleased = VoidCb( &CMenuCoFExtras::Hide );
		AddItem( close );
		return;
	}

	AddButton( L( "Done" ), L( "Go back to the Main menu" ), PC_DONE,
		VoidCb( &CMenuCoFExtras::Hide ), QMF_NOTIFY );

	AddItem( banner );
}

void CMenuCoFExtras::_VidInit( void )
{
	if( !IsPanel( ))
		return;

	Rect c = ContentRect();
	const int pitch = c.sz.h / COF_NUM_EXTRAS;
	int y = c.pt.y;

	for( int i = 0; i < COF_NUM_EXTRAS; i++ )
	{
		extraRow[i].SetRect( c.pt.x, y, c.sz.w, THEME_BODY_TALL + 6 );
		extraUrl[i].SetRect( c.pt.x + 10, y + THEME_BODY_TALL + 6, c.sz.w - 10, THEME_LABEL_TALL );
		y += pitch;
	}

	CMenuPicButton *row[] = { &close };
	LayoutButtonRow( row, 1 );
}

ADD_MENU( menu_cofextras, CMenuCoFExtras, UI_CoFExtras_Menu );

// ---------------------------------------------------------------------------
// credits
// ---------------------------------------------------------------------------

/*
=================
CMenuCoFCredits

Three sections, each a small uppercase section label, one or more body lines,
and a link button that hands a URL to the system browser through
EngFuncs::ShellExecute - the same route the Links page uses, because the
original panel's SteamFriends()->ActivateGameOverlayToWebPage is a silent no-op
under FWGS.

The Polish section and the first name carry non-ASCII letters (l-stroke,
o-diaeresis). The literals below are UTF-8 and the theme's primary font backend
is stb_truetype over the shipped Inter faces (--enable-stbtt, see the Type
section of docs/cof-ui-m3-theme.md), which rasterises by code point, so they
render as themselves.

One column, sized to fit 1280x720 without scrolling: a 640x470 panel in the
1024x768 virtual space is 61 percent of the height, and the section pitch is
derived from the content rect rather than hard-coded, so it follows the panel
at every resolution.
=================
*/
#define COF_NUM_CREDIT_SECTIONS 3
#define COF_MAX_CREDIT_LINES    4

struct cofCreditSection_t
{
	const char *label;
	const char *lines[COF_MAX_CREDIT_LINES];
	int         numLines;
	int         dimFrom;   // first line index drawn dim; >= numLines for none
	const char *linkName;
	const char *linkUrl;
};

static const cofCreditSection_t s_cofCredits[COF_NUM_CREDIT_SECTIONS] =
{
	{
		"TEAM PSYKSKALLAR",
		{ "Andreas \"ruMpel\" Rönnberg and James \"Minuit\" Marchant",
		  "Creators of Cry of Fear", NULL, NULL },
		2, 1,
		"YouTube", "https://www.youtube.com/@TeamPsykskallar"
	},
	{
		"CRY OF FEAR: ENHANCED",
		{ "haej", "Community-driven", NULL, NULL },
		2, 1,
		"Project site", "https://cofenhanced.haej.pl"
	},
	{
		"CRY OF FEAR SPOLSZCZENIE (POLISH LOCALISATION)",
		{ "Avioo - Tłumaczenie, grafiki, testowanie",
		  "Mixdedemon - Tłumaczenie",
		  "hexag0n - Technikalia, testowanie",
		  "Izonka - Testowanie" },
		4, 4,
		"Steam Workshop", "https://steamcommunity.com/sharedfiles/filedetails/?id=3164091802"
	}
};

class CMenuCoFCredits : public CMenuFramework
{
public:
	CMenuCoFCredits() : CMenuFramework( "CMenuCoFCredits" ) { }

private:
	void _Init() override;
	void _VidInit() override;

	static void OpenCb( CMenuBaseItem *pSelf, void *pExtra );

	CMenuAction    sectionLabel[COF_NUM_CREDIT_SECTIONS];
	CMenuAction    bodyLine[COF_NUM_CREDIT_SECTIONS][COF_MAX_CREDIT_LINES];
	CEventCallback linkCallback[COF_NUM_CREDIT_SECTIONS];
	CMenuPicButton linkBtn[COF_NUM_CREDIT_SECTIONS];
	CMenuPicButton close;
};

void CMenuCoFCredits::OpenCb( CMenuBaseItem *pSelf, void *pExtra )
{
	const int index = (int)(size_t)pExtra;

	if( index < 0 || index >= COF_NUM_CREDIT_SECTIONS )
		return;

	EngFuncs::ShellExecute( s_cofCredits[index].linkUrl, NULL, false );
}

void CMenuCoFCredits::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Credits" );

	if( theme )
		SetPanel( "CREDITS", 640, 470 );

	for( int s = 0; s < COF_NUM_CREDIT_SECTIONS; s++ )
	{
		const cofCreditSection_t *sec = &s_cofCredits[s];

		sectionLabel[s].iFlags = QMF_INACTIVE;
		sectionLabel[s].colorBase = uiColorHelp;
		sectionLabel[s].SetCharSize( QM_SMALLFONT );
		sectionLabel[s].szName = sec->label;
		AddItem( sectionLabel[s] );

		for( int i = 0; i < sec->numLines; i++ )
		{
			bodyLine[s][i].iFlags = QMF_INACTIVE;
			bodyLine[s][i].colorBase = ( i >= sec->dimFrom ) ? uiColorHelp : uiColorWhite;
			bodyLine[s][i].SetCharSize( QM_SMALLFONT );
			bodyLine[s][i].szName = sec->lines[i];
			AddItem( bodyLine[s][i] );
		}

		linkCallback[s] = OpenCb;
		linkCallback[s].pExtra = (void *)(size_t)s;

		if( theme )
		{
			linkBtn[s].SetNameAndStatus( sec->linkName, sec->linkUrl );
			linkBtn[s].iFlags |= QMF_NOTIFY;
			linkBtn[s].eThemeStyle = CMenuPicButton::THEME_ROW;
			linkBtn[s].eTextAlignment = QM_CENTER;
			linkBtn[s].onReleased = linkCallback[s];
			AddItem( linkBtn[s] );
		}
		else
		{
			AddButton( sec->linkName, sec->linkUrl, PC_VISIT_WEB_SITE,
				linkCallback[s], QMF_NOTIFY );
		}
	}

	if( theme )
	{
		close.szName = L( "Close" );
		close.SetPicture( PC_DONE );
		close.iFlags |= QMF_NOTIFY;
		close.onReleased = VoidCb( &CMenuCoFCredits::Hide );
		AddItem( close );
		return;
	}

	AddButton( L( "Done" ), L( "Go back to the Main menu" ), PC_DONE,
		VoidCb( &CMenuCoFCredits::Hide ), QMF_NOTIFY );

	AddItem( banner );
}

void CMenuCoFCredits::_VidInit( void )
{
	if( !IsPanel( ))
		return;

	Rect c = ContentRect();

	// every section gets the same slice of the content rect, so the spacing
	// stays generous and nothing is pinned to one resolution
	const int pitch = c.sz.h / COF_NUM_CREDIT_SECTIONS;
	int y = c.pt.y;

	// one width for all three link buttons, measured from the longest label so
	// "Steam Workshop" is not clipped to "Steam ...". THEME_BTN_MIN_W is 110
	// virtual units and two of the three labels are wider than that.
	int btnW = THEME_BTN_MIN_W;

	if( uiStatic.scaleX > 0.0f )
	{
		const int chSize = (int)( THEME_BODY_TALL * uiStatic.scaleY );

		for( int s = 0; s < COF_NUM_CREDIT_SECTIONS; s++ )
		{
			int w = (int)( g_FontMgr->GetTextWideScaled( uiStatic.hThemeBody,
				s_cofCredits[s].linkName, chSize ) / uiStatic.scaleX ) + 24;

			if( w > btnW )
				btnW = w;
		}
	}

	for( int s = 0; s < COF_NUM_CREDIT_SECTIONS; s++ )
	{
		const cofCreditSection_t *sec = &s_cofCredits[s];
		int ly = y;

		sectionLabel[s].SetRect( c.pt.x, ly, c.sz.w, THEME_LABEL_TALL );
		ly += THEME_LABEL_TALL + 8;

		for( int i = 0; i < sec->numLines; i++ )
		{
			// only the line the link button sits beside has to make room for
			// it; the others get the full content width, otherwise the long
			// first line of the first section is clipped
			const bool beside = ( i == sec->numLines - 1 );

			bodyLine[s][i].SetRect( c.pt.x, ly,
				beside ? c.sz.w - btnW - THEME_BTN_GAP : c.sz.w, THEME_LABEL_TALL );
			ly += THEME_LABEL_TALL + 4;
		}

		// the link button is right-aligned, level with the section's last line
		linkBtn[s].SetRect( c.pt.x + c.sz.w - btnW,
			ly - ( THEME_LABEL_TALL + 4 ) - ( THEME_ROW_H - THEME_LABEL_TALL ) / 2,
			btnW, THEME_ROW_H );

		y += pitch;
	}

	CMenuPicButton *row[] = { &close };
	LayoutButtonRow( row, 1 );
}

ADD_MENU( menu_cofcredits, CMenuCoFCredits, UI_CoFCredits_Menu );

// ---------------------------------------------------------------------------
// unlockables
// ---------------------------------------------------------------------------

/*
=================
UI_CoFUnlockables

The unlock state lives in cryoffear/scriptsettings.dat as 101 fixed-width
obfuscated tokens compared against a table compiled into client.dll, so the
menu library cannot render the gallery itself. Run the client's own entry
command instead, which is what the original main menu did; it loads
maps/c_unlockables.bsp, where the gallery entity draws the real state.

This is now the "Open original gallery" button of the Unlockables page rather
than the main menu's own route: the gallery panel the client draws on that map
is mispositioned at high resolutions, and its MAIN MENU button returns to the
client's old menu.
=================
*/
void UI_CoFUnlockables( void )
{
	EngFuncs::CvarSetValue( "deathmatch", 0.0f );
	EngFuncs::CvarSetValue( "coop", 0.0f );
	EngFuncs::CvarSetValue( "maxplayers", 1.0f );
	EngFuncs::PlayBackgroundTrack( NULL, NULL );

	EngFuncs::ClientCmd( false, "unlockablescmd\n" );
	UI_CloseMenu();
}

// ---------------------------------------------------------------------------
// unlockables page
//
// The unlock state is real, and this is how it is read.
//
// MEASURED, cryoffear/scriptsettings.dat (canonical copy, 2222 bytes, SHA-256
// 194E7144C398A7176D7BCA8543FFD4547949AC998E6F38FA38982DC7D6295F1E): 101 lines
// of exactly 20 bytes separated by CRLF, with a trailing CRLF after the last
// one (101 * 20 + 101 * 2 = 2222). Line bytes are printable ASCII plus the
// single extended byte 0xA3 - the character the Steam guide renders as "£".
// There is no header, no magic and no length prefix.
//
// MEASURED, client.dll .rdata: immediately after the "/alt.dat" literal
// (VA 1014118C) the client carries 34 pairs of 20-byte tokens, each stored as
// <20 bytes>'\n' padded to 24, running from VA 10141198 to VA 101417F7 and
// followed by the unrelated "Ending seen: %d" strings. The two members of a
// pair differ in exactly one character. That is the whole "encryption": a flag
// is set by writing one of its two tokens into its own line of the file, and
// checked by comparing the line to both. (The '\n' terminator in the binary is
// why the file is CRLF: the client writes the token through a text-mode
// stream.)
//
// MEASURED, this session: every one of the 34 pairs matches exactly one line
// of the canonical scriptsettings.dat, and the 34 lines are all distinct, so
// the pair -> line mapping below is derived from the file itself, not guessed.
// All 27 named entries additionally match the Steam guide's table byte for
// byte, including the 0xA3 bytes - which is what pins the names to the lines.
// The first-listed member of each pair is the locked token (measured for the
// 27 named entries against the guide; assumed for the other 7, and consistent
// with a fresh install holding the first member on all 34 lines).
//
// The unlock *conditions* are still almost entirely untextual: a full sweep of
// the shipped data (txtfiles, notes, resource, scripts, the gallery map's
// entity lump) and of both game DLLs' string tables found only the
// "Unlocked: ..." announcements, the gallery art names, and five hint-page
// titles in hl.dll ("How To Unlock Book Pages", "Ranking System Explained",
// "How To Unlock Hoodies", "How To Unlock Secret Items", "How To Unlock Secret
// Weapons") - whose order matches the guide's five hint-page lines 76..80
// exactly. The pages themselves are rasterised textures inside
// models/bookmenu.mdl (page, page1..page5), so no string form exists. The
// "how" column is therefore blank wherever nothing is measured; nothing here
// is invented.
// ---------------------------------------------------------------------------

// MEASURED 2026-09-22 (milestone 5b): the user's all-unlocked reference file
// (K:\LLM\COF_Fix\CoF All Unlockables\scriptsettings.dat, SHA-256
// 7C76FEC82FD546BB8DA6FFFF863EE464E14801B7EED0AC645A3B734D947579AA) differs
// from the canonical one in exactly the 27 named lines (3, 39, 51-62, 70-80,
// 82, 83). The seven "Unidentified flag" pairs (lines 7, 15, 22, 45, 47, 48,
// 81) keep their locked token in a save with everything unlocked, so they are
// not unlockables. They stay in the table - UI_CoFUnlockableStatus still
// answers for them by index - but are flagged hidden and the page does not
// list them.
#define COF_NUM_UNLOCKABLES  34
#define COF_TOKEN_LEN        20
#define COF_UNLOCK_NIGHTMARE 33  // index of scriptsettings.dat line 83, below

typedef struct
{
	int         line;     // 1-based line number in cryoffear/scriptsettings.dat
	int         pair;     // index of the pair in client.dll's .rdata table
	bool        hidden;   // kept for the status function, not listed on the page
	const char *name;
	const char *how;      // "" when no condition is measured - never invented
	const char *locked;   // the client's own bytes, 0xA3 written as \243
	const char *unlocked;
} cofUnlockable_t;

static const cofUnlockable_t s_cofUnlockables[COF_NUM_UNLOCKABLES] =
{
	{  3,  4, false, "Secret room, empty hint book, MP5",
		"One flag for all three; MP5 via Steam group",
		"z3,4lj)4b!9g7vgi4x5a",
		"z3,4rj)4b!9g7vgi4x5a" },
	{  7,  1, true,  "Unidentified flag (line 7)",
		"",
		"ye&ole1^0(fcanb3v5t4",
		"ye&0le1^0(fcanb3v5t4" },
	{ 15,  0, true,  "Unidentified flag (line 15)",
		"",
		"v9agjak+p)(,o3qb)aq4",
		"v9agjak+p)(,03qb)aq4" },
	{ 22,  3, true,  "Unidentified flag (line 22)",
		"",
		"2quwo6z3y4w\243ld)y!i35",
		"2quvo6z3y4w\243ld)y!i35" },
	{ 39,  5, false, "FAMAS with infinite ammo",
		"",
		"r1&,iji4cjtvf,44w919",
		"r1&,iji4cjtvf.44w919" },
	{ 45,  2, true,  "Unidentified flag (line 45)",
		"",
		"1r6fd46ob7u2yj9q83yx",
		"1r6fd46ob7u2yj9r83yx" },
	{ 47, 32, true,  "Unidentified flag (line 47)",
		"",
		"g_t\2430j^zrqhli09jb5rc",
		"c_t\2430j^zrqhli09jb5rc" },
	{ 48, 33, true,  "Unidentified flag (line 48)",
		"",
		"kk78kewvx9!%n218dkss",
		"ck78kewvx9!%n218dkss" },
	{ 51,  6, false, "David Leatherhoff suit",
		"",
		"olt0\243s^\243rn(aki!9eqkh",
		"olt0\243s^\243rn(aki!9eqkj" },
	{ 52,  7, false, "ModDB hoodie",
		"",
		"h*wchks\243n&%!\243bf%jtew",
		"h*wchks\243n&%!\243bf%jtei" },
	{ 53,  8, false, "Hello Kitty suit",
		"",
		"%xn%i)jhxmfnbgjsr^if",
		"\243xn%i)jhxmfnbgjsr^if" },
	{ 54,  9, false, "Afraid of Monsters suit",
		"",
		"fy^xix&&vkz)vcdn!wtv",
		"fy^xix&&vkz)vcrn!wtv" },
	{ 55, 10, false, "Camouflage hoodie",
		"",
		"jwsyv**yb&(qtqlg)moj",
		"jwtyv**yb&(qtqlg)moj" },
	{ 56, 11, false, "Half Life Creations hoodie",
		"",
		"henp(cm$at!n%\243f(c$jp",
		"henp(cm$at!m%\243f(c$jp" },
	{ 57, 12, false, "Black Metal suit",
		"",
		"yef!xkyzw*^vd*tj)zkc",
		"yef!&kyzw*^vd*tj)zkc" },
	{ 58, 13, false, "Team Psykskallar hoodie",
		"",
		"z(*mbwy(talqlrqf$lmw",
		"z(*mbwy(tolqlrqf$lmw" },
	{ 59, 14, false, "Awesome suit",
		"",
		"zgpwg\243vyqlzensuauwat",
		"zwpwg\243vyqlzensuauwat" },
	{ 60, 15, false, "Sick Simon suit",
		"",
		"dzpy)*iiy$oj!akyfbn%",
		"dzpy)*iiy$0j!akyfbn%" },
	{ 61, 16, false, "AoM Twitcher suit",
		"",
		"pe%%*\243\243rxa(%(bog)l^j",
		"pe%%*\243\243rxr(%(bog)l^j" },
	{ 62, 17, false, "Custom hoodies",
		"",
		"efq%)x)mk\243*odxsdocb)",
		"efq%)u)mk\243*odxsdocb)" },
	{ 70, 18, false, "David Leatherhoff's axe",
		"",
		"pzakmx&p^lbk^kw($(fo",
		"pzakhx&p^lbk^kw($(fo" },
	{ 71, 19, false, "Digital camera",
		"",
		"g*q$cceqhz&vcdp(au%!",
		"g*q$cceqhz&vcdp(av%!" },
	{ 72, 20, false, "Simon's book",
		"",
		"aaa^$i)whenzprdh!vus",
		"aba^$i)whenzprdh!vus" },
	{ 73, 21, false, "Developer commentary",
		"Enable it on the New Game page once unlocked",
		"mwu\243g$&ztmkfwej(bg^a",
		"mwu\243g$&stmkfwej(bg^a" },
	{ 74, 22, false, "Hidden package",
		"Tied to the secret ending",
		"tzi&aoxfbkb^n&ikaxj%",
		"tzi&a0xfbkb^n&ikaxj%" },
	{ 75, 23, false, "Gasmask (night vision)",
		"",
		"wph!uhhgrws)*erdb*rg",
		"wph!uhmgrws)*erdb*rg" },
	{ 76, 24, false, "First hint page",
		"In-game hint page \"How To Unlock Book Pages\"",
		"&x&w%dvcr&$yoqs%c%ar",
		"&x&v%dvcr&$yoqs%c%ar" },
	{ 77, 25, false, "Second hint page",
		"In-game hint page \"Ranking System Explained\"",
		"feyvvuxq^i)brfentm$&",
		"foyvvuxq^i)brfentm$&" },
	{ 78, 26, false, "Third hint page",
		"In-game hint page \"How To Unlock Hoodies\"",
		"*j%gb*^noyresqjihoez",
		"*j%gb*^noyresqjihoes" },
	{ 79, 27, false, "Fourth hint page",
		"In-game hint page \"How To Unlock Secret Items\"",
		"nz(xdxj*w(\243!&uh\243qn*j",
		"nz(xdxj*w)\243!&uh\243qn*j" },
	{ 80, 28, false, "Fifth hint page",
		"In-game hint page \"How To Unlock Secret Weapons\"",
		"lvdw&bhwjqsjm$pfwnp)",
		"rvdw&bhwjqsjm$pfwnp)" },
	{ 81, 29, true,  "Unidentified flag (line 81)",
		"",
		"zqv%n(nqquvh)dlmtl!x",
		"zqv%n(noquvh)dlmtl!x" },
	{ 82, 30, false, "Doctor mode",
		"",
		")cfhc$vle$^hn\243padlvd",
		")cfhc$vle$^hn\243podlvd" },
	{ 83, 31, false, "Nightmare difficulty",
		"",
		"&fx!hd$bw^ayk%dfmcbg",
		"&fx!hd$bw!ayk%dfmcbg" },
};

// -1 unknown, 0 locked, 1 unlocked, one entry per row of the table above.
static signed char s_cofUnlockState[COF_NUM_UNLOCKABLES];
static bool        s_cofUnlockRead = false;

/*
=================
UI_CoFDatLine

The 1-based line lineNo of a scriptsettings.dat buffer, without its line
terminator. Splitting on '\n' and dropping a trailing '\r' rather than striding
by a fixed 22 bytes, so a file written with LF only still reads correctly.
Returns false when the file is too short.
=================
*/
static bool UI_CoFDatLine( const char *buf, int len, int lineNo, const char **out, int *outLen )
{
	int at = 1;
	int start = 0;
	int i;

	for( i = 0; i <= len; i++ )
	{
		if( i == len || buf[i] == '\n' )
		{
			if( at == lineNo )
			{
				int end = i;

				if( end > start && buf[end - 1] == '\r' )
					end--;

				// a trailing CRLF makes the last split empty; that is not a line
				if( i == len && end == start )
					return false;

				*out = buf + start;
				*outLen = end - start;
				return true;
			}
			at++;
			start = i + 1;
		}
	}
	return false;
}

/*
=================
UI_CoFUnlockablesReload

Read cryoffear/scriptsettings.dat once and resolve all 34 flags from it. The
file is 2 KiB, so this is cheap enough to redo whenever a page that shows the
state opens; nothing is cached across a page visit on purpose, because the
player can unlock something and come straight back to the menu.

Every flag whose line is missing, short, or equal to neither of its two tokens
stays unknown. A missing file leaves all 34 unknown rather than pretending
everything is locked - an empty or replaced file is not the same fact as a
locked flag.
=================
*/
void UI_CoFUnlockablesReload( void )
{
	char *buf;
	int   len = 0;
	int   i;

	for( i = 0; i < COF_NUM_UNLOCKABLES; i++ )
		s_cofUnlockState[i] = -1;

	s_cofUnlockRead = true;

	if( !UI_IsCryOfFear( ))
		return;

	buf = (char *)EngFuncs::COM_LoadFile( "scriptsettings.dat", &len );
	if( !buf )
	{
		Con_DPrintf( "Cry of Fear: scriptsettings.dat not found, unlock state unknown\n" );
		return;
	}

	for( i = 0; i < COF_NUM_UNLOCKABLES; i++ )
	{
		const char *line;
		int lineLen = 0;

		if( !UI_CoFDatLine( buf, len, s_cofUnlockables[i].line, &line, &lineLen ))
			continue;
		if( lineLen != COF_TOKEN_LEN )
			continue;

		if( !memcmp( line, s_cofUnlockables[i].unlocked, COF_TOKEN_LEN ))
			s_cofUnlockState[i] = 1;
		else if( !memcmp( line, s_cofUnlockables[i].locked, COF_TOKEN_LEN ))
			s_cofUnlockState[i] = 0;
	}

	EngFuncs::COM_FreeFile( (byte *)buf );

	// one developer line with every listed row, so the page can be checked
	// from a log (27 rows do not fit on one screen of the table)
	{
		char rows[512];
		int  used = 0, listed = 0, counts[3] = { 0, 0, 0 };

		rows[0] = '\0';
		for( i = 0; i < COF_NUM_UNLOCKABLES; i++ )
		{
			const int st = s_cofUnlockState[i];

			if( s_cofUnlockables[i].hidden )
				continue;

			listed++;
			counts[st + 1]++;
			if( used < (int)sizeof( rows ) - 8 )
				used += snprintf( rows + used, sizeof( rows ) - used, " %d:%c",
					s_cofUnlockables[i].line, st == 1 ? 'U' : st == 0 ? 'L' : '?' );
		}

		Con_DPrintf( "Cry of Fear: unlockables page %d rows: %d unlocked, %d locked, %d unknown; Nightmare (line 83) %s; line:state%s\n",
			listed, counts[2], counts[1], counts[0],
			s_cofUnlockState[COF_UNLOCK_NIGHTMARE] == 1 ? "unlocked" : "locked", rows );
	}
}

/*
=================
UI_CoFUnlockableStatus

-1 unknown, 0 locked, 1 unlocked. Reads the file lazily the first time, so a
caller that forgets to reload still gets a real answer.
=================
*/
int UI_CoFUnlockableStatus( int index )
{
	if( index < 0 || index >= COF_NUM_UNLOCKABLES )
		return -1;

	if( !s_cofUnlockRead )
		UI_CoFUnlockablesReload();

	return s_cofUnlockState[index];
}

int UI_CoFNightmareStatus( void )
{
	return UI_CoFUnlockableStatus( COF_UNLOCK_NIGHTMARE );
}

// page row -> table index, over the entries that are not hidden
static int UI_CoFUnlockableRowIndex( int row )
{
	int i;

	for( i = 0; i < COF_NUM_UNLOCKABLES; i++ )
	{
		if( s_cofUnlockables[i].hidden )
			continue;

		if( row-- == 0 )
			return i;
	}

	return -1;
}

static int UI_CoFUnlockableRowCount( void )
{
	int i, n = 0;

	for( i = 0; i < COF_NUM_UNLOCKABLES; i++ )
	{
		if( !s_cofUnlockables[i].hidden )
			n++;
	}

	return n;
}

class CMenuCoFUnlockables;

class CMenuCoFUnlockablesModel : public CMenuBaseModel
{
public:
	void Update() override { }
	int GetColumns() const override { return 3; }
	int GetRows() const override { return UI_CoFUnlockableRowCount(); }
	const char *GetCellText( int row, int column ) override
	{
		const int line = UI_CoFUnlockableRowIndex( row );

		if( line < 0 || line >= COF_NUM_UNLOCKABLES )
			return NULL;

		switch( column )
		{
		case 0: return s_cofUnlockables[line].name;
		case 1: return s_cofUnlockables[line].how;
		case 2:
			switch( UI_CoFUnlockableStatus( line ))
			{
			case 1:  return "Unlocked";
			case 0:  return "Locked";
			default: return "?";
			}
		}
		return NULL;
	}

	// The status column carries the theme's one accent when a flag is set and
	// the dim body colour when it is not, so the page reads at a glance. Every
	// other column keeps the table's own row colour.
	bool GetCellColors( int row, int column, unsigned int &textColor, bool &force ) const override
	{
		const int line = UI_CoFUnlockableRowIndex( row );

		if( column != 2 || !UI_ThemeActive( ) ||
			line < 0 || line >= COF_NUM_UNLOCKABLES )
			return false;

		force = true;

		switch( s_cofUnlockRead ? s_cofUnlockState[line] : -1 )
		{
		case 1:  textColor = THEME_ACCENT; break;
		case 0:  textColor = THEME_TEXT_DIM; break;
		default: textColor = THEME_DISABLED; break;
		}
		return true;
	}
};

class CMenuCoFUnlockables : public CMenuFramework
{
public:
	CMenuCoFUnlockables() : CMenuFramework( "CMenuCoFUnlockables" ) { }

	// the page is the one place the state is shown, so re-read the file every
	// time it opens rather than trusting whatever the last visit saw
	void Show() override
	{
		UI_CoFUnlockablesReload();
		CMenuFramework::Show();
	}

private:
	void _Init() override;
	void _VidInit() override;

	CMenuTable     list;
	CMenuAction    hint;
	CMenuPicButton gallery;
	CMenuPicButton close;
	CMenuCoFUnlockablesModel model;
};

void CMenuCoFUnlockables::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Unlockables" );

	if( theme )
		SetPanel( "UNLOCKABLES", 940, 600 );

	list.SetupColumn( 0, L( "Unlockable" ), 0.42f );
	list.SetupColumn( 1, L( "How to unlock" ), 0.42f );
	list.SetupColumn( 2, L( "Status" ), 0.16f );
	list.SetModel( &model );
	list.SetCharSize( QM_SMALLFONT );
	list.SetRect( 360, 230, -20, 465 );

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = "Status comes from cryoffear/scriptsettings.dat: each flag is one 20-byte line, compared to the two tokens client.dll carries for it. \"?\" means the file is missing or the line matches neither.";

	gallery.szName = L( "Open original gallery" );
	gallery.SetPicture( PC_VIEW_GAME_INFO );
	gallery.iFlags |= QMF_NOTIFY;
	gallery.szStatusText = "Load the c_unlockables gallery map, the client's own screen";
	gallery.onReleased = UI_CoFUnlockables;
	gallery.SetCoord( 72, 230 );

	close.szName = L( "Close" );
	close.SetPicture( PC_DONE );
	close.iFlags |= QMF_NOTIFY;
	close.onReleased = VoidCb( &CMenuCoFUnlockables::Hide );
	close.SetCoord( 72, 280 );

	if( !theme )
		AddItem( banner );

	AddItem( list );
	AddItem( hint );
	AddItem( gallery );
	AddItem( close );
}

void CMenuCoFUnlockables::_VidInit( void )
{
	if( !IsPanel( ))
		return;

	Rect c = ContentRect();
	const int hintH = THEME_LABEL_TALL * 2 + 4;

	list.SetRect( c.pt.x, c.pt.y, c.sz.w, c.sz.h - hintH - 10 );
	hint.SetRect( c.pt.x, c.pt.y + c.sz.h - hintH, c.sz.w, hintH );

	CMenuPicButton *row[] = { &gallery, &close };
	LayoutButtonRow( row, 2 );
}

ADD_MENU( menu_cofunlockables, CMenuCoFUnlockables, UI_CoFUnlockables_Menu );

// ---------------------------------------------------------------------------
// death page (command menu_cofdeath, opened by the engine)
//
// Measured, in the shipped binaries and in the engine fixture
// (docs/cof-ui-death-flow.md):
//
//   * dying sends one user message, "VGUIMenu" with a first byte of 35;
//     client.dll's CClientViewport::ShowVGUIMenu maps index 35 onto CGameOver
//     (ctor at VA 10040620, the panel that loads gfx/vgui/game_over*.tga and
//     plays game_over.mp3), and nothing else in the game sends index 35;
//   * the panel is "GAME OVER" over the still-running scene with two text
//     links, LOAD GAME and EXIT, each on its own faint dark strip. No panel,
//     no scrim, and the world keeps rendering;
//   * LOAD GAME (command id 1, CGameOverHandler_Command::actionPerformed at
//     VA 10040B40) runs no console command in single player: it hides itself
//     and shows the client's own save/load panel (gViewport+0x1464). In coop
//     (the [105431DC] arm) it is a RESTART instead and issues "map <level>";
//   * EXIT (command id 2) hides itself and issues, through pfnClientCmd,
//     "map c_game_menu1" (the push at VA 10040B74) - the very command
//     cof_ui_menu_map_redirect already turns into disconnect + menu_main +
//     map_background;
//   * nothing else follows. No stufftext arrives and the game never returns to
//     the menu map by itself, so there is no auto-return to fight.
//
// This page is that screen in our theme: the same structure, the same two
// choices, the same end states.
// ---------------------------------------------------------------------------

/*
=================
CMenuCoFDeathTitle

"GAME OVER" in the main menu's wordmark handle, centred, on its own backing
strip. Tracked like the wordmark, because UI_DrawString cannot add advance.
=================
*/
class CMenuCoFDeathTitle : public CMenuBaseItem
{
public:
	CMenuCoFDeathTitle()
	{
		iFlags = QMF_INACTIVE;
		szName = "GAME OVER";
	}

	void Draw() override;

	static int Tall() { return THEME_LOGO_TALL; }
};

void CMenuCoFDeathTitle::Draw()
{
	const int tall  = (int)( THEME_LOGO_TALL * uiStatic.scaleY );
	const int track = (int)( THEME_LOGO_TRACKING * uiStatic.scaleX );
	const int w = UI_ThemeTrackedWidth( uiStatic.hThemeLogo, szName, tall, track );
	const int x = m_scPos.x + ( m_scSize.w - w ) / 2;

	UI_ThemeDeathBacking( x, m_scPos.y, w, tall );
	UI_ThemeDrawTracked( uiStatic.hThemeLogo, x, m_scPos.y, szName, THEME_TEXT_HI, tall, track );
}

/*
=================
CMenuCoFDeathItem

A main-menu style item with the original's backing strip behind its own text.
Everything else - hover, keyboard focus, the click - is the stock pic button.
=================
*/
class CMenuCoFDeathItem : public CMenuPicButton
{
public:
	void Draw() override
	{
		if( UI_ThemeActive( ))
		{
			int w = g_FontMgr->GetTextWideScaled( font, szName, m_scChSize );
			int x = m_scPos.x + ( m_scSize.w - w ) / 2;

			UI_ThemeDeathBacking( x, m_scPos.y + ( m_scSize.h - m_scChSize ) / 2, w, m_scChSize );
		}

		CMenuPicButton::Draw();
	}
};

class CMenuCoFDeath : public CMenuFramework
{
public:
	CMenuCoFDeath() : CMenuFramework( "CMenuCoFDeath" ) { }

	void Show() override;
	void Hide() override;
	void Think() override;

	// Escape has nowhere to go from here: the session is over and the only
	// window on the stack is this one. Swallow it rather than drop the player
	// back onto the client's own panel with no way to reach either choice.
	bool KeyDown( int key ) override
	{
		if( UI::Key::IsEscape( key ))
			return true;

		return CMenuFramework::KeyDown( key );
	}

private:
	void _Init() override;
	void _VidInit() override;

	void LoadCb();
	void ExitCb();

	CMenuCoFDeathTitle title;
	CMenuCoFDeathItem  load;
	CMenuCoFDeathItem  exit;
};

/*
=================
CMenuCoFDeath::LoadCb

The original's LOAD GAME opened the client's own save/load panel, which is
gated off with the rest of the client layer while our menu has focus. Our Load
Game page is the replacement, pushed over this one so its Cancel comes back
here - which is what the original did too (the client re-shows CGameOver from
its save/load panel's back button while the player is still dead, client.dll
VA 100315B0).
=================
*/
void CMenuCoFDeath::LoadCb()
{
	UI_LoadGame_Menu();
}

/*
=================
CMenuCoFDeath::ExitCb

The original's EXIT issued "map c_game_menu1" through pfnClientCmd, which the
engine's cof_ui_menu_map_redirect turns into disconnect + menu_main +
map_background. Emit exactly that, so this button and the original link take
the same path and reach the same state; with the redirect switched off, ask the
engine for the return sequence directly instead of loading the old menu map.
=================
*/
void CMenuCoFDeath::ExitCb()
{
	if( EngFuncs::GetCvarFloat( "cof_ui_menu_map_redirect" ) != 0.0f )
		EngFuncs::ClientCmd( false, "map c_game_menu1\n" );
	else
		EngFuncs::ClientCmd( false, "cof_ui_menu_return\n" );
}

/*
=================
CMenuCoFDeath::Show

REVERSED 2026-09-22, by the user's own play feedback. This page used to issue
the client's "stopmp3" here and once more on its first Think frame, to silence
game_over.mp3 - measured ordering, docs/cof-ui-death-flow.md section 1.1: the
client's CGameOver::setVisible (client.dll VA 10040E60) starts that track while
it handles the same "VGUIMenu" message the engine latched, so a stop issued from
Show() always landed after it.

That was the wrong call. The original death screen is a panel over a still
running game: the ambience, the rain, a chainsaw that was already running and
the death music all keep playing while the player reads the screen. Silencing
the music made our page feel like a freeze-frame, which is exactly what the
user reported. Both calls are gone, and the engine half of the same fix
(cof_ui_death_keep_running) stops the engine pausing the world and muting the
mixer while this page owns input - see docs/cof-ui-death-flow.md section 6.

Nothing replaces them: the page now leaves the client's MP3 player alone. The
Load Game and New Game pages still issue their own "stopmp3", which is a
different question (music bleeding across a transition, cof_mp3_stop_on_map).
=================
*/
void CMenuCoFDeath::Show()
{
	// no scrim, no panel: the scene stays exactly as the player left it
	UI_ThemeSetSceneUnveiled( true );

	CMenuFramework::Show();
}

void CMenuCoFDeath::Hide()
{
	UI_ThemeSetSceneUnveiled( false );
	CMenuFramework::Hide();
}

/*
=================
CMenuCoFDeath::Think

Close as soon as the session this page belongs to is gone. Loading a save goes
through UI_CloseMenu, which cleans the whole stack, but Exit only queues a
disconnect: the redirect's own "menu_main" then becomes the current window and
this one would sit behind it forever, still holding the scrim suppression.
=================
*/
void CMenuCoFDeath::Think()
{
	if( !EngFuncs::ClientInGame( ) || EngFuncs::GetCvarFloat( "cl_background" ) != 0.0f )
	{
		UI_ThemeSetSceneUnveiled( false );

		if( uiStatic.menu.Count() > 1 )
			Hide();

		return;
	}

	CMenuFramework::Think();
}

void CMenuCoFDeath::_Init( void )
{
	banner.szName = L( "Game over" );

	load.szName = L( "Load Game" );
	load.SetPicture( PC_LOAD_GAME );
	load.iFlags |= QMF_NOTIFY;
	load.onReleased = VoidCb( &CMenuCoFDeath::LoadCb );

	exit.szName = L( "Exit" );
	exit.SetPicture( PC_QUIT );
	exit.iFlags |= QMF_NOTIFY;
	exit.onReleased = VoidCb( &CMenuCoFDeath::ExitCb );

	if( !UI_ThemeActive( ))
		AddItem( banner );
	else
		AddItem( title );

	AddItem( load );
	AddItem( exit );
}

void CMenuCoFDeath::_VidInit( void )
{
	if( !UI_ThemeActive( ))
	{
		load.SetCoord( 72, 230 );
		exit.SetCoord( 72, 280 );
		return;
	}

	const int chSize = (int)( THEME_ITEM_TALL * uiStatic.scaleY );
	CMenuCoFDeathItem *items[] = { &load, &exit };
	int y = ( 768 * THEME_DEATH_ITEM_PCT ) / 100;

	title.SetRect( 0, ( 768 * THEME_DEATH_TITLE_PCT ) / 100,
		uiStatic.width, CMenuCoFDeathTitle::Tall( ));

	for( int i = 0; i < 2; i++ )
	{
		int w = THEME_BTN_MIN_W;

		if( uiStatic.scaleX > 0.0f )
			w = (int)( g_FontMgr->GetTextWideScaled( uiStatic.hThemeItem,
				items[i]->szName, chSize ) / uiStatic.scaleX ) + 12;

		items[i]->eThemeStyle = CMenuPicButton::THEME_PLAIN;
		items[i]->eTextAlignment = QM_CENTER;
		items[i]->SetRect(( uiStatic.width - w ) / 2, y, w, THEME_DEATH_PITCH );
		y += THEME_DEATH_PITCH;
	}
}

ADD_MENU( menu_cofdeath, CMenuCoFDeath, UI_CoFDeath_Menu );
