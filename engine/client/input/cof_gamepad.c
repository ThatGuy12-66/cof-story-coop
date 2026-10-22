/*
cof_gamepad.c - Cry of Fear: Enhanced gamepad support (engine input side)
Copyright (C) 2026 haej (Cry of Fear: Enhanced contributors)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

What this file adds on top of the engine's own SDL2 GameController input
(platform/sdl2/joy_sdl2.c, in_joy.c) for the Cry of Fear client:

 1. CL_CoF_ClickablePanelOpen(): "a clickable CoF panel is open" (inventory,
    note, documents, keypad, phone, tape-recorder page, YES/NO, ...). The
    source of truth is the panel identity of cof_panel_pause.c: the VGUI
    support library names every visible client panel by its class each
    painted frame (CL_CoF_PanelsOnScreen, COF_PANEL_* bits), and any modal
    class on screen means a panel is open. Fallback only: the client marks
    every such panel by showing the VGUI arrow cursor
    (TeamFortressViewport::UpdateCursorState, client 100AAAD0), which the
    engine records in host.mouse_visible (Platform_SetCursorType); that still
    counts when the class list names nothing (a panel class it does not know,
    or a vgui.dll without the panel report), and cof_input_trace says so.
    While it holds (and cof_pad_panel_gate is on):
      * the sticks do not move or turn the player (IN_EngineAppendMove);
      * START reaches the engine pause menu the way Escape does (in_keys.c);
      * the close key (cof_pad_close_key, B) backs out of the panel: the
        support library clicks the panel's own Back / Close / Cancel / No
        button (vguiapi_t::CofPanelAction, cofpanels.cpp); the inventory is
        handed the game's own "+inventory" binding string, which is what its
        key handler (client 10044040) compares to close itself; a panel
        without such a button gets the game's own Escape handling (its
        KeyInput sees ESCAPE / "cancelselect", as in retail) - no key is
        faked, no command is typed;
      * A / X click the left / right mouse button at the virtual cursor; an A
        on a tape recorder slot is followed by the second click the page
        wants (it acts on a double click), so the slot saves / loads at once.
 2. Pad defaults (cof_pad_defaults, cof_pad_defaults_gen): the game's own
    config.cfg starts with "unbindall", which wipes the engine's built-in pad
    binds. Once per profile, after config.cfg ran, every pad key is given the
    Cry of Fear layout below if no pad key is bound at all; a profile that has
    an older generation's defaults gets every key it did not customise moved to
    the new layout. The binds then live in config.cfg like any other bind.
    Gyro aiming (joy_gyro_enable) is off by default; saved profiles retain
    their gyro setting. B is a crouch toggle (cof_duck_toggle, an
    engine-side latch on the game's +duck, released on every level change and
    load). L3 is cof_sprint_toggle_press (cof_sprint_toggle: 1 = the first
    press holds the game's +sprint until the player stops moving, presses it
    again, or a load / level change; 0 = held while the key is held).
 3. The virtual cursor (cof_vcursor*): while a clickable panel is open the
    sticks move an engine-side cursor with acceleration; FreeVGUI reads it
    through VGUI_GetMousePos / VGui_MouseMove and the OS cursor is warped to
    match (cof_vcursor_warp) but hidden: the engine draws its own cursor there
    instead (CL_CoF_VCursorDraw: gfx/shell/gamepad/cursor.png, the user's
    square-frame art, with a dark shadow for the white note pages; the drawn
    ring of the first round when the file is missing). A real mouse movement
    hands the pointer back, with the OS arrow.
 5. The last input device (cof_last_input: pad / mouse / keyboard), from the
    SDL events themselves (host_sdl2.c), so a stick-made arrow key or a warp of
    the hidden cursor never counts as keyboard or mouse. The menu reads it for
    its button prompts. While the pad is the last device the OS arrow stays
    hidden in the menus and over the in-play panels (cof_pad_hide_cursor),
    decided inside Platform_SetCursorType itself, so the arrow the client or
    the menu asks for is never shown for a frame; a panel opened with the pad
    gets the gamepad cursor at once.
 4. The client's own WinMM joystick code (cvar "joystick") is kept off, so the
    engine's SDL pad is the only source (cof_joy_legacy_off).
 6. cof_quickturn (R3): a smooth 180-degree turn on the pad look's view-angle
    path (cof_quickturn_time).
 7. Triggers with hysteresis (cof_pad_trigger_hyst, in_joy.c): one key edge per
    real pull, so a trigger resting near its threshold cannot flip the aim
    down sights on and off.
 8. The left stick shaped like the movement keys (cof_pad_move_mode and
    friends): Digital-style preserves continuous direction and cancels retail
    CoF's mixed-axis forward halving; mode 2 retains its snapped mapping.
 9. Pad styles (cof_pad_style, cof_pad_style_auto): the style of the pad in
    use from SDL's controller type and ids (Xbox, PlayStation, Nintendo,
    Steam Deck), and face buttons by POSITION on Nintendo pads too
    (cof_pad_positional), so the layout, the menus, the on-screen keyboard
    and the panels agree on every pad.

Nothing here does anything without a gamepad: the sticks are zero, pad keys
never arrive, the binds are inert. The developer hooks cof_joy_axis_probe and
cof_vcursor_test drive the same code paths from a cfg, like cof_key_probe
does for keys, without any OS input.
*/

#include <float.h>
#if XASH_SDL == 2
#include <SDL.h>
#endif

#include "common.h"
#include "input.h"
#include "keydefs.h"
#include "client.h"
#include "vgui_draw.h"
#include "platform/platform.h"
#include "ref_common.h"
#include "vgui_api.h"

#ifndef SHRT_MAX
#define SHRT_MAX 0x7FFF
#endif

// bump when the default layout changes in a way existing profiles should get
// (CL_CoF_PadDefaultsCheck migrates keys still holding the previous defaults).
// 6 (1.2.3): the same layout as 5; the engine's own Half-Life pad binds a
// profile kept are moved to it (cof_pad_layout_builtin)
#define COF_PAD_DEFAULTS_GENERATION 6

static CVAR_DEFINE_AUTO( cof_pad_panel_gate, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad: while a clickable panel (inventory, note, keypad...) is open the sticks do not move the player, START opens the pause menu, the close key closes the inventory and A/X click" );
static CVAR_DEFINE_AUTO( cof_pad_close_key, "B_BUTTON", FCVAR_ARCHIVE, "Cry of Fear gamepad: key that backs out of an open panel (its own Back/Close/Cancel/No button; the inventory closes through its +inventory binding); empty = off" );
static CVAR_DEFINE_AUTO( cof_vcursor, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad: the sticks move a mouse cursor while a clickable panel is open" );
static CVAR_DEFINE_AUTO( cof_vcursor_speed, "0.9", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: speed at full stick deflection, in screen heights per second" );
static CVAR_DEFINE_AUTO( cof_vcursor_accel, "1.5", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: extra speed reached after holding the stick for cof_vcursor_accel_time (0 = constant speed; 1.5 = up to 2.5x)" );
static CVAR_DEFINE_AUTO( cof_vcursor_accel_time, "0.6", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: seconds of continuous stick motion until the full acceleration" );
static CVAR_DEFINE_AUTO( cof_vcursor_curve, "2", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: response exponent (1 = linear, 2 = fine control near the centre)" );
static CVAR_DEFINE_AUTO( cof_vcursor_deadzone, "0.2", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: stick deadzone, 0..0.9 of full deflection" );
static CVAR_DEFINE_AUTO( cof_vcursor_warp, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: also move the visible OS cursor to the gamepad cursor" );
static CVAR_DEFINE_AUTO( cof_vcursor_click_key, "A_BUTTON", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: key that clicks the left mouse button while a panel is open; empty = off" );
static CVAR_DEFINE_AUTO( cof_vcursor_rclick_key, "X_BUTTON", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: key that clicks the right mouse button while a panel is open; empty = off" );
static CVAR_DEFINE_AUTO( cof_vcursor_size, "44", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: size of the cursor in pixels at UI scale 1 (grows with the in-game UI scale)" );
static CVAR_DEFINE_AUTO( cof_vcursor_alpha, "0.85", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: opacity of the cursor, 0..1" );
static CVAR_DEFINE_AUTO( cof_vcursor_shadow, "0.7", FCVAR_ARCHIVE, "Cry of Fear gamepad cursor: opacity of the dark shadow under the cursor art that keeps it visible on white paper, 0..1" );
static CVAR_DEFINE_AUTO( cof_pad_hide_cursor, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad: hide the Windows arrow in the menus and over the in-play panels while the gamepad is the last input device" );
static CVAR_DEFINE_AUTO( cof_last_input, "mouse", 0, "Cry of Fear: the last input device (pad, mouse or keyboard); the menu shows its button prompts while it is pad" );
static CVAR_DEFINE_AUTO( cof_sprint_toggle, "1", FCVAR_ARCHIVE, "Cry of Fear: what cof_sprint_toggle_press (L3 by default) does - 1 = toggle: the first press sprints until you stop moving or press it again, 0 = hold: sprint while the key is held" );
static CVAR_DEFINE_AUTO( cof_sprint_stop_threshold, "0.2", FCVAR_ARCHIVE, "Cry of Fear sprint toggle: below this left-stick deflection (0..0.9, and with no movement key held) the player counts as stopped" );
static CVAR_DEFINE_AUTO( cof_sprint_stop_time, "0.15", FCVAR_ARCHIVE, "Cry of Fear sprint toggle: seconds of standing still that release the sprint" );
CVAR_DEFINE_AUTO( cof_joy_pulse_hysteresis, "0.25", FCVAR_ARCHIVE, "Cry of Fear gamepad: a stick's +forward/+back/+moveleft/+moveright key is released only this far (0..0.6 of full deflection) below the level that pressed it, so a stick resting near the walk threshold cannot double-tap a dodge" );
static CVAR_DEFINE_AUTO( cof_pad_no_doubletap, "1", FCVAR_ARCHIVE, "Cry of Fear: suppress dodge from engine stick movement pulses only; 0 restores stick double-tap dodge (keyboard unchanged)" );
static CVAR_DEFINE_AUTO( cof_joy_legacy_off, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad: keep the game's own WinMM joystick code off (cvar joystick 0) so the engine's gamepad is the only source" );
static CVAR_DEFINE_AUTO( cof_pad_defaults_gen, "0", FCVAR_ARCHIVE, "Cry of Fear gamepad: default layout generation already applied to this profile (0 = never)" );
static CVAR_DEFINE_AUTO( cof_input_trace, "0", 0, "Cry of Fear gamepad: developer log of the panel check, START/close/click routing, stick suppression and the virtual cursor" );

// gamepad3 round: quick turn, trigger hysteresis, stick movement shape, pad styles
static CVAR_DEFINE_AUTO( cof_quickturn_time, "0.18", FCVAR_ARCHIVE, "Cry of Fear: seconds cof_quickturn takes for its 180-degree turn (0.05..1; eased in and out)" );
static CVAR_DEFINE_AUTO( cof_pad_trigger_hyst, "0.1", FCVAR_ARCHIVE, "Cry of Fear gamepad: trigger hysteresis, 0..0.4 of the full pull around joy_lt_threshold / joy_rt_threshold: LT/RT press above threshold + this and release below threshold - this (0 = the engine's plain threshold)" );
static CVAR_DEFINE_AUTO( cof_pad_move_fix, "1", FCVAR_ARCHIVE, "Cry of Fear Digital-style movement: cancel retail mixed-axis forward slowdown; 0 restores legacy behavior" );
static CVAR_DEFINE_AUTO( cof_pad_move_mode, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad movement: 0 = raw analogue stick, 1 = digital-style (above cof_pad_move_walk the stick counts as fully pushed in its direction, like the movement keys), 2 = digital-style snapped to 8 directions" );
static CVAR_DEFINE_AUTO( cof_pad_move_deadzone, "0.15", FCVAR_ARCHIVE, "Cry of Fear gamepad movement (modes 1 and 2): radial deadzone of the left stick, 0..0.5 of full deflection" );
static CVAR_DEFINE_AUTO( cof_pad_move_walk, "0.6", FCVAR_ARCHIVE, "Cry of Fear gamepad movement (modes 1 and 2): deflection (0.2..1) from which the stick counts as fully pushed; below it the player walks slower, shaped by cof_pad_move_curve" );
static CVAR_DEFINE_AUTO( cof_pad_move_diagonal, "1.2", FCVAR_ARCHIVE, "Cry of Fear gamepad movement (modes 1 and 2): diagonal boost, 1..1.5 - 1 = the keyboard's diagonal speed (the game walks diagonals at 187.5 instead of 225 u/s), 1.2 = diagonals as fast as straight ahead (the game caps at the straight speed)" );
static CVAR_DEFINE_AUTO( cof_pad_move_curve, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad movement (modes 1 and 2): response exponent between the deadzone and cof_pad_move_walk (1 = linear, 2 = finer control of slow walking)" );
CVAR_DEFINE_AUTO( cof_pad_style, "-1", FCVAR_ARCHIVE, "Cry of Fear gamepad: button pictures and names - -1 (or auto) = Auto, the style of the pad in use (cof_pad_style_auto); 0 = Xbox, 1 = PlayStation, 2 = Nintendo, 3 = Steam Deck by hand" );
static CVAR_DEFINE_AUTO( cof_pad_style_auto, "0", FCVAR_READ_ONLY, "Cry of Fear gamepad: the style of the pad in use, 0..3 = Xbox, PlayStation, Nintendo, Steam Deck (Xbox without a pad); what cof_pad_style -1 (Auto) shows" );
static CVAR_DEFINE_AUTO( cof_pad_positional, "1", FCVAR_ARCHIVE, "Cry of Fear gamepad: 1 = face buttons by POSITION on every pad (a Nintendo pad's bottom button is A_BUTTON = jump / confirm, its right one B_BUTTON = back), 0 = by the printed label (SDL's default for Nintendo pads)" );
static CVAR_DEFINE_AUTO( cof_pad_type, "none", FCVAR_READ_ONLY, "Cry of Fear gamepad: the style detected for the pad in use (xbox, ps5, switch, steamdeck; none = no pad)" );

typedef struct
{
	int        key;
	const char *binding;
} cof_pad_bind_t;

// the Cry of Fear gamepad layout (generation 5, still in 6); the same list is in
// gfx/shell/kb_def.lst (Options > Keybinds > Use defaults) and
// docs/patches/cof-gamepad-input.md. The engine key names are POSITIONS
// (A = the bottom face button, B = right, X = left, Y = top) on every pad,
// Nintendo pads included (cof_pad_positional), so the layout is positional.
// On an open panel A/X click and B backs out instead (CL_CoF_PadKeyEvent).
static const cof_pad_bind_t cof_pad_layout[] =
{
	{ K_JOY2,          "+attack" },         // RTRIGGER
	{ K_JOY1,          "+attack3" },        // LTRIGGER: aim down sights, Hold/Toggle per cof_ads_toggle
	{ K_A_BUTTON,      "+jump" },
	{ K_B_BUTTON,      "cof_duck_toggle" }, // crouch, press to toggle
	{ K_X_BUTTON,      "+reload" },
	{ K_Y_BUTTON,      "+use" },
	{ K_L1_BUTTON,     "+attack2" },        // secondary: bash / weapon mode / flashlight (the MOUSE3 action)
	{ K_R1_BUTTON,     "+dodge" },          // ALT action
	{ K_LSTICK,        "cof_sprint_toggle_press" }, // sprint, Toggle / Hold per cof_sprint_toggle
	{ K_RSTICK,        "cof_quickturn" },    // quick 180-degree turn
	{ K_DPAD_UP,       "quicksel 1" },
	{ K_DPAD_LEFT,     "quicksel 2" },
	{ K_DPAD_RIGHT,    "quicksel 3" },
	{ K_DPAD_DOWN,     "weapontoggle" },    // the game's "Toggle weapon function"
	{ K_BACK_BUTTON,   "+inventory" },      // View: opens and closes the inventory (the game's own key check)
	{ K_START_BUTTON,  "cancelselect" },
};

// Generation 4 defaults: migrate only bindings that still match.
static const cof_pad_bind_t cof_pad_layout_gen4[] =
{
	{ K_JOY2,          "+attack" },         // RTRIGGER
	{ K_JOY1,          "+attack3" },        // LTRIGGER: aim down sights, Hold/Toggle per cof_ads_toggle
	{ K_A_BUTTON,      "+jump" },
	{ K_B_BUTTON,      "cof_duck_toggle" }, // crouch, press to toggle
	{ K_X_BUTTON,      "+reload" },
	{ K_Y_BUTTON,      "+use" },
	{ K_L1_BUTTON,     "+attack2" },        // secondary: bash / weapon mode / flashlight (the MOUSE3 action)
	{ K_R1_BUTTON,     "cof_quickturn" },   // quick 180-degree turn
	{ K_LSTICK,        "cof_sprint_toggle_press" }, // sprint, Toggle / Hold per cof_sprint_toggle
	{ K_RSTICK,        "+duck" },           // crouch while held
	{ K_DPAD_UP,       "quicksel 1" },
	{ K_DPAD_LEFT,     "quicksel 2" },
	{ K_DPAD_RIGHT,    "quicksel 3" },
	{ K_DPAD_DOWN,     "weapontoggle" },    // the game's "Toggle weapon function"
	{ K_BACK_BUTTON,   "+inventory" },      // View: opens and closes the inventory (the game's own key check)
	{ K_START_BUTTON,  "cancelselect" },
};

// generation 3 (gamepad2 round): a key that still holds its generation-3 bind
// was never customised and moves to the layout above
static const cof_pad_bind_t cof_pad_layout_gen3[] =
{
	{ K_JOY2,          "+attack" },
	{ K_JOY1,          "+attack3" },
	{ K_A_BUTTON,      "+jump" },
	{ K_B_BUTTON,      "cof_duck_toggle" },
	{ K_X_BUTTON,      "+use" },
	{ K_Y_BUTTON,      "+inventory" },
	{ K_L1_BUTTON,     "+reload" },
	{ K_R1_BUTTON,     "+attack2" },
	{ K_LSTICK,        "cof_sprint_toggle_press" },
	{ K_RSTICK,        "+duck" },
	{ K_DPAD_UP,       "quicksel 1" },
	{ K_DPAD_LEFT,     "quicksel 2" },
	{ K_DPAD_RIGHT,    "quicksel 3" },
	{ K_DPAD_DOWN,     "weapontoggle" },
	{ K_BACK_BUTTON,   "+objectives" },
	{ K_START_BUTTON,  "cancelselect" },
};

// generation 2 (m8): a key that still holds its generation-2 bind was never
// customised and moves to the layout above
static const cof_pad_bind_t cof_pad_layout_gen2[] =
{
	{ K_JOY2,          "+attack" },
	{ K_JOY1,          "+attack3" },
	{ K_A_BUTTON,      "+jump" },
	{ K_B_BUTTON,      "cof_duck_toggle" },
	{ K_X_BUTTON,      "+use" },
	{ K_Y_BUTTON,      "+inventory" },
	{ K_L1_BUTTON,     "+reload" },
	{ K_R1_BUTTON,     "+attack2" },
	{ K_LSTICK,        "+sprint" },
	{ K_RSTICK,        "+duck" },
	{ K_DPAD_UP,       "quicksel 1" },
	{ K_DPAD_LEFT,     "quicksel 2" },
	{ K_DPAD_RIGHT,    "quicksel 3" },
	{ K_DPAD_DOWN,     "flashlight" },
	{ K_BACK_BUTTON,   "+objectives" },
	{ K_START_BUTTON,  "cancelselect" },
};

// 1.2.3: the engine's own pad binds (in_keys.c keynames, Half-Life's: +speed and
// lastinv do not exist in Cry of Fear). The game's config.cfg starts with
// "unbindall", which wipes them; the config.cfg the co-op app writes for a new
// copy (its keyboard list) does not, so every such profile kept them, and the
// defaults check saw pad keys bound and left them: sprint on the left trigger
// and stick did nothing ("Unknown command: +speed", a guest's log 2026-10-21).
// A key that still holds one moves to the layout above.
static const cof_pad_bind_t cof_pad_layout_builtin[] =
{
	{ K_A_BUTTON,      "+jump" },
	{ K_B_BUTTON,      "+use" },
	{ K_X_BUTTON,      "+reload" },
	{ K_Y_BUTTON,      "impulse 100" },
	{ K_BACK_BUTTON,   "pause" },
	{ K_START_BUTTON,  "cancelselect" },
	{ K_LSTICK,        "+speed" },
	{ K_RSTICK,        "+duck" },
	{ K_L1_BUTTON,     "+duck" },
	{ K_R1_BUTTON,     "+attack" },
	{ K_DPAD_UP,       "impulse 201" },
	{ K_DPAD_DOWN,     "lastinv" },
	{ K_DPAD_LEFT,     "invprev" },
	{ K_DPAD_RIGHT,    "invnext" },
	{ K_L2_BUTTON,     "+speed" },
	{ K_R2_BUTTON,     "+attack2" },
	{ K_JOY1,          "+speed" },
	{ K_JOY2,          "+attack2" },
};

// generation 1 (the first staging of this round): a key that still holds its
// generation-1 bind was never customised and moves to the layout above
static const cof_pad_bind_t cof_pad_layout_gen1[] =
{
	{ K_JOY2,          "+attack" },
	{ K_JOY1,          "+attack3" },
	{ K_A_BUTTON,      "+use" },
	{ K_B_BUTTON,      "+inventory" },
	{ K_X_BUTTON,      "+reload" },
	{ K_Y_BUTTON,      "flashlight" },
	{ K_L1_BUTTON,     "invprev" },
	{ K_R1_BUTTON,     "invnext" },
	{ K_LSTICK,        "+sprint" },
	{ K_RSTICK,        "+duck" },
	{ K_DPAD_UP,       "quicksel 1" },
	{ K_DPAD_LEFT,     "quicksel 2" },
	{ K_DPAD_RIGHT,    "quicksel 3" },
	{ K_DPAD_DOWN,     "+jump" },
	{ K_BACK_BUTTON,   "+objectives" },
	{ K_START_BUTTON,  "cancelselect" },
};

static struct
{
	qboolean panel_open;       // last frame's CL_CoF_ClickablePanelOpen()
	unsigned int panel_classes; // COF_PANEL_* bits behind the last answer (0 = none, or the cursor fallback)
	qboolean panel_by_cursor;  // the last answer came from the cursor fallback
	qboolean defaults_checked; // the once-per-launch defaults check ran
	qboolean legacy_reported;
	qboolean move_suppressed;  // reported once per open panel
	char     defaults_note[192]; // what the once-per-launch defaults check did

	// crouch toggle (cof_duck_toggle)
	qboolean duck_on;
	int      duck_servercount;
	double   next_trace;

	// sprint (cof_sprint_toggle_press)
	qboolean sprint_on;
	qboolean sprint_hold;      // hold mode: released with the key
	qboolean sprint_moved;     // the player moved since the press
	int      sprint_servercount;
	int      sprint_still_frames;
	double   sprint_since;     // press time
	double   sprint_still_since;

	// the last input device (cof_last_input) and the OS arrow
	int      device;           // COF_INPUT_*
	qboolean os_hidden;        // the OS arrow is hidden by this file
	int      warp_target[4][2]; // where the engine last warped the OS cursor (window pixels), a small ring
	double   warp_time[4];
	int      warp_next;
	float    mouse_travel;     // real mouse motion since the last pad input
	double   mouse_travel_time;

	// cursor art (gfx/shell/gamepad/cursor.png) and its shadow
	int      art_tex, shadow_tex;
	qboolean art_missing;

	// virtual cursor, in the render pixels Platform_GetMousePos() returns
	qboolean engaged;          // the gamepad owns the pointer
	qboolean centre_pending;   // the next engagement starts in the middle of the screen (a panel just opened)
	float    x, y;
	int      warp_x, warp_y;   // where the OS cursor was last sent
	int      real_x, real_y;   // real mouse position seen last frame
	float    hold;             // seconds of continuous stick motion

	// pad key presses captured while a panel was open: what their release
	// must do (K_MOUSE1/K_MOUSE2 = release that button, -1 = swallow)
	short    captured[256];

	// quick turn (cof_quickturn)
	qboolean turn_on;
	double   turn_t;           // seconds into the turn
	float    turn_done;        // degrees applied so far
	float    turn_yaw0;
	int      turn_servercount;

	// stick movement shape: the stick counts as fully pushed this frame
	qboolean move_saturated;
	qboolean move_digital_active;

	// pad styles (-1 = not known yet)
	int      style_detected;   // the style of the pad in use
	int      style_resolved;   // cof_pad_style_auto as last set
	int      hint_labels;      // SDL_GAMECONTROLLER_USE_BUTTON_LABELS as last set
	int      pad_type;         // SDL_GameControllerType of the pad in use
	char     pad_name[64];

	// input counters (cof_pad_measure)
	int      stat_events, stat_axis_events, stat_sensor_events, stat_trigger_edges, stat_pad_keys, stat_pad_switches;
	double   stat_event_time;

	// a cof_pad_measure window
	qboolean meas_on;
	char     meas_label[64];
	double   meas_start, meas_end, meas_sum, meas_max, meas_event_time;
	int      meas_frames;
	int      meas_c[6];
	float    meas_vmax, meas_vsum;
	vec3_t   meas_org;
	char     meas_next[64];    // alias to run when the window ends

	// cof_pad_jitter: an axis moved between two values every frame
	int      jit_axis;         // -1 = off
	int      jit_lo, jit_hi, jit_phase;
	qboolean jit_virtual;
} cof_pad;

static void CL_CoF_MeasureFrame( void );
static void CL_CoF_PadStyleFrame( void );
static void CL_CoF_PadTrace( const char *fmt, ... ) FORMAT_CHECK( 1 );
static void CL_CoF_PadTrace( const char *fmt, ... )
{
	va_list args;
	char    text[1024];

	if( !cof_input_trace.value )
		return;

	va_start( args, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, args );
	va_end( args );

	Con_Printf( "[cof-pad] %s", text );
}

/*
============
CL_CoF_ClickablePanelOpen

A clickable Cry of Fear panel owns the screen: in play, and a modal client
panel is on screen by class (CL_CoF_PanelsOnScreen, cof_panel_pause.c); the
VGUI arrow cursor counts only when the class list names none. See the comment
at the top of this file.
============
*/
qboolean CL_CoF_ClickablePanelOpen( void )
{
	qboolean     reporting = false;
	qboolean     by_cursor = false;
	qboolean     open = false;
	unsigned int classes = 0;

	if( cls.state == ca_active && cls.key_dest == key_game && !cl.background && VGui_IsActive( ))
	{
		classes = CL_CoF_PanelsOnScreen( &reporting );
		open = classes != 0;

		if( !open && host.mouse_visible )
			open = by_cursor = true;
	}

	if( by_cursor && !cof_pad.panel_by_cursor )
		CL_CoF_PadTrace( "panel open by the arrow cursor only: %s\n",
			reporting ? "no known panel class on screen (the class list missed it)" : "no panel report from vgui.dll" );

	cof_pad.panel_classes = classes;
	cof_pad.panel_by_cursor = by_cursor;

	return open;
}

static qboolean CL_CoF_PadGateActive( void )
{
	return cof_pad_panel_gate.value && CL_CoF_ClickablePanelOpen( );
}

static int CL_CoF_PadKeyFromCvar( const convar_t *cv )
{
	if( !cv->string || !cv->string[0] )
		return -1;

	for( int i = K_JOY1; i <= K_AUX32; i++ )
	{
		if( !Q_stricmp( Key_KeynumToString( i ), cv->string ))
			return i;
	}

	return -1;
}

static float CL_CoF_PadAxis( engineAxis_t axis )
{
	return (float)Joy_CoF_AxisValue( axis ) / (float)SHRT_MAX;
}

/*
==============================================================================

The last input device (cof_last_input) and the OS arrow

==============================================================================
*/
enum
{
	COF_INPUT_MOUSE = 0,
	COF_INPUT_KEYBOARD,
	COF_INPUT_PAD,
};

static const char *const cof_input_names[] = { "mouse", "keyboard", "pad" };

static int CL_CoF_DeviceFromCvar( void )
{
	const char *s = cof_last_input.string;

	if( !Q_stricmp( s, "pad" ) || !Q_stricmp( s, "gamepad" ) || !Q_stricmp( s, "joy" ) || !Q_strcmp( s, "1" ))
		return COF_INPUT_PAD;
	if( !Q_stricmp( s, "keyboard" ))
		return COF_INPUT_KEYBOARD;
	return COF_INPUT_MOUSE;
}

/*
============
CL_CoF_PadHidesOSCursor

in_sdl2.c Platform_SetCursorType asks this before it shows the arrow: while
the gamepad is the last input device the arrow stays hidden in the menus and
over the in-play panels (the only reason the client shows an arrow in play is
a modal panel, and there the gamepad draws its own cursor). host.mouse_visible
and the cursor type stay as asked, only the OS arrow is left out, so the panel
check and the mouse grab logic are unaffected.
============
*/
qboolean CL_CoF_PadHidesOSCursor( void )
{
	if( !cof_pad_hide_cursor.value || CL_CoF_DeviceFromCvar( ) != COF_INPUT_PAD || !Joy_IsActive( ))
		return false;

	if( cls.key_dest == key_menu )
		return true;

	if( cls.key_dest == key_game && cls.state == ca_active && !cl.background
		&& cof_pad_panel_gate.value && cof_vcursor.value )
		return true;

	return false;
}

/*
============
CL_CoF_OSCursorRequest

in_sdl2.c Platform_SetCursorType, for a visible cursor type: whether the OS
arrow may really be shown (cof_input_trace 2 logs every such request).
============
*/
qboolean CL_CoF_OSCursorRequest( int type )
{
	const qboolean hide = CL_CoF_PadHidesOSCursor( );

	if( cof_input_trace.value >= 2 )
		CL_CoF_PadTrace( "cursor %d requested (key_dest %d, frame %d): OS arrow %s\n", type, (int)cls.key_dest, (int)host.framecount,
			hide ? "kept hidden, the gamepad is the last input" : "shown" );

	cof_pad.os_hidden = cof_pad.os_hidden || hide;
	return !hide;
}

/*
============
CL_CoF_ApplyOSCursor

Brings the OS arrow in line with the answer above (and hides it while the
gamepad cursor owns the pointer); shows it again only when this file hid it.
============
*/
static void CL_CoF_ApplyOSCursor( void )
{
#if XASH_SDL == 2
	qboolean hide = host.mouse_visible && ( CL_CoF_PadHidesOSCursor( ) || cof_pad.engaged );
	int      shown = SDL_ShowCursor( SDL_QUERY );

	if( hide && shown == SDL_ENABLE )
	{
		SDL_ShowCursor( SDL_DISABLE );
		CL_CoF_PadTrace( "OS arrow hidden (%s, key_dest %d, frame %d)\n", cof_pad.engaged ? "gamepad cursor" : "gamepad is the last input",
			(int)cls.key_dest, (int)host.framecount );
	}
	else if( !hide && shown == SDL_DISABLE && cof_pad.os_hidden && host.mouse_visible )
	{
		SDL_ShowCursor( SDL_ENABLE );
		CL_CoF_PadTrace( "OS arrow shown (%s input, key_dest %d, frame %d)\n", cof_input_names[CL_CoF_DeviceFromCvar( )],
			(int)cls.key_dest, (int)host.framecount );
	}

	cof_pad.os_hidden = hide;

	if( cof_input_trace.value >= 2 )
		CL_CoF_PadTrace( "frame %d: OS arrow %s (key_dest %d, mouse_visible %d, last input %s, gamepad cursor %s)\n", (int)host.framecount,
			SDL_ShowCursor( SDL_QUERY ) == SDL_ENABLE ? "SHOWN" : "hidden", (int)cls.key_dest, (int)host.mouse_visible,
			cof_input_names[CL_CoF_DeviceFromCvar( )], cof_pad.engaged ? "engaged" : "idle" );
#endif
}

/*
============
CL_CoF_NoteInput

The last input device changed (or was confirmed).
============
*/
static void CL_CoF_NoteInput( int device, const char *why )
{
	if( device == COF_INPUT_PAD )
	{
		cof_pad.mouse_travel = 0.0f;
		cof_pad.mouse_travel_time = host.realtime;
	}

	if( CL_CoF_DeviceFromCvar( ) == device )
		return;

	Cvar_DirectSet( &cof_last_input, cof_input_names[device] );
	cof_pad.device = device;
	CL_CoF_PadTrace( "last input: %s (%s)\n", cof_input_names[device], why );
	CL_CoF_ApplyOSCursor( );
}

/*
============
CL_CoF_NoteMouseWarp

in_sdl2.c Platform_SetMousePos: where the engine itself moved the OS cursor
(window pixels), so the motion event SDL makes of the warp is not taken for
the mouse.
============
*/
void CL_CoF_NoteMouseWarp( int x, int y )
{
	int i = cof_pad.warp_next;

	// a few of them: hiding and showing the arrow in one frame warps twice
	// (to the centre, then back), and SDL reports both afterwards
	cof_pad.warp_target[i][0] = x;
	cof_pad.warp_target[i][1] = y;
	cof_pad.warp_time[i] = host.realtime;
	cof_pad.warp_next = ( i + 1 ) % ARRAYSIZE( cof_pad.warp_time );
}

static qboolean CL_CoF_IsWarpEvent( int x, int y )
{
	// the window centre: where the engine parks a hidden cursor, and where
	// the client's own IN_ResetMouse (SetCursorPos, client 1007BBB0) puts it
	// when its panels change
	if( abs( x - host.window_center_x ) <= 1 && abs( y - host.window_center_y ) <= 1 )
		return true;

	for( int i = 0; i < ARRAYSIZE( cof_pad.warp_time ); i++ )
	{
		if( x == cof_pad.warp_target[i][0] && y == cof_pad.warp_target[i][1] && host.realtime - cof_pad.warp_time[i] < 1.0 )
			return true;
	}

	return false;
}

/*
============
CL_CoF_InputEvent

host_sdl2.c SDLash_EventHandler, before the event is handled: which device
the player is using. Real key presses are the keyboard (the arrow keys the
engine makes from the stick in menus never pass here), real motion and
buttons are the mouse (a warp's own motion event is left out, and a few
pixels of travel are needed, so a desk bump does not take the pointer from
the pad), controller buttons and deflected sticks/triggers are the pad (the
gyro and touchpad reports are not).
============
*/
void CL_CoF_InputEvent( const void *sdlEvent )
{
#if XASH_SDL == 2
	const SDL_Event *ev = (const SDL_Event *)sdlEvent;

	switch( ev->type )
	{
	case SDL_KEYDOWN:
		if( !ev->key.repeat )
			CL_CoF_NoteInput( COF_INPUT_KEYBOARD, "key" );
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEWHEEL:
		CL_CoF_NoteInput( COF_INPUT_MOUSE, "mouse button" );
		break;
	case SDL_MOUSEMOTION:
		if( ev->motion.which == SDL_TOUCH_MOUSEID )
			break;

		// the events SDL makes of the engine's (and the client's) own warps
		if( CL_CoF_IsWarpEvent( ev->motion.x, ev->motion.y ))
			break;

		if( host.realtime - cof_pad.mouse_travel_time > 0.5 )
			cof_pad.mouse_travel = 0.0f;

		cof_pad.mouse_travel += abs( ev->motion.xrel ) + abs( ev->motion.yrel );
		cof_pad.mouse_travel_time = host.realtime;

		if( cof_pad.mouse_travel >= 6.0f )
			CL_CoF_NoteInput( COF_INPUT_MOUSE, va( "mouse moved, to %d %d", ev->motion.x, ev->motion.y ));
		break;
	case SDL_CONTROLLERBUTTONDOWN:
		CL_CoF_NoteInput( COF_INPUT_PAD, "gamepad button" );
		break;
	case SDL_CONTROLLERSENSORUPDATE:
		cof_pad.stat_sensor_events++;
		break;
	case SDL_CONTROLLERAXISMOTION:
		cof_pad.stat_axis_events++;
		if( abs( ev->caxis.value ) > 12000 )
			CL_CoF_NoteInput( COF_INPUT_PAD, "gamepad stick" );
		break;
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYHATMOTION:
		CL_CoF_NoteInput( COF_INPUT_PAD, "joystick" );
		break;
	}
#else
	(void)sdlEvent;
#endif
}

/*
============
CL_CoF_NotePadAxis

in_joy.c Joy_AxisMotionEvent (the SDL backend and cof_joy_axis_probe both
come through there): a stick or trigger pushed well past its deadzone.
============
*/
void CL_CoF_NotePadAxis( int axis, short value )
{
	(void)axis;

	if( abs( value ) > 12000 )
		CL_CoF_NoteInput( COF_INPUT_PAD, "gamepad axis" );
}

/*
============
CL_CoF_PadSuppressMove

IN_EngineAppendMove asks whether the sticks may drive the player this frame.
============
*/
qboolean CL_CoF_PadSuppressMove( void )
{
	if( !CL_CoF_PadGateActive( ))
		return false;

	if( !cof_pad.move_suppressed )
	{
		cof_pad.move_suppressed = true;
		CL_CoF_PadTrace( "panel open: sticks do not move or turn the player (fwd %.2f side %.2f pitch %.2f yaw %.2f)\n",
			CL_CoF_PadAxis( JOY_AXIS_FWD ), CL_CoF_PadAxis( JOY_AXIS_SIDE ), CL_CoF_PadAxis( JOY_AXIS_PITCH ), CL_CoF_PadAxis( JOY_AXIS_YAW ));
	}

	return true;
}

float CL_CoF_JoyPulseHysteresis( void )
{
	return bound( 0.0f, cof_joy_pulse_hysteresis.value, 0.6f );
}

/*
============
CL_CoF_PadTraceMove

cof_input_trace: what the sticks put into the usercmd, twice a second while a
stick is off centre, with the predicted origin, so a log shows whether the
player moved.
============
*/
void CL_CoF_PadTraceMove( float forwardmove, float sidemove, qboolean suppressed )
{
	float fwd = CL_CoF_PadAxis( JOY_AXIS_FWD ), side = CL_CoF_PadAxis( JOY_AXIS_SIDE );
	float pitch = CL_CoF_PadAxis( JOY_AXIS_PITCH ), yaw = CL_CoF_PadAxis( JOY_AXIS_YAW );

	if( !cof_input_trace.value || host.realtime < cof_pad.next_trace )
		return;

	if( !fwd && !side && !pitch && !yaw )
		return;

	cof_pad.next_trace = host.realtime + 0.5;
	CL_CoF_PadTrace( "move: sticks fwd %.2f side %.2f pitch %.2f yaw %.2f -> cmd forward %.0f side %.0f%s, origin %.1f %.1f %.1f, angles %.1f %.1f\n",
		fwd, side, pitch, yaw, forwardmove, sidemove, suppressed ? " (panel open: suppressed)" : "",
		cl.simorg[0], cl.simorg[1], cl.simorg[2], cl.viewangles[PITCH], cl.viewangles[YAW] );
}

/*
============
CL_CoF_PadTracePulses

cof_input_trace: the +forward/+back/+moveleft/+moveright keys the stick
presses and releases (input.c IN_JoyAppendMove), to check the dodge guard.
Flags: 1 forward, 2 back, 4 left, 8 right (input.c F/B/L/R).
============
*/
void CL_CoF_PadTracePulses( uint oldflags, uint newflags, float forwardmove, float sidemove )
{
	static const char *const names[4] = { "forward", "back", "moveleft", "moveright" };
	char text[128];

	if( !cof_input_trace.value )
		return;

	text[0] = 0;
	for( int i = 0; i < 4; i++ )
	{
		if(( oldflags ^ newflags ) & BIT( i ))
			Q_strncat( text, va( " %c%s", ( newflags & BIT( i )) ? '+' : '-', names[i] ), sizeof( text ));
	}

	if( text[0] )
		CL_CoF_PadTrace( "stick keys:%s (forward %.2f side %.2f, hysteresis %.2f, t %.3f)\n", text, forwardmove, sidemove,
			CL_CoF_JoyPulseHysteresis( ), host.realtime );
}

/*
============
CL_CoF_PadStartAsEscape

in_keys.c: START gets the Escape bypass while a clickable panel is open (the
panels swallow every key but their own, so START would never reach the
client's "cancelselect" -> "escape"). Only while START still carries its
stock "cancelselect" binding.
============
*/
qboolean CL_CoF_PadStartAsEscape( int key, const char *binding )
{
	if( key != K_START_BUTTON || !binding || Q_stricmp( binding, "cancelselect" ))
		return false;

	if( !CL_CoF_PadGateActive( ))
		return false;

	CL_CoF_PadTrace( "START on an open panel: routed to the engine pause menu like Escape\n" );
	return true;
}

/*
============
CL_CoF_VCursorApply

Send the virtual cursor to FreeVGUI (push path) and the OS cursor.
============
*/
static void CL_CoF_VCursorApply( void )
{
	int x, y;

	cof_pad.x = bound( 0.0f, cof_pad.x, (float)( refState.width - 1 ));
	cof_pad.y = bound( 0.0f, cof_pad.y, (float)( refState.height - 1 ));
	x = (int)cof_pad.x;
	y = (int)cof_pad.y;

	if( cof_vcursor_warp.value && ( x != cof_pad.warp_x || y != cof_pad.warp_y ))
	{
		float sx = refState.scale_x > 0.0f ? refState.scale_x : 1.0f;
		float sy = refState.scale_y > 0.0f ? refState.scale_y : 1.0f;

		// Platform_GetMousePos() multiplies by the render scale, so divide here
		Platform_SetMousePos( (int)( x / sx ), (int)( y / sy ));
	}

	cof_pad.warp_x = x;
	cof_pad.warp_y = y;

	VGui_MouseMove( x, y );
}

static void CL_CoF_VCursorEngage( void )
{
	int x, y;

	if( cof_pad.engaged )
		return;

	Platform_GetMousePos( &x, &y );
	cof_pad.x = x;
	cof_pad.y = y;
	cof_pad.warp_x = x;
	cof_pad.warp_y = y;
	cof_pad.real_x = x;
	cof_pad.real_y = y;
	cof_pad.hold = 0.0f;
	cof_pad.engaged = true;
	CL_CoF_ApplyOSCursor( );

	CL_CoF_PadTrace( "virtual cursor engaged at %d %d\n", x, y );
}

/*
============
CL_CoF_VCursorEngagePad

The gamepad is the last input device and a panel is open: the gamepad cursor
takes the pointer. On the panel's first engagement it starts in the middle of
the screen, where every CoF panel is (the OS pointer was wherever the last
menu left it); later (the mouse had it in between) where the pointer is.
============
*/
static void CL_CoF_VCursorEngagePad( void )
{
	if( cof_pad.engaged )
		return;

	CL_CoF_VCursorEngage( );

	if( !cof_pad.centre_pending )
		return;

	cof_pad.centre_pending = false;
	cof_pad.x = refState.width * 0.5f;
	cof_pad.y = refState.height * 0.5f;
	CL_CoF_VCursorApply( );
	CL_CoF_PadTrace( "virtual cursor centred at %d %d (a panel opened with the gamepad as the last input)\n", (int)cof_pad.x, (int)cof_pad.y );
}

static void CL_CoF_VCursorRelease( const char *why )
{
	if( !cof_pad.engaged )
		return;

	cof_pad.engaged = false;
	CL_CoF_ApplyOSCursor( );
	CL_CoF_PadTrace( "virtual cursor released (%s) at %d %d\n", why, (int)cof_pad.x, (int)cof_pad.y );
}

/*
============
CL_CoF_VCursorCoverage

The ring cursor, drawn in code (the thin-ring / ticked-dial look the user
asked for): an outer ring, a ring of 48 radial ticks, a centre dot. x, y are
relative to the centre, s is the diameter, grow widens every shape (the dark
halo that keeps the white cursor visible on white paper). 1 inside, 0 outside.
============
*/
static float CL_CoF_VCursorCoverage( float x, float y, float s, float grow )
{
	const float r = sqrt( x * x + y * y );
	const float ring = 0.46f * s, ring_hw = Q_max( 0.6f, 0.012f * s );
	const float tick_in = 0.355f * s, tick_out = 0.415f * s, tick_hw = Q_max( 0.45f, 0.0065f * s );
	const float dot = Q_max( 1.2f, 0.035f * s );

	if( fabs( r - ring ) <= ring_hw + grow )
		return 1.0f;

	if( r <= dot + grow )
		return 1.0f;

	if( r >= tick_in - grow && r <= tick_out + grow )
	{
		const float step = M_PI2_F / 48.0f;
		float a = atan2( y, x );
		float k = floor( a / step + 0.5f ) * step;

		if( fabs( r * sin( a - k )) <= tick_hw + grow )
			return 1.0f;
	}

	return 0.0f;
}

/*
============
CL_CoF_VCursorTexture

Generated at the exact size it is drawn at, so the thin lines stay crisp;
remade when the size changes (UI scale, cof_vcursor_size) or the renderer
lost it (video restart).
============
*/
static int CL_CoF_VCursorTexture( int size )
{
	static int last_size;
	char       name[32];
	int        tex, grid = size + 4;
	float      grow = Q_max( 1.0f, 0.02f * size );
	byte       *buf;

	Q_snprintf( name, sizeof( name ), "*cof_vcursor%d", size );
	tex = ref.dllFuncs.GL_FindTexture( name );
	if( tex )
		return tex;

	if( last_size && last_size != size )
	{
		char old[32];
		int  oldtex;

		Q_snprintf( old, sizeof( old ), "*cof_vcursor%d", last_size );
		if(( oldtex = ref.dllFuncs.GL_FindTexture( old )) != 0 )
			ref.dllFuncs.GL_FreeTexture( oldtex );
	}

	buf = Z_Malloc( grid * grid * 4 );

	for( int py = 0; py < grid; py++ )
	{
		for( int px = 0; px < grid; px++ )
		{
			float a = 0.0f, h = 0.0f, out;
			byte  *p = buf + ( py * grid + px ) * 4;

			// 4x4 supersampling
			for( int sy = 0; sy < 4; sy++ )
			{
				for( int sx = 0; sx < 4; sx++ )
				{
					float fx = px + ( sx + 0.5f ) * 0.25f - grid * 0.5f;
					float fy = py + ( sy + 0.5f ) * 0.25f - grid * 0.5f;

					a += CL_CoF_VCursorCoverage( fx, fy, size, 0.0f );
					h += CL_CoF_VCursorCoverage( fx, fy, size, grow );
				}
			}

			a /= 16.0f;
			h = h / 16.0f * 0.45f; // the halo is half-transparent black
			out = a + ( 1.0f - a ) * h;

			p[0] = p[1] = p[2] = out > 0.0f ? (byte)( 255.0f * a / out ) : 0;
			p[3] = (byte)( 255.0f * out );
		}
	}

	{
		rgbdata_t pic =
		{
			.width = grid,
			.height = grid,
			.type = PF_RGBA_32,
			.size = grid * grid * 4,
			.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
			.buffer = buf,
		};

		tex = GL_LoadTextureInternal( name, &pic, TF_IMAGE );
	}

	Z_Free( buf );
	last_size = size;
	return tex;
}

/*
============
CL_CoF_VCursorArt

The cursor art (gfx/shell/gamepad/cursor.png, the user's white square frame
with an inner line, a centre dot and a soft glow; 128x128 RGBA) as a
mipmapped texture, and next to it a black shadow made from the same picture
(its alpha spread by a few pixels and softened), which is what keeps a white
cursor visible on the white note pages. Both are made once; after a video
restart the renderer has lost them and they are made again. 0 when the file
is missing: the drawn ring of the first round is used then.
============
*/
#define COF_VCURSOR_ART "gfx/shell/gamepad/cursor.png"

static int CL_CoF_VCursorArt( int *shadow )
{
	rgbdata_t *pic;
	byte      *buf;
	int        w, h, r;

	*shadow = 0;

	if( cof_pad.art_tex && ref.dllFuncs.GL_FindTexture( "*cof_vcursor_art" ) == cof_pad.art_tex )
	{
		*shadow = cof_pad.shadow_tex;
		return cof_pad.art_tex;
	}

	cof_pad.art_tex = cof_pad.shadow_tex = 0;

	if( cof_pad.art_missing )
		return 0;

	pic = FS_LoadImage( COF_VCURSOR_ART, NULL, 0 );

	if( !pic || pic->type != PF_RGBA_32 || pic->width < 8 || pic->height < 8 || pic->width > 1024 || pic->height > 1024 )
	{
		Con_Printf( S_WARN "Cry of Fear gamepad: %s %s, the drawn ring cursor is used\n", COF_VCURSOR_ART, pic ? "is not an RGBA picture" : "not found" );
		if( pic )
			FS_FreeImage( pic );
		cof_pad.art_missing = true;
		return 0;
	}

	w = pic->width;
	h = pic->height;

	{
		// Mipmap upload may overwrite its input in place. Keep the decoded
		// alpha intact: it is still needed below to build the cursor shadow.
		byte *upload = Z_Malloc( w * h * 4 );
		memcpy( upload, pic->buffer, w * h * 4 );
		rgbdata_t art =
		{
			.width = w,
			.height = h,
			.type = PF_RGBA_32,
			.size = w * h * 4,
			.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
			.buffer = upload,
		};

		cof_pad.art_tex = GL_LoadTextureInternal( "*cof_vcursor_art", &art, TF_CLAMP );
		Z_Free( upload );
	}

	// the shadow: alpha spread over a disc of r pixels, then a 3x3 box blur
	r = Q_max( 2, w / 40 );
	buf = Z_Calloc( w * h * 4 );

	for( int y = 0; y < h; y++ )
	{
		for( int x = 0; x < w; x++ )
		{
			int a = 0;

			for( int dy = -r; dy <= r; dy++ )
			{
				for( int dx = -r; dx <= r; dx++ )
				{
					int sx = x + dx, sy = y + dy;

					if( dx * dx + dy * dy > r * r || sx < 0 || sy < 0 || sx >= w || sy >= h )
						continue;

					a = Q_max( a, pic->buffer[( sy * w + sx ) * 4 + 3] );
				}
			}

			buf[( y * w + x ) * 4 + 3] = a;
		}
	}

	for( int pass = 0; pass < 2; pass++ )
	{
		for( int y = 0; y < h; y++ )
		{
			for( int x = 0; x < w; x++ )
			{
				int sum = 0, n = 0;

				for( int dy = -1; dy <= 1; dy++ )
				{
					for( int dx = -1; dx <= 1; dx++ )
					{
						int sx = x + dx, sy = y + dy;

						if( sx < 0 || sy < 0 || sx >= w || sy >= h )
							continue;

						sum += buf[( sy * w + sx ) * 4 + 3];
						n++;
					}
				}

				// the colour bytes are free until the end: park the result there
				buf[( y * w + x ) * 4 + ( pass ? 0 : 1 )] = sum / n;
			}
		}

		for( int i = 0; i < w * h; i++ )
			buf[i * 4 + 3] = buf[i * 4 + ( pass ? 0 : 1 )];
	}

	for( int i = 0; i < w * h; i++ )
		buf[i * 4 + 0] = buf[i * 4 + 1] = buf[i * 4 + 2] = 0;

	{
		rgbdata_t sh =
		{
			.width = w,
			.height = h,
			.type = PF_RGBA_32,
			.size = w * h * 4,
			.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA,
			.buffer = buf,
		};

		cof_pad.shadow_tex = GL_LoadTextureInternal( "*cof_vcursor_shadow", &sh, TF_CLAMP );
	}

	Z_Free( buf );
	FS_FreeImage( pic );

	CL_CoF_PadTrace( "cursor art %s loaded (%dx%d), shadow radius %d\n", COF_VCURSOR_ART, w, h, r );

	*shadow = cof_pad.shadow_tex;
	return cof_pad.art_tex;
}

/*
============
CL_CoF_VCursorDraw

vgui_draw.c VGui_Paint, after the client's panels: the gamepad cursor at the
gamepad position, only while the gamepad owns the pointer (a panel opened with
the gamepad as the last input device gets it on its first frame). Centred on
the position, size cof_vcursor_size times the in-game UI scale (cof_ui_scale
/ HUD scale), opacity cof_vcursor_alpha, the dark shadow under it at
cof_vcursor_shadow; it shrinks a little while the click key is held.
============
*/
void CL_CoF_VCursorDraw( void )
{
	static convar_t *osk_over_game;
	float scale, draw;
	int   size, tex, shadow = 0, alpha;
	qboolean art;

	// only over the game itself: never over a menu window, such as the
	// on-screen keyboard (cof-osk-engine), which keeps the game's panels
	// painted under it (its cvar is looked up, so this file does not depend on
	// that patch being in the tree)
	if( cls.key_dest != key_game )
		return;

	if( !osk_over_game )
		osk_over_game = Cvar_FindVar( "cof_osk_over_game" );

	if( osk_over_game && osk_over_game->value )
		return;

	if( !cof_pad.engaged && CL_CoF_DeviceFromCvar( ) == COF_INPUT_PAD && cof_vcursor.value && Joy_IsActive( ) && CL_CoF_PadGateActive( ))
	{
		if( !cof_pad.panel_open )
			cof_pad.centre_pending = true; // the panel's first frame, before the input frame saw it
		CL_CoF_VCursorEngagePad( );
	}

	if( !cof_pad.engaged || !CL_CoF_PadGateActive( ))
		return;

	scale = CL_CoF_UIScale( );
	if( scale <= 0.0f )
		scale = 1.0f;

	size = bound( 12, (int)( cof_vcursor_size.value * scale + 0.5f ), 256 );

	art = ( tex = CL_CoF_VCursorArt( &shadow )) != 0;

	if( !art && ( tex = CL_CoF_VCursorTexture( size )) == 0 )
		return;

	draw = art ? size : size + 4; // the drawn ring has a 2 px margin
	for( int i = 0; i < ARRAYSIZE( cof_pad.captured ); i++ )
	{
		if( cof_pad.captured[i] == K_MOUSE1 )
		{
			draw *= 0.85f; // pressed
			break;
		}
	}

	alpha = (int)( bound( 0.0f, cof_vcursor_alpha.value, 1.0f ) * 255.0f );

	ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );

	if( shadow && cof_vcursor_shadow.value > 0.0f )
	{
		ref.dllFuncs.Color4ub( 255, 255, 255, (int)( alpha * bound( 0.0f, cof_vcursor_shadow.value, 1.0f )));
		ref.dllFuncs.R_DrawStretchPic( cof_pad.x - draw * 0.5f, cof_pad.y - draw * 0.5f, draw, draw, 0, 0, 1, 1, shadow );
	}

	ref.dllFuncs.Color4ub( 255, 255, 255, alpha );
	ref.dllFuncs.R_DrawStretchPic( cof_pad.x - draw * 0.5f, cof_pad.y - draw * 0.5f, draw, draw, 0, 0, 1, 1, tex );
	ref.dllFuncs.GL_SetRenderMode( kRenderNormal );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

/*
============
CL_CoF_VCursorPos

vgui_draw.c VGUI_GetMousePos: while the gamepad owns the pointer FreeVGUI's
per-frame cursor poll reads this position instead of SDL's, so it never
depends on when SDL catches up with a warp.
============
*/
qboolean CL_CoF_VCursorPos( int *x, int *y )
{
	if( !cof_pad.engaged )
		return false;

	*x = (int)cof_pad.x;
	*y = (int)cof_pad.y;
	return true;
}

/*
============
CL_CoF_VCursorClick

Press or release a mouse button at the virtual cursor, through the same
VGui_MouseEvent the real mouse uses in key_game (input.c IN_MouseEvent). The
client's own IN_MouseEvent is not called: with a panel open it ignores
buttons anyway (g_iVisibleMouse).
============
*/
static void CL_CoF_VCursorClick( int button, qboolean down )
{
	if( down )
	{
		// click where the stick left the cursor, or where the mouse is
		if( cof_pad.engaged )
			VGui_MouseMove( (int)cof_pad.x, (int)cof_pad.y );
	}

	VGui_MouseEvent( button, down ? 1 : 0 );
}

/*
============
CL_CoF_PadKeyEvent

in_keys.c Key_Event, before the client sees the key. Returns true when the
key was consumed here.
============
*/
qboolean CL_CoF_PadKeyEvent( int key, int down )
{
	int click, rclick, close;

	if( key < K_JOY1 || key > K_AUX32 )
		return false;

	if( !down )
	{
		int what = cof_pad.captured[key];

		if( !what )
			return false;

		cof_pad.captured[key] = 0;

		if( what == K_MOUSE1 || what == K_MOUSE2 )
		{
			char info[256];

			CL_CoF_VCursorClick( what, false );
			CL_CoF_PadTrace( "%s up: %s released\n", Key_KeynumToString( key ), what == K_MOUSE1 ? "left button" : "right button" );

			// a tape recorder slot acts on a double click: A on a slot is a
			// complete pick (the support library adds the second click; the
			// mouse keeps the page's own behaviour)
			if( what == K_MOUSE1 && VGui_CoF_PanelAction( COF_VGUI_PANEL_CONFIRM, info, sizeof( info )))
				CL_CoF_PadTrace( "%s up: %s\n", Key_KeynumToString( key ), info );
		}

		return true;
	}

	// a gamepad key (the SDL event already said so; this covers cof_key_probe)
	cof_pad.stat_pad_keys++;
	if( cof_input_trace.value )
		CL_CoF_PadTrace( "%s down (binding \"%s\")\n", Key_KeynumToString( key ), Key_GetBinding( key ) ? Key_GetBinding( key ) : "" );
	CL_CoF_NoteInput( COF_INPUT_PAD, "gamepad key" );

	if( cof_pad.captured[key] ) // a press already captured and not released: ignore repeats
		return true;

	// joy_enable 0 switches the gamepad off; its buttons then keep their plain binds
	if( !Joy_IsActive( ) || !CL_CoF_PadGateActive( ))
		return false;

	click = cof_vcursor.value ? CL_CoF_PadKeyFromCvar( &cof_vcursor_click_key ) : -1;
	rclick = cof_vcursor.value ? CL_CoF_PadKeyFromCvar( &cof_vcursor_rclick_key ) : -1;
	close = CL_CoF_PadKeyFromCvar( &cof_pad_close_key );

	if( key == click || key == rclick )
	{
		int button = key == click ? K_MOUSE1 : K_MOUSE2;

		cof_pad.captured[key] = button;
		CL_CoF_VCursorClick( button, true );
		CL_CoF_PadTrace( "%s down on an open panel: %s pressed at %d %d\n", Key_KeynumToString( key ),
			button == K_MOUSE1 ? "left button" : "right button", (int)cof_pad.x, (int)cof_pad.y );
		return true;
	}

	if( key == close )
	{
		char info[256];
		int  handled;

		cof_pad.captured[key] = -1;

		// the open panel's own Back / Close / Cancel / No button, clicked by
		// the support library (dialogs first: YES/NO, the save confirm box)
		if( VGui_CoF_PanelAction( COF_VGUI_PANEL_BACK, info, sizeof( info )))
		{
			CL_CoF_PadTrace( "%s down on an open panel: back - %s\n", Key_KeynumToString( key ), info );
			return true;
		}

		if( FBitSet( cof_pad.panel_classes, COF_PANEL_INVENTORY ) || !cof_pad.panel_classes )
		{
			// the inventory's key handler closes the inventory for exactly this
			// binding string (also what the arrow-cursor fallback gets first)
			handled = clgame.dllFuncs.pfnKey_Event( true, key, "+inventory" );
			CL_CoF_PadTrace( "%s down on an open panel: %s%soffered to the client as \"+inventory\" (client returned %d)\n",
				Key_KeynumToString( key ), info, info[0] ? "; " : "", handled );

			if( handled || FBitSet( cof_pad.panel_classes, COF_PANEL_INVENTORY ))
				return true;
		}

		// no back button known: the game's own Escape handling for its panel,
		// what retail did with Escape (the input gate now sends Escape to the
		// engine menu instead)
		handled = clgame.dllFuncs.pfnKey_Event( true, K_ESCAPE, "cancelselect" );
		clgame.dllFuncs.pfnKey_Event( false, K_ESCAPE, "cancelselect" );
		CL_CoF_PadTrace( "%s down on an open panel: %s%soffered to the client as ESCAPE / \"cancelselect\" (client returned %d)\n",
			Key_KeynumToString( key ), info, info[0] ? "; " : "", handled );
		return true;
	}

	return false;
}

/*
============
CL_CoF_PadDefaults
============
*/
static qboolean CL_CoF_PadAnyBound( void )
{
	for( int key = K_JOY1; key <= K_AUX30; key++ )
	{
		const char *b = Key_GetBinding( key );

		if( COM_StringEmptyOrNULL( b ))
			continue;

		// what "unbindall" itself leaves on START
		if( key == K_START_BUTTON && !Q_stricmp( b, "cancelselect" ))
			continue;

		return true;
	}

	return false;
}

static int CL_CoF_PadApplyLayout( qboolean force )
{
	int n = 0;

	for( int i = 0; i < ARRAYSIZE( cof_pad_layout ); i++ )
	{
		const char *b = Key_GetBinding( cof_pad_layout[i].key );

		if( !force && !COM_StringEmptyOrNULL( b ))
			continue;

		Key_SetBinding( cof_pad_layout[i].key, cof_pad_layout[i].binding );
		n++;
	}

	return n;
}

// Only the engine stick path calls this helper. The client tests its cvar's
// numeric value synchronously in the directional command handler. Keep the
// player's string, flags and saved setting intact; never persist an override.
void CL_CoF_PadMoveCommand( const char *command )
{
	convar_t *guard = cof_pad_no_doubletap.value ? Cvar_FindVar( "cl_nodoubletapdodge" ) : NULL;
	float saved = guard ? guard->value : 0.0f;

	if( guard ) guard->value = 1.0f;
	Cmd_ExecuteString( command );
	if( guard ) guard->value = saved;
}

static const char *CL_CoF_PadLayoutBind( const cof_pad_bind_t *layout, int count, int key )
{
	for( int i = 0; i < count; i++ )
	{
		if( layout[i].key == key )
			return layout[i].binding;
	}

	return NULL;
}

/*
============
CL_CoF_PadMigrate

Every pad key that still holds exactly the default of the profile's older
generation moves to the current layout; a key the player rebound or cleared
keeps what it has.
============
*/
static void CL_CoF_PadMigrate( const cof_pad_bind_t *old, int count, int *moved, int *kept )
{
	*moved = *kept = 0;

	for( int i = 0; i < count; i++ )
	{
		int        key = old[i].key;
		const char *cur = Key_GetBinding( key );
		const char *want = CL_CoF_PadLayoutBind( cof_pad_layout, ARRAYSIZE( cof_pad_layout ), key );

		if( !cur || Q_stricmp( cur, old[i].binding ))
		{
			( *kept )++;
			continue;
		}

		if( !want )
			want = "";

		if( Q_stricmp( cur, want ))
		{
			Key_SetBinding( key, want );
			( *moved )++;
		}
	}
}

static void CL_CoF_PadDefaultsCheck( void )
{
	int      gen = (int)cof_pad_defaults_gen.value;

	if( gen >= COF_PAD_DEFAULTS_GENERATION )
		return;

	// Preserve saved gyro settings on upgrade. Fresh profiles use the cvar default.

	if( gen == 0 && !CL_CoF_PadAnyBound( ))
	{
		int n = CL_CoF_PadApplyLayout( false );
		Q_snprintf( cof_pad.defaults_note, sizeof( cof_pad.defaults_note ), "default layout applied (%d binds, generation %d)", n, COF_PAD_DEFAULTS_GENERATION );
	}
	else
	{
		int moved = 0, kept = 0, builtin = 0, unchanged = 0;

		if( gen == 1 )
			CL_CoF_PadMigrate( cof_pad_layout_gen1, ARRAYSIZE( cof_pad_layout_gen1 ), &moved, &kept );
		else if( gen == 2 )
			CL_CoF_PadMigrate( cof_pad_layout_gen2, ARRAYSIZE( cof_pad_layout_gen2 ), &moved, &kept );
		else if( gen == 3 )
			CL_CoF_PadMigrate( cof_pad_layout_gen3, ARRAYSIZE( cof_pad_layout_gen3 ), &moved, &kept );
		else if( gen == 4 )
			CL_CoF_PadMigrate( cof_pad_layout_gen4, ARRAYSIZE( cof_pad_layout_gen4 ), &moved, &kept );

		// 1.2.3: the engine's Half-Life pad binds the profile kept (cof_pad_layout_builtin)
		CL_CoF_PadMigrate( cof_pad_layout_builtin, ARRAYSIZE( cof_pad_layout_builtin ), &builtin, &unchanged );

		if( gen >= 1 && gen <= 4 )
			Q_snprintf( cof_pad.defaults_note, sizeof( cof_pad.defaults_note ), "generation %d layout migrated to generation %d (%d binds changed, %d customised keys kept, %d Half-Life pad binds moved)", gen, COF_PAD_DEFAULTS_GENERATION, moved, kept, builtin );
		else if( builtin )
			Q_snprintf( cof_pad.defaults_note, sizeof( cof_pad.defaults_note ), "%d of the engine's Half-Life pad binds moved to the Cry of Fear layout (generation %d)", builtin, COF_PAD_DEFAULTS_GENERATION );
		else if( gen == 5 )
			Q_snprintf( cof_pad.defaults_note, sizeof( cof_pad.defaults_note ), "generation 5 layout kept as generation %d", COF_PAD_DEFAULTS_GENERATION );
		else Q_snprintf( cof_pad.defaults_note, sizeof( cof_pad.defaults_note ), "gamepad keys already bound, default layout not applied (generation %d)", COF_PAD_DEFAULTS_GENERATION );
	}

	Con_Printf( "Cry of Fear gamepad: %s\n", cof_pad.defaults_note );

	Cvar_DirectSetValue( &cof_pad_defaults_gen, COF_PAD_DEFAULTS_GENERATION );
}

/*
============
CL_CoF_DuckToggle_f / CL_CoF_DuckRelease

cof_duck_toggle (B's default bind): press once to crouch, again to stand. The
latch holds the game's own +duck through the command buffer exactly like a
held key (no keynum: the client treats it as a console press, and its -duck
releases every holder); in play only. Released on its own at every level
change and load (the server count changes, or the client leaves ca_active),
so a new map never starts crouched.
============
*/
static void CL_CoF_DuckRelease( const char *why )
{
	if( !cof_pad.duck_on )
		return;

	cof_pad.duck_on = false;

	// at the head of the buffer like the +duck (and a key's own +/-): a
	// release appended behind a long cfg would wait for the whole cfg
	if( clgame.hInstance )
		Cbuf_InsertText( "-duck\n" );

	CL_CoF_PadTrace( "crouch toggle off (%s)\n", why );
}

static void CL_CoF_DuckToggle_f( void )
{
	if( cls.state != ca_active || cls.key_dest != key_game || cl.background )
	{
		CL_CoF_PadTrace( "crouch toggle ignored: not in play\n" );
		return;
	}

	if( cof_pad.duck_on )
	{
		CL_CoF_DuckRelease( "toggled" );
		return;
	}

	cof_pad.duck_on = true;
	cof_pad.duck_servercount = cl.servercount;
	Cbuf_InsertText( "+duck\n" );
	CL_CoF_PadTrace( "crouch toggle on\n" );
}

/*
============
cof_sprint_toggle_press (L3's default bind) and the sprint latch

cof_sprint_toggle 1 (toggle, the default): the first press holds the game's
own +sprint (queued at the head of the command buffer without a key number,
like a held key; -sprint releases every holder), a second press lets go, and
it lets go by itself when the player stops moving: the left stick below
cof_sprint_stop_threshold with no movement key held for cof_sprint_stop_time
seconds (and at least 3 frames), or, when the press came while standing
still, when no movement starts within 0.75 s. cof_sprint_toggle 0 (hold):
+sprint while a key bound to this command is held, as L3 did before. Both
let go when a panel or a menu opens, and at every load and level change. In
play only. The keyboard keeps its plain +sprint unless this command is bound.
============
*/
static void CL_CoF_SprintRelease( const char *why )
{
	if( !cof_pad.sprint_on )
		return;

	cof_pad.sprint_on = false;

	if( clgame.hInstance )
		Cbuf_InsertText( "-sprint\n" );

	CL_CoF_PadTrace( "sprint off (%s)\n", why );
}

static qboolean CL_CoF_SprintKeyHeld( void )
{
	for( int key = 0; key < 256; key++ )
	{
		const char *b = Key_GetBinding( key );

		if( b && !Q_stricmp( b, "cof_sprint_toggle_press" ) && Key_IsDown( key ))
			return true;
	}

	return false;
}

static qboolean CL_CoF_PlayerMoving( float *stick )
{
	float fwd = CL_CoF_PadAxis( JOY_AXIS_FWD ), side = CL_CoF_PadAxis( JOY_AXIS_SIDE );

	*stick = sqrt( fwd * fwd + side * side );

	if( *stick >= bound( 0.05f, cof_sprint_stop_threshold.value, 0.9f ))
		return true;

	for( int key = 0; key < 256; key++ )
	{
		const char *b;

		if( !Key_IsDown( key ) || ( b = Key_GetBinding( key )) == NULL )
			continue;

		if( !Q_strnicmp( b, "+forward", 8 ) || !Q_strnicmp( b, "+back", 5 ) || !Q_strnicmp( b, "+moveleft", 9 ) || !Q_strnicmp( b, "+moveright", 10 ))
			return true;
	}

	return false;
}

static void CL_CoF_SprintPress_f( void )
{
	float stick;

	if( cls.state != ca_active || cls.key_dest != key_game || cl.background )
	{
		CL_CoF_PadTrace( "sprint press ignored: not in play\n" );
		return;
	}

	if( cof_pad.sprint_on )
	{
		CL_CoF_SprintRelease( "pressed again" );
		return;
	}

	cof_pad.sprint_on = true;
	cof_pad.sprint_hold = !cof_sprint_toggle.value && CL_CoF_SprintKeyHeld( );
	cof_pad.sprint_moved = CL_CoF_PlayerMoving( &stick );
	cof_pad.sprint_servercount = cl.servercount;
	cof_pad.sprint_since = cof_pad.sprint_still_since = host.realtime;
	cof_pad.sprint_still_frames = 0;
	Cbuf_InsertText( "+sprint\n" );
	CL_CoF_PadTrace( "sprint on (%s, stick %.2f)\n", cof_pad.sprint_hold ? "hold" : "toggle", stick );
}

static void CL_CoF_SprintFrame( void )
{
	float stick;

	if( !cof_pad.sprint_on )
		return;

	if( cls.state != ca_active )
	{
		CL_CoF_SprintRelease( "left the game / loading" );
		return;
	}

	if( cl.servercount != cof_pad.sprint_servercount )
	{
		CL_CoF_SprintRelease( "level change" );
		return;
	}

	if( cls.key_dest != key_game || CL_CoF_PadGateActive( ))
	{
		CL_CoF_SprintRelease( "menu or panel opened" );
		return;
	}

	if( cof_pad.sprint_hold )
	{
		if( !CL_CoF_SprintKeyHeld( ))
			CL_CoF_SprintRelease( "key released" );
		return;
	}

	if( CL_CoF_PlayerMoving( &stick ))
	{
		cof_pad.sprint_moved = true;
		cof_pad.sprint_still_since = host.realtime;
		cof_pad.sprint_still_frames = 0;
		return;
	}

	cof_pad.sprint_still_frames++;

	if( cof_pad.sprint_moved )
	{
		if( host.realtime - cof_pad.sprint_still_since >= Q_max( 0.0f, cof_sprint_stop_time.value ) && cof_pad.sprint_still_frames >= 3 )
			CL_CoF_SprintRelease( va( "stopped moving, stick %.2f", stick ));
	}
	else if( host.realtime - cof_pad.sprint_since >= 0.75 )
		CL_CoF_SprintRelease( "no movement after the press" );
}

static void CL_CoF_PadDefaults_f( void )
{
	const char *arg = Cmd_Argc( ) > 1 ? Cmd_Argv( 1 ) : "";

	if( !Q_stricmp( arg, "list" ))
	{
		for( int i = 0; i < ARRAYSIZE( cof_pad_layout ); i++ )
		{
			const char *b = Key_GetBinding( cof_pad_layout[i].key );
			Con_Printf( "%-12s default \"%s\", now \"%s\"\n", Key_KeynumToString( cof_pad_layout[i].key ),
				cof_pad_layout[i].binding, b ? b : "" );
		}
		return;
	}

	if( arg[0] && Q_stricmp( arg, "force" ))
	{
		Con_Printf( S_USAGE "cof_pad_defaults [force|list] : bind the Cry of Fear gamepad layout to unbound pad keys (force: to all of its keys)\n" );
		return;
	}

	Con_Printf( "Cry of Fear gamepad: %d binds set\n", CL_CoF_PadApplyLayout( !Q_stricmp( arg, "force" )));
	Cvar_DirectSetValue( &cof_pad_defaults_gen, COF_PAD_DEFAULTS_GENERATION );
}

/*
============
CL_CoF_PadLegacyJoystick

client.dll registers "joystick" (stock HL WinMM code, IN_ReadJoystick 1007BB20,
polled only while the cvar is set). The game's config.cfg ships "0"; keep it
there so an XInput pad is never read twice.
============
*/
static void CL_CoF_PadLegacyJoystick( void )
{
	static double next_check;
	convar_t *joystick;

	if( !cof_joy_legacy_off.value || !clgame.hInstance )
		return;

	// looked up afresh (the client's cvars go away with the client), twice a second
	if( host.realtime < next_check && host.realtime >= next_check - 1.0 )
		return;

	next_check = host.realtime + 0.5;
	joystick = Cvar_FindVar( "joystick" );

	if( !joystick || !joystick->value )
		return;

	Cvar_DirectSet( joystick, "0" );

	if( !cof_pad.legacy_reported )
	{
		cof_pad.legacy_reported = true;
		Con_Printf( "Cry of Fear gamepad: the game's own joystick code was switched off (joystick 0); the gamepad is read by the engine (cof_joy_legacy_off 0 allows it)\n" );
	}
}

/*
============
CL_CoF_PadFrame

Host_InputFrame, before IN_MouseMove. Returns true while the virtual cursor
owns the pointer, so the real mouse position is not pushed over it.
============
*/
qboolean CL_CoF_PadFrame( void )
{
	qboolean open;
	float    lx, ly, rx, ry, sx, sy, m, dz, dt;
	int      real_x, real_y;

	CL_CoF_MeasureFrame( );

	if( !cof_pad.defaults_checked && host.config_executed )
	{
		cof_pad.defaults_checked = true;
		CL_CoF_PadDefaultsCheck( );
	}

	CL_CoF_PadStyleFrame( );

	CL_CoF_PadLegacyJoystick( );

	if( cof_pad.duck_on && ( cls.state != ca_active || cl.servercount != cof_pad.duck_servercount ))
		CL_CoF_DuckRelease( cls.state != ca_active ? "left the game / loading" : "level change" );

	CL_CoF_SprintFrame( );

	// the last input device may also have been set from outside (the menu
	// marks its gamepad keys, a test cfg sets the cvar)
	if( cof_pad.device != CL_CoF_DeviceFromCvar( ))
	{
		cof_pad.device = CL_CoF_DeviceFromCvar( );
		CL_CoF_PadTrace( "last input: %s (cvar)\n", cof_input_names[cof_pad.device] );
	}

	open = CL_CoF_PadGateActive( );

	if( open != cof_pad.panel_open )
	{
		cof_pad.panel_open = open;
		cof_pad.move_suppressed = false;
		if( !cof_pad.engaged || !open )
			cof_pad.centre_pending = open;
		if( !open || !cof_pad.engaged || CL_CoF_DeviceFromCvar( ) != COF_INPUT_PAD )
			CL_CoF_VCursorRelease( open ? "panel opened" : "panel closed" );
		CL_CoF_PadTrace( "clickable panel %s (last input %s, frame %d)\n", open ? "OPEN" : "closed", cof_input_names[cof_pad.device], (int)host.framecount );
	}

	if( !open || !cof_vcursor.value )
	{
		CL_CoF_VCursorRelease( "gate off" );
		CL_CoF_ApplyOSCursor( );
		return false;
	}

	// joy_enable 0: the sticks move nothing, the mouse keeps the pointer
	if( !Joy_IsActive( ))
	{
		CL_CoF_VCursorRelease( "joy_enable 0" );
		CL_CoF_ApplyOSCursor( );
		return false;
	}

	// the gamepad is the last input device: the panel gets the gamepad cursor
	// right away; the mouse or the keyboard takes it back
	if( cof_pad.device == COF_INPUT_PAD )
		CL_CoF_VCursorEngagePad( );
	else
	{
		// the mouse had this panel: a later stick picks up where the arrow is
		cof_pad.centre_pending = false;
		if( cof_pad.engaged )
			CL_CoF_VCursorRelease( cof_pad.device == COF_INPUT_MOUSE ? "mouse moved" : "keyboard used" );
	}

	CL_CoF_ApplyOSCursor( );

	lx = CL_CoF_PadAxis( JOY_AXIS_SIDE );
	ly = CL_CoF_PadAxis( JOY_AXIS_FWD );
	rx = CL_CoF_PadAxis( JOY_AXIS_YAW );
	ry = CL_CoF_PadAxis( JOY_AXIS_PITCH );

	// either stick moves the cursor, the one pushed further wins
	if( lx * lx + ly * ly >= rx * rx + ry * ry )
		sx = lx, sy = ly;
	else sx = rx, sy = ry;

	m = sqrt( sx * sx + sy * sy );
	dz = bound( 0.0f, cof_vcursor_deadzone.value, 0.9f );
	dt = bound( 0.0f, (float)host.realframetime, 0.1f );

	Platform_GetMousePos( &real_x, &real_y );

	if( m > dz )
	{
		float norm, speed, accel;

		CL_CoF_VCursorEngage( );

		norm = Q_min( ( m - dz ) / ( 1.0f - dz ), 1.0f );
		norm = pow( norm, Q_max( cof_vcursor_curve.value, 0.1f ));

		cof_pad.hold += dt;
		accel = 1.0f;
		if( cof_vcursor_accel.value > 0.0f )
		{
			float t = cof_vcursor_accel_time.value > 0.0f ? cof_pad.hold / cof_vcursor_accel_time.value : 1.0f;
			accel += cof_vcursor_accel.value * Q_min( t, 1.0f );
		}

		speed = cof_vcursor_speed.value * refState.height * norm * accel;
		cof_pad.x += sx / m * speed * dt;
		cof_pad.y += sy / m * speed * dt; // SDL stick y grows downwards, like the screen

		CL_CoF_VCursorApply( );
	}
	else
	{
		cof_pad.hold = 0.0f;

		// the real mouse moved on its own (not to where we warped it): the
		// mouse takes over again
		if( cof_pad.engaged && ( real_x != cof_pad.real_x || real_y != cof_pad.real_y )
			&& ( !cof_vcursor_warp.value || abs( real_x - cof_pad.warp_x ) > 2 || abs( real_y - cof_pad.warp_y ) > 2 ))
		{
			CL_CoF_VCursorRelease( "mouse moved" );
		}
	}

	cof_pad.real_x = real_x;
	cof_pad.real_y = real_y;

	if( cof_pad.engaged && cof_input_trace.value && host.realtime >= cof_pad.next_trace && m > dz )
	{
		cof_pad.next_trace = host.realtime + 0.25;
		CL_CoF_PadTrace( "virtual cursor %d %d (stick %.2f %.2f, speed x%.2f)\n", (int)cof_pad.x, (int)cof_pad.y, sx, sy,
			1.0f + cof_vcursor_accel.value * Q_min( cof_vcursor_accel_time.value > 0.0f ? cof_pad.hold / cof_vcursor_accel_time.value : 1.0f, 1.0f ));
	}

	return cof_pad.engaged;
}

/*
==============================================================================

Quick turn (cof_quickturn, R3's default bind)

A smooth 180-degree turn over cof_quickturn_time seconds (eased in and out),
added to the yaw of IN_EngineAppendMove - the same view-angle path the right
stick turns the player with, after the client built its command - so it never
fights the client's own view handling or the aim down sights. Ignored while a
panel or a menu is open, outside play, and while a turn is running; a level
change or a load drops a turn in progress. Bindable to any key.

==============================================================================
*/
static void CL_CoF_QuickTurn_f( void )
{
	if( cls.state != ca_active || cls.key_dest != key_game || cl.background || cl.paused || cl.intermission )
	{
		CL_CoF_PadTrace( "quick turn ignored: not in play\n" );
		return;
	}

	if( CL_CoF_ClickablePanelOpen( ))
	{
		CL_CoF_PadTrace( "quick turn ignored: a panel is open\n" );
		return;
	}

	if( cof_pad.turn_on )
	{
		CL_CoF_PadTrace( "quick turn ignored: already turning\n" );
		return;
	}

	cof_pad.turn_on = true;
	cof_pad.turn_t = 0.0;
	cof_pad.turn_done = 0.0f;
	cof_pad.turn_servercount = cl.servercount;
	cof_pad.turn_yaw0 = cl.viewangles[YAW];
	Con_Reportf( "[cof-pad] quick turn: start at yaw %.1f, %.2f s\n", cl.viewangles[YAW], bound( 0.05f, cof_quickturn_time.value, 1.0f ));
}

static float CL_CoF_Ease( float x )
{
	x = bound( 0.0f, x, 1.0f );
	return x * x * ( 3.0f - 2.0f * x );
}

/*
============
CL_CoF_QuickTurnYaw

input.c IN_EngineAppendMove, every command frame in play: the yaw this frame
adds to the turn (0 when no turn runs). The eased curve is sampled at the
accumulated time, so the frame deltas always add up to exactly 180 degrees.
============
*/
float CL_CoF_QuickTurnYaw( void )
{
	float T, t, delta;

	if( !cof_pad.turn_on )
		return 0.0f;

	if( cls.state != ca_active || cl.servercount != cof_pad.turn_servercount )
	{
		cof_pad.turn_on = false;
		Con_Reportf( "[cof-pad] quick turn dropped (level change / load)\n" );
		return 0.0f;
	}

	T = bound( 0.05f, cof_quickturn_time.value, 1.0f );
	t = cof_pad.turn_t + bound( 0.0f, (float)host.realframetime, 0.1f );
	delta = 180.0f * CL_CoF_Ease( t / T ) - cof_pad.turn_done;

	cof_pad.turn_t = t;
	cof_pad.turn_done += delta;

	if( t >= T )
	{
		cof_pad.turn_on = false;
		Con_Reportf( "[cof-pad] quick turn: done, %.1f degrees in %.3f s (yaw %.1f -> %.1f)\n", cof_pad.turn_done, t,
			cof_pad.turn_yaw0, anglemod( cl.viewangles[YAW] + delta ));
	}

	return delta;
}

/*
==============================================================================

Triggers: one key edge per real pull (cof_pad_trigger_hyst)

==============================================================================
*/
float CL_CoF_TriggerHysteresis( void )
{
	return bound( 0.0f, cof_pad_trigger_hyst.value, 0.4f );
}

void CL_CoF_NoteTriggerEdge( int axis, qboolean down, short value, int threshold )
{
	cof_pad.stat_trigger_edges++;
	CL_CoF_PadTrace( "%s %s at %d (threshold %d, hysteresis %.2f)\n", axis == JOY_AXIS_LT ? "LT" : "RT", down ? "pressed" : "released",
		(int)value, threshold, CL_CoF_TriggerHysteresis( ));
}

/*
==============================================================================

Stick movement shape (cof_pad_move_mode)

The engine starts with proportional left-stick input. Retail CoF halves the
forward command whenever both movement components are nonzero, then uses the
Euclidean wish-vector length and its game speed limits. Even tiny lateral
stick drift can therefore halve straight walking speed.

With cof_pad_move_fix enabled, mode 1 preserves the radial direction and
input.c calls CL_CoF_PadMoveCorrectCommand after composing the usercmd to cancel
that exact factor. Below cof_pad_move_walk magnitude follows the deadzone and
response curve; above it magnitude is one. The existing diagonal preference
remains available. Mode 2 retains the legacy square eight-direction mapping;
mode 0 and the opt-out retain their previous behavior.

==============================================================================
*/
qboolean CL_CoF_PadMoveShape( float *fw, float *side, float fwscale, float sidescale )
{
	const int mode = (int)cof_pad_move_mode.value;
	float     x, y, m, dz, walk, mag;

	cof_pad.move_saturated = false;
	cof_pad.move_digital_active = false;

	if( mode < 1 || mode > 2 )
		return false;

	x = (float)Joy_CoF_AxisRaw( JOY_AXIS_SIDE ) / (float)SHRT_MAX;
	y = -(float)Joy_CoF_AxisRaw( JOY_AXIS_FWD ) / (float)SHRT_MAX;
	m = sqrt( x * x + y * y );

	dz = bound( 0.0f, cof_pad_move_deadzone.value, 0.5f );
	walk = bound( dz + 0.05f, cof_pad_move_walk.value, 1.0f );

	if( m <= dz || m <= 0.0f )
		return true;

	cof_pad.move_digital_active = mode == 1 && cof_pad_move_fix.value != 0 && GI && !Q_stricmp( GI->gamefolder, "cryoffear" );
	x /= m;
	y /= m;

	if( mode == 2 )
	{
		float a = atan2( y, x );

		a = floor( a / ( M_PI_F * 0.25f ) + 0.5f ) * ( M_PI_F * 0.25f );
		x = cos( a );
		y = sin( a );
		if( fabs( x ) < 0.001f ) x = 0.0f;
		if( fabs( y ) < 0.001f ) y = 0.0f;
	}

	if( m >= walk )
	{
		mag = 1.0f;
		cof_pad.move_saturated = true;
	}
	else mag = pow(( m - dz ) / ( walk - dz ), Q_max( 0.2f, cof_pad_move_curve.value ));

	// Mode 1 keeps a continuous radial direction. The retail PM code halves
	// forward whenever both command axes are nonzero; input.c cancels that
	// exact factor after composing the command. Mode 2 retains its existing
	// square, eight-direction behavior. Keep the user's diagonal preference.
	{
		const float big = Q_max( fabs( x ), fabs( y ));
		const float ratio = big > 0.0f ? Q_min( fabs( x ), fabs( y )) / big : 0.0f;
		const float boost = bound( 1.0f, cof_pad_move_diagonal.value, 1.5f );
		const float k = 1.0f + ( boost - 1.0f ) * ratio;
		if(( mode == 2 || !cof_pad.move_digital_active ) && big > 0.0f ) { x /= big; y /= big; }
		x *= k;
		y *= k;
	}

	*fw += y * mag * fwscale;
	*side += x * mag * sidescale;
	return true;
}

// Retail PM_CheckParameters (client 10010DC0) halves forward for every
// mixed-axis command, even tiny stick drift. Invert only that factor for
// continuous Digital-style input. The game's speed cap remains authoritative.
void CL_CoF_PadMoveCorrectCommand( usercmd_t *cmd, float forward, float side )
{
    if( (!forward && !side) || !Joy_IsActive() || !cof_pad.move_digital_active || !cof_pad_move_fix.value ||
        (int)cof_pad_move_mode.value != 1 || !GI || Q_stricmp( GI->gamefolder, "cryoffear" )) return;
    // Reject non-finite components before arithmetic. Commands cannot validly
    // exceed the finite float range; division also avoids overflow on doubling.
    if( !(fabs( cmd->forwardmove ) <= FLT_MAX / 2.0f) || !(fabs( cmd->sidemove ) <= FLT_MAX)) return;
    // Wire components truncate to whole units. Make sub-unit zero locally too,
    // so prediction and server agree on the mixed-axis branch.
    if( fabs( cmd->forwardmove ) < 1.0f ) cmd->forwardmove = 0.0f;
    if( fabs( cmd->sidemove ) < 1.0f ) cmd->sidemove = 0.0f;
    if( !cmd->forwardmove || !cmd->sidemove ) return;
    // Retail's active sprint branch zeros side and does not halve forward.
    // 0x40000 is the CoF-specific player flag tested by that branch.
    if(( cmd->buttons & IN_RUN ) && clgame.pmove && ( clgame.pmove->flags & 0x40000 )) return;
    cmd->forwardmove *= 2.0f;
}

/*
============
CL_CoF_PadMoveKeyThresholds

input.c IN_JoyAppendMove: the deflections that press +forward / +back and
+moveleft / +moveright. Mode 0: the engine's 0.7 / 0.9; modes 1 and 2: the
direction's sector (0.38 = cos 67.5 degrees) once the stick counts as fully
pushed, no keys while walking slower.
============
*/
void CL_CoF_PadMoveKeyThresholds( float *forward, float *side )
{
	const int mode = (int)cof_pad_move_mode.value;

	if( mode < 1 || mode > 2 )
		return;

	if( cof_pad.move_saturated )
		*forward = *side = 0.38f;
	else *forward = *side = 1.5f;
}

/*
==============================================================================

Pad styles (cof_pad_style 0..3 = Xbox, PlayStation, Nintendo, Steam Deck)

The style only decides which button pictures and names the menus show
(gfx/shell/gamepad/icons.txt, style ids xbox / ps5 / switch / steamdeck). What
a button DOES is decided by its position: the engine key names are positions
(A_BUTTON = the bottom face button, B_BUTTON = right, X_BUTTON = left,
Y_BUTTON = top), and cof_pad_positional (default 1) makes SDL report a
Nintendo pad's face buttons by position too - SDL 2's default is by the
printed label, which would put the Nintendo "B" (bottom) on B_BUTTON. SDL's
own HIDAPI Switch driver (Pro Controller, Joy-Cons, pads in Switch mode such
as the 8BitDo Ultimate, which present themselves as a Pro Controller) follows
the hint at run time, so the menus, the on-screen keyboard and the panels,
which all read the engine keys, agree with each other without a remap of
their own.

==============================================================================
*/
enum
{
	COF_STYLE_XBOX = 0,
	COF_STYLE_PLAYSTATION,
	COF_STYLE_NINTENDO,
	COF_STYLE_STEAMDECK,
	COF_STYLE_COUNT
};

static const char *const cof_style_names[COF_STYLE_COUNT] = { "xbox", "playstation", "nintendo", "steamdeck" };
static const char *const cof_style_icon_ids[COF_STYLE_COUNT] = { "xbox", "ps5", "switch", "steamdeck" };

#if XASH_SDL == 2
static const char *CL_CoF_PadTypeName( int type )
{
	switch( type )
	{
	case SDL_CONTROLLER_TYPE_XBOX360: return "XBOX360";
	case SDL_CONTROLLER_TYPE_XBOXONE: return "XBOXONE";
	case SDL_CONTROLLER_TYPE_PS3: return "PS3";
	case SDL_CONTROLLER_TYPE_PS4: return "PS4";
	case SDL_CONTROLLER_TYPE_PS5: return "PS5";
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return "NINTENDO_SWITCH_PRO";
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "NINTENDO_SWITCH_JOYCON_LEFT";
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "NINTENDO_SWITCH_JOYCON_RIGHT";
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "NINTENDO_SWITCH_JOYCON_PAIR";
	case SDL_CONTROLLER_TYPE_VIRTUAL: return "VIRTUAL";
	case SDL_CONTROLLER_TYPE_AMAZON_LUNA: return "AMAZON_LUNA";
	case SDL_CONTROLLER_TYPE_GOOGLE_STADIA: return "GOOGLE_STADIA";
	case SDL_CONTROLLER_TYPE_NVIDIA_SHIELD: return "NVIDIA_SHIELD";
	}
	return "UNKNOWN";
}

/*
============
CL_CoF_StyleForPad

Which style fits a pad: SDL's controller type first, then the USB ids and the
name. The Steam Deck: its built-in controller ("Steam Deck", Valve 28DE:1205,
SDL's HIDAPI driver), or - in the Deck's game mode, where Steam sets
SteamDeck=1 in the game's environment - Steam Input's virtual gamepad (Valve
28DE), unless Steam tells SDL (SteamVirtualGamepadInfo) that a PlayStation or
Nintendo pad is behind it.
============
*/
static int CL_CoF_StyleForPad( int type, int vid, int pid, const char *name, qboolean deck_env, const char **why )
{
	if( name && Q_stristr( name, "Steam Deck" ))
		return *why = "name \"Steam Deck\"", COF_STYLE_STEAMDECK;

	if( vid == 0x28DE && pid == 0x1205 )
		return *why = "Valve 28DE:1205 (Steam Deck built-in controller)", COF_STYLE_STEAMDECK;

	switch( type )
	{
	case SDL_CONTROLLER_TYPE_PS3:
	case SDL_CONTROLLER_TYPE_PS4:
	case SDL_CONTROLLER_TYPE_PS5:
		return *why = "SDL type PlayStation", COF_STYLE_PLAYSTATION;
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
		return *why = "SDL type Nintendo Switch", COF_STYLE_NINTENDO;
	}

	if( vid == 0x057E )
		return *why = "Nintendo vendor id 057E", COF_STYLE_NINTENDO;

	if( name && ( Q_stristr( name, "Nintendo" ) || Q_stristr( name, "Joy-Con" ) || Q_stristr( name, "Switch Pro" )))
		return *why = "Nintendo name", COF_STYLE_NINTENDO;

	if( deck_env && vid == 0x28DE )
		return *why = "Steam Input virtual gamepad with SteamDeck=1", COF_STYLE_STEAMDECK;

	return *why = "default", COF_STYLE_XBOX;
}

static qboolean CL_CoF_DeckEnv( void )
{
	const char *s = getenv( "SteamDeck" );
	return s && s[0] == '1';
}
#endif // XASH_SDL == 2

/*
============
CL_CoF_PadActivated

joy_sdl2.c SDLash_SetActiveGameController: the pad that sent the last event
changed (or the first pad appeared). Only noted here; CL_CoF_PadStyleFrame
applies the style once the profile's config has run.
============
*/
void CL_CoF_PadActivated( void *gamecontroller )
{
	cof_pad.stat_pad_switches++;

#if XASH_SDL == 2
	{
		SDL_GameController *gc = (SDL_GameController *)gamecontroller;
		const char *why = "";
		int         type, vid, pid, style;
		const char  *name;

		if( gc )
		{
			type = SDL_GameControllerGetType( gc );
			vid = SDL_GameControllerGetVendor( gc );
			pid = SDL_GameControllerGetProduct( gc );
			name = SDL_GameControllerName( gc );
		}
		else
		{
			// the pad in use went away: the first other game controller
			// still connected stands for it until one sends an event
			int i, n = SDL_WasInit( SDL_INIT_GAMECONTROLLER ) ? SDL_NumJoysticks( ) : 0;

			for( i = 0; i < n && !SDL_IsGameController( i ); i++ );

			if( i >= n )
			{
				cof_pad.style_detected = -1;
				cof_pad.pad_name[0] = 0;
				Cvar_FullSet( cof_pad_type.name, "none", cof_pad_type.flags );
				Con_Reportf( "[cof-pad] no gamepad in use\n" );
				return;
			}

			type = SDL_GameControllerTypeForIndex( i );
			vid = SDL_JoystickGetDeviceVendor( i );
			pid = SDL_JoystickGetDeviceProduct( i );
			name = SDL_GameControllerNameForIndex( i );
		}
		style = CL_CoF_StyleForPad( type, vid, pid, name, CL_CoF_DeckEnv( ), &why );

		if( style != cof_pad.style_detected || Q_strcmp( cof_pad.pad_name, name ? name : "" ))
		{
			Con_Reportf( "[cof-pad] gamepad in use: \"%s\" (SDL type %s, %04X:%04X): style %s (%s)\n", name ? name : "?",
				CL_CoF_PadTypeName( type ), vid, pid, cof_style_names[style], why );
		}

		Q_strncpy( cof_pad.pad_name, name ? name : "", sizeof( cof_pad.pad_name ));
		cof_pad.pad_type = type;
		cof_pad.style_detected = style;
		Cvar_FullSet( cof_pad_type.name, cof_style_icon_ids[style], cof_pad_type.flags );
	}
#else
	(void)gamecontroller;
#endif
}

// cof_pad_style: -1 (or "auto") = follow the pad in use, 0..3 = that style
static int CL_CoF_PadStyleSetting( void )
{
	int v;

	if( !Q_stricmp( cof_pad_style.string, "auto" ))
		return -1;

	v = (int)cof_pad_style.value;
	return ( v >= 0 && v < COF_STYLE_COUNT ) ? v : -1;
}

static void CL_CoF_PadStyleFrame( void )
{
#if XASH_SDL == 2
	// the face buttons by position (see above); SDL follows the hint at once
	{
		const int labels = cof_pad_positional.value ? 0 : 1;

		if( labels != cof_pad.hint_labels )
		{
			SDL_SetHint( SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, labels ? "1" : "0" );
			cof_pad.hint_labels = labels;
			Con_Reportf( "[cof-pad] Nintendo face buttons reported by %s (SDL_GAMECONTROLLER_USE_BUTTON_LABELS %s)\n",
				labels ? "their printed label" : "position", SDL_GetHint( SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS ));
		}
	}
#endif

	// cof_pad_style_auto: the style of the pad in use, for Auto (-1)
	{
		const int detected = cof_pad.style_detected >= 0 ? cof_pad.style_detected : COF_STYLE_XBOX;

		if( detected != cof_pad.style_resolved )
		{
			cof_pad.style_resolved = detected;
			Cvar_FullSet( cof_pad_style_auto.name, va( "%d", detected ), cof_pad_style_auto.flags );
			Con_Reportf( "[cof-pad] cof_pad_style_auto %d (%s: %s)\n", detected, cof_style_icon_ids[detected],
				cof_pad.style_detected >= 0 ? cof_pad.pad_name : "no pad" );
		}
	}
}

/*
============
cof_pad_style_test

Developer: the style detection and the button positions run over a table of
simulated pads (no device, no OS input), one line per case with PASS / FAIL.
============
*/
static void CL_CoF_PadStyleTest_f( void )
{
#if XASH_SDL == 2
	static const struct
	{
		int        type, vid, pid;
		const char *name;
		qboolean   deck;
		int        want;
	} cases[] =
	{
		{ SDL_CONTROLLER_TYPE_XBOXONE, 0x045E, 0x02FF, "Xbox One Controller", false, COF_STYLE_XBOX },
		{ SDL_CONTROLLER_TYPE_XBOX360, 0x045E, 0x028E, "Xbox 360 Controller", false, COF_STYLE_XBOX },
		{ SDL_CONTROLLER_TYPE_PS5, 0x054C, 0x0CE6, "DualSense Wireless Controller", false, COF_STYLE_PLAYSTATION },
		{ SDL_CONTROLLER_TYPE_PS4, 0x054C, 0x09CC, "PS4 Controller", false, COF_STYLE_PLAYSTATION },
		{ SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO, 0x057E, 0x2009, "Nintendo Switch Pro Controller", false, COF_STYLE_NINTENDO },
		{ SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO, 0x057E, 0x2009, "Pro Controller", false, COF_STYLE_NINTENDO }, // 8BitDo Ultimate in Switch mode
		{ SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR, 0x057E, 0x2008, "Nintendo Switch Joy-Con (L/R)", false, COF_STYLE_NINTENDO },
		{ SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT, 0x057E, 0x2006, "Nintendo Switch Joy-Con (L)", false, COF_STYLE_NINTENDO },
		{ SDL_CONTROLLER_TYPE_UNKNOWN, 0x057E, 0x2017, "SNES Controller", false, COF_STYLE_NINTENDO },
		{ SDL_CONTROLLER_TYPE_UNKNOWN, 0x28DE, 0x1205, "Steam Deck", false, COF_STYLE_STEAMDECK },
		{ SDL_CONTROLLER_TYPE_XBOX360, 0x28DE, 0x11FF, "Steam Virtual Gamepad", true, COF_STYLE_STEAMDECK },
		{ SDL_CONTROLLER_TYPE_XBOX360, 0x28DE, 0x11FF, "Steam Virtual Gamepad", false, COF_STYLE_XBOX },
		{ SDL_CONTROLLER_TYPE_PS5, 0x28DE, 0x11FF, "PS5 Controller", true, COF_STYLE_PLAYSTATION }, // a DualSense on a Deck, through Steam Input
		{ SDL_CONTROLLER_TYPE_XBOXONE, 0x2DC8, 0x3106, "8BitDo Ultimate Wireless / Pro 2 Wired Controller", false, COF_STYLE_XBOX }, // X-input mode
	};
	int pass = 0;

	for( int i = 0; i < ARRAYSIZE( cases ); i++ )
	{
		const char *why = "";
		int got = CL_CoF_StyleForPad( cases[i].type, cases[i].vid, cases[i].pid, cases[i].name, cases[i].deck, &why );

		if( got == cases[i].want )
			pass++;

		Con_Printf( "[cof-pad] style test %2d %s: \"%s\" %s %04X:%04X%s -> %s (%s)\n", i + 1, got == cases[i].want ? "PASS" : "FAIL",
			cases[i].name, CL_CoF_PadTypeName( cases[i].type ), cases[i].vid, cases[i].pid, cases[i].deck ? " SteamDeck=1" : "",
			cof_style_names[got], why );
	}

	Con_Printf( "[cof-pad] style test: %d of %d pass; SDL_GAMECONTROLLER_USE_BUTTON_LABELS is \"%s\" (\"0\" = Nintendo face buttons by position)\n",
		pass, (int)ARRAYSIZE( cases ), SDL_GetHint( SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS ) ? SDL_GetHint( SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS ) : "(unset)" );
#endif
}

/*
============
cof_pad_virtual <switchpro|joycons|deck|xbox|ps5> | button <0..20> <0|1> | axis <0..5> <value> | off

Developer: an SDL virtual game controller (in-process, no OS device, no OS
input) with the USB ids and the name of a real pad, so the style detection
and the whole SDL -> engine key path run as for that pad; "button" presses
one of its SDL buttons (0 = SDL A, 1 = B, 2 = X, 3 = Y ...).
============
*/
#if XASH_SDL == 2 && SDL_VERSION_ATLEAST( 2, 0, 24 )
static int           cof_virtual_index = -1;
static SDL_Joystick *cof_virtual_joy;
static char          cof_virtual_bg_hint[8]; // SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS before the test
#endif

static void CL_CoF_PadVirtualAxis( int axis, int value )
{
#if XASH_SDL == 2 && SDL_VERSION_ATLEAST( 2, 0, 24 )
	if( cof_virtual_joy )
		SDL_JoystickSetVirtualAxis( cof_virtual_joy, axis, (Sint16)bound( -32768, value, 32767 ));
#else
	(void)axis; (void)value;
#endif
}

static void CL_CoF_PadVirtual_f( void )
{
#if XASH_SDL == 2 && SDL_VERSION_ATLEAST( 2, 0, 24 )
	static const struct { const char *id; int vid, pid; const char *name; } pads[] =
	{
		{ "switchpro", 0x057E, 0x2009, "Nintendo Switch Pro Controller" },
		{ "joycons",   0x057E, 0x2008, "Nintendo Switch Joy-Con (L/R)" },
		{ "deck",      0x28DE, 0x1205, "Steam Deck" },
		{ "xbox",      0x045E, 0x02FF, "Xbox One Controller" },
		{ "ps5",       0x054C, 0x0CE6, "DualSense Wireless Controller" },
	};
	const char *arg = Cmd_Argc( ) > 1 ? Cmd_Argv( 1 ) : "";

	if( !host_developer.value )
	{
		Con_Printf( "cof_pad_virtual is a developer command: set \"developer 1\" first\n" );
		return;
	}

	if( !Q_stricmp( arg, "button" ) && Cmd_Argc( ) == 4 )
	{
		if( !cof_virtual_joy )
		{
			Con_Printf( "[cof-pad] virtual pad: none attached\n" );
			return;
		}

		if( SDL_JoystickSetVirtualButton( cof_virtual_joy, Q_atoi( Cmd_Argv( 2 )), Q_atoi( Cmd_Argv( 3 )) ? SDL_PRESSED : SDL_RELEASED ) < 0 )
			Con_Printf( "[cof-pad] virtual pad: SDL_JoystickSetVirtualButton failed: %s\n", SDL_GetError( ));
		else Con_Printf( "[cof-pad] virtual pad: SDL button %d %s\n", Q_atoi( Cmd_Argv( 2 )), Q_atoi( Cmd_Argv( 3 )) ? "down" : "up" );
		return;
	}

	if( !Q_stricmp( arg, "axis" ) && Cmd_Argc( ) == 4 )
	{
		if( !cof_virtual_joy )
		{
			Con_Printf( "[cof-pad] virtual pad: none attached\n" );
			return;
		}

		// quiet: a test cfg may move an axis every frame
		CL_CoF_PadVirtualAxis( Q_atoi( Cmd_Argv( 2 )), Q_atoi( Cmd_Argv( 3 )));
		return;
	}

	if( !Q_stricmp( arg, "off" ))
	{
		if( cof_virtual_joy )
			SDL_JoystickClose( cof_virtual_joy );
		cof_virtual_joy = NULL;
		if( cof_virtual_index >= 0 )
			SDL_JoystickDetachVirtual( cof_virtual_index );
		cof_virtual_index = -1;
		if( cof_virtual_bg_hint[0] == '-' )
			SDL_ResetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS );
		else if( cof_virtual_bg_hint[0] )
			SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, cof_virtual_bg_hint );
		cof_virtual_bg_hint[0] = 0;
		Con_Printf( "[cof-pad] virtual pad detached\n" );
		return;
	}

	for( int i = 0; i < ARRAYSIZE( pads ); i++ )
	{
		SDL_VirtualJoystickDesc desc;

		if( Q_stricmp( arg, pads[i].id ))
			continue;

		if( cof_virtual_index >= 0 )
		{
			Con_Printf( "[cof-pad] virtual pad: one is attached already (cof_pad_virtual off)\n" );
			return;
		}

		SDL_zero( desc );
		desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
		desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
		desc.naxes = SDL_CONTROLLER_AXIS_MAX;
		desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
		desc.button_mask = ( 1u << SDL_CONTROLLER_BUTTON_MAX ) - 1; // every button and axis mapped (0 = none on SDL 2.30+)
		desc.axis_mask = ( 1u << SDL_CONTROLLER_AXIS_MAX ) - 1;
		desc.vendor_id = pads[i].vid;
		desc.product_id = pads[i].pid;
		desc.name = pads[i].name;

		cof_virtual_index = SDL_JoystickAttachVirtualEx( &desc );
		if( cof_virtual_index < 0 )
		{
			Con_Printf( "[cof-pad] virtual pad: SDL_JoystickAttachVirtualEx failed: %s\n", SDL_GetError( ));
			return;
		}

		cof_virtual_joy = SDL_JoystickOpen( cof_virtual_index );

		// SDL drops joystick events while the window is not focused; a test
		// launch must not depend on focus (nothing here may take it), so the
		// virtual pad's events are let through until it is detached
		{
			const char *bg = SDL_GetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS );

			Q_strncpy( cof_virtual_bg_hint, bg ? bg : "-", sizeof( cof_virtual_bg_hint ));
			SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );
		}
		Con_Printf( "[cof-pad] virtual pad \"%s\" %04X:%04X attached as device %d (game controller %d, SDL type %s)\n", pads[i].name,
			pads[i].vid, pads[i].pid, cof_virtual_index, SDL_IsGameController( cof_virtual_index ),
			CL_CoF_PadTypeName( SDL_GameControllerTypeForIndex( cof_virtual_index )));
		return;
	}

	Con_Printf( S_USAGE "cof_pad_virtual <switchpro|joycons|deck|xbox|ps5> | button <n> <0|1> | axis <n> <value> | off\n" );
#else
	Con_Printf( "cof_pad_virtual: needs SDL 2.0.24 or newer\n" );
#endif
}

/*
==============================================================================

Measurement (developer): cof_pad_measure <label> <seconds>

Frame rate (mean, slowest frame), the time the SDL event loop took, the
input counters (SDL events, gamepad axis events, trigger key edges, pad keys,
active-pad switches) and the player's horizontal speed over a window. For the
frame-rate and the movement checks of this round; nothing else uses it.

==============================================================================
*/
void CL_CoF_NoteEventLoop( double seconds, int events )
{
	cof_pad.stat_event_time += seconds;
	cof_pad.stat_events += events;
}

static void CL_CoF_PadVirtualAxis( int axis, int value );

static void CL_CoF_JitterFrame( void )
{
	static const engineAxis_t sdl_to_engine[6] = { JOY_AXIS_SIDE, JOY_AXIS_FWD, JOY_AXIS_YAW, JOY_AXIS_PITCH, JOY_AXIS_LT, JOY_AXIS_RT };
	int value;

	if( cof_pad.jit_axis < 0 )
		return;

	cof_pad.jit_phase ^= 1;
	value = cof_pad.jit_phase ? cof_pad.jit_hi : cof_pad.jit_lo;

	if( cof_pad.jit_virtual )
		CL_CoF_PadVirtualAxis( cof_pad.jit_axis, value );
	else Joy_AxisMotionEvent( sdl_to_engine[cof_pad.jit_axis], value );
}

/*
============
cof_pad_jitter <sdl axis 0..5> <lo> <hi> [virtual] | off

Developer: move one axis between two values every frame (a trigger resting
near its threshold). SDL axis numbers: 0/1 left stick x/y, 2/3 right stick
x/y, 4 LT, 5 RT. Without `virtual` the values go into Joy_AxisMotionEvent
(like cof_joy_axis_probe); with it into the attached cof_pad_virtual pad, so
they come back through SDL's own event path.
============
*/
static void CL_CoF_Jitter_f( void )
{
	if( !host_developer.value )
	{
		Con_Printf( "cof_pad_jitter is a developer command: set \"developer 1\" first\n" );
		return;
	}

	if( Cmd_Argc( ) == 2 && !Q_stricmp( Cmd_Argv( 1 ), "off" ))
	{
		cof_pad.jit_axis = -1;
		Con_Printf( "[cof-pad] jitter off\n" );
		return;
	}

	if( Cmd_Argc( ) < 4 )
	{
		Con_Printf( S_USAGE "cof_pad_jitter <sdl axis 0..5> <lo> <hi> [virtual] | off\n" );
		return;
	}

	cof_pad.jit_axis = bound( 0, Q_atoi( Cmd_Argv( 1 )), 5 );
	cof_pad.jit_lo = bound( -32768, Q_atoi( Cmd_Argv( 2 )), 32767 );
	cof_pad.jit_hi = bound( -32768, Q_atoi( Cmd_Argv( 3 )), 32767 );
	cof_pad.jit_virtual = Cmd_Argc( ) > 4 && !Q_stricmp( Cmd_Argv( 4 ), "virtual" );
	cof_pad.jit_phase = 0;
	Con_Printf( "[cof-pad] jitter: SDL axis %d between %d and %d every frame%s\n", cof_pad.jit_axis, cof_pad.jit_lo, cof_pad.jit_hi,
		cof_pad.jit_virtual ? " (virtual pad, SDL event path)" : "" );
}

static void CL_CoF_MeasureFrame( void )
{
	const double ft = host.realframetime;
	float        v;

	CL_CoF_JitterFrame( );

	if( !cof_pad.meas_on )
		return;

	if( cof_pad.meas_frames > 0 ) // the first frame belongs to the command's own frame
	{
		cof_pad.meas_sum += ft;
		cof_pad.meas_max = Q_max( cof_pad.meas_max, ft );
	}
	cof_pad.meas_frames++;

	v = sqrt( cl.simvel[0] * cl.simvel[0] + cl.simvel[1] * cl.simvel[1] );
	cof_pad.meas_vmax = Q_max( cof_pad.meas_vmax, v );
	cof_pad.meas_vsum += v;

	if( host.realtime < cof_pad.meas_end )
		return;

	{
		const double dt = host.realtime - cof_pad.meas_start;
		const int    n = cof_pad.meas_frames - 1;
		const float  dx = cl.simorg[0] - cof_pad.meas_org[0], dy = cl.simorg[1] - cof_pad.meas_org[1];
		const float  dist = sqrt( dx * dx + dy * dy );

		cof_pad.meas_on = false;
		if( cof_pad.meas_next[0] )
			Cbuf_AddText( va( "%s\n", cof_pad.meas_next ));
		Con_Printf( "[cof-meas] %s: %.2f s, %d frames, %.1f fps (mean frame %.2f ms, slowest %.2f ms), SDL event loop %.3f ms/frame, "
			"events %d (axis %d, sensor %d), trigger edges %d, pad keys %d, pad switches %d; speed %.1f u/s (velocity mean %.1f, max %.1f), moved %.1f %.1f, yaw %.1f; "
			"zoom target %g, fov %.1f; sticks fwd %.2f side %.2f, lt %.2f; cof_pad_move_mode %s\n",
			cof_pad.meas_label, dt, n, n > 0 ? n / cof_pad.meas_sum : 0.0, n > 0 ? cof_pad.meas_sum * 1000.0 / n : 0.0, cof_pad.meas_max * 1000.0,
			n > 0 ? ( cof_pad.stat_event_time - cof_pad.meas_event_time ) * 1000.0 / n : 0.0,
			cof_pad.stat_events - cof_pad.meas_c[0], cof_pad.stat_axis_events - cof_pad.meas_c[1], cof_pad.stat_sensor_events - cof_pad.meas_c[2],
			cof_pad.stat_trigger_edges - cof_pad.meas_c[3], cof_pad.stat_pad_keys - cof_pad.meas_c[4], cof_pad.stat_pad_switches - cof_pad.meas_c[5],
			dt > 0.0 ? dist / dt : 0.0, cof_pad.meas_frames > 0 ? cof_pad.meas_vsum / cof_pad.meas_frames : 0.0f, cof_pad.meas_vmax, dx, dy, cl.viewangles[YAW],
			cls.state == ca_active ? cl.frames[cl.parsecountmod].clientdata.vuser2[0] : -1.0f, cl.local.scr_fov,
			CL_CoF_PadAxis( JOY_AXIS_FWD ), CL_CoF_PadAxis( JOY_AXIS_SIDE ), CL_CoF_PadAxis( JOY_AXIS_LT ), cof_pad_move_mode.string );
	}
}

static void CL_CoF_Measure_f( void )
{
	if( Cmd_Argc( ) != 3 && Cmd_Argc( ) != 4 )
	{
		Con_Printf( S_USAGE "cof_pad_measure <label> <seconds> [alias to run afterwards] : frame rate, input counters and horizontal speed over a window\n" );
		return;
	}

	Q_strncpy( cof_pad.meas_next, Cmd_Argc( ) == 4 ? Cmd_Argv( 3 ) : "", sizeof( cof_pad.meas_next ));

	Q_strncpy( cof_pad.meas_label, Cmd_Argv( 1 ), sizeof( cof_pad.meas_label ));
	cof_pad.meas_on = true;
	cof_pad.meas_start = host.realtime;
	cof_pad.meas_end = host.realtime + bound( 0.1, Q_atof( Cmd_Argv( 2 )), 60.0 );
	cof_pad.meas_frames = 0;
	cof_pad.meas_sum = cof_pad.meas_max = 0.0;
	cof_pad.meas_vmax = cof_pad.meas_vsum = 0.0f;
	cof_pad.meas_event_time = cof_pad.stat_event_time;
	cof_pad.meas_c[0] = cof_pad.stat_events;
	cof_pad.meas_c[1] = cof_pad.stat_axis_events;
	cof_pad.meas_c[2] = cof_pad.stat_sensor_events;
	cof_pad.meas_c[3] = cof_pad.stat_trigger_edges;
	cof_pad.meas_c[4] = cof_pad.stat_pad_keys;
	cof_pad.meas_c[5] = cof_pad.stat_pad_switches;
	VectorCopy( cl.simorg, cof_pad.meas_org );
}

/*
============
cof_joy_axis_probe <side|fwd|pitch|yaw|lt|rt> <-32768..32767>

Developer hook: the value goes into Joy_AxisMotionEvent, the entry point the
SDL backend feeds, exactly like cof_key_probe feeds Key_Event. No OS input.
============
*/
static void CL_CoF_JoyAxisProbe_f( void )
{
	static const char *const names[MAX_AXES] = { "side", "fwd", "pitch", "yaw", "rt", "lt" };
	int axis = -1, value;

	if( !host_developer.value )
	{
		Con_Printf( "cof_joy_axis_probe is a developer command: set \"developer 1\" first\n" );
		return;
	}

	if( Cmd_Argc( ) != 3 )
	{
		Con_Printf( S_USAGE "cof_joy_axis_probe <side|fwd|pitch|yaw|lt|rt> <-32768..32767>\n" );
		return;
	}

	for( int i = 0; i < MAX_AXES; i++ )
	{
		if( !Q_stricmp( Cmd_Argv( 1 ), names[i] ))
			axis = i;
	}

	if( axis < 0 )
	{
		Con_Printf( "cof_joy_axis_probe: unknown axis \"%s\"\n", Cmd_Argv( 1 ));
		return;
	}

	value = bound( -32768, Q_atoi( Cmd_Argv( 2 )), 32767 );
	Con_Printf( "[cof-pad] axis probe %s = %d\n", names[axis], value );
	Joy_AxisMotionEvent( axis, value );
}

/*
============
cof_vcursor_test <dx> <dy> [0|1|2]
cof_vcursor_test abs <x> <y> [0|1|2]

Developer hook: moves the virtual cursor by (or to) render pixels and
optionally clicks (1 left, 2 right; press and release), through the same
functions the sticks and A/X use. Only while a clickable panel is open.
============
*/
static void CL_CoF_VCursorTest_f( void )
{
	qboolean absolute;
	int      argi, button;
	float    a, b;

	if( !host_developer.value )
	{
		Con_Printf( "cof_vcursor_test is a developer command: set \"developer 1\" first\n" );
		return;
	}

	// cof_vcursor_test button <handler class> <command> [0|1|2]: onto that
	// visible panel button (found by the support library, VGUI coordinates
	// turned into render pixels the way VGUI_GetMousePos does the reverse)
	if( Cmd_Argc( ) > 3 && !Q_stricmp( Cmd_Argv( 1 ), "button" ))
	{
		char  where[128];
		float bx, by, sx, sy, s = CL_CoF_UIScale( );

		if( !CL_CoF_PadGateActive( ))
		{
			Con_Printf( "[cof-pad] cof_vcursor_test: no clickable panel is open\n" );
			return;
		}

		Q_snprintf( where, sizeof( where ), "%s %s", Cmd_Argv( 2 ), Cmd_Argv( 3 ));
		if( !VGui_CoF_PanelAction( COF_VGUI_PANEL_LOCATE, where, sizeof( where )) || sscanf( where, "%f %f", &bx, &by ) != 2 )
		{
			Con_Printf( "[cof-pad] cof_vcursor_test: no visible button with %s command %s\n", Cmd_Argv( 2 ), Cmd_Argv( 3 ));
			return;
		}

		sx = (float)refState.width / (float)clgame.scrInfo.iWidth * ( s > 0.0f ? s : 1.0f );
		sy = (float)refState.height / (float)clgame.scrInfo.iHeight * ( s > 0.0f ? s : 1.0f );
		CL_CoF_NoteInput( COF_INPUT_PAD, "cof_vcursor_test" );
		CL_CoF_VCursorEngage( );
		cof_pad.x = bx * sx;
		cof_pad.y = by * sy;
		CL_CoF_VCursorApply( );
		Con_Printf( "[cof-pad] virtual cursor test: onto the %s command %s button, now at %d %d\n", Cmd_Argv( 2 ), Cmd_Argv( 3 ), (int)cof_pad.x, (int)cof_pad.y );

		button = Cmd_Argc( ) > 4 ? Q_atoi( Cmd_Argv( 4 )) : 0;
		if( button == 1 || button == 2 )
		{
			int mb = button == 1 ? K_MOUSE1 : K_MOUSE2;

			CL_CoF_VCursorClick( mb, true );
			CL_CoF_VCursorClick( mb, false );
			Con_Printf( "[cof-pad] virtual cursor test: %s click\n", button == 1 ? "left" : "right" );
		}
		return;
	}

	absolute = Cmd_Argc( ) > 1 && !Q_stricmp( Cmd_Argv( 1 ), "abs" );
	argi = absolute ? 2 : 1;

	if( Cmd_Argc( ) < argi + 2 )
	{
		Con_Printf( S_USAGE "cof_vcursor_test [abs] <x> <y> [0|1|2] : move the gamepad cursor by (abs: to) render pixels, then optionally click left (1) or right (2)\n" );
		return;
	}

	if( !CL_CoF_PadGateActive( ))
	{
		Con_Printf( "[cof-pad] cof_vcursor_test: no clickable panel is open\n" );
		return;
	}

	a = Q_atof( Cmd_Argv( argi ));
	b = Q_atof( Cmd_Argv( argi + 1 ));
	button = Cmd_Argc( ) > argi + 2 ? Q_atoi( Cmd_Argv( argi + 2 )) : 0;

	// stands in for a stick: the gamepad is the last input device
	CL_CoF_NoteInput( COF_INPUT_PAD, "cof_vcursor_test" );
	CL_CoF_VCursorEngage( );

	if( absolute )
	{
		cof_pad.x = a;
		cof_pad.y = b;
	}
	else
	{
		cof_pad.x += a;
		cof_pad.y += b;
	}

	CL_CoF_VCursorApply( );
	Con_Printf( "[cof-pad] virtual cursor test: now at %d %d\n", (int)cof_pad.x, (int)cof_pad.y );

	if( button == 1 || button == 2 )
	{
		int mb = button == 1 ? K_MOUSE1 : K_MOUSE2;

		CL_CoF_VCursorClick( mb, true );
		CL_CoF_VCursorClick( mb, false );
		Con_Printf( "[cof-pad] virtual cursor test: %s click at %d %d\n", button == 1 ? "left" : "right", (int)cof_pad.x, (int)cof_pad.y );
	}
}

/*
============
cof_pad_panel_dump

Developer: the buttons of the open panels with their handlers (the B / A
mapping's evidence).
============
*/
static void CL_CoF_PanelDump_f( void )
{
	char info[128];

	if( !VGui_CoF_PanelAction( COF_VGUI_PANEL_DUMP, info, sizeof( info )))
		Con_Printf( "[cof-pad] cof_pad_panel_dump: the VGUI support library does not offer the panel actions\n" );
	else Con_Printf( "[cof-pad] cof_pad_panel_dump: %s\n", info );
}

/*
============
cof_pad_status
============
*/
static void CL_CoF_PadStatus_f( void )
{
	convar_t *joystick = Cvar_FindVar( "joystick" );

#if XASH_SDL == 2
	{
		SDL_version v;
		int n = SDL_WasInit( SDL_INIT_GAMECONTROLLER ) ? SDL_NumJoysticks( ) : 0;

		SDL_GetVersion( &v );
		Con_Printf( "[cof-pad] SDL %d.%d.%d, %d joystick device(s)%s\n", v.major, v.minor, v.patch, n,
			SDL_WasInit( SDL_INIT_GAMECONTROLLER ) ? "" : " (GameController subsystem not initialised)" );

		for( int i = 0; i < n; i++ )
		{
			char guid[64];
			SDL_JoystickGetGUIDString( SDL_JoystickGetDeviceGUID( i ), guid, sizeof( guid ));
			Con_Printf( "[cof-pad]   #%d \"%s\" game controller %d, GUID %s, instance %d\n", i,
				SDL_IsGameController( i ) ? SDL_GameControllerNameForIndex( i ) : SDL_JoystickNameForIndex( i ),
				SDL_IsGameController( i ), guid, (int)SDL_JoystickGetDeviceInstanceID( i ));
		}
	}
#endif

	Con_Printf( "[cof-pad] clickable panel open %d (panel classes 0x%x%s; state %d, key_dest %d, cursor visible %d), gate %g, virtual cursor %s at %d %d\n",
		CL_CoF_ClickablePanelOpen( ), cof_pad.panel_classes, cof_pad.panel_by_cursor ? ", cursor fallback" : "",
		(int)cls.state, (int)cls.key_dest, (int)host.mouse_visible, cof_pad_panel_gate.value,
		cof_pad.engaged ? "engaged" : "idle", (int)cof_pad.x, (int)cof_pad.y );
	Con_Printf( "[cof-pad] joy_enable %d, sticks side %.2f fwd %.2f pitch %.2f yaw %.2f, triggers lt %.2f rt %.2f, client joystick %s, defaults generation %g\n",
		(int)Joy_IsActive( ), CL_CoF_PadAxis( JOY_AXIS_SIDE ), CL_CoF_PadAxis( JOY_AXIS_FWD ), CL_CoF_PadAxis( JOY_AXIS_PITCH ),
		CL_CoF_PadAxis( JOY_AXIS_YAW ), CL_CoF_PadAxis( JOY_AXIS_LT ), CL_CoF_PadAxis( JOY_AXIS_RT ),
		joystick ? joystick->string : "(not registered)", cof_pad_defaults_gen.value );
	Con_Printf( "[cof-pad] origin %.1f %.1f %.1f, angles %.1f %.1f\n", cl.simorg[0], cl.simorg[1], cl.simorg[2], cl.viewangles[PITCH], cl.viewangles[YAW] );
	Con_Printf( "[cof-pad] defaults this launch: %s\n", cof_pad.defaults_note[0] ? cof_pad.defaults_note : "nothing to do (generation already applied)" );
	Con_Printf( "[cof-pad] style %s (cof_pad_style %s, cof_pad_style_auto %s; pad in use \"%s\" SDL type %s -> %s), face buttons by %s, quick turn %s (%.2f s), move mode %s, trigger hysteresis %.2f\n",
		CL_CoF_PadStyleSetting( ) < 0 ? "auto" : cof_style_names[CL_CoF_PadStyleSetting( )], cof_pad_style.string, cof_pad_style_auto.string, cof_pad.pad_name,
#if XASH_SDL == 2
		cof_pad.style_detected >= 0 ? CL_CoF_PadTypeName( cof_pad.pad_type ) : "-",
#else
		"-",
#endif
		cof_pad.style_detected >= 0 ? cof_style_icon_ids[cof_pad.style_detected] : "-", cof_pad_positional.value ? "position" : "label",
		cof_pad.turn_on ? "RUNNING" : "idle", bound( 0.05f, cof_quickturn_time.value, 1.0f ), cof_pad_move_mode.string, CL_CoF_TriggerHysteresis( ));
	Con_Printf( "[cof-pad] crouch toggle %s, player hull %d (1 = crouched), view height %.1f, gyro aiming %s\n",
		cof_pad.duck_on ? "ON" : "off", cl.local.usehull, cl.viewheight[2], Cvar_VariableString( "joy_gyro_enable" ));
	Con_Printf( "[cof-pad] sprint %s (%s mode, cof_sprint_toggle %g), last input %s, OS arrow %s (hidden by the pad %d, host.mouse_visible %d), cursor art %s\n",
		cof_pad.sprint_on ? "ON" : "off", cof_pad.sprint_on ? ( cof_pad.sprint_hold ? "hold" : "toggle" ) : "-", cof_sprint_toggle.value,
		cof_input_names[CL_CoF_DeviceFromCvar( )],
#if XASH_SDL == 2
		SDL_ShowCursor( SDL_QUERY ) == SDL_ENABLE ? "shown" : "hidden",
#else
		"?",
#endif
		(int)cof_pad.os_hidden, (int)host.mouse_visible,
		cof_pad.art_tex ? COF_VCURSOR_ART : cof_pad.art_missing ? "missing (drawn ring)" : "not loaded yet" );

	if( Cmd_Argc( ) < 2 || Q_stricmp( Cmd_Argv( 1 ), "binds" ))
		return;

	for( int key = K_JOY1; key <= K_AUX30; key++ )
	{
		const char *b = Key_GetBinding( key );

		if( !COM_StringEmptyOrNULL( b ))
			Con_Printf( "[cof-pad]   %-12s \"%s\"\n", Key_KeynumToString( key ), b );
	}
}

/*
============
CL_CoF_PadInit

IN_Init: registered before config.cfg runs, so the archived cvars load.
============
*/
void CL_CoF_PadInit( void )
{
	Cvar_RegisterVariable( &cof_pad_panel_gate );
	Cvar_RegisterVariable( &cof_pad_close_key );
	Cvar_RegisterVariable( &cof_vcursor );
	Cvar_RegisterVariable( &cof_vcursor_speed );
	Cvar_RegisterVariable( &cof_vcursor_accel );
	Cvar_RegisterVariable( &cof_vcursor_accel_time );
	Cvar_RegisterVariable( &cof_vcursor_curve );
	Cvar_RegisterVariable( &cof_vcursor_deadzone );
	Cvar_RegisterVariable( &cof_vcursor_warp );
	Cvar_RegisterVariable( &cof_vcursor_click_key );
	Cvar_RegisterVariable( &cof_vcursor_rclick_key );
	Cvar_RegisterVariable( &cof_vcursor_size );
	Cvar_RegisterVariable( &cof_vcursor_alpha );
	Cvar_RegisterVariable( &cof_vcursor_shadow );
	Cvar_RegisterVariable( &cof_pad_hide_cursor );
	Cvar_RegisterVariable( &cof_last_input );
	Cvar_RegisterVariable( &cof_sprint_toggle );
	Cvar_RegisterVariable( &cof_sprint_stop_threshold );
	Cvar_RegisterVariable( &cof_sprint_stop_time );
	Cvar_RegisterVariable( &cof_joy_pulse_hysteresis );
	Cvar_RegisterVariable( &cof_joy_legacy_off );
	Cvar_RegisterVariable( &cof_pad_no_doubletap );
	Cvar_RegisterVariable( &cof_pad_defaults_gen );
	Cvar_RegisterVariable( &cof_input_trace );
	Cvar_RegisterVariable( &cof_quickturn_time );
	Cvar_RegisterVariable( &cof_pad_trigger_hyst );
	Cvar_RegisterVariable( &cof_pad_move_fix );
	Cvar_RegisterVariable( &cof_pad_move_mode );
	Cvar_RegisterVariable( &cof_pad_move_deadzone );
	Cvar_RegisterVariable( &cof_pad_move_walk );
	Cvar_RegisterVariable( &cof_pad_move_curve );
	Cvar_RegisterVariable( &cof_pad_move_diagonal );
	Cvar_RegisterVariable( &cof_pad_style );
	Cvar_RegisterVariable( &cof_pad_style_auto );
	Cvar_RegisterVariable( &cof_pad_positional );
	Cvar_RegisterVariable( &cof_pad_type );

	cof_pad.style_detected = cof_pad.style_resolved = cof_pad.hint_labels = -1;
	cof_pad.jit_axis = -1;

	Cmd_AddCommand( "cof_duck_toggle", CL_CoF_DuckToggle_f, "Cry of Fear: crouch toggle - the first press crouches (holds +duck), the next one stands up; released on every level change and load" );
	Cmd_AddCommand( "cof_sprint_toggle_press", CL_CoF_SprintPress_f, "Cry of Fear: sprint - toggle (cof_sprint_toggle 1: sprints until you stop moving or press again) or hold (0: while the key is held); released on every level change and load" );
	Cmd_AddCommand( "cof_pad_panel_dump", CL_CoF_PanelDump_f, "developer: list the buttons of the open Cry of Fear panels with their handlers" );
	Cmd_AddRestrictedCommand( "cof_pad_defaults", CL_CoF_PadDefaults_f, "bind the Cry of Fear gamepad layout to unbound pad keys (force: overwrite; list: show)" );
	Cmd_AddCommand( "cof_pad_status", CL_CoF_PadStatus_f, "print the Cry of Fear gamepad state: controllers, panel check, virtual cursor, sticks (binds: also the pad binds)" );
	Cmd_AddRestrictedCommand( "cof_joy_axis_probe", CL_CoF_JoyAxisProbe_f, "developer: feed a gamepad axis value to the engine's own axis path, without any OS input" );
	Cmd_AddRestrictedCommand( "cof_vcursor_test", CL_CoF_VCursorTest_f, "developer: move the gamepad cursor and optionally click, while a CoF panel is open" );
	Cmd_AddCommand( "cof_quickturn", CL_CoF_QuickTurn_f, "Cry of Fear: quick 180-degree turn (cof_quickturn_time seconds); in play only" );
	Cmd_AddRestrictedCommand( "cof_pad_measure", CL_CoF_Measure_f, "developer: frame rate, input counters and horizontal speed over a window" );
	Cmd_AddRestrictedCommand( "cof_pad_jitter", CL_CoF_Jitter_f, "developer: move one gamepad axis between two values every frame" );
	Cmd_AddRestrictedCommand( "cof_pad_style_test", CL_CoF_PadStyleTest_f, "developer: run the pad style detection over simulated pads" );
	Cmd_AddRestrictedCommand( "cof_pad_virtual", CL_CoF_PadVirtual_f, "developer: attach an SDL virtual game controller with a real pad's ids (in-process, no OS input)" );
}
