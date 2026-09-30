/*
CheckBox.h - checkbox
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
#include "CheckBox.h"
#include "Utils.h"

CMenuCheckBox::CMenuCheckBox() : BaseClass()
{
	SetCharSize( QM_DEFAULTFONT );
	SetSize( 32, 32 );
	SetPicture( UI_CHECKBOX_EMPTY,
		UI_CHECKBOX_FOCUS,
		UI_CHECKBOX_PRESSED,
		UI_CHECKBOX_ENABLED,
		UI_CHECKBOX_GRAYED );
	bChecked = false;
	eFocusAnimation = QM_HIGHLIGHTIFFOCUS;
	iFlags |= QMF_DROPSHADOW;
	bChangeOnPressed = false;
	colorBase = uiColorWhite;
	colorFocus = uiColorWhite;
	iMask = 0;
	bInvertMask = false;
}

/*
=================
CMenuCheckBox::Init
=================
*/
void CMenuCheckBox::VidInit( void )
{
	if( UI_ThemeActive( ))
	{
		// the stock 32x32 is sized for gfx/shell/cb_*.tga; the primitive box is
		// 22x22 so it sits on the label's cap height
		if( size.w == 32 && size.h == 32 )
			SetSize( THEME_CHECK_SIZE, THEME_CHECK_SIZE );

		colorText.SetDefault( THEME_TEXT );
		font = uiStatic.hThemeBody;
		charSize = THEME_BODY_TALL;
		iFlags &= ~QMF_DROPSHADOW;
	}

	colorText.SetDefault( uiColorHelp );
	BaseClass::VidInit();

	if( UI_ThemeActive( ))
	{
		m_scTextPos.x = m_scPos.x + m_scSize.w + (int)( 10 * uiStatic.scaleX );
		m_scTextPos.y = m_scPos.y + ( m_scSize.h - m_scChSize ) / 2;
	}
	else
	{
		m_scTextPos.x = m_scPos.x + ( m_scSize.w * 1.25f );
		m_scTextPos.y = m_scPos.y;
	}

	m_scTextSize.w = g_FontMgr->GetTextWideScaled( font, szName, m_scChSize );
	m_scTextSize.h = m_scChSize;
}

bool CMenuCheckBox::KeyUp( int key )
{
	const char	*sound = 0;

	if( UI::Key::IsLeftMouse( key ) && FBitSet( iFlags, QMF_HASMOUSEFOCUS ))
		sound = uiStatic.sounds[SND_GLOW];
	else if( UI::Key::IsEnter( key ) && !FBitSet( iFlags, QMF_MOUSEONLY ))
		sound = uiStatic.sounds[SND_GLOW];

	if( sound )
	{
		_Event( QM_RELEASED );
		if( !bChangeOnPressed )
		{
			bChecked = !bChecked;	// apply on release
			SetCvarValue( bChecked );
			_Event( QM_CHANGED );
		}
		PlayLocalSound( sound );
	}

	return sound != NULL;
}

bool CMenuCheckBox::KeyDown( int key )
{
	const char	*sound = 0;

	if( UI::Key::IsLeftMouse( key ) && FBitSet( iFlags, QMF_HASMOUSEFOCUS ))
		sound = uiStatic.sounds[SND_GLOW];
	else if( UI::Key::IsEnter( key ) && !FBitSet( iFlags, QMF_MOUSEONLY ))
		sound = uiStatic.sounds[SND_GLOW];

	if( sound )
	{
		_Event( QM_PRESSED );
		if( bChangeOnPressed )
		{
			bChecked = !bChecked;	// apply on release
			SetCvarValue( bChecked );
			_Event( QM_CHANGED );
		}
		PlayLocalSound( sound );
	}

	return sound != NULL;
}


/*
=================
CMenuCheckBox::Draw
=================
*/
void CMenuCheckBox::Draw( void )
{
	uint textflags = ( iFlags & QMF_DROPSHADOW ? ETF_SHADOW : 0 ) | ETF_NOSIZELIMIT | ETF_FORCECOL;

	if( UI_ThemeActive( ))
	{
		// Cry of Fear ships no gfx/shell/cb_* artwork, and UI_DrawPic silently
		// no-ops on a null handle, so the stock control is an invisible hole
		// with a label next to it. Draw the box from primitives instead.
		const bool grayed = FBitSet( iFlags, QMF_GRAYED ) != 0;
		const bool hot = !grayed && UI_THEME_HOT( this );

		UI_DrawString( font, m_scTextPos, m_scTextSize, szName,
			grayed ? THEME_DISABLED : ( hot ? THEME_TEXT_HI : colorText ),
			m_scChSize, eTextAlignment, textflags );

		UI_FillRect( m_scPos, m_scSize, THEME_INPUT_BG );
		UI_DrawHairline( m_scPos, m_scSize,
			grayed ? THEME_DISABLED : ( hot ? THEME_ACCENT : THEME_INPUT_BORDER ));

		if( bChecked )
		{
			int inset = m_scSize.w / 4;

			UI_ThemeDrawTick( Point( m_scPos.x + inset, m_scPos.y + inset ),
				Size( m_scSize.w - inset * 2, m_scSize.h - inset * 2 ),
				grayed ? THEME_DISABLED : THEME_ACCENT );
		}
		return;
	}

	UI_DrawString( font, m_scTextPos, m_scTextSize, szName, colorText, m_scChSize, eTextAlignment, textflags );

	if( szStatusText && iFlags & QMF_NOTIFY )
	{
		Point coord;

		if( szName[0] )
			coord.x = ( uiStatic.buttons_draw_size.w + 40 ) * uiStatic.scaleX;
		else
			coord.x = m_scSize.w + 16 * uiStatic.scaleX;
		coord.x += m_scPos.x;
		coord.y = m_scPos.y + m_scSize.h / 2 - EngFuncs::ConsoleCharacterHeight() / 2;

		int	r, g, b;

		UnpackRGB( r, g, b, uiColorHelp );
		EngFuncs::DrawSetTextColor( r, g, b );
		EngFuncs::DrawConsoleString( coord, szStatusText );
	}

	if( iFlags & QMF_GRAYED )
	{
		UI_DrawPic( m_scPos, m_scSize, uiColorWhite, szGrayedPic );
		return; // grayed
	}

	if((( iFlags & QMF_MOUSEONLY ) && !( iFlags & QMF_HASMOUSEFOCUS ))
	   || ( this != m_pParent->ItemAtCursor() ) )
	{
		if( !bChecked )
			UI_DrawPic( m_scPos, m_scSize, colorBase, szEmptyPic );
		else UI_DrawPic( m_scPos, m_scSize, colorBase, szCheckPic );
		return; // no focus
	}

	if( m_bPressed )
	{
		UI_DrawPic( m_scPos, m_scSize, colorBase, szPressPic );
	}
	else if( eFocusAnimation == QM_HIGHLIGHTIFFOCUS )
	{
		if( bChecked )
		{
			// use two textures for it. Second is just focus texture, slightly orange. Looks pretty.
			UI_DrawPic( m_scPos, m_scSize, colorBase, szPressPic );
			UI_DrawPic( m_scPos, m_scSize, uiInputTextColor, szFocusPic, QM_DRAWADDITIVE );
		}
		else
		{
			UI_DrawPic( m_scPos, m_scSize, colorFocus, szFocusPic );
		}
	}
	else if( bChecked )
	{
		UI_DrawPic( m_scPos, m_scSize, colorBase, szCheckPic );
	}
	else
	{
		UI_DrawPic( m_scPos, m_scSize, colorBase, szEmptyPic );
	}
}

void CMenuCheckBox::UpdateEditable()
{
	bChecked = !!CvarValue();
}
