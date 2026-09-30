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
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "TabView.h"
#include "CoFOptions.h"

#define ART_BANNER	     	"gfx/shell/head_config"

class CMenuOptions: public CMenuFramework
{
private:
	void _Init( void ) override;
	void _VidInit( void ) override;

	CMenuPicButton row[4];
	CMenuPicButton close;

public:
	typedef CMenuFramework BaseClass;
	CMenuOptions() : CMenuFramework("CMenuOptions") { }

	// update dialog
	CMenuYesNoMessageBox msgBox;
};

/*
=================
CMenuOptions::Init
=================
*/
void CMenuOptions::_Init( void )
{
	banner.SetPicture( ART_BANNER );

	msgBox.SetMessage( L( "Check the Internet for updates?" ) );
	SET_EVENT( msgBox.onPositive, UI_OpenUpdatePage( false, true ) );

	msgBox.Link( this );

	if( UI_ThemeActive( ))
	{
		// Touch, Gamepad and Update have no place in this build's Options:
		// touch is inert, the gamepad page is not part of Cry of Fear's own
		// menu, and the update check points at upstream FWGS.
		//
		// Game is first and is the page the advanced control options, the
		// pause-menu-saves switch and the subtitle language moved onto; it is
		// menus/AdvancedControls.cpp, which is no longer reachable from
		// Controls.
		static const char *names[4]  = { "Game", "Controls", "GameUI_Audio", "GameUI_Video" };
		static const char *hints[4]  =
		{
			"Aiming, mouse look, saves and language",
			"Change keyboard and mouse settings",
			"Change sound volume and quality",
			"Change screen size, video mode and gamma"
		};
		void (*handlers[4])( void ) = { UI_AdvControls_Menu, UI_Controls_Menu, UI_Audio_Menu, UI_Video_Menu };

		SetPanel( "OPTIONS", 420, 340 );

		for( int i = 0; i < 4; i++ )
		{
			row[i].SetNameAndStatus( L( names[i] ), L( hints[i] ));
			row[i].iFlags |= QMF_NOTIFY;
			row[i].eThemeStyle = CMenuPicButton::THEME_ROW;
			row[i].eTextAlignment = QM_LEFT;
			row[i].onReleased = handlers[i];
			AddItem( row[i] );
		}

		close.szName = L( "Close" );
		close.SetPicture( PC_DONE );
		close.iFlags |= QMF_NOTIFY;
		close.onReleased = VoidCb( &CMenuOptions::Hide );
		AddItem( close );
		return;
	}

	AddItem( banner );
	AddButton( L( "Controls" ), L( "Change keyboard and mouse settings" ),
		PC_CONTROLS, UI_Controls_Menu, QMF_NOTIFY );
	AddButton( L( "GameUI_Audio" ), L( "Change sound volume and quality" ),
		PC_AUDIO, UI_Audio_Menu, QMF_NOTIFY );
	AddButton( L( "GameUI_Video" ), L( "Change screen size, video mode and gamma" ),
		PC_VIDEO, UI_Video_Menu, QMF_NOTIFY );
	AddButton( L( "Touch" ), L( "Change touch settings and buttons" ),
		PC_TOUCH, UI_Touch_Menu, QMF_NOTIFY, 't' );
	AddButton( L( "GameUI_Joystick" ), L( "Change gamepad axis and button settings" ),
		PC_GAMEPAD, UI_GamePad_Menu, QMF_NOTIFY, 'g' );
	AddButton( L( "Update" ), L( "Check for updates" ),
		PC_UPDATE, msgBox.MakeOpenEvent(), QMF_NOTIFY );
	AddButton( L( "Done" ), L( "Go back to the Main menu" ),
		PC_DONE, VoidCb( &CMenuOptions::Hide ), QMF_NOTIFY );
}

void CMenuOptions::_VidInit( void )
{
	if( !IsPanel( ))
		return;

	for( int i = 0; i < 4; i++ )
	{
		Rect r = RowRect( i );

		row[i].SetRect( r.pt.x, r.pt.y, r.sz.w, r.sz.h );
	}

	CMenuPicButton *btns[] = { &close };
	LayoutButtonRow( btns, 1 );
}

ADD_MENU3( menu_options, CMenuOptions, UI_Options_Menu );

/*
=================
UI_Options_Menu

Cry of Fear with the theme: Options is one tabbed window (Game, Controls,
Keybinds, Gamepad, Audio, Video; controls/Framework.cpp, menus/CoFOptions.cpp)
and opens on its first tab. The list page above stays the path of the WON
look and of the theme for other games.
=================
*/
void UI_Options_Menu( void )
{
	if( UI_CoFOptionsLayout( ))
	{
		UI_AdvControls_Menu();
		return;
	}

	menu_options->Show();
}
