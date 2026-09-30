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
#include "Bitmap.h"
#include "PicButton.h"
#include "Action.h"
#include "CheckBox.h"

#define ART_BANNER		"gfx/shell/head_saveload"

class CMenuSaveLoad : public CMenuFramework
{
public:
	CMenuSaveLoad() : CMenuFramework( "CMenuSaveLoad" ), saveGame( NULL ), m_bCoF( false ) { }

	// the pause-menu-saves switch now lives on the Game options page, so
	// re-read it every time this menu is opened
	void Show() override
	{
		CMenuFramework::Show();

		if( m_bCoF && saveGame )
		{
			cofMenuSaves.UpdateEditable();
			saveGame->SetVisibility( cofMenuSaves.bChecked );
		}
	}
private:
	void _Init();
	void _VidInit();
	void CofMenuSavesChanged();
	void UpdateCofMenuSavesLabel();

	CMenuAction	hintMessage;
	CMenuPicButton *saveGame;
	CMenuPicButton loadRow;
	CMenuPicButton saveRow;
	CMenuPicButton close;
	CMenuCheckBox cofMenuSaves;
	bool		m_bCoF;
	char		cofMenuSavesLabel[CS_SIZE];
	char		hintText[MAX_HINT_TEXT];
};

/*
=================
UI_SaveLoad_Init
=================
*/
void CMenuSaveLoad::_Init( void )
{
	char gameDir[256];
	EngFuncs::GetGameDir( gameDir );
	char *base = strrchr( gameDir, '/' );
	if( !base ) base = strrchr( gameDir, '\\' );
	const bool cof = !stricmp( base ? base + 1 : gameDir, "cryoffear" );

	m_bCoF = cof && UI_ThemeActive();

	if( cof && UI_ThemeActive( ))
		Q_strncpy( hintText, "Cry of Fear uses five shared save slots. Turn on \"Pause menu saves\" in Options > Game to save from this menu.", sizeof( hintText ));
	else if( cof )
		Q_strncpy( hintText, "Cry of Fear uses five shared save slots. Enable pause-menu saves to save from this menu.", sizeof( hintText ));
	else
		snprintf( hintText, sizeof( hintText ),
			L( "During play, you can quickly save your game by pressing %s.\nLoad this game again by pressing %s." ),
			EngFuncs::KeynumToString( KEY_GetKey( "save quick" ) ),
			EngFuncs::KeynumToString( KEY_GetKey( "load quick" ) ) );

	banner.SetPicture( ART_BANNER );

	hintMessage.iFlags = QMF_INACTIVE;
	hintMessage.colorBase = uiColorHelp;
	hintMessage.SetCharSize( QM_SMALLFONT );
	hintMessage.szName = hintText;
	hintMessage.SetCoord( 360, 480 );

	if( UI_ThemeActive( ))
	{
		SetPanel( "SAVE / LOAD", 480, 300 );

		loadRow.SetNameAndStatus( L( "GameUI_LoadGame" ), L( "GameUI_LoadGameHelp" ));
		loadRow.iFlags |= QMF_NOTIFY;
		loadRow.eThemeStyle = CMenuPicButton::THEME_ROW;
		loadRow.eTextAlignment = QM_LEFT;
		loadRow.onReleased = UI_LoadGame_Menu;
		AddItem( loadRow );

		saveRow.SetNameAndStatus( L( "GameUI_SaveGame" ), L( "GameUI_SaveGameHelp" ));
		saveRow.iFlags |= QMF_NOTIFY;
		saveRow.eThemeStyle = CMenuPicButton::THEME_ROW;
		saveRow.eTextAlignment = QM_LEFT;
		saveRow.onReleased = UI_SaveGame_Menu;
		AddItem( saveRow );
		saveGame = &saveRow;

		close.szName = L( "Close" );
		close.SetPicture( PC_DONE );
		close.iFlags |= QMF_NOTIFY;
		close.onReleased = VoidCb( &CMenuSaveLoad::Hide );
		AddItem( close );

		if( cof )
		{
			// the switch itself moved to the top-level Game options page; this
			// menu only reads it, to decide whether Save Game is offered
			cofMenuSaves.LinkCvar( "cof_pause_menu_saves" );
			cofMenuSaves.UpdateEditable();
			UpdateCofMenuSavesLabel();
			cofMenuSaves.SetVisibility( false );
			saveGame->SetVisibility( cofMenuSaves.bChecked );
			AddItem( cofMenuSaves );
		}

		AddItem( hintMessage );
		return;
	}

	AddItem( banner );
	AddButton( L( "GameUI_LoadGame" ), L( "GameUI_LoadGameHelp" ), PC_LOAD_GAME, UI_LoadGame_Menu, QMF_NOTIFY );
	saveGame = AddButton( L( "GameUI_SaveGame" ), L( "GameUI_SaveGameHelp" ), PC_SAVE_GAME, UI_SaveGame_Menu, QMF_NOTIFY );
	if( cof )
	{
		cofMenuSaves.LinkCvar( "cof_pause_menu_saves" );
		cofMenuSaves.UpdateEditable();
		UpdateCofMenuSavesLabel();
		cofMenuSaves.onChanged = VoidCb( &CMenuSaveLoad::CofMenuSavesChanged );
		cofMenuSaves.SetCoord( 72, 380 );
		saveGame->SetVisibility( cofMenuSaves.bChecked );
		AddItem( cofMenuSaves );
	}
	AddButton( L( "Done" ), L( "Go back to the Main menu" ), PC_DONE, VoidCb( &CMenuSaveLoad::Hide ), QMF_NOTIFY );
	AddItem( hintMessage );
}

void CMenuSaveLoad::_VidInit()
{
	if( !IsPanel( ))
		return;

	Rect c = ContentRect();

	loadRow.SetRect( c.pt.x, c.pt.y, c.sz.w, THEME_ROW_H );
	saveRow.SetRect( c.pt.x, c.pt.y + THEME_ROW_PITCH, c.sz.w, THEME_ROW_H );
	hintMessage.SetRect( c.pt.x, c.pt.y + THEME_ROW_PITCH * 2 + 14,
		c.sz.w, THEME_LABEL_TALL * 3 );

	CMenuPicButton *btns[] = { &close };
	LayoutButtonRow( btns, 1 );
}

void CMenuSaveLoad::CofMenuSavesChanged()
{
	// This is an opt-in feature, so make the change durable as soon as it is
	// selected and immediately expose or hide the Save Game button.
	cofMenuSaves.WriteCvar();
	UpdateCofMenuSavesLabel();
	saveGame->SetVisibility( cofMenuSaves.bChecked );
}

void CMenuSaveLoad::UpdateCofMenuSavesLabel()
{
	// the [ON ]/[OFF] marker was only needed because Cry of Fear ships no
	// gfx/shell/cb_* artwork and the stock checkbox drew nothing at all; the
	// theme draws the box from primitives
	if( UI_ThemeActive( ))
		Q_strncpy( cofMenuSavesLabel, L( "Enable pause-menu saves" ), sizeof( cofMenuSavesLabel ));
	else
		snprintf( cofMenuSavesLabel, sizeof( cofMenuSavesLabel ), "%s %s",
			cofMenuSaves.bChecked ? "[ON ]" : "[OFF]", L( "Enable pause-menu saves" ));

	cofMenuSaves.szName = cofMenuSavesLabel;
}

ADD_MENU( menu_saveload, CMenuSaveLoad, UI_SaveLoad_Menu );
