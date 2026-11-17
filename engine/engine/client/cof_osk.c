/*
cof_osk.c - Cry of Fear on-screen keyboard, the engine half (cof-osk-engine.patch)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

The keyboard itself is the menu's (3rdparty/mainui/menus/CoFOsk.cpp,
cof-mainui-osk.patch). This file lets it type into the game's own text
fields, the VGUI TextEntry objects of client.dll (the computer's user name and
password):

  1. FreeVGUI tells the engine when a TextEntry gains or loses key focus or is
     clicked (platform/xash3d-fwgs/cofosk.cpp -> vguiapi_t::CofOskEntry ->
     CL_CoF_OskEntry).
  2. If the last key the engine saw came from a gamepad (the pad cursor's A
     click, CL_CoF_OskNoteKey from Key_Event) and the game has the focus,
     the keyboard is due. Two painted frames later - the game clears its
     "Enter a username..." placeholder on the click - the entry's text and
     whether it hides it (a password) go into cof_osk_field /
     cof_osk_hidden and the menu is asked: "menu_cof_osk game".
  3. While the keyboard is up the menu sets cof_osk_over_game 1, and
     VGui_Paint keeps painting the game's panels under it, so the computer
     stays on screen.
  4. Done / Close: the menu leaves the text (hex) in cof_osk_result and sets
     cof_osk_submit (1 Close, 2 Done). The next painted frame puts the text
     into the focused TextEntry (vguiapi_t::CofOskCall op 0, TextEntry::setText,
     the member the game itself uses) and, for Done, runs cof_enter_pulse if
     the engine has it (cof_enter_hook.c, cof-client-enter-hook.patch): the
     game's own Enter check sees one press.

Steps 2 and 4 run from the paint pass (CL_CoF_OskFrame, VGui_Paint) and put
their commands at the head of the command buffer, so they happen on the next
frame whatever else is queued.

A keyboard-and-mouse player never sees it: a real key or mouse button is the
last input, so step 2 does nothing.

cof_osk_set <hex>|-   the text into the focused game text field (by hand)
cof_osk_probe <n> [keyboard]   test hook (below)
cof_osk_status        what the engine knows; the focused entry's text
=============================================================================
*/

#include "common.h"
#include "client.h"
#include "keydefs.h"

static CVAR_DEFINE_AUTO( cof_osk, "1", FCVAR_ARCHIVE, "Cry of Fear: open the on-screen keyboard when a gamepad clicks into a text field of the game (the computer login)" );
static CVAR_DEFINE_AUTO( cof_osk_over_game, "0", 0, "Cry of Fear: set by the menu while its on-screen keyboard is open over the game; the game's panels stay painted under it" );
static CVAR_DEFINE_AUTO( cof_osk_field, "", 0, "Cry of Fear: the text of the game text field the on-screen keyboard was opened for" );
static CVAR_DEFINE_AUTO( cof_osk_field_index, "0", 0, "Cry of Fear: active game text field, one-based; no text content" );
static CVAR_DEFINE_AUTO( cof_osk_field_count, "0", 0, "Cry of Fear: visible enabled text fields in the active form" );
static CVAR_DEFINE_AUTO( cof_osk_hidden, "0", 0, "Cry of Fear: 1 when that field hides its text (a password)" );
static CVAR_DEFINE_AUTO( cof_osk_result, "", 0, "Cry of Fear: set by the menu's on-screen keyboard: the typed text, hex bytes" );
static CVAR_DEFINE_AUTO( cof_osk_submit, "0", 0, "Cry of Fear: set by the menu's on-screen keyboard: 1 Close, 2 Done (next field or close; never submits the form)" );
static CVAR_DEFINE_AUTO( cof_osk_next_field, "1", FCVAR_ARCHIVE, "Cry of Fear: Done advances to the next game text field; the last field only closes" );
static CVAR_DEFINE_AUTO( cof_osk_trace, "0", 0, "developer: log the game text field reports and the keyboard requests" );

#define COF_OSK_OPEN_DELAY 2	// painted frames between the click and the keyboard

static struct
{
	qboolean pad_last;	// the last key down came from a gamepad
	qboolean entry;		// a game text entry has key focus
	qboolean hidden;
	int pending;		// painted frames until the keyboard opens, 0 none
	int reports;
} osk_state = { false, false, false, 0, 0 };

static qboolean CL_CoF_OskIsPadKey( int key )
{
	return key >= K_JOY1 && key <= K_AUX32;
}

/*
====================
CL_CoF_OskNoteKey

Key_Event, every key: which kind of device pressed last. Mouse wheel steps
and releases do not count.
====================
*/
void CL_CoF_OskNoteKey( int key, int down )
{
	if( !down || key == K_MWHEELUP || key == K_MWHEELDOWN )
		return;

	osk_state.pad_last = CL_CoF_OskIsPadKey( key );
}

static qboolean CL_CoF_OskGameHasFocus( void )
{
	return cls.state == ca_active && cls.key_dest == key_game && !cl.background;
}

/*
====================
CL_CoF_OskEntry

vguiapi_t::CofOskEntry. event: 0 lost focus, 1 got focus, 2 clicked. Called
from inside the support library's input or paint pass; the keyboard itself is
only asked for from CL_CoF_OskFrame.
====================
*/
void CL_CoF_OskEntry( int event, int hidden, const char *text )
{
	osk_state.reports++;

	if( cof_osk_trace.value )
		Con_Printf( "[cof-osk] text entry %s%s (%d chars), last key from the %s\n",
			event == 0 ? "lost the focus" : event == 1 ? "got the focus" : "clicked",
			hidden ? ", hidden" : "", text ? (int)Q_strlen( text ) : 0, osk_state.pad_last ? "gamepad" : "keyboard/mouse" );

	if( event == 0 )
	{
		osk_state.entry = false;
		return;
	}

	osk_state.entry = true;
	osk_state.hidden = hidden ? true : false;

	// the click and the focus change of one A press arrive a frame apart:
	// one request
	if( cof_osk.value && osk_state.pad_last && CL_CoF_OskGameHasFocus( ) && !osk_state.pending )
	{
		osk_state.pending = COF_OSK_OPEN_DELAY;
		Con_Reportf( "[cof-osk] game text field %s by the gamepad: on-screen keyboard\n", event == 1 ? "focused" : "clicked" );
	}
}

static int CL_CoF_OskHexValue( int c )
{
	if( c >= '0' && c <= '9' ) return c - '0';
	if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

// hex bytes (or "-", empty) into the focused game text entry
static qboolean CL_CoF_OskPut( const char *hex )
{
	char text[256];
	int n = 0;

	if( Q_strcmp( hex, "-" ))
	{
		for( ; hex[0] && hex[1] && n < (int)sizeof( text ) - 1; hex += 2 )
		{
			int hi = CL_CoF_OskHexValue( hex[0] ), lo = CL_CoF_OskHexValue( hex[1] );

			if( hi < 0 || lo < 0 )
				break;
			text[n++] = (char)( hi * 16 + lo );
		}
	}
	text[n] = 0;

	if( !VGui_CoF_OskCall( 0, text, sizeof( text )))
	{
		Con_Printf( S_WARN "[cof-osk] no game text field has the focus; the text was dropped\n" );
		return false;
	}

	Con_Reportf( "[cof-osk] %d chars put into the game text field\n", n );
	return true;
}

/*
====================
CL_CoF_OskFrame

VGui_Paint, every painted frame of a running game. Opens the keyboard that is
due (step 2) and takes the keyboard's result (step 4). Returns true while the
game's panels must stay painted under the menu's keyboard.
====================
*/
qboolean CL_CoF_OskFrame( void )
{
	if( cof_osk_submit.value )
	{
		const int submit = (int)cof_osk_submit.value;
		CL_CoF_EnterOskDismiss();

		if( CL_CoF_OskPut( cof_osk_result.string ) && submit == 2 )
		{
			// The menu has closed before handing its result back. Advance only
			// within the same live form; never submit after losing game focus.
			const int next = !CL_CoF_OskGameHasFocus( ) ? -1 :
				cof_osk_next_field.value ? VGui_CoF_OskCall( 3, NULL, 0 ) : 0;

			if( next > 0 )
			{
				// Keep the pad's request even when Done was clicked with a mouse.
				// FreeVGUI commits its deferred focus before we reopen.
				osk_state.pad_last = true;
				osk_state.pending = COF_OSK_OPEN_DELAY;
				Con_Reportf( "[cof-osk] Done: next field\n" );
			}
			else if( next == 0 )
				Con_Reportf( "[cof-osk] Done: text kept; use the game's OK button to submit\n" );
			else Con_Reportf( "[cof-osk] Done: form no longer available; not submitted\n" );
		}
		Cvar_DirectSet( &cof_osk_result, "" );
		Cvar_DirectSet( &cof_osk_submit, "0" );
	}

	if( osk_state.pending > 0 && --osk_state.pending == 0 )
	{
		char text[256];
		int have;

		text[0] = 0;
		have = VGui_CoF_OskCall( 2, text, sizeof( text ));

		// still a pad player: a real mouse click reaches the support library
		// (VGui_MouseEvent) before its Key_Event (via the client), so right
		// after pad use a mouse click may have asked for the keyboard; its
		// Key_Event has arrived by now and says it was the mouse
		if( have && osk_state.pad_last && CL_CoF_OskGameHasFocus( ))
		{
			// Metadata contains only field position/count, never a label inferred
			// from user-entered text. Op 4 packs count/index in two bytes.
			const int fields = VGui_CoF_OskCall( 4, NULL, 0 );
			Cvar_DirectSetValue( &cof_osk_field_index, fields & 255 );
			Cvar_DirectSetValue( &cof_osk_field_count, ( fields >> 8 ) & 255 );
			osk_state.hidden = have == 2;
			Cvar_DirectSet( &cof_osk_field, text );
			Cvar_DirectSet( &cof_osk_hidden, have == 2 ? "1" : "0" );
			Cbuf_InsertText( "menu_cof_osk game\n" );
		}
	}

	if( cls.key_dest == key_game )
	{
		// the menu closed (by the keyboard or otherwise); never let the flag
		// outlive it into the next pause menu
		if( cof_osk_over_game.value )
			Cvar_DirectSet( &cof_osk_over_game, "0" );
		return false;
	}

	return CL_CoF_OskOverGame( );
}

/*
====================
CL_CoF_OskOverGame

The menu's keyboard is open over the running game: VGui_Paint and the
support library's "in game" test (VGUI_IsInGame) keep the game's panels on
screen under it.
====================
*/
qboolean CL_CoF_OskOverGame( void )
{
	return cof_osk_over_game.value && cls.key_dest == key_menu && cls.state == ca_active;
}

/*
====================
CL_CoF_OskSet_f

cof_osk_set <hex>|-   this text into the focused text entry of the game ("-" =
empty), by hand. Hex, so quotes, semicolons and backslashes survive the
command buffer.
====================
*/
static void CL_CoF_OskSet_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Con_Printf( S_USAGE "cof_osk_set <hex bytes>|-\n" );
		return;
	}

	CL_CoF_OskPut( Cmd_Argv( 1 ));
}

/*
====================
CL_CoF_OskProbe_f

cof_osk_probe <n> [keyboard]    test hook: click the n-th visible text entry
on screen (0 = the first, in panel order) the way the mouse does, with the
gamepad as the last input, so the keyboard opens exactly as after a pad
click; with "keyboard" the last input is left as it is (after a keyboard
key: nothing opens). No OS input of any kind.
====================
*/
static void CL_CoF_OskProbe_f( void )
{
	char arg[16];
	const qboolean keyboard = Cmd_Argc() > 2 && !Q_stricmp( Cmd_Argv( 2 ), "keyboard" );

	Q_strncpy( arg, Cmd_Argc() > 1 ? Cmd_Argv( 1 ) : "0", sizeof( arg ));
	if( !keyboard )
		osk_state.pad_last = true;

	if( VGui_CoF_OskCall( 1, arg, sizeof( arg )))
		Con_Printf( "cof_osk_probe: text entry %s clicked, the last input from the %s\n", arg,
			osk_state.pad_last ? "gamepad" : "keyboard and mouse" );
	else
		Con_Printf( "cof_osk_probe: no visible text entry %s\n", arg );
}

static void CL_CoF_OskStatus_f( void )
{
	char text[256];
	int have;

	text[0] = 0;
	have = VGui_CoF_OskCall( 2, text, sizeof( text ));

	Con_Printf( "[cof-osk] cof_osk %d, last key from the %s, entry focused %d, keyboard due %d, over the game %d, reports %d\n",
		(int)cof_osk.value, osk_state.pad_last ? "gamepad" : "keyboard/mouse", osk_state.entry,
		osk_state.pending, cof_osk_over_game.value && cls.key_dest == key_menu, osk_state.reports );

	if( have == 2 )
		Con_Printf( "[cof-osk] focused entry: %d chars (hidden)\n", (int)Q_strlen( text ));
	else if( have )
		Con_Printf( "[cof-osk] focused entry: \"%s\"\n", text );
	else
		Con_Printf( "[cof-osk] focused entry: none\n" );
}

void CL_CoF_OskInit( void )
{
	Cvar_RegisterVariable( &cof_osk );
	Cvar_RegisterVariable( &cof_osk_over_game );
	Cvar_RegisterVariable( &cof_osk_field );
	Cvar_RegisterVariable( &cof_osk_hidden );
	Cvar_RegisterVariable( &cof_osk_field_index );
	Cvar_RegisterVariable( &cof_osk_field_count );
	Cvar_RegisterVariable( &cof_osk_result );
	Cvar_RegisterVariable( &cof_osk_submit );
	Cvar_RegisterVariable( &cof_osk_trace );
	Cvar_RegisterVariable( &cof_osk_next_field );
	Cmd_AddCommand( "cof_osk_set", CL_CoF_OskSet_f, "on-screen keyboard: text (hex bytes) into the focused text field of the game" );
	Cmd_AddCommand( "cof_osk_probe", CL_CoF_OskProbe_f, "test hook: click the n-th visible game text field as the mouse would, the gamepad the last input" );
	Cmd_AddCommand( "cof_osk_status", CL_CoF_OskStatus_f, "on-screen keyboard: the engine's state and the focused game text field" );
}
