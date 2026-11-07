// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#include "font.h"
#include "platform/common/font.h"
#include "fileimage.h"

using namespace vgui;

static int staticFontId = 100;
static vgui::TextSubstituteFn g_textSubstituteHook = nullptr;
static Dar<vgui::BaseFontPlat*> staticFontPlatDar;

// a debugging font, just draws boxes with character number inside
class StubFontPlat : public BaseFontPlat
{
	static constexpr int DIGIT_WIDE = 3;
	static constexpr int DIGIT_TALL = 4;
	static constexpr int MIN_TALL = DIGIT_TALL * 2 + 1 + 2;

public:
	StubFontPlat( const char *newName, int newTall, int newWide, float, int, bool, bool, bool, bool ) :
		tall( newTall ), scale( scaleFor( newTall )), wide( widthFor( newTall, newWide ))
	{
	}

	virtual bool equals( const char *newName, int newTall, int newWide, float, int, bool, bool, bool, bool )
	{
		return tall == newTall && wide == widthFor( newTall, newWide );
	}

	virtual void getCharRGBA( int ch, int rgbaX, int rgbaY, int rgbaWide, int rgbaTall, unsigned char *rgba )
	{
		static const unsigned char digits[16][DIGIT_TALL] =
		{
			{ 7, 5, 5, 7 }, { 2, 6, 2, 7 }, { 7, 1, 6, 7 }, { 7, 3, 1, 7 },
			{ 5, 7, 1, 1 }, { 7, 4, 3, 7 }, { 4, 7, 5, 7 }, { 7, 1, 2, 4 },
			{ 7, 5, 2, 7 }, { 7, 5, 7, 1 }, { 2, 5, 7, 5 }, { 6, 5, 6, 7 },
			{ 7, 4, 4, 7 }, { 6, 5, 5, 6 }, { 7, 4, 6, 7 }, { 7, 6, 4, 4 },
		};

		if( !rgba )
			return;

		// the box, on the cell's outer edge
		for( int x = 0; x < wide; x++ )
		{
			plot( rgba, rgbaWide, rgbaTall, rgbaX + x, rgbaY );
			plot( rgba, rgbaWide, rgbaTall, rgbaX + x, rgbaY + tall - 1 );
		}

		for( int y = 0; y < tall; y++ )
		{
			plot( rgba, rgbaWide, rgbaTall, rgbaX, rgbaY + y );
			plot( rgba, rgbaWide, rgbaTall, rgbaX + wide - 1, rgbaY + y );
		}

		// anything this small only gets the box
		if( tall < MIN_TALL )
			return;

		int digitWide = DIGIT_WIDE * scale;
		int digitTall = DIGIT_TALL * scale; // digit-al heh :)

		// the area the digits are anchored into, just inside the border, inset by
		// a further pixel on whichever axis has the room to spare.
		int left = 1, top = 1, right = wide - 2, bottom = tall - 2;

		if( right - left + 1 >= digitWide * 2 + 3 )
		{
			left++;
			right--;
		}

		if( bottom - top + 1 >= digitTall * 2 + 3 )
		{
			top++;
			bottom--;
		}

		// most significant nibble top left, least significant bottom right
		for( int i = 0; i < 4; i++ )
		{
			const unsigned char *glyph = digits[( ch >> (( 3 - i ) * 4 )) & 0xf];
			int digitX = ( i & 1 ) ? right - digitWide + 1 : left;
			int digitY = ( i >> 1 ) ? bottom - digitTall + 1 : top;

			for( int row = 0; row < DIGIT_TALL; row++ )
			{
				for( int col = 0; col < DIGIT_WIDE; col++ )
				{
					if( !( glyph[row] & ( 1 << ( DIGIT_WIDE - 1 - col ))))
						continue;

					for( int y = 0; y < scale; y++ )
					{
						for( int x = 0; x < scale; x++ )
						{
							plot( rgba, rgbaWide, rgbaTall,
								rgbaX + digitX + col * scale + x,
								rgbaY + digitY + row * scale + y );
						}
					}
				}
			}
		}
	}

	virtual void getCharABCwide( int ch , int &a, int &b, int &c )
	{
		a = c = 0;
		b = wide;
	}

	virtual int getTall()
	{
		return tall;
	}

	virtual int getWide()
	{
		return wide;
	}

	virtual void drawSetTextFont( SurfacePlat* )
	{
	}

private:
	// height controls the scale, nobody asks for fonts by their width here
	static int scaleFor( int tall )
	{
		int scale = ( tall - 2 - 1 ) / ( DIGIT_TALL * 2 );

		return scale > 1 ? scale : 1;
	}

	static int widthFor( int tall, int wide )
	{
		int need = DIGIT_WIDE * 2 * scaleFor( tall ) + 1 + 2 + 2;

		return wide > need ? wide : need;
	}

	void plot( unsigned char *rgba, int rgbaWide, int rgbaTall, int x, int y )
	{
		if( x < 0 || y < 0 || x >= rgbaWide || y >= rgbaTall )
			return;

		memset( &rgba[( y * rgbaWide + x ) * 4], 255, 4 );
	}

	int tall, scale, wide;
};

static vgui::FontSubstituteFn g_fontSubstituteHook = nullptr;
static vgui::FontCreatedFn g_fontCreatedHook = nullptr;
static vgui::FontUtf8Fn g_fontUtf8Hook = nullptr;

void vgui::Font_SetUtf8Hook( FontUtf8Fn fn )
{
	g_fontUtf8Hook = fn;
}

bool vgui::Font_IsUtf8( Font *font )
{
	return font && g_fontUtf8Hook && g_fontUtf8Hook( font );
}

static vgui::TextPrepareFn g_textPrepareHook = nullptr;

void vgui::Text_SetPrepareHook( TextPrepareFn fn )
{
	g_textPrepareHook = fn;
}

void vgui::Text_Prepare( const char *text )
{
	if( text && g_textPrepareHook )
		g_textPrepareHook( text );
}

int vgui::Font_AllocId( void )
{
	return staticFontId++;
}

void vgui::Font_SetSubstituteHook( FontSubstituteFn fn )
{
	g_fontSubstituteHook = fn;
}

void vgui::Font_SetCreatedHook( FontCreatedFn fn )
{
	g_fontCreatedHook = fn;
}

void vgui::Text_SetSubstituteHook( TextSubstituteFn fn )
{
	g_textSubstituteHook = fn;
}

const char *vgui::Text_Substitute( const char *str )
{
	if( !str || !g_textSubstituteHook )
		return str;

	const char *sub = g_textSubstituteHook( str );

	return sub ? sub : str;
}

vgui::Font *vgui::Font_Active( Font *font )
{
	if( !font || !g_fontSubstituteHook )
		return font;

	return g_fontSubstituteHook( font );
}

void vgui::Font_Reset( void )
{
	staticFontPlatDar.setCount( 0 );
}

Font::Font( const char *name, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol )
{
	init( name, nullptr, 0, tall, wide, rotation, weight, italic, underline, strikeout, symbol );
}

Font::Font( const char *name, void *pFileData, int fileDataLen, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol )
{
	init( name, pFileData, fileDataLen, tall, wide, rotation, weight, italic, underline, strikeout, symbol );
}

void Font::init( const char *newName, void *pFileData, int fileDataLen, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol )
{
	name = vgui_strdup( newName );
	id = -1;
	impl = nullptr;

	if( pFileData )
	{
		FileImageStream_Memory stream( pFileData, fileDataLen );
		FontPlat_Bitmap *bitmap = FontPlat_Bitmap::Create( name, &stream );

		if( bitmap )
		{
			impl = bitmap;
			staticFontPlatDar.addElement( impl );
			id = staticFontId++;
		}
	}
	else
	{
		for( int i = 0; i < staticFontPlatDar.getCount(); i++ )
		{
			if( staticFontPlatDar[i]->equals( name, tall, wide, rotation, weight, italic, underline, strikeout, symbol ))
			{
				impl = staticFontPlatDar[i];
				break;
			}
		}

		if( !impl )
		{
			impl = FontPlat_CreateSystem( name, tall, wide, rotation, weight, italic, underline, strikeout, symbol );

			if( !impl )
				impl = new StubFontPlat( name, tall, wide, rotation, weight, italic, underline, strikeout, symbol );

			staticFontPlatDar.addElement( impl );
			id = staticFontId++;
		}
	}

	// Cry of Fear engine-rasterised VGUI fonts: a font built from file data is
	// one of the game's bitmap strips, and the engine has just latched which
	// scheme that strip belonged to
	if( g_fontCreatedHook )
		g_fontCreatedHook( this, pFileData != nullptr );
}

void Font::getCharRGBA( int ch, int rgbaX, int rgbaY, int rgbaWide, int rgbaTall, unsigned char *rgba )
{
	impl->getCharRGBA( ch, rgbaX, rgbaY, rgbaWide, rgbaTall, rgba );
}

void Font::getCharABCwide( int ch, int &a, int &b, int &c )
{
	impl->getCharABCwide( ch, a, b, c );
}

void Font::getTextSize( const char *str, int &wide, int &tall )
{
	wide = 0;
	tall = 0;

	if( !str )
		return;

	// the engine may have substituted a font of its own for this one; every
	// metric below has to come from whichever is really going to be drawn
	Font *active = Font_Active( this );

	tall = active->getTall();

	for( int i = 0, xx = 0;; i++ )
	{
		char ch = str[i];

		if( !ch )
			break;

		if( ch == '\n' )
		{
			tall += active->getTall();
			xx = 0;
		}

		int a, b, c;
		// the cast is load bearing: a plain char sign-extends 0x80..0xFF to a
		// negative int, and a font whose getCharABCwide() indexes a 256-entry
		// table without masking would then read outside it. Harmless only as
		// long as every font masks, which an engine-supplied one need not
		active->getCharABCwide( (unsigned char)ch, a, b, c );
		xx += a + b + c;

		if( xx > wide )
			wide = xx;
	}
}

BaseFontPlat *Font::getPlat()
{
	return impl;
}

int Font::getTall()
{
	return impl->getTall();
}

#ifndef _WIN32
int Font::getWide()
{
	return impl->getWide();
}
#endif // _WIN32

int Font::getId()
{
	return id;
}
