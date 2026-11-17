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
#include "Table.h"
#include "Action.h"
#include "YesNoMessageBox.h"

#define ART_BANNER_LOAD "gfx/shell/head_load"
#define ART_BANNER_SAVE "gfx/shell/head_save"

#define LEVELSHOT_X		72
#define LEVELSHOT_Y		400
#define LEVELSHOT_W		260
#define LEVELSHOT_H		160

#define MAX_CELLSTRING CS_SIZE

static bool UI_IsCoFRootSaveGame()
{
	char gameDir[256];
	char *base;

	EngFuncs::GetGameDir( gameDir );
	base = strrchr( gameDir, '/' );
	if( !base ) base = strrchr( gameDir, '\\' );
	return !stricmp( base ? base + 1 : gameDir, "cryoffear" );
}

static bool UI_CoFMenuSavesEnabled()
{
	return UI_IsCoFRootSaveGame() && EngFuncs::GetCvarFloat( "cof_pause_menu_saves" ) != 0.0f;
}

// Cry of Fear quick save (engine cof_quick_saves; cof_quicksave writes
// SAVE/cofquick.sav and the label cryoffear/SAVE/saveinfoquick.cof)
#define COF_QUICKSAVE_NAME "cofquick"

static bool UI_CoFQuickSavesEnabled()
{
	return UI_IsCoFRootSaveGame() && EngFuncs::GetCvarFloat( "cof_quick_saves" ) != 0.0f;
}

static bool UI_IsCoFSaveTimestamp( const char *text )
{
	char weekday[4], month[4];
	int day, hour, minute, second, year;
	char extra;

	if( !text || sscanf( text, "%3s %3s %d %d:%d:%d %d %c", weekday, month,
		&day, &hour, &minute, &second, &year, &extra ) != 7 )
		return false;
	return day >= 1 && day <= 31 && hour >= 0 && hour < 24 &&
		minute >= 0 && minute < 60 && second >= 0 && second < 60 && year >= 1970;
}

class CMenuLoadGame;

struct save_t
{
	char name[CS_SIZE];

private:
	friend class CMenuSavesListModel;
	char date[CS_SIZE];
	char comment[256];
	char elapsed_time[CS_SIZE];
};

/*
============
COM_CompareSaves
============
*/
static int COM_CompareSaves( const void *a, const void *b )
{
	const char *file1 = *((const char **)a);
	const char *file2 = *((const char **)b);
	int bResult = 0;

	EngFuncs::CompareFileTime( file2, file1, &bResult );

	return bResult;
}

class CMenuSavePreview : public CMenuBaseItem
{
public:
	CMenuSavePreview() : CMenuBaseItem(), fallback( "{GRAF001" )
	{
		iFlags = QMF_INACTIVE;
	}

	void Draw() override;

	void SetSaveName( const char *name )
	{
		if( name == nullptr )
			saveshot.ForceUnload();
		else
		{
			char path[128];
			bool gamedironly = true;

			// cof_save_root_compat keeps Cry of Fear's five slots and their
			// thumbnails in the runtime root SAVE folder, not in
			// cryoffear/save, so look the preview up there - the same place
			// and the same gamedironly=false lookup the slot list itself uses.
			if( UI_IsCoFRootSaveGame( ))
			{
				snprintf( path, sizeof( path ), "SAVE/%s.bmp", name );
				gamedironly = false;
			}
			else
			{
				snprintf( path, sizeof( path ), "save/%s.bmp", name );
			}

			saveshot.ForceUnload();
			if( EngFuncs::FileExists( path, gamedironly ) )
				saveshot.Load( path );

			if( saveshot.IsValid( ))
			{
				double w = EngFuncs::PIC_Width( saveshot.Handle( ));
				double h = EngFuncs::PIC_Height( saveshot.Handle( ));

				size.h = round( size.w / ( w / h ));
				CalcSizes();
			}
		}
	}

	CImage fallback;
	CImage saveshot;
};

class CMenuSavesListModel : public CMenuBaseModel, public CUtlVector<save_t>
{
public:
	CMenuSavesListModel( CMenuLoadGame *parent ) : parent( parent ) { }

	void Update() override;
	int GetColumns() const override
	{
		// time, name, gametime
		return 3;
	}
	int GetRows() const override
	{
		return Count();
	}
	const char *GetCellText( int line, int column ) override
	{
		switch( column )
		{
		case 0: return Element( line ).date;
		case 1: return Element( line ).comment;
		case 2: return Element( line ).elapsed_time;
		}
		ASSERT( 0 );
		return NULL;
	}
	unsigned int GetAlignmentForColumn(int column) const override
	{
		return column == 2 ? QM_RIGHT : QM_LEFT;
	}

	void OnDeleteEntry( int line ) override;
	void OnActivateEntry( int line ) override;
private:
	CMenuLoadGame *parent;
};

class CMenuLoadGame : public CMenuFramework
{
public:
	// m_fSaveMode decides the panel title, which of the two primary buttons is
	// laid out into the bottom row and which of them is visible at all, and
	// _VidInit() runs from Show() - so it must never be read before it has
	// been written. It used to be left indeterminate here, which is half of
	// the "first Load Game opens as SAVE GAME" bug; the other half is the call
	// order in UI_LoadSaveGame_Menu() below.
	CMenuLoadGame() : CMenuFramework( "CMenuLoadGame" ), m_fSaveMode( false ), savesListModel( this ) { }

	// true to turn this menu into save mode, false to turn into load mode
	void SetSaveMode( bool saveMode );
	bool IsSaveMode() { return m_fSaveMode; }
	void UpdateList() { savesListModel.Update(); }

private:
	void _Init( void );
	void _VidInit( void );

	void LoadGame();
	void SaveGame();
	void ConfirmSaveGame();
	void UpdateGame();
	void DeleteGame();

	CMenuPicButton	load;
	CMenuPicButton  save;
	CMenuPicButton	remove;
	CMenuPicButton	cancel;

	CMenuTable	savesList;

	CMenuSavePreview	levelShot;
	bool m_fSaveMode;
	char		hintText[MAX_HINT_TEXT];

	// Keep the destructive delete and overwrite confirmations separate.
	CMenuYesNoMessageBox msgBox;
	CMenuYesNoMessageBox overwriteBox;
	CMenuSavesListModel savesListModel;

	friend class CMenuSavesListModel;
};

void CMenuSavePreview::Draw()
{
	if( saveshot.IsValid( ))
		UI_DrawPic( m_scPos, m_scSize, uiColorWhite, saveshot );
	else if( UI_IsCoFRootSaveGame() )
		UI_DrawString( font, m_scPos, m_scSize, L( "No preview" ), colorBase, m_scChSize, QM_CENTER, ETF_SHADOW );
	else
		UI_DrawPic( m_scPos, m_scSize, uiColorWhite, fallback, QM_DRAWADDITIVE );

	// draw the rectangle
	UI_DrawRectangle( m_scPos, m_scSize, uiInputFgColor );
}

/*
=================
CMenuSavesListModel::Update
=================
*/
void CMenuSavesListModel::Update( void )
{
	char **filenames;
	int numFiles;

	RemoveAll();
	if( UI_IsCoFRootSaveGame() )
	{
		// The quick save (cof_quicksave, F5; patches/cof-quicksave.patch):
		// a row of its own at the top of the Load list while the option is
		// on and the file exists. Never in the Save tab - only F5 and the
		// pause menu's Quick save write it - so nothing here can overwrite.
		if( !parent->IsSaveMode() && UI_CoFQuickSavesEnabled() &&
			EngFuncs::FileExists( "SAVE/" COF_QUICKSAVE_NAME ".sav", false ))
		{
			save_t save;
			char comment[256];
			int length = 0;
			byte *info = EngFuncs::COM_LoadFile( "SAVE/saveinfoquick.cof", &length );

			Q_strncpy( save.name, COF_QUICKSAVE_NAME, sizeof( save.name ));
			// "Quick" in the Slot column (short: the column is narrow and
			// "Quick save" does not fit it in every language); the label
			// "Quick save (<map>)" in the Save column, translated at draw
			// time like "Pause Save (%s)" (key "Quick save (%s)"), and the
			// timestamp in the Date column, exactly like the five slots
			Q_strncpy( save.date, L( "Quick" ), sizeof( save.date ));
			save.comment[0] = '\0';
			save.elapsed_time[0] = '\0';
			if( info && length > 0 )
			{
				const size_t copy = Q_min( (size_t)length, sizeof( save.comment ) - 1 );
				memcpy( save.comment, info, copy );
				save.comment[copy] = '\0';
				char *stamp = NULL;
				for( char *candidate = strstr( save.comment, " - " ); candidate;
					candidate = strstr( candidate + 3, " - " ))
					stamp = candidate;
				if( stamp && UI_IsCoFSaveTimestamp( stamp + 3 ))
				{
					*stamp = '\0';
					Q_strncpy( save.elapsed_time, stamp + 3, sizeof( save.elapsed_time ));
				}
			}
			if( info )
				EngFuncs::COM_FreeFile( info );
			if( !save.comment[0] && EngFuncs::GetSaveComment( "SAVE/" COF_QUICKSAVE_NAME ".sav", comment ))
				Q_strncpy( save.comment, comment, sizeof( save.comment ));

			Con_DPrintf( "Load list: quick save row \"%s\" | \"%s\" | \"%s\"\n",
				save.date, save.comment, save.elapsed_time );
			AddToTail( save );
		}

		// CoF owns five named slots. Their .sav files are at the runtime
		// root and their optional display labels remain in cryoffear/SAVE.
		for( int slot = 1; slot <= 5; slot++ )
		{
			save_t save;
			char savePath[64];
			char infoPath[64];
			char comment[256];
			int length = 0;
			byte *info;

			snprintf( save.name, sizeof( save.name ), "cofsave%i", slot );
			snprintf( savePath, sizeof( savePath ), "SAVE/%s.sav", save.name );
			const bool occupied = EngFuncs::FileExists( savePath, false );
			if( !occupied && !(parent->IsSaveMode() && UI_CoFMenuSavesEnabled()))
				continue;

			snprintf( save.date, sizeof( save.date ), "Slot %i", slot );
			save.comment[0] = '\0';
			save.elapsed_time[0] = '\0';
			snprintf( infoPath, sizeof( infoPath ), "SAVE/saveinfo%i.cof", slot );
			info = occupied ? EngFuncs::COM_LoadFile( infoPath, &length ) : NULL;
			if( info && length > 0 )
			{
				const size_t copy = Q_min( (size_t)length, sizeof( save.comment ) - 1 );
				memcpy( save.comment, info, copy );
				save.comment[copy] = '\0';
				char *stamp = NULL;
				for( char *candidate = strstr( save.comment, " - " ); candidate;
					candidate = strstr( candidate + 3, " - " ))
					stamp = candidate;
				if( stamp && UI_IsCoFSaveTimestamp( stamp + 3 ))
				{
					*stamp = '\0';
					Q_strncpy( save.elapsed_time, stamp + 3, sizeof( save.elapsed_time ));
				}
			}
			if( info )
				EngFuncs::COM_FreeFile( info );
			if( occupied && !save.comment[0] && EngFuncs::GetSaveComment( savePath, comment ))
			{
				Q_strncpy( save.comment, comment, sizeof( save.comment ));
			}
			if( !occupied && !save.comment[0] )
				Q_strncpy( save.comment, L( "Empty" ), sizeof( save.comment ));

			AddToTail( save );
		}
		parent->load.SetGrayed( !IsValidIndex( 0 ) );
		parent->save.SetGrayed( !UI_CoFMenuSavesEnabled() || !CL_IsActive() );
		parent->remove.SetGrayed( true );
		parent->UpdateGame();
		return;
	}

	filenames = EngFuncs::GetFilesList( "save/*.sav", &numFiles, true );

	// sort the saves in reverse order (oldest past at the end)
	qsort( filenames, numFiles, sizeof( *filenames ), COM_CompareSaves );

	if( parent->IsSaveMode( ) && CL_IsActive( ))
	{
		// create new entry for current save game
		save_t save;
		Q_strncpy( save.name, "new", sizeof( save.name )); // special name, handled in SV_Save_f
		Q_strncpy( save.date, L( "GameUI_SaveGame_Current" ), sizeof( save.date ));
		Q_strncpy( save.comment, L( "GameUI_SaveGame_NewSavedGame" ), sizeof( save.comment ));
		Q_strncpy( save.elapsed_time, L( "GameUI_SaveGame_New" ), sizeof( save.elapsed_time ));

		AddToTail( save );
	}

	for( int i = 0; i < numFiles; i++ )
	{
		save_t save;
		char comment[256];
		char time[CS_TIME];
		char date[CS_TIME];

		// strip path, leave only filename (empty slots doesn't have savename)
		COM_FileBase( filenames[i], save.name, sizeof( save.name ));

		if( !EngFuncs::GetSaveComment( filenames[i], comment ))
		{
			if( comment[0] )
			{
				// get name string even if not found - SV_GetComment can be mark saves
				// as <CORRUPTED> <OLD VERSION> etc
				Q_strncpy( save.date, comment, sizeof( save.date ));
				save.elapsed_time[0] = 0;
				save.comment[0] = 0;

				AddToTail( save );
			}
			continue;
		}

		// they are defined by comment string format
		// time and date
		Q_strncpy( time, comment + CS_SIZE, CS_TIME );
		Q_strncpy( date, comment + CS_SIZE + CS_TIME, CS_TIME );

		snprintf( save.date, sizeof( save.date ), "%s %s", time, date );

		// ingame time
		Q_strncpy( save.elapsed_time, comment + CS_SIZE + CS_TIME * 2, sizeof( save.elapsed_time ));

		char *title, *type, *p;
		type = p = nullptr;

		// we need real title
		// so search for square brackets
		if( comment[0] == '[' && ( p = strchr( comment, ']' )))
		{
			type = comment + 1; // this might be "autosave", "quick", etc...
			title = p + 1; // this is a title
		}
		else title = comment;

		if( title[0] == '#' )
		{
			char s[CS_SIZE];

			// remove the second ], we don't need it to concatenate
			if( p )
				*p = 0;

			// strip everything after first space, assume translatable save titles have no space
			p = strchr( title, ' ' );
			if( p )
				*p = 0;

			Q_strncpy( s, title, sizeof( s ));

			if( type )
			{
				// Localize known save-type identifiers (quick/autosave). Otherwise, fall back to original behavior.
				if( !stricmp( type, "quick" ))
					snprintf( save.comment, sizeof( save.comment ), "%s%s", L( "GameUI_QuickSave" ), L( s ));
				else if( !stricmp( type, "autosave" ))
					snprintf( save.comment, sizeof( save.comment ), "%s%s", L( "GameUI_AutoSave" ), L( s ));
				else
					snprintf( save.comment, sizeof( save.comment ), "[%.16s]%s", type, L( s ));
			}
			else Q_strncpy( save.comment, L( s ), sizeof( save.comment ));
		}
		else
		{
			// strip whitespace from the end of string
			for( size_t len = strlen( title ) - 1; len >= 0; len-- )
			{
				if( !isspace( title[len] ))
					break;

				title[len] = '\0';
			}

			Q_strncpy( save.comment, comment, sizeof( save.comment ));
		}

		AddToTail( save );
	}

	if( !parent->IsSaveMode( ))
	{
		parent->load.SetGrayed( !IsValidIndex( 0 ) );
	}
	else
	{
		parent->save.SetGrayed( !IsValidIndex( 0 ) || !CL_IsActive( ));
	}

	parent->remove.SetGrayed( !IsValidIndex( 0 ));
	parent->UpdateGame();
}

// Cry of Fear gamepad: A (and Enter, or a double click) on a row loads it, or
// in the Save tab saves into it - the overwrite question still comes for a
// used slot
void CMenuSavesListModel::OnActivateEntry( int line )
{
	if( !IsValidIndex( line ))
		return;

	if( parent->IsSaveMode( ))
		parent->SaveGame();
	else parent->LoadGame();
}

void CMenuSavesListModel::OnDeleteEntry( int line )
{
	if( UI_IsCoFRootSaveGame() )
		return;

	parent->msgBox.Show();
}

/*
=================
UI_LoadGame_Init
=================
*/
void CMenuLoadGame::_Init( void )
{
	save.szName = L( "GameUI_Save" );
	save.SetPicture( PC_SAVE_GAME );
	save.onReleased = VoidCb( &CMenuLoadGame::SaveGame );
	save.SetCoord( 72, 230 );

	load.szName = L( "GameUI_Load" );
	load.SetPicture( PC_LOAD_GAME );
	load.onReleased = VoidCb( &CMenuLoadGame::LoadGame );
	load.SetCoord( 72, 230 );

	remove.szName = L( "Delete" );
	remove.SetPicture( PC_DELETE );
	remove.onReleased = msgBox.MakeOpenEvent();
	remove.SetCoord( 72, 280 );

	cancel.szName = L( "GameUI_Cancel" );
	cancel.SetPicture( PC_CANCEL );
	cancel.onReleased = VoidCb( &CMenuLoadGame::Hide );
	cancel.SetCoord( 72, 330 );

	savesList.szName = hintText;
	savesList.onChanged = VoidCb( &CMenuLoadGame::UpdateGame );
	savesList.SetupColumn( 0, L( "GameUI_Time" ), 0.30f );
	savesList.SetupColumn( 1, L( "GameUI_Game" ), 0.55f );
	savesList.SetupColumn( 2, L( "GameUI_ElapsedTime" ), 0.15f );
	if( UI_IsCoFRootSaveGame() )
	{
		savesList.SetupColumn( 0, L( "Slot" ), 0.20f );
		savesList.SetupColumn( 1, L( "Save" ), 0.55f );
		savesList.SetupColumn( 2, L( "Date" ), 0.25f );
	}

	savesList.SetModel( &savesListModel );
	savesList.SetCharSize( QM_SMALLFONT );
	savesList.SetRect( 360, 230, -20, 465 );

	msgBox.SetMessage( L( "Delete this saved game?" ) );
	msgBox.onPositive = VoidCb( &CMenuLoadGame::DeleteGame );
	msgBox.Link( this );

	overwriteBox.SetMessage( "Overwrite this saved game?" );
	overwriteBox.SetPositiveButton( "Overwrite", PC_SAVE_GAME );
	overwriteBox.onPositive = VoidCb( &CMenuLoadGame::ConfirmSaveGame );
	overwriteBox.Link( this );

	levelShot.SetRect( LEVELSHOT_X, LEVELSHOT_Y, LEVELSHOT_W, LEVELSHOT_H );

	if( UI_ThemeActive( ))
	{
		SetPanel( "LOAD GAME", 860, 560 );
		msgBox.szPanelTitle = "DELETE SAVE";
		overwriteBox.szPanelTitle = "OVERWRITE SAVE";
	}
	else
	{
		AddItem( banner );
	}

	AddItem( savesList );
	AddItem( levelShot );
	AddItem( remove );
	AddItem( cancel );
	AddItem( load );
	AddItem( save );
}

void CMenuLoadGame::_VidInit( void )
{
	if( !IsPanel( ))
		return;

	szPanelTitle = m_fSaveMode ? "SAVE GAME" : "LOAD GAME";

	Rect left = ColumnRect2( 0, 0.62f );
	Rect right = ColumnRect2( 1, 0.62f );

	savesList.SetRect( left.pt.x, left.pt.y, left.sz.w, left.sz.h );
	levelShot.SetRect( right.pt.x, right.pt.y, right.sz.w, right.sz.w * 9 / 16 );

	CMenuPicButton *btns[3];
	int n = 0;

	if( remove.IsVisible( ))
		btns[n++] = &remove;
	btns[n++] = &cancel;

	CMenuPicButton *primary = m_fSaveMode ? &save : &load;

	if( primary->IsVisible( ))
		btns[n++] = primary;

	LayoutButtonRow( btns, n );
}

void CMenuLoadGame::LoadGame()
{
	if( !savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
		return;

	char cmd[128];
	const char *name = savesListModel[savesList.GetCurrentIndex( )].name;

	snprintf( cmd, sizeof( cmd ), "load \"%s\"\n", name );
	EngFuncs::StopBackgroundTrack( );

	// Cry of Fear's own Load Game panel stopped the client's MP3 player before
	// loading; the engine menu never did, so the background map's track kept
	// playing into a save whose map starts no track of its own. "stopmp3" is
	// the client's own console command (no arguments, unconditional stop) and
	// is a different player from the engine's StopBackgroundTrack above.
	if( UI_IsCoFRootSaveGame( ))
		EngFuncs::ClientCmd( false, "stopmp3\n" );

	EngFuncs::ClientCmd( false, cmd );
	UI_CloseMenu();
}

void CMenuLoadGame::SaveGame()
{
	if( !savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
		return;

	const char *name = savesListModel[savesList.GetCurrentIndex( )].name;

	if( UI_IsCoFRootSaveGame() )
	{
		char savePath[64];

		if( !UI_CoFMenuSavesEnabled() )
			return;

		snprintf( savePath, sizeof( savePath ), "SAVE/%s.sav", name );
		if( EngFuncs::FileExists( savePath, false ) )
		{
			overwriteBox.Show();
			return;
		}
	}

	ConfirmSaveGame();
}

void CMenuLoadGame::ConfirmSaveGame()
{
	if( !savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
		return;

	char cmd[128];
	const char *name = savesListModel[savesList.GetCurrentIndex( )].name;

	if( UI_IsCoFRootSaveGame() )
	{
		if( !UI_CoFMenuSavesEnabled() )
			return;
		snprintf( cmd, sizeof( cmd ), "cof_menu_save %s\n", name + strlen( "cofsave" ));
	}
	else
	{
		char previewPath[128];
		snprintf( previewPath, sizeof( previewPath ), "save/%s.bmp", name );
		EngFuncs::PIC_Free( previewPath );
		snprintf( cmd, sizeof( cmd ), "save \"%s\"\n", name );
	}
	EngFuncs::ClientCmd( false, cmd );
	UI_CloseMenu();
}

void CMenuLoadGame::UpdateGame()
{
	if( UI_IsCoFRootSaveGame() )
	{
		remove.SetGrayed( true );
		if( savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
			levelShot.SetSaveName( savesListModel[savesList.GetCurrentIndex( )].name );
		else
			levelShot.SetSaveName( nullptr );
		return;
	}

	// first item is for creating new saves
	if(( IsSaveMode() && savesList.GetCurrentIndex() == 0 ) || !savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
	{
		remove.SetGrayed( true );
		levelShot.SetSaveName( nullptr );
	}
	else
	{
		remove.SetGrayed( false );
		levelShot.SetSaveName( savesListModel[savesList.GetCurrentIndex( )].name );
	}
}

void CMenuLoadGame::DeleteGame()
{
	if( UI_IsCoFRootSaveGame() )
		return;

	if( !savesListModel.IsValidIndex( savesList.GetCurrentIndex( )))
		return;

	char cmd[128];
	const char *name = savesListModel[savesList.GetCurrentIndex( )].name;

	snprintf( cmd, sizeof( cmd ), "killsave \"%s\"\n", name );
	EngFuncs::ClientCmd( true, cmd );

	snprintf( cmd, sizeof( cmd ), "save/%s.bmp", name );
	EngFuncs::PIC_Free( cmd );

	savesListModel.Update();
}

void CMenuLoadGame::SetSaveMode( bool saveMode )
{
	m_fSaveMode = saveMode;
	if( UI_IsCoFRootSaveGame() )
	{
		banner.SetPicture( saveMode ? ART_BANNER_SAVE : ART_BANNER_LOAD );
		save.SetVisibility( UI_CoFMenuSavesEnabled() && saveMode );
		load.SetVisibility( !saveMode );
		remove.SetVisibility( false );
		szName = saveMode ? "CMenuSaveGame" : "CMenuLoadGame";
		return;
	}

	if( saveMode )
	{
		banner.SetPicture( ART_BANNER_SAVE );
		save.SetVisibility( true );
		load.SetVisibility( false );
		szName = "CMenuSaveGame";
	}
	else
	{
		banner.SetPicture( ART_BANNER_LOAD );
		save.SetVisibility( false );
		load.SetVisibility( true );
		szName = "CMenuLoadGame";
	}
}

static CMenuLoadGame *menu_loadgame = NULL;

/*
=================
UI_LoadGame_Precache
=================
*/
void UI_LoadSaveGame_Precache( void )
{
	menu_loadgame = new CMenuLoadGame();
	EngFuncs::CvarRegister( "cof_pause_menu_saves", "0", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( "cof_quick_saves", "0", FCVAR_ARCHIVE );
	EngFuncs::PIC_Load( ART_BANNER_SAVE );
	EngFuncs::PIC_Load( ART_BANNER_LOAD );
}

void UI_LoadSaveGame_Menu( bool saveMode )
{
	if( gMenu.m_gameinfo.gamemode == GAME_MULTIPLAYER_ONLY )
	{
		// completely ignore save\load menus for multiplayer_only
		return;
	}

	if( !EngFuncs::CheckGameDll( )) return;

	// The mode has to be applied BEFORE Show(), because Show() is
	// Init() + VidInit() (controls/BaseWindow.cpp) and _VidInit() is where the
	// panel title is chosen and where LayoutButtonRow() places whichever of
	// Load / Save is the primary button for this mode. With the old order
	// _VidInit() read the PREVIOUS mode: the first Load Game of a session came
	// up titled SAVE GAME with Save laid out into the row, and then
	// SetSaveMode() hid Save and showed Load - which nothing had positioned, so
	// it kept its WON-layout coordinate (72, 230) and drew as a bare label at
	// the left edge, outside the panel. Opening the page a second time looked
	// right only because by then the stale mode happened to match.
	menu_loadgame->SetSaveMode( saveMode );
	menu_loadgame->Show();
	menu_loadgame->UpdateList();
}

void UI_LoadSaveGame_Shutdown( void )
{
	delete menu_loadgame;
}

/*
=================
UI_LoadGame_Menu
=================
*/
void UI_LoadGame_Menu( void )
{
	UI_LoadSaveGame_Menu( false );
}

void UI_SaveGame_Menu( void )
{
	if( UI_IsCoFRootSaveGame() && !UI_CoFMenuSavesEnabled() )
		return;
	UI_LoadSaveGame_Menu( true );
}
ADD_MENU4( menu_loadgame, UI_LoadSaveGame_Precache, UI_LoadGame_Menu, UI_LoadSaveGame_Shutdown );
ADD_MENU4( menu_savegame, NULL, UI_SaveGame_Menu, NULL );
