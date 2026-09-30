/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Framework.h"
#include "Slider.h"
#include "Bitmap.h"
#include "PicButton.h"
#include "CheckBox.h"
#include "SpinControl.h"
#include "StringArrayModel.h"
#include "CryOfFear.h"

#define ART_BANNER			"gfx/shell/head_audio"

class CMenuAudio : public CMenuFramework
{
public:
	typedef CMenuFramework BaseClass;

	CMenuAudio() : CMenuFramework("CMenuAudio") { }

private:
	void _Init() override;
	void _VidInit() override;
	void GetConfig();
	void VibrateChanged();
	void SaveAndPopMenu() override;

	CMenuSlider	soundVolume;
	CMenuSlider	musicVolume;
	CMenuSlider	menuVolume;
	CMenuSlider	suitVolume;
	CMenuSlider	vibration;
	CMenuCheckBox noDSP;
	CMenuCheckBox useAlphaDSP;
	CMenuCheckBox muteFocusLost;
	CMenuCheckBox vibrationEnable;

	float oldVibrate;
};

/*
=================
CMenuAudio::GetConfig
=================
*/
void CMenuAudio::GetConfig( void )
{
	soundVolume.LinkCvar( "volume" );
	musicVolume.LinkCvar( "MP3Volume" );
	// the engine cvar that scales the menu library's own sounds and nothing
	// else (engine/client/dll_int/cl_gameui.c, pfnPlaySound)
	menuVolume.LinkCvar( "ui_sound_volume" );
	suitVolume.LinkCvar( "suitvolume" );
	vibration.LinkCvar( "vibration_length" );

	noDSP.LinkCvar( "room_off" );
	useAlphaDSP.LinkCvar( "dsp_coeff_table" );
	muteFocusLost.LinkCvar( "snd_mute_losefocus" );
	vibrationEnable.LinkCvar( "vibration_enable" );

	if( !vibrationEnable.bChecked )
		vibration.SetGrayed( true );
	oldVibrate = vibration.GetCurrentValue();
}

void CMenuAudio::VibrateChanged()
{
	float newVibrate = vibration.GetCurrentValue();
	if( oldVibrate != newVibrate )
	{
		char cmd[64];
		snprintf( cmd, 64, "vibrate %f", newVibrate );
		EngFuncs::ClientCmd( false, cmd );
		vibration.WriteCvar();
		oldVibrate = newVibrate;
	}
}

/*
=================
CMenuAudio::SetConfig
=================
*/
void CMenuAudio::SaveAndPopMenu()
{
	soundVolume.WriteCvar();
	musicVolume.WriteCvar();
	if( menuVolume.IsVisible( ))
		menuVolume.WriteCvar();
	suitVolume.WriteCvar();
	vibration.WriteCvar();
	noDSP.WriteCvar();
	useAlphaDSP.WriteCvar();
	muteFocusLost.WriteCvar();
	vibrationEnable.WriteCvar();

	CMenuFramework::SaveAndPopMenu();
}

/*
=================
CMenuAudio::Init
=================
*/
void CMenuAudio::_Init( void )
{
	banner.SetPicture(ART_BANNER);

	if( UI_ThemeActive( ))
		SetPanel( "AUDIO", 640, 460 );

	soundVolume.szName = L( "GameUI_SoundEffectVolume" );
	soundVolume.Setup( 0.0, 1.0, 0.05f );
	soundVolume.onChanged = CMenuEditable::WriteCvarCb;
	soundVolume.SetCoord( 320, 280 );
	soundVolume.size.w = 300;

	musicVolume.szName = L( "GameUI_MP3Volume" );
	musicVolume.Setup( 0.0, 1.0, 0.05f );
	musicVolume.onChanged = CMenuEditable::WriteCvarCb;
	musicVolume.SetCoord( 320, 340 );
	musicVolume.size.w = 300;

	// Menu sounds. Only in theme mode: the WON layout is a grid of fixed
	// coordinates and adding a row to it would move controls the ui_theme 0
	// path promises to leave exactly as they were.
	menuVolume.szName = L( "Menu sounds" );
	menuVolume.szStatusText = L( "Volume of the menu's own button and rollover sounds" );
	menuVolume.Setup( 0.0, 1.0, 0.05f );
	menuVolume.onChanged = CMenuEditable::WriteCvarCb;
	menuVolume.SetCoord( 320, 400 );
	menuVolume.size.w = 300;
	menuVolume.SetVisibility( UI_ThemeActive( ));

	suitVolume.szName = L( "GameUI_HEVSuitVolume" );
	suitVolume.Setup( 0.0, 1.0, 0.05f );
	suitVolume.onChanged = CMenuEditable::WriteCvarCb;
	suitVolume.SetCoord( 320, 400 );
	suitVolume.size.w = 300;

	noDSP.szName = L( "Disable DSP effects" );
	noDSP.onChanged = CMenuEditable::WriteCvarCb;
	noDSP.SetCoord( 320, 460 );

	useAlphaDSP.szName = L( "Use Alpha DSP effects" );
	useAlphaDSP.onChanged = CMenuEditable::WriteCvarCb;
	useAlphaDSP.SetCoord( 320, 510 );

	muteFocusLost.szName = L( "Mute when inactive" );
	muteFocusLost.onChanged = CMenuEditable::WriteCvarCb;
	muteFocusLost.SetCoord( 320, 560 );

	vibrationEnable.szName = L( "Enable vibration" );
	vibrationEnable.iMask = (QMF_GRAYED|QMF_INACTIVE);
	vibrationEnable.bInvertMask = true;
	vibrationEnable.onChanged = CMenuCheckBox::BitMaskCb;
	vibrationEnable.onChanged.pExtra = &vibration.iFlags;
	vibrationEnable.SetCoord( 700, 460 );

	vibration.szName = L( "Vibration" );
	vibration.Setup( 0.0f, 5.0f, 0.05f );
	vibration.onChanged = VoidCb( &CMenuAudio::VibrateChanged );
	vibration.SetCoord( 700, 560 );

	if( !UI_ThemeActive( ))
		AddItem( banner );
	AddButton( UI_ThemeActive() ? L( "Close" ) : L( "Done" ), nullptr, PC_DONE,
		VoidCb( &CMenuAudio::SaveAndPopMenu ));
	AddItem( soundVolume );
	AddItem( musicVolume );
	AddItem( menuVolume );
	AddItem( suitVolume );
	AddItem( noDSP );
	AddItem( useAlphaDSP );
	AddItem( muteFocusLost );
	AddItem( vibrationEnable );
	AddItem( vibration );
}

void CMenuAudio::_VidInit( )
{
	if( IsPanel( ))
	{
		// left column: the volume sliders
		// right column: the DSP and focus checkboxes
		// the two vibration items are mobile-only and are hidden on desktop
		Rect left = ColumnRect( 0, 2 );
		Rect right = ColumnRect( 1, 2 );
		int y = left.pt.y + THEME_LABEL_TALL + 8;

		// Cry of Fear has no HEV suit, so it has no suit voice to set a volume
		// for; "suitvolume" only drives Half-Life's suit speech.
		suitVolume.SetVisibility( !UI_IsCryOfFear( ));

		soundVolume.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );
		y += THEME_CTRL_PITCH;
		musicVolume.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );
		y += THEME_CTRL_PITCH;
		menuVolume.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );

		if( suitVolume.IsVisible( ))
		{
			y += THEME_CTRL_PITCH;
			suitVolume.SetRect( left.pt.x, y, left.sz.w, THEME_SLIDER_H );
		}

		y = right.pt.y + THEME_LABEL_TALL + 8;
		noDSP.SetCoord( right.pt.x, y );
		y += THEME_ROW_PITCH;
		useAlphaDSP.SetCoord( right.pt.x, y );
		y += THEME_ROW_PITCH;
		muteFocusLost.SetCoord( right.pt.x, y );

		vibrationEnable.SetVisibility( false );
		vibration.SetVisibility( false );

		RealignButtons();
	}

	GetConfig();
}

ADD_MENU( menu_audio, CMenuAudio, UI_Audio_Menu );
