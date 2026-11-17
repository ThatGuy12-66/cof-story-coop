/*
cof_panel_pause.c - Cry of Fear: stop the single-player world while a safe panel is open

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

====================================================================

cof_panel_pause (saved, default 0)

Cry of Fear never stops the world for its in-play panels: while the inventory,
a note or the tape recorder's SAVED GAMES page is open, monsters keep moving
and attacking. With cof_panel_pause 1, in single player, the server stops
simulating while one of the "tier 1" panels below is on screen, exactly the
way it already stops for the engine menu or the console:

	inventory          CRECBInventory
	documents list     CDocuments
	note               CNoteDocument
	user document      CUserDocument
	billboard          CVGUIBillboard
	YES/NO question    CYesno
	tape recorder page CSaveLoad, and its confirm box CSave

It never stops for the phone (CPhone), the landline (CTelephone), the computer
(CComputer), the padlock (CPadlock) or the boiler puzzle (CPuzzleBar): what
those panels wait for is scripted in the world (a call that answers, a door a
keypad opens) or is the live 3D scene itself, and their clock-based debounces
would never expire while the server clock stands still. Any of them on screen
wins over a tier 1 panel. The keypad, the animal statue and the text window
(tier 2 in the research) and every other modal client panel are left alone.

Which panel is open is not guessed from a cursor or a message: the VGUI
support library identifies every visible panel below the client's viewport by
its class (the MSVC run-time type information client.dll carries) and reports
one bit per class each painted frame, through vguiapi_t::CofPanelReport. The
report goes stale after COF_PANEL_REPORT_FRAMES host frames without a painted
VGUI frame (engine menu, console, loading), so nothing can stay paused on an
old report. It is counted in frames, not seconds: one slow frame (a
screenshot being written, a texture upload) must not let the world run.

The mechanism is the one the engine menu already uses in single player:
SV_IsSimulating() returns false (no entity thinks, no physics, the server clock
stops), string commands from the client keep running (inventoryequip,
closedocument, the tape recorder's unfreezesave all arrive and are handled at
once) and SV_ExecuteClientMessage() zeroes the player's movement. It is NOT the
`pause` command: cl.paused is never set, so the client clock, the sound mixer,
the MP3 stream and the client's own music keep running, and the HUD draws.

The same report is the engine's one answer to "which Cry of Fear panel is
open": CL_CoF_PanelsOnScreen() hands it to the gamepad code (cof_gamepad.c),
which freezes the sticks, routes START/B/A/X and moves its cursor while a
modal panel is up.

Research: stage1/controller-research-20260923/RESULTS.md, Q2.
*/

#include "common.h"
#include "client.h"
#include "server.h"
#include "vgui_api.h"

#define COF_PANEL_REPORT_FRAMES	2	// host frames a support-library report stays valid

// safe to pause: nothing behind them needs the world to think while they are up
#define COF_PANEL_TIER1		( COF_PANEL_INVENTORY|COF_PANEL_DOCUMENTS|COF_PANEL_NOTE|COF_PANEL_USERDOC \
				| COF_PANEL_BILLBOARD|COF_PANEL_YESNO|COF_PANEL_SAVELOAD|COF_PANEL_SAVECONFIRM )

// never pause while any of these is up, even together with a tier 1 panel
#define COF_PANEL_LIVE		( COF_PANEL_PHONE|COF_PANEL_TELEPHONE|COF_PANEL_COMPUTER|COF_PANEL_PADLOCK \
				| COF_PANEL_PUZZLEBAR|COF_PANEL_KEYPAD|COF_PANEL_STATUE|COF_PANEL_TEXTWINDOW|COF_PANEL_OTHER )

static CVAR_DEFINE_AUTO( cof_panel_pause, "0", FCVAR_ARCHIVE, "Cry of Fear: in single player, stop the world while the inventory, a note, the documents list, a billboard, a YES/NO question or the tape recorder page is open (never for the phone, landline, computer, padlock or boiler puzzle)" );
static CVAR_DEFINE_AUTO( cof_panel_trace, "0", 0, "developer: log every change of the visible Cry of Fear panels and of the panel pause, with the server clock (2 = also name each VGUI panel class the first time it is on screen)" );

static const struct
{
	unsigned int	bit;
	const char	*name;
} cof_panel_names[] =
{
	{ COF_PANEL_INVENTORY,   "inventory" },
	{ COF_PANEL_DOCUMENTS,   "documents" },
	{ COF_PANEL_NOTE,        "note" },
	{ COF_PANEL_USERDOC,     "userdoc" },
	{ COF_PANEL_BILLBOARD,   "billboard" },
	{ COF_PANEL_YESNO,       "yesno" },
	{ COF_PANEL_SAVELOAD,    "tapepage" },
	{ COF_PANEL_SAVECONFIRM, "saveconfirm" },
	{ COF_PANEL_PHONE,       "phone" },
	{ COF_PANEL_TELEPHONE,   "landline" },
	{ COF_PANEL_COMPUTER,    "computer" },
	{ COF_PANEL_PADLOCK,     "padlock" },
	{ COF_PANEL_PUZZLEBAR,   "puzzlebar" },
	{ COF_PANEL_KEYPAD,      "keypad" },
	{ COF_PANEL_STATUE,      "statue" },
	{ COF_PANEL_TEXTWINDOW,  "textwindow" },
	{ COF_PANEL_OTHER,       "othermodal" },
	{ COF_PANEL_HUD,         "hud" },
};

static struct
{
	unsigned int	visible;	// last report from the support library
	uint		frame;		// host.framecount of that report
	unsigned int	logged;		// last set of panels the trace printed
	qboolean	paused;		// last answer of CL_CoF_PanelPaused
	qboolean	reported;	// the support library has reported at least once
} cof_panels;

static const char *CL_CoF_PanelNames( unsigned int bits, char *buf, size_t size )
{
	size_t	i;

	buf[0] = '\0';

	for( i = 0; i < ARRAYSIZE( cof_panel_names ); i++ )
	{
		if( !FBitSet( bits, cof_panel_names[i].bit ))
			continue;

		if( buf[0] )
			Q_strncat( buf, ",", size );
		Q_strncat( buf, cof_panel_names[i].name, size );
	}

	if( !buf[0] )
		Q_strncpy( buf, "none", size );

	return buf;
}

/*
====================
CL_CoF_PanelsVisible

The panels on screen according to the latest report, or none when it is
stale (no VGUI frame painted lately: menu, console, loading).
====================
*/
static unsigned int CL_CoF_PanelsVisible( void )
{
	if( !cof_panels.reported || host.framecount - cof_panels.frame > COF_PANEL_REPORT_FRAMES )
		return 0;

	return cof_panels.visible;
}

/*
====================
CL_CoF_PanelsOnScreen

The modal Cry of Fear panels on screen, by class (the HUD container left
out). *reporting says whether the support library has sent a fresh report:
false with a vgui.dll that has no panel report, or while no VGUI frame is
painted (engine menu, console, loading) - the caller then has no class
answer and may fall back on its own signal.
====================
*/
unsigned int CL_CoF_PanelsOnScreen( qboolean *reporting )
{
	const qboolean fresh = cof_panels.reported && host.framecount - cof_panels.frame <= COF_PANEL_REPORT_FRAMES;

	if( reporting )
		*reporting = fresh;

	return fresh ? ( cof_panels.visible & ~COF_PANEL_HUD ) : 0;
}

/*
====================
CL_CoF_PanelReport

vguiapi_t::CofPanelReport: the support library's view of the client's panels,
one COF_PANEL_* bit per visible class, once per painted frame. Returns the
trace level, so the library can name the classes it sees at level 2.
====================
*/
int CL_CoF_PanelReport( unsigned int visible )
{
	cof_panels.visible = visible;
	cof_panels.frame = host.framecount;
	cof_panels.reported = true;

	// the HUD container is always there; only the modal panels are news
	visible &= ~COF_PANEL_HUD;

	if( visible != cof_panels.logged )
	{
		if( cof_panel_trace.value != 0.0f )
		{
			char	was[256], now[256];

			Con_Printf( "[cof-panel] panels %s -> %s (sv.time %.3f worldframes %i simulating %i)\n",
				CL_CoF_PanelNames( cof_panels.logged, was, sizeof( was )),
				CL_CoF_PanelNames( visible, now, sizeof( now )),
				sv.time, sv.framecount, sv.simulating ? 1 : 0 );
		}

		cof_panels.logged = visible;
	}

	return (int)cof_panel_trace.value;
}

/*
====================
CL_CoF_PanelPaused

True while cof_panel_pause should keep the single-player world stopped. Read
by SV_IsSimulating() and SV_ExecuteClientMessage().
====================
*/
qboolean CL_CoF_PanelPaused( void )
{
	unsigned int	visible;
	qboolean	paused = false;

	if( cof_panel_pause.value != 0.0f
		&& cls.state == ca_active && cls.key_dest == key_game
		&& cl.maxclients <= 1 && !cl.background
		&& SV_Active( ) && svs.maxclients <= 1 )
	{
		visible = CL_CoF_PanelsVisible( );
		paused = FBitSet( visible, COF_PANEL_TIER1 ) && !FBitSet( visible, COF_PANEL_LIVE );
	}

	if( paused != cof_panels.paused )
	{
		if( cof_panel_trace.value != 0.0f )
		{
			char	now[256];

			Con_Printf( "[cof-panel] pause %s (%s; sv.time %.3f worldframes %i)\n",
				paused ? "ON" : "OFF",
				CL_CoF_PanelNames( CL_CoF_PanelsVisible( ) & ~COF_PANEL_HUD, now, sizeof( now )),
				sv.time, sv.framecount );
		}

		cof_panels.paused = paused;
	}

	return paused;
}

/*
====================
CL_CoF_PanelStatus_f

cfg-only test hook: one line with everything the panel pause depends on.
====================
*/
static void CL_CoF_PanelStatus_f( void )
{
	char	now[256];

	Con_Printf( "[cof-panel] %s panels %s pause %s (cvar %g) sv.time %.3f worldframes %i simulating %i ingame %i maxclients %i report %i frames old\n",
		Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "status",
		CL_CoF_PanelNames( CL_CoF_PanelsVisible( ) & ~COF_PANEL_HUD, now, sizeof( now )),
		CL_CoF_PanelPaused( ) ? "ON" : "off", cof_panel_pause.value,
		sv.time, sv.framecount, sv.simulating ? 1 : 0, CL_IsInGame( ) ? 1 : 0,
		cl.maxclients, cof_panels.reported ? (int)( host.framecount - cof_panels.frame ) : -1 );
}

void CL_CoF_PanelPauseInit( void )
{
	Cvar_RegisterVariable( &cof_panel_pause );
	Cvar_RegisterVariable( &cof_panel_trace );
	Cmd_AddCommand( "cof_panel_status", CL_CoF_PanelStatus_f, "developer: print the Cry of Fear panels on screen, the panel pause and the server clock" );
}
