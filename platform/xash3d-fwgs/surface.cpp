// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#include "vgui_internal.h"
#include "font.h"
#include "support.h"
#include "coffont.h"
#include "cof_text_style.h"

using namespace vgui;

Dar<XashSurface::FontCache *> XashSurface::fontCacheDar;
int XashSurface::fontGeneration = 0;

static bool Q_isspace( int ch ) // locale independent
{
	return ch == ' ' || ( ch >= '\t' && ch <= '\r' );
}

XashSurface::XashSurface( Panel *embeddedPanel ) :
	SurfaceBase( embeddedPanel ),
	currentFont( nullptr ), currentFontCache( nullptr ),
	sawCaret( false ), colorIndex( DEFAULT_COLOR ), paintStackPos( 0 ),
	backingGlyphCount( 0 ), backingLineCount( 0 ), backingAlpha( 0 ), textMessageStyle( false ),
	textYTarget( -1 ), textTrace( 0 ), textRole( "" )
{
	// don't forget alpha is inverted in VGUI... 255 means fully transparent
	for( int i = 0; i < 4; i++ )
		drawColor[i] = textColor[i] = 255;

	textPos[0] = textPos[1] = 0;
	translate[0] = translate[1] = 0;

	// DrawInit/DrawShutdown cleans up textures
	for( int i = 0; i < fontCacheDar.getCount(); i++ )
		delete fontCacheDar[i];

	fontCacheDar.removeAll();
	fontGeneration++;
}

XashSurface::~XashSurface()
{
	g_engine->DrawShutdown();
}

void XashSurface::setTitle( const char * )
{
	// no-op in the game engine context
}

bool XashSurface::setFullscreenMode( int, int, int )
{
	// no-op in the game engine context
	return false;
}

void XashSurface::setWindowedMode()
{
	// no-op in the game engine context
}

void XashSurface::setAsTopMost( bool )
{
	// no-op in the game engine context
}

void XashSurface::createPopup( Panel * )
{
	// no-op in the game engine context
}

bool XashSurface::hasFocus()
{
	// no-op in the game engine context
	return true;
}

bool XashSurface::isWithin( int, int )
{
	// the surface covers the whole virtual screen
	return true;
}

int XashSurface::createNewTextureID()
{
	return g_engine->GenerateTexture();
}

void XashSurface::GetMousePos( int &x, int &y )
{
	g_engine->GetCursorPos( &x, &y );
}

void XashSurface::invalidate( Panel * )
{
}

void XashSurface::enableMouseCapture( bool )
{
	// the engine delivers all input to us regardless, so there is nothing to capture
}

void XashSurface::setCursor( Cursor *newCursor )
{
	cursor = newCursor;

	// bitmap cursors report DC_USER, which the engine platform layer hides just like DC_NONE
	if( cursor )
		g_engine->CursorSelect( (VGUI_DefaultCursor)cursor->getDefaultCursor());
}

void XashSurface::swapBuffers()
{
	// no-op in the game engine context
}

void XashSurface::applyChanges()
{
}

void XashSurface::drawSetColor( int r, int g, int b, int a )
{
	drawColor[0] = r;
	drawColor[1] = g;
	drawColor[2] = b;
	drawColor[3] = a;
}

void XashSurface::drawSetTextColor( int r, int g, int b, int a )
{
	textColor[0] = r;
	textColor[1] = g;
	textColor[2] = b;
	textColor[3] = a;
}

void XashSurface::drawSetTextPos( int x, int y )
{
	textPos[0] = x;
	textPos[1] = y;
}

void XashSurface::drawFilledRect( int x0, int y0, int x1, int y1 )
{
	if( drawColor[3] >= 255 ) // fully transparent
		return;

	vpoint_t topLeft, bottomRight, clippedTopLeft, clippedBottomRight;

	topLeft.point[0] = x0 + translate[0];
	topLeft.point[1] = y0 + translate[1];
	bottomRight.point[0] = x1 + translate[0];
	bottomRight.point[1] = y1 + translate[1];
	topLeft.coord[0] = topLeft.coord[1] = 0.0f;
	bottomRight.coord[0] = bottomRight.coord[1] = 0.0f;

	if( !g_scissor.clip( topLeft, bottomRight, clippedTopLeft, clippedBottomRight ))
		return;

	// Cry of Fear, milestone 5b: the same trace flag the text collector uses
	// (cof_hud_text_trace), extended to the client's own filled rectangles.
	// The chapter card's decorative rule is one of these and there was no
	// other way to tell which draw produced the line through the title.
	if( CofFont_TraceOn( ) && g_engine && g_engine->CofPrint )
	{
		char	msg[256];

		snprintf( msg, sizeof( msg ),
			"[cof-vguirect] role \"%s\" rect %i,%i..%i,%i translate %i,%i"
			" device_y %i..%i colour %i,%i,%i,%i\n",
			textRole ? textRole : "", x0, y0, x1, y1, translate[0], translate[1],
			CofUI_ToDeviceY( y0 + translate[1] ), CofUI_ToDeviceY( y1 + translate[1] ),
			drawColor[0], drawColor[1], drawColor[2], drawColor[3] );
		g_engine->CofPrint( msg );
	}

	g_engine->SetupDrawingRect( drawColor );
	g_engine->EnableTexture( false );
	g_engine->DrawQuad( &clippedTopLeft, &clippedBottomRight );
	g_engine->EnableTexture( true ); // the flag is engine global, restore it
}

void XashSurface::drawOutlinedRect( int x0, int y0, int x1, int y1 )
{
	if( drawColor[3] >= 255 )
		return;

	drawFilledRect( x0, y0, x1, y0 + 1 );
	drawFilledRect( x0, y1 - 1, x1, y1 );

	// inset vertically so the corners aren't drawn twice
	drawFilledRect( x0, y0 + 1, x0 + 1, y1 - 1 );
	drawFilledRect( x1 - 1, y0 + 1, x1, y1 - 1 );
}

void XashSurface::drawTexturedRect( int x0, int y0, int x1, int y1 )
{
	vpoint_t topLeft, bottomRight, clippedTopLeft, clippedBottomRight;

	topLeft.point[0] = x0 + translate[0];
	topLeft.point[1] = y0 + translate[1];
	bottomRight.point[0] = x1 + translate[0];
	bottomRight.point[1] = y1 + translate[1];
	topLeft.coord[0] = topLeft.coord[1] = 0.0f;
	bottomRight.coord[0] = bottomRight.coord[1] = 1.0f;

	if( !g_scissor.clip( topLeft, bottomRight, clippedTopLeft, clippedBottomRight ))
		return;

	if( CofFont_TraceOn( ) && g_engine && g_engine->CofPrint )
	{
		char	msg[256];

		snprintf( msg, sizeof( msg ),
			"[cof-vguitex] role \"%s\" rect %i,%i..%i,%i translate %i,%i"
			" device_y %i..%i colour %i,%i,%i,%i\n",
			textRole ? textRole : "", x0, y0, x1, y1, translate[0], translate[1],
			CofUI_ToDeviceY( y0 + translate[1] ), CofUI_ToDeviceY( y1 + translate[1] ),
			drawColor[0], drawColor[1], drawColor[2], drawColor[3] );
		g_engine->CofPrint( msg );
	}

	// the draw colour, not the text colour
	g_engine->SetupDrawingImage( drawColor );
	g_engine->DrawQuad( &clippedTopLeft, &clippedBottomRight );
}

void XashSurface::drawSetTextureRGBA( int id, const char *rgba, int wide, int tall )
{
	g_engine->UploadTexture( id, rgba, wide, tall );
}

void XashSurface::drawSetTexture( int id )
{
	g_engine->BindTexture( id );
}

void XashSurface::uploadFontPage( FontCache *cache, int index, const unsigned char *pixels )
{
	if( !cache->texture[index] )
		cache->texture[index] = g_engine->GenerateTexture();

	g_engine->UploadTexture( cache->texture[index], (const char *)pixels, FONT_PAGE_SIZE, FONT_PAGE_SIZE );
}

XashSurface::FontCache *XashSurface::buildFontCache( Font *font )
{
	const int pageBytes = FONT_PAGE_SIZE * FONT_PAGE_SIZE * 4;
	FontCache *cache = new FontCache;
	unsigned char *pixels = new unsigned char[pageBytes];
	int tall = font->getTall();
	int x = 0, y = 0, pageIndex = 0;
	bool pageLimitHit = false;

	memset( cache, 0, sizeof( *cache ));
	memset( pixels, 0, pageBytes );

	// Cry of Fear engine-rasterised fonts: the atlas is packed and rasterised
	// in DEVICE pixels, while everything the client and drawPrintText() see is
	// reported in logical ones. That split is what makes the glyph land on
	// exactly the pixels it was drawn for once the engine scales the quad
	CofFont *cof = CofFont_Cast( font );

	if( cof )
		tall = cof->deviceTall();

	cache->id = font->getId();
	cache->generation = fontGeneration;

	// a1ba, TODO: use lightmap atlas generator here later
	for( int ch = 0; ch < 256; ch++ )
	{
		int a, b, c;

		// whitespace is never drawn and a wide space glyph would waste a lot of the page
		if( Q_isspace( ch ))
			continue;

		if( cof )
			cof->deviceABCwide( ch, a, b, c );
		else font->getCharABCwide( ch, a, b, c );

		if( b <= 0 ) // nothing in the face for this byte
			continue;

		if( x + b > FONT_PAGE_SIZE ) // doesn't fit in this row anymore
		{
			x = 0;
			y += tall + 1;

			if( y + tall > FONT_PAGE_SIZE ) // ...and the new row doesn't fit on this page
			{
				uploadFontPage( cache, pageIndex, pixels );

				if( ++pageIndex >= MAX_FONT_PAGES )
				{
					pageLimitHit = true;
					break;
				}

				memset( pixels, 0, pageBytes );
				x = y = 0;
			}
		}

		font->getCharRGBA( ch, x, y, FONT_PAGE_SIZE, FONT_PAGE_SIZE, pixels );

		cache->page[ch] = (unsigned char)pageIndex;
		cache->coord[ch][0] = (float)((double)x / FONT_PAGE_SIZE );
		cache->coord[ch][1] = (float)((double)y / FONT_PAGE_SIZE );
		cache->coord[ch][2] = (float)((double)( x + b ) / FONT_PAGE_SIZE );
		cache->coord[ch][3] = (float)((double)( y + tall ) / FONT_PAGE_SIZE );

		x += b + 1; // one pixel of padding
	}

	if( !pageLimitHit ) // the last, partially filled page
		uploadFontPage( cache, pageIndex++, pixels );

	cache->pageCount = pageIndex;

	delete[] pixels;

	// entries are appended, never updated in place: a rebuilt entry has to take precedence
	// over the one it replaces, and the lookup below is what makes that happen
	fontCacheDar.addElement( cache );

	return cache;
}

void XashSurface::cofDrawProbe()
{
	char	text[256], role[64];

	if( !g_engine || !g_engine->CofFontProbe )
		return;

	if( !g_engine->CofFontProbe( text, sizeof( text ), role, sizeof( role )))
		return;

	Font *font = CofFont_ForRole( role );

	if( !font )
		return;

	int length = (int)strlen( text );

	if( length <= 0 )
		return;

	drawSetTextFont( font );
	drawSetTextColor( 255, 216, 0, 0 ); // alpha is inverted in VGUI: 0 is opaque
	drawSetTextPos( 12, 12 );
	drawPrintText( text, length );
}

void XashSurface::drawSetTextFont( Font *font )
{
	// Cry of Fear engine-rasterised VGUI fonts: this is the one place the
	// surface learns which font the client wants, so it is where the engine's
	// own font takes over. Font_Active() returns the client's font unchanged
	// when cof_ui_inter_fonts is 0 or the role is unknown
	font = Font_Active( font );

	// Cry of Fear, milestone 4b/4c: a change of font can change whether the
	// text gets a backing strip and where it is placed, so anything already
	// collected under the old answers has to be drawn before they change
	int wantBacking = 0, wantTarget = -1, wantTrace = 0;
	const char *wantRole = "";

	if( !CofFont_Info( font, &wantBacking, &wantTarget, &wantTrace, &wantRole ))
		wantRole = "(client font)";	// not substituted: the trace should say so

	// the trace flag is global, not per font, so text the engine did not
	// substitute is traced too
	wantTrace = CofFont_TraceOn() ? 1 : 0;

	const bool wantStyle = CofFont_MessageStyle( font );
	if( wantBacking != backingAlpha || wantTarget != textYTarget || wantTrace != textTrace || wantStyle != textMessageStyle )
		flushBackingText();

	backingAlpha = wantBacking;
	textMessageStyle = wantStyle;
	textYTarget = wantTarget;
	textTrace = wantTrace;
	textRole = wantRole;

	currentFont = font;
	currentFontCache = nullptr;

	// a null font is a valid latch, subsequent text draws then do nothing
	if( !font )
		return;

	int id = font->getId();

	for( int i = 0; i < fontCacheDar.getCount(); i++ )
	{
		if( fontCacheDar[i]->id == id )
			currentFontCache = fontCacheDar[i]; // scan on, the last match wins
	}

	// an entry from a previous surface carries texture ids that no longer exist. In practice
	// this never fires, the constructor already cleared the cache
	if( currentFontCache && currentFontCache->generation != fontGeneration )
		currentFontCache = nullptr;

	if( !currentFontCache )
		currentFontCache = buildFontCache( font );
}

// VGUI uses inverse alpha. Both direct overflow draws and deferred lines
// share this renderer, so fade/clip handling cannot diverge.
static void CofDrawStyledQuad( vpoint_t topLeft, vpoint_t bottomRight, const int *color, bool styled, int pass, int tall )
{
	int ink[4];
	if( styled ) CoF_TextStyleColor( color[0], color[1], color[2], 255 - color[3], 0, ink );
	else { for( int i = 0; i < 4; i++ ) ink[i] = color[i]; ink[3] = 255 - color[3]; }
	for( int tap = 0; tap < ( pass == 0 ? 5 : 1 ); tap++ )
	{
		vpoint_t a = topLeft, b = bottomRight, ca, cb;
		int draw[4] = { ink[0], ink[1], ink[2], 255 - ink[3] };
		if( pass == 0 )
		{
			float dx, dy; int weight;
			CoF_TextShadowTap( tall, tap, &dx, &dy, &weight );
			a.point[0] += dx; b.point[0] += dx;
			a.point[1] += dy; b.point[1] += dy;
			draw[0] = 24; draw[1] = 25; draw[2] = 27;
			draw[3] = 255 - ink[3] * weight / 255;
		}
		if( draw[3] >= 255 ) continue;
		if( g_scissor.clip( a, b, ca, cb ))
		{
			g_engine->SetupDrawingImage( draw );
			g_engine->DrawQuad( &ca, &cb );
		}
	}
}

void XashSurface::drawPrintText( const char *text, int textLen )
{
	if( !text || textLen <= 0 )
		return;

	if( !currentFont || !currentFontCache )
		return;

	if( textColor[3] >= 255 )
		return;

	// FIXME: remove this, a remnant of original Xash3D feature with colorcodes in VGUI
	// horribly breaks mods
	if( textLen == 1 )
	{
		if( text[0] == '^' )
		{
			sawCaret = true;
			return;
		}

		if( sawCaret && text[0] >= '0' && text[0] <= '9' )
		{
			colorIndex = text[0] - '0';
			sawCaret = false;
			return;
		}

		sawCaret = false;
	}

	int color[4];

	if( colorIndex == DEFAULT_COLOR )
	{
		memcpy( color, textColor, sizeof( color ));
	}
	else
	{
		for( int i = 0; i < 3; i++ )
			color[i] = g_engine->GetColor( bound( 0, colorIndex, DEFAULT_COLOR ), i );

		color[3] = textColor[3]; // alpha always comes from the latched text colour
	}

	int x = textPos[0] + translate[0];
	int y = textPos[1] + translate[1];
	int tall = currentFont->getTall();
	int advance = 0;

	for( int i = 0; i < textLen; i++ )
	{
		int ch = g_engine->ProcessUtfChar( (unsigned char)text[i] );

		if( !ch ) // incomplete multi-byte sequence
			continue;

		if( ch < 0 || ch > 255 ) // the decoder shouldn't hand us these, but the tables are 256 entries
			continue;

		int a, b, c;

		currentFont->getCharABCwide( ch, a, b, c );
		advance += a;

		if( !Q_isspace( ch ) && ( backingAlpha > 0 || textTrace || textYTarget >= 0 || textMessageStyle ) &&
			deferBackingGlyph( ch, x + advance, y, b, tall, color ))
		{
			// Cry of Fear, milestone 4b: collected, drawn in flushBackingText()
			// after this line's backing strip
		}
		else if( !Q_isspace( ch )) // no glyph was rasterised for whitespace
		{
			vpoint_t topLeft, bottomRight, clippedTopLeft, clippedBottomRight;

			// already translated, don't translate again
			topLeft.point[0] = x + advance;
			topLeft.point[1] = y;
			bottomRight.point[0] = topLeft.point[0] + b;
			bottomRight.point[1] = y + tall;
			topLeft.coord[0] = currentFontCache->coord[ch][0];
			topLeft.coord[1] = currentFontCache->coord[ch][1];
			bottomRight.coord[0] = currentFontCache->coord[ch][2];
			bottomRight.coord[1] = currentFontCache->coord[ch][3];

			g_engine->BindTexture( currentFontCache->texture[currentFontCache->page[ch]] );
			if( textMessageStyle ) CofDrawStyledQuad( topLeft, bottomRight, color, true, 0, tall );
			CofDrawStyledQuad( topLeft, bottomRight, color, textMessageStyle, 1, tall );
		}

		advance += b + c;
	}

	// so that consecutive calls continue where this one stopped
	textPos[0] += advance;
}

void XashSurface::makeCurrent( const PaintState *ps )
{
	translate[0] = ps->translate[0];
	translate[1] = ps->translate[1];

	g_scissor.setRect( ps->clip[0], ps->clip[1], ps->clip[2], ps->clip[3] );

	// set the offset so the game can draw relative to panel coordinates,
	// goldsrc here install projection matrix
	g_engine->SetPaintOffset( translate[0], translate[1] );
}

void XashSurface::pushMakeCurrent( Panel *panel, bool useInsets )
{
	if( paintStackPos >= MAX_PAINT_STACK )
	{
		vgui_dprintf( "vgui: paint state stack overflow, broken panel tree?\n" );
		return;
	}

	int inset[4] = { 0, 0, 0, 0 };
	int abs[4], clip[4];

	if( useInsets )
		panel->getInset( inset[0], inset[1], inset[2], inset[3] );

	panel->getAbsExtents( abs[0], abs[1], abs[2], abs[3] );
	panel->getClipRect( clip[0], clip[1], clip[2], clip[3] );

	PaintState *ps = &paintStack[paintStackPos++];

	ps->panel = panel;

	// every coordinate the panel draws at is relative to its inner upper-left corner...
	ps->translate[0] = abs[0] + inset[0];
	ps->translate[1] = abs[1] + inset[1];

	// ...but the clip stays the panel's full clip rect: borders deliberately draw into the inset ring
	memcpy( ps->clip, clip, sizeof( ps->clip ));

	makeCurrent( ps );
}

/*
====================
XashSurface::deferBackingGlyph

Collect one glyph of a backing-strip font. Returns false when the caller has
to draw it itself after all (no room left).
====================
*/
bool XashSurface::deferBackingGlyph( int ch, int x, int y, int w, int tall, const int *color )
{
	BackingLine *line = nullptr;

	if( backingGlyphCount >= MAX_TEXT_BACKING_GLYPHS )
		return false;

	// the line this glyph belongs to: same baseline y. Searched from the end,
	// because a text block is laid out one line at a time
	for( int i = backingLineCount - 1; i >= 0; i-- )
	{
		if( backingLines[i].y == y && backingLines[i].tall == tall )
		{
			line = &backingLines[i];
			break;
		}
	}

	if( !line )
	{
		if( backingLineCount >= MAX_TEXT_BACKING_LINES )
			return false;

		line = &backingLines[backingLineCount++];
		line->y = y;
		line->tall = tall;
		line->minx = x;
		line->maxx = x + w;
		line->first = backingGlyphCount;
		line->count = 0;
	}
	else
	{
		// the slices have to stay contiguous
		if( line->first + line->count != backingGlyphCount )
			return false;

		if( x < line->minx ) line->minx = x;
		if( x + w > line->maxx ) line->maxx = x + w;
	}

	BackingGlyph &g = backingGlyphs[backingGlyphCount++];

	g.ch = ch;
	g.x = x;
	g.y = y;
	g.w = w;
	g.tall = tall;
	g.color[0] = color[0];
	g.color[1] = color[1];
	g.color[2] = color[2];
	g.color[3] = color[3];
	g.cache = currentFontCache;
	line->count++;

	return true;
}

/*
====================
XashSurface::flushBackingText

One strip per collected line, then that line's glyphs over it. Runs while the
drawing panel's translate and scissor are still current, so both the strip and
the glyphs are clipped exactly as the glyphs would have been.

VGUI alpha is inverted - 0 is opaque - so the strip's alpha is 255 minus the
value the engine gave, and 0 from the engine means "no strip" rather than
"opaque".
====================
*/
/*
====================
XashSurface::applyTextYTarget

Cry of Fear, milestone 4c: move the collected run so its first line lands on
the device row the engine named, keeping the spacing of the rest.

Applied to the collected text rather than to the panel on purpose. The panel
that carries this text also carries the health bar, the stamina bar and the
hint bar, and those must stay exactly where the client put them; and the text
is the only part of it whose place the player actually cares about.
====================
*/
bool XashSurface::runIsShortEnough( void ) const
{
	return backingLineCount > 0 && backingLineCount <= MAX_MESSAGE_LINES;
}

bool XashSurface::runIsMessage( void ) const
{
	int	screenTall = 0, deviceY;

	if( textYTarget < 0 || !runIsShortEnough( ))
		return false;

	// Cry of Fear, milestone 5a: and it has to be WHERE the message strip is.
	// CHUDControl's other full-width "credits" Label is the hint bar, pinned to
	// the top of the screen, and moving that onto the message row was wrong in
	// exactly the way the message text being left on its own row was.
	//
	// The test is on the run's own position BEFORE the move. Once the move has
	// happened the run sits on textYTarget, which is inside the band by
	// construction, so the answer does not change between the two calls.
	CofUI_ScreenSize( nullptr, &screenTall );

	if( screenTall <= 0 )
		return true; // no transform to ask: behave as before

	deviceY = CofUI_ToDeviceY( backingLines[0].y );

	return deviceY >= screenTall * MSG_BAND_LOW_PCT / 100 &&
		deviceY <= screenTall * MSG_BAND_HIGH_PCT / 100;
}

/*
====================
XashSurface::runIsTopBand

Cry of Fear, milestone 5b. The other half of the same distinction: a short
"credits"-role run whose own row is ABOVE the message band is CHUDControl's top
hint bar ("Press E to ...", "Using Gas Mask"). Milestone 5a already leaves it
where the client put it; the user then reported that the backing strip under it
looks wrong, and it does - that bar comes with the client's own painted
background, so the strip draws a second, darker box inside it.

So the hint bar keeps the SemiBold face and loses the strip. Asked BEFORE
applyTextYTarget(), like runIsMessage(), for the same reason: after a move the
run no longer sits where it was laid out.
====================
*/
bool XashSurface::runIsTopBand( void ) const
{
	int	screenTall = 0, deviceY;

	if( textYTarget < 0 || !runIsShortEnough( ))
		return false;

	CofUI_ScreenSize( nullptr, &screenTall );

	if( screenTall <= 0 )
		return false; // no transform to ask: behave as before

	deviceY = CofUI_ToDeviceY( backingLines[0].y );

	return deviceY < screenTall * MSG_BAND_LOW_PCT / 100;
}

int XashSurface::applyTextYTarget( void )
{
	if( !runIsMessage( ))
		return 0;

	int delta = CofUI_ToLayoutY( textYTarget ) - backingLines[0].y;

	if( delta == 0 )
		return 0;

	for( int i = 0; i < backingLineCount; i++ )
		backingLines[i].y += delta;

	for( int i = 0; i < backingGlyphCount; i++ )
		backingGlyphs[i].y += delta;

	return delta;
}

/*
====================
CofTextOverflowOn

cof-coop-bridge: the engine's cof_vgui_text_overflow, read off the font
descriptor the same way CofFont_TraceOn reads the trace flag. Only asked for a
line that actually overflows, which is rare.
====================
*/
static bool CofTextOverflowOn( void )
{
	cof_vgui_font_t	desc;

	if( !g_engine || !g_engine->CofFontDesc )
		return false;

	memset( &desc, 0, sizeof( desc ));

	if( !g_engine->CofFontDesc( "pager", &desc ))
		return false;

	return desc.overflow != 0;
}

void XashSurface::flushBackingText( void )
{
	if( backingGlyphCount == 0 )
	{
		backingLineCount = 0;
		return;
	}

	// Cry of Fear, milestone 5a. The run is moved off the row its own panel
	// put it on, but the clip rectangle in force is still that panel's - and
	// the panel that carries this text is a 64 px bar. So as soon as the move
	// is bigger than the few pixels of slack inside that bar, every glyph AND
	// the backing strip are scissored away and the text simply does not
	// appear. That is exactly what happened at 3840x2160: the client places
	// the strip at layout y 381 (its cinematic-bar unit is computed once from
	// the real screen height and then used against the transformed one, so the
	// bar climbs as the display grows - 574 at 1080p, 517 at 1440p, 403 at
	// 2160p), the target row is layout y 562, and a 159 px move out of a 64 px
	// band left nothing on screen.
	//
	// The clip has to travel with the text. Moving it rather than dropping it
	// keeps every other guarantee: the run is still clipped to its panel's
	// width and to a band of its panel's height, just at its new row.
	// milestone 5b: asked before the move, see runIsTopBand()
	const bool	topBand = runIsTopBand();
	int	yDelta = applyTextYTarget();
	int	savedClip[4];
	bool	clipMoved = false;

	if( yDelta != 0 )
	{
		g_scissor.getRect( savedClip );
		g_scissor.setRect( savedClip[0], savedClip[1] + yDelta,
			savedClip[2], savedClip[3] + yDelta );
		clipMoved = true;
	}

	for( int i = 0; i < backingLineCount; i++ )
	{
		const BackingLine &line = backingLines[i];

		if( line.count <= 0 )
			continue;

		if( textTrace )
		{
			char	text[128];
			int	n = line.count < (int)sizeof( text ) - 1 ? line.count : (int)sizeof( text ) - 1;
			int	screenTall = 0;

			for( int j = 0; j < n; j++ )
			{
				int ch = backingGlyphs[line.first + j].ch;

				text[j] = ( ch >= 32 && ch < 127 ) ? (char)ch : '.';
			}
			text[n] = '\0';

			CofUI_ScreenSize( nullptr, &screenTall );

			// vgui_dprintf goes to stderr, which the game log never sees, so
			// the one line the trace wants goes out through the engine
			char	msg[384];
			int	deviceY = CofUI_ToDeviceY( line.y );
			int	panelClip[4];

			// milestone 5a: the clip rectangle the run is drawn under, which
			// is what decided whether it appeared at all before the move above
			g_scissor.getRect( panelClip );

			snprintf( msg, sizeof( msg ),
				"[cof-vguitext] role \"%s\" line y=%i tall=%i x=%i..%i"
				" device_y=%i (%.2f%% of %i) moved=%i clip=%i,%i,%i,%i \"%s\"\n",
				textRole ? textRole : "", line.y, line.tall, line.minx, line.maxx,
				deviceY,
				screenTall > 0 ? deviceY * 100.0f / screenTall : 0.0f,
				screenTall, yDelta,
				panelClip[0], panelClip[1], panelClip[2], panelClip[3], text );

			if( g_engine && g_engine->CofPrint )
				g_engine->CofPrint( msg );
			else vgui_dprintf( "%s", msg );
		}

		// cof-coop-bridge: a line cut on BOTH sides by its own label is a
		// centred caption the client sized for its own, narrower bitmap font
		// - the co-op lobby's "Press MOUSE1 to become ready/unready" and the
		// votekick hint lost their first and last words that way. Such a line
		// may use its parent panel's width (never more), for this line only.
		// A left-aligned line that runs off one edge stays cut, as before.
		// The parent is asked for its clip rectangle directly: the paint
		// stack does not nest panels (Panel::paintTraverse pops a panel
		// before it paints the children).
		int	lineClip[4];
		bool	lineWidened = false;
		Panel	*linePanel = paintStackPos > 0 ? paintStack[paintStackPos - 1].panel : nullptr;
		Panel	*lineParent = linePanel ? linePanel->getParent() : nullptr;

		if( lineParent )
		{
			g_scissor.getRect( lineClip );

			if( line.minx < lineClip[0] && line.maxx > lineClip[2] && CofTextOverflowOn( ))
			{
				int	outer[4];
				int	x0 = line.minx - line.tall / 2;
				int	x1 = line.maxx + line.tall / 2;

				lineParent->getClipRect( outer[0], outer[1], outer[2], outer[3] );

				if( x0 < outer[0] ) x0 = outer[0];
				if( x1 > outer[2] ) x1 = outer[2];

				if( x0 < lineClip[0] || x1 > lineClip[2] )
				{
					g_scissor.setRect( x0 < lineClip[0] ? x0 : lineClip[0], lineClip[1],
						x1 > lineClip[2] ? x1 : lineClip[2], lineClip[3] );
					lineWidened = true;
				}
			}
		}

		// Which runs get a strip, milestone 5b:
		//
		//   pager (textYTarget < 0)     yes - the notes and document reader
		//   message band, short         yes - the pickup lines and subtitles
		//   TOP band, short             NO  - CHUDControl's hint bar already
		//                                     paints its own background, and a
		//                                     strip inside it reads as a black
		//                                     box (user report, 2026-09-22).
		//                                     It keeps the SemiBold face.
		//   longer than MAX_MESSAGE_LINES  NO - the intro text card and the
		//                                     credits roll, as before.
		// Cry of Fear (cof_panel_transparent): the strip is only as opaque as
		// the most opaque glyph on its line. The client fades captions by
		// their text alpha (VGUI1: 255 is invisible), and a strip drawn at
		// full strength under fully faded text was a dark bar under the
		// billboard and the tape recorder page - invisible on the old black
		// backdrop, plain to see over the live world.
		int	lineOpacity = 0;

		for( int j = 0; j < line.count; j++ )
		{
			int opacity = 255 - backingGlyphs[line.first + j].color[3];

			if( opacity > lineOpacity )
				lineOpacity = opacity;
		}

		if( lineOpacity > 255 )
			lineOpacity = 255;

		if( backingAlpha > 0 && lineOpacity > 0 && !topBand && ( textYTarget < 0 || runIsShortEnough( )))
		{
			// padding from the glyph height, so it follows the display the
			// same way the rest of milestone 4 does, and the same shape the
			// engine HUD text and the menu's GAME OVER backing use
			int padx = line.tall / 2;
			int pady = line.tall / 6;

			if( padx < 2 ) padx = 2;
			if( pady < 1 ) pady = 1;

			int saved[4] = { drawColor[0], drawColor[1], drawColor[2], drawColor[3] };

			drawSetColor( 0, 0, 0, 255 - backingAlpha * lineOpacity / 255 );
			drawFilledRect( line.minx - translate[0] - padx, line.y - translate[1] - pady,
				line.maxx - translate[0] + padx, line.y - translate[1] + line.tall + pady );
			drawSetColor( saved[0], saved[1], saved[2], saved[3] );
		}

		// The whole line's shadow precedes all foreground ink.
		for( int pass = textMessageStyle ? 0 : 1; pass < 2; pass++ )
		for( int j = 0; j < line.count; j++ )
		{
			const BackingGlyph &g = backingGlyphs[line.first + j];
			vpoint_t topLeft, bottomRight, clippedTopLeft, clippedBottomRight;

			if( !g.cache )
				continue;

			topLeft.point[0] = g.x;
			topLeft.point[1] = g.y;
			bottomRight.point[0] = g.x + g.w;
			bottomRight.point[1] = g.y + g.tall;
			topLeft.coord[0] = g.cache->coord[g.ch][0];
			topLeft.coord[1] = g.cache->coord[g.ch][1];
			bottomRight.coord[0] = g.cache->coord[g.ch][2];
			bottomRight.coord[1] = g.cache->coord[g.ch][3];

			g_engine->BindTexture( g.cache->texture[g.cache->page[g.ch]] );
			CofDrawStyledQuad( topLeft, bottomRight, g.color, textMessageStyle, pass, g.tall );
		}

		if( lineWidened )
			g_scissor.setRect( lineClip[0], lineClip[1], lineClip[2], lineClip[3] );
	}

	if( clipMoved )
		g_scissor.setRect( savedClip[0], savedClip[1], savedClip[2], savedClip[3] );

	backingGlyphCount = 0;
	backingLineCount = 0;
}

void XashSurface::popMakeCurrent( Panel *panel )
{
	// Cry of Fear, milestone 4b: draw whatever this panel collected before its
	// translate and scissor are replaced by the parent's
	flushBackingText();

	if( paintStackPos <= 0 )
	{
		vgui_dprintf( "vgui: paint state stack underflow, broken panel tree?\n" );
		return;
	}

	if( paintStack[paintStackPos - 1].panel != panel )
	{
		vgui_dprintf( "vgui: paint state stack popped out of order, broken panel tree?\n" );
		return;
	}

	paintStackPos--;

	if( paintStackPos > 0 )
	{
		makeCurrent( &paintStack[paintStackPos - 1] );
	}
	else
	{
		// nothing draws outside a push/pop pair, but don't leave the last panel's state behind
		int abs[4];

		getPanel()->getAbsExtents( abs[0], abs[1], abs[2], abs[3] );

		translate[0] = translate[1] = 0;
		g_scissor.setRect( abs[0], abs[1], abs[2], abs[3] );
		g_engine->SetPaintOffset( 0, 0 );
	}
}
