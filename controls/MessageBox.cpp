/*
MessageBox.cpp -- simple messagebox with text
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
#include "Utils.h"
#include "Action.h"
#include "ItemsHolder.h"
#include "MessageBox.h"

CMenuMessageBox::CMenuMessageBox(const char *name) : BaseClass( name )
{
	iFlags |= QMF_INACTIVE;
	szPanelTitle = "NOTICE";
}

void CMenuMessageBox::_Init()
{
	dlgMessage.eTextAlignment = QM_CENTER; // center
	dlgMessage.iFlags = QMF_INACTIVE|QMF_DROPSHADOW;
	dlgMessage.SetCoord( 0, 0 );
	dlgMessage.size = size;

	if( UI_ThemeActive( ))
	{
		// keep the message clear of the title band
		dlgMessage.iFlags = QMF_INACTIVE;
		dlgMessage.SetCoord( THEME_PAD_X, THEME_TITLE_BAND_H + THEME_PAD_TOP );
		dlgMessage.size.w = size.w - THEME_PAD_X * 2;
		dlgMessage.size.h = size.h - THEME_TITLE_BAND_H - THEME_PAD_TOP - THEME_PAD_BOTTOM;
	}

	AddItem( dlgMessage );
}

/*
==============
CMenuMessageBox::_VidInit

_Init froze the message rect from whatever size the owner had set at that
moment, so a later SetRect never reached the text and a long notice was
ellipsised. In theme mode the panel is sized to the wrapped message instead,
centred like every other dialog; the owner's rect is only used without the
theme.
==============
*/
void CMenuMessageBox::_VidInit()
{
	if( !UI_ThemeActive( ))
		return;

	int w = 520, h = 200;

	UI_ThemeMessagePanelSize( dlgMessage.szName, &w, &h );

	// no button row here, so the message keeps that space
	h -= THEME_BTNROW_H + THEME_BTNROW_GAP;
	if( h < THEME_TITLE_BAND_H + THEME_PAD_TOP + THEME_PAD_BOTTOM + THEME_ROW_H )
		h = THEME_TITLE_BAND_H + THEME_PAD_TOP + THEME_PAD_BOTTOM + THEME_ROW_H;

	SetRect(( uiStatic.width - w ) / 2, ( 768 - h ) / 2, w, h );
	pos.x += uiStatic.xOffset;
	pos.y += uiStatic.yOffset;

	// QM_TOP keeps the horizontal centring (QM_CENTER is 0) but uses the whole
	// height, unlike UI_DrawString's vertically centred path
	dlgMessage.eTextAlignment = QM_TOP;
	dlgMessage.SetRect( THEME_PAD_X, THEME_TITLE_BAND_H + THEME_PAD_TOP,
		w - THEME_PAD_X * 2, h - THEME_TITLE_BAND_H - THEME_PAD_TOP - THEME_PAD_BOTTOM );
	dlgMessage.font = uiStatic.hThemeBody;
	dlgMessage.charSize = THEME_BODY_TALL;
}

void CMenuMessageBox::Draw()
{
	if( UI_ThemeActive( ))
	{
		UI_ThemeDrawDialogPanel( m_scPos, m_scSize, szPanelTitle );
		BaseClass::Draw();
		return;
	}

	EngFuncs::FillRGBA( m_scPos.x, m_scPos.y, m_scSize.w, m_scSize.h, 20, 20, 20, 235 );
	UI_DrawRectangle( m_scPos, m_scSize, uiInputFgColor );
	BaseClass::Draw();
}

void CMenuMessageBox::SetMessage( const char *sz )
{
	dlgMessage.szName = sz;
}
