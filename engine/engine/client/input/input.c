/*
input.c - win32 input devices
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#if XASH_SDL == 2
#include <SDL.h>
#elif XASH_SDL == 3
#include <SDL3/SDL.h>
#endif

#include "common.h"
#include "input.h"
#include "client.h"
#include "vgui_draw.h"
#include "cursor_type.h"
#include "platform/platform.h"

static CVAR_DEFINE_AUTO( cof_pad_move_debug, "0", 0, "Cry of Fear movement diagnostics: 1 logs input and command snapshots four times per second; 0 disables" );

static qboolean	in_mouseactive;				// false when not focus app
static qboolean	in_mouseinitialized;
static struct
{
	int x, y;
} in_lastvalidpos;
static qboolean	in_mouse_savedpos;
static int in_mstate = 0;
static struct inputstate_s
{
	float lastpitch, lastyaw;
} inputstate;

CVAR_DEFINE_AUTO( m_pitch, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse pitch value" );
CVAR_DEFINE_AUTO( m_yaw, "0.022", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "mouse yaw value" );
CVAR_DEFINE_AUTO( m_ignore, DEFAULT_M_IGNORE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "ignore mouse events" );
static CVAR_DEFINE_AUTO( look_filter, "0", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "filter look events making it smoother" );
static CVAR_DEFINE_AUTO( m_rawinput, "1", FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable mouse raw input" );

static CVAR_DEFINE_AUTO( cl_forwardspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default forward move speed" );
static CVAR_DEFINE_AUTO( cl_backspeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default back move speed"  );
static CVAR_DEFINE_AUTO( cl_sidespeed, "400", FCVAR_ARCHIVE | FCVAR_CLIENTDLL | FCVAR_FILTERABLE, "Default side move speed"  );

static CVAR_DEFINE_AUTO( m_grab_debug, "0", FCVAR_PRIVILEGED, "show debug messages on mouse state change" );
CVAR_DEFINE_AUTO( touch_enable, DEFAULT_TOUCH_ENABLE, FCVAR_ARCHIVE | FCVAR_FILTERABLE, "enable touch controls" );

/*
================
IN_CollectInputDevices

Returns a bit mask representing connected devices or, at least, enabled
================
*/
uint IN_CollectInputDevices( void )
{
	uint ret = 0;

	if( !m_ignore.value ) // no way to check is mouse connected, so use cvar only
		ret |= INPUT_DEVICE_MOUSE;

	if( touch_enable.value )
		ret |= INPUT_DEVICE_TOUCH;

	if( Joy_IsActive() ) // connected or enabled
		ret |= INPUT_DEVICE_JOYSTICK;

	Con_Reportf( "Connected devices: %s%s%s%s\n",
		FBitSet( ret, INPUT_DEVICE_MOUSE )    ? "mouse " : "",
		FBitSet( ret, INPUT_DEVICE_TOUCH )    ? "touch " : "",
		FBitSet( ret, INPUT_DEVICE_JOYSTICK ) ? "joy " : "",
		FBitSet( ret, INPUT_DEVICE_VR )       ? "vr " : "");

	return ret;
}

/*
=================
IN_LockInputDevices

tries to lock any possibilty to connect another input device after
player is connected to the server
=================
*/
void IN_LockInputDevices( qboolean lock )
{
	extern convar_t joy_enable; // private to input system

	if( lock )
	{
		SetBits( m_ignore.flags, FCVAR_READ_ONLY );
		SetBits( joy_enable.flags, FCVAR_READ_ONLY );
		SetBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
	else
	{
		ClearBits( m_ignore.flags, FCVAR_READ_ONLY );
		ClearBits( joy_enable.flags, FCVAR_READ_ONLY );
		ClearBits( touch_enable.flags, FCVAR_READ_ONLY );
	}
}


/*
===========
IN_StartupMouse
===========
*/
static void IN_StartupMouse( void )
{
	Cvar_RegisterVariable( &m_ignore );

	Cvar_RegisterVariable( &m_pitch );
	Cvar_RegisterVariable( &m_yaw );
	Cvar_RegisterVariable( &look_filter );
	Cvar_RegisterVariable( &m_rawinput );
	Cvar_RegisterVariable( &m_grab_debug );
	Cvar_RegisterVariable( &touch_enable );

	// You can use -nomouse argument to prevent using mouse from client
	// -noenginemouse will disable all mouse input
	if( Sys_CheckParm(  "-noenginemouse" )) return;

	in_mouseinitialized = true;
}

/*
===========
IN_MouseSavePos

Save mouse pos before state change e.g. changelevel
===========
*/
void IN_MouseSavePos( void )
{
	if( !in_mouseactive )
		return;

	Platform_GetMousePos( &in_lastvalidpos.x, &in_lastvalidpos.y );

	in_mouse_savedpos = true;
}

/*
===========
IN_MouseRestorePos

Restore right position for background
===========
*/
void IN_MouseRestorePos( void )
{
	if( !in_mouse_savedpos )
		return;

	Platform_SetMousePos( in_lastvalidpos.x, in_lastvalidpos.y );

	in_mouse_savedpos = false;
}

/*
===========
IN_ToggleClientMouse

Called when key_dest is changed
===========
*/
void IN_ToggleClientMouse( int newstate, int oldstate )
{
	if( newstate == oldstate )
		return;

	// since SetCursorType controls cursor visibility
	// execute it first, and then check mouse grab state
	if( newstate == key_menu || newstate == key_console )
	{
		Platform_SetCursorType( dc_arrow );

#if XASH_USE_EVDEV
		Evdev_SetGrab( false );
#endif
	}
	else
	{
		Platform_SetCursorType( dc_none );

#if XASH_USE_EVDEV
		Evdev_SetGrab( true );
#endif
	}

	// don't leave the user without cursor if they enabled m_ignore
	if( m_ignore.value )
		return;

	if( oldstate == key_game )
	{
		IN_DeactivateMouse();
	}
	else if( newstate == key_game )
	{
		IN_ActivateMouse();
	}
}

void IN_SetRelativeMouseMode( qboolean set )
{
	static qboolean s_bRawInput;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_TRUE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, true );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2
		s_bRawInput = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bRawInput )
	{
#if XASH_SDL >= 2
		SDL_GetRelativeMouseState( NULL, NULL );
#if XASH_SDL == 2
		SDL_SetRelativeMouseMode( SDL_FALSE );
#else // XASH_SDL != 2
		SDL_SetWindowRelativeMouseMode( host.hWnd, false );
#endif // XASH_SDL != 2
#endif // XASH_SDL >= 2

		s_bRawInput = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

void IN_SetMouseGrab( qboolean set )
{
	static qboolean s_bMouseGrab;
	qboolean verbose = m_grab_debug.value ? true : false;

	if( set && !s_bMouseGrab )
	{
		Platform_SetMouseGrab( true );

		s_bMouseGrab = true;
		if( verbose )
			Con_Printf( "%s: true\n", __func__ );
	}
	else if( !set && s_bMouseGrab )
	{
		Platform_SetMouseGrab( false );

		s_bMouseGrab = false;
		if( verbose )
			Con_Printf( "%s: false\n", __func__ );
	}
}

static void IN_CheckMouseState( qboolean active )
{
	qboolean use_raw_input;

#if XASH_WIN32
	use_raw_input = ( m_rawinput.value && clgame.client_dll_uses_sdl ) || clgame.dllFuncs.pfnLookEvent != NULL;
#else
	use_raw_input = true; // always use SDL code
#endif

	if( m_ignore.value )
		active = false;

	if( active && use_raw_input && !host.mouse_visible && cls.state == ca_active )
		IN_SetRelativeMouseMode( true );
	else
		IN_SetRelativeMouseMode( false );

	if( active && !host.mouse_visible && cls.state == ca_active )
		IN_SetMouseGrab( true );
	else
		IN_SetMouseGrab( false );
}

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( true );
	if( clgame.dllFuncs.IN_ActivateMouse )
		clgame.dllFuncs.IN_ActivateMouse();
	in_mouseactive = true;
}

/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void )
{
	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( false );
	if( clgame.dllFuncs.IN_DeactivateMouse )
		clgame.dllFuncs.IN_DeactivateMouse();
	in_mouseactive = false;
}



/*
================
IN_MouseMove
================
*/
static void IN_MouseMove( void )
{
	static int oldx, oldy;

	if( !in_mouseinitialized )
		return;

	if( Touch_WantVisibleCursor( ))
	{
		// touch emulation overrides all input
		Touch_KeyEvent( 0, 0 );
		return;
	}

	// find mouse movement
	int x, y;
	Platform_GetMousePos( &x, &y );

	// touchscreen moves the cursor on its own, don't drag it back to where the mouse is left
	if( x == oldx && y == oldy )
		return;

	oldx = x;
	oldy = y;

	VGui_MouseMove( x, y );

	// if the menu is visible, move the menu cursor
	UI_MouseMove( x, y );

	// CoF milestone 3b: the styled console window has a close button; it only
	// remembers the position, nothing reads it unless that window is up
	Con_MouseMove( x, y );
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int key, int down )
{
	if( !in_mouseinitialized )
		return;

	if( down )
		SetBits( in_mstate, BIT( key ));
	else ClearBits( in_mstate, BIT( key ));

	// touch emulation overrides all input
	if( Touch_WantVisibleCursor( ))
	{
		Touch_KeyEvent( K_MOUSE1 + key, down );
	}
	else if( cls.key_dest == key_game )
	{
		// perform button actions
		VGui_MouseEvent( K_MOUSE1 + key, down );

		// don't do Key_Event here
		// client may override IN_MouseEvent
		// but by default it calls back to Key_Event anyway
		if( in_mouseactive )
			clgame.dllFuncs.IN_MouseEvent( in_mstate );
	}
	else
	{
		// perform button actions
		Key_Event( K_MOUSE1 + key, down );
	}
}

/*
===========
IN_ClearMouseState

mouse button releases are handled here to ensure that only actual mouse button clicks are released
===========
*/
void IN_ClearMouseState( void )
{
	for( int i = 0; i <= K_MOUSE5 - K_MOUSE1; i++ )
	{
		if( FBitSet( in_mstate, BIT( i ))) // is this an actual mouse button click?
			IN_MouseEvent( i, false );
	}
}

/*
==============
IN_MWheelEvent

direction is negative for wheel down, otherwise wheel up
==============
*/
void IN_MWheelEvent( int y )
{
	int b = y > 0 ? K_MWHEELUP : K_MWHEELDOWN;

	VGui_MWheelEvent( y );

	Key_Event( b, true );
	Key_Event( b, false );
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void )
{
	IN_DeactivateMouse( );

#if XASH_USE_EVDEV
	Evdev_Shutdown();
#endif

	Touch_Shutdown();
}


/*
===========
IN_Init
===========
*/
void IN_Init( void )
{
	Cvar_RegisterVariable( &cof_pad_move_debug );
	Cvar_RegisterVariable( &cl_forwardspeed );
	Cvar_RegisterVariable( &cl_backspeed );
	Cvar_RegisterVariable( &cl_sidespeed );

	if( !Host_IsDedicated() )
	{
		IN_StartupMouse( );

		IN_GyroInit();

		OSK_Init();

		Joy_Init(); // common joystick support init

		CL_CoF_PadInit(); // Cry of Fear gamepad cvars, before config.cfg runs

		Touch_Init();

#if XASH_USE_EVDEV
		Evdev_Init();
#endif
	}
}

/*
================
IN_JoyMove

Common function for engine joystick movement

	-1 < forwardmove < 1,	-1 < sidemove < 1

================
*/

#define F (1U << 0)	// Forward
#define B (1U << 1)	// Back
#define L (1U << 2)	// Left
#define R (1U << 3)	// Right
#define T (1U << 4)	// Forward stop
#define S (1U << 5)	// Side stop
// Observational only: never alter input, commands, key state, or movement speed.
static void IN_CoFMoveDiagnostic( const usercmd_t *cmd, float preForward, float preSide,
    float forward, float side, uint flags )
{
    static double next;
    if( !cof_pad_move_debug.value || !GI || Q_stricmp( GI->gamefolder, "cryoffear" )) return;
    if( host.realtime < next ) return;
    next = host.realtime + 0.25;
    Con_Printf( "[cof move] mode=%.0f raw=%d,%d shaped=%.3f,%.3f speeds=%.1f,%.1f preEngine=%.1f,%.1f postEngine=%.1f,%.1f buttons=0x%04x pulseFlags=0x%02x gate=%d velocity=%.1f,%.1f,%.1f origin=%.1f,%.1f,%.1f\n",
        Cvar_VariableValue( "cof_pad_move_mode" ), (int)Joy_CoF_AxisRaw( JOY_AXIS_FWD ), (int)Joy_CoF_AxisRaw( JOY_AXIS_SIDE ),
        forward, side, cl_forwardspeed.value, cl_sidespeed.value, preForward, preSide,
        cmd->forwardmove, cmd->sidemove, (unsigned)cmd->buttons, flags, (int)CL_CoF_UIGateActive(),
        cl.simvel[0], cl.simvel[1], cl.simvel[2], cl.simorg[0], cl.simorg[1], cl.simorg[2] );
}

static void IN_JoyAppendMove( usercmd_t *cmd, float forwardmove, float sidemove )
{
	static uint moveflags = T | S;
	const float preForward = cmd->forwardmove, preSide = cmd->sidemove;
	// Cry of Fear: the client turns a second +forward (+back, +moveleft,
	// +moveright) within 0.18 s into a dodge. A stick resting near the 0.7 /
	// 0.9 thresholds used to press and release the key every few frames and
	// dodge on its own; a pressed key is now released only below the
	// threshold minus cof_joy_pulse_hysteresis. A deliberate double flick
	// through the centre still emits pulses. CL_CoF_PadMoveCommand blocks
	// their double-tap dodge unless cof_pad_no_doubletap is disabled.
	const float hyst = CL_CoF_JoyPulseHysteresis( );
	const uint oldflags = moveflags;
	// the deflections that press the keys: 0.7 / 0.9, or the direction's
	// sector with cof_pad_move_mode 1/2 (cof_gamepad.c)
	float fthr = 0.7f, sthr = 0.9f;

	CL_CoF_PadMoveKeyThresholds( &fthr, &sthr );

	if( forwardmove ) cmd->forwardmove  = forwardmove * cl_forwardspeed.value;
	if( sidemove ) cmd->sidemove  = sidemove * cl_sidespeed.value;

	if( forwardmove )
	{
		moveflags &= ~T;
	}
	else if( !( moveflags & T ) )
	{
		CL_CoF_PadMoveCommand( "-back" );
		CL_CoF_PadMoveCommand( "-forward" );
		moveflags |= T;
	}

	if( sidemove )
	{
		moveflags &= ~S;
	}
	else if( !( moveflags & S ) )
	{
		CL_CoF_PadMoveCommand( "-moveleft" );
		CL_CoF_PadMoveCommand( "-moveright" );
		moveflags |= S;
	}

	if ( forwardmove > fthr && !( moveflags & F ))
	{
		moveflags |= F;
		CL_CoF_PadMoveCommand( "+forward" );
	}
	else if ( forwardmove < fthr - hyst && ( moveflags & F ))
	{
		moveflags &= ~F;
		CL_CoF_PadMoveCommand( "-forward" );
	}

	if ( forwardmove < -fthr && !( moveflags & B ))
	{
		moveflags |= B;
		CL_CoF_PadMoveCommand( "+back" );
	}
	else if ( forwardmove > -fthr + hyst && ( moveflags & B ))
	{
		moveflags &= ~B;
		CL_CoF_PadMoveCommand( "-back" );
	}

	if ( sidemove > sthr && !( moveflags & R ))
	{
		moveflags |= R;
		CL_CoF_PadMoveCommand( "+moveright" );
	}
	else if ( sidemove < sthr - hyst && ( moveflags & R ))
	{
		moveflags &= ~R;
		CL_CoF_PadMoveCommand( "-moveright" );
	}

	if ( sidemove < -sthr && !( moveflags & L ))
	{
		moveflags |= L;
		CL_CoF_PadMoveCommand( "+moveleft" );
	}
	else if ( sidemove > -sthr + hyst && ( moveflags & L ))
	{
		moveflags &= ~L;
		CL_CoF_PadMoveCommand( "-moveleft" );
	}

	CL_CoF_PadMoveCorrectCommand( cmd, forwardmove, sidemove );

	IN_CoFMoveDiagnostic( cmd, preForward, preSide, forwardmove, sidemove, moveflags );

	if( moveflags != oldflags )
		CL_CoF_PadTracePulses( oldflags, moveflags, forwardmove, sidemove );
}

static void IN_CollectInput( float *forward, float *side, float *pitch, float *yaw, qboolean includeMouse )
{
	if( includeMouse )
	{
		float x, y;
		Platform_MouseMove( &x, &y );
		*pitch += y * m_pitch.value;
		*yaw   -= x * m_yaw.value;

#if XASH_USE_EVDEV
		IN_EvdevMove( yaw, pitch );
#endif
	}

	IN_GyroFinalizeMove( forward, side, pitch, yaw );
	Joy_FinalizeMove( forward, side, pitch, yaw );
	Touch_GetMove( forward, side, pitch, yaw );

	if( look_filter.value )
	{
		*pitch = ( inputstate.lastpitch + *pitch ) / 2;
		*yaw   = ( inputstate.lastyaw   + *yaw ) / 2;
		inputstate.lastpitch = *pitch;
		inputstate.lastyaw   = *yaw;
	}

}

/*
================
IN_EngineAppendMove

Called from cl_main.c after generating command in client
================
*/
void IN_EngineAppendMove( float frametime, usercmd_t *cmd, qboolean active )
{
	if( clgame.dllFuncs.pfnLookEvent )
		return;

	if( cls.key_dest != key_game || cl.paused || cl.intermission )
		return;

	// Cry of Fear: while a clickable CoF panel (inventory, note, keypad...) is
	// open the client swallows every key, but these moves bypass it; the
	// sticks drive the panel cursor instead (cof_gamepad.c). The zero move
	// releases any +forward/... the stick was still holding.
	if( CL_CoF_PadSuppressMove( ))
	{
		IN_JoyAppendMove( cmd, 0.0f, 0.0f );
		CL_CoF_PadTraceMove( cmd->forwardmove, cmd->sidemove, true );
		return;
	}

	if( active )
	{
		float forward = 0, side = 0, pitch = 0, yaw = 0;
		float sensitivity = 1;//( (float)cl.local.scr_fov / (float)90.0f );

		IN_CollectInput( &forward, &side, &pitch, &yaw, false );

		IN_JoyAppendMove( cmd, forward, side );
		CL_CoF_PadTraceMove( cmd->forwardmove, cmd->sidemove, false );

		// Cry of Fear: the quick turn (cof_quickturn) goes through the same
		// view-angle path as the right stick
		yaw += CL_CoF_QuickTurnYaw( );

		if( pitch || yaw )
		{
			cmd->viewangles[YAW]   += yaw * sensitivity;
			cmd->viewangles[PITCH] += pitch * sensitivity;
			cmd->viewangles[PITCH] = bound( -90, cmd->viewangles[PITCH], 90 );
			VectorCopy( cmd->viewangles, cl.viewangles );
		}
	}
}

static void IN_Commands( void )
{
#if XASH_USE_EVDEV
	IN_EvdevFrame();
#endif

	if( clgame.dllFuncs.pfnLookEvent )
	{
		float forward = 0, side = 0, pitch = 0, yaw = 0;

		IN_CollectInput( &forward, &side, &pitch, &yaw, in_mouseinitialized && !m_ignore.value );

		if( cls.key_dest == key_game )
		{
			clgame.dllFuncs.pfnLookEvent( yaw, pitch );
			clgame.dllFuncs.pfnMoveEvent( forward, side );
		}
	}

	if( !in_mouseinitialized )
		return;

	IN_CheckMouseState( in_mouseactive );
}

/*
==================
Host_InputFrame

Called every frame, even if not generating commands
==================
*/
void Host_InputFrame( void )
{
	IN_Commands();

	// Cry of Fear: the gamepad's panel cursor; while it owns the pointer the
	// real mouse position is not pushed over it (cof_gamepad.c)
	if( !CL_CoF_PadFrame( ))
		IN_MouseMove();
}
