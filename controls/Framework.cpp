/*
Framework.cpp -- base menu fullscreen root window
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
#include "Framework.h"
#include "PicButton.h"
#include "keydefs.h"
#include "FontManager.h"
#include "CoFOptions.h"
#include "CoFOsk.h"

// menu banners used fiexed rectangle (virtual screenspace at 640x480)
#define UI_BANNER_POSX		72
#define UI_BANNER_POSY		72
#define UI_BANNER_WIDTH		736
#define UI_BANNER_HEIGHT		128

CMenuFramework::CMenuFramework( const char *name ) : BaseClass( name )
{
	memset( m_apBtns, 0, sizeof( m_apBtns ) );
	m_iBtnsNum = 0;
	bannerAnimDirection = ANIM_NO;

	bPanel = false;
	bCloseAdded = false;
	panelSize = Size( 560, 400 );
	szPanelTitle = NULL;
	pPadSecondary = NULL;
}

void CMenuFramework::SetPanel( const char *title, int w, int h )
{
	bPanel = true;
	szPanelTitle = title;
	panelSize = Size( w, h );
}

Point CMenuFramework::PanelPos() const
{
	return Point(( uiStatic.width - panelSize.w ) / 2, ( 768 - panelSize.h ) / 2 );
}

Rect CMenuFramework::ContentRect() const
{
	Point p = PanelPos();

	const int tabs = TabStripH();

	return Rect( p.x + THEME_PAD_X,
		p.y + THEME_TITLE_BAND_H + THEME_PAD_TOP + tabs,
		panelSize.w - THEME_PAD_X * 2,
		panelSize.h - THEME_TITLE_BAND_H - THEME_PAD_TOP - THEME_PAD_BOTTOM
			- THEME_BTNROW_H - THEME_BTNROW_GAP - tabs );
}

// ---------------------------------------------------------------------------
// Cry of Fear tabbed windows (menus/CoFOptions.cpp)
// ---------------------------------------------------------------------------

int CMenuFramework::TabStripH() const
{
	return ( bPanel && UI_ThemeActive() && UI_CoFTabGroup( szPanelTitle, NULL )) ? COF_TAB_STRIP_H : 0;
}

// the tabs left to right from the content's left edge, each as wide as a
// button with the same label; tabs the group hides right now take no room
Rect CMenuFramework::TabRect( int index ) const
{
	int cur = 0;
	const cofTabGroup_t *g = UI_CoFTabGroup( szPanelTitle, &cur );
	Point p = PanelPos();
	int x = p.x + THEME_PAD_X;
	const int y = p.y + THEME_TITLE_BAND_H + 8;

	for( int i = 0; g && i < g->count; i++ )
	{
		if( !UI_CoFTabAvailable( g, i ))
			continue;

		const int w = UI_ThemeButtonWidth( L( g->tabs[i].label ));

		if( i == index )
			return Rect( x, y, w, COF_TAB_H );
		x += w + 4;
	}
	return Rect( 0, 0, 0, 0 );
}

void CMenuFramework::DrawTabStrip( void )
{
	int cur = 0;
	const cofTabGroup_t *g = UI_CoFTabGroup( szPanelTitle, &cur );

	if( !g )
		return;

	const int charH = (int)( THEME_BUTTON_TALL * uiStatic.scaleY );

	for( int i = 0; i < g->count; i++ )
	{
		if( !UI_CoFTabAvailable( g, i ))
			continue;

		Rect r = TabRect( i );
		Point pt = r.pt.Scale() + m_scPos;
		Size sz = r.sz.Scale();
		const bool active = i == cur;
		const bool hover = !active && UI_CursorInRect( pt, sz ) && !UI_CoFPadMode();

		if( active )
			UI_FillRect( pt, sz, THEME_SELECT_FILL );
		else if( hover )
			UI_FillRect( pt, sz, THEME_HOVER_FILL );

		if( active )
		{
			const int line = UI_ThemeHairline() * 2;
			UI_FillRect( pt.x, pt.y + sz.h - line, sz.w, line, THEME_ACCENT_FILL );
		}

		UI_DrawString( uiStatic.hThemeButton, Point( pt.x, pt.y + ( sz.h - charH ) / 2 ), Size( sz.w, charH ),
			L( g->tabs[i].label ), active || hover ? THEME_TEXT_HI : THEME_TEXT, charH,
			QM_CENTER, ETF_FORCECOL | ETF_NO_WRAP );
	}

	Point p = PanelPos();
	UI_ThemeSeparator( (int)(( p.x + THEME_PAD_X ) * uiStatic.scaleX ) + m_scPos.x,
		(int)(( p.y + THEME_TITLE_BAND_H + 8 + COF_TAB_H ) * uiStatic.scaleY ) + m_scPos.y,
		(int)(( panelSize.w - THEME_PAD_X * 2 ) * uiStatic.scaleX ), THEME_SEPARATOR );
}

/*
=================
CMenuFramework::GoToTab

One window with tabs, built from separate pages: the page on screen leaves
the stack and the tab's own page comes in, with no fade either way, so the
switch looks like one window changing its content. The window under the tab
pages (main or pause menu) is never shown in between.
=================
*/
void CMenuFramework::GoToTab( int index )
{
	int cur = 0;
	const cofTabGroup_t *g = UI_CoFTabGroup( szPanelTitle, &cur );

	if( !g || index == cur || !UI_CoFTabAvailable( g, index ))
		return;

	Hide();
	DisableTransition();

	CMenuBaseWindow *below = m_pStack->Current();
	if( below )
		below->DisableTransition();

	g->tabs[index].show();

	if( below )
		below->DisableTransition();

	CMenuBaseWindow *top = m_pStack->Current();
	if( top )
		top->DisableTransition();
}

bool CMenuFramework::TabsKeyDown( int key )
{
	int cur = 0;
	const cofTabGroup_t *g = UI_CoFTabGroup( szPanelTitle, &cur );

	if( !g || !IsPanel( ))
		return false;

	// LB / RB, and Tab / Shift+Tab on the keyboard: the previous / next tab
	// the group shows right now, wrapping
	if( key == K_L1_BUTTON || key == K_R1_BUTTON || key == K_TAB )
	{
		const int dir = ( key == K_R1_BUTTON || ( key == K_TAB && !EngFuncs::KEY_IsDown( K_SHIFT ))) ? 1 : -1;

		for( int n = 1; n < g->count; n++ )
		{
			const int i = ( cur + dir * n + g->count * n ) % g->count;

			if( UI_CoFTabAvailable( g, i ))
			{
				PlayLocalSound( uiStatic.sounds[SND_MOVE] );
				GoToTab( i );
				break;
			}
		}
		return true;
	}

	if( key == K_MOUSE1 )
	{
		for( int i = 0; i < g->count; i++ )
		{
			if( !UI_CoFTabAvailable( g, i ))
				continue;

			Rect r = TabRect( i );
			if( UI_CursorInRect( r.pt.Scale() + m_scPos, r.sz.Scale( )))
			{
				if( i != cur )
				{
					PlayLocalSound( uiStatic.sounds[SND_LAUNCH] );
					GoToTab( i );
				}
				return true;
			}
		}
	}
	return false;
}

/*
=================
CMenuFramework::FocusFirstItem

A page opens with its first control in focus, so a pad (or the arrow keys)
starts somewhere: the first focusable item inside the content area - not the
title band's close X, not the button row - or failing that the first
focusable item at all. With the mouse the item under the pointer keeps the
focus; in pad mode the focus is also drawn (QMF_HASKEYBOARDFOCUS).
=================
*/
void CMenuFramework::FocusFirstItem( void )
{
	CMenuBaseItem *cur = ItemAtCursor();
	const bool pad = UI_CoFPadMode();

	if( cur && FBitSet( cur->iFlags, QMF_HASMOUSEFOCUS ) && !pad )
		return;

	int pick = -1, any = -1;
	const Rect c = IsPanel() ? ContentRect() : Rect( 0, 0, 0, 0 );

	for( int i = 0; i < m_pItems.Count(); i++ )
	{
		CMenuBaseItem *item = m_pItems[i];

		if( !item->IsVisible() || FBitSet( item->iFlags, QMF_INACTIVE | QMF_GRAYED | QMF_MOUSEONLY ))
			continue;
		if( any < 0 )
			any = i;
		if( !IsPanel() || ( item->pos.y >= c.pt.y - 4 && item->pos.y < ButtonRowY() - 4 ))
		{
			pick = i;
			break;
		}
	}

	if( pick < 0 )
		pick = any;
	if( pick < 0 )
		return;

	for( int i = 0; i < m_pItems.Count(); i++ )
		ClearBits( m_pItems[i]->iFlags, QMF_HASKEYBOARDFOCUS );

	m_iCursorPrev = m_iCursor;
	m_iCursor = pick;
	if( pad )
		SetBits( m_pItems[pick]->iFlags, QMF_HASKEYBOARDFOCUS );
}

/*
=================
CMenuFramework::DrawPadHints

With a pad as the last input device, one line of button prompts under the
panel (or at the bottom of a list menu): the glyph of the current button
style, 34 units tall (48 px at 1080p), and its action in words. Mouse
and keyboard users see nothing.
=================
*/
void CMenuFramework::DrawPadHints( void )
{
	// the on-screen keyboard draws its own line over the page (menus/CoFOsk.cpp)
	if( !UI_CoFOptionsLayout() || !UI_CoFPadMode( ) || UI_CoFOsk_Active( ))
		return;

	struct { const char *glyph; const char *text; } hint[6];
	int n = 0;

	hint[n].glyph = UI_CoFPadGlyph( "a.png", "cross.png" );
	hint[n++].text = L( "Select" );
	// the death page swallows B / Escape (nowhere to go back to), so it
	// offers no Back prompt
	if( !szName || strcmp( szName, "CMenuCoFDeath" ))
	{
		hint[n].glyph = UI_CoFPadGlyph( "b.png", "circle.png" );
		hint[n++].text = L( "Back" );
	}
	int tabs = 0, cur = 0;
	const cofTabGroup_t *group = UI_CoFTabGroup( szPanelTitle, &cur );

	for( int i = 0; group && i < group->count; i++ )
		tabs += UI_CoFTabAvailable( group, i ) ? 1 : 0;

	if( TabStripH() && tabs > 1 )
	{
		hint[n].glyph = UI_CoFPadGlyph( "lb.png", "l1.png" );
		hint[n++].text = NULL;	// drawn next to RB
		hint[n].glyph = UI_CoFPadGlyph( "rb.png", "r1.png" );
		hint[n++].text = L( "Tabs" );
	}
	if( pPadSecondary && pPadSecondary->IsVisible() && pPadSecondary->szName )
	{
		hint[n].glyph = UI_CoFPadGlyph( "x.png", "square.png" );
		hint[n++].text = pPadSecondary->szName;
	}
	if( CL_IsActive() && ( !szName || strcmp( szName, "CMenuCoFDeath" )))
	{
		hint[n].glyph = UI_CoFPadGlyph( "menu.png", "options.png" );
		hint[n++].text = L( "Resume" );
	}

	// 34 units: 48 px at 1080p, 64 at 1440p, 96 at 2160p
	const int glyph = (int)( 34 * uiStatic.scaleY );
	const int charH = (int)( THEME_BODY_TALL * uiStatic.scaleY );
	const int gap = (int)( 6 * uiStatic.scaleX );
	const int sep = (int)( 26 * uiStatic.scaleX );
	int total = 0;

	for( int i = 0; i < n; i++ )
	{
		total += glyph;
		if( hint[i].text )
			total += gap + g_FontMgr->GetTextWideScaled( uiStatic.hThemeBody, hint[i].text, charH ) + sep;
		else
			total += gap / 2;
	}
	total -= sep;

	int y;
	if( IsPanel( ))
		y = (int)(( PanelPos().y + panelSize.h + 6 ) * uiStatic.scaleY ) + m_scPos.y;
	else
		y = (int)(( 768 - 44 ) * uiStatic.scaleY ) + m_scPos.y;

	int x = ( ScreenWidth - total ) / 2;
	const int padX = (int)( 12 * uiStatic.scaleX ), padY = (int)( 3 * uiStatic.scaleY );

	// a dark chip behind the line, so the white glyphs read on any scene
	UI_FillRect( x - padX, y - padY, total + padX * 2, glyph + padY * 2, THEME_PANEL );

	for( int i = 0; i < n; i++ )
	{
		UI_DrawPic( x, y, glyph, glyph, THEME_TEXT_HI, hint[i].glyph, QM_DRAWTRANS );
		x += glyph;
		if( !hint[i].text )
		{
			x += gap / 2;
			continue;
		}
		x += gap;
		UI_DrawString( uiStatic.hThemeBody, x, y + ( glyph - charH ) / 2, total, charH, hint[i].text,
			THEME_TEXT_HI, charH, QM_LEFT, ETF_FORCECOL | ETF_NO_WRAP | ETF_NOSIZELIMIT );
		x += g_FontMgr->GetTextWideScaled( uiStatic.hThemeBody, hint[i].text, charH ) + sep;
	}
}

int CMenuFramework::ButtonRowY() const
{
	return PanelPos().y + panelSize.h - THEME_PAD_BOTTOM - THEME_BTNROW_H;
}

Rect CMenuFramework::ColumnRect( int index, int count ) const
{
	Rect c = ContentRect();

	if( count < 2 )
		return c;

	int gutter = ( count == 2 ) ? THEME_GUTTER2 : THEME_GUTTER3;
	int w = ( c.sz.w - gutter * ( count - 1 )) / count;

	return Rect( c.pt.x + index * ( w + gutter ), c.pt.y, w, c.sz.h );
}

// two columns with an explicit split, e.g. a 0.62 list and a 0.38 preview
Rect CMenuFramework::ColumnRect2( int index, float firstFrac ) const
{
	Rect c = ContentRect();
	int inner = c.sz.w - THEME_GUTTER2;
	int w0 = (int)( inner * firstFrac );

	if( index == 0 )
		return Rect( c.pt.x, c.pt.y, w0, c.sz.h );

	return Rect( c.pt.x + w0 + THEME_GUTTER2, c.pt.y, inner - w0, c.sz.h );
}

Rect CMenuFramework::RowRect( int index ) const
{
	Rect c = ContentRect();

	return Rect( c.pt.x, c.pt.y + index * THEME_ROW_PITCH, c.sz.w, THEME_ROW_H );
}

/*
=================
CMenuFramework::LayoutButtonRow

One right-aligned row along the bottom of the panel, left to right in the order
given, so the primary action is simply the last one. Every menu in the tree that
calls AddButton gets this through RealignButtons; menus that own their buttons
as members call it directly.
=================
*/
void CMenuFramework::LayoutButtonRow( CMenuPicButton **btns, int count )
{
	int widths[MAX_FRAMEWORK_PICBUTTONS];
	int total = 0;

	if( count <= 0 || count > MAX_FRAMEWORK_PICBUTTONS )
		return;

	for( int i = 0; i < count; i++ )
	{
		widths[i] = UI_ThemeButtonWidth( btns[i]->szName );
		total += widths[i] + ( i ? THEME_BTN_GAP : 0 );
	}

	Rect c = ContentRect();
	int x = c.pt.x + c.sz.w - total;
	int y = ButtonRowY();

	for( int i = 0; i < count; i++ )
	{
		btns[i]->eThemeStyle = CMenuPicButton::THEME_BOX;
		btns[i]->eTextAlignment = QM_CENTER;
		btns[i]->font = uiStatic.hThemeButton;
		btns[i]->charSize = THEME_BUTTON_TALL;
		btns[i]->SetRect( x, y, widths[i], THEME_BTNROW_H );
		btns[i]->CalcPosition();
		btns[i]->CalcSizes();

		x += widths[i] + THEME_BTN_GAP;
	}
}

CMenuFramework::CMenuCloseButton::CMenuCloseButton()
{
	iFlags = QMF_MOUSEONLY;
	eFocusAnimation = QM_HIGHLIGHTIFFOCUS;
	szName = "";
	SetSize( THEME_CLOSE_SIZE, THEME_CLOSE_SIZE );
}

bool CMenuFramework::CMenuCloseButton::KeyDown( int key )
{
	if( UI::Key::IsLeftMouse( key ) && FBitSet( iFlags, QMF_HASMOUSEFOCUS ))
	{
		_Event( QM_PRESSED );
		return true;
	}
	return false;
}

bool CMenuFramework::CMenuCloseButton::KeyUp( int key )
{
	if( UI::Key::IsLeftMouse( key ) && FBitSet( iFlags, QMF_HASMOUSEFOCUS ))
	{
		PlayLocalSound( uiStatic.sounds[SND_LAUNCH] );
		_Event( QM_RELEASED );
		return true;
	}
	return false;
}

void CMenuFramework::CMenuCloseButton::Draw()
{
	const bool hot = UI_THEME_HOT( this );
	const int inset = m_scSize.w / 4;

	if( hot )
		UI_FillRect( m_scPos, m_scSize, THEME_HOVER_FILL );

	UI_ThemeDrawCross( Point( m_scPos.x + inset, m_scPos.y + inset ),
		Size( m_scSize.w - inset * 2, m_scSize.h - inset * 2 ),
		hot ? THEME_TEXT_HI : THEME_TEXT );
}

CMenuFramework::~CMenuFramework()
{
	for( int i = 0; i < m_iBtnsNum; i++ )
	{
		RemoveItem( *( m_apBtns[i] ) );
		delete m_apBtns[i];
		m_apBtns[i] = NULL;
	}
}

void CMenuFramework::Show()
{
	BaseClass::Show();

	if( UI_CoFOptionsLayout( ))
		FocusFirstItem();
}

void CMenuFramework::Draw()
{
	static int statusFadeTime;
	static CMenuBaseItem *lastItem;
	CMenuBaseItem *item;

	if( IsPanel( ))
		DrawPanelChrome();

	BaseClass::Draw();

	DrawPadHints();

	item = ItemAtCursor();
	// a1ba: maybe this should be somewhere else?
	if( item != lastItem )
	{
		if( item ) item->m_iLastFocusTime = uiStatic.realTime;
		statusFadeTime = uiStatic.realTime;

		lastItem = item;
	}

	// draw status text if status text isn't rendered at right side of an item
	// ideally, framework shouldn't even know about this but since everything
	// WON-styled goes here, let's leave it here...
	//
	// In theme mode no control draws its own status text any more (they all
	// used EngFuncs::DrawConsoleString, the engine bitmap console font, which
	// cannot match a TTF theme), so this single hint line owns all of them.
	const bool themeStatus = UI_ThemeActive();

	if( item && item == lastItem && ( themeStatus || !FBitSet( item->iFlags, QMF_NOTIFY )) && item->szStatusText != NULL )
	{
		float alpha = bound( 0, ((( uiStatic.realTime - statusFadeTime ) - 100 ) * 0.01f ), 1 );
		int r, g, b, x, len;

		if( themeStatus )
		{
			Point pt;
			Size sz;

			if( IsPanel( ))
			{
				Rect c = ContentRect();

				pt = Point( c.pt.x, ButtonRowY() + ( THEME_BTNROW_H - THEME_LABEL_TALL ) / 2 ).Scale();
				sz = Size( c.sz.w - THEME_BTN_MIN_W, THEME_LABEL_TALL ).Scale();
			}
			else
			{
				pt = Point( THEME_MENU_MARGIN, 768 - 52 ).Scale();
				sz = Size( uiStatic.width - THEME_MENU_MARGIN * 2, THEME_LABEL_TALL ).Scale();
			}

			pt = pt + m_scPos;

			UI_DrawString( uiStatic.hThemeLabel, pt, sz, item->szStatusText,
				PackAlpha( THEME_TEXT_DIM, (unsigned int)( alpha * 255 )),
				(int)( THEME_LABEL_TALL * uiStatic.scaleY ), QM_LEFT,
				ETF_FORCECOL | ETF_NO_WRAP );
		}
		else
		{
			EngFuncs::ConsoleStringLen( item->szStatusText, &len, NULL );

			UnpackRGB( r, g, b, uiColorHelp );
			EngFuncs::DrawSetTextColor( r, g, b, alpha * 255 );
			x = ( ScreenWidth - len ) * 0.5f; // centering

			EngFuncs::DrawConsoleString( x, uiStatic.yOffset + 720 * uiStatic.scaleY, item->szStatusText );
		}
	}
	else statusFadeTime = uiStatic.realTime;
}

/*
=================
CMenuFramework::DrawPanelChrome

Scrim, panel, hairline border, title band with its uppercase title, and the
hairline above the button row. Everything is axis-aligned filled rectangles:
the library has no rounded rectangle, no gradient and no line primitive - and
the Source dialogs this follows are square anyway.
=================
*/
void CMenuFramework::DrawPanelChrome()
{
	Point p = ( PanelPos().Scale() ) + m_scPos;
	Size  s = panelSize.Scale();

	// dim everything behind the dialog
	UI_FillRect( 0, 0, ScreenWidth, ScreenHeight, THEME_SCRIM );

	// a tabbed window shows its group's title, then the tab strip
	const cofTabGroup_t *group = UI_CoFTabGroup( szPanelTitle, NULL );

	UI_ThemeDrawDialogPanel( p, s, group ? group->title : szPanelTitle );

	if( group )
		DrawTabStrip();

	// hairline above the button row
	UI_ThemeSeparator( p.x + (int)( THEME_PAD_X * uiStatic.scaleX ),
		(int)(( ButtonRowY() - THEME_BTNROW_GAP / 2 ) * uiStatic.scaleY ) + m_scPos.y,
		s.w - (int)( THEME_PAD_X * 2 * uiStatic.scaleX ), THEME_SEPARATOR );
}

void CMenuFramework::Hide()
{
	BaseClass::Hide();
}

void CMenuFramework::Init()
{
	BaseClass::Init();
	pos.x = uiStatic.xOffset;
	pos.y = uiStatic.yOffset;
	size.w = uiStatic.width;
	size.h = 768;

	// _Init() has run by now, so a menu that asked for panel mode is known.
	// The close button is added last so it draws over the title band.
	if( IsPanel() && !bCloseAdded )
	{
		closeBtn.onReleased = VoidCb( &CMenuFramework::Hide );
		AddItem( closeBtn );
		bCloseAdded = true;
	}
}

void CMenuFramework::VidInit()
{
	pos.x = uiStatic.xOffset;
	pos.y = uiStatic.yOffset;
	size.w = uiStatic.width;
	size.h = 768;

	// every tab of a tabbed window has the same frame, so switching tabs
	// never moves the window
	if( IsPanel() && TabStripH( ))
		panelSize = Size( COF_TAB_FRAME_W, COF_TAB_FRAME_H );

	if( IsPanel( ))
	{
		// the WON banner and its slide transition have no place in a panel
		banner.Hide();

		Point p = PanelPos();

		closeBtn.SetRect( p.x + panelSize.w - THEME_CLOSE_INSET - THEME_CLOSE_SIZE,
			p.y + ( THEME_TITLE_BAND_H - THEME_CLOSE_SIZE ) / 2,
			THEME_CLOSE_SIZE, THEME_CLOSE_SIZE );
	}

	BaseClass::VidInit();

	if( IsPanel( ))
		RealignButtons();
}

CMenuPicButton * CMenuFramework::AddButton(const char *szName, const char *szStatus, EDefaultBtns buttonPicId, CEventCallback onReleased, int iFlags)
{
	if( m_iBtnsNum >= MAX_FRAMEWORK_PICBUTTONS )
	{
		Host_Error( "Too many pic buttons in framework!" );
		return NULL;
	}

	CMenuPicButton *btn = new CMenuPicButton();

	btn->SetNameAndStatus( szName, szStatus );
	btn->SetPicture( buttonPicId );
	btn->iFlags |= iFlags;
	btn->onReleased = onReleased;
	btn->SetCoord( 72, 230 + m_iBtnsNum * 50 );
	AddItem( btn );

	m_apBtns[m_iBtnsNum++] = btn;

	return btn;
}

CMenuPicButton * CMenuFramework::AddButton( const char *szName, const char *szStatus, const char *szButtonPath, CEventCallback onReleased, int iFlags, int hotkey )
{
	if( m_iBtnsNum >= MAX_FRAMEWORK_PICBUTTONS )
	{
		Host_Error( "Too many pic buttons in framework!" );
		return NULL;
	}

	CMenuPicButton *btn = new CMenuPicButton();

	btn->SetNameAndStatus( szName, szStatus );
	btn->SetPicture( szButtonPath, hotkey );
	btn->iFlags |= iFlags;
	btn->onReleased = onReleased;
	btn->SetCoord( 72, 230 + m_iBtnsNum * 50 );
	AddItem( btn );

	m_apBtns[m_iBtnsNum++] = btn;

	return btn;
}

void CMenuFramework::RealignButtons( void )
{
	if( IsPanel( ))
	{
		CMenuPicButton *visible[MAX_FRAMEWORK_PICBUTTONS];
		int n = 0;

		for( int i = 0; i < m_iBtnsNum; i++ )
		{
			if( m_apBtns[i]->IsVisible())
				visible[n++] = m_apBtns[i];
		}

		LayoutButtonRow( visible, n );
		return;
	}

	for( int i = 0, j = 0; i < m_iBtnsNum; i++ )
	{
		if( !m_apBtns[i]->IsVisible())
			continue;

		m_apBtns[i]->SetCoord( 72, 230 + j * 50 );
		m_apBtns[i]->CalcPosition();
		j++;
	}
}

bool CMenuFramework::KeyDown( int key )
{
	// tabs first: LB/RB would otherwise page a focused list
	if( TabsKeyDown( key ))
		return true;

	// X on a pad: the page's secondary action, when it has one
	if( key == K_X_BUTTON && pPadSecondary && pPadSecondary->IsVisible() &&
		!FBitSet( pPadSecondary->iFlags, QMF_GRAYED | QMF_INACTIVE ))
	{
		PlayLocalSound( uiStatic.sounds[SND_LAUNCH] );
		pPadSecondary->_Event( QM_RELEASED );
		return true;
	}

	bool b = BaseClass::KeyDown( key );

	if( UI::Key::IsEscape( key ))
	{
		const CMenuBaseWindow *newWindow = WindowStack()->Current();

		if( newWindow != nullptr && newWindow != this && newWindow->IsRoot() )
		{
			PrepareBannerAnimation( ANIM_CLOSING, nullptr );
		}
	}

	return b;
}

void CMenuFramework::PrepareBannerAnimation( EAnimation direction, CMenuPicButton *initiator )
{
	// the theme has no banners, and no banner slide: the only animation it
	// keeps is the window opacity cross-fade in CMenuBaseWindow::DrawAnimation
	if( UI_ThemeActive( ))
		return;

	// banner is not present, ignore
	if( banner.Parent() != this )
		return;

	bannerAnimDirection = direction;
	if( initiator )
	{
		bannerRects[0].pt = initiator->GetRenderPosition();
		bannerRects[0].sz = initiator->GetRenderSize();

		bannerRects[1].pt = banner.GetRenderPosition();
		bannerRects[1].sz = banner.GetRenderSize();

		banner.szName = initiator->szName;
	}
}

bool CMenuFramework::DrawAnimation()
{
	bool b = CMenuBaseWindow::DrawAnimation( );

	if( bannerAnimDirection != ANIM_NO )
	{
		float frac;

		if( bannerAnimDirection == ANIM_OPENING )
			frac = ( uiStatic.realTime - m_iTransitionStartTime ) / TTT_PERIOD;
		else
			frac = 1.0f - ( uiStatic.realTime - m_iTransitionStartTime ) / TTT_PERIOD;
		Rect r = Rect::Lerp( bannerRects[0], bannerRects[1], frac );

		banner.Draw( r.pt, r.sz );
	}

	if( b )
		bannerAnimDirection = ANIM_NO;

	return b;
}

CMenuFramework::CMenuBannerBitmap::CMenuBannerBitmap()
{
	SetRect( UI_BANNER_POSX, UI_BANNER_POSY, UI_BANNER_WIDTH, UI_BANNER_HEIGHT );
}

void CMenuFramework::CMenuBannerBitmap::SetPicture(const char *pic)
{
	image.Load( pic );
}

void CMenuFramework::CMenuBannerBitmap::Draw( Point pt, Size sz )
{
	if( image.IsValid() )
	{
		UI_DrawPic( pt, sz, uiColorWhite, image, QM_DRAWADDITIVE );
	}
	else
	{
		UI_DrawString( uiStatic.hHeavyBlur, pt,
			sz,
			szName,
			uiPromptTextColor, m_scChSize,
			QM_LEFT, ETF_ADDITIVE | ETF_NOSIZELIMIT | ETF_FORCECOL );

		UI_DrawString( uiStatic.hLightBlur, pt,
			sz,
			szName,
			uiPromptTextColor, m_scChSize,
			QM_LEFT, ETF_ADDITIVE | ETF_NOSIZELIMIT | ETF_FORCECOL );
	}
}

void CMenuFramework::CMenuBannerBitmap::Draw()
{
	CMenuFramework *pParent = (CMenuFramework *)Parent();

	// no banners in the theme: the panel title band carries the page name, and
	// the fallback text path here needs the two blur fonts the theme drops
	if( UI_ThemeActive( ))
		return;

	if (pParent && (pParent->bannerAnimDirection == ANIM_OPENING || pParent->eTransitionType == ANIM_OPENING))
		return;

	Draw( m_scPos, m_scSize );
}

