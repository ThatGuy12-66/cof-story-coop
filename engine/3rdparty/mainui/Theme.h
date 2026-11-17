/*
Theme.h -- Source-style theme for the Cry of Fear unified UI (milestone 3a)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

------------------------------------------------------------------------------

One place for every colour, metric and primitive the minimalist Source-era look
needs. The theme is a compile-time addition guarded by the archived cvar
"ui_theme" (1 = this theme, 0 = the upstream WON look); the value is sampled
once in UI_Init, because turning it on also picks the font family and rewrites
the library's colour globals.

Nothing here loads a bitmap: Cry of Fear ships no gfx/shell artwork at all, so
checkboxes, sliders, spinner arrows, table arrows and the panel close button are
drawn from filled rectangles instead of pictures.

All sizes below are in the 768-unit-tall virtual space (UI_VidInit), not pixels.
*/
#ifndef MAINUI_THEME_H
#define MAINUI_THEME_H

#include "Primitive.h"

// ---------------------------------------------------------------------------
// palette, packed 0xAARRGGBB as PackRGBA builds it
// ---------------------------------------------------------------------------
#define THEME_SCRIM         0x96000000u // 0 0 0 150      full-screen dim behind a modal panel
#define THEME_PANEL         0xE6202020u // 32 32 32 230   dialog body
#define THEME_PANEL_BORDER  0xC8969696u // 150 150 150 200
#define THEME_TITLE_BAND    0xF0141414u // 20 20 20 240
#define THEME_TITLE_TEXT    0xFFDCDCDCu // 220 220 220
#define THEME_TEXT          0xFFBEBEBEu // 190 190 190    body, rows, items at rest
#define THEME_TEXT_DIM      0xFF8C8C8Cu // 140 140 140    labels above controls, hints
#define THEME_TEXT_HI       0xFFF0F0F0u // 240 240 240    hovered
// The accent is Cry of Fear's deep red, in two strengths (menufix round,
// 2026-09-24): a light red for text and thin strokes on the dark panel
// (focused frames, ticks, knobs, spinner and list arrows, selected-row
// text, the gamepad marker), which keeps 4.5:1 or more against the panel
// even over a white scene, and the deep red itself for fills, bars and
// underlines (active tab underline, slider fill, selection fill).
#define THEME_ACCENT        0xFFFF524Au // 255 82 74      light red: text and thin strokes
#define THEME_ACCENT_FILL   0xFFA31515u // 163 21 21      deep red: fills, bars, underlines
#define THEME_SELECT_FILL   0x40A31515u // 163 21 21 @ 64 behind the selected row / tab
#define THEME_HOVER_FILL    0x14FFFFFFu
#define THEME_PRESS_FILL    0x28FFFFFFu
#define THEME_DISABLED      0xFF646464u // 100 100 100
#define THEME_INPUT_BG      0xC80C0C0Cu // 12 12 12 200
#define THEME_INPUT_BORDER  0xFF5A5A5Au // 90 90 90
#define THEME_TRACK         0xFF3C3C3Cu // 60 60 60
#define THEME_SEPARATOR     0x40FFFFFFu
// The death page has no panel and no scrim, so each of its text lines carries
// its own faint dark backing strip - the same trick Cry of Fear's own GAME
// OVER panel uses to stay readable over an arbitrary scene.
#define THEME_DEATH_BACKING 0x5A000000u // 0 0 0 90

// ---------------------------------------------------------------------------
// type scale, "tall" values handed to CFontBuilder (multiplied by scaleY)
// ---------------------------------------------------------------------------
#define THEME_TITLE_TALL    17
#define THEME_LABEL_TALL    16
#define THEME_BODY_TALL     19
#define THEME_BUTTON_TALL   19
#define THEME_ITEM_TALL     24
#define THEME_LOGO_TALL     46
#define THEME_LOGOSUB_TALL  20

// ---------------------------------------------------------------------------
// panel metrics
// ---------------------------------------------------------------------------
#define THEME_TITLE_BAND_H  34
#define THEME_CLOSE_SIZE    26
#define THEME_CLOSE_INSET   6
#define THEME_PAD_X         24
#define THEME_PAD_TOP       20
#define THEME_PAD_BOTTOM    20
#define THEME_BTNROW_H      32
#define THEME_BTNROW_GAP    20
#define THEME_BTN_MIN_W     110
#define THEME_BTN_GAP       12
#define THEME_GUTTER2       28
#define THEME_GUTTER3       22
#define THEME_ROW_H         30 // a full-width selectable row (difficulty, language, ...)
#define THEME_ROW_PITCH     34
#define THEME_CTRL_H        30 // spin control, edit field, drop-down
#define THEME_CTRL_PITCH    62 // label line + control + gap
#define THEME_CHECK_SIZE    22
#define THEME_SLIDER_TRACK  6
#define THEME_SLIDER_H      26

// main menu (centred) and pause menu (lower left)
#define THEME_MENU_MARGIN   64
#define THEME_MENU_PITCH    34
#define THEME_MENU_BOTTOM   96 // pause menu: last item baseline sits at 768 - this
#define THEME_MENU_CENTER_PCT 52 // main menu: the logo+list block is centred on this % of the height
#define THEME_LOGO_GAP      28 // between the logo block and the first item
#define THEME_LOGO_GAP_CENTER 48
#define THEME_LOGO_TRACKING 4  // extra letter spacing for "CRY OF FEAR", virtual units

// death page: the wordmark-style title and the two links, as percentages of
// the 768-unit height, matching the original CGameOver panel's own placement
#define THEME_DEATH_TITLE_PCT 35
#define THEME_DEATH_ITEM_PCT  60
#define THEME_DEATH_PITCH     36 // between "Load Game" and "Exit"
#define THEME_DEATH_PAD_X     28 // backing strip padding, virtual units
#define THEME_DEATH_PAD_Y     6

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------
extern bool g_bThemeActive;

inline bool UI_ThemeActive( void ) { return g_bThemeActive; }

// registers ui_theme, samples it, and rewrites the uiColor* globals.
// Called from UI_Init *before* UI_ApplyCustomColors, so a gfx/shell/colors.lst
// is still the last word on the seven keys it can reach (it cannot set alpha).
void UI_ThemeInit( void );

// CoF menu geometry multiplier (sampled at VidInit; 1 restores prior size).
void UI_ThemeApplyMenuScale( void );
float UI_ThemeMenuScaleApplied( void );

// Defaults that have to be applied *after* config.cfg has run, because
// UI_Init happens inside CL_Init, before Host_Init execs it. Called once from
// the same UI_UpdateMenu block that loads the backgrounds and the strings.
void UI_ThemeApplyDeferredDefaults( void );

// the GDI family the theme asks for, and the chain tried after it
const char *UI_ThemeFontFamily( void );
const char *UI_ThemeFontFallback( int i ); // NULL when the chain is exhausted

// ---------------------------------------------------------------------------
// hover / focus
//
// Every themed control draws its "hot" state from the item's own focus flags,
// never from "this == m_pParent->ItemAtCursor()". CMenuItemsHolder::MouseMove
// leaves m_iCursor on the last item the pointer touched, so the cursor-index
// test keeps an item lit after the cursor has left it - the hover that never
// clears. QMF_HASMOUSEFOCUS is set only on the item the pointer is inside and
// cleared on every other item on each move; QMF_HASKEYBOARDFOCUS follows arrow
// key navigation and is dropped as soon as the mouse moves focus elsewhere. So
// the pointer wins while it is over an item, the keyboard item stays lit when
// the pointer is over nothing, and nothing is lit when neither applies.
// ---------------------------------------------------------------------------
#define UI_THEME_HOT( item ) \
	( FBitSet(( item )->iFlags, QMF_HASMOUSEFOCUS | QMF_HASKEYBOARDFOCUS ) != 0 )

// ---------------------------------------------------------------------------
// primitives
// ---------------------------------------------------------------------------

// one physical pixel at 1080p, two at 2160p - never the scaled 4-unit outline
int  UI_ThemeHairline( void );

// a hairline border *outside* the given (already scaled) rect, like every other
// UI_DrawRectangle call site in the library
void UI_DrawHairline( Point pos, Size size, unsigned int color );

// a filled 1px separator line inside a rect
void UI_ThemeSeparator( int x, int y, int w, unsigned int color );

// the death page's per-line backing strip, around an already-scaled text box
void UI_ThemeDeathBacking( int x, int y, int w, int h );

// While this is set the in-game background draws nothing at all, so the live
// scene shows through untouched. Only the death page uses it: Cry of Fear's own
// GAME OVER screen does not dim the world, and neither should ours.
void UI_ThemeSetSceneUnveiled( bool on );
bool UI_ThemeSceneUnveiled( void );

// diagonal cross, used for the panel close button. Scaled coordinates.
void UI_ThemeDrawCross( Point pos, Size size, unsigned int color );

// solid triangle from filled rows. dir: 0 left, 1 right, 2 up, 3 down.
void UI_ThemeDrawTriangle( Point pos, Size size, int dir, unsigned int color );

// a tick mark for a checked checkbox
void UI_ThemeDrawTick( Point pos, Size size, unsigned int color );

// width of a themed button, in virtual units, from the button font metrics
int  UI_ThemeButtonWidth( const char *name );

// panel fill, hairline border, title band and uppercase title. Scaled
// coordinates. Shared by CMenuFramework's panel mode and the message boxes.
void UI_ThemeDrawDialogPanel( Point pos, Size size, const char *title );

// ---------------------------------------------------------------------------
// message panels
// ---------------------------------------------------------------------------

// Number of lines UI_DrawString will wrap str into inside widthPx, counting
// explicit newlines. Mirrors UI_DrawString's own word wrapping.
int  UI_ThemeWrappedLines( int font, const char *str, int charH, int widthPx );

// Panel size, in virtual units, for a message box holding str: wide enough for
// a readable measure, tall enough for the wrapped message plus the button row.
// Never taller than the 768-unit space.
void UI_ThemeMessagePanelSize( const char *str, int *w, int *h );

// UI_DrawString with extra advance between glyphs; returns the drawn width.
// Used only by the main-menu logo, the one place the look wants tracking.
int  UI_ThemeDrawTracked( int font, int x, int y, const char *str,
	unsigned int color, int charH, int tracking );
int  UI_ThemeTrackedWidth( int font, const char *str, int charH, int tracking );

#endif // MAINUI_THEME_H
