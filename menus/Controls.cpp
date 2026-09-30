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
#include "keydefs.h"
#include "Bitmap.h"
#include "PicButton.h"
#include "Action.h"
#include "YesNoMessageBox.h"
#include "MessageBox.h"
#include "Table.h"
#include "utlvector.h"
#include "KbActListModel.h"
#include "CoFOptions.h"

#define ART_BANNER		"gfx/shell/head_controls"

class CMenuControls;

class CMenuKeysModel : public CMenuKbActListModel
{
public:
	CMenuKeysModel( CMenuControls *parent ) : CMenuKbActListModel( VIEW_BINDINGS ), parent( parent ) { }

	void OnActivateEntry( int line ) override;
	void OnDeleteEntry( int line ) override;

private:
	CMenuControls *parent;
};

class CMenuControls : public CMenuFramework
{
public:
	CMenuControls() : CMenuFramework("CMenuControls"), keysListModel( this ) { }

	void _Init();
	void _VidInit();
	void EnterGrabMode( void );
	void UnbindEntry( void );

	// the grab box's bind, shared with the cfg-only test hook
	// (menu_cof_options_select keybinds, menus/CoFOptions.cpp)
	bool BindCurrent( int key );
	void OptionsProbe( void );

	// state toggle by
	CMenuTable keysList;
	CMenuKeysModel keysListModel;

private:
	void UnbindCommand( const char *command );
	void ResetKeysList( void );
	void Cancel( void )
	{
		EngFuncs::ClientCmd( true, "exec keyboard\n" );
		Hide();
	}

	// redefine key wait dialog
	class CGrabKeyMessageBox : public CMenuMessageBox
	{
	public:
		bool KeyUp( int key ) override;
		bool KeyDown( int key ) override;
	} msgBox1; // small msgbox

	CMenuYesNoMessageBox msgBox2; // large msgbox
};

void CMenuControls::UnbindCommand( const char *command )
{
	const size_t command_len = strlen( command );

	for( int i = 0; ; i++ )
	{
		const char *str = EngFuncs::KeynumToString( i );

		if( !strcmp( str, "<OUT OF RANGE>" ))
			break;

		const char *b = EngFuncs::KEY_GetBinding( i );
		if( !b )
			continue;

		// the pad buttons belong to the Gamepad controls page
		if( UI_CoFOptionsLayout( ) && UI_CoFIsPadKey( i ))
			continue;

		if( !strncmp( b, command, command_len ))
			EngFuncs::KEY_SetBinding( i, "" );
	}
}

void CMenuKeysModel::OnActivateEntry(int line)
{
	parent->EnterGrabMode();
}

void CMenuKeysModel::OnDeleteEntry(int line)
{
	parent->UnbindEntry();
}

void CMenuControls::ResetKeysList( void )
{
	char *afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/kb_def.lst", NULL );
	char *pfile = afile;
	char token[1024];

	if( !afile )
	{
		UI_ShowMessageBox( "UI_Parse_KeysList: kb_act.lst not found\n" );
		return;
	}
	
	EngFuncs::ClientCmd( true, "unbindall" );

	while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
	{
		char	key[32];

		Q_strncpy( key, token, sizeof( key ));

		pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
		if( !pfile ) break;	// technically an error

		char	cmd[4096];

		if( key[0] == '\\' && key[1] == '\\' )
		{
			key[0] = '\\';
			key[1] = '\0';
		}

		snprintf( cmd, sizeof( cmd ), "bind \"%s\" \"%s\"\n", key, token );
		EngFuncs::ClientCmd( true, cmd );
	}

	EngFuncs::COM_FreeFile( afile );
	keysListModel.Update();
}

bool CMenuControls::CGrabKeyMessageBox::KeyUp( int key )
{
	EUISounds sound;
	CMenuControls *parent = ((CMenuControls*)m_pParent);

	// defining a key
	// escape is special, should allow rebind all keys on gamepad
	if( UI::Key::IsConsole( key ) || key == K_ESCAPE
		|| !parent->keysListModel.entries.IsValidIndex( parent->keysList.GetCurrentIndex( )))
	{
		sound = SND_BUZZ;
	}
	else
	{
		parent->BindCurrent( key );

		sound = SND_LAUNCH;
	}

	parent->keysListModel.Update();
	Hide();
	PlayLocalSound( uiStatic.sounds[sound] );

	return true;
}

bool CMenuControls::CGrabKeyMessageBox::KeyDown( int key )
{
	return true;
}

bool CMenuControls::BindCurrent( int key )
{
	if( !keysListModel.IsLineUsable( keysList.GetCurrentIndex( )))
		return false;

	const char *bindName = keysListModel.entries[keysList.GetCurrentIndex( )].bind;

	// what "bind" does, without running the command buffer from inside a menu
	// event (a test hook runs inside a cfg's own buffer)
	EngFuncs::KEY_SetBinding( key, bindName );
	return true;
}

void CMenuControls::UnbindEntry()
{
	if( !keysListModel.IsLineUsable( keysList.GetCurrentIndex( )))
	{
		PlayLocalSound( uiStatic.sounds[SND_BUZZ] );
		return; // not a key
	}

	const char *bindName = keysListModel.entries[keysList.GetCurrentIndex( )].bind;

	UnbindCommand( bindName );
	PlayLocalSound( uiStatic.sounds[SND_REMOVEKEY] );
	keysListModel.Update();

	// disabled: left command just unbinded
	// msgBox1.Show();
}

void CMenuControls::EnterGrabMode()
{
	if( !keysListModel.IsLineUsable( keysList.GetCurrentIndex( )))
	{
		PlayLocalSound( uiStatic.sounds[SND_REMOVEKEY] );
		return;
	}

	// entering to grab-mode
	const char *bindName = keysListModel.entries[keysList.GetCurrentIndex( )].bind;

	int keys[2];

	CMenuKbActListModel::LookupBoundKeys( bindName, keys );
	if( keys[1] != -1 )
		UnbindCommand( bindName );

	msgBox1.Show();

	PlayLocalSound( uiStatic.sounds[SND_KEY] );
}

/*
=================
UI_Controls_Init
=================
*/
void CMenuControls::_Init( void )
{
	banner.SetPicture( ART_BANNER );

	// Cry of Fear: this is the Keybinds page since the options relayout
	// (menus/CoFOptions.cpp); "Controls" is the aiming/mouse/gamepad page now
	if( UI_ThemeActive( ))
		SetPanel( UI_CoFOptionsLayout( ) ? "KEYBINDS" : "CONTROLS", 900, 600 );

	keysList.SetRect( 360, 230, -20, 465 );
	keysList.SetModel( &keysListModel );
	keysList.SetupColumn( 0, L( "GameUI_Action" ), 0.50f );
	keysList.SetupColumn( 1, L( "GameUI_KeyButton" ), 0.25f );
	keysList.SetupColumn( 2, L( "GameUI_Alternate" ), 0.25f );

	msgBox1.SetMessage( L( "Press a key or button" ) );
	msgBox1.Link( this );

	msgBox2.SetMessage( L( "GameUI_KeyboardSettingsText" ) );
	msgBox2.onPositive = VoidCb( &CMenuControls::ResetKeysList );
	msgBox2.Link( this );

	if( !UI_ThemeActive( ))
		AddItem( banner );

	// X on a pad: Use defaults (controls/Framework.cpp)
	pPadSecondary = AddButton( L( "GameUI_UseDefaults" ), nullptr, PC_USE_DEFAULTS, msgBox2.MakeOpenEvent( ));

	// The advanced controls now live on the top-level Game options page
	// (menus/AdvancedControls.cpp, "GAME"), which Options lists first, so the
	// button that hid them behind Controls is gone from the themed page.
	if( !UI_ThemeActive( ))
		AddButton( L( "Adv. Controls" ), nullptr, PC_ADV_CONTROLS, UI_AdvControls_Menu );

	// secondary first, so the button row reads ... Cancel then OK
	if( UI_ThemeActive( ))
	{
		AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuControls::Cancel ));
		AddButton( L( "GameUI_OK" ), nullptr, PC_OK, VoidCb( &CMenuControls::SaveAndPopMenu ));
	}
	else
	{
		AddButton( L( "GameUI_OK" ), nullptr, PC_OK, VoidCb( &CMenuControls::SaveAndPopMenu ));
		AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuControls::Cancel ));
	}

	AddItem( keysList );
}

void CMenuControls::_VidInit()
{
	if( IsPanel( ))
	{
		Rect c = ContentRect();

		keysList.SetRect( c.pt.x, c.pt.y, c.sz.w, c.sz.h );
		msgBox1.szPanelTitle = "PRESS A KEY";
	}
	else
	{
		// in theme mode CMenuMessageBox sizes and centres itself around the
		// wrapped message, so do not stamp the WON rect over it
		msgBox1.SetRect( DLG_X + 192, 256, 640, 128 );
		msgBox1.pos.x += uiStatic.xOffset;
		msgBox1.pos.y += uiStatic.yOffset;
	}

	keysListModel.Update();

	// the first row of the list is a section caption in theme mode, so start
	// the cursor on the first real binding instead
	keysList.SetCurrentIndex( 0 );
}

ADD_MENU( menu_controls, CMenuControls, UI_Controls_Menu );

/*
=================
CMenuControls::OptionsProbe

menu_cof_options_select keybinds [<command> [<KEY>]] (menus/CoFOptions.cpp).
This project's tests may not inject a key, so the hook does what a player
does on this page: it puts the cursor on the row whose action is <command>
(the kb_act.lst bind, e.g. +reload) and, with a key name, runs what the
"press a key" box runs when that key comes up - EnterGrabMode's unbind of a
command that already has two keys, then BindCurrent. Without arguments it
lists the rows and their keys.
=================
*/
void CMenuControls::OptionsProbe( void )
{
	char commandBuf[64], keyBuf[32];
	Q_strncpy( commandBuf, EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "", sizeof( commandBuf ));
	Q_strncpy( keyBuf, EngFuncs::CmdArgc() > 3 ? EngFuncs::CmdArgv( 3 ) : "", sizeof( keyBuf ));
	const char *command = commandBuf[0] ? commandBuf : NULL;
	const char *keyName = keyBuf[0] ? keyBuf : NULL;

	keysListModel.Update();

	if( !command || !command[0] )
	{
		for( int i = 0; i < keysListModel.entries.Count(); i++ )
		{
			const CMenuKbActListModel::entry_t &e = keysListModel.entries[i];

			if( e.bind[0] )
				Con_Printf( "menu_cof_options_select keybinds: row %d \"%s\" = %s [%s] [%s]\n", i, e.display, e.bind, e.first, e.second );
		}
		return;
	}

	int row = -1;
	for( int i = 0; i < keysListModel.entries.Count() && row < 0; i++ )
	{
		if( !stricmp( keysListModel.entries[i].bind, command ))
			row = i;
	}

	if( row < 0 )
	{
		Con_Printf( "menu_cof_options_select keybinds: no row for \"%s\"\n", command );
		return;
	}

	keysList.SetCurrentIndex( row );
	Con_Printf( "menu_cof_options_select keybinds: row %d (%s) selected\n", row, command );

	if( !keyName || !keyName[0] )
		return;

	// the exact spelling first ("g" is key 103, "G" the unused key 71)
	int key = -1;
	for( int pass = 0; pass < 2 && key < 0; pass++ )
	{
		for( int i = 0; ; i++ )
		{
			const char *str = EngFuncs::KeynumToString( i );

			if( !strcmp( str, "<OUT OF RANGE>" ))
				break;
			if( pass ? !stricmp( str, keyName ) : !strcmp( str, keyName ))
			{
				key = i;
				break;
			}
		}
	}

	if( key < 0 || UI::Key::IsConsole( key ) || key == K_ESCAPE )
	{
		Con_Printf( "menu_cof_options_select keybinds: key \"%s\" refused\n", keyName );
		return;
	}

	// as EnterGrabMode, then the grab box's KeyUp
	int keys[2];
	CMenuKbActListModel::LookupBoundKeys( command, keys );
	if( keys[1] != -1 )
		UnbindCommand( command );

	BindCurrent( key );
	keysListModel.Update();

	const CMenuKbActListModel::entry_t &e = keysListModel.entries[row];
	Con_Printf( "menu_cof_options_select keybinds: %s is now on [%s] [%s]\n", command, e.first, e.second );
}

void UI_Controls_OptionsProbe( void )
{
	if( menu_controls->WindowStack()->Current() != menu_controls )
		UI_Controls_Menu();
	menu_controls->OptionsProbe();
}
