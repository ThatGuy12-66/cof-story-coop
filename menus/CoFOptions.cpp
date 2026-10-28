/*
CoFOptions.cpp -- Cry of Fear options layout: the Controls page (aiming,
mouse and gamepad settings), the Gamepad controls page (the action on each
pad button) and the cfg-only test hook menu_cof_options_select

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

Options for Cry of Fear with the theme reads Game, Controls, Keybinds,
Gamepad controls, Audio, Video (menus/Configuration.cpp):

  Game              menus/AdvancedControls.cpp: the game switches (crosshair,
                    look spring, console, pause-menu saves, notifications,
                    Pause on Inventory), Input devices, Language, HUD scale
  Controls          this file: Aim down sights and the mouse switches that
                    were on the Game page, plus the engine's gamepad cvars
  Keybinds          menus/Controls.cpp, the key list (it was "Controls")
  Gamepad controls  this file: every pad button and the action bound to it,
                    read from and written to the engine's key bindings

See docs/design/ui-theme.md, "Options relayout".
*/

#include "q_client.h"
#include "Framework.h"
#include "MenuStrings.h"
#include "PicButton.h"
#include "CheckBox.h"
#include "Slider.h"
#include "SpinControl.h"
#include "Action.h"
#include "Table.h"
#include "MessageBox.h"
#include "YesNoMessageBox.h"
#include "keydefs.h"
#include "utlvector.h"
#include "CryOfFear.h"
#include "CoFOptions.h"
#include "CoFOsk.h"

bool UI_CoFOptionsLayout( void )
{
	return UI_ThemeActive() && UI_IsCryOfFear();
}

// ---------------------------------------------------------------------------
// shared bits
// ---------------------------------------------------------------------------

// the exact spelling first ("g" is key 103, "G" the unused key 71), then
// any case
static int UI_CoFKeyByName( const char *name )
{
	for( int pass = 0; pass < 2; pass++ )
	{
		for( int i = 0; ; i++ )
		{
			const char *str = EngFuncs::KeynumToString( i );

			if( !str || !strcmp( str, "<OUT OF RANGE>" ))
				break;
			if( pass ? !stricmp( str, name ) : !strcmp( str, name ))
				return i;
		}
	}
	return -1;
}

void UI_CoFProbeCheck( CMenuCheckBox &box, const char *value )
{
	box.bChecked = atoi( value ) != 0;
	box.SetCvarValue( box.bChecked ? 1.0f : 0.0f );
	box._Event( QM_CHANGED );
}

void UI_CoFProbeSlider( CMenuSlider &slider, const char *value )
{
	slider.SetCurrentValue( (float)atof( value ));
	slider.SetCvarValue( slider.GetCurrentValue( ));
	slider._Event( QM_CHANGED );
}

void UI_CoFProbeSpin( CMenuSpinControl &spin, const char *value )
{
	spin.SetCurrentValue( (float)atoi( value ));	// raises QM_CHANGED itself
}

/*
=================
CMenuCoFReadoutSlider

A themed slider that prints its value at the right end of its label line,
like the Video page's field-of-view sliders: for a sensitivity or a deadzone
the number is the point. bDirty is set by a player's change, so a value the
page only shows (a deadzone of 4096 reads "13 %") is never written back
rounded.
=================
*/
class CMenuCoFReadoutSlider : public CMenuSlider
{
public:
	CMenuCoFReadoutSlider() : CMenuSlider(), iDecimals( 0 ), szUnit( "" ), bDirty( false ) { }

	void Draw() override
	{
		CMenuSlider::Draw();

		if( !UI_ThemeActive( ))
			return;

		char text[32];
		snprintf( text, sizeof( text ), "%.*f%s", iDecimals, GetCurrentValue(), szUnit );
		UI_DrawString( font, m_scPos.x, m_scPos.y - (int)( m_scChSize * 1.5f ),
			m_scSize.w, m_scChSize, text,
			FBitSet( iFlags, QMF_GRAYED ) ? THEME_DISABLED : THEME_TEXT, m_scChSize,
			QM_TOPRIGHT, ETF_FORCECOL | ETF_NO_WRAP );
	}

	int iDecimals;
	const char *szUnit;
	bool bDirty;
};

class CMenuCoFDirtyCheck : public CMenuCheckBox
{
public:
	CMenuCoFDirtyCheck() : CMenuCheckBox(), bDirty( false ) { }
	bool bDirty;
};

// a section caption: small dim uppercase label with a hairline under it, the
// same look as the section captions of the key list (controls/Table.cpp)
class CMenuCoFCaption : public CMenuBaseItem
{
public:
	CMenuCoFCaption() : CMenuBaseItem() { iFlags |= QMF_INACTIVE; }

	void Draw() override
	{
		const int capH = (int)( THEME_LABEL_TALL * uiStatic.scaleY );

		UI_DrawString( uiStatic.hThemeLabel, m_scPos, Size( m_scSize.w, capH ), szName,
			THEME_TEXT_DIM, capH, QM_LEFT, ETF_FORCECOL | ETF_NO_WRAP | ETF_NOSIZELIMIT );
		UI_ThemeSeparator( m_scPos.x, m_scPos.y + capH + (int)( 4 * uiStatic.scaleY ), m_scSize.w, THEME_SEPARATOR );
	}
};

#define COF_CAPTION_H	26	// caption + hairline + gap, virtual units

// ---------------------------------------------------------------------------
// Controls: aiming, mouse, gamepad
// ---------------------------------------------------------------------------

static const char *g_cofAdsModesC[] = { "Hold", "Toggle" };

class CMenuCoFAdsModelC : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return 2; }
	const char *GetText( int line ) override { return L( g_cofAdsModesC[line ? 1 : 0] ); }
};

// the move stick (cof_pad_move_mode, gamepad round 3): 0 = analogue,
// 1 = digital-style (full speed in the pushed direction, like the keys)
static const char *g_cofMoveModesC[] = { "Analogue", "Digital-style" };

class CMenuCoFMoveModelC : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return 2; }
	const char *GetText( int line ) override { return L( g_cofMoveModesC[line ? 1 : 0] ); }
};

// an engine cvar a later round may add: the spin control shows it when the
// engine has it (greyed, at the engine default, when not) and writes it live
static bool UI_CoFHaveCvar( const char *name )
{
	const char *s = EngFuncs::GetCvarString( name );
	return s && s[0];
}

static void UI_CoFSpinFromCvar( CMenuSpinControl &spin, const char *cvar, int def )
{
	const bool have = UI_CoFHaveCvar( cvar );

	spin.SetGrayed( !have );
	spin.SetCurrentValue( (float)( have ? ( EngFuncs::GetCvarFloat( cvar ) != 0.0f ? 1 : 0 ) : def ));
}

static void UI_CoFSpinToCvar( CMenuSpinControl &spin, const char *cvar )
{
	if( UI_CoFHaveCvar( cvar ))
		EngFuncs::CvarSetValue( cvar, spin.GetCurrentValue() >= 1.0f ? 1.0f : 0.0f );
}

// joy_axis_binding: one letter per SDL axis (left X, left Y, right Y, right X,
// then the triggers); "sfpyrl" is the engine default, and the swap puts look
// on the left stick and movement on the right (engine/client/input/in_joy.c,
// Joy_FinalizeMove)
#define COF_AXES_DEFAULT	"sfpyrl"
#define COF_AXES_SWAPPED	"ypfsrl"
#define COF_AXIS_FULL		32767.0f

class CMenuCoFControls : public CMenuFramework
{
public:
	typedef CMenuFramework BaseClass;
	CMenuCoFControls() : CMenuFramework( "CMenuCoFControls" ) { }

	void ToggleLook( bool write );
	void OptionsProbe( void );

private:
	void _Init( void ) override;
	void _VidInit( void ) override;
	void SaveAndPopMenu( void ) override;

	void GetConfig( void );
	void AdsChanged( void );
	void PitchInvert( void );

	CMenuCoFCaption capMouse, capPad, capMove;

	// gamepad movement (gamepad rounds 2 and 3): cof_sprint_toggle (L3 sprint
	// Hold / Toggle), cof_pad_move_mode (analogue / digital-style stick)
	CMenuSpinControl sprintMode, moveMode;
	CMenuCoFAdsModelC sprintModel;
	CMenuCoFMoveModelC moveModel;

	// aiming and mouse (moved here from the Game page)
	CMenuSpinControl adsMode;
	CMenuCoFAdsModelC adsModel;
	CMenuCoFReadoutSlider sensitivity;
	CMenuCheckBox rawinput, autoaim, lookFilter, mouseLook, invertMouse, lookStrafe;

	// gamepad (the engine's joy_* cvars)
	CMenuCheckBox joyEnable;
	// gyro aiming: joy_gyro_enable and joy_gyro_pitch / joy_gyro_yaw
	// (engine/client/input/in_joy.c); needs a pad with a gyroscope that the
	// engine has calibrated (joy_calibrate_gyro)
	CMenuCheckBox gyroEnable;
	CMenuCoFDirtyCheck invertLook, swapSticks;
	CMenuCoFReadoutSlider lookX, lookY, lookDeadzone, moveDeadzone, trigger, gyroSens;

	CMenuPicButton done;
};

void CMenuCoFControls::GetConfig( void )
{
	kbutton_t *mlook = (kbutton_t *)EngFuncs::KEY_GetState( "in_mlook" );

	invertMouse.bChecked = EngFuncs::GetCvarFloat( "m_pitch" ) < 0.0f;

	if( mlook )
		mouseLook.bChecked = ( mlook->state & 1 ) != 0;
	else
	{
		mouseLook.SetGrayed( true );
		mouseLook.bChecked = true;
	}

	lookStrafe.LinkCvar( "lookstrafe" );
	lookFilter.LinkCvar( "look_filter" );
	autoaim.LinkCvar( "sv_aim" );
	rawinput.LinkCvar( "m_rawinput" );
	sensitivity.LinkCvar( "sensitivity" );
	sensitivity.bDirty = false;

	adsMode.SetCurrentValue( EngFuncs::GetCvarFloat( "cof_ads_toggle" ) != 0.0f ? 1.0f : 0.0f );
	UI_CoFSpinFromCvar( sprintMode, "cof_sprint_toggle", 1 );
	UI_CoFSpinFromCvar( moveMode, "cof_pad_move_mode", 1 );

	joyEnable.LinkCvar( "joy_enable" );
	gyroEnable.LinkCvar( "joy_gyro_enable" );
	gyroSens.SetCurrentValue( EngFuncs::GetCvarFloat( "joy_gyro_yaw" ));

	const float pitch = EngFuncs::GetCvarFloat( "joy_pitch" );
	invertLook.bChecked = pitch < 0.0f;
	invertLook.bDirty = false;
	swapSticks.bChecked = !stricmp( EngFuncs::GetCvarString( "joy_axis_binding" ), COF_AXES_SWAPPED );
	swapSticks.bDirty = false;

	lookX.SetCurrentValue( fabs( EngFuncs::GetCvarFloat( "joy_yaw" )));
	lookY.SetCurrentValue( fabs( pitch ));
	lookDeadzone.SetCurrentValue( floor( EngFuncs::GetCvarFloat( "joy_pitch_deadzone" ) * 100.0f / COF_AXIS_FULL + 0.5f ));
	moveDeadzone.SetCurrentValue( floor( EngFuncs::GetCvarFloat( "joy_forward_deadzone" ) * 100.0f / COF_AXIS_FULL + 0.5f ));
	trigger.SetCurrentValue( floor( EngFuncs::GetCvarFloat( "joy_lt_threshold" ) * 100.0f / COF_AXIS_FULL + 0.5f ));

	CMenuCoFReadoutSlider *pad[] = { &lookX, &lookY, &lookDeadzone, &moveDeadzone, &trigger, &gyroSens };
	for( size_t i = 0; i < V_ARRAYSIZE( pad ); i++ )
		pad[i]->bDirty = false;

	ToggleLook( false );
}

void CMenuCoFControls::AdsChanged( void )
{
	// live and saved, as on the Game page before (engine: CL_CoF_ADSSync)
	EngFuncs::CvarSetValue( "cof_ads_toggle", adsMode.GetCurrentValue() >= 1.0f ? 1.0f : 0.0f );
}

void CMenuCoFControls::PitchInvert( void )
{
	const bool invert = invertMouse.bChecked;
	const float m_pitch = EngFuncs::GetCvarFloat( "m_pitch" );

	if(( invert && m_pitch > 0 ) || ( !invert && m_pitch < 0 ))
		EngFuncs::CvarSetValue( "m_pitch", -m_pitch );
}

void CMenuCoFControls::ToggleLook( bool write )
{
	lookStrafe.SetGrayed( mouseLook.bChecked );

	if( write )
		EngFuncs::ClientCmd( false, mouseLook.bChecked ? "+mlook\nbind _force_write\n" : "-mlook\nbind _force_write\n" );
}

/*
=================
CMenuCoFControls::SaveAndPopMenu

Close writes, as the Game page did for these switches; Escape leaves without
writing. Aim down sights, Mouse look and Reverse mouse apply the moment they
change (as before). The gamepad values are written only when the player
moved them, so an engine value the sliders can only show rounded survives.
=================
*/
void CMenuCoFControls::SaveAndPopMenu( void )
{
	lookStrafe.WriteCvar();
	lookFilter.WriteCvar();
	if( EngFuncs::GetCvarString( "m_filter" )[0] )
		EngFuncs::CvarSetValue( "m_filter", lookFilter.bChecked );
	autoaim.WriteCvar();
	rawinput.WriteCvar();
	sensitivity.WriteCvar();
	ToggleLook( true );

	joyEnable.WriteCvar();
	gyroEnable.WriteCvar();

	if( gyroSens.bDirty )
	{
		EngFuncs::CvarSetValue( "joy_gyro_pitch", gyroSens.GetCurrentValue( ));
		EngFuncs::CvarSetValue( "joy_gyro_yaw", gyroSens.GetCurrentValue( ));
	}

	if( lookX.bDirty )
	{
		const float sign = EngFuncs::GetCvarFloat( "joy_yaw" ) < 0.0f ? -1.0f : 1.0f;
		EngFuncs::CvarSetValue( "joy_yaw", sign * lookX.GetCurrentValue( ));
	}

	if( lookY.bDirty || invertLook.bDirty )
		EngFuncs::CvarSetValue( "joy_pitch", ( invertLook.bChecked ? -1.0f : 1.0f ) * lookY.GetCurrentValue( ));

	if( lookDeadzone.bDirty )
	{
		const float v = floor( lookDeadzone.GetCurrentValue() * COF_AXIS_FULL / 100.0f + 0.5f );
		EngFuncs::CvarSetValue( "joy_pitch_deadzone", v );
		EngFuncs::CvarSetValue( "joy_yaw_deadzone", v );
	}

	if( moveDeadzone.bDirty )
	{
		const float v = floor( moveDeadzone.GetCurrentValue() * COF_AXIS_FULL / 100.0f + 0.5f );
		EngFuncs::CvarSetValue( "joy_forward_deadzone", v );
		EngFuncs::CvarSetValue( "joy_side_deadzone", v );
	}

	if( trigger.bDirty )
	{
		const float v = floor( trigger.GetCurrentValue() * COF_AXIS_FULL / 100.0f + 0.5f );
		EngFuncs::CvarSetValue( "joy_lt_threshold", v );
		EngFuncs::CvarSetValue( "joy_rt_threshold", v );
	}

	if( swapSticks.bDirty )
		EngFuncs::CvarSetString( "joy_axis_binding", swapSticks.bChecked ? COF_AXES_SWAPPED : COF_AXES_DEFAULT );

	CMenuFramework::SaveAndPopMenu();
}

void CMenuCoFControls::_Init( void )
{
	SetPanel( "CONTROLS", 800, 560 );

	capMouse.szName = L( "AIMING AND MOUSE" );
	capPad.szName = L( "GAMEPAD" );
	capMove.szName = L( "GAMEPAD MOVEMENT" );

	sprintMode.szName = L( "Sprint" );
	sprintMode.Setup( &sprintModel );
	sprintMode.SetCharSize( QM_SMALLFONT );
	sprintMode.iFlags |= QMF_NOTIFY;
	sprintMode.szStatusText = L( "Hold: sprint while the sprint button is held. Toggle: press it once; sprinting stops when you stop moving" );
	SET_EVENT( sprintMode.onChanged, UI_CoFSpinToCvar( *(CMenuSpinControl *)pSelf, "cof_sprint_toggle" ));

	moveMode.szName = L( "Stick movement" );
	moveMode.Setup( &moveModel );
	moveMode.SetCharSize( QM_SMALLFONT );
	moveMode.iFlags |= QMF_NOTIFY;
	moveMode.szStatusText = L( "Analogue: the move stick sets the speed. Digital-style: full speed in the pushed direction, like the movement keys" );
	SET_EVENT( moveMode.onChanged, UI_CoFSpinToCvar( *(CMenuSpinControl *)pSelf, "cof_pad_move_mode" ));

	adsMode.szName = L( "Aim down sights" );
	adsMode.Setup( &adsModel );
	adsMode.SetCharSize( QM_SMALLFONT );
	adsMode.iFlags |= QMF_NOTIFY;
	adsMode.szStatusText = L( "Hold: aim while the aim key is held. Toggle: press once to aim, press again to stop" );
	adsMode.onChanged = VoidCb( &CMenuCoFControls::AdsChanged );

	sensitivity.szName = L( "GameUI_MouseSensitivity" );
	sensitivity.Setup( 0.0f, 20.0f, 0.1f );
	sensitivity.iDecimals = 1;

	rawinput.SetNameAndStatus( L( "GameUI_RawInput" ), L( "GameUI_RawInputLabel" ));
	autoaim.SetNameAndStatus( L( "GameUI_AutoAim" ), L( "GameUI_AutoaimLabel" ));
	lookFilter.SetNameAndStatus( L( "GameUI_MouseFilter" ), L( "GameUI_MouseFilterLabel" ));
	mouseLook.SetNameAndStatus( L( "GameUI_MouseLook" ), L( "GameUI_MouseLookLabel" ));
	SET_EVENT( mouseLook.onChanged, ((CMenuCoFControls *)pSelf->Parent())->ToggleLook( true ));
	invertMouse.SetNameAndStatus( L( "GameUI_ReverseMouse" ), L( "GameUI_ReverseMouseLabel" ));
	invertMouse.onChanged = VoidCb( &CMenuCoFControls::PitchInvert );
	lookStrafe.SetNameAndStatus( L( "Look strafe" ), L( "In combination with your mouse look modifier, causes left-right movements to strafe instead of turn" ));

	joyEnable.SetNameAndStatus( L( "Enable gamepad" ), L( "Use a connected gamepad for moving, looking and the button actions" ));
	invertLook.SetNameAndStatus( L( "Invert look" ), L( "Push the look stick up to look down" ));
	SET_EVENT( invertLook.onChanged, ((CMenuCoFDirtyCheck *)pSelf)->bDirty = true );
	swapSticks.SetNameAndStatus( L( "Swap sticks" ), L( "Look with the left stick and move with the right stick" ));
	SET_EVENT( swapSticks.onChanged, ((CMenuCoFDirtyCheck *)pSelf)->bDirty = true );

	lookX.szName = L( "Look speed, horizontal" );
	lookX.szStatusText = L( "How fast the view turns with the look stick fully pushed, in degrees per second" );
	lookX.Setup( 20.0f, 400.0f, 10.0f );
	lookY.szName = L( "Look speed, vertical" );
	lookY.szStatusText = L( "How fast the view turns with the look stick fully pushed, in degrees per second" );
	lookY.Setup( 20.0f, 400.0f, 10.0f );
	lookDeadzone.szName = L( "Look stick deadzone" );
	lookDeadzone.szStatusText = L( "How far a stick has to move before it counts, in percent of its travel" );
	lookDeadzone.Setup( 0.0f, 50.0f, 1.0f );
	lookDeadzone.szUnit = " %";
	moveDeadzone.szName = L( "Move stick deadzone" );
	moveDeadzone.szStatusText = L( "How far a stick has to move before it counts, in percent of its travel" );
	moveDeadzone.Setup( 0.0f, 50.0f, 1.0f );
	moveDeadzone.szUnit = " %";
	trigger.szName = L( "Trigger threshold" );
	trigger.szStatusText = L( "How far a trigger has to be pulled to count as pressed, in percent" );
	trigger.Setup( 5.0f, 95.0f, 1.0f );
	trigger.szUnit = " %";
	gyroEnable.SetNameAndStatus( L( "Gyro aiming" ), L( "Aim by tilting a pad that has a gyroscope" ));
	gyroSens.szName = L( "Gyro sensitivity" );
	gyroSens.szStatusText = L( "How far the view turns for a tilt of the pad" );
	gyroSens.Setup( 0.1f, 4.0f, 0.1f );
	gyroSens.iDecimals = 1;

	CMenuCoFReadoutSlider *sliders[] = { &sensitivity, &lookX, &lookY, &lookDeadzone, &moveDeadzone, &trigger, &gyroSens };
	for( size_t i = 0; i < V_ARRAYSIZE( sliders ); i++ )
	{
		sliders[i]->iFlags |= QMF_NOTIFY;
		SET_EVENT( sliders[i]->onChanged, ((CMenuCoFReadoutSlider *)pSelf)->bDirty = true );
	}

	CMenuCheckBox *boxes[] = { &rawinput, &autoaim, &lookFilter, &mouseLook, &invertMouse, &lookStrafe, &joyEnable, &invertLook, &swapSticks, &gyroEnable };
	for( size_t i = 0; i < V_ARRAYSIZE( boxes ); i++ )
		boxes[i]->iFlags |= QMF_NOTIFY;

	done.szName = L( "Close" );
	done.SetPicture( PC_DONE );
	done.onReleased = VoidCb( &CMenuCoFControls::SaveAndPopMenu );

	AddItem( capMouse );
	AddItem( adsMode );
	AddItem( sensitivity );
	AddItem( rawinput );
	AddItem( autoaim );
	AddItem( lookFilter );
	AddItem( mouseLook );
	AddItem( invertMouse );
	AddItem( lookStrafe );
	AddItem( capMove );
	AddItem( sprintMode );
	AddItem( moveMode );
	AddItem( capPad );
	AddItem( joyEnable );
	AddItem( invertLook );
	AddItem( swapSticks );
	AddItem( lookX );
	AddItem( lookY );
	AddItem( lookDeadzone );
	AddItem( moveDeadzone );
	AddItem( trigger );
	AddItem( gyroEnable );
	AddItem( gyroSens );
	AddItem( done );
}

void CMenuCoFControls::_VidInit( void )
{
	Rect left = ColumnRect( 0, 2 );
	Rect right = ColumnRect( 1, 2 );
	int y;

	// left: aiming and mouse
	y = left.pt.y;
	capMouse.SetRect( left.pt.x, y, left.sz.w, COF_CAPTION_H );
	y += COF_CAPTION_H + THEME_LABEL_TALL + 12;	// the spin control's caption sits above its box
	adsMode.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
	y += THEME_CTRL_PITCH;
	sensitivity.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );
	y += THEME_SLIDER_H + 18;

	CMenuCheckBox *mouseBoxes[] = { &rawinput, &autoaim, &lookFilter, &mouseLook, &invertMouse, &lookStrafe };
	for( size_t i = 0; i < V_ARRAYSIZE( mouseBoxes ); i++ )
	{
		mouseBoxes[i]->SetCoord( left.pt.x, y );
		y += THEME_ROW_PITCH;
	}

	// under them: gamepad movement, two half-width spin controls
	y += 8;
	capMove.SetRect( left.pt.x, y, left.sz.w, COF_CAPTION_H );
	y += COF_CAPTION_H + THEME_LABEL_TALL + 12;
	{
		const int half = ( left.sz.w - THEME_GUTTER3 ) / 2;

		sprintMode.SetRect( left.pt.x, y, half, THEME_CTRL_H );
		moveMode.SetRect( left.pt.x + left.sz.w - half, y, half, THEME_CTRL_H );
	}

	// right: gamepad
	y = right.pt.y;
	capPad.SetRect( right.pt.x, y, right.sz.w, COF_CAPTION_H );
	y += COF_CAPTION_H + 4;

	CMenuCheckBox *padBoxes[] = { &joyEnable, &invertLook, &swapSticks, &gyroEnable };
	for( size_t i = 0; i < V_ARRAYSIZE( padBoxes ); i++ )
	{
		padBoxes[i]->SetCoord( right.pt.x, y );
		y += THEME_ROW_PITCH;
	}

	y += THEME_LABEL_TALL + 14;
	CMenuCoFReadoutSlider *padSliders[] = { &lookX, &lookY, &lookDeadzone, &moveDeadzone, &trigger, &gyroSens };
	for( size_t i = 0; i < V_ARRAYSIZE( padSliders ); i++ )
	{
		padSliders[i]->SetRect( right.pt.x, y, right.sz.w, THEME_SLIDER_H );
		y += THEME_CTRL_PITCH - 12;
	}

	CMenuPicButton *btns[] = { &done };
	LayoutButtonRow( btns, 1 );

	GetConfig();
}

/*
=================
CMenuCoFControls::OptionsProbe

menu_cof_options_select controls [<item> <value> | close | cancel]
=================
*/
void CMenuCoFControls::OptionsProbe( void )
{
	char item[64], value[64];
	Q_strncpy( item, EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "", sizeof( item ));
	Q_strncpy( value, EngFuncs::CmdArgc() > 3 ? EngFuncs::CmdArgv( 3 ) : "", sizeof( value ));

	if( !stricmp( item, "close" ))
	{
		Con_Printf( "menu_cof_options_select controls: Close\n" );
		SaveAndPopMenu();
		return;
	}
	if( !stricmp( item, "cancel" ))
	{
		Con_Printf( "menu_cof_options_select controls: Escape (nothing written)\n" );
		Hide();
		return;
	}

	struct { const char *name; CMenuCheckBox *box; CMenuSlider *slider; const char *cvar; } items[] =
	{
		{ "rawinput",   &rawinput,    NULL,          "m_rawinput" },
		{ "autoaim",    &autoaim,     NULL,          "sv_aim" },
		{ "filter",     &lookFilter,  NULL,          "look_filter" },
		{ "mlook",      &mouseLook,   NULL,          "in_mlook" },
		{ "reverse",    &invertMouse, NULL,          "m_pitch" },
		{ "lookstrafe", &lookStrafe,  NULL,          "lookstrafe" },
		{ "joyenable",  &joyEnable,   NULL,          "joy_enable" },
		{ "invertlook", &invertLook,  NULL,          "joy_pitch" },
		{ "swapsticks", &swapSticks,  NULL,          "joy_axis_binding" },
		{ "sensitivity", NULL,        &sensitivity,  "sensitivity" },
		{ "lookx",      NULL,         &lookX,        "joy_yaw" },
		{ "looky",      NULL,         &lookY,        "joy_pitch" },
		{ "lookdz",     NULL,         &lookDeadzone, "joy_pitch_deadzone" },
		{ "movedz",     NULL,         &moveDeadzone, "joy_forward_deadzone" },
		{ "trigger",    NULL,         &trigger,      "joy_lt_threshold" },
		{ "gyro",       &gyroEnable,  NULL,          "joy_gyro_enable" },
		{ "gyrosens",   NULL,         &gyroSens,     "joy_gyro_yaw" },
	};

	if( !stricmp( item, "ads" ))
	{
		if( value[0] )
			UI_CoFProbeSpin( adsMode, value );
		Con_Printf( "menu_cof_options_select controls: ads = %d (%s) (cof_ads_toggle \"%s\")\n",
			(int)adsMode.GetCurrentValue(), adsMode.GetCurrentString(), EngFuncs::GetCvarString( "cof_ads_toggle" ));
		return;
	}

	struct { const char *name; CMenuSpinControl *spin; const char *cvar; } spins[] =
	{
		{ "sprint",   &sprintMode, "cof_sprint_toggle" },
		{ "movemode", &moveMode,   "cof_pad_move_mode" },
	};

	for( size_t i = 0; i < V_ARRAYSIZE( spins ); i++ )
	{
		if( item[0] && stricmp( item, spins[i].name ))
			continue;
		if( item[0] && value[0] )
			UI_CoFProbeSpin( *spins[i].spin, value );
		Con_Printf( "menu_cof_options_select controls: %s = %d (%s) (%s \"%s\")%s\n", spins[i].name,
			(int)spins[i].spin->GetCurrentValue(), spins[i].spin->GetCurrentString(), spins[i].cvar,
			EngFuncs::GetCvarString( spins[i].cvar ), FBitSet( spins[i].spin->iFlags, QMF_GRAYED ) ? " grayed (no such engine cvar)" : "" );
		if( item[0] )
			return;
	}

	bool any = false;
	for( size_t i = 0; i < V_ARRAYSIZE( items ); i++ )
	{
		if( item[0] && stricmp( item, items[i].name ))
			continue;
		any = true;

		if( item[0] && value[0] )
		{
			if( items[i].box )
				UI_CoFProbeCheck( *items[i].box, value );
			else
				UI_CoFProbeSlider( *items[i].slider, value );
		}

		if( items[i].box )
			Con_Printf( "menu_cof_options_select controls: %s = %d (%s \"%s\")%s\n", items[i].name, items[i].box->bChecked ? 1 : 0,
				items[i].cvar, EngFuncs::GetCvarString( items[i].cvar ), FBitSet( items[i].box->iFlags, QMF_GRAYED ) ? " grayed" : "" );
		else
			Con_Printf( "menu_cof_options_select controls: %s = %g (%s \"%s\")\n", items[i].name, items[i].slider->GetCurrentValue(),
				items[i].cvar, EngFuncs::GetCvarString( items[i].cvar ));
	}

	if( !item[0] )
		Con_Printf( "menu_cof_options_select controls: ads = %d (cof_ads_toggle \"%s\")\n",
			(int)adsMode.GetCurrentValue(), EngFuncs::GetCvarString( "cof_ads_toggle" ));
	else if( !any )
		Con_Printf( "menu_cof_options_select controls: no item \"%s\"\n", item );
}

ADD_MENU( menu_cofcontrols, CMenuCoFControls, UI_CoFControls_Menu );

// ---------------------------------------------------------------------------
// Gamepad controls
// ---------------------------------------------------------------------------

// an engine key name, never drawn as text (the menu-string extractor,
// scripts/polish/extract-menu-strings.ps1, skips COF_KEY arguments)
#define COF_KEY( name )	name

/*
The rows: every button a pad gives the engine (SDL2 GameController through
engine/platform/sdl2/joy_sdl2.c; key names from engine/client/input/in_keys.c).
LT and RT are the analog triggers, which the engine turns into the keys
LTRIGGER / RTRIGGER (K_JOY1 / K_JOY2) past joy_lt_threshold / joy_rt_threshold.
Names are the Xbox and the PlayStation ones; the icons, the picture and the
marker positions come from gfx/shell/gamepad/icons.txt (Zacksly's Button
Icons and Controls, CC BY 3.0).
*/
static const struct { const char *key; const char *xbox; const char *ps; } g_cofPadRows[] =
{
	{ NULL,        "FACE BUTTONS",           NULL },
	{ COF_KEY( "A_BUTTON" ),  "A",                      "Cross" },
	{ COF_KEY( "B_BUTTON" ),  "B",                      "Circle" },
	{ COF_KEY( "X_BUTTON" ),  "X",                      "Square" },
	{ COF_KEY( "Y_BUTTON" ),  "Y",                      "Triangle" },
	{ NULL,        "SHOULDERS AND TRIGGERS", NULL },
	{ COF_KEY( "L1_BUTTON" ), "LB",                     "L1" },
	{ COF_KEY( "R1_BUTTON" ), "RB",                     "R1" },
	{ COF_KEY( "LTRIGGER" ),  "LT",                     "L2" },
	{ COF_KEY( "RTRIGGER" ),  "RT",                     "R2" },
	{ NULL,        "STICKS",                 NULL },
	{ COF_KEY( "STICK1" ),    "Left stick click",       "L3" },
	{ COF_KEY( "STICK2" ),    "Right stick click",      "R3" },
	{ NULL,        "D-PAD",                  NULL },
	{ COF_KEY( "DPAD_UP" ),   "D-pad up",               "D-pad up" },
	{ COF_KEY( "DPAD_DOWN" ), "D-pad down",             "D-pad down" },
	{ COF_KEY( "DPAD_LEFT" ), "D-pad left",             "D-pad left" },
	{ COF_KEY( "DPAD_RIGHT" ),"D-pad right",            "D-pad right" },
	{ NULL,        "MENU BUTTONS",           NULL },
	{ COF_KEY( "START" ),     "Menu",                   "Options" },
	{ COF_KEY( "BACK" ),      "View",                   "Create" },
};
#define COF_PAD_ROWS	( (int)V_ARRAYSIZE( g_cofPadRows ))

// every key name a pad can produce; "Use defaults" clears these before it
// applies the pad lines of gfx/shell/kb_def.lst
static const char *g_cofPadKeys[] =
{
	COF_KEY( "A_BUTTON" ), COF_KEY( "B_BUTTON" ), COF_KEY( "X_BUTTON" ), COF_KEY( "Y_BUTTON" ), COF_KEY( "BACK" ), COF_KEY( "MODE" ), COF_KEY( "START" ),
	COF_KEY( "STICK1" ), COF_KEY( "STICK2" ), COF_KEY( "L1_BUTTON" ), COF_KEY( "R1_BUTTON" ), COF_KEY( "DPAD_UP" ), COF_KEY( "DPAD_DOWN" ),
	COF_KEY( "DPAD_LEFT" ), COF_KEY( "DPAD_RIGHT" ), COF_KEY( "L2_BUTTON" ), COF_KEY( "R2_BUTTON" ), COF_KEY( "LTRIGGER" ), COF_KEY( "RTRIGGER" ),
	COF_KEY( "JOY3" ), COF_KEY( "JOY4" ), COF_KEY( "C_BUTTON" ), COF_KEY( "Z_BUTTON" ), COF_KEY( "MISC_BUTTON" ), COF_KEY( "PADDLE1" ), COF_KEY( "PADDLE2" ),
	COF_KEY( "PADDLE3" ), COF_KEY( "PADDLE4" ), COF_KEY( "TOUCHPAD" ), COF_KEY( "MISC2_BUTTON" ),
};
#define COF_PAD_KEYS	( (int)V_ARRAYSIZE( g_cofPadKeys ))

static bool UI_CoFIsPadKeyName( const char *name )
{
	for( int i = 0; i < COF_PAD_KEYS; i++ )
	{
		if( !stricmp( name, g_cofPadKeys[i] ))
			return true;
	}
	return false;
}

bool UI_CoFIsPadKey( int keynum )
{
	const char *name = keynum >= 0 ? EngFuncs::KeynumToString( keynum ) : NULL;

	return name && UI_CoFIsPadKeyName( name );
}

#define COF_PAD_DIR		"gfx/shell/gamepad/"
#define COF_PAD_MAX_ACTIONS	96

// the button styles, cof_pad_style 0..3; the ids are the ones icons.txt
// and the engine use
#define COF_PAD_STYLES		4
static const char *g_cofPadStyleIds[COF_PAD_STYLES] = { "xbox", "ps5", "switch", "steamdeck" };

struct cofPadIcon_t
{
	char key[24];
	char file[COF_PAD_STYLES][64];	// relative to gfx/shell/gamepad/; empty = the Xbox one
	char label[COF_PAD_STYLES][32];	// the printed name on that pad
	float u[COF_PAD_STYLES], v[COF_PAD_STYLES];	// marker centre, fractions of the picture
	float size[COF_PAD_STYLES];	// marker size, fraction of the picture width
	bool placed[COF_PAD_STYLES];	// u / v / size given for that style
};

/*
=================
gfx/shell/gamepad/icons.txt

One button or style per line, // comments, three kinds of line:

  <engine key> <xbox icon> <ps5 icon> <xbox label> <ps label>
      <xbox u> <xbox v> <ps5 u> <ps5 v> <marker size>      (10 fields)
  "STYLE" <style id> <controller picture>                   (3 fields)
  <engine key> <style id> <icon> <label> <u> <v> <marker size>   (7 fields)

style ids: xbox, ps5, switch, steamdeck (cof_pad_style 0..3). A key with no
row for a style uses the Xbox icon and name; a style with no STYLE line uses
the Xbox picture and the Xbox marker positions; a style that has its own
picture marks only the keys that have a row for it.
=================
*/
struct cofPadIcons_t
{
	cofPadIcon_t icon[48];
	int count;
	char pic[COF_PAD_STYLES][96];	// "" = the Xbox picture
	bool loaded;
};
static cofPadIcons_t g_cofPadIcons;

static int UI_CoFPadStyleId( const char *id )
{
	for( int i = 0; i < COF_PAD_STYLES; i++ )
	{
		if( !stricmp( id, g_cofPadStyleIds[i] ))
			return i;
	}
	return -1;
}

/*
cof_pad_style: -1 (or "auto", the default) = Auto: the engine resolves the
style from the detected controller and reports it in cof_pad_style_auto
(0..3 or a style id; gamepad round 3); without that cvar Auto shows Xbox.
0..3 = Xbox, PlayStation, Nintendo, Steam Deck chosen by hand.

A profile saved before Auto existed holds "0" only because that was the old
default: it becomes Auto once (ui_cof_pad_style_gen 0 -> 1). Called from
UI_UpdateMenu's first frame (BaseMenu.cpp), after config.cfg has run.
*/
void UI_CoFPadStyleMigrate( void )
{
	static bool done = false;

	if( done )
		return;
	done = true;

	if( EngFuncs::GetCvarFloat( "ui_cof_pad_style_gen" ) >= 1.0f )
		return;

	const char *s = EngFuncs::GetCvarString( "cof_pad_style" );
	if( s && !strcmp( s, "0" ))
	{
		EngFuncs::CvarSetString( "cof_pad_style", "-1" );
		Con_DPrintf( "menu_cofgamepad: cof_pad_style 0 from an older profile is now -1 (Auto)\n" );
	}
	EngFuncs::CvarSetValue( "ui_cof_pad_style_gen", 1.0f );
}

static void UI_CoFPadStyleRegister( void )
{
	EngFuncs::CvarRegister( "cof_pad_style", "-1", FCVAR_ARCHIVE );
	EngFuncs::CvarRegister( "ui_cof_pad_style_gen", "0", FCVAR_ARCHIVE );
}

// the setting: -1 Auto, 0..3 a style chosen by hand
int UI_CoFPadStyleSetting( void )
{
	const char *s = EngFuncs::GetCvarString( "cof_pad_style" );
	if( !s || !s[0] || !stricmp( s, "auto" ))
		return -1;

	const int st = atoi( s );
	return st < 0 ? -1 : st >= COF_PAD_STYLES ? COF_PAD_STYLES - 1 : st;
}

// the style in force, 0..3
int UI_CoFPadStyle( void )
{
	int st = UI_CoFPadStyleSetting();

	if( st < 0 )
	{
		const char *a = EngFuncs::GetCvarString( "cof_pad_style_auto" );

		st = 0;
		if( a && a[0] )
			st = UI_CoFPadStyleId( a ) >= 0 ? UI_CoFPadStyleId( a ) : atoi( a );
	}
	return st < 0 ? 0 : st >= COF_PAD_STYLES ? COF_PAD_STYLES - 1 : st;
}

static cofPadIcon_t *UI_CoFPadIconFor( const char *key, bool create )
{
	for( int i = 0; i < g_cofPadIcons.count; i++ )
	{
		if( !stricmp( g_cofPadIcons.icon[i].key, key ))
			return &g_cofPadIcons.icon[i];
	}
	if( !create || g_cofPadIcons.count >= (int)V_ARRAYSIZE( g_cofPadIcons.icon ))
		return NULL;

	cofPadIcon_t *ic = &g_cofPadIcons.icon[g_cofPadIcons.count++];
	memset( ic, 0, sizeof( *ic ));
	Q_strncpy( ic->key, key, sizeof( ic->key ));
	return ic;
}

static void UI_CoFPadIconsLoad( bool force )
{
	if( g_cofPadIcons.loaded && !force )
		return;

	memset( &g_cofPadIcons, 0, sizeof( g_cofPadIcons ));
	g_cofPadIcons.loaded = true;
	Q_strncpy( g_cofPadIcons.pic[0], "xbox/controller.png", sizeof( g_cofPadIcons.pic[0] ));
	Q_strncpy( g_cofPadIcons.pic[1], "ps5/controller.png", sizeof( g_cofPadIcons.pic[1] ));

	char *afile = (char *)EngFuncs::COM_LoadFile( COF_PAD_DIR "icons.txt", NULL );
	if( !afile )
	{
		Con_Printf( "menu_cofgamepad: " COF_PAD_DIR "icons.txt not found, rows without icons\n" );
		return;
	}

	int rows = 0, styleRows = 0, styles = 0;
	char *line = afile;

	while( line && *line )
	{
		char *end = line;
		while( *end && *end != '\n' && *end != '\r' )
			end++;
		const char save = *end;
		*end = 0;

		char *cmt = strstr( line, "//" );
		if( cmt )
			*cmt = 0;

		char tok[12][96];
		int n = 0;
		char *p = line;

		while( n < 12 && ( p = EngFuncs::COM_ParseFile( p, tok[n], sizeof( tok[n] ))) != NULL )
			n++;

		if( n == 3 && !stricmp( tok[0], "STYLE" ) && UI_CoFPadStyleId( tok[1] ) >= 0 )
		{
			Q_strncpy( g_cofPadIcons.pic[UI_CoFPadStyleId( tok[1] )], tok[2], sizeof( g_cofPadIcons.pic[0] ));
			styles++;
		}
		else if( n >= 10 && strchr( tok[1], '/' ))
		{
			cofPadIcon_t *ic = UI_CoFPadIconFor( tok[0], true );
			if( ic )
			{
				for( int s = 0; s < 2; s++ )
				{
					Q_strncpy( ic->file[s], tok[1 + s], sizeof( ic->file[s] ));
					Q_strncpy( ic->label[s], tok[3 + s], sizeof( ic->label[s] ));
					ic->u[s] = atof( tok[5 + s * 2] );
					ic->v[s] = atof( tok[6 + s * 2] );
					ic->size[s] = atof( tok[9] );
					ic->placed[s] = true;
				}
				rows++;
			}
		}
		else if( n >= 7 && UI_CoFPadStyleId( tok[1] ) >= 0 )
		{
			const int s = UI_CoFPadStyleId( tok[1] );
			cofPadIcon_t *ic = UI_CoFPadIconFor( tok[0], true );
			if( ic )
			{
				Q_strncpy( ic->file[s], tok[2], sizeof( ic->file[s] ));
				Q_strncpy( ic->label[s], tok[3], sizeof( ic->label[s] ));
				ic->u[s] = atof( tok[4] );
				ic->v[s] = atof( tok[5] );
				ic->size[s] = atof( tok[6] );
				ic->placed[s] = true;
				styleRows++;
			}
		}
		else if( n > 0 )
			Con_DPrintf( "menu_cofgamepad: icons.txt line not understood (%d fields, \"%s\")\n", n, tok[0] );

		*end = save;
		line = end;
		while( *line == '\n' || *line == '\r' )
			line++;
	}

	EngFuncs::COM_FreeFile( afile );
	Con_DPrintf( "menu_cofgamepad: icons.txt: %d buttons, %d per-style rows, %d style pictures\n", rows, styleRows, styles );
}

// the icon of a key for a style, the Xbox one when the style has none
static const char *UI_CoFPadIconFile( const cofPadIcon_t *ic, int st )
{
	if( !ic )
		return "";
	return ic->file[st][0] ? ic->file[st] : ic->file[0];
}

struct cofPadAction_t
{
	char command[64];
	char label[96];
};

class CMenuCoFGamepad;

// the list with taller rows: a table draws an image cell as tall as a row,
// and a glyph must stay readable (about 34 px at 1080p, 68 at 2160p)
class CMenuCoFPadTable : public CMenuTable
{
public:
	void VidInit() override
	{
		CMenuTable::VidInit();
		m_scChSize = (int)( 24 * uiStatic.scaleY );
	}
};

class CMenuCoFPadModel : public CMenuBaseModel
{
public:
	CMenuCoFPadModel( CMenuCoFGamepad *parent ) : m_pParent( parent ) { }

	void Update() override;
	int GetColumns() const override { return 3; }
	int GetRows() const override { return COF_PAD_ROWS; }
	const char *GetCellText( int line, int column ) override;
	ECellType GetCellType( int line, int column ) override
	{
		return ( column == 0 && g_cofPadRows[line].key ) ? CELL_IMAGE_TRANS : CELL_TEXT;
	}
	bool GetCellColors( int line, int column, unsigned int &textColor, bool &force ) const override;
	bool IsCellTextWrapped( int line, int column ) override { return false; }
	bool IsLineSectionHeader( int line ) const override { return line >= 0 && line < COF_PAD_ROWS && !g_cofPadRows[line].key; }
	void OnActivateEntry( int line ) override;
	void OnDeleteEntry( int line ) override;

	char m_szIcon[COF_PAD_ROWS][96];
	char m_szAction[COF_PAD_ROWS][96];

private:
	CMenuCoFGamepad *m_pParent;
};

class CMenuCoFActionModel : public CMenuBaseArrayModel
{
public:
	CMenuCoFActionModel( CMenuCoFGamepad *parent ) : m_pParent( parent ) { }
	void Update() override { }
	int GetRows() const override;
	const char *GetText( int line ) override;
private:
	CMenuCoFGamepad *m_pParent;
};

// the spin control's rows: Auto, then the styles (row = cof_pad_style + 1)
static const char *g_cofPadStyles[COF_PAD_STYLES + 1] = { "Auto", "Xbox", "PlayStation", "Nintendo", "Steam Deck" };

class CMenuCoFStyleModel : public CMenuBaseArrayModel
{
public:
	void Update() override { }
	int GetRows() const override { return COF_PAD_STYLES + 1; }
	const char *GetText( int line ) override { return L( g_cofPadStyles[( line >= 0 && line <= COF_PAD_STYLES ) ? line : 0] ); }
};

// the controller picture, with the selected row's button marked on it
class CMenuCoFPadPicture : public CMenuBaseItem
{
public:
	CMenuCoFPadPicture() : CMenuBaseItem(), m_pPage( NULL ) { iFlags |= QMF_INACTIVE; }
	void Draw() override;
	CMenuCoFGamepad *m_pPage;
};

class CMenuCoFGamepad : public CMenuFramework
{
public:
	typedef CMenuFramework BaseClass;
	CMenuCoFGamepad() : CMenuFramework( "CMenuCoFGamepad" ), padModel( this ), actionModel( this ),
		m_iNumActions( 0 ), m_bSyncing( false ) { memset( m_bOwnPic, 0, sizeof( m_bOwnPic )); }

	void Show() override;

	int Style( void ) const { return UI_CoFPadStyle(); }
	int CurrentRow( void ) { return padList.GetCurrentIndex(); }
	const char *RowName( int row ) const;
	const cofPadIcon_t *Icon( int row ) const;
	int RowKey( int row ) const { return ( row >= 0 && row < COF_PAD_ROWS ) ? m_iKey[row] : -1; }
	const char *ActionLabel( const char *command ) const;

	void EnterGrabMode( void );
	bool SwapWith( int key );
	void ClearCurrent( void );
	bool ApplyDefaults( void );
	void UseDefaults( void ) { ApplyDefaults(); }
	void OptionsProbe( void );

	int m_iNumActions;
	cofPadAction_t m_actions[COF_PAD_MAX_ACTIONS];	// [0] = not bound
	CImage m_pic[COF_PAD_STYLES];
	char m_szPic[COF_PAD_STYLES][160];	// CImage keeps the pointer
	bool m_bOwnPic[COF_PAD_STYLES];	// the style has its own picture (a STYLE line)

private:
	void _Init( void ) override;
	void _VidInit( void ) override;
	void SaveAndPopMenu( void ) override;

	void GetConfig( void );
	void BuildActions( void );
	void AddAction( const char *command, const char *label );
	void SyncAction( void );
	void ActionChanged( void );
	void StyleChanged( void );
	void Cancel( void );
	void BindKey( int key, const char *command );

	CMenuCoFPadTable padList;
	CMenuCoFPadModel padModel;
	CMenuSpinControl action;
	CMenuCoFActionModel actionModel;
	CMenuSpinControl style;
	CMenuCoFStyleModel styleModel;
	CMenuCoFPadPicture picture;
	CMenuAction keyLine, hint, padNote;
	char m_szKeyLine[96];

	int m_iKey[COF_PAD_ROWS];
	bool m_bSyncing;

	// bindings when the page opened, for Cancel
	char m_szSnapshot[COF_PAD_KEYS][128];

	class CGrabPadBox : public CMenuMessageBox
	{
	public:
		bool KeyUp( int key ) override;
		bool KeyDown( int key ) override { return true; }
	} grabBox;

	CMenuYesNoMessageBox defaultsBox;
};

// ------------------------------------------------------------------ model

void CMenuCoFPadModel::Update()
{
	for( int i = 0; i < COF_PAD_ROWS; i++ )
	{
		m_szIcon[i][0] = m_szAction[i][0] = 0;

		if( !g_cofPadRows[i].key )
			continue;

		const cofPadIcon_t *icon = m_pParent->Icon( i );
		const char *file = UI_CoFPadIconFile( icon, m_pParent->Style( ));
		if( file[0] )
			snprintf( m_szIcon[i], sizeof( m_szIcon[i] ), COF_PAD_DIR "%s", file );

		const int key = m_pParent->RowKey( i );
		const char *b = key >= 0 ? EngFuncs::KEY_GetBinding( key ) : NULL;

		Q_strncpy( m_szAction[i], ( b && b[0] ) ? m_pParent->ActionLabel( b ) : L( "Not bound" ), sizeof( m_szAction[i] ));
	}
}

const char *CMenuCoFPadModel::GetCellText( int line, int column )
{
	if( line < 0 || line >= COF_PAD_ROWS )
		return NULL;

	if( !g_cofPadRows[line].key )
		return column == 0 ? L( g_cofPadRows[line].xbox ) : NULL;

	switch( column )
	{
	case 0: return m_szIcon[line][0] ? m_szIcon[line] : NULL;
	case 1: return m_pParent->RowName( line );
	case 2: return m_szAction[line];
	}
	return NULL;
}

bool CMenuCoFPadModel::GetCellColors( int line, int column, unsigned int &textColor, bool &force ) const
{
	if( column == 0 )
	{
		textColor = line == m_pParent->CurrentRow() ? THEME_ACCENT : THEME_TEXT;
		force = true;
		return true;
	}

	// an unbound button reads dim
	if( column == 2 && g_cofPadRows[line].key )
	{
		const int key = m_pParent->RowKey( line );
		const char *b = key >= 0 ? EngFuncs::KEY_GetBinding( key ) : NULL;

		if( !b || !b[0] )
		{
			textColor = THEME_TEXT_DIM;
			force = true;
			return true;
		}
	}
	return false;
}

void CMenuCoFPadModel::OnActivateEntry( int line )
{
	m_pParent->EnterGrabMode();
}

void CMenuCoFPadModel::OnDeleteEntry( int line )
{
	m_pParent->ClearCurrent();
}

int CMenuCoFActionModel::GetRows() const
{
	return m_pParent->m_iNumActions;
}

const char *CMenuCoFActionModel::GetText( int line )
{
	if( line < 0 || line >= m_pParent->m_iNumActions )
		return "";
	return m_pParent->m_actions[line].label;
}

// ------------------------------------------------------------------ data

// Xbox and PlayStation: the page's own names (translated); Nintendo and
// Steam Deck: the printed name icons.txt gives for that pad where it differs
// from the Xbox one (L, ZR, +, L1, R2 ...), the Xbox row name otherwise (the
// Nintendo face buttons keep the Xbox letters at their physical places)
const char *CMenuCoFGamepad::RowName( int row ) const
{
	if( row < 0 || row >= COF_PAD_ROWS || !g_cofPadRows[row].key )
		return "";

	const int st = Style();
	if( st == 1 )
		return L( g_cofPadRows[row].ps );

	const cofPadIcon_t *icon = Icon( row );
	if( st >= 2 && icon && icon->label[st][0] && stricmp( icon->label[st], icon->label[0] ))
		return L( icon->label[st] );

	return L( g_cofPadRows[row].xbox );
}

const cofPadIcon_t *CMenuCoFGamepad::Icon( int row ) const
{
	if( row < 0 || row >= COF_PAD_ROWS || !g_cofPadRows[row].key )
		return NULL;
	return UI_CoFPadIconFor( g_cofPadRows[row].key, false );
}

const char *CMenuCoFGamepad::ActionLabel( const char *command ) const
{
	for( int i = 1; i < m_iNumActions; i++ )
	{
		if( !stricmp( m_actions[i].command, command ))
			return m_actions[i].label;
	}
	return command;	// a command no list names: shown as it is
}

void CMenuCoFGamepad::AddAction( const char *command, const char *label )
{
	if( !command || !command[0] || m_iNumActions >= COF_PAD_MAX_ACTIONS )
		return;

	for( int i = 1; i < m_iNumActions; i++ )
	{
		if( !stricmp( m_actions[i].command, command ))
			return;
	}

	Q_strncpy( m_actions[m_iNumActions].command, command, sizeof( m_actions[0].command ));
	Q_strncpy( m_actions[m_iNumActions].label, label, sizeof( m_actions[0].label ));
	m_iNumActions++;
}

/*
=================
CMenuCoFGamepad::BuildActions

What a button can be given: every action of the key list
(gfx/shell/kb_act.lst, the Keybinds page's own list), the engine's menu
command, then every command the pad lines of gfx/shell/kb_def.lst and the
current pad bindings use that neither names (shown as the command itself), so
a default layout added to kb_def.lst later shows up here on its own.
=================
*/
void CMenuCoFGamepad::BuildActions( void )
{
	char token[1024];
	char *afile, *pfile;

	m_iNumActions = 0;
	Q_strncpy( m_actions[0].command, "", sizeof( m_actions[0].command ));
	Q_strncpy( m_actions[0].label, L( "Not bound" ), sizeof( m_actions[0].label ));
	m_iNumActions = 1;

	if(( afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/kb_act.lst", NULL )) != NULL )
	{
		char bind[64];

		pfile = afile;
		while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			Q_strncpy( bind, token, sizeof( bind ));
			pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile )
				break;
			if( !stricmp( bind, "blank" ))
				continue;
			// the game's "Tertiary Attack" (+attack3) is its aim down
			// sights (LT by default): "Aim", as on the Keybinds tab
			if( !stricmp( bind, "+attack3" ))
				AddAction( bind, L( "Aim" ));
			else AddAction( bind, token[0] == '#' ? L( token ) : token );
		}
		EngFuncs::COM_FreeFile( afile );
	}

	// the engine's own commands the pad layouts use (gamepad rounds 1-3),
	// in case the list lacks them; the game's own label wins where it has one
	AddAction( "cof_duck_toggle", L( "Crouch (toggle)" ));
	AddAction( "cof_sprint_toggle_press", L( "Sprint (toggle)" ));
	AddAction( "cof_quickturn", L( "Quick turn" ));
	AddAction( "+dodge", L( "Dodge" )); // ALT action; RB default since generation 5
	AddAction( "weapontoggle", L( "Toggle weapon function" ));
	AddAction( "cancelselect", L( "Open the menu" ));

	if(( afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/kb_def.lst", NULL )) != NULL )
	{
		char key[32];

		pfile = afile;
		while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL )
		{
			Q_strncpy( key, token, sizeof( key ));
			pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile )
				break;
			if( UI_CoFIsPadKeyName( key ))
				AddAction( token, token );
		}
		EngFuncs::COM_FreeFile( afile );
	}

	for( int i = 0; i < COF_PAD_ROWS; i++ )
	{
		const char *b = m_iKey[i] >= 0 ? EngFuncs::KEY_GetBinding( m_iKey[i] ) : NULL;
		if( b && b[0] )
			AddAction( b, b );
	}
}

void CMenuCoFGamepad::GetConfig( void )
{
	for( int i = 0; i < COF_PAD_ROWS; i++ )
		m_iKey[i] = g_cofPadRows[i].key ? UI_CoFKeyByName( g_cofPadRows[i].key ) : -1;

	UI_CoFPadIconsLoad( true );	// read again: art copied in while the game runs shows up
	BuildActions();

	for( int s = 0; s < COF_PAD_STYLES; s++ )
	{
		m_bOwnPic[s] = g_cofPadIcons.pic[s][0] != 0;
		snprintf( m_szPic[s], sizeof( m_szPic[s] ), COF_PAD_DIR "%s", m_bOwnPic[s] ? g_cofPadIcons.pic[s] : g_cofPadIcons.pic[0] );
		m_pic[s] = m_szPic[s];
		if( !m_pic[s].Handle( ) && s > 0 )
		{
			// a STYLE line whose picture is not there (yet): the Xbox one
			Con_DPrintf( "menu_cofgamepad: %s not found, the Xbox picture instead\n", m_szPic[s] );
			m_bOwnPic[s] = false;
			snprintf( m_szPic[s], sizeof( m_szPic[s] ), COF_PAD_DIR "%s", g_cofPadIcons.pic[0] );
			m_pic[s] = m_szPic[s];
		}
	}

	m_bSyncing = true;
	action.Setup( &actionModel );
	style.SetCurrentValue( (float)( UI_CoFPadStyleSetting() + 1 ));
	m_bSyncing = false;

	padModel.Update();
	SyncAction();
}

// the action box follows the selected row, without binding anything
void CMenuCoFGamepad::SyncAction( void )
{
	const int row = padList.GetCurrentIndex();
	const int key = RowKey( row );
	const char *b = key >= 0 ? EngFuncs::KEY_GetBinding( key ) : NULL;
	int idx = 0;

	if( b && b[0] )
	{
		for( int i = 1; i < m_iNumActions; i++ )
		{
			if( !stricmp( m_actions[i].command, b ))
			{
				idx = i;
				break;
			}
		}
	}

	m_bSyncing = true;
	action.SetCurrentValue( (float)idx );
	m_bSyncing = false;

	snprintf( m_szKeyLine, sizeof( m_szKeyLine ), L( "Config key: %s" ), row >= 0 && row < COF_PAD_ROWS && g_cofPadRows[row].key ? g_cofPadRows[row].key : "-" );
	keyLine.szName = m_szKeyLine;
}

// straight into the engine's key table, as the "bind" command does: a
// ClientCmd( true, ... ) would run the whole pending command buffer from here
void CMenuCoFGamepad::BindKey( int key, const char *command )
{
	EngFuncs::KEY_SetBinding( key, command ? command : "" );
}

void CMenuCoFGamepad::ActionChanged( void )
{
	if( m_bSyncing )
		return;

	const int row = padList.GetCurrentIndex();
	const int key = RowKey( row );
	const int idx = (int)action.GetCurrentValue();

	if( key < 0 || idx < 0 || idx >= m_iNumActions )
		return;

	BindKey( key, m_actions[idx].command );
	padModel.Update();
}

void CMenuCoFGamepad::StyleChanged( void )
{
	if( m_bSyncing )
		return;

	int st = (int)style.GetCurrentValue() - 1;	// row 0 = Auto = -1
	st = st < -1 ? -1 : st >= COF_PAD_STYLES ? COF_PAD_STYLES - 1 : st;
	EngFuncs::CvarSetValue( "cof_pad_style", (float)st );
	padModel.Update();
	SyncAction();
}

void CMenuCoFGamepad::EnterGrabMode( void )
{
	if( RowKey( padList.GetCurrentIndex( )) < 0 )
	{
		PlayLocalSound( uiStatic.sounds[SND_BUZZ] );
		return;
	}

	grabBox.Show();
	PlayLocalSound( uiStatic.sounds[SND_KEY] );
}

/*
=================
CMenuCoFGamepad::SwapWith

The selected row's button and the pressed one trade actions: remapping by
pressing never leaves a button with a duplicate or a lost action.
=================
*/
bool CMenuCoFGamepad::SwapWith( int key )
{
	const int rowKey = RowKey( padList.GetCurrentIndex( ));
	const char *name = EngFuncs::KeynumToString( key );

	if( rowKey < 0 || !name || !UI_CoFIsPadKeyName( name ))
		return false;

	if( key != rowKey )
	{
		char a[128], b[128];
		const char *ba = EngFuncs::KEY_GetBinding( rowKey );
		const char *bb = EngFuncs::KEY_GetBinding( key );

		Q_strncpy( a, ba ? ba : "", sizeof( a ));
		Q_strncpy( b, bb ? bb : "", sizeof( b ));
		BindKey( rowKey, b );
		BindKey( key, a );
	}

	BuildActions();
	action.Setup( &actionModel );
	padModel.Update();
	SyncAction();
	return true;
}

void CMenuCoFGamepad::ClearCurrent( void )
{
	const int key = RowKey( padList.GetCurrentIndex( ));

	if( key < 0 )
	{
		PlayLocalSound( uiStatic.sounds[SND_BUZZ] );
		return;
	}

	BindKey( key, "" );
	padModel.Update();
	SyncAction();
	PlayLocalSound( uiStatic.sounds[SND_REMOVEKEY] );
}

bool CMenuCoFGamepad::CGrabPadBox::KeyUp( int key )
{
	CMenuCoFGamepad *parent = (CMenuCoFGamepad *)m_pParent;
	// Escape (the keyboard key) cancels; B and every other pad button is a
	// button to swap with, so it cannot be the way out
	const bool ok = key != K_ESCAPE && !UI::Key::IsConsole( key ) && parent->SwapWith( key );

	Hide();
	PlayLocalSound( uiStatic.sounds[ok ? SND_LAUNCH : SND_BUZZ] );
	return true;
}

/*
=================
CMenuCoFGamepad::ApplyDefaults

"Use defaults" for the pad only: the pad lines of gfx/shell/kb_def.lst (the
file the Keybinds page's own "Use defaults" applies, with every key), after
clearing every pad button. A kb_def.lst without pad lines changes nothing.
=================
*/
bool CMenuCoFGamepad::ApplyDefaults( void )
{
	char *afile = (char *)EngFuncs::COM_LoadFile( "gfx/shell/kb_def.lst", NULL );
	char key[32], token[1024];
	struct { char key[32]; char cmd[128]; } lines[64];
	int n = 0;

	if( afile )
	{
		char *pfile = afile;
		while(( pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ))) != NULL && n < (int)V_ARRAYSIZE( lines ))
		{
			Q_strncpy( key, token, sizeof( key ));
			pfile = EngFuncs::COM_ParseFile( pfile, token, sizeof( token ));
			if( !pfile )
				break;
			if( !UI_CoFIsPadKeyName( key ))
				continue;
			Q_strncpy( lines[n].key, key, sizeof( lines[n].key ));
			Q_strncpy( lines[n].cmd, token, sizeof( lines[n].cmd ));
			n++;
		}
		EngFuncs::COM_FreeFile( afile );
	}

	if( !n )
	{
		Con_Printf( "menu_cofgamepad: gfx/shell/kb_def.lst has no gamepad lines, nothing changed\n" );
		UI_ShowMessageBox( L( "The default key list has no gamepad buttons yet." ));
		return false;
	}

	for( int i = 0; i < COF_PAD_KEYS; i++ )
	{
		const int k = UI_CoFKeyByName( g_cofPadKeys[i] );
		if( k >= 0 )
			BindKey( k, "" );
	}

	for( int i = 0; i < n; i++ )
	{
		const int k = UI_CoFKeyByName( lines[i].key );
		if( k >= 0 )
			BindKey( k, lines[i].cmd );
	}

	Con_Printf( "menu_cofgamepad: %d gamepad defaults applied from gfx/shell/kb_def.lst\n", n );

	BuildActions();
	action.Setup( &actionModel );
	padModel.Update();
	SyncAction();
	return true;
}

void CMenuCoFGamepad::Show( void )
{
	// what Cancel goes back to
	for( int i = 0; i < COF_PAD_KEYS; i++ )
	{
		const int k = UI_CoFKeyByName( g_cofPadKeys[i] );
		const char *b = k >= 0 ? EngFuncs::KEY_GetBinding( k ) : NULL;

		Q_strncpy( m_szSnapshot[i], b ? b : "", sizeof( m_szSnapshot[i] ));
	}

	BaseClass::Show();
}

void CMenuCoFGamepad::Cancel( void )
{
	for( int i = 0; i < COF_PAD_KEYS; i++ )
	{
		const int k = UI_CoFKeyByName( g_cofPadKeys[i] );
		const char *b = k >= 0 ? EngFuncs::KEY_GetBinding( k ) : NULL;

		if( k >= 0 && strcmp( b ? b : "", m_szSnapshot[i] ))
			BindKey( k, m_szSnapshot[i] );
	}
	Hide();
}

void CMenuCoFGamepad::SaveAndPopMenu( void )
{
	// the bindings are live already; this writes config.cfg now
	CMenuFramework::SaveAndPopMenu();
}

void CMenuCoFGamepad::_Init( void )
{
	SetPanel( "GAMEPAD CONTROLS", 900, 640 );

	// cof_pad_style is registered once, in UI_CoFGamepad_Precache (before
	// config.cfg runs): registering an existing cvar again sets it back to
	// its default (engine Cvar_Get -> Cvar_DirectSet)

	padList.SetModel( &padModel );
	padList.SetupColumn( 0, NULL, 34.0f, true );
	padList.SetupColumn( 1, L( "Button" ), 0.46f );
	padList.SetupColumn( 2, L( "GameUI_Action" ), 0.54f );
	padList.onChanged = VoidCb( &CMenuCoFGamepad::SyncAction );

	action.szName = L( "Action for the selected button" );
	action.Setup( &actionModel );
	action.SetCharSize( QM_SMALLFONT );
	action.iFlags |= QMF_NOTIFY;
	action.szStatusText = L( "The action this button performs in the game" );
	action.onChanged = VoidCb( &CMenuCoFGamepad::ActionChanged );

	style.szName = L( "Button style" );
	style.Setup( &styleModel );
	style.SetCharSize( QM_SMALLFONT );
	style.iFlags |= QMF_NOTIFY;
	style.szStatusText = L( "Auto follows the connected controller; or the button names and pictures of an Xbox, PlayStation, Nintendo or Steam Deck controller" );
	style.onChanged = VoidCb( &CMenuCoFGamepad::StyleChanged );

	picture.m_pPage = this;

	m_szKeyLine[0] = 0;
	keyLine.iFlags = QMF_INACTIVE;
	keyLine.colorBase = uiColorHelp;
	keyLine.SetCharSize( QM_SMALLFONT );
	keyLine.szName = m_szKeyLine;

	hint.iFlags = QMF_INACTIVE;
	hint.colorBase = uiColorHelp;
	hint.SetCharSize( QM_SMALLFONT );
	hint.szName = L( "Pick a button, then its action below. Or press Enter on a button and then another gamepad button to swap the two." );

	padNote.iFlags = QMF_INACTIVE;
	padNote.colorBase = uiColorHelp;
	padNote.SetCharSize( QM_SMALLFONT );
	padNote.szName = L( "In the menus: A confirm \xC2\xB7 B back \xC2\xB7 LB/RB tabs \xC2\xB7 X use defaults \xC2\xB7 Start resume" );

	grabBox.SetMessage( L( "Press the gamepad button to swap with this one" ));
	grabBox.Link( this );

	defaultsBox.SetMessage( L( "Reset every gamepad button to the default layout?" ));
	defaultsBox.onPositive = VoidCb( &CMenuCoFGamepad::UseDefaults );
	defaultsBox.Link( this );

	pPadSecondary = AddButton( L( "GameUI_UseDefaults" ), nullptr, PC_USE_DEFAULTS, defaultsBox.MakeOpenEvent( ));
	AddButton( L( "GameUI_Cancel" ), nullptr, PC_CANCEL, VoidCb( &CMenuCoFGamepad::Cancel ));
	AddButton( L( "GameUI_OK" ), nullptr, PC_OK, VoidCb( &CMenuCoFGamepad::SaveAndPopMenu ));

	// pad / arrow-key order: the list, its action, then the button style
	AddItem( picture );
	AddItem( padList );
	AddItem( action );
	AddItem( style );
	AddItem( keyLine );
	AddItem( hint );
	AddItem( padNote );
}

void CMenuCoFGamepad::_VidInit( void )
{
	Rect left = ColumnRect2( 0, 0.40f );
	Rect right = ColumnRect2( 1, 0.40f );
	int y = left.pt.y;

	picture.SetRect( left.pt.x, y, left.sz.w, 290 );
	y += 290 + THEME_LABEL_TALL + 16;
	style.SetRect( left.pt.x, y, left.sz.w, THEME_CTRL_H );
	y += THEME_CTRL_H + 12;
	keyLine.SetRect( left.pt.x, y, left.sz.w, THEME_LABEL_TALL + 4 );
	y += THEME_LABEL_TALL + 14;
	hint.SetRect( left.pt.x, y, left.sz.w, 3 * THEME_LABEL_TALL + 12 );
	y += 3 * THEME_LABEL_TALL + 20;
	padNote.SetRect( left.pt.x, y, left.sz.w, left.pt.y + left.sz.h - y );

	const int actionBlock = THEME_LABEL_TALL + 16 + THEME_CTRL_H;
	padList.SetRect( right.pt.x, right.pt.y, right.sz.w, right.sz.h - actionBlock - 8 );
	action.SetRect( right.pt.x, right.pt.y + right.sz.h - THEME_CTRL_H, right.sz.w, THEME_CTRL_H );

	grabBox.szPanelTitle = "PRESS A BUTTON";

	GetConfig();

	const int cur = padList.GetCurrentIndex();
	if( cur < 0 || cur >= COF_PAD_ROWS || !g_cofPadRows[cur].key )
		padList.SetCurrentIndex( 0 );	// skips the caption
	SyncAction();
}

/*
=================
CMenuCoFPadPicture::Draw

The pad drawn dim in the theme's grey, fitted into the item's rect below a
band kept free for the trigger markers (LT/RT sit above the shoulders), and
the selected row's button marked with its own icon in the accent colour.
=================
*/
void CMenuCoFPadPicture::Draw( void )
{
	if( !m_pPage )
		return;

	const int st = m_pPage->Style();
	CImage &pic = m_pPage->m_pic[st];

	if( !pic.Handle( ))
		return;

	const Size isz = EngFuncs::PIC_Size( pic.Handle( ));
	if( isz.w <= 0 || isz.h <= 0 )
		return;

	const int band = m_scSize.h / 8;
	Size sz;
	sz.w = m_scSize.w;
	sz.h = (int)((float)sz.w * isz.h / isz.w );
	if( sz.h > m_scSize.h - band )
	{
		sz.h = m_scSize.h - band;
		sz.w = (int)((float)sz.h * isz.w / isz.h );
	}

	Point pt( m_scPos.x + ( m_scSize.w - sz.w ) / 2, m_scPos.y + band );

	UI_DrawPic( pt, sz, THEME_TEXT_DIM, pic, QM_DRAWTRANS );

	// a style with its own picture marks the keys placed on it; one that
	// shows the Xbox picture uses the Xbox positions and icon
	const cofPadIcon_t *icon = m_pPage->Icon( m_pPage->CurrentRow( ));
	const int ps = m_pPage->m_bOwnPic[st] ? st : 0;
	if( !icon || !icon->placed[ps] || icon->size[ps] <= 0.0f )
		return;

	char path[128];
	snprintf( path, sizeof( path ), COF_PAD_DIR "%s", UI_CoFPadIconFile( icon, ps ? st : 0 ));

	CImage marker = path;
	if( !marker.Handle( ))
		return;

	const int ms = (int)( icon->size[ps] * sz.w );
	const int cx = pt.x + (int)( icon->u[ps] * sz.w );
	const int cy = pt.y + (int)( icon->v[ps] * sz.h );

	UI_DrawPic( Point( cx - ms / 2, cy - ms / 2 ), Size( ms, ms ), THEME_ACCENT, marker, QM_DRAWTRANS );
}

/*
=================
CMenuCoFGamepad::OptionsProbe

menu_cof_options_select gamepad
    [<KEY> [<command> | <action index>]]  select a row; set its action
    [press <KEY> <OTHER KEY>]             the grab box: swap two buttons
    [clear <KEY>]                         Delete on a row
    [style auto|-1|0|1|2|3] [defaults] [ok] [cancel]
Without arguments: every row with its binding.
=================
*/
void CMenuCoFGamepad::OptionsProbe( void )
{
	// copies: the engine's argument buffer belongs to whatever command runs next
	const int argc = EngFuncs::CmdArgc();
	char a1[64], a2[128], a3[64];
	Q_strncpy( a1, argc > 2 ? EngFuncs::CmdArgv( 2 ) : "", sizeof( a1 ));
	Q_strncpy( a2, argc > 3 ? EngFuncs::CmdArgv( 3 ) : "", sizeof( a2 ));
	Q_strncpy( a3, argc > 4 ? EngFuncs::CmdArgv( 4 ) : "", sizeof( a3 ));

	auto selectRow = [this]( const char *keyName ) -> bool
	{
		for( int i = 0; i < COF_PAD_ROWS; i++ )
		{
			if( g_cofPadRows[i].key && !stricmp( g_cofPadRows[i].key, keyName ))
			{
				padList.SetCurrentIndex( i );
				SyncAction();
				return true;
			}
		}
		Con_Printf( "menu_cof_options_select gamepad: no row \"%s\"\n", keyName );
		return false;
	};

	if( !stricmp( a1, "ok" ))
	{
		Con_Printf( "menu_cof_options_select gamepad: OK\n" );
		SaveAndPopMenu();
		return;
	}
	if( !stricmp( a1, "cancel" ))
	{
		Con_Printf( "menu_cof_options_select gamepad: Cancel\n" );
		Cancel();
		return;
	}
	if( !stricmp( a1, "defaults" ))
	{
		Con_Printf( "menu_cof_options_select gamepad: Use defaults -> %s\n", ApplyDefaults() ? "applied" : "nothing to apply" );
	}
	else if( !stricmp( a1, "style" ))
	{
		const int st = !stricmp( a2, "auto" ) ? -1 : atoi( a2 );
		style.SetCurrentValue( (float)(( st < -1 ? -1 : st >= COF_PAD_STYLES ? COF_PAD_STYLES - 1 : st ) + 1 ));
		Con_Printf( "menu_cof_options_select gamepad: style %s (cof_pad_style \"%s\")\n", style.GetCurrentString(), EngFuncs::GetCvarString( "cof_pad_style" ));
	}
	else if( !stricmp( a1, "press" ))
	{
		if( selectRow( a2 ))
		{
			const int k = UI_CoFKeyByName( a3 );
			Con_Printf( "menu_cof_options_select gamepad: press %s on %s -> %s\n", a3, a2,
				k >= 0 && SwapWith( k ) ? "swapped" : "refused" );
		}
	}
	else if( !stricmp( a1, "clear" ))
	{
		if( selectRow( a2 ))
			ClearCurrent();
	}
	else if( a1[0] )
	{
		if( selectRow( a1 ) && a2[0] )
		{
			int idx = -1;
			for( int i = 0; i < m_iNumActions && idx < 0; i++ )
			{
				if( !stricmp( m_actions[i].command, a2 ))
					idx = i;
			}
			if( idx < 0 && a2[0] >= '0' && a2[0] <= '9' )
				idx = atoi( a2 );
			if( idx < 0 || idx >= m_iNumActions )
				Con_Printf( "menu_cof_options_select gamepad: no action \"%s\"\n", a2 );
			else
				action.SetCurrentValue( (float)idx );	// raises QM_CHANGED -> ActionChanged
		}
	}

	for( int i = 0; i < COF_PAD_ROWS; i++ )
	{
		if( !g_cofPadRows[i].key )
			continue;
		const char *b = m_iKey[i] >= 0 ? EngFuncs::KEY_GetBinding( m_iKey[i] ) : NULL;
		Con_Printf( "menu_cof_options_select gamepad: row %d %s \"%s\" = \"%s\" (%s)%s\n", i, g_cofPadRows[i].key, RowName( i ),
			b ? b : "", padModel.m_szAction[i], i == padList.GetCurrentIndex() ? " (selected)" : "" );
	}
	Con_Printf( "menu_cof_options_select gamepad: %d actions, action box \"%s\", %s\n", m_iNumActions, action.GetCurrentString(), m_szKeyLine );
}

static CMenuCoFGamepad *menu_cofgamepad = NULL;

static void UI_CoFGamepad_Precache( void )
{
	UI_CoFPadStyleRegister();	// as early as the menu can, before config.cfg if possible
	menu_cofgamepad = new CMenuCoFGamepad();
}

static void UI_CoFGamepad_Shutdown( void )
{
	delete menu_cofgamepad;
	menu_cofgamepad = NULL;
}

void UI_CoFGamepad_Menu( void )
{
	menu_cofgamepad->Show();
}

ADD_MENU4( menu_cofgamepad, UI_CoFGamepad_Precache, UI_CoFGamepad_Menu, UI_CoFGamepad_Shutdown );

// ---------------------------------------------------------------------------
// tabbed windows
// ---------------------------------------------------------------------------

static bool UI_CoFSaveTabAvailable( void )
{
	// the pause menu's own rule for Save Game: in play, single player, with
	// pause-menu saves switched on (menus/Main.cpp)
	return CL_IsActive() && EngFuncs::GetCvarFloat( "maxplayers" ) <= 1.0f &&
		EngFuncs::GetCvarFloat( "cof_pause_menu_saves" ) != 0.0f;
}

static const cofTab_t g_cofOptionsTabs[] =
{
	{ "Game",         "GAME",             UI_AdvControls_Menu, NULL },
	{ "Controls",     "CONTROLS",         UI_CoFControls_Menu, NULL },
	{ "Keybinds",     "KEYBINDS",         UI_Controls_Menu,    NULL },
	{ "Gamepad",      "GAMEPAD CONTROLS", UI_CoFGamepad_Menu,  NULL },
	{ "GameUI_Audio", "AUDIO",            UI_Audio_Menu,       NULL },
	{ "GameUI_Video", "VIDEO",            UI_Video_Menu,       NULL },
};

// 1.3.6: Host co-op and Join co-op moved to the CO-OP window below
static const cofTab_t g_cofExtrasTabs[] =
{
	{ "Unlockables",  "UNLOCKABLES",  UI_CoFUnlockables_Menu, NULL },
	{ "Links",        "EXTRAS",       UI_CoFExtras_Menu,      NULL },
	{ "Credits",      "CREDITS",      UI_CoFCredits_Menu,     NULL },
};

static const cofTab_t g_cofSaveLoadTabs[] =
{
	{ "Load Game", "LOAD GAME", UI_LoadGame_Menu, NULL },
	{ "Save Game", "SAVE GAME", UI_SaveGame_Menu, UI_CoFSaveTabAvailable },
};

// 1.3.6: the main list's Co-op (menus/CoFStoryCoop.cpp, menus/CoFCoop.cpp), in the order of its own list
static const cofTab_t g_cofCoopTabs[] =
{
	{ "Host co-op",     "HOST CO-OP",     UI_CoFStoryHost_Menu,     NULL },
	{ "Join co-op",     "JOIN CO-OP",     UI_CoFJoin_Menu,          NULL },
	{ "Continue",       "CONTINUE CO-OP", UI_CoFStoryContinue_Menu, NULL },
	{ "Play with AI",   "PLAY WITH AI",   UI_CoFStorySolo_Menu,     NULL },
	{ "Original co-op", "ORIGINAL CO-OP", UI_CoFHost_Menu,          NULL },
};

static const cofTabGroup_t g_cofTabGroups[] =
{
	{ "OPTIONS",     g_cofOptionsTabs,  (int)V_ARRAYSIZE( g_cofOptionsTabs ) },
	{ "EXTRAS",      g_cofExtrasTabs,   (int)V_ARRAYSIZE( g_cofExtrasTabs ) },
	{ "CO-OP",       g_cofCoopTabs,     (int)V_ARRAYSIZE( g_cofCoopTabs ) },
	{ "SAVE / LOAD", g_cofSaveLoadTabs, (int)V_ARRAYSIZE( g_cofSaveLoadTabs ) },
};

const cofTabGroup_t *UI_CoFTabGroup( const char *panelTitle, int *index )
{
	if( !panelTitle || !UI_CoFOptionsLayout( ))
		return NULL;

	for( size_t g = 0; g < V_ARRAYSIZE( g_cofTabGroups ); g++ )
	{
		for( int i = 0; i < g_cofTabGroups[g].count; i++ )
		{
			if( !stricmp( g_cofTabGroups[g].tabs[i].title, panelTitle ))
			{
				if( index )
					*index = i;
				return &g_cofTabGroups[g];
			}
		}
	}
	return NULL;
}

bool UI_CoFTabAvailable( const cofTabGroup_t *group, int index )
{
	if( !group || index < 0 || index >= group->count )
		return false;
	return !group->tabs[index].available || group->tabs[index].available();
}

// ---------------------------------------------------------------------------
// pad input
// ---------------------------------------------------------------------------

static bool g_cofPadInput = false;

bool UI_CoFPadMode( void )
{
	// the engine's own record, when it keeps one (gamepad round)
	const char *s = EngFuncs::GetCvarString( "cof_last_input" );

	if( s && s[0] )
		return !stricmp( s, "pad" ) || !stricmp( s, "gamepad" ) || !stricmp( s, "joy" ) || !strcmp( s, "1" );

	return g_cofPadInput;
}

void UI_CoFInputMouse( void )
{
	g_cofPadInput = false;
	UI_CoFOsk_NoteMouse();	// menus/CoFOsk.cpp
}

bool UI_CoFInputEvent( int key, int down )
{
	if( !down )
		return false;

	const char *name = EngFuncs::KeynumToString( key );
	const bool pad = name && UI_CoFIsPadKeyName( name );

	// the engine keeps the record (cof_last_input, gamepad round 2): a gamepad
	// key that reaches the menu is the gamepad, whoever delivered it
	if( pad )
	{
		const char *last = EngFuncs::GetCvarString( "cof_last_input" );

		if( last && last[0] && stricmp( last, "pad" ))
			EngFuncs::CvarSetString( "cof_last_input", "pad" );
	}

	if( pad )
		g_cofPadInput = true;
	else if( key != K_MWHEELUP && key != K_MWHEELDOWN )
		g_cofPadInput = false;

	// the on-screen keyboard opens on a field the pad's direction keys moved to
	UI_CoFOsk_NoteKey( key, down );

	// Start resumes the game from any Cry of Fear menu page (a root window,
	// so never from inside a dialog or a key-capture box), like Resume game;
	// the death page has no game to go back to
	if( key == K_START_BUTTON && UI_CoFOptionsLayout() && CL_IsActive( ))
	{
		CMenuBaseWindow *cur = uiStatic.menu.Current();

		if( cur && cur->IsRoot() && ( !cur->szName || strcmp( cur->szName, "CMenuCoFDeath" )))
		{
			UI_CloseMenu();
			return true;
		}
	}
	return false;
}

const char *UI_CoFPadGlyph( const char *xboxFile, const char *psFile )
{
	static char path[16][96];	// the hints line holds up to six at once, the keyboard's eight
	static int n;
	const int st = UI_CoFPadStyle();

	n = ( n + 1 ) & 15;
	if( st <= 1 )
	{
		snprintf( path[n], sizeof( path[n] ), COF_PAD_DIR "%s/%s", st ? "ps5" : "xbox", st ? psFile : xboxFile );
		return path[n];
	}

	// Nintendo, Steam Deck: the style's own icon for the key whose Xbox icon
	// this is (icons.txt), else the Xbox icon
	char xbox[64];
	snprintf( xbox, sizeof( xbox ), "xbox/%s", xboxFile );
	UI_CoFPadIconsLoad( false );

	const char *file = xbox;
	for( int i = 0; i < g_cofPadIcons.count; i++ )
	{
		if( !stricmp( g_cofPadIcons.icon[i].file[0], xbox ))
		{
			file = UI_CoFPadIconFile( &g_cofPadIcons.icon[i], st );
			break;
		}
	}
	snprintf( path[n], sizeof( path[n] ), COF_PAD_DIR "%s", file );
	return path[n];
}

// ---------------------------------------------------------------------------
// menu_cof_key <KEY> [down|up]
// ---------------------------------------------------------------------------

/*
=================
UI_CoFKey

The pad (and keyboard) path of the menu without a pad: the engine hands every
key it gets in menu mode to UI_KeyEvent (engine/client/input/in_keys.c,
Key_Event -> UI_KeyEvent), and this cfg-only command calls the same function
with the same key number, press then release unless "down" or "up" is given.
An in-process menu event, not input injection: nothing reaches the OS, the
engine or the game.
=================
*/
static void UI_CoFKey( void )
{
	if( EngFuncs::CmdArgc() < 2 )
	{
		Con_Printf( "usage: menu_cof_key <key name> [down|up]   (e.g. A_BUTTON, DPAD_DOWN, L1_BUTTON, ESCAPE)\n" );
		return;
	}

	char name[32], mode[8];
	Q_strncpy( name, EngFuncs::CmdArgv( 1 ), sizeof( name ));
	Q_strncpy( mode, EngFuncs::CmdArgc() > 2 ? EngFuncs::CmdArgv( 2 ) : "", sizeof( mode ));

	const int key = UI_CoFKeyByName( name );
	if( key < 0 )
	{
		Con_Printf( "menu_cof_key: no key \"%s\"\n", name );
		return;
	}

	if( stricmp( mode, "up" ))
		UI_KeyEvent( key, 1 );
	if( stricmp( mode, "down" ))
		UI_KeyEvent( key, 0 );

	CMenuBaseWindow *cur = uiStatic.menu.Current();
	CMenuBaseItem *item = cur ? cur->ItemAtCursor() : NULL;

	Con_Printf( "menu_cof_key %s: window %s, focus %s \"%s\"%s\n", name,
		cur && cur->szName ? cur->szName : "-",
		item ? "on" : "none", item && item->szName ? item->szName : "",
		item && FBitSet( item->iFlags, QMF_HASKEYBOARDFOCUS ) ? " (shown)" : "" );
}

ADD_COMMAND( menu_cof_key, UI_CoFKey );

/*
=================
menu_cof_nav

Prints the focus graph of the window on top: every control the pad can
focus, its place in virtual units, and where up / down / left / right take
the focus from it (controls/ItemsHolder.cpp, UI_CoFNavTarget). A test hook
that reads the layout only.
=================
*/
static const char *UI_CoFNavName( CMenuBaseItem *item, int index )
{
	static char buf[8][80];
	static int n;

	n = ( n + 1 ) & 7;
	if( !item )
		snprintf( buf[n], sizeof( buf[n] ), "-" );
	else if( item->szName && item->szName[0] )
		snprintf( buf[n], sizeof( buf[n] ), "#%d \"%s\"", index, item->szName );
	else
		snprintf( buf[n], sizeof( buf[n] ), "#%d", index );
	return buf[n];
}

static void UI_CoFNav( void )
{
	CMenuBaseWindow *win = uiStatic.menu.Current();

	if( !win )
	{
		Con_Printf( "menu_cof_nav: no menu window\n" );
		return;
	}

	static const int keys[4] = { K_UPARROW, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW };
	const float sx = uiStatic.scaleX > 0.0f ? uiStatic.scaleX : 1.0f;
	const float sy = uiStatic.scaleY > 0.0f ? uiStatic.scaleY : 1.0f;

	Con_Printf( "menu_cof_nav: window %s, %d items, focus %s\n", win->szName ? win->szName : "-", win->ItemCount(),
		UI_CoFNavName( win->ItemAtCursor(), win->GetCursor( )));

	for( int i = 0; i < win->ItemCount(); i++ )
	{
		CMenuBaseItem *item = win->GetItemByIndex( i );
		if( !UI_CoFNavUsable( item ))
			continue;

		int t[4];
		for( int k = 0; k < 4; k++ )
			t[k] = UI_CoFNavTarget( win, i, keys[k], true );

		Con_Printf( "menu_cof_nav: %s at %d,%d %dx%d: up %s, down %s, left %s, right %s\n",
			UI_CoFNavName( item, i ),
			(int)(( item->GetRenderPosition().x - win->GetRenderPosition().x ) / sx ),
			(int)(( item->GetRenderPosition().y - win->GetRenderPosition().y ) / sy ),
			(int)( item->GetRenderSize().w / sx ), (int)( item->GetRenderSize().h / sy ),
			t[0] >= 0 ? UI_CoFNavName( win->GetItemByIndex( t[0] ), t[0] ) : "-",
			t[1] >= 0 ? UI_CoFNavName( win->GetItemByIndex( t[1] ), t[1] ) : "-",
			t[2] >= 0 ? UI_CoFNavName( win->GetItemByIndex( t[2] ), t[2] ) : "-",
			t[3] >= 0 ? UI_CoFNavName( win->GetItemByIndex( t[3] ), t[3] ) : "-" );
	}
}

ADD_COMMAND( menu_cof_nav, UI_CoFNav );

// ---------------------------------------------------------------------------
// menu_cof_options_select <page> [...]
// ---------------------------------------------------------------------------

/*
=================
UI_CoFOptionsSelect

This project's tests may not inject a click or a key (docs/dev/testing.md),
so this cfg-only hook opens an options page and changes a control exactly as
a click does - the control's own value, then the same QM_CHANGED event - or
presses the page's buttons:

  menu_cof_options_select options
  menu_cof_options_select game     [<item> <0|1> | hudscale <row> | close]
  menu_cof_options_select controls [<item> <value> | ads <0|1> | close | cancel]
  menu_cof_options_select keybinds [<command> [<KEY>]]
  menu_cof_options_select gamepad  [<KEY> [<command>] | press <KEY> <KEY> |
                                    clear <KEY> | style <0|1> | defaults | ok | cancel]

Each prints what it did and the cvars or bindings behind it. Cry of Fear with
the theme only.
=================
*/
static void UI_CoFOptionsSelect( void )
{
	const char *page = EngFuncs::CmdArgc() > 1 ? EngFuncs::CmdArgv( 1 ) : "";

	if( !UI_CoFOptionsLayout( ))
	{
		Con_Printf( "menu_cof_options_select: Cry of Fear with ui_theme 1 only\n" );
		return;
	}

	if( !stricmp( page, "options" ))
	{
		UI_Options_Menu();
		Con_Printf( "menu_cof_options_select: Options opened on its Game tab\n" );
	}
	else if( !stricmp( page, "game" ))
		UI_AdvControls_OptionsProbe();
	else if( !stricmp( page, "controls" ))
	{
		if( menu_cofcontrols->WindowStack()->Current() != menu_cofcontrols )
			UI_CoFControls_Menu();
		menu_cofcontrols->OptionsProbe();
	}
	else if( !stricmp( page, "keybinds" ))
		UI_Controls_OptionsProbe();
	else if( !stricmp( page, "gamepad" ))
	{
		if( menu_cofgamepad->WindowStack()->Current() != menu_cofgamepad )
			UI_CoFGamepad_Menu();
		menu_cofgamepad->OptionsProbe();
	}
	else
		Con_Printf( "usage: menu_cof_options_select <options|game|controls|keybinds|gamepad> [...]\n" );
}

ADD_COMMAND( menu_cof_options_select, UI_CoFOptionsSelect );
