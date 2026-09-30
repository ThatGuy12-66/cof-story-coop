/*
input.h - win32 input devices
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

#ifndef INPUT_H
#define INPUT_H

/*
==============================================================

INPUT

==============================================================
*/

#include "keydefs.h"

//
// input.c
//
void IN_Init( void );
void Host_InputFrame( void );
void IN_Shutdown( void );
void IN_MouseEvent( int key, int down );
void IN_ClearMouseState( void );
void IN_MWheelEvent( int direction );
void IN_ActivateMouse( void );
void IN_DeactivateMouse( void );
void IN_MouseSavePos( void );
void IN_MouseRestorePos( void );
void IN_ToggleClientMouse( int newstate, int oldstate );
void IN_GyroInit( void );
void IN_GyroCheckAvailability( void );
void IN_GyroEvent( vec3_t data );
void IN_GyroFinalizeMove( float *fw, float *side, float *dpitch, float *dyaw );
void IN_GyroDrawDebug( void );

uint IN_CollectInputDevices( void );
void IN_LockInputDevices( qboolean lock );
void IN_EngineAppendMove( float frametime, usercmd_t *cmd, qboolean active );

void IN_SetRelativeMouseMode( qboolean set );
void IN_SetMouseGrab( qboolean set );

extern convar_t m_yaw;
extern convar_t m_pitch;
extern convar_t touch_enable;

//
// in_touch.c
//
typedef enum
{
	event_down = 0,
	event_up,
	event_motion
} touchEventType;

#if XASH_NO_TOUCH
static inline void Touch_Draw( void ) { }
static inline void Touch_SetClientOnly( byte state ) { }
static inline void Touch_RemoveButton( const char *name, qboolean privileged ) { }
static inline void Touch_HideButtons( const char *name, unsigned char hide, qboolean privileged ) { }
static inline void Touch_AddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags ) { }
static inline void Touch_AddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags ) { }
static inline void Touch_WriteConfig( void ) { }
static inline void Touch_Init( void ) { }
static inline void Touch_Shutdown( void ) { }
static inline void Touch_GetMove( float * forward, float *side, float *pitch, float *yaw ) { }
static inline void Touch_ResetDefaultButtons( void ) { }
static inline int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy ) { return 0; }
static inline void Touch_KeyEvent( int key, int down ) { }
static inline qboolean Touch_WantVisibleCursor( void ) { return false; }
static inline void Touch_NotifyResize( void ) { }
#else
void Touch_Draw( void );
void Touch_SetClientOnly( byte state );
void Touch_RemoveButton( const char *name, qboolean privileged );
void Touch_HideButtons( const char *name, unsigned char hide, qboolean privileged );
void Touch_AddClientButton( const char *name, const char *texture, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags );
void Touch_AddDefaultButton( const char *name, const char *texturefile, const char *command, float x1, float y1, float x2, float y2, byte *color, int round, float aspect, int flags );
void Touch_WriteConfig( void );
void Touch_Init( void );
void Touch_Shutdown( void );
void Touch_GetMove( float * forward, float *side, float *pitch, float *yaw );
void Touch_ResetDefaultButtons( void );
int IN_TouchEvent( touchEventType type, int fingerID, float x, float y, float dx, float dy );
void Touch_KeyEvent( int key, int down );
qboolean Touch_WantVisibleCursor( void );
void Touch_NotifyResize( void );
#endif

//
// in_osk.c
//
extern convar_t osk_enable;
void OSK_Init( void );
qboolean OSK_KeyEvent( int key, int down );
void OSK_EnableTextInput( qboolean enable, qboolean force );
void OSK_Draw( void );

//
// in_joy.c
//
enum
{
	JOY_HAT_CENTERED = 0,
	JOY_HAT_UP    = BIT(0),
	JOY_HAT_RIGHT = BIT(1),
	JOY_HAT_DOWN  = BIT(2),
	JOY_HAT_LEFT  = BIT(3),
	JOY_HAT_RIGHTUP   = JOY_HAT_RIGHT | JOY_HAT_UP,
	JOY_HAT_RIGHTDOWN = JOY_HAT_RIGHT | JOY_HAT_DOWN,
	JOY_HAT_LEFTUP    = JOY_HAT_LEFT  | JOY_HAT_UP,
	JOY_HAT_LEFTDOWN  = JOY_HAT_LEFT  | JOY_HAT_DOWN
};

typedef enum engineAxis_e
{
	JOY_AXIS_SIDE = 0,
	JOY_AXIS_FWD,
	JOY_AXIS_PITCH,
	JOY_AXIS_YAW,
	JOY_AXIS_RT,
	JOY_AXIS_LT,
	MAX_AXES,
} engineAxis_t;

typedef enum engineTouchpad_e
{
	JOY_TOUCHPAD_LEFT = 0,
	JOY_TOUCHPAD_RIGHT,
	MAX_TOUCHPADS,
} engineTouchpad_t;

typedef enum joy_calibration_state_s
{
	JOY_NOT_CALIBRATED = 0,
	JOY_CALIBRATING,
	JOY_FAILED_TO_CALIBRATE,
	JOY_CALIBRATED
} joy_calibration_state_t;

qboolean Joy_IsActive( void );
void Joy_SetCapabilities( qboolean have_gyro, int num_touchpads );
void Joy_SetCalibrationState( joy_calibration_state_t state );
void Joy_AxisMotionEvent( engineAxis_t engineAxis, short value );
void Joy_GyroEvent( vec3_t data );
void Joy_TouchpadEvent( engineTouchpad_t pad, qboolean down, float x, float y, float pressure );
void Joy_FinalizeMove( float *fw, float *side, float *dpitch, float *dyaw );
void Joy_DrawDebug( void );
void Joy_Init( void );
void Joy_Shutdown( void );
short Joy_CoF_AxisValue( engineAxis_t axis );
short Joy_CoF_AxisRaw( engineAxis_t axis );

//
// cof_gamepad.c (Cry of Fear: Enhanced gamepad support)
//
extern convar_t cof_joy_pulse_hysteresis;
void CL_CoF_PadInit( void );
void CL_CoF_PadMoveCommand( const char *command ); // scoped stick dodge guard
qboolean CL_CoF_ClickablePanelOpen( void );
qboolean CL_CoF_PadSuppressMove( void );
void CL_CoF_PadTraceMove( float forwardmove, float sidemove, qboolean suppressed );
void CL_CoF_PadTracePulses( uint oldflags, uint newflags, float forwardmove, float sidemove );
float CL_CoF_JoyPulseHysteresis( void );
qboolean CL_CoF_PadStartAsEscape( int key, const char *binding );
qboolean CL_CoF_PadKeyEvent( int key, int down );
qboolean CL_CoF_PadFrame( void );
qboolean CL_CoF_VCursorPos( int *x, int *y );
void CL_CoF_VCursorDraw( void );
void CL_CoF_InputEvent( const void *sdlEvent );	// host_sdl2.c: the last input device
void CL_CoF_NotePadAxis( int axis, short value );	// in_joy.c
void CL_CoF_NoteMouseWarp( int x, int y );		// in_sdl2.c Platform_SetMousePos
qboolean CL_CoF_PadHidesOSCursor( void );
qboolean CL_CoF_OSCursorRequest( int type );		// in_sdl2.c Platform_SetCursorType
float CL_CoF_QuickTurnYaw( void );			// input.c IN_EngineAppendMove: cof_quickturn
float CL_CoF_TriggerHysteresis( void );			// in_joy.c Joy_ProcessTrigger
void CL_CoF_NoteTriggerEdge( int axis, qboolean down, short value, int threshold );
void CL_CoF_PadMoveCorrectCommand( usercmd_t *cmd, float forward, float side );
qboolean CL_CoF_PadMoveShape( float *fw, float *side, float fwscale, float sidescale ); // in_joy.c Joy_FinalizeMove
void CL_CoF_PadMoveKeyThresholds( float *forward, float *side );	// input.c IN_JoyAppendMove
void CL_CoF_PadActivated( void *gamecontroller );	// joy_sdl2.c: the pad in use changed
void CL_CoF_NoteEventLoop( double seconds, int events );	// host_sdl2.c Platform_RunEvents

#endif//INPUT_H
