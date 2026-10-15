// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#ifndef VGUI_XASH3D_SUPPORT_H
#define VGUI_XASH3D_SUPPORT_H

#include "vgui.h"
#include "app.h"
#include "input.h"
#include "panel.h"
#include "surface.h"

#include "vgui_api.h"
#include "coffont.h"

namespace vgui
{
class Font;
class XashSurface;

extern vguiapi_t *g_engine; // the engine half of the API. The structure is owned by the engine, we only keep the pointer

// the root panel the game client library parents its own panels to, and the surface that draws it
extern Panel *g_rootPanel;
extern XashSurface *g_surface;

// the engine 2D drawing path has no scissor test of its own, so we clip rectangles ourselves
class Scissor
{
public:
	Scissor();

	void enable();
	void disable();
	void setRect( int x0, int y0, int x1, int y1 );
	// Cry of Fear, milestone 5a: the in-game message run is deliberately moved
	// off the row its panel put it on, so its clip rectangle has to move with
	// it. Read the current one, move it, put it back.
	void getRect( int *out ) const;

	// intersects the quad with the current rectangle, interpolating the texture coordinates
	// of the clipped corners. False means the quad is entirely outside and must not be drawn
	bool clip( const vpoint_t &topLeft, const vpoint_t &bottomRight, vpoint_t &clippedTopLeft, vpoint_t &clippedBottomRight ) const;

private:
	bool enabled;
	int  rect[4];
};

extern Scissor g_scissor;

// Cry of Fear in-game UI scaling (cofscale.cpp)
void CofUI_Refresh( void );
bool CofUI_RootSize( int &wide, int &tall );
// milestone 4c, for the developer trace: the device render size the transform
// was last refreshed with, and a layout y turned into a device y
void CofUI_ScreenSize( int *wide, int *tall );
int  CofUI_ToDeviceY( int layoutY );
int  CofUI_ToLayoutY( int deviceY );
void CofUI_SolvePanel( Panel *panel );

// Cry of Fear panel identification (cofpanels.cpp): the client's in-play
// panels told apart by class, COF_PANEL_* bits from vgui_api.h, plus this
// library's own plain panel
static const unsigned int COF_CLASS_PLAIN_PANEL = 1u << 31;
void CofPanels_Frame( Panel *root );
unsigned int CofPanels_Class( Panel *panel );
unsigned int CofPanels_Visible( void );
const char *CofPanels_ClassName( Panel *panel, char *buf, size_t size ); // traces only

// Cry of Fear gamepad (cof-gamepad-input): the open panel's own buttons -
// vguiapi_t::CofPanelAction, COF_VGUI_PANEL_* actions (cofpanels.cpp)
int CofPanels_Action( int action, char *info, int infoSize );

// Cry of Fear: the live world behind the in-play panels (cofbackdrop.cpp)
void CofBackdrop_Install( void );
void CofBackdrop_Frame( Panel *root );

// Cry of Fear story co-op: the 12-slot inventory grid over the client's inventory
// panel, every painted frame after CofBackdrop_Frame (cofinventory.cpp)
void CofInventory_Frame( Panel *root );

// input entry points the engine calls (input.cpp)
void XashMouse( VGUI_MouseAction action, int code );
void XashKey( VGUI_KeyAction action, VGUI_KeyCode code );
void XashMouseMove( int x, int y );
void XashTextInput( const char *text );

class XashApp : public App
{
public:
	XashApp() : App( true ) { }
	virtual void main( int, char ** ) override { }

	// Cry of Fear gamepad: the panel the last mouse event went to
	Panel *cofMouseFocus() { return mouseFocusPanel; }
};

class XashSurface : public SurfaceBase
{
public:
	XashSurface( Panel *embeddedPanel );
	~XashSurface();

	// the engine owns the window, all of these are no-op
	virtual void setTitle( const char * ) override;
	virtual bool setFullscreenMode( int, int, int ) override;
	virtual void setWindowedMode() override;
	virtual void setAsTopMost( bool ) override;
	virtual void createPopup( Panel * ) override;
	virtual bool hasFocus() override;
	virtual bool isWithin( int, int ) override;
	virtual int createNewTextureID() override;
	virtual void GetMousePos( int &x, int &y ) override;

	// cof_font_probe: draw the developer string with an engine font, so a
	// language pack's code page can be checked without a map or a mouse
	void cofDrawProbe();
	// Cry of Fear, milestone 4b: draw the collected backing text. Public
	// because the paint pass also has to close out anything drawn outside a
	// push/pop pair, such as the cof_font_probe developer string.
	void flushBackingText( void );

protected:
	virtual void drawSetColor( int r, int g, int b, int a ) override;
	virtual void drawFilledRect( int x0, int y0, int x1, int y1 ) override;
	virtual void drawOutlinedRect( int x0, int y0, int x1, int y1 ) override;
	virtual void drawSetTextFont( Font *font ) override;
	virtual void drawSetTextColor( int r, int g, int b, int a ) override;
	virtual void drawSetTextPos( int x, int y ) override;
	virtual void drawPrintText( const char *text, int textLen ) override;
	virtual void drawSetTextureRGBA( int id, const char *rgba, int wide, int tall ) override;
	virtual void drawSetTexture( int id ) override;
	virtual void drawTexturedRect( int x0, int y0, int x1, int y1 ) override;
	virtual void invalidate( Panel * ) override;
	virtual void enableMouseCapture( bool ) override;
	virtual void setCursor( Cursor *cursor ) override;
	virtual void swapBuffers() override;
	virtual void pushMakeCurrent( Panel *panel, bool useInsets ) override;
	virtual void popMakeCurrent( Panel *panel ) override;
	virtual void applyChanges() override;

private:
	enum
	{
		FONT_PAGE_SIZE  = 512,
		// Cry of Fear engine-rasterised fonts: the chapter title is 3.91 % of
		// the render height, which at 2160p with the HUD scale at 200 % is a
		// 169 px cell - about 18 glyphs to a page, so eight pages truncated the
		// set. Pages are still only uploaded as they fill, so the usual case
		// (four pages for a title font at 2160p) costs nothing extra
		MAX_FONT_PAGES  = 16, // per font
		MAX_PAINT_STACK = 16,
		DEFAULT_COLOR   = 7,
	};

	// vgui doesn't make font atlas, we handle that ourselves
	struct FontCache
	{
		int           id;                   // Font::getId()
		int           pageCount;
		unsigned char page[256];            // atlas page each glyph landed on
		int           texture[MAX_FONT_PAGES];
		float         coord[256][4];        // s0, t0, s1, t1
		int           generation;
	};

	struct PaintState
	{
		Panel *panel; // for validation only
		int translate[2];
		int clip[4];
	};

	FontCache *buildFontCache( Font *font );
	void uploadFontPage( FontCache *cache, int index, const unsigned char *pixels );
	void makeCurrent( const PaintState *ps );

	int   drawColor[4];
	int   textColor[4];
	int   textPos[2];
	int   translate[2];
	Font *currentFont;

	FontCache  *currentFontCache;
	static Dar<FontCache *> fontCacheDar;
	static int fontGeneration;

	// ^N colour escape state, carried across drawPrintText calls
	bool sawCaret;
	int  colorIndex;

	PaintState paintStack[MAX_PAINT_STACK];
	int paintStackPos;

	// -------------------------------------------------------------------
	// Cry of Fear, milestone 4b: the translucent backing strip behind the
	// notes / document reader text.
	//
	// The same problem the engine HUD text has: a line's extent is only
	// known once its last glyph has been emitted, and by then a strip would
	// paint over the text. So the glyphs of a backing font are collected
	// instead of drawn, grouped by their baseline y, and flushed - strip
	// first, glyphs second - when the panel that drew them pops its paint
	// state, which is the last moment its translate and scissor are still
	// current. TextImage, the layout path behind every client label, calls
	// drawPrintText once PER CHARACTER, so nothing smaller than a whole
	// panel could group a line.
	//
	// Only fonts whose role the engine gave a non-zero backing alpha take
	// this path (CofFont_Backing), which today is the "pager" role alone.
	// -------------------------------------------------------------------
	enum
	{
		MAX_TEXT_BACKING_GLYPHS = 2048,
		MAX_TEXT_BACKING_LINES  = 96,
		// Cry of Fear, milestone 4c: how many lines a run may have and still
		// count as in-game message text. The role a client font gets is the
		// scheme strip it loaded first, and "credits" is shared by the message
		// strip, the intro text card and the credits roll - so the role alone
		// cannot tell a one-line pickup notification from a seven-line card.
		// A run longer than this keeps the client's own placement and gets no
		// backing strip, which is what leaves those two untouched.
		MAX_MESSAGE_LINES       = 3,
		// Cry of Fear, milestone 5a: WHERE a run has to already be to count as
		// the in-game message strip, as a percentage of the render height,
		// measured before it is moved.
		//
		// The role is not enough on its own. CHUDControl carries TWO full-width
		// 64 px "credits" Labels: the message strip, which the client puts at
		// ScreenHeight - 3*G, and a second one pinned to the top of the screen
		// which is where the hints and "Press E" prompts appear. Retargeting by
		// role alone dragged the hint bar down onto the message row as well.
		//
		// Measured device rows of the message strip, HUD scale 150 %:
		// 79.7 % at 1080p, 71.8 % at 1440p, 56.0 % at 2160p (it climbs because
		// the client's cinematic-bar unit G is computed once from the real
		// screen height and then used against the transformed one). The top
		// Label sits at 0 %. The band is generous on both sides of the first
		// group and excludes the second outright.
		MSG_BAND_LOW_PCT        = 35,
		MSG_BAND_HIGH_PCT       = 90,
	};

	struct BackingGlyph
	{
		int        ch;
		int        x, y;      // already translated, pre-clip
		int        w, tall;
		int        color[4];
		FontCache *cache;
	};

	struct BackingLine
	{
		int y, tall, minx, maxx, first, count;
	};

	BackingGlyph backingGlyphs[MAX_TEXT_BACKING_GLYPHS];
	BackingLine  backingLines[MAX_TEXT_BACKING_LINES];
	int          backingGlyphCount;
	int          backingLineCount;
	int          backingAlpha;
	bool         textMessageStyle;
	// milestone 4c: the device y this font's text belongs at (-1 for every
	// role that keeps the client's own placement), whether to log it, and the
	// role name the log prints
	int          textYTarget;
	int          textTrace;
	const char  *textRole;

	bool deferBackingGlyph( int ch, int x, int y, int w, int tall, const int *color );
	// returns how far the run was moved, so flushBackingText() can move the
	// clip rectangle the same distance
	int  applyTextYTarget( void );
	// short enough to be a message rather than the intro card or the credits
	// roll, which share the "credits" role with it
	bool runIsShortEnough( void ) const;
	// ...and sitting where the message strip sits, see MSG_BAND_*_PCT
	bool runIsMessage( void ) const;
	// Cry of Fear, milestone 5b: a short run of a retargeted role that sits
	// ABOVE the message band, i.e. CHUDControl's top hint bar. It keeps the
	// client's own placement (runIsMessage is false for it) and the engine's
	// SemiBold face, but it must NOT get a backing strip: that bar already
	// paints a background of its own and a second strip over it reads as a
	// black box around the hint.
	bool runIsTopBand( void ) const;
};
}

#endif // VGUI_XASH3D_SUPPORT_H
