/*
CoFCoop.cpp -- Cry of Fear co-op bridge: the Host co-op and Join co-op pages

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

Cry of Fear's own Join Server / Host Server buttons sent "optionshack 0/1",
which only CoF's patched GoldSrc engine understood (it opened the Steam server
browser and GameUI's stock Create Server dialog). Under FWGS they are dead, and
mainui's stock pages do not fit the game: the Create Server page issues a
"disconnect" first, which under the unified UI (cof_ui_menu_map_redirect)
re-arms the single-player background map and forces maxplayers 1, so the game
started with single-player rules and no lobby.

Co-op itself needs nothing but plain GoldSrc multiplayer: hl.dll picks its
co-op rules from deathmatch != 0 (the "coop" cvar would FORCE single-player
rules), the lobby lives in hl.dll on maps whose worldspawn carries iuser1, and
the lobby's autostart time is the engine's mp_footsteps cvar, relabelled by
the original settings.scr. Measured in stage1/coop-investigation-20260922.

Host co-op issues exactly, in one command buffer write and with NO disconnect:

    name "<player>"
    hostname "<server>"
    sv_password "<password>"
    sv_lan <0|1>
    mp_footsteps <lobby autostart seconds>
    sv_auto_respawn_time <seconds>
    pausable 0
    difficulty <1..4>
    coop 0
    deathmatch 1
    maxplayers <2..4>
    map <entry map>

The "map" after a maxplayers change shuts the background server down by
itself ("Server was killed due to maxclients change", sv_init.c).

Join co-op issues "name "<player>"" and "connect <address>"; FWGS shuts the
local background server down itself when it connects. When the remote session
ends, the engine brings this menu back over the background map
(cof_ui_remote_end_menu, patches/cof-coop-bridge.patch).

Both pages remember their settings in archived cof_coop_* cvars; the password
is deliberately not stored. The player name is the engine's own archived
"name": Cry of Fear's shipped config.cfg says "Unknown" (Steam used to override
it), which is what the co-op lobby and the HUD player list showed, so a name of
"Unknown" is offered as the OS user name instead.

Nothing here is reachable unless the running game directory is "cryoffear".
*/

#include "Framework.h"
#include "Action.h"
#include "CheckBox.h"
#include "Field.h"
#include "PicButton.h"
#include "SpinControl.h"
#include "Table.h"
#include "BaseArrayModel.h"
#include "MenuStrings.h"
#include "Utils.h"
#include "CryOfFear.h"

// the page settings, archived so a host does not type them twice
#define COF_COOP_CVAR_CAMPAIGN   "cof_coop_campaign"
#define COF_COOP_CVAR_DIFFICULTY "cof_coop_difficulty"
#define COF_COOP_CVAR_PLAYERS    "cof_coop_players"
#define COF_COOP_CVAR_AUTOSTART  "cof_coop_autostart"
#define COF_COOP_CVAR_RESPAWN    "cof_coop_respawn"
#define COF_COOP_CVAR_HOSTNAME   "cof_coop_hostname"
#define COF_COOP_CVAR_LAN        "cof_coop_lan"
#define COF_COOP_CVAR_ADDRESS    "cof_coop_address"

// the original settings.scr defaults: "Lobby autostart time" 120, "Auto
// respawn time" 384, maxplayers 4 (every co-op map has four spawn slots and
// the lobby panel four portraits)
#define COF_COOP_DEF_AUTOSTART  "120"
#define COF_COOP_DEF_RESPAWN    "384"
#define COF_COOP_DEF_PLAYERS    "4"
#define COF_COOP_DEF_HOSTNAME   "Cry of Fear co-op"

#define COF_COOP_MAX_AUTOSTART  600
#define COF_COOP_MAX_RESPAWN    9999

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

/*
=================
UI_CoFCoopQuoteSafe

Everything a page puts between quotes on the command line comes from a text
field. A quote, a newline or a semicolon would end the argument or the
command and start another one, so they are dropped, as are control bytes.
=================
*/
static void UI_CoFCoopQuoteSafe( char *out, size_t size, const char *in )
{
	size_t n = 0;

	if( !size )
		return;

	for( ; in && *in && n + 1 < size; in++ )
	{
		const unsigned char c = (unsigned char)*in;

		if( c < 32 || c == '"' || c == ';' || c == 127 )
			continue;

		out[n++] = (char)c;
	}

	out[n] = 0;
}

/*
=================
UI_CoFCoopAddressSafe

A server address: a host name or IPv4/IPv6 literal with an optional port.
Anything else is refused rather than cleaned, so a typo is not silently
turned into a different server.
=================
*/
static bool UI_CoFCoopAddressSafe( const char *in )
{
	if( !in || !in[0] )
		return false;

	for( const char *p = in; *p; p++ )
	{
		if( *p >= 'a' && *p <= 'z' ) continue;
		if( *p >= 'A' && *p <= 'Z' ) continue;
		if( *p >= '0' && *p <= '9' ) continue;
		if( *p == '.' || *p == ':' || *p == '-' || *p == '_' || *p == '[' || *p == ']' ) continue;
		return false;
	}

	return true;
}

/*
=================
UI_CoFCoopDefaultName

The name the pages offer. The engine's archived "name" wins, except for the
"Unknown" Cry of Fear's config.cfg ships with, which is replaced by the OS
user name the engine already knows (ui_username), then by "Player".
=================
*/
static const char *UI_CoFCoopDefaultName( void )
{
	const char *name = EngFuncs::GetCvarString( "name" );

	if( name && name[0] && stricmp( name, "Unknown" ) && stricmp( name, "Player" ))
		return name;

	const char *user = EngFuncs::GetCvarString( "ui_username" );

	if( user && user[0] && stricmp( user, "Unknown" ))
		return user;

	return "Player";
}

static int UI_CoFCoopClampInt( float value, int lo, int hi )
{
	int v = (int)( value + ( value >= 0.0f ? 0.5f : -0.5f ));

	if( v < lo ) v = lo;
	if( v > hi ) v = hi;
	return v;
}

// ---------------------------------------------------------------------------
// the co-op campaigns
// ---------------------------------------------------------------------------

/*
The entry maps, measured from the shipped entity lumps
(stage1/coop-investigation-20260922/RESULTS.md section 1.6). The two with a
lobby (worldspawn iuser1) are cof_campaign_01 and cof_manhunt_campaign; the
survival maps start at once and run a 30-minute timer. A map that is not
installed is left off the list.
*/
struct cofCoopCampaign_t
{
	const char *map;
	const char *name;
	const char *detail;
};

static const cofCoopCampaign_t s_cofCoopCampaigns[] =
{
	{ "cof_campaign_01",      "Story co-op",
		"Chapters 9 to 12, starting with \"Where is Simon?\".\nLobby: everyone gets ready, or the autostart time runs out." },
	{ "cof_manhunt_campaign", "Manhunt",
		"The Manhunt campaign, from \"A new beginning\" to the cabin.\nLobby: everyone gets ready, or the autostart time runs out." },
	{ "cof_suicide1",         "Survival 1",
		"Survive for 30 minutes. No lobby: the game starts at once.\nThe next survival map follows." },
	{ "cof_suicide2",         "Survival 2",
		"Survive for 30 minutes. No lobby: the game starts at once.\nThe next survival map follows." },
	{ "cof_suicide3",         "Survival 3",
		"Survive for 30 minutes. No lobby: the game starts at once.\nThe next survival map follows." },
	{ "cof_suicide4",         "Survival 4",
		"Survive for 30 minutes. No lobby: the game starts at once.\nThe first survival map follows." },
};

#define COF_COOP_NUM_CAMPAIGNS ( (int)( sizeof( s_cofCoopCampaigns ) / sizeof( s_cofCoopCampaigns[0] )))

class CMenuCoFCoopCampaignModel : public CMenuBaseModel
{
public:
	CMenuCoFCoopCampaignModel() : m_iCount( 0 ) { }

	void Update() override
	{
		m_iCount = 0;

		for( int i = 0; i < COF_COOP_NUM_CAMPAIGNS; i++ )
		{
			if( EngFuncs::IsMapValid( s_cofCoopCampaigns[i].map ))
				m_iIndex[m_iCount++] = i;
		}
	}

	int GetColumns() const override { return 2; }
	int GetRows() const override { return m_iCount; }
	const char *GetCellText( int line, int column ) override
	{
		const cofCoopCampaign_t *c = Get( line );

		if( !c )
			return "";

		return column == 0 ? L( c->name ) : c->map;
	}

	const cofCoopCampaign_t *Get( int line ) const
	{
		if( line < 0 || line >= m_iCount )
			return NULL;
		return &s_cofCoopCampaigns[m_iIndex[line]];
	}

	int Find( const char *map ) const
	{
		for( int i = 0; map && i < m_iCount; i++ )
		{
			if( !stricmp( s_cofCoopCampaigns[m_iIndex[i]].map, map ))
				return i;
		}
		return -1;
	}

private:
	int m_iIndex[COF_COOP_NUM_CAMPAIGNS];
	int m_iCount;
};

// Easy / Medium / Difficult, plus Nightmare only when it is unlocked - the
// same gate as the single-player difficulty page (scriptsettings.dat line 83)
static const char *s_cofCoopSkill[] = { "Easy", "Medium", "Difficult", "Nightmare" };

class CMenuCoFCoopSkillModel : public CMenuBaseArrayModel
{
public:
	CMenuCoFCoopSkillModel() : m_bNightmare( false ) { }

	void Update() override { m_bNightmare = ( UI_CoFNightmareStatus() == 1 ); }
	int GetRows() const override { return m_bNightmare ? 4 : 3; }
	const char *GetText( int line ) override
	{
		if( line < 0 || line >= GetRows( ))
			line = 1;
		return L( s_cofCoopSkill[line] );
	}

	bool m_bNightmare;
};

// ---------------------------------------------------------------------------
// Host co-op
// ---------------------------------------------------------------------------

class CMenuCoFHost : public CMenuFramework
{
public:
	CMenuCoFHost() : CMenuFramework( "CMenuCoFHost" ) { m_szDetail[0] = 0; }

	void Show() override
	{
		// base Show() runs Init() the first time, which creates the items
		CMenuFramework::Show();
		Refresh();
	}

	// the Start button; also the cfg-only test hook menu_cof_host_start
	bool Start( void );

	// test hook: set the controls the way a user would, then Start()
	void SetForTest( const char *map, int skill, int players, int autostart, int respawn, int lan );

private:
	void _Init() override;
	void _VidInit() override;

	void Refresh( void );
	void UpdateDetail( void );
	void StartCb( void ) { Start(); }

	CMenuTable     campaigns;
	CMenuCoFCoopCampaignModel campaignModel;
	CMenuAction    detail;

	CMenuField     playerName;
	CMenuField     hostName;
	CMenuField     password;
	CMenuCheckBox  lanOnly;

	CMenuSpinControl difficulty;
	CMenuCoFCoopSkillModel skillModel;
	CMenuSpinControl players;
	CMenuSpinControl autostart;
	CMenuField     respawn;

	char m_szDetail[320];
};

void CMenuCoFHost::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Host co-op" );

	if( theme )
		SetPanel( "HOST CO-OP", 900, 560 );

	campaigns.SetCharSize( QM_SMALLFONT );
	campaigns.SetupColumn( 0, L( "Campaign" ), 0.46f );
	campaigns.SetupColumn( 1, L( "First map" ), 0.54f );
	campaigns.SetModel( &campaignModel );
	campaigns.onChanged = VoidCb( &CMenuCoFHost::UpdateDetail );

	detail.iFlags = QMF_INACTIVE;
	detail.colorBase = uiColorHelp;
	detail.SetCharSize( QM_SMALLFONT );
	detail.szName = m_szDetail;

	playerName.szName = L( "Your name" );
	playerName.iMaxLength = 31;
	playerName.szStatusText = L( "Shown in the lobby and on the co-op player list" );

	hostName.szName = L( "Server name" );
	hostName.iMaxLength = 28;
	hostName.szStatusText = L( "The name of this game in server lists" );

	password.szName = L( "Password" );
	password.iMaxLength = 16;
	password.bHideInput = true;
	password.szStatusText = L( "Empty for an open game; never saved" );

	lanOnly.szName = L( "LAN only" );
	lanOnly.szStatusText = L( "Only players on the local network can join" );

	difficulty.szName = L( "Difficulty" );
	difficulty.Setup( &skillModel );
	difficulty.SetCharSize( QM_SMALLFONT );
	difficulty.szStatusText = L( "Nightmare appears once it is unlocked" );

	players.szName = L( "Players" );
	players.Setup( 2, 4, 1 );
	players.SetDisplayPrecision( 0 );
	players.SetCharSize( QM_SMALLFONT );
	players.szStatusText = L( "Most players, you included (four slots)" );

	autostart.szName = L( "Lobby autostart (seconds)" );
	autostart.Setup( 0, COF_COOP_MAX_AUTOSTART, 10 );
	autostart.SetDisplayPrecision( 0 );
	autostart.SetCharSize( QM_SMALLFONT );
	autostart.szStatusText = L( "Start even if not all are ready; 0 = wait" );

	respawn.szName = L( "Auto respawn (seconds)" );
	respawn.iMaxLength = 4;
	respawn.bNumbersOnly = true;
	respawn.szStatusText = L( "Dead players come back after this long" );

	if( !theme )
		AddItem( banner );

	AddItem( campaigns );
	AddItem( detail );
	AddItem( playerName );
	AddItem( hostName );
	AddItem( password );
	AddItem( lanOnly );
	AddItem( difficulty );
	AddItem( players );
	AddItem( autostart );
	AddItem( respawn );

	// button row, secondary first: Cancel then Start
	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFHost::Hide ), QMF_NOTIFY );
	AddButton( L( "Start" ), L( "Start the server on this campaign" ),
		PC_CREATE_GAME, VoidCb( &CMenuCoFHost::StartCb ), QMF_NOTIFY );
}

void CMenuCoFHost::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect left  = ColumnRect2( 0, 0.42f );
		Rect right = ColumnRect2( 1, 0.42f );
		const int colW = ( right.sz.w - THEME_GUTTER2 ) / 2;
		const int xa = right.pt.x;
		const int xb = right.pt.x + colW + THEME_GUTTER2;
		const int detailH = THEME_LABEL_TALL * 4 + 8;

		campaigns.SetRect( left.pt.x, left.pt.y, left.sz.w, left.sz.h - detailH - 10 );
		detail.SetRect( left.pt.x, left.pt.y + left.sz.h - detailH, left.sz.w, detailH );

		// label line above every control: start one label height down
		int y = right.pt.y + THEME_LABEL_TALL + 8;

		playerName.SetRect( xa, y, colW, THEME_CTRL_H );
		difficulty.SetRect( xb, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		hostName.SetRect( xa, y, colW, THEME_CTRL_H );
		players.SetRect( xb, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		password.SetRect( xa, y, colW, THEME_CTRL_H );
		autostart.SetRect( xb, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		respawn.SetRect( xb, y, colW, THEME_CTRL_H );
		lanOnly.SetCoord( xa, y );
		return;
	}

	// the WON fallback (ui_theme 0)
	campaigns.SetRect( 360, 230, 300, 300 );
	detail.SetRect( 360, 540, 300, 120 );
	playerName.SetRect( 700, 250, 250, 32 );
	hostName.SetRect( 700, 320, 250, 32 );
	password.SetRect( 700, 390, 250, 32 );
	lanOnly.SetCoord( 700, 450 );
	difficulty.SetRect( 72, 250, 250, 32 );
	players.SetRect( 72, 320, 250, 32 );
	autostart.SetRect( 72, 390, 250, 32 );
	respawn.SetRect( 72, 460, 250, 32 );
}

void CMenuCoFHost::UpdateDetail( void )
{
	const cofCoopCampaign_t *c = campaignModel.Get( campaigns.GetCurrentIndex( ));

	if( !c )
	{
		Q_strncpy( m_szDetail, L( "No co-op map is installed." ), sizeof( m_szDetail ));
		return;
	}

	snprintf( m_szDetail, sizeof( m_szDetail ), "%s", L( c->detail ));
}

/*
=================
CMenuCoFHost::Refresh

Every control from its cvar, each time the page opens: the archived page
cvars, the engine's "name" (see UI_CoFCoopDefaultName), the live sv_password
and the game's own "difficulty" when the page cvar was never set.
=================
*/
void CMenuCoFHost::Refresh( void )
{
	campaignModel.Update();

	int row = campaignModel.Find( EngFuncs::GetCvarString( COF_COOP_CVAR_CAMPAIGN ));
	campaigns.SetCurrentIndex( row >= 0 ? row : 0 );
	UpdateDetail();

	playerName.SetBuffer( UI_CoFCoopDefaultName( ));
	hostName.SetBuffer( EngFuncs::GetCvarString( COF_COOP_CVAR_HOSTNAME ));
	password.SetBuffer( EngFuncs::GetCvarString( "sv_password" ));
	lanOnly.bChecked = EngFuncs::GetCvarFloat( COF_COOP_CVAR_LAN ) != 0.0f;

	skillModel.Update();
	difficulty.Setup( &skillModel );
	int skill = UI_CoFCoopClampInt( EngFuncs::GetCvarFloat( COF_COOP_CVAR_DIFFICULTY ), 1, 4 );
	if( skill > skillModel.GetRows( ))
		skill = skillModel.GetRows( );
	difficulty.SetCurrentValue( (float)( skill - 1 ));

	players.SetCurrentValue( (float)UI_CoFCoopClampInt( EngFuncs::GetCvarFloat( COF_COOP_CVAR_PLAYERS ), 2, 4 ));
	autostart.SetCurrentValue( (float)UI_CoFCoopClampInt( EngFuncs::GetCvarFloat( COF_COOP_CVAR_AUTOSTART ), 0, COF_COOP_MAX_AUTOSTART ));

	char buf[16];
	snprintf( buf, sizeof( buf ), "%d", UI_CoFCoopClampInt( EngFuncs::GetCvarFloat( COF_COOP_CVAR_RESPAWN ), 0, COF_COOP_MAX_RESPAWN ));
	respawn.SetBuffer( buf );
}

/*
=================
CMenuCoFHost::Start

See the top of this file for the command sequence and why it has no
"disconnect". It is written to the command buffer in one call, so nothing the
menu does afterwards can interleave with it.
=================
*/
bool CMenuCoFHost::Start( void )
{
	const cofCoopCampaign_t *c = campaignModel.Get( campaigns.GetCurrentIndex( ));

	if( !c || !EngFuncs::IsMapValid( c->map ))
	{
		Con_Printf( "Cry of Fear co-op: no installed campaign selected\n" );
		return false;
	}

	// reachable only from the main menu; never start a server on top of a
	// running game (the test hook could be typed anywhere)
	if( CL_IsActive( ))
	{
		Con_Printf( "Cry of Fear co-op: leave the current game before hosting\n" );
		return false;
	}

	char name[64], host[64], pass[64];

	UI_CoFCoopQuoteSafe( name, sizeof( name ), playerName.GetBuffer( ));
	if( !name[0] )
		Q_strncpy( name, UI_CoFCoopDefaultName( ), sizeof( name ));
	UI_CoFCoopQuoteSafe( host, sizeof( host ), hostName.GetBuffer( ));
	if( !host[0] )
		Q_strncpy( host, COF_COOP_DEF_HOSTNAME, sizeof( host ));
	UI_CoFCoopQuoteSafe( pass, sizeof( pass ), password.GetBuffer( ));

	int skill = UI_CoFCoopClampInt( difficulty.GetCurrentValue( ) + 1.0f, 1, skillModel.GetRows( ));
	const int maxpl = UI_CoFCoopClampInt( players.GetCurrentValue( ), 2, 4 );
	const int lobby = UI_CoFCoopClampInt( autostart.GetCurrentValue( ), 0, COF_COOP_MAX_AUTOSTART );
	const int resp  = UI_CoFCoopClampInt( (float)atoi( respawn.GetBuffer( )), 0, COF_COOP_MAX_RESPAWN );
	const int lan   = lanOnly.bChecked ? 1 : 0;

	// remember the page, not the password
	EngFuncs::CvarSetString( COF_COOP_CVAR_CAMPAIGN, c->map );
	EngFuncs::CvarSetValue( COF_COOP_CVAR_DIFFICULTY, (float)skill );
	EngFuncs::CvarSetValue( COF_COOP_CVAR_PLAYERS, (float)maxpl );
	EngFuncs::CvarSetValue( COF_COOP_CVAR_AUTOSTART, (float)lobby );
	EngFuncs::CvarSetValue( COF_COOP_CVAR_RESPAWN, (float)resp );
	EngFuncs::CvarSetString( COF_COOP_CVAR_HOSTNAME, host );
	EngFuncs::CvarSetValue( COF_COOP_CVAR_LAN, (float)lan );

	char cmd[1024];

	snprintf( cmd, sizeof( cmd ),
		"name \"%s\"\n"
		"hostname \"%s\"\n"
		"sv_password \"%s\"\n"
		"sv_lan %d\n"
		"mp_footsteps %d\n"
		"sv_auto_respawn_time %d\n"
		"pausable 0\n"
		"difficulty %d\n"
		"coop 0\n"
		"deathmatch 1\n"
		"maxplayers %d\n"
		"map %s\n",
		name, host, pass, lan, lobby, resp, skill, maxpl, c->map );

	Con_Printf( "Cry of Fear co-op: hosting %s (%s), difficulty %d, %d players, lobby autostart %d s, respawn %d s, %s\n",
		c->map, c->name, skill, maxpl, lobby, resp, lan ? "LAN only" : "Internet" );

	EngFuncs::ClientCmd( false, cmd );
	UI_CloseMenu();
	return true;
}

void CMenuCoFHost::SetForTest( const char *map, int skill, int maxpl, int lobby, int resp, int lan )
{
	if( map && map[0] )
	{
		const int row = campaignModel.Find( map );

		if( row < 0 )
			Con_Printf( "menu_cof_host_start: %s is not an installed co-op entry map\n", map );
		else
			campaigns.SetCurrentIndex( row );
	}

	if( skill >= 1 )
		difficulty.SetCurrentValue( (float)( UI_CoFCoopClampInt( (float)skill, 1, skillModel.GetRows( )) - 1 ));
	if( maxpl >= 1 )
		players.SetCurrentValue( (float)UI_CoFCoopClampInt( (float)maxpl, 2, 4 ));
	if( lobby >= 0 )
		autostart.SetCurrentValue( (float)UI_CoFCoopClampInt( (float)lobby, 0, COF_COOP_MAX_AUTOSTART ));
	if( resp >= 0 )
	{
		char buf[16];
		snprintf( buf, sizeof( buf ), "%d", UI_CoFCoopClampInt( (float)resp, 0, COF_COOP_MAX_RESPAWN ));
		respawn.SetBuffer( buf );
	}
	if( lan >= 0 )
		lanOnly.bChecked = lan != 0;

	UpdateDetail();
}

// ---------------------------------------------------------------------------
// Join co-op
// ---------------------------------------------------------------------------

class CMenuCoFJoin : public CMenuFramework
{
public:
	CMenuCoFJoin() : CMenuFramework( "CMenuCoFJoin" ) { }

	void Show() override
	{
		CMenuFramework::Show();
		playerName.SetBuffer( UI_CoFCoopDefaultName( ));
		address.SetBuffer( EngFuncs::GetCvarString( COF_COOP_CVAR_ADDRESS ));
	}

	// the Connect button; also the cfg-only test hook menu_cof_join_connect
	bool Connect( void );
	void SetAddress( const char *addr ) { address.SetBuffer( addr ); }

private:
	void _Init() override;
	void _VidInit() override;

	void ConnectCb( void ) { Connect(); }
	void LanCb( void );
	void WriteName( void );

	CMenuField  address;
	CMenuField  playerName;
	CMenuAction hint;
};

void CMenuCoFJoin::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Join co-op" );

	if( theme )
		SetPanel( "JOIN CO-OP", 760, 400 );

	address.szName = L( "Server address" );
	address.iMaxLength = 63;
	address.szStatusText = L( "IP or host name, optionally :port" );

	playerName.szName = L( "Your name" );
	playerName.iMaxLength = 31;
	playerName.szStatusText = L( "Shown in the lobby and on the co-op player list" );

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = L( "Ask the host for their address. The default port is 27015; over the Internet the host has to forward UDP 27015 to their computer." );

	if( !theme )
		AddItem( banner );

	AddItem( address );
	AddItem( playerName );
	AddItem( hint );

	// secondary first: LAN servers, Cancel, then Connect
	AddButton( L( "LAN servers" ), L( "Servers on the local network" ), PC_LAN_GAME,
		VoidCb( &CMenuCoFJoin::LanCb ), QMF_NOTIFY );
	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFJoin::Hide ), QMF_NOTIFY );
	AddButton( L( "Connect" ), L( "Connect to this address" ), PC_JOIN_GAME,
		VoidCb( &CMenuCoFJoin::ConnectCb ), QMF_NOTIFY );
}

void CMenuCoFJoin::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect c = ContentRect();
		int y = c.pt.y + THEME_LABEL_TALL + 8;

		address.SetRect( c.pt.x, y, c.sz.w, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		playerName.SetRect( c.pt.x, y, c.sz.w / 2, THEME_CTRL_H );
		y += THEME_CTRL_H + 18;
		hint.SetRect( c.pt.x, y, c.sz.w, THEME_LABEL_TALL * 3 + 4 );
		return;
	}

	address.SetRect( 360, 280, 400, 32 );
	playerName.SetRect( 360, 360, 250, 32 );
	hint.SetRect( 360, 420, 400, 80 );
}

void CMenuCoFJoin::WriteName( void )
{
	char name[64];

	UI_CoFCoopQuoteSafe( name, sizeof( name ), playerName.GetBuffer( ));
	if( !name[0] )
		Q_strncpy( name, UI_CoFCoopDefaultName( ), sizeof( name ));

	EngFuncs::ClientCmdF( false, "name \"%s\"\n", name );
}

bool CMenuCoFJoin::Connect( void )
{
	const char *addr = address.GetBuffer();

	if( !UI_CoFCoopAddressSafe( addr ))
	{
		Con_Printf( "Cry of Fear co-op: \"%s\" is not a server address\n", addr ? addr : "" );
		return false;
	}

	EngFuncs::CvarSetString( COF_COOP_CVAR_ADDRESS, addr );

	char name[64];
	UI_CoFCoopQuoteSafe( name, sizeof( name ), playerName.GetBuffer( ));
	if( !name[0] )
		Q_strncpy( name, UI_CoFCoopDefaultName( ), sizeof( name ));

	Con_Printf( "Cry of Fear co-op: joining %s as \"%s\"\n", addr, name );

	// one write, name first so the server sees it in the connect's userinfo
	EngFuncs::ClientCmdF( false, "name \"%s\"\nconnect %s\n", name, addr );
	UI_CloseMenu();
	return true;
}

void CMenuCoFJoin::LanCb( void )
{
	// the stock server browser, LAN tab only; it connects on its own
	WriteName();
	UI_LanGame_Menu();
}

// ---------------------------------------------------------------------------
// registration and cfg-only test hooks
// ---------------------------------------------------------------------------

static CMenuCoFHost *menu_cofhost = NULL;
static CMenuCoFJoin *menu_cofjoin = NULL;

static void UI_CoFCoop_Precache( void )
{
	menu_cofhost = new CMenuCoFHost();
	menu_cofjoin = new CMenuCoFJoin();

	EngFuncs::CvarRegister( COF_COOP_CVAR_CAMPAIGN,   "cof_campaign_01",      FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_DIFFICULTY, "2",                    FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_PLAYERS,    COF_COOP_DEF_PLAYERS,   FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_AUTOSTART,  COF_COOP_DEF_AUTOSTART, FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_RESPAWN,    COF_COOP_DEF_RESPAWN,   FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_HOSTNAME,   COF_COOP_DEF_HOSTNAME,  FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_LAN,        "0",                    FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( COF_COOP_CVAR_ADDRESS,    "",                     FCVAR_ARCHIVE );
}

static void UI_CoFCoop_Shutdown( void )
{
	delete menu_cofhost;
	menu_cofhost = NULL;
	delete menu_cofjoin;
	menu_cofjoin = NULL;
}

void UI_CoFHost_Menu( void )
{
	menu_cofhost->Show();
}

void UI_CoFJoin_Menu( void )
{
	menu_cofjoin->Show();
}

ADD_MENU4( menu_cofhost, UI_CoFCoop_Precache, UI_CoFHost_Menu, UI_CoFCoop_Shutdown );
ADD_MENU4( menu_cofjoin, NULL, UI_CoFJoin_Menu, NULL );

/*
=================
menu_cof_host_start [map] [difficulty] [players] [autostart] [respawn] [lan]

cfg-only test hook, because this project may not inject the clicks: opens the
Host co-op page exactly as the Extras item does, moves its controls the way the
user would (each argument optional, "-" or -1 leaves a control alone), then
runs the Start button's own handler. The name, server name and password come
from the page as it opened (the engine "name", cof_coop_hostname, sv_password).
=================
*/
static int UI_CoFCoopArgInt( int i )
{
	if( EngFuncs::CmdArgc() <= i )
		return -1;

	const char *s = EngFuncs::CmdArgv( i );

	if( !s[0] || !strcmp( s, "-" ))
		return -1;

	return atoi( s );
}

static void UI_CoFHostStart_f( void )
{
	const char *map = EngFuncs::CmdArgc() > 1 ? EngFuncs::CmdArgv( 1 ) : "";

	UI_CoFHost_Menu();

	if( !strcmp( map, "-" ))
		map = "";

	menu_cofhost->SetForTest( map, UI_CoFCoopArgInt( 2 ), UI_CoFCoopArgInt( 3 ),
		UI_CoFCoopArgInt( 4 ), UI_CoFCoopArgInt( 5 ), UI_CoFCoopArgInt( 6 ));
	menu_cofhost->Start();
}

/*
=================
menu_cof_join_connect [address] [cfg]

The same for the Join co-op page: opens it as the Extras item does, types the
address into the field when one is given, then runs the Connect button's own
handler. The button's command goes to the END of the command buffer, as it
does for a click; a test cfg that has to go on after the connect names a cfg
here, and it is exec'd from behind that command.
=================
*/
static void UI_CoFJoinConnect_f( void )
{
	UI_CoFJoin_Menu();

	if( EngFuncs::CmdArgc() > 1 && EngFuncs::CmdArgv( 1 )[0] && strcmp( EngFuncs::CmdArgv( 1 ), "-" ))
		menu_cofjoin->SetAddress( EngFuncs::CmdArgv( 1 ));

	if( menu_cofjoin->Connect() && EngFuncs::CmdArgc() > 2 && UI_CoFCoopAddressSafe( EngFuncs::CmdArgv( 2 )))
		EngFuncs::ClientCmdF( false, "exec %s\n", EngFuncs::CmdArgv( 2 ));
}

ADD_COMMAND( menu_cof_host_start, UI_CoFHostStart_f );
ADD_COMMAND( menu_cof_join_connect, UI_CoFJoinConnect_f );
