/*
CoFOptions.h -- Cry of Fear options layout: the Controls and Gamepad controls
pages and the cfg-only test hook shared by the four input pages

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifndef COF_OPTIONS_H
#define COF_OPTIONS_H

class CMenuCheckBox;
class CMenuSlider;
class CMenuSpinControl;

// Cry of Fear with the theme: Options lists Game, Controls, Keybinds,
// Gamepad controls, Audio, Video (patches/cof-mainui-options-layout.patch)
bool UI_CoFOptionsLayout( void );

// the two new pages (menus/CoFOptions.cpp)
void UI_CoFControls_Menu( void );	// menu_cofcontrols: aiming, mouse, gamepad settings
void UI_CoFGamepad_Menu( void );	// menu_cofgamepad: the action on each pad button

// menu_cof_options_select <page> ...: each page's part of the test hook
void UI_AdvControls_OptionsProbe( void );	// menus/AdvancedControls.cpp (Game)
void UI_Controls_OptionsProbe( void );		// menus/Controls.cpp (Keybinds)

// a gamepad button (A_BUTTON ... TOUCHPAD): with the options layout the
// Keybinds page leaves these to the Gamepad controls page
bool UI_CoFIsPadKey( int keynum );

// ---------------------------------------------------------------------------
// tabbed windows: Options, Extras, Save/Load. Every page keeps its own class;
// the framework (controls/Framework.cpp) draws one shared tab strip under the
// title band of any page whose panel title belongs to a group, gives it the
// group's common frame size and switches pages in place.
// ---------------------------------------------------------------------------
#define COF_TAB_FRAME_W		960	// one frame for every tabbed window, virtual units
#define COF_TAB_FRAME_H		680	// leaves room under it for the pad hints line
#define COF_TAB_H		32	// a tab
#define COF_TAB_STRIP_H		48	// the strip: gap, tab, hairline, gap

struct cofTab_t
{
	const char *label;		// through L()
	const char *title;		// the page's own panel title (SetPanel)
	void (*show)( void );
	bool (*available)( void );	// NULL: always
};

struct cofTabGroup_t
{
	const char *title;		// drawn in the title band
	const cofTab_t *tabs;
	int count;
};

// the group a page belongs to (by its panel title) and its tab, or NULL;
// Cry of Fear with the theme only
const cofTabGroup_t *UI_CoFTabGroup( const char *panelTitle, int *index );
bool UI_CoFTabAvailable( const cofTabGroup_t *group, int index );

// the last input device: a pad key since the last keyboard key, mouse button
// or mouse move (or the engine's cof_last_input when it exists)
bool UI_CoFPadMode( void );
// UI_KeyEvent / UI_MouseMove hooks (BaseMenu.cpp); true = the key is used up
bool UI_CoFInputEvent( int key, int down );
void UI_CoFInputMouse( void );
// "gfx/shell/gamepad/<pad>/<file>" for the current button style (Nintendo
// and Steam Deck: that style's icon for the same key in icons.txt, else Xbox)
const char *UI_CoFPadGlyph( const char *xboxFile, const char *psFile );
// the button style in force: 0 Xbox, 1 PlayStation, 2 Nintendo, 3 Steam
// Deck (cof_pad_style, Auto resolved through the engine's cof_pad_style_auto)
int UI_CoFPadStyle( void );
// the setting itself: -1 Auto (default), 0..3 chosen by hand
int UI_CoFPadStyleSetting( void );
// once, after config.cfg: an old profile's cof_pad_style 0 becomes Auto
void UI_CoFPadStyleMigrate( void );

// pad / arrow-key focus by position (controls/ItemsHolder.cpp): the index
// of the item the focus moves to from `cur` for a navigation key, -1 for
// none, COF_NAV_STOCK when the stock creation order should handle it
class CMenuBaseItem;
class CMenuItemsHolder;
#define COF_NAV_STOCK	( -2 )
int UI_CoFNavTarget( CMenuItemsHolder *holder, int cur, int key, bool wrap );
bool UI_CoFNavUsable( CMenuBaseItem *item );

// set a control the way a click does (its value, then QM_CHANGED)
void UI_CoFProbeCheck( CMenuCheckBox &box, const char *value );
void UI_CoFProbeSlider( CMenuSlider &slider, const char *value );
void UI_CoFProbeSpin( CMenuSpinControl &spin, const char *value );

#endif // COF_OPTIONS_H
