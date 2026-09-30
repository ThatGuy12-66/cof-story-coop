/*
cof_quicksave.c - Cry of Fear: quick save and quick load (cof_quick_saves)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

====================================================================

cof_quick_saves (saved, default 0)

Cry of Fear keeps five shared save slots (the tape recorders, and with
cof_pause_menu_saves the pause menu). This adds one more file of its own, the
quick save, written and read through exactly the same root-SAVE path:

	cof_quicksave   F5   SAVE/cofquick.sav (+ .bmp) and cryoffear/SAVE/
	                     saveinfoquick.cof, "Quick save (<map>) - <date>"
	cof_quickload   F9   loads SAVE/cofquick.sav

The writing itself is SV_CoFQuickSave (server/sv_save.c): the same
transaction as a pause-menu save (label first, the old files kept as
.coffix-backup until the new pair is in place, rollback on any I/O error).
This file only decides whether a quick save may happen now:

	off                 cof_quick_saves 0: "Quick saves are disabled in
	                    Options > Game", nothing else
	multiplayer         co-op host or joiner: never (a load would end the
	                    session, a save is refused by the engine anyway)
	not in play         no local game, the menu background map, a level still
	                    loading (loading plaque up), a server not yet active
	menu or console     key_dest is not key_game (this includes the death page)
	a panel             the client shows its arrow cursor (host.mouse_visible):
	                    inventory, notes, tape recorder page, computer, ...
	dead / no player    IsValidSave (server/sv_save.c) says so

The quick load needs only the option, single player and the file.

What the player is told goes where the game tells its own: the client's
message strip under the middle of the screen (CHUDControl), through the
"ProFont" user message hl.dll uses for the pickup lines and for the tape
recorder's own "Saved" (hl.dll 100DF132: ProFont, colour 4). So these lines
get everything that strip already has: the placement (cof_hud_msg_y_pct), the
Inter SemiBold face and its size (the "credits" role, times the HUD scale),
the backing strip (cof_hud_text_backing, 0 = off), the client's own fade, and
the language pack's string table at the client's setText
(languages/<code>/strings/dll-strings.tsv, English key = the text below).
Nothing goes to the top-left notify lines: the log lines are developer-only
(Con_Reportf). What a player can only reach from the console (menu, panel,
loading, dead) is answered in the console.

The menu side (the Game tab switch, the pause menu's first item, the Load
list's first row and the two Keybinds rows) is
patches/cof-mainui-quicksave.patch.
*/

#include "common.h"
#include "client.h"
#include "server.h"

static CVAR_DEFINE_AUTO( cof_quick_saves, "0", FCVAR_ARCHIVE, "Cry of Fear: allow the quick save (cof_quicksave, F5) and the quick load (cof_quickload, F9); off by default" );

// the message strip's colour index (cof_hud_msg_probe lists them)
#define COF_QUICK_GREEN 4 // hl.dll's own "Saved"
#define COF_QUICK_GREY  5 // the pickup lines

// the "Quick loaded" line waits for the loaded level: up, loading plaque gone,
// and drawn for a few frames, so the client's strip is there to take it
static qboolean cof_quick_load_pending;
static double   cof_quick_load_deadline;
static int      cof_quick_load_settle;

static void CL_CoF_QuickShow( const char *text, int colour )
{
	byte	buf[256];
	int	len;

	Con_Reportf( "[cof-quick] %s\n", text );

	if( cls.state != ca_active )
		return;

	buf[0] = (byte)colour;
	Q_strncpy( (char *)buf + 1, text, sizeof( buf ) - 1 );
	len = 1 + Q_strlen( (char *)buf + 1 ) + 1;

	if( !CL_DispatchUserMessage( "ProFont", len, buf ))
		Con_Printf( "%s\n", text ); // no strip (another client): at least the console
}

static qboolean CL_CoF_QuickEnabled( void )
{
	if( cof_quick_saves.value != 0.0f )
		return true;

	if( cls.state == ca_active )
		CL_CoF_QuickShow( "Quick saves are disabled in Options > Game", COF_QUICK_GREY );
	else
		Con_Printf( "Quick saves are disabled in Options > Game\n" );
	return false;
}

// a co-op session, as host (local server with more than one slot) or as a
// joiner (connected to a server with more than one slot)
static qboolean CL_CoF_QuickMultiplayer( void )
{
	if( SV_Active( ) && SV_GetMaxClients( ) > 1 )
		return true;

	if( cls.state >= ca_connected && cl.maxclients > 1 )
		return true;

	return false;
}

static void CL_CoF_QuickSave_f( void )
{
	if( !CL_CoF_QuickEnabled( ))
		return;

	if( CL_CoF_QuickMultiplayer( ))
	{
		CL_CoF_QuickShow( "Quick saves work in single player only", COF_QUICK_GREY );
		return;
	}

	if( !SV_Active( ) || cls.state != ca_active || cl.background || cls.disable_screen != 0.0f )
	{
		Con_Printf( "Quick save: not in play (no game, the menu map, or still loading).\n" );
		return;
	}

	if( cls.key_dest != key_game )
	{
		Con_Printf( "Quick save: close the menu or the console first.\n" );
		return;
	}

	if( host.mouse_visible )
	{
		Con_Printf( "Quick save: close the open panel first.\n" );
		return;
	}

	if( SV_CoFQuickSave( ))
		CL_CoF_QuickShow( "Quick saved", COF_QUICK_GREEN );
}

static void CL_CoF_QuickLoad_f( void )
{
	if( !CL_CoF_QuickEnabled( ))
		return;

	if( CL_CoF_QuickMultiplayer( ))
	{
		CL_CoF_QuickShow( "Quick saves work in single player only", COF_QUICK_GREY );
		return;
	}

	if( !SV_CoFQuickSaveExists( ))
	{
		if( cls.state == ca_active )
			CL_CoF_QuickShow( "There is no quick save yet", COF_QUICK_GREY );
		else
			Con_Printf( "There is no quick save yet\n" );
		return;
	}

	Con_Reportf( "[cof-quick] loading " DEFAULT_SAVE_DIRECTORY COF_QUICKSAVE_NAME ".sav\n" );
	cof_quick_load_pending = SV_LoadGame( DEFAULT_SAVE_DIRECTORY COF_QUICKSAVE_NAME ".sav" );
	cof_quick_load_deadline = host.realtime + 120.0;
	cof_quick_load_settle = 0;
}

/*
====================
CL_CoF_QuickSaveFrame

Host_ClientFrame, every frame: "Quick loaded" once the level the quick load
started is up (not from the command buffer, which a running cfg can hold up).
====================
*/
void CL_CoF_QuickSaveFrame( void )
{
	if( !cof_quick_load_pending )
		return;

	if( cls.state != ca_active || cls.disable_screen != 0.0f || cl.background )
	{
		cof_quick_load_settle = 0;
		if( host.realtime > cof_quick_load_deadline )
			cof_quick_load_pending = false; // the load never finished: say nothing
		return;
	}

	if( ++cof_quick_load_settle < 20 )
		return;

	cof_quick_load_pending = false;
	CL_CoF_QuickShow( "Quick loaded", COF_QUICK_GREEN );
}

void CL_CoF_QuickSaveInit( void )
{
	Cvar_RegisterVariable( &cof_quick_saves );
	Cmd_AddCommand( "cof_quicksave", CL_CoF_QuickSave_f, "Cry of Fear: quick save (SAVE/cofquick.sav) when cof_quick_saves is on" );
	Cmd_AddCommand( "cof_quickload", CL_CoF_QuickLoad_f, "Cry of Fear: load the quick save when cof_quick_saves is on" );
}
