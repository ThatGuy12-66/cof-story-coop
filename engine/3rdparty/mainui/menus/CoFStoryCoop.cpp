/*
CoFStoryCoop.cpp -- story co-op in the main menu: Host co-op, Play with AI, Continue co-op

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

Cry of Fear Co-op 1.3.6 (the owner: "integrate the mod into the main menu somehow", then: run co-op from the menu,
its own item near the top). The main list's Co-op item (menus/Main.cpp) opens a second list, and its pages are one
tabbed window, CO-OP (menus/CoFOptions.cpp):

    Host co-op       a new game or a chapter, hosted for friends     (the app's Host a game)
    Join co-op       a join code or an address                       (menus/CoFCoop.cpp, the app's Join a game)
    Continue co-op   a co-op checkpoint, hosted or with AI           (the app's Continue from)
    Play with AI     a new game or a chapter with AI companions      (the app's Play solo)
    Original co-op   Cry of Fear's own co-op maps                    (menus/CoFCoop.cpp, the app's own co-op)

Each Start writes, in one command-buffer write and with no "disconnect", exactly what the Cry of Fear Co-op app starts
the game with for the same choices: menus/CoFStoryCoopLogic.h builds the text, and tests/test_menu_coop.py compares
it with the app's. The app opens the game to this menu (its Play card): it sets the release cvars, the player name
and cof_menu_maps (its copy's merged-maps tag), and writes cryoffear/cof_invite.txt with the join code it found;
Host co-op shows that code.

Settings are remembered in archived cof_story_* cvars; the password, as on the other co-op pages, is not.
Nothing here is reachable unless the running game directory is "cryoffear".
*/

#include "Framework.h"
#include "Action.h"
#include "CheckBox.h"
#include "Field.h"
#include "PicButton.h"
#include "SpinControl.h"
#include "Table.h"
#include "BaseModel.h"
#include "MenuStrings.h"
#include "Utils.h"
#include "CryOfFear.h"
#include "CoFStoryCoopLogic.h"

#define COF_STORY_CVAR_START      "cof_story_start"       // the start map last chosen
#define COF_STORY_CVAR_PLAYERS    "cof_story_players"
#define COF_STORY_CVAR_FILL       "cof_story_fill"        // AI companions in the empty places (Host)
#define COF_STORY_CVAR_COMPANIONS "cof_story_companions"  // Play with AI
#define COF_STORY_CVAR_BONUS      "cof_story_bonus"
#define COF_STORY_CVAR_MERGED     "cof_story_merged"
#define COF_STORY_CVAR_TORCH      "cof_story_torch"
#define COF_STORY_CVAR_MAPS       "cof_menu_maps"         // set by the app; never archived
#define COF_STORY_INVITE          "cof_invite.txt"        // written by the app
#define COF_STORY_MAX_CHECKPOINTS 32
#define COF_STORY_MAX_SAVE_FILES  256

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

// the name the pages offer: the engine's archived "name", except the "Unknown" Cry of Fear's config.cfg ships with
static void UI_CoFStoryDefaultName( char *out, size_t size )
{
	const char *name = EngFuncs::GetCvarString( "name" );
	const char *user = EngFuncs::GetCvarString( "ui_username" );

	if( name && name[0] && stricmp( name, "Unknown" ) && stricmp( name, "Player" ))
		CoFStory_Clean( out, size, name, COF_STORY_NAME_MAX, true );
	else if( user && user[0] && stricmp( user, "Unknown" ))
		CoFStory_Clean( out, size, user, COF_STORY_NAME_MAX, true );
	else
		out[0] = 0;

	if( !out[0] )
		CoFStory_Copy( out, size, "Player" );
}

static void UI_CoFStoryFieldName( char *out, size_t size, CMenuField &field )
{
	CoFStory_Clean( out, size, field.GetBuffer(), COF_STORY_NAME_MAX, true );
	if( !out[0] )
		UI_CoFStoryDefaultName( out, size );
}

static int UI_CoFStoryClamp( float value, int lo, int hi )
{
	int v = (int)( value + ( value >= 0.0f ? 0.5f : -0.5f ));

	if( v < lo ) v = lo;
	if( v > hi ) v = hi;
	return v;
}

static bool UI_CoFStoryCanStart( void )
{
	// reachable only from the main menu; never start a server on top of a running game (the test hooks can be
	// typed anywhere)
	if( CL_IsActive( ))
	{
		Con_Printf( "Cry of Fear co-op: leave the current game first\n" );
		return false;
	}
	return true;
}

// the join code the app found (cryoffear/cof_invite.txt), and the port this game hosts on
static void UI_CoFStoryInviteText( char *out, size_t size )
{
	cofStoryInvite_t invite;
	int length = 0;
	int port = UI_CoFStoryClamp( EngFuncs::GetCvarFloat( "hostport" ), 0, 65535 );
	byte *raw = EngFuncs::COM_LoadFile( COF_STORY_INVITE, &length );

	if( port <= 0 )
		port = COF_STORY_DEFAULT_PORT;

	memset( &invite, 0, sizeof( invite ));
	if( raw )
	{
		CoFStory_ParseInvite( (const char *)raw, (size_t)length, &invite );
		EngFuncs::COM_FreeFile( raw );
	}

	if( invite.code[0] )
		snprintf( out, size, "Friends join with your join code:\n%s\nThe Cry of Fear Co-op app shows it too, with a "
			"Copy button. Over the Internet, UDP %d has to reach this PC (the app asks your router).", invite.code, port );
	else
		snprintf( out, size, "Friends join with your join code (the Cry of Fear Co-op app shows it while this game "
			"runs) or your address, on UDP %d. Over the Internet that port has to reach this PC.", port );
}

// the name and password fields as the app takes them: false, with the app's message in error, otherwise. An empty
// name is the default one (the field opens with it); anything the app would refuse is refused, not changed.
static bool UI_CoFStoryFieldsValid( CMenuField &name, CMenuField *password, char *error, size_t size )
{
	if( !CoFStory_FieldValid( name.GetBuffer(), COF_STORY_NAME_MAX, true, true ))
	{
		CoFStory_FieldMessage( error, size, "Your name", COF_STORY_NAME_MAX, false );
		return false;
	}
	if( password && !CoFStory_FieldValid( password->GetBuffer(), COF_STORY_PASSWORD_MAX, false, true ))
	{
		CoFStory_FieldMessage( error, size, "The password", COF_STORY_PASSWORD_MAX, false );
		return false;
	}
	return true;
}

// player tools (2026-11-09): the host's revive time, auto-respawn time and teammate outlines, as set now (the last game's,
// the console's, or the default when the game has not set the cvar yet); no widgets for them on any page
static void UI_CoFStoryHostTunables( cofStoryStart_t *s )
{
	s->cprTime = CoFStory_HostTunable( EngFuncs::GetCvarFloat( COF_STORY_CVAR_CPR ),
		COF_STORY_CPR_MIN, COF_STORY_CPR_MAX, COF_STORY_CPR_DEFAULT );
	s->respawnDelay = CoFStory_HostTunable( EngFuncs::GetCvarFloat( COF_STORY_CVAR_RESPAWN ),
		COF_STORY_RESPAWN_MIN, COF_STORY_RESPAWN_MAX, COF_STORY_RESPAWN_DEFAULT );
	s->outlines = CoFStory_HostTunable( EngFuncs::GetCvarFloat( COF_STORY_CVAR_OUTLINES ), 0, 1, 0 ) == 1;
}

static void UI_CoFStoryRun( const char *what, const char *text )
{
	Con_Printf( "Cry of Fear co-op: %s\n", what );
	// a game opened without the app: the join check's version, so the app's Join takes this game (the app sets it,
	// and the release, when it opens the game)
	if( !EngFuncs::GetCvarString( "cof_build" )[0] )
		EngFuncs::ClientCmd( false, "cof_build " COF_STORY_BUILD "\n" );
	EngFuncs::ClientCmd( false, text );
	UI_CloseMenu();
}

// ---------------------------------------------------------------------------
// the start list: New game, then the chapters
// ---------------------------------------------------------------------------

class CMenuCoFStoryStartModel : public CMenuBaseModel
{
public:
	void Update() override { }
	int GetColumns() const override { return 1; }
	int GetRows() const override { return COF_STORY_NUM_STARTS; }
	const char *GetCellText( int line, int ) override
	{
		if( line < 0 || line >= COF_STORY_NUM_STARTS )
			return "";
		return g_cofStoryStarts[line].label;
	}

	static int Find( const char *map )
	{
		for( int i = 0; map && i < COF_STORY_NUM_STARTS; i++ )
			if( !stricmp( g_cofStoryStarts[i].map, map ))
				return i;
		return -1;
	}
};

// ---------------------------------------------------------------------------
// Host co-op and Play with AI: one page, two modes
// ---------------------------------------------------------------------------

class CMenuCoFStoryStart : public CMenuFramework
{
public:
	CMenuCoFStoryStart( bool solo ) : CMenuFramework( solo ? "CMenuCoFStorySolo" : "CMenuCoFStoryHost" ), m_bSolo( solo )
	{
		m_szInvite[0] = 0;
	}

	void Show() override
	{
		CMenuFramework::Show();
		Refresh();
	}

	// the Start button; also the test hook menu_cof_story_start
	bool Start( void );
	void SetForTest( const char *map, int players, int companions );

	// Host co-op: the join code the app finds after the game opened shows up without opening the page again
	void Think( void ) override
	{
		CMenuFramework::Think();
		if( !m_bSolo && !m_bError && uiStatic.realTime - m_iInviteTime > 2000 )
		{
			m_iInviteTime = uiStatic.realTime;
			UI_CoFStoryInviteText( m_szInvite, sizeof( m_szInvite ));
		}
	}

private:
	void _Init() override;
	void _VidInit() override;
	void Refresh( void );
	void StartCb( void ) { Start(); }

	const bool m_bSolo;
	bool m_bError = false;    // the hint shows why Start was refused, until the page opens again
	int  m_iInviteTime = 0;

	CMenuTable     starts;
	CMenuCoFStoryStartModel startModel;

	CMenuField     playerName;
	CMenuField     password;
	CMenuSpinControl players;
	CMenuCheckBox  fill;
	CMenuSpinControl companions;
	CMenuCheckBox  bonus;
	CMenuCheckBox  merged;
	CMenuCheckBox  torch;
	CMenuAction    hint;

	char m_szInvite[400];
};

void CMenuCoFStoryStart::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = m_bSolo ? L( "Play with AI" ) : L( "Host co-op" );
	if( theme )
		SetPanel( m_bSolo ? "PLAY WITH AI" : "HOST CO-OP", 900, 560 );

	starts.SetCharSize( QM_SMALLFONT );
	starts.SetupColumn( 0, L( "Start from" ), 1.0f );
	starts.SetModel( &startModel );

	playerName.szName = L( "Your name" );
	playerName.iMaxLength = COF_STORY_NAME_MAX;
	playerName.szStatusText = L( "Items come back from a checkpoint only under the same name" );

	password.szName = L( "Password" );
	password.iMaxLength = COF_STORY_PASSWORD_MAX;
	password.bHideInput = true;
	password.szStatusText = L( "Optional: friends need it to join; never saved" );

	players.szName = L( "Players" );
	players.Setup( 2, 4, 1 );
	players.SetDisplayPrecision( 0 );
	players.SetCharSize( QM_SMALLFONT );
	players.szStatusText = L( "Places in this game, you included" );

	fill.szName = L( "AI in the empty places" );
	fill.szStatusText = L( "An AI companion in each place a friend has not taken; each makes room when a friend joins" );

	companions.szName = L( "AI companions" );
	companions.Setup( 1, COF_STORY_MAX_COMPANIONS, 1 );
	companions.SetDisplayPrecision( 0 );
	companions.SetCharSize( QM_SMALLFONT );
	companions.szStatusText = L( "One to three AI companions play with you, on this PC only" );

	bonus.szName = L( "Open the bonus room" );
	bonus.szStatusText = L( "Ticked: open for everyone. Unticked: as far as you unlocked it in Cry of Fear" );

	merged.szName = L( "Merged maps" );
	merged.szStatusText = L( "Fewer loading screens: door-joined story maps load as one" );

	torch.szName = L( "Co-op flashlight on F" );
	torch.szStatusText = L( "A free torch for everyone; the original has only Simon's phone" );

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = m_szInvite;

	if( !theme )
		AddItem( banner );

	AddItem( starts );
	AddItem( playerName );
	if( m_bSolo )
	{
		AddItem( companions );
	}
	else
	{
		AddItem( players );
		AddItem( password );
		AddItem( fill );
	}
	AddItem( bonus );
	AddItem( merged );
	AddItem( torch );
	AddItem( hint );

	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFStoryStart::Hide ), QMF_NOTIFY );
	AddButton( L( "Start" ), m_bSolo ? L( "Start the story with your AI companions" ) : L( "Start the story and host it" ),
		PC_START_GAME, VoidCb( &CMenuCoFStoryStart::StartCb ), QMF_NOTIFY );
}

void CMenuCoFStoryStart::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect left  = ColumnRect2( 0, 0.42f );
		Rect right = ColumnRect2( 1, 0.42f );
		const int colW = ( right.sz.w - THEME_GUTTER2 ) / 2;
		const int xa = right.pt.x;
		const int xb = right.pt.x + colW + THEME_GUTTER2;
		const int checkPitch = THEME_CHECK_SIZE + 18;
		int y = right.pt.y + THEME_LABEL_TALL + 8;

		starts.SetRect( left.pt.x, left.pt.y, left.sz.w, left.sz.h );

		playerName.SetRect( xa, y, colW, THEME_CTRL_H );
		if( m_bSolo )
			companions.SetRect( xb, y, colW, THEME_CTRL_H );
		else
			players.SetRect( xb, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;

		if( !m_bSolo )
		{
			password.SetRect( xa, y, colW, THEME_CTRL_H );
			y += THEME_CTRL_H + 22;
			fill.SetCoord( xa, y );
			y += checkPitch;
		}
		else
			y -= THEME_LABEL_TALL;

		bonus.SetCoord( xa, y );
		y += checkPitch;
		merged.SetCoord( xa, y );
		y += checkPitch;
		torch.SetCoord( xa, y );
		y += checkPitch + 6;

		hint.SetRect( xa, y, right.sz.w, right.pt.y + right.sz.h - y );
		return;
	}

	// the WON fallback (ui_theme 0)
	starts.SetRect( 72, 230, 330, 400 );
	playerName.SetRect( 440, 250, 250, 32 );
	if( m_bSolo )
		companions.SetRect( 720, 250, 200, 32 );
	else
	{
		players.SetRect( 720, 250, 200, 32 );
		password.SetRect( 440, 320, 250, 32 );
		fill.SetCoord( 720, 320 );
	}
	bonus.SetCoord( 440, 390 );
	merged.SetCoord( 440, 430 );
	torch.SetCoord( 440, 470 );
	hint.SetRect( 440, 520, 480, 120 );
}

void CMenuCoFStoryStart::Refresh( void )
{
	char name[64];

	startModel.Update();
	const int row = CMenuCoFStoryStartModel::Find( EngFuncs::GetCvarString( COF_STORY_CVAR_START ));
	starts.SetCurrentIndex( row >= 0 ? row : 0 );

	UI_CoFStoryDefaultName( name, sizeof( name ));
	playerName.SetBuffer( name );
	password.SetBuffer( "" );
	players.SetCurrentValue( (float)UI_CoFStoryClamp( EngFuncs::GetCvarFloat( COF_STORY_CVAR_PLAYERS ), 2, 4 ));
	companions.SetCurrentValue( (float)UI_CoFStoryClamp( EngFuncs::GetCvarFloat( COF_STORY_CVAR_COMPANIONS ), 1, COF_STORY_MAX_COMPANIONS ));
	fill.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_FILL ) != 0.0f;
	bonus.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_BONUS ) != 0.0f;
	merged.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_MERGED ) != 0.0f;
	torch.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_TORCH ) != 0.0f;

	m_bError = false;
	m_iInviteTime = uiStatic.realTime;
	if( m_bSolo )
		Q_strncpy( m_szInvite, L( "You and one to three AI companions, on this PC only. Enter skips the cutscenes." ), sizeof( m_szInvite ));
	else
		UI_CoFStoryInviteText( m_szInvite, sizeof( m_szInvite ));
}

bool CMenuCoFStoryStart::Start( void )
{
	const int row = starts.GetCurrentIndex();

	if( row < 0 || row >= COF_STORY_NUM_STARTS || !UI_CoFStoryCanStart( ))
		return false;

	const cofStoryChapter_t *start = &g_cofStoryStarts[row];

	if( !EngFuncs::IsMapValid( start->map ))
	{
		Con_Printf( "Cry of Fear co-op: %s is not installed\n", start->map );
		return false;
	}

	if( !UI_CoFStoryFieldsValid( playerName, m_bSolo ? NULL : &password, m_szInvite, sizeof( m_szInvite )))
	{
		m_bError = true;
		Con_Printf( "Cry of Fear co-op: %s\n", m_szInvite );
		return false;
	}

	char name[64], pass[80];
	UI_CoFStoryFieldName( name, sizeof( name ), playerName );
	CoFStory_Clean( pass, sizeof( pass ), password.GetBuffer(), COF_STORY_PASSWORD_MAX, false );

	const int wanted = UI_CoFStoryClamp( players.GetCurrentValue(), 2, 4 );
	const int ais = UI_CoFStoryClamp( companions.GetCurrentValue(), 1, COF_STORY_MAX_COMPANIONS );
	int places = 0, count = 0;
	CoFStory_PartySize( m_bSolo, wanted, ais, fill.bChecked, 0, &places, &count );

	cofStoryStart_t s;
	s.solo = m_bSolo;
	s.cont = false;
	s.name = name;
	s.password = pass;
	s.map = start->map;
	s.slot = "";
	s.maps = EngFuncs::GetCvarString( COF_STORY_CVAR_MAPS );
	s.places = places;
	s.companions = count;
	s.bonusRoom = bonus.bChecked;
	s.merged = merged.bChecked;
	s.flashlight = torch.bChecked;
	UI_CoFStoryHostTunables( &s );

	static char text[COF_STORY_TEXT_MAX];
	if( !CoFStory_StartCommands( text, sizeof( text ), &s ))
	{
		Con_Printf( "Cry of Fear co-op: this start was refused\n" );
		return false;
	}

	// remember the page, not the password
	EngFuncs::CvarSetString( COF_STORY_CVAR_START, start->map );
	EngFuncs::CvarSetValue( COF_STORY_CVAR_BONUS, bonus.bChecked ? 1.0f : 0.0f );
	EngFuncs::CvarSetValue( COF_STORY_CVAR_MERGED, merged.bChecked ? 1.0f : 0.0f );
	EngFuncs::CvarSetValue( COF_STORY_CVAR_TORCH, torch.bChecked ? 1.0f : 0.0f );
	if( m_bSolo )
		EngFuncs::CvarSetValue( COF_STORY_CVAR_COMPANIONS, (float)ais );
	else
	{
		EngFuncs::CvarSetValue( COF_STORY_CVAR_PLAYERS, (float)wanted );
		EngFuncs::CvarSetValue( COF_STORY_CVAR_FILL, fill.bChecked ? 1.0f : 0.0f );
	}

	char what[160];
	snprintf( what, sizeof( what ), "%s %s, %d places, %d AI companions", m_bSolo ? "playing with AI from" : "hosting",
		start->map, places, count );
	UI_CoFStoryRun( what, text );
	return true;
}

void CMenuCoFStoryStart::SetForTest( const char *map, int maxpl, int ais )
{
	const int row = CMenuCoFStoryStartModel::Find( map );

	if( map && map[0] && strcmp( map, "-" ))
	{
		if( row < 0 )
			Con_Printf( "menu_cof_story_start: %s is not a start of the list\n", map );
		else
			starts.SetCurrentIndex( row );
	}
	if( maxpl >= 2 )
		players.SetCurrentValue( (float)UI_CoFStoryClamp( (float)maxpl, 2, 4 ));
	if( ais >= 1 )
		companions.SetCurrentValue( (float)UI_CoFStoryClamp( (float)ais, 1, COF_STORY_MAX_COMPANIONS ));
}

// ---------------------------------------------------------------------------
// Continue co-op: the copy's co-op checkpoints, newest first
// ---------------------------------------------------------------------------

class CMenuCoFStoryCheckpointModel : public CMenuBaseModel
{
public:
	CMenuCoFStoryCheckpointModel() : m_iCount( 0 ) { }

	void Update() override;
	int GetColumns() const override { return 2; }
	int GetRows() const override { return m_iCount; }
	const char *GetCellText( int line, int column ) override
	{
		if( line < 0 || line >= m_iCount )
			return "";
		return column == 0 ? m_szTitle[line] : ( m_list[line].section[0] ? m_list[line].section : m_list[line].map );
	}

	const cofStoryCheckpoint_t *Get( int line ) const
	{
		return ( line < 0 || line >= m_iCount ) ? NULL : &m_list[line];
	}

	int Find( const char *slot ) const
	{
		for( int i = 0; slot && i < m_iCount; i++ )
			if( !strcmp( m_list[i].slot, slot ))
				return i;
		return -1;
	}

private:
	cofStoryCheckpoint_t m_list[COF_STORY_MAX_CHECKPOINTS];
	char m_szTitle[COF_STORY_MAX_CHECKPOINTS][48];
	int m_iCount;
};

/*
The folder the engine reads them from (sv_cof_session.inc through sv_save.c's save paths, SV_SaveFSOpen): the
runtime root's SAVE/cofcoop with cof_save_root_compat (the app's copies), the game's own save/cofcoop otherwise.
Only that one: the engine does not load from the other. Newest first, every file sorted before the list is cut.
*/
void CMenuCoFStoryCheckpointModel::Update( void )
{
	const bool root = EngFuncs::GetCvarFloat( "cof_save_root_compat" ) != 0.0f;
	static char paths[COF_STORY_MAX_SAVE_FILES][96];
	int numFiles = 0, n = 0;
	char **files = EngFuncs::GetFilesList( root ? "SAVE/cofcoop/*.cof" : "save/cofcoop/*.cof", &numFiles, root ? 0 : 1 );

	m_iCount = 0;

	// the list is the engine's own buffer, valid until the next search: copied first
	for( int i = 0; files && i < numFiles && n < COF_STORY_MAX_SAVE_FILES; i++ )
		CoFStory_Copy( paths[n++], sizeof( paths[0] ), files[i] );

	// newest first (insertion sort on the files' times; CompareFileTime: 1 when the first is newer)
	for( int i = 1; i < n; i++ )
	{
		for( int j = i; j > 0; j-- )
		{
			int newer = 0;
			char p[96];

			EngFuncs::CompareFileTime( paths[j], paths[j - 1], &newer );
			if( newer <= 0 )
				break;
			memcpy( p, paths[j], sizeof( p ));
			memcpy( paths[j], paths[j - 1], sizeof( p ));
			memcpy( paths[j - 1], p, sizeof( p ));
		}
	}

	for( int i = 0; i < n && m_iCount < COF_STORY_MAX_CHECKPOINTS; i++ )
	{
		char slot[64];
		int length = 0;
		byte *raw;

		COM_FileBase( paths[i], slot, sizeof( slot ));
		if( !CoFStory_SlotValid( slot ))
			continue;
		raw = EngFuncs::COM_LoadFile( paths[i], &length );
		if( !raw )
			continue;
		if( length > 0 && CoFStory_ParseCheckpoint( slot, raw, (size_t)length, &m_list[m_iCount] ))
		{
			CoFStory_SlotTitle( m_list[m_iCount].slot, m_szTitle[m_iCount], sizeof( m_szTitle[0] ));
			m_iCount++;
		}
		EngFuncs::COM_FreeFile( raw );
	}
}

class CMenuCoFStoryContinue : public CMenuFramework
{
public:
	CMenuCoFStoryContinue() : CMenuFramework( "CMenuCoFStoryContinue" ), m_pHost( NULL ), m_pSolo( NULL )
	{
		m_szDetail[0] = 0;
	}

	void Show() override
	{
		CMenuFramework::Show();
		Refresh();
	}

	// the Play with AI and Host buttons; also the test hook menu_cof_story_continue
	bool Start( bool solo );
	bool SelectForTest( const char *slot );

private:
	void _Init() override;
	void _VidInit() override;
	void Refresh( void );
	void UpdateDetail( void );
	void HostCb( void ) { Start( false ); }
	void SoloCb( void ) { Start( true ); }

	CMenuTable     checkpoints;
	CMenuCoFStoryCheckpointModel model;
	CMenuAction    detail;

	CMenuField     playerName;
	CMenuField     password;
	CMenuCheckBox  fill;
	CMenuSpinControl companions;
	CMenuCheckBox  bonus;
	CMenuCheckBox  torch;

	CMenuPicButton *m_pHost;
	CMenuPicButton *m_pSolo;

	char m_szDetail[700];
};

void CMenuCoFStoryContinue::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Continue co-op" );
	if( theme )
		SetPanel( "CONTINUE CO-OP", 900, 560 );

	checkpoints.SetCharSize( QM_SMALLFONT );
	checkpoints.SetupColumn( 0, L( "Checkpoint" ), 0.56f );
	checkpoints.SetupColumn( 1, L( "Level" ), 0.44f );
	checkpoints.SetModel( &model );
	checkpoints.onChanged = VoidCb( &CMenuCoFStoryContinue::UpdateDetail );

	detail.iFlags = QMF_INACTIVE;
	detail.colorBase = uiColorHelp;
	detail.SetCharSize( QM_SMALLFONT );
	detail.szName = m_szDetail;

	playerName.szName = L( "Your name" );
	playerName.iMaxLength = COF_STORY_NAME_MAX;
	playerName.szStatusText = L( "Items come back only under the name they were saved with" );
	playerName.onChanged = VoidCb( &CMenuCoFStoryContinue::UpdateDetail );

	password.szName = L( "Password" );
	password.iMaxLength = COF_STORY_PASSWORD_MAX;
	password.bHideInput = true;
	password.szStatusText = L( "Host: optional, friends need it to join; never saved" );

	fill.szName = L( "AI in the empty places" );
	fill.szStatusText = L( "Host: an AI companion in each place a friend has not taken" );

	companions.szName = L( "AI companions" );
	companions.Setup( 1, COF_STORY_MAX_COMPANIONS, 1 );
	companions.SetDisplayPrecision( 0 );
	companions.SetCharSize( QM_SMALLFONT );
	companions.szStatusText = L( "Play with AI: as many as the checkpoint has places for" );

	// the app's Continue shows these too; merged maps is the checkpoint's own
	bonus.szName = L( "Open the bonus room" );
	bonus.szStatusText = L( "Ticked: open for everyone. Unticked: as far as you unlocked it in Cry of Fear" );

	torch.szName = L( "Co-op flashlight on F" );
	torch.szStatusText = L( "A free torch for everyone; the original has only Simon's phone" );

	if( !theme )
		AddItem( banner );

	AddItem( checkpoints );
	AddItem( detail );
	AddItem( playerName );
	AddItem( companions );
	AddItem( password );
	AddItem( fill );
	AddItem( bonus );
	AddItem( torch );

	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFStoryContinue::Hide ), QMF_NOTIFY );
	m_pSolo = AddButton( L( "Play with AI" ), L( "Continue from this checkpoint with AI companions, on this PC" ),
		PC_LOAD_GAME, VoidCb( &CMenuCoFStoryContinue::SoloCb ), QMF_NOTIFY );
	m_pHost = AddButton( L( "Host" ), L( "Continue from this checkpoint and host it for friends" ),
		PC_CREATE_GAME, VoidCb( &CMenuCoFStoryContinue::HostCb ), QMF_NOTIFY );
}

void CMenuCoFStoryContinue::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect left  = ColumnRect2( 0, 0.5f );
		Rect right = ColumnRect2( 1, 0.5f );
		const int colW = ( right.sz.w - THEME_GUTTER2 ) / 2;
		const int xa = right.pt.x;
		const int xb = right.pt.x + colW + THEME_GUTTER2;
		int y = right.pt.y + THEME_LABEL_TALL + 8;

		checkpoints.SetRect( left.pt.x, left.pt.y, left.sz.w, left.sz.h );

		playerName.SetRect( xa, y, colW, THEME_CTRL_H );
		companions.SetRect( xb, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		password.SetRect( xa, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_H + 22;
		fill.SetCoord( xa, y );
		y += THEME_CHECK_SIZE + 18;
		bonus.SetCoord( xa, y );
		y += THEME_CHECK_SIZE + 18;
		torch.SetCoord( xa, y );
		y += THEME_CHECK_SIZE + 24;

		detail.SetRect( xa, y, right.sz.w, right.pt.y + right.sz.h - y );
		return;
	}

	checkpoints.SetRect( 72, 230, 420, 400 );
	playerName.SetRect( 530, 250, 200, 32 );
	companions.SetRect( 760, 250, 180, 32 );
	password.SetRect( 530, 320, 200, 32 );
	fill.SetCoord( 760, 320 );
	bonus.SetCoord( 530, 380 );
	torch.SetCoord( 530, 420 );
	detail.SetRect( 530, 470, 420, 200 );
}

void CMenuCoFStoryContinue::Refresh( void )
{
	char name[64];

	model.Update();
	checkpoints.SetCurrentIndex( 0 );

	UI_CoFStoryDefaultName( name, sizeof( name ));
	playerName.SetBuffer( name );
	password.SetBuffer( "" );
	fill.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_FILL ) != 0.0f;
	bonus.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_BONUS ) != 0.0f;
	torch.bChecked = EngFuncs::GetCvarFloat( COF_STORY_CVAR_TORCH ) != 0.0f;
	companions.SetCurrentValue( (float)UI_CoFStoryClamp( EngFuncs::GetCvarFloat( COF_STORY_CVAR_COMPANIONS ), 1, COF_STORY_MAX_COMPANIONS ));

	const bool any = model.GetRows() > 0;
	if( m_pHost ) m_pHost->SetGrayed( !any );
	if( m_pSolo ) m_pSolo->SetGrayed( !any );
	UpdateDetail();
}

void CMenuCoFStoryContinue::UpdateDetail( void )
{
	const cofStoryCheckpoint_t *c = model.Get( checkpoints.GetCurrentIndex( ));
	cofStoryText_t t;
	char name[64], warning[512];

	CoFStory_TextInit( &t, m_szDetail, sizeof( m_szDetail ));
	if( !c )
	{
		CoFStory_TextAdd( &t, "%s", L( "No co-op checkpoint yet. The game makes one at every level change and at "
			"each tape recorder save of a co-op game." ));
		return;
	}

	char title[48];
	CoFStory_SlotTitle( c->slot, title, sizeof( title ));
	CoFStory_TextAdd( &t, "%s: %s, %d places%s", title, c->section[0] ? c->section : c->map, c->players,
		c->merged ? ", merged maps" : "" );
	if( c->numNames )
	{
		CoFStory_TextAdd( &t, ".\nSaved by " );
		for( int i = 0; i < c->numNames; i++ )
			CoFStory_TextAdd( &t, "%s%s", i ? ", " : "", c->names[i] );
	}
	CoFStory_TextAdd( &t, ".\nThe game keeps the checkpoint's %d places, and its own merged maps setting.", c->players );

	UI_CoFStoryFieldName( name, sizeof( name ), playerName );
	if( CoFStory_ContinueWarning( c, name, warning, sizeof( warning )))
		CoFStory_TextAdd( &t, "\n%s", warning );
}

bool CMenuCoFStoryContinue::Start( bool solo )
{
	const cofStoryCheckpoint_t *c = model.Get( checkpoints.GetCurrentIndex( ));

	if( !c || !UI_CoFStoryCanStart( ))
		return false;

	if( !EngFuncs::IsMapValid( c->map ))
	{
		Con_Printf( "Cry of Fear co-op: %s is not installed\n", c->map );
		return false;
	}

	if( !UI_CoFStoryFieldsValid( playerName, solo ? NULL : &password, m_szDetail, sizeof( m_szDetail )))
	{
		Con_Printf( "Cry of Fear co-op: %s\n", m_szDetail );
		return false;
	}

	char name[64], pass[80];
	UI_CoFStoryFieldName( name, sizeof( name ), playerName );
	CoFStory_Clean( pass, sizeof( pass ), password.GetBuffer(), COF_STORY_PASSWORD_MAX, false );

	const int ais = UI_CoFStoryClamp( companions.GetCurrentValue(), 1, COF_STORY_MAX_COMPANIONS );
	int places = 0, count = 0;
	CoFStory_PartySize( solo, c->players, ais, fill.bChecked, c->players, &places, &count );

	cofStoryStart_t s;
	s.solo = solo;
	s.cont = true;
	s.name = name;
	s.password = pass;
	s.map = c->map;
	s.slot = c->slot;
	s.maps = EngFuncs::GetCvarString( COF_STORY_CVAR_MAPS );
	s.places = places;
	s.companions = count;
	s.bonusRoom = bonus.bChecked;
	s.merged = c->merged;	// a continued session keeps the mode it was saved with
	s.flashlight = torch.bChecked;
	UI_CoFStoryHostTunables( &s );

	static char text[COF_STORY_TEXT_MAX];
	if( !CoFStory_StartCommands( text, sizeof( text ), &s ))
	{
		Con_Printf( "Cry of Fear co-op: this checkpoint was refused\n" );
		return false;
	}

	if( !solo )
		EngFuncs::CvarSetValue( COF_STORY_CVAR_FILL, fill.bChecked ? 1.0f : 0.0f );
	EngFuncs::CvarSetValue( COF_STORY_CVAR_BONUS, bonus.bChecked ? 1.0f : 0.0f );
	EngFuncs::CvarSetValue( COF_STORY_CVAR_TORCH, torch.bChecked ? 1.0f : 0.0f );

	char what[160];
	snprintf( what, sizeof( what ), "continuing %s (%s) %s, %d places, %d AI companions", c->slot, c->map,
		solo ? "with AI" : "as host", places, count );
	UI_CoFStoryRun( what, text );
	return true;
}

bool CMenuCoFStoryContinue::SelectForTest( const char *slot )
{
	if( !slot || !slot[0] || !strcmp( slot, "-" ))
		return true;

	const int row = model.Find( slot );
	if( row < 0 )
	{
		Con_Printf( "menu_cof_story_continue: no usable checkpoint %s\n", slot );
		return false;
	}
	checkpoints.SetCurrentIndex( row );
	UpdateDetail();
	return true;
}

// ---------------------------------------------------------------------------
// registration and cfg-only test hooks
// ---------------------------------------------------------------------------

static CMenuCoFStoryStart *menu_cofstoryhost = NULL;
static CMenuCoFStoryStart *menu_cofstorysolo = NULL;
static CMenuCoFStoryContinue *menu_cofstorycontinue = NULL;

static void UI_CoFStory_Precache( void )
{
	menu_cofstoryhost = new CMenuCoFStoryStart( false );
	menu_cofstorysolo = new CMenuCoFStoryStart( true );
	menu_cofstorycontinue = new CMenuCoFStoryContinue();

	EngFuncs::CvarRegister( COF_STORY_CVAR_START,      COF_STORY_NEW_GAME_MAP, FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_PLAYERS,    "2", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_FILL,       "0", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_COMPANIONS, "1", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_BONUS,      "0", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_MERGED,     "1", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_TORCH,      "0", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_STORY_CVAR_MAPS,       "", 0 );
}

static void UI_CoFStory_Shutdown( void )
{
	delete menu_cofstoryhost;
	menu_cofstoryhost = NULL;
	delete menu_cofstorysolo;
	menu_cofstorysolo = NULL;
	delete menu_cofstorycontinue;
	menu_cofstorycontinue = NULL;
}

void UI_CoFStoryHost_Menu( void )
{
	menu_cofstoryhost->Show();
}

void UI_CoFStorySolo_Menu( void )
{
	menu_cofstorysolo->Show();
}

void UI_CoFStoryContinue_Menu( void )
{
	menu_cofstorycontinue->Show();
}

ADD_MENU4( menu_cofstoryhost, UI_CoFStory_Precache, UI_CoFStoryHost_Menu, UI_CoFStory_Shutdown );
ADD_MENU4( menu_cofstorysolo, NULL, UI_CoFStorySolo_Menu, NULL );
ADD_MENU4( menu_cofstorycontinue, NULL, UI_CoFStoryContinue_Menu, NULL );

static int UI_CoFStoryArgInt( int i )
{
	if( EngFuncs::CmdArgc() <= i )
		return -1;

	const char *s = EngFuncs::CmdArgv( i );
	if( !s[0] || !strcmp( s, "-" ))
		return -1;
	return atoi( s );
}

/*
=================
menu_cof_story_start <host|solo> [map] [players] [companions]

cfg-only test hook, because this project may not inject the clicks: opens Host co-op or Play with AI exactly as
the Co-op list does, moves the start list and the spin controls the way the user would (each argument optional,
"-" leaves a control alone), then runs the Start button's own handler.
=================
*/
static void UI_CoFStoryStart_f( void )
{
	const bool solo = EngFuncs::CmdArgc() > 1 && !stricmp( EngFuncs::CmdArgv( 1 ), "solo" );
	CMenuCoFStoryStart *page = solo ? menu_cofstorysolo : menu_cofstoryhost;

	page->Show();
	page->SetForTest( EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "", UI_CoFStoryArgInt( 3 ), UI_CoFStoryArgInt( 4 ));
	page->Start();
}

/*
=================
menu_cof_story_continue <host|solo> [slot]

The same for Continue co-op: the newest checkpoint, or the one named, then the Host or Play with AI button.
=================
*/
static void UI_CoFStoryContinue_f( void )
{
	const bool solo = EngFuncs::CmdArgc() > 1 && !stricmp( EngFuncs::CmdArgv( 1 ), "solo" );

	menu_cofstorycontinue->Show();
	if( menu_cofstorycontinue->SelectForTest( EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "" ))
		menu_cofstorycontinue->Start( solo );
}

ADD_COMMAND( menu_cof_story_start, UI_CoFStoryStart_f );
ADD_COMMAND( menu_cof_story_continue, UI_CoFStoryContinue_f );
