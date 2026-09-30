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


#include "Framework.h"
#include "Action.h"
#include "Bitmap.h"
#include "PicButton.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "MenuStrings.h"
#include "PlayerIntroduceDialog.h"
#include "gameinfo.h"
#include "AnimatedBanner.h"
#include "MovieBanner.h"
#include "CryOfFear.h"
#include "CoFOptions.h"

// the Game options page switch "Pause menu saves" (menus/AdvancedControls.cpp,
// archived cvar registered by menus/LoadGame.cpp)
static bool CoFPauseSavesEnabled( void )
{
	return EngFuncs::GetCvarFloat( "cof_pause_menu_saves" ) != 0.0f;
}

// co-op bridge: a multiplayer session (host or joiner). The stock pause menu's
// own test (CMenuMain::VidInit, "single = gpGlobals->maxClients < 2"); the
// engine refuses saves there anyway ("Can't save multiplayer games.")
static bool CoFInMultiplayer( void )
{
	return gpGlobals->maxClients > 1;
}

// the Game options switch "Quick saves" (engine cof_quick_saves)
static bool CoFQuickSavesEnabled( void )
{
	return EngFuncs::GetCvarFloat( "cof_quick_saves" ) != 0.0f;
}

// the pause list's "Quick save": the engine's cof_quicksave, then back to the
// game. The command runs from the command buffer on the next frame, when the
// menu is already closed, so it is not refused as "menu open".
static void UI_CoFQuickSave( void )
{
	EngFuncs::ClientCmd( false, "cof_quicksave\n" );
	UI_CloseMenu();
}

#define ART_MINIMIZE_N	"gfx/shell/min_n"
#define ART_MINIMIZE_F	"gfx/shell/min_f"
#define ART_MINIMIZE_D	"gfx/shell/min_d"
#define ART_CLOSEBTN_N	"gfx/shell/cls_n"
#define ART_CLOSEBTN_F	"gfx/shell/cls_f"
#define ART_CLOSEBTN_D	"gfx/shell/cls_d"

/*
=================
CMenuCoFLogo

The main menu's text logo: "CRY OF FEAR" in the theme's largest handle over
"ENHANCED" in a much smaller one, both left-aligned on the item list's own
margin. Text only - no bitmap - and main menu only, never the pause menu.

UI_DrawString advances strictly by DrawCharacter's return value, so the tracking
the wordmark wants is not expressible through it; UI_ThemeDrawTracked walks the
string itself and adds the extra advance.
=================
*/
class CMenuCoFLogo : public CMenuBaseItem
{
public:
	CMenuCoFLogo()
	{
		iFlags = QMF_INACTIVE;
		szName = "CRY OF FEAR";
		bCentered = false;
	}

	void Draw() override;

	// centre both lines inside the item rect instead of left-aligning them
	bool bCentered;

	static const char *Title() { return "CRY OF FEAR"; }
	static const char *Sub()   { return "ENHANCED"; }

	// height of the whole two-line block, in virtual units
	static int BlockHeight() { return THEME_LOGO_TALL + 6 + THEME_LOGOSUB_TALL; }
};

void CMenuCoFLogo::Draw()
{
	const int tall    = (int)( THEME_LOGO_TALL * uiStatic.scaleY );
	const int subTall = (int)( THEME_LOGOSUB_TALL * uiStatic.scaleY );
	const int gap     = (int)( 6 * uiStatic.scaleY );
	const int track   = (int)( THEME_LOGO_TRACKING * uiStatic.scaleX );
	int x1 = m_scPos.x, x2 = m_scPos.x;

	if( bCentered )
	{
		x1 += ( m_scSize.w - UI_ThemeTrackedWidth( uiStatic.hThemeLogo, Title(), tall, track )) / 2;
		x2 += ( m_scSize.w - UI_ThemeTrackedWidth( uiStatic.hThemeLogoSub, Sub(), subTall, track / 2 )) / 2;
	}

	UI_ThemeDrawTracked( uiStatic.hThemeLogo, x1, m_scPos.y,
		Title(), THEME_TEXT_HI, tall, track );

	UI_ThemeDrawTracked( uiStatic.hThemeLogoSub, x2, m_scPos.y + tall + gap,
		Sub(), THEME_TEXT_DIM, subTall, track / 2 );
}

class CMenuMain: public CMenuFramework
{
public:
	CMenuMain() : CMenuFramework( "CMenuMain" ) { }

	bool KeyDown( int key ) override;

	void QuitDialogCb();

	// console entry point for the second level of the Cry of Fear main list:
	// that list is switched by a mouse click or by Enter on the Extras item,
	// and this project may not inject either, so the level is also reachable
	// as a command. Same pattern as the engine's cof_ui_menu_panel_probe.
	void CoFShowLevel( int level ) { if( bCoF ) CoFSetLevel( level ); }

	// console entry point for Quit to menu, for the same reason: the real way
	// in is a click on that item and then on the confirmation's positive
	// button. It runs exactly the callback the dialog runs.
	void CoFQuitToMenu() { DisconnectCb(); }

private:
	void _Init() override;
	void _VidInit( ) override;
	void Think() override;

	void VidInit(bool connected);

	// Cry of Fear replaces the stock item set with its own, see InitCoF().
	void InitCoF();
	void VidInitCoF( bool connected );
	// Cry of Fear's main list has a second level: Extras swaps the list in
	// place, with the wordmark staying where it is. See CoFSetLevel().
	void CoFSetLevel( int level );
	void CoFExtrasCb();
	void CoFBackCb();

	void DisconnectCb();
	void DisconnectDialogCb();
	// co-op bridge: the pause list was last laid out for a multiplayer
	// session (Think lays it out again when that changes)
	bool bCoFLaidOutMP;
	void HazardCourseDialogCb();
	void HazardCourseCb();

	CMenuAnimatedBanner animatedBanner;
	CMenuMovieBanner movieBanner;

	CMenuPicButton	console;
	CMenuPicButton	resumeGame;
	CMenuPicButton	disconnect;
	CMenuPicButton	newGame;
	CMenuPicButton	hazardCourse;
	CMenuPicButton	configuration;
	CMenuPicButton	saveRestore;
	CMenuPicButton	multiPlayer;
	CMenuPicButton	customGame;
	CMenuPicButton	previews;
	CMenuPicButton	quit;

	// Cry of Fear main-menu items
	CMenuPicButton	cofCampaign;
	CMenuPicButton	cofJoinServer;
	CMenuPicButton	cofHostServer;
	CMenuPicButton	cofLanguage;
	CMenuPicButton	cofUnlockables;
	CMenuPicButton	cofExtras;
	CMenuPicButton	cofLinks;
	CMenuPicButton	cofCredits;
	CMenuPicButton	cofBack;
	CMenuPicButton	cofQuitToMenu;
	// pause list: "Save Game" of its own, shown only while pause-menu saves
	// are enabled (cof_pause_menu_saves); saveRestore is "Load Game" there
	CMenuPicButton	cofSaveGame;
	// pause list: "Quick save" (cof_quicksave), the FIRST item while quick
	// saves are on (cof_quick_saves), single player only
	CMenuPicButton	cofQuickSave;
	CMenuCoFLogo	cofLogo;

	// buttons on top right. Maybe should be drawn if fullscreen == 1?
	CMenuBitmap	minimizeBtn;
	CMenuBitmap	quitButton;

	// quit dialog
	CMenuYesNoMessageBox dialog;

	bool bTrainMap;
	bool bCustomGame;
	bool bCoF;
	bool bCoFWantBackgroundMap;
	int  iCoFBackgroundMapDelay;
	// 0 = the main list, 1 = the Extras list. Pause menu always 0.
	int  iCoFLevel;
};

// Cry of Fear main list levels
#define COF_LEVEL_MAIN   0
#define COF_LEVEL_EXTRAS 1

// Frames the engine has to look completely idle before Quit to menu is allowed
// to bring the background map back. A level change that is only queued is not
// visible in any cvar yet, so wait a moment instead of racing it.
#define COF_BACKGROUND_RESTART_DELAY 30

void CMenuMain::QuitDialogCb()
{
	if( CL_IsActive() && EngFuncs::GetCvarFloat( "host_serverstate" ) && EngFuncs::GetCvarFloat( "maxplayers" ) == 1.0f )
		dialog.SetMessage( L( "StringsList_235" ) );
	else
		dialog.SetMessage( L( "GameUI_QuitConfirmationText" ) );

	dialog.szPanelTitle = "QUIT";
	if( UI_ThemeActive( ))
		dialog.SetPositiveButton( L( "GameUI_GameMenu_Quit" ), PC_QUIT );
	dialog.onPositive.SetCommand( false, "quit \"menu dialog\"\n" );
	dialog.Show();
}

void CMenuMain::DisconnectCb()
{
	EngFuncs::ClientCmd( false, "disconnect\n" );
	// mainui only ever starts a background map once per process
	// (BaseMenu.cpp, the "first" latch in UI_UpdateMenu). Cry of Fear's menu
	// lives on that map, so remember to bring it back as soon as the
	// disconnect has actually gone through; Think() does it.
	bCoFWantBackgroundMap = bCoF;
	iCoFBackgroundMapDelay = COF_BACKGROUND_RESTART_DELAY;
	VidInit( false );
	CalcPosition();
	CalcSizes();
	VidInitItems();
}

void CMenuMain::DisconnectDialogCb()
{
	// co-op bridge: in multiplayer the same item reads "Disconnect"
	const bool mp = bCoF && CL_IsActive( ) && CoFInMultiplayer( );

	dialog.szPanelTitle = mp ? "DISCONNECT" : "QUIT TO MENU";
	dialog.onPositive = VoidCb( &CMenuMain::DisconnectCb );
	dialog.SetMessage( mp ? L( "Leave the co-op game and return to the main menu?" ) : L( "Really disconnect?" ) );
	dialog.Show();
}

void CMenuMain::HazardCourseDialogCb()
{
	dialog.onPositive = VoidCb( &CMenuMain::HazardCourseCb );;
	dialog.SetMessage( L( "StringsList_234" ) );
	dialog.Show();
}

/*
=================
CMenuMain::Key
=================
*/
bool CMenuMain::KeyDown( int key )
{
	if( UI::Key::IsEscape( key ) )
	{
		// Cry of Fear second-level list: Escape is Back, not Quit. Only the
		// main menu ever has a second level (the pause list stays flat), and
		// the dialog owns Escape while it is up.
		if( bCoF && iCoFLevel != COF_LEVEL_MAIN && !CL_IsActive( ) && !dialog.IsVisible( ))
		{
			CoFBackCb();
			return true;
		}

		if ( CL_IsActive( ))
		{
			if( !dialog.IsVisible() )
				UI_CloseMenu();
		}
		else
		{
			QuitDialogCb( );
		}
		return true;
	}
	return CMenuFramework::KeyDown( key );
}

/*
=================
UI_Main_HazardCourse
=================
*/
void CMenuMain::HazardCourseCb()
{
	if( EngFuncs::GetCvarFloat( "host_serverstate" ) && EngFuncs::GetCvarFloat( "maxplayers" ) > 1 )
		EngFuncs::HostEndGame( "end of the game" );

	EngFuncs::CvarSetValue( "skill", 1.0f );
	EngFuncs::CvarSetValue( "deathmatch", 0.0f );
	EngFuncs::CvarSetValue( "teamplay", 0.0f );
	EngFuncs::CvarSetValue( "pausable", 1.0f ); // singleplayer is always allowing pause
	EngFuncs::CvarSetValue( "coop", 0.0f );
	EngFuncs::CvarSetValue( "maxplayers", 1.0f ); // singleplayer

	EngFuncs::PlayBackgroundTrack( NULL, NULL );

	EngFuncs::ClientCmd( false, "hazardcourse\n" );
}

void CMenuMain::_Init( void )
{
	bCoF = UI_IsCryOfFear();
	bCoFWantBackgroundMap = false;
	bCoFLaidOutMP = false;
	iCoFBackgroundMapDelay = COF_BACKGROUND_RESTART_DELAY;
	iCoFLevel = COF_LEVEL_MAIN;

	if( gMenu.m_gameinfo.trainmap[0] && stricmp( gMenu.m_gameinfo.trainmap, gMenu.m_gameinfo.startmap ) != 0 )
		bTrainMap = true;
	else bTrainMap = false;

	if( EngFuncs::GetCvarFloat( "host_allow_changegame" ))
		bCustomGame = true;
	else bCustomGame = false;

	// console
	console.SetNameAndStatus( L( "GameUI_Console" ), L( "Show console" ) );
	console.iFlags |= QMF_NOTIFY;
	console.SetPicture( PC_CONSOLE );
	console.SetVisibility( gpGlobals->developer );
	SET_EVENT_MULTI( console.onReleased,
	{
		UI_SetActiveMenu( false );
		EngFuncs::KEY_SetDest( KEY_CONSOLE );
	});

	resumeGame.SetNameAndStatus( L( "GameUI_GameMenu_ResumeGame" ), L( "StringsList_188" ) );
	resumeGame.SetPicture( PC_RESUME_GAME );
	resumeGame.iFlags |= QMF_NOTIFY;
	resumeGame.onReleased = UI_CloseMenu;

	disconnect.SetNameAndStatus( L( "GameUI_GameMenu_Disconnect" ), L( "Disconnect from server" ) );
	disconnect.SetPicture( PC_DISCONNECT );
	disconnect.iFlags |= QMF_NOTIFY;
	disconnect.onReleased = VoidCb( &CMenuMain::DisconnectDialogCb );

	newGame.SetNameAndStatus( L( "GameUI_NewGame" ), L( "StringsList_189" ) );
	newGame.SetPicture( PC_NEW_GAME );
	newGame.iFlags |= QMF_NOTIFY;
	newGame.onReleased = UI_NewGame_Menu;

	hazardCourse.SetNameAndStatus( L( "GameUI_TrainingRoom" ), L( "StringsList_190" ) );
	hazardCourse.SetPicture( PC_HAZARD_COURSE );
	hazardCourse.iFlags |= QMF_NOTIFY;
	hazardCourse.onReleasedClActive = VoidCb( &CMenuMain::HazardCourseDialogCb );
	hazardCourse.onReleased = VoidCb( &CMenuMain::HazardCourseCb );

	multiPlayer.SetNameAndStatus( L( "GameUI_Multiplayer" ), L( "StringsList_198" ) );
	multiPlayer.SetPicture( PC_MULTIPLAYER );
	multiPlayer.iFlags |= QMF_NOTIFY;
	multiPlayer.onReleased = UI_MultiPlayer_Menu;

	configuration.SetNameAndStatus( L( "GameUI_Options" ), L( "StringsList_193" ) );
	configuration.SetPicture( PC_CONFIG );
	configuration.iFlags |= QMF_NOTIFY;
	configuration.onReleased = UI_Options_Menu;

	saveRestore.iFlags |= QMF_NOTIFY;

	customGame.SetNameAndStatus( L( "GameUI_ChangeGame" ), L( "StringsList_530" ) );
	customGame.SetPicture( PC_CUSTOM_GAME );
	customGame.iFlags |= QMF_NOTIFY;
	customGame.onReleased = UI_CustomGame_Menu;

	previews.SetNameAndStatus( L( "Previews" ), L( "StringsList_400" ) );
	previews.SetPicture( PC_PREVIEWS );
	previews.iFlags |= QMF_NOTIFY;
	SET_EVENT( previews.onReleased, EngFuncs::ShellExecute( MenuStrings[ IDS_MEDIA_PREVIEWURL ], NULL, false ) );

	quit.SetNameAndStatus( L( "GameUI_GameMenu_Quit" ), L( "GameUI_QuitConfirmationText" ) );
	quit.SetPicture( PC_QUIT );
	quit.iFlags |= QMF_NOTIFY;
	quit.onReleased = VoidCb( &CMenuMain::QuitDialogCb );

	quitButton.SetPicture( ART_CLOSEBTN_N, ART_CLOSEBTN_F, ART_CLOSEBTN_D );
	quitButton.iFlags = QMF_MOUSEONLY;
	quitButton.eFocusAnimation = QM_HIGHLIGHTIFFOCUS;
	quitButton.onReleased = VoidCb( &CMenuMain::QuitDialogCb );

	minimizeBtn.SetPicture( ART_MINIMIZE_N, ART_MINIMIZE_F, ART_MINIMIZE_D );
	minimizeBtn.iFlags = QMF_MOUSEONLY;
	minimizeBtn.eFocusAnimation = QM_HIGHLIGHTIFFOCUS;
	minimizeBtn.onReleased.SetCommand( false, "minimize\n" );

	if ( gMenu.m_gameinfo.gamemode == GAME_MULTIPLAYER_ONLY || gMenu.m_gameinfo.startmap[0] == 0 )
		newGame.SetGrayed( true );

	if ( gMenu.m_gameinfo.gamemode == GAME_SINGLEPLAYER_ONLY )
		multiPlayer.SetGrayed( true );

	if ( gMenu.m_gameinfo.gamemode == GAME_MULTIPLAYER_ONLY )
	{
		saveRestore.SetGrayed( true );
		hazardCourse.SetGrayed( true );
	}

	// too short execute string - not a real command
	if( strlen( MenuStrings[IDS_MEDIA_PREVIEWURL] ) <= 3 )
	{
		previews.SetGrayed( true );
	}

	// server.dll needs for reading savefiles or startup newgame
	if( !EngFuncs::CheckGameDll( ))
	{
		saveRestore.SetGrayed( true );
		hazardCourse.SetGrayed( true );
		newGame.SetGrayed( true );
	}

	// The theme has no banners at all: no animated title, no logo.avi movie
	// banner, no WON banner bitmap.
	if( !UI_ThemeActive( ))
	{
		if( FBitSet( gMenu.m_gameinfo.flags, GFL_ANIMATED_TITLE ))
		{
			if( animatedBanner.TryLoad())
				AddItem( animatedBanner );
		}
		else
		{
			AddItem( movieBanner );
		}
	}

	dialog.Link( this );

	if( bCoF )
	{
		InitCoF();
		return;
	}

	AddItem( banner );
	AddItem( console );
	AddItem( disconnect );
	AddItem( resumeGame );
	AddItem( newGame );

	if ( bTrainMap )
		AddItem( hazardCourse );

	AddItem( configuration );
	AddItem( saveRestore );
	AddItem( multiPlayer );

	if ( bCustomGame )
		AddItem( customGame );

	AddItem( previews );
	AddItem( quit );
	AddItem( minimizeBtn );
	AddItem( quitButton );
}

/*
=================
CMenuMain::InitCoF

Cry of Fear's own main menu is

    New Game, Load Game, Custom Campaign, Join Server, Host Server,
    Language, Unlockables, Extras, Options, Quit

and in game

    Resume, Save Game, Load Game, Options, Quit to menu, Quit

(Save Game only while pause-menu saves are enabled, milestone 5b)

so the stock Console, Training Room, Multiplayer, Previews and Change Game
items are not added at all. Neither are minimizeBtn and quitButton: they are
CMenuBitmaps of gfx/shell/min_n and gfx/shell/cls_n, and Cry of Fear ships no
gfx/shell button art at all (only gfx/shell/kb_act.lst), so CMenuBitmap::Draw
painted the engine's fallback texture - the two white squares in the top right
corner of the menu. The window already has a Quit item and the title bar has
the real buttons.
=================
*/
void CMenuMain::InitCoF( void )
{
	// New Game goes to the Cry of Fear difficulty page instead of mainui's
	// Easy/Medium/Hard page: this game reads the "difficulty" cvar, not
	// "skill", and expects the game DLL's own skillset command.
	newGame.onReleased = UI_CoFDifficulty_Menu;
	newGame.onReleasedClActive = UI_CoFDifficulty_Menu;

	cofCampaign.SetNameAndStatus( L( "Custom Campaign" ), L( "Play one of the campaigns in cryoffear/maps" ) );
	cofCampaign.SetPicture( PC_CUSTOM_GAME );
	cofCampaign.iFlags |= QMF_NOTIFY;
	cofCampaign.onReleased = UI_CoFCampaign_Menu;

	// co-op bridge: the Cry of Fear pages (menus/CoFCoop.cpp), not mainui's
	// Internet browser and Create Server page - the latter issues a
	// "disconnect" that the unified UI turns into a single-player background
	// map, so the game started with single-player rules and no lobby
	cofJoinServer.SetNameAndStatus( L( "Join Server" ), L( "Join a co-op game by address or on the local network" ) );
	cofJoinServer.SetPicture( PC_INET_GAME );
	cofJoinServer.iFlags |= QMF_NOTIFY;
	cofJoinServer.onReleased = UI_CoFJoin_Menu;

	cofHostServer.SetNameAndStatus( L( "Host Server" ), L( "Host a co-op game: story, Manhunt or survival" ) );
	cofHostServer.SetPicture( PC_CREATE_GAME );
	cofHostServer.iFlags |= QMF_NOTIFY;
	cofHostServer.onReleased = UI_CoFHost_Menu;

	// Language left the main list in the 2026-09-22 restructure; it lives on
	// the Game options page and nowhere else. The item stays declared because
	// the layout arrays below are index-free and an unused, never-added item
	// costs nothing, but it is not added and not reachable from here.
	cofLanguage.SetNameAndStatus( L( "Language" ), L( "Choose the subtitle and document language" ) );
	cofLanguage.SetPicture( PC_CUSTOMIZE );
	cofLanguage.iFlags |= QMF_NOTIFY;
	cofLanguage.onReleased = UI_CoFLanguage_Menu;

	// the engine-menu page, not "map c_unlockables": that gallery panel is
	// mispositioned at high resolutions and its MAIN MENU button returns to the
	// client's old menu. The page keeps a button for the original route.
	cofUnlockables.SetNameAndStatus( L( "Unlockables" ), L( "List the unlockables and open the gallery" ) );
	cofUnlockables.SetPicture( PC_VIEW_GAME_INFO );
	cofUnlockables.iFlags |= QMF_NOTIFY;
	cofUnlockables.onReleased = UI_CoFUnlockables_Menu;

	// Extras is now a second level of the same list, not a page: pressing it
	// swaps the main list for Join Server / Host Server / Unlockables / Links /
	// Back, drawn in the same place and the same style with the wordmark left
	// where it is. "Links" is the old Extras page, the collection of URLs.
	cofExtras.SetNameAndStatus( L( "Extras" ), L( "Co-op, unlockables, soundtrack and community links" ) );
	cofExtras.SetPicture( PC_VISIT_WEB_SITE );
	cofExtras.iFlags |= QMF_NOTIFY;
	cofExtras.onReleased = VoidCb( &CMenuMain::CoFExtrasCb );

	cofLinks.SetNameAndStatus( L( "Links" ), L( "Soundtrack, merchandise and community links" ) );
	cofLinks.SetPicture( PC_VISIT_WEB_SITE );
	cofLinks.iFlags |= QMF_NOTIFY;
	cofLinks.onReleased = UI_CoFExtras_Menu;

	cofCredits.SetNameAndStatus( L( "Credits" ), L( "Who made Cry of Fear, this project and the Polish localisation" ) );
	cofCredits.SetPicture( PC_VIEW_GAME_INFO );
	cofCredits.iFlags |= QMF_NOTIFY;
	cofCredits.onReleased = UI_CoFCredits_Menu;

	cofBack.SetNameAndStatus( L( "Back" ), L( "Return to the main menu" ) );
	cofBack.SetPicture( PC_DISCONNECT );
	cofBack.iFlags |= QMF_NOTIFY;
	cofBack.onReleased = VoidCb( &CMenuMain::CoFBackCb );

	cofQuitToMenu.SetNameAndStatus( L( "Quit to menu" ), L( "Leave the game and return to the main menu" ) );
	cofQuitToMenu.SetPicture( PC_DISCONNECT );
	cofQuitToMenu.iFlags |= QMF_NOTIFY;
	cofQuitToMenu.onReleased = VoidCb( &CMenuMain::DisconnectDialogCb );

	// Pause list, milestone 5b (user request 2026-09-22): Save Game and Load
	// Game are two items instead of one "Save\Load Game" page. Both open the
	// shared save/load page; UI_SaveGame_Menu / UI_LoadGame_Menu apply the
	// mode with SetSaveMode() BEFORE Show() (menus/LoadGame.cpp). Save Game is
	// only on the list while pause-menu saves are enabled, see VidInitCoF.
	cofSaveGame.SetNameAndStatus( L( "GameUI_SaveGame" ), L( "Save into one of the five shared Cry of Fear slots" ) );
	cofSaveGame.SetPicture( PC_SAVE_GAME );
	cofSaveGame.iFlags |= QMF_NOTIFY;
	cofSaveGame.onReleased = UI_SaveGame_Menu;

	cofQuickSave.SetNameAndStatus( L( "Quick save" ), L( "Save to the quick save slot now and return to the game" ) );
	cofQuickSave.SetPicture( PC_SAVE_GAME );
	cofQuickSave.iFlags |= QMF_NOTIFY;
	cofQuickSave.onReleased = UI_CoFQuickSave;

	if( UI_ThemeActive( ))
		AddItem( cofLogo );
	else
		AddItem( banner );

	// AddItem order is also the keyboard navigation order (CMenuItemsHolder::
	// AdjustCursor walks m_pItems and steps over everything invisible), so the
	// two levels are added in the order each is drawn and never interleaved.
	AddItem( cofQuickSave );
	AddItem( resumeGame );
	AddItem( newGame );
	AddItem( cofSaveGame );
	AddItem( saveRestore );
	AddItem( cofCampaign );
	AddItem( cofExtras );
	AddItem( configuration );
	AddItem( cofQuitToMenu );
	AddItem( quit );

	// second level
	AddItem( cofJoinServer );
	AddItem( cofHostServer );
	AddItem( cofUnlockables );
	AddItem( cofLinks );
	AddItem( cofCredits );
	AddItem( cofBack );
}

/*
=================
CMenuMain::CoFSetLevel

Swap the main list for the second level, or back, without leaving CMenuMain.

Everything the swap needs already exists: the items are named members added
once in InitCoF, VidInitCoF decides per frame-size which of them are visible and
where they sit, and both the mouse hit test (CMenuItemsHolder::MouseMove) and
the keyboard cursor (AdjustCursor) skip invisible items on their own. So the
level is one member, and changing it is the same four calls DisconnectCb already
makes after it changes what the list contains.
=================
*/
void CMenuMain::CoFSetLevel( int level )
{
	iCoFLevel = level;

	VidInit( CL_IsActive( ));
	CalcPosition();
	CalcSizes();
	VidInitItems();

	// AdjustCursor wraps, so after a swap the cursor can land anywhere; put it
	// on the first item of the list that is now on screen.
	if( level == COF_LEVEL_EXTRAS )
		SetCursorToItem( cofJoinServer, false );
	else
		SetCursorToItem( newGame, false );
}

void CMenuMain::CoFExtrasCb()
{
	// the options relayout makes Extras one tabbed window (Host co-op, Join
	// co-op, Unlockables, Links, Credits; menus/CoFOptions.cpp)
	if( UI_CoFOptionsLayout( ))
	{
		UI_CoFHost_Menu();
		return;
	}

	CoFSetLevel( COF_LEVEL_EXTRAS );
}

void CMenuMain::CoFBackCb()
{
	CoFSetLevel( COF_LEVEL_MAIN );
}

/*
=================
UI_Main_Init
=================
*/
/*
=================
CMenuMain::VidInitCoF

Ten items do not fit the stock bottom-anchored WON layout, so the Cry of Fear
list runs top down from the usual first-button line instead. Nothing else about
the look changes; styling is a later milestone.
=================
*/
void CMenuMain::VidInitCoF( bool connected )
{
	const int hoffset = ( 70 / 640.0 ) * 1024.0;
	const int ygap = (( 404 - 373 ) / 480.0 ) * 768.0;

	// Only the main menu has a second level; going in game always drops back to
	// the flat pause list, and coming back out starts at the top again.
	if( connected )
		iCoFLevel = COF_LEVEL_MAIN;

	const bool lvlMain   = !connected && iCoFLevel == COF_LEVEL_MAIN;
	const bool lvlExtras = !connected && iCoFLevel == COF_LEVEL_EXTRAS;

	// co-op bridge: in a multiplayer session the pause list has no Save Game
	// and no Load Game (the engine refuses both there, and loading would end
	// everyone's session), and Quit to menu reads Disconnect
	const bool mp = connected && CoFInMultiplayer( );
	bCoFLaidOutMP = mp;

	resumeGame.SetVisibility( connected );
	cofQuitToMenu.SetVisibility( connected );
	if( mp )
		cofQuitToMenu.SetNameAndStatus( L( "Disconnect" ), L( "Leave the co-op game and return to the main menu" ) );
	else
		cofQuitToMenu.SetNameAndStatus( L( "Quit to menu" ), L( "Leave the game and return to the main menu" ) );
	saveRestore.SetVisibility(( connected && !mp ) || lvlMain );
	configuration.SetVisibility( connected || lvlMain );
	quit.SetVisibility( connected || lvlMain );
	newGame.SetVisibility( lvlMain );
	cofCampaign.SetVisibility( lvlMain );
	cofExtras.SetVisibility( lvlMain );
	cofJoinServer.SetVisibility( lvlExtras );
	cofHostServer.SetVisibility( lvlExtras );
	cofUnlockables.SetVisibility( lvlExtras );
	cofLinks.SetVisibility( lvlExtras );
	cofCredits.SetVisibility( lvlExtras );
	cofBack.SetVisibility( lvlExtras );
	// Language left the main list; the Game options page owns it now.
	cofLanguage.SetVisibility( false );
	// Save Game only in game and only with pause-menu saves on; Think() lays
	// the list out again when the option is switched while this menu is open
	cofSaveGame.SetVisibility( connected && !mp && CoFPauseSavesEnabled( ));
	cofQuickSave.SetVisibility( connected && !mp && CoFQuickSavesEnabled( ));

	if( connected )
	{
		// the pause list's Load Game: the shared page in load mode, the
		// same page Save Game opens in save mode
		saveRestore.SetNameAndStatus( L( "GameUI_LoadGame" ), L( "StringsList_191" ) );
		saveRestore.SetPicture( PC_LOAD_GAME );
		saveRestore.onReleased = UI_LoadGame_Menu;
	}
	else
	{
		saveRestore.SetNameAndStatus( L( "GameUI_LoadGame" ), L( "StringsList_191" ) );
		saveRestore.SetPicture( PC_LOAD_GAME );
		saveRestore.onReleased = UI_LoadGame_Menu;
	}

	// The drawing order of both levels, in one array: only the level that is
	// on screen has visible items in it, and the loops below skip the rest, so
	// the geometry always follows the list actually being shown - at every
	// resolution, because the rectangles are recomputed here on every VidInit.
	CMenuPicButton *order[] =
	{
		// pause list, and the first level of the main list
		&cofQuickSave, &resumeGame, &newGame, &cofSaveGame, &saveRestore, &cofCampaign, &cofExtras,
		&configuration, &cofQuitToMenu, &quit,
		// second level of the main list
		&cofJoinServer, &cofHostServer, &cofUnlockables, &cofLinks, &cofCredits,
		&cofBack
	};
	const size_t count = sizeof( order ) / sizeof( order[0] );

	if( UI_ThemeActive( ))
	{
		// Main menu: a centred column, as in Cry of Fear's own menu - every
		// line centred on the screen's horizontal middle, and the wordmark plus
		// the list treated as one group whose centre sits at
		// THEME_MENU_CENTER_PCT of the height.
		//
		// Pause menu: the Source-reference lower-left list, flush to a 64-unit
		// margin, anchored 96 units above the bottom of the 768-unit space.
		//
		// Both give each item a hit box exactly as wide as its own text, so the
		// mouse rectangle and the keyboard cursor follow the drawn position at
		// every resolution.
		int visible = 0;

		for( size_t i = 0; i < count; i++ )
			if( order[i]->IsVisible( )) visible++;

		const bool centered = !connected;
		const int itemsH = visible * THEME_MENU_PITCH;
		const int blockH = CMenuCoFLogo::BlockHeight() + THEME_LOGO_GAP_CENTER + itemsH;
		int y;

		if( centered )
			y = ( 768 * THEME_MENU_CENTER_PCT ) / 100 - blockH / 2
				+ CMenuCoFLogo::BlockHeight() + THEME_LOGO_GAP_CENTER;
		else
			y = 768 - THEME_MENU_BOTTOM - THEME_ITEM_TALL - ( visible - 1 ) * THEME_MENU_PITCH;

		const int firstY = y;
		const int chSize = (int)( THEME_ITEM_TALL * uiStatic.scaleY );

		for( size_t i = 0; i < count; i++ )
		{
			if( !order[i]->IsVisible( ))
				continue;

			int w = THEME_BTN_MIN_W;

			if( uiStatic.scaleX > 0.0f )
				w = (int)( g_FontMgr->GetTextWideScaled( uiStatic.hThemeItem,
					order[i]->szName, chSize ) / uiStatic.scaleX ) + 12;

			order[i]->eThemeStyle = CMenuPicButton::THEME_PLAIN;
			order[i]->eTextAlignment = centered ? QM_CENTER : QM_LEFT;
			order[i]->SetRect( centered ? ( uiStatic.width - w ) / 2 : THEME_MENU_MARGIN,
				y, w, THEME_MENU_PITCH );
			y += THEME_MENU_PITCH;
		}

		// the wordmark belongs to the main menu only, never the pause menu
		cofLogo.SetVisibility( centered );
		cofLogo.bCentered = true;
		cofLogo.SetRect( 0,
			firstY - THEME_LOGO_GAP_CENTER - CMenuCoFLogo::BlockHeight(),
			uiStatic.width, CMenuCoFLogo::BlockHeight( ));
		return;
	}

	int yoffset = 230;
	for( size_t i = 0; i < count; i++ )
	{
		if( !order[i]->IsVisible( ))
			continue;

		order[i]->SetCoord( hoffset, yoffset );
		yoffset += ygap;
	}
}

void CMenuMain::VidInit( bool connected )
{
	if( bCoF )
	{
		VidInitCoF( connected );
		return;
	}

	int hoffset = ( 70 / 640.0 ) * 1024.0;

	// in original menu Previews is located at specific point
	int previews_voffset = ( 404 / 480.0 ) * 768.0;

	// no visible console button gap
	int ygap = (( 404 - 373 ) / 480.0 ) * 768.0;

	// statically positioned items
	minimizeBtn.SetRect( uiStatic.width - 72, 13, 32, 32 );
	quitButton.SetRect( uiStatic.width - 36, 13, 32, 32 );

	previews.SetCoord( hoffset, previews_voffset );
	quit.SetCoord( hoffset, previews_voffset + ygap );

	// let's start calculating positions
	int yoffset = previews_voffset - ygap;

	if( bCustomGame )
	{
		customGame.SetCoord( hoffset, yoffset );
		yoffset -= ygap;
	}

	multiPlayer.SetCoord( hoffset, yoffset );
	yoffset -= ygap;

	bool single = gpGlobals->maxClients < 2;

	saveRestore.SetCoord( hoffset, yoffset );
	yoffset -= ygap;

	configuration.SetCoord( hoffset, yoffset );
	yoffset -= ygap;

	if( bTrainMap )
	{
		hazardCourse.SetCoord( hoffset, yoffset );
		yoffset -= ygap;
	}

	newGame.SetCoord( hoffset, yoffset );
	yoffset -= ygap;

	if( connected )
	{
		resumeGame.SetCoord( hoffset, yoffset );
		yoffset -= ygap;

		if( !single )
		{
			disconnect.SetCoord( hoffset, yoffset );
			yoffset -= ygap;
		}
	}

	console.SetCoord( hoffset, yoffset );
	yoffset -= ygap;

	// now figure out what's visible
	resumeGame.SetVisibility( connected );
	disconnect.SetVisibility( connected && !single );

	if( connected && single )
	{
		saveRestore.SetNameAndStatus( L( "Save\\Load Game" ), L( "StringsList_192" ) );
		saveRestore.SetPicture( PC_SAVE_LOAD_GAME );
		saveRestore.onReleased = UI_SaveLoad_Menu;
	}
	else
	{
		saveRestore.SetNameAndStatus( L( "GameUI_LoadGame" ), L( "StringsList_191" ) );
		saveRestore.SetPicture( PC_LOAD_GAME );
		saveRestore.onReleased = UI_LoadGame_Menu;
	}
}

void CMenuMain::_VidInit()
{
	VidInit( CL_IsActive() );
}

void CMenuMain::Think()
{
	if( bCoF )
	{
		// Quit to menu has disconnected us; once that has actually happened,
		// and nothing else is loading, put Cry of Fear's own menu scene back
		// under the menu.
		// co-op bridge: the scene is already back - the engine re-armed it
		// itself (CL_CoF_Disconnect_f after a single-player disconnect, or
		// cof_ui_session_end after a remote session) - so the request is
		// done. Left standing it would fire during the NEXT connection
		// attempt, which looks exactly as idle as this, and kill it.
		if( bCoFWantBackgroundMap && EngFuncs::GetCvarFloat( "cl_background" ) != 0.0f )
			bCoFWantBackgroundMap = false;

		if( bCoFWantBackgroundMap )
		{
			const bool idle = !EngFuncs::ClientInGame() &&
				EngFuncs::GetCvarFloat( "cl_background" ) == 0.0f &&
				EngFuncs::GetCvarFloat( "host_serverstate" ) == 0.0f;

			// A level change that is still sitting in the command buffer is
			// not visible anywhere yet, so only restart after the engine has
			// looked completely idle for a while; any sign of life restarts
			// the count, and the request never overlaps a real level load.
			if( !idle )
				iCoFBackgroundMapDelay = COF_BACKGROUND_RESTART_DELAY;
			else if( --iCoFBackgroundMapDelay <= 0 )
			{
				bCoFWantBackgroundMap = false;
				UI_StartBackGroundMap();
			}
		}

		// the Game options page can switch pause-menu saves while the pause
		// list is underneath it; when that changed what the list should
		// hold, lay it out again (same four calls as CoFSetLevel)
		if( CL_IsActive( ) && ( bCoFLaidOutMP != CoFInMultiplayer( ) ||
			cofSaveGame.IsVisible( ) != ( CoFPauseSavesEnabled( ) && !CoFInMultiplayer( )) ||
			cofQuickSave.IsVisible( ) != ( CoFQuickSavesEnabled( ) && !CoFInMultiplayer( ))))
		{
			VidInit( true );
			CalcPosition();
			CalcSizes();
			VidInitItems();
		}

		CMenuFramework::Think();
		return;
	}

	if( gpGlobals->developer )
	{
		if( !console.IsVisible( ))
			console.Show();
	}
	else
	{
		if( console.IsVisible( ))
			console.Hide();
	}

	CMenuFramework::Think();
}

ADD_MENU( menu_main, CMenuMain, UI_Main_Menu );

/*
=================
menu_cof_extras_list / menu_cof_main_list

The two levels of the Cry of Fear main list, as console commands. The real way
in is a click on Extras and a click on Back (or Escape), which this project may
not inject; these run exactly the same CoFSetLevel path so a cfg can screenshot
both levels.
=================
*/
static void UI_CoFMainExtrasList( void )
{
	menu_main->Show();
	menu_main->CoFShowLevel( COF_LEVEL_EXTRAS );
}

static void UI_CoFMainMainList( void )
{
	menu_main->Show();
	menu_main->CoFShowLevel( COF_LEVEL_MAIN );
}

static void UI_CoFMainQuitToMenu( void )
{
	menu_main->Show();
	menu_main->CoFQuitToMenu();
}

/*
=================
menu_cof_sound_probe

The menu's own sounds are only ever started by a click, a key or a pointer
moving onto an item, and this project may not inject any of those. This plays
one slot of uiStatic.sounds through exactly the call every control uses -
EngFuncs::PlayLocalSound -> pfnPlaySound -> S_StartLocalSound - so a cfg can
prove what volume the mixer is handed (see ui_sound_volume). Default slot is
SND_LAUNCH, the one every button uses.
=================
*/
static void UI_CoFSoundProbe( void )
{
	int slot = SND_LAUNCH;

	if( EngFuncs::CmdArgc() > 1 )
		slot = atoi( EngFuncs::CmdArgv( 1 ));

	if( slot < 0 || slot >= SND_COUNT )
	{
		Con_Printf( "menu_cof_sound_probe: slot must be 0..%d\n", SND_COUNT - 1 );
		return;
	}

	Con_Printf( "menu_cof_sound_probe: slot %d = \"%s\", ui_sound_volume %.2f\n",
		slot, uiStatic.sounds[slot], EngFuncs::GetCvarFloat( "ui_sound_volume" ));

	EngFuncs::PlayLocalSound( uiStatic.sounds[slot] );
}

ADD_COMMAND( menu_cof_extras_list, UI_CoFMainExtrasList );
ADD_COMMAND( menu_cof_main_list, UI_CoFMainMainList );
ADD_COMMAND( menu_cof_quit_to_menu, UI_CoFMainQuitToMenu );
ADD_COMMAND( menu_cof_sound_probe, UI_CoFSoundProbe );

/*
=================
UI_QuitDialog_f

called by the engine when the OS asks to quit (window close button, Cmd+Q on macOS...)
=================
*/
static void UI_QuitDialog_f( void )
{
	// nothing to lose, quit immediately
	if( !CL_IsActive( ))
	{
		EngFuncs::ClientCmd( false, "quit \"menu\"\n" );
		return;
	}

	if( !UI_IsVisible( ))
		UI_Main_Menu();

	menu_main->QuitDialogCb();
}
ADD_COMMAND( menu_quit, UI_QuitDialog_f );
