/*
Framework.h -- base menu fullscreen root window
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
#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include "BaseWindow.h"
#include "PicButton.h"
#include "Primitive.h"

#define MAX_FRAMEWORK_PICBUTTONS 16

/*
 * WON-style menu framework
 */
class CMenuFramework : public CMenuBaseWindow
{
public:
	typedef CMenuBaseWindow BaseClass;

	CMenuFramework( const char *name = "Unnamed Framework" );
	virtual ~CMenuFramework() override;

	void Show() override;
	void Draw() override;
	void Init() final override;
	void VidInit() final override;
	void Hide() override;
	bool IsRoot() const override { return true; }

	bool KeyDown( int key ) override;

	CMenuPicButton *AddButton( const char *szName, const char *szStatus,
		EDefaultBtns iButton, CEventCallback onReleased = CEventCallback(), int iFlags = 0 );

	CMenuPicButton *AddButton( const char *szName, const char *szStatus,
		const char *szButtonPath, CEventCallback onReleased = CEventCallback(), int iFlags = 0, int hotkey = 0 );

	void RealignButtons();

	bool DrawAnimation() override;

	void PrepareBannerAnimation( EAnimation direction, CMenuPicButton *initiator );

private:
	// Cry of Fear tabbed windows and pad input (Framework.cpp)
	bool TabsKeyDown( int key );
	void GoToTab( int index );
	Rect TabRect( int index ) const;
	void DrawTabStrip( void );
	void DrawPadHints( void );
	void FocusFirstItem( void );
public:

	// -----------------------------------------------------------------------
	// Source-style panel mode (Theme.h)
	//
	// The framework stays a full-screen holder and the panel is a rectangle
	// drawn inside it, so every child keeps screen-virtual coordinates. That
	// matters: several menus position lists with negative coordinates meaning
	// "from the right/bottom edge of the parent" (LoadGame, Controls,
	// CustomGame, CryOfFear), and turning the framework itself into the panel
	// would silently re-anchor all of them.
	//
	// Layout helpers return absolute virtual units, ready for SetRect.
	// -----------------------------------------------------------------------
	void SetPanel( const char *title, int w, int h );
	bool IsPanel() const { return bPanel && UI_ThemeActive(); }
	Point PanelPos() const;
	Size  PanelSize() const { return panelSize; }
	Rect  ContentRect() const;
	Rect  ColumnRect( int index, int count ) const;
	Rect  ColumnRect2( int index, float firstFrac ) const;
	int   ButtonRowY() const;

	// A row in the content area: full width, THEME_ROW_H tall, pitched by
	// THEME_ROW_PITCH from the top of the content rect.
	Rect  RowRect( int index ) const;

	// Cry of Fear tabbed windows and pad input (menus/CoFOptions.cpp): a page
	// whose panel title belongs to a tab group (Options, Extras, Save/Load)
	// gets the group's frame, a tab strip under the title band (ContentRect
	// starts below it), LB/RB and a mouse click on a tab switch pages in
	// place. pPadSecondary is what X does on a pad (e.g. Use defaults).
	int   TabStripH() const;
	CMenuBaseItem *pPadSecondary;

	void  DrawPanelChrome();

	// right-aligned button row, left to right in the order given
	void  LayoutButtonRow( CMenuPicButton **btns, int count );

	// The panel close button. Two diagonals of filled squares, because there is
	// no line primitive and Cry of Fear ships no gfx/shell artwork.
	class CMenuCloseButton : public CMenuBaseItem
	{
	public:
		CMenuCloseButton();
		void Draw() override;
		bool KeyUp( int key ) override;
		bool KeyDown( int key ) override;
	} closeBtn;

	class CMenuBannerBitmap : public CMenuBaseItem
	{
	public:
		CMenuBannerBitmap();
		void Draw() override;
		void SetPicture( const char *pic );

		void Draw( Point pt, Size sz );

	private:
		CImage image;
	} banner;

protected:
	EAnimation bannerAnimDirection;
	Rect bannerRects[2];

	CMenuPicButton *m_apBtns[MAX_FRAMEWORK_PICBUTTONS];
	int m_iBtnsNum;

	bool bPanel;
	bool bCloseAdded;
	Size panelSize;
	const char *szPanelTitle;
};

#endif // FRAMEWORK_H
