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

Cry of Fear Co-op 1.3.6: both pages are tabs of the main menu's CO-OP window now (menus/CoFStoryCoop.cpp), out of
Extras. Host co-op is that window's "Original co-op" (the story is hosted from its own Host co-op), and it sends
what the Cry of Fear Co-op app sends for the game's own co-op (coop_play.official_launch: the story layer, the AI
companions, merged maps and Continue off) before the lobby options above. Join co-op takes the app's join codes as
well as addresses, and a password, and joins with the story settings the app joins with
(menus/CoFStoryCoopLogic.h; tests/test_menu_coop.py).
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
#include "CoFStoryCoopLogic.h"

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
survival maps start at once and run a 30-minute timer. 1.3.9: the co-op
campaign's later maps too (coop_play.OFFICIAL_CHOICES), no lobby. A map that is
not installed is left off the list.
*/
struct cofCoopCampaign_t
{
	const char *map;
	const char *name;
	const char *detail;
};

static const cofCoopCampaign_t s_cofCoopCampaigns[] =
{
	{ "cof_campaign_01",      "Co-op campaign",
		"Chapters 9 to 12, starting with \"Where is Simon?\".\nLobby: everyone gets ready, or the autostart time runs out." },
	// 1.3.9: a later part of the campaign, as the app's own co-op page had them (a player: "it would be great to
	// have the option to select a specific level"); a player new to the game gets the part's own weapons
	// (maps/<map>.cfg, ApplyCFGSettingToPlayerLate in the game's rules)
	{ "cof_campaign_01_p2",   "Co-op campaign, part 2",
		"The co-op campaign from its second map.\nNo lobby: the game starts at once, with that part's weapons." },
	{ "cof_campaign_01_p3",   "Co-op campaign, part 3",
		"The co-op campaign from its third map.\nNo lobby: the game starts at once, with that part's weapons." },
	{ "cof_campaign_01_p4",   "Co-op campaign, part 4",
		"The co-op campaign from its last map.\nNo lobby: the game starts at once, with that part's weapons." },
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

	banner.szName = L( "Cry of Fear's own co-op" );

	if( theme )
		SetPanel( "ORIGINAL CO-OP", 900, 560 );

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
	playerName.iMaxLength = COF_STORY_NAME_MAX;
	playerName.szStatusText = L( "Shown in the lobby and on the co-op player list" );

	hostName.szName = L( "Server name" );
	hostName.iMaxLength = 28;
	hostName.szStatusText = L( "The name of this game in server lists" );

	password.szName = L( "Password" );
	password.iMaxLength = COF_STORY_PASSWORD_MAX;
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

	// 1.3.6: what the app refuses is refused, not changed (a password changed here would not be the one friends type)
	if( !CoFStory_FieldValid( playerName.GetBuffer(), COF_STORY_NAME_MAX, true, true ) ||
		!CoFStory_FieldValid( password.GetBuffer(), COF_STORY_PASSWORD_MAX, false, true ))
	{
		const bool badName = !CoFStory_FieldValid( playerName.GetBuffer(), COF_STORY_NAME_MAX, true, true );
		CoFStory_FieldMessage( m_szDetail, sizeof( m_szDetail ), badName ? "Your name" : "The password",
			badName ? COF_STORY_NAME_MAX : COF_STORY_PASSWORD_MAX, false );
		Con_Printf( "Cry of Fear co-op: %s\n", m_szDetail );
		return false;
	}

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

	// 1.3.6: the app's own co-op start (coop_play.official_launch), then the lobby options
	static char cmd[COF_STORY_TEXT_MAX];
	cofStoryOfficial_t o;
	char storyName[64], storyPass[80];

	CoFStory_Clean( storyName, sizeof( storyName ), name, COF_STORY_NAME_MAX, true );
	if( !storyName[0] )
		CoFStory_Copy( storyName, sizeof( storyName ), "Player" );
	CoFStory_Clean( storyPass, sizeof( storyPass ), pass, COF_STORY_PASSWORD_MAX, false );
	o.name = storyName;
	o.hostname = host;
	o.password = storyPass;
	o.map = c->map;
	o.players = maxpl;
	o.lan = lan;
	o.autostart = lobby;
	o.respawn = resp;
	o.difficulty = skill;
	if( !CoFStory_OfficialCommands( cmd, sizeof( cmd ), &o ))
	{
		Con_Printf( "Cry of Fear co-op: this co-op start was refused\n" );
		return false;
	}

	Con_Printf( "Cry of Fear co-op: hosting %s (%s), difficulty %d, %d players, lobby autostart %d s, respawn %d s, %s\n",
		c->map, c->name, skill, maxpl, lobby, resp, lan ? "LAN only" : "Internet" );

	// a game opened without the app: the join check's version, so the app's Join takes this game
	if( !EngFuncs::GetCvarString( "cof_build" )[0] )
		EngFuncs::ClientCmd( false, "cof_build " COF_STORY_BUILD "\n" );
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

// the name field as the app's clean_name takes it
static void UI_CoFStoryCleanName( char *out, size_t size, const char *in )
{
	CoFStory_Clean( out, size, in, COF_STORY_NAME_MAX, true );
	if( !out[0] )
		CoFStory_Clean( out, size, UI_CoFCoopDefaultName( ), COF_STORY_NAME_MAX, true );
	if( !out[0] )
		CoFStory_Copy( out, size, "Player" );
}

class CMenuCoFJoin : public CMenuFramework
{
public:
	CMenuCoFJoin() : CMenuFramework( "CMenuCoFJoin" ) { m_szHint[0] = 0; }

	void Show() override
	{
		CMenuFramework::Show();
		char name[64];
		UI_CoFStoryCleanName( name, sizeof( name ), UI_CoFCoopDefaultName( ));
		playerName.SetBuffer( name );
		address.SetBuffer( EngFuncs::GetCvarString( COF_COOP_CVAR_ADDRESS ));
		password.SetBuffer( "" );
		SetHint( NULL );
	}

	// the Connect button; also the cfg-only test hook menu_cof_join_connect
	bool Connect( void );
	void SetAddress( const char *addr ) { address.SetBuffer( addr ); }

private:
	void _Init() override;
	void _VidInit() override;

	void ConnectCb( void ) { Connect(); }
	void LanCb( void );
	void WriteSettings( void );
	void SetHint( const char *error );

	CMenuField  address;
	CMenuField  password;
	CMenuField  playerName;
	CMenuAction hint;

	char m_szHint[400];
};

void CMenuCoFJoin::_Init( void )
{
	const bool theme = UI_ThemeActive();

	banner.szName = L( "Join co-op" );

	if( theme )
		SetPanel( "JOIN CO-OP", 760, 400 );

	address.szName = L( "Join code or address" );
	address.iMaxLength = 127;
	address.szStatusText = L( "The join code your host sent you (COF1-...), or an address with an optional :port" );

	password.szName = L( "Password" );
	password.iMaxLength = COF_STORY_PASSWORD_MAX;
	password.bHideInput = true;
	password.szStatusText = L( "Only if the host set one" );

	playerName.szName = L( "Your name" );
	playerName.iMaxLength = COF_STORY_NAME_MAX;
	playerName.szStatusText = L( "Your items come back from a checkpoint only under the same name" );

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = m_szHint;

	if( !theme )
		AddItem( banner );

	AddItem( address );
	AddItem( password );
	AddItem( playerName );
	AddItem( hint );

	// secondary first: LAN servers, Cancel, then Connect
	AddButton( L( "LAN servers" ), L( "Servers on the local network" ), PC_LAN_GAME,
		VoidCb( &CMenuCoFJoin::LanCb ), QMF_NOTIFY );
	AddButton( L( "GameUI_Cancel" ), L( "Go back to the Main menu" ), PC_CANCEL,
		VoidCb( &CMenuCoFJoin::Hide ), QMF_NOTIFY );
	AddButton( L( "Connect" ), L( "Join this game" ), PC_JOIN_GAME,
		VoidCb( &CMenuCoFJoin::ConnectCb ), QMF_NOTIFY );
}

void CMenuCoFJoin::_VidInit( void )
{
	if( IsPanel( ))
	{
		Rect c = ContentRect();
		const int colW = ( c.sz.w - THEME_GUTTER2 ) / 2;
		int y = c.pt.y + THEME_LABEL_TALL + 8;

		address.SetRect( c.pt.x, y, c.sz.w, THEME_CTRL_H );
		y += THEME_CTRL_PITCH;
		password.SetRect( c.pt.x, y, colW, THEME_CTRL_H );
		playerName.SetRect( c.pt.x + colW + THEME_GUTTER2, y, colW, THEME_CTRL_H );
		y += THEME_CTRL_H + 22;
		hint.SetRect( c.pt.x, y, c.sz.w, THEME_LABEL_TALL * 5 + 8 );
		return;
	}

	address.SetRect( 360, 280, 400, 32 );
	password.SetRect( 360, 350, 190, 32 );
	playerName.SetRect( 570, 350, 190, 32 );
	hint.SetRect( 360, 410, 400, 100 );
}

void CMenuCoFJoin::SetHint( const char *error )
{
	if( error && error[0] )
		snprintf( m_szHint, sizeof( m_szHint ), "%s", error );
	else
		Q_strncpy( m_szHint, L( "Paste the join code your host sent you (Ctrl+V), or their address. The host's "
			"Cry of Fear Co-op app shows the code; the default port is 27015." ), sizeof( m_szHint ));
}

// the story settings, the name and the password, as the app joins (coop_play.join_launch)
void CMenuCoFJoin::WriteSettings( void )
{
	char name[64], pass[80];
	cofStoryText_t t;
	static char text[COF_STORY_TEXT_MAX];

	UI_CoFStoryCleanName( name, sizeof( name ), playerName.GetBuffer( ));
	CoFStory_Clean( pass, sizeof( pass ), password.GetBuffer(), COF_STORY_PASSWORD_MAX, false );

	CoFStory_TextInit( &t, text, sizeof( text ));
	CoFStory_AddSettings( &t );
	CoFStory_TextAdd( &t, "name \"%s\"\npassword \"%s\"\n", name, pass );
	if( !t.overflow )
		EngFuncs::ClientCmd( false, text );
}

bool CMenuCoFJoin::Connect( void )
{
	char host[256], name[64], pass[80], refused[160];
	int port = 0;
	const char *error = "";
	static char text[COF_STORY_TEXT_MAX];

	if( !CoFStory_ParseEndpoint( address.GetBuffer(), host, sizeof( host ), &port, &error ))
	{
		Con_Printf( "Cry of Fear co-op: %s\n", error );
		SetHint( error );
		return false;
	}
	// what the app refuses is refused, not changed: a password changed here would not be the host's
	if( !CoFStory_FieldValid( playerName.GetBuffer(), COF_STORY_NAME_MAX, true, true ) ||
		!CoFStory_FieldValid( password.GetBuffer(), COF_STORY_PASSWORD_MAX, false, true ))
	{
		const bool badName = !CoFStory_FieldValid( playerName.GetBuffer(), COF_STORY_NAME_MAX, true, true );
		CoFStory_FieldMessage( refused, sizeof( refused ), badName ? "Your name" : "The password",
			badName ? COF_STORY_NAME_MAX : COF_STORY_PASSWORD_MAX, false );
		Con_Printf( "Cry of Fear co-op: %s\n", refused );
		SetHint( refused );
		return false;
	}

	UI_CoFStoryCleanName( name, sizeof( name ), playerName.GetBuffer( ));
	CoFStory_Clean( pass, sizeof( pass ), password.GetBuffer(), COF_STORY_PASSWORD_MAX, false );
	if( !CoFStory_JoinCommands( text, sizeof( text ), name, pass, host, port ))
	{
		SetHint( "This join was refused." );
		return false;
	}

	// remember what was typed (a join code stays a join code), never the password
	char typed[128];
	UI_CoFCoopQuoteSafe( typed, sizeof( typed ), address.GetBuffer( ));
	EngFuncs::CvarSetString( COF_COOP_CVAR_ADDRESS, typed );

	Con_Printf( "Cry of Fear co-op: joining %s:%d as \"%s\"\n", host, port, name );

	// one write, name first so the server sees it in the connect's userinfo
	EngFuncs::ClientCmd( false, text );
	UI_CloseMenu();
	return true;
}

void CMenuCoFJoin::LanCb( void )
{
	// the stock server browser, LAN tab only; it connects on its own, with the story settings, name and password
	WriteSettings();
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
