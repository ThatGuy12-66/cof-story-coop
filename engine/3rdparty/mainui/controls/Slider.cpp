/*
Slider.h - slider
Copyright (C) 2010 Uncle Mike
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Slider.h"
#include "Utils.h"

CMenuSlider::CMenuSlider() : BaseClass(), m_flMinValue(), m_flMaxValue(), m_flCurValue(),
	m_flDrawStep(), m_iNumSteps(), m_flRange(), m_iKeepSlider()
{
	m_iSliderOutlineWidth = 6;

	size.w = 200;
	size.h = 2 + m_iSliderOutlineWidth * 2;

	m_flRange = 1.0f;

	eFocusAnimation = QM_HIGHLIGHTIFFOCUS;

	SetCharSize( QM_DEFAULTFONT );

	imgSlider = UI_SLIDER_MAIN;

	iFlags |= QMF_DROPSHADOW;
}

/*
=================
CMenuSlider::Init
=================
*/
void CMenuSlider::VidInit(  )
{
	if( m_flRange < 0.05f )
		m_flRange = 0.05f;

	colorBase.SetDefault( uiColorWhite );
	colorFocus.SetDefault( uiColorWhite );

	if( UI_ThemeActive( ))
	{
		// a 6-unit track in a 26-unit hit box, instead of 2 + 6*2 = 14
		if( size.h == 2 + m_iSliderOutlineWidth * 2 )
			size.h = THEME_SLIDER_H;

		font = uiStatic.hThemeLabel;
		charSize = THEME_LABEL_TALL;
		iFlags &= ~QMF_DROPSHADOW;
	}

	BaseClass::VidInit();

	// scale the center box
	m_scCenterBox.w = 40 * uiStatic.scaleX;
	m_scCenterBox.h = m_scSize.h - m_iSliderOutlineWidth * 2;

	m_iNumSteps = (m_flMaxValue - m_flMinValue) / m_flRange + 1;
	m_flDrawStep = (float)(m_scSize.w - m_iSliderOutlineWidth - m_scCenterBox.w) / (float)m_iNumSteps;
}

bool CMenuSlider::KeyUp( int key )
{
	if( m_iKeepSlider )
	{
		// tell menu about changes
		SetCvarValue( m_flCurValue );
		_Event( QM_CHANGED );
		m_iKeepSlider = false; // button released
	}
	return true;

}

/*
=================
CMenuSlider::Key
=================
*/
bool CMenuSlider::KeyDown( int key )
{
	if( UI::Key::IsLeftMouse( key ))
	{
		if( !UI_CursorInRect( m_scPos, m_scSize ) )
		{
			m_iKeepSlider = false;
			return true;
		}

		m_iKeepSlider = true;

		// immediately move slider into specified place
		int	dist, numSteps;

		dist = uiStatic.cursorX - (m_scPos.x + m_iSliderOutlineWidth + m_scCenterBox.w);
		numSteps = floor(dist / m_flDrawStep);
		m_flCurValue = bound( m_flMinValue, numSteps * m_flRange + m_flMinValue, m_flMaxValue );

		// tell menu about changes
		SetCvarValue( m_flCurValue );
		_Event( QM_CHANGED );

		return true;
	}
	else if( UI::Key::IsLeftArrow( key ))
	{
		m_flCurValue -= m_flRange;

		if( m_flCurValue < m_flMinValue )
		{
			m_flCurValue = m_flMinValue;
			PlayLocalSound( uiStatic.sounds[SND_BUZZ] );
			return true;
		}

		// tell menu about changes
		SetCvarValue( m_flCurValue );
		_Event( QM_CHANGED );

		PlayLocalSound( uiStatic.sounds[SND_KEY] );
		return true;
	}
	else if( UI::Key::IsRightArrow( key ))
	{
		m_flCurValue += m_flRange;

		if( m_flCurValue > m_flMaxValue )
		{
			m_flCurValue = m_flMaxValue;
			PlayLocalSound( uiStatic.sounds[SND_BUZZ] );
			return true;
		}

		// tell menu about changes
		SetCvarValue( m_flCurValue );
		_Event( QM_CHANGED );
		PlayLocalSound( uiStatic.sounds[SND_KEY] );
		return true;
	}

	return false;
}

/*
=================
CMenuSlider::Draw
=================
*/
void CMenuSlider::Draw( void )
{
	int	textHeight, sliderX;
	uint textflags = ( iFlags & QMF_DROPSHADOW ) ? ETF_SHADOW : 0;

	// in theme mode the framework draws one hint line in the theme font
	if( szStatusText && iFlags & QMF_NOTIFY && !UI_ThemeActive( ))
	{
		Point coord;

		coord.x = m_scPos.x + 16 * uiStatic.scaleX;
		coord.y = m_scPos.y + m_scSize.h / 2 - EngFuncs::ConsoleCharacterHeight() / 2;

		int	r, g, b;

		UnpackRGB( r, g, b, uiColorHelp );
		EngFuncs::DrawSetTextColor( r, g, b );
		EngFuncs::DrawConsoleString( coord, szStatusText );
	}

	if( m_iKeepSlider )
	{
		if( !UI_CursorInRect( m_scPos.x, m_scPos.y - 40, m_scSize.w, m_scSize.h + 80 ) )
			m_iKeepSlider = false;
		else
		{
			int	dist, numSteps;

			if( UI_ThemeActive( ))
			{
				// the theme knob is 10 virtual units wide and the track spans
				// the whole control, so map the cursor straight onto it
				const int knobW = (int)( 10 * uiStatic.scaleX );
				float frac = (float)( uiStatic.cursorX - m_scPos.x - knobW / 2 ) /
					(float)( m_scSize.w - knobW );

				frac = bound( 0.0f, frac, 1.0f );
				numSteps = (int)(( frac * ( m_flMaxValue - m_flMinValue )) / m_flRange + 0.5f );
			}
			else
			{
				// move slider follow the holded mouse button
				dist = uiStatic.cursorX - m_scPos.x - m_iSliderOutlineWidth - (m_scCenterBox.w/2);
				numSteps = floor(dist / m_flDrawStep);
			}
			m_flCurValue = bound( m_flMinValue, numSteps * m_flRange + m_flMinValue, m_flMaxValue );

			// tell menu about changes
			SetCvarValue( m_flCurValue );
			_Event( QM_CHANGED );
		}
	}

	// keep value in range
	m_flCurValue = bound( m_flMinValue, m_flCurValue, m_flMaxValue );

	// calc slider position
	sliderX = m_scPos.x + (m_iSliderOutlineWidth/2) // start
		+ ( ( m_flCurValue - m_flMinValue ) / ( m_flMaxValue - m_flMinValue ) )  // calc fractional part
		* ( m_scSize.w - m_iSliderOutlineWidth - (m_scCenterBox.w) );


	if( UI_ThemeActive( ))
	{
		// UI_SLIDER_MAIN is gfx/shell artwork this game does not ship, so the
		// knob is a filled rectangle and the track a filled bar.
		const bool hot = UI_THEME_HOT( this );
		const int trackH = (int)( THEME_SLIDER_TRACK * uiStatic.scaleY );
		const int trackY = m_scPos.y + ( m_scSize.h - trackH ) / 2;
		const int knobW = (int)( 10 * uiStatic.scaleX );
		int knobX = m_scPos.x + ( ( m_flCurValue - m_flMinValue ) / ( m_flMaxValue - m_flMinValue ) )
			* ( m_scSize.w - knobW );

		UI_FillRect( m_scPos.x, trackY, m_scSize.w, trackH, THEME_TRACK );
		// the filled part is a bar: the deep red (menufix round)
		UI_FillRect( m_scPos.x, trackY, knobX - m_scPos.x + knobW / 2, trackH,
			THEME_ACCENT_FILL );
		UI_FillRect( knobX, m_scPos.y, knobW, m_scSize.h, hot ? THEME_TEXT_HI : THEME_ACCENT );

		textHeight = m_scPos.y - (m_scChSize * 1.5f);
		UI_DrawString( font, m_scPos.x, textHeight, m_scSize.w, m_scChSize, szName,
			hot ? THEME_TEXT_HI : THEME_TEXT_DIM, m_scChSize, eTextAlignment, ETF_FORCECOL );
		return;
	}

	UI_DrawRectangleExt( m_scPos.x + m_iSliderOutlineWidth / 2, m_scPos.y + m_iSliderOutlineWidth, m_scSize.w - m_iSliderOutlineWidth, m_scCenterBox.h, uiInputBgColor, m_iSliderOutlineWidth );
	if( eFocusAnimation == QM_HIGHLIGHTIFFOCUS && this == m_pParent->ItemAtCursor())
		UI_DrawPic( sliderX, m_scPos.y, m_scCenterBox.w, m_scSize.h, uiColorHelp, imgSlider );
	else
		UI_DrawPic( sliderX, m_scPos.y, m_scCenterBox.w, m_scSize.h, uiColorWhite, imgSlider );


	textHeight = m_scPos.y - (m_scChSize * 1.5f);
	UI_DrawString( font, m_scPos.x, textHeight, m_scSize.w, m_scChSize, szName, uiColorHelp, m_scChSize, eTextAlignment, textflags | ETF_FORCECOL );
}

void CMenuSlider::UpdateEditable()
{
	float flValue = CvarValue();

	m_flCurValue = flValue;
}
