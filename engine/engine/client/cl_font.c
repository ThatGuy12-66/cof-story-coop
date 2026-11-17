/*
cl_font.c - bare bones engine font manager
Copyright (C) 2023 Alibek Omarov

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "filesystem.h"
#include "client.h"
#include "qfont.h"
#include "swaplib.h"
#include "cof_language.h"
#include "cof_glyphs.h"

le_struct_begin( charinfo_swap )
	le_struct_field( charinfo, startoffset )
	le_struct_field( charinfo, charwidth )
le_struct_end();

le_struct_begin( qfont_swap )
	le_struct_field( qfont_t, width )
	le_struct_field( qfont_t, height )
	le_struct_field( qfont_t, rowcount )
	le_struct_field( qfont_t, rowheight )
	le_struct_array_child( qfont_t, fontinfo, charinfo_swap, NUM_GLYPHS )
le_struct_end();

qboolean CL_FixedFont( cl_font_t *font )
{
	return font && font->valid && font->type == FONT_FIXED;
}

static int CL_LoadFontTexture( const char *fontname, uint texFlags, int *width )
{
	if( !g_fsapi.FileExists( fontname, false ))
		return 0;

	int tex = ref.dllFuncs.GL_LoadTexture( fontname, NULL, 0, texFlags );
	if( !tex )
		return 0;

	int font_width = REF_GET_PARM( PARM_TEX_WIDTH, tex );
	if( !font_width )
	{
		ref.dllFuncs.GL_FreeTexture( tex );
		return 0;
	}

	*width = font_width;
	return tex;
}

static int CL_FontRenderMode( convar_t *fontrender )
{
	switch((int)fontrender->value )
	{
	case 0:
		return kRenderTransAdd;
	case 1:
		return kRenderTransAlpha;
	case 2:
		return kRenderTransTexture;
	default:
		Cvar_DirectSet( fontrender, fontrender->def_string );
	}

	return kRenderTransTexture;
}

void CL_SetFontRendermode( cl_font_t *font )
{
	ref.dllFuncs.GL_SetRenderMode( CL_FontRenderMode( font->rendermode ));
}

void CL_SetFontColor( cl_font_t *font, const rgba_t color )
{
	ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
}

qboolean Con_LoadFixedWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	int font_width;

	if( !rendermode )
		return false;

	if( font->valid )
		return true; // already loaded

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_FIXED;
	font->valid = true;
	font->scale = scale;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( font_width / 16 * scale );

	for( int i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		font->fontRc[i].left   = ( i * font_width / 16 ) % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + font_width / 16;
		font->fontRc[i].top    = ( i / 16 ) * ( font_width / 16 );
		font->fontRc[i].bottom = font->fontRc[i].top + font_width / 16;

		font->charWidths[i] = Q_rint( font_width / 16 * scale );
	}

	return true;
}

qboolean Con_LoadVariableWidthFont( const char *fontname, cl_font_t *font, float scale, convar_t *rendermode, uint texFlags )
{
	fs_offset_t length;
	qfont_t src;
	int font_width;

	if( !rendermode )
		return false;

	if( font->valid )
		return true;

	byte *pfile = g_fsapi.LoadFile( fontname, &length, false );
	if( !pfile )
		return false;

	if( length < sizeof( src ))
	{
		Mem_Free( pfile );
		return false;
	}

	memcpy( &src, pfile, sizeof( src ));
	le_struct_swap( qfont_swap, &src );
	Mem_Free( pfile );

	font->hFontTexture = CL_LoadFontTexture( fontname, texFlags, &font_width );
	if( !font->hFontTexture )
		return false;

	font->type = FONT_VARIABLE;
	font->valid = true;
	font->scale = scale ? scale : 1.0f;
	font->rendermode = rendermode;
	font->charHeight = Q_rint( src.rowheight * scale );

	for( int i = 0; i < ARRAYSIZE( font->fontRc ); i++ )
	{
		const charinfo *ci = &src.fontinfo[i];

		font->fontRc[i].left   = (word)ci->startoffset % font_width;
		font->fontRc[i].right  = font->fontRc[i].left + ci->charwidth;
		font->fontRc[i].top    = (word)ci->startoffset / font_width;
		font->fontRc[i].bottom = font->fontRc[i].top + src.rowheight;

		font->charWidths[i] = Q_rint( src.fontinfo[i].charwidth * scale );
	}

	return true;
}

void CL_FreeFont( cl_font_t *font )
{
	if( !font || !font->valid )
		return;

	ref.dllFuncs.GL_FreeTexture( font->hFontTexture );
	memset( font, 0, sizeof( *font ));
}

static int CL_CalcTabStop( const cl_font_t *font, int x )
{
	int space = font->charWidths['0'];
	int tab   = space * 6; // 6 spaces
	int stop  = tab - x % tab;

	if( stop < space )
		return tab * 2 - x % tab; // select next

	return stop;
}

// One atlas cell, as the stock CL_DrawCharacter drew it; returns the atlas's own width for it.
static int CL_DrawAtlasCell( float x, float y, int number, const rgba_t color, cl_font_t *font, int flags )
{
	int advance = font->cofAtlasKept ? font->cofAtlasWidths[number & 255] : font->charWidths[number & 255];

	if( !number || !advance )
		return 0;

	int texw, texh;
	R_GetTextureParms( &texw, &texh, font->hFontTexture );
	if( !texw || !texh )
		return advance;

	wrect_t *rc = &font->fontRc[number];
	float half = 0.5f;

	if( font->scale <= 1.f || !REF_GET_PARM( PARM_TEX_FILTERING, font->hFontTexture ))
		half = 0;

	float s1 = ((float)rc->left + half ) / texw;
	float t1 = ((float)rc->top + half ) / texh;
	float s2 = ((float)rc->right - half ) / texw;
	float t2 = ((float)rc->bottom - half ) / texh;
	float w = ( rc->right - rc->left ) * font->scale;
	float h = ( rc->bottom - rc->top ) * font->scale;

	if( FBitSet( flags, FONT_DRAW_HUD ))
		SPR_AdjustSize( &x, &y, &w, &h );

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	if( !FBitSet( flags, FONT_DRAW_NOCOLOR ))
		CL_SetFontColor( font, color );

	ref.dllFuncs.R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, font->hFontTexture );

	return advance;
}

int CL_DrawCharacter( float x, float y, int number, const rgba_t color, cl_font_t *font, int flags )
{
	if( !font || !font->valid || y < -font->charHeight )
		return 0;

	// check if printable
	if( number <= 32 )
	{
		if( number == ' ' )
			return font->charWidths[' '];
		else if( number == '\t' )
			return CL_CalcTabStop( font, x );
		return 0;
	}

	// Cry of Fear (cof_glyphs.c): a byte of the text code page is a character, drawn from the atlas when the atlas
	// has it and from TrueType when not. A byte of UTF-8 is only part of one: whole characters come through
	// CL_DrawCodepoint, from the decoding string paths and pfnDrawCharacter.
	if( CoF_Glyphs_On( ) && !FBitSet( flags, FONT_DRAW_UTF8 ) && ( number & 255 ) >= 0x80 )
	{
		int codepage = CoF_Glyphs_Codepage();
		uint32_t cp;

		if( codepage == COF_CP_UTF8 )
			return font->charWidths[number & 255];

		if(( cp = CoF_Text_Byte( codepage, number )) == 0 )
			return 0;

		return CL_DrawCodepoint( x, y, cp, color, font, flags, 0 );
	}

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		number = Con_UtfProcessChar( number & 255 );
	else number &= 255;

	if( !number || !font->charWidths[number])
		return 0;

	return CL_DrawAtlasCell( x, y, number, color, font, flags );
}

/*
====================
CL_DrawCodepoint

Cry of Fear (cof_glyphs.c): one character, from the atlas when it has it, else from TrueType. cellw > 0 centres it in
a cell of that width (the client's byte-by-byte layout of UTF-8 text). Returns the advance in the font's pixels.
====================
*/
int CL_DrawCodepoint( float x, float y, uint32_t cp, const rgba_t color, cl_font_t *font, int flags, int cellw )
{
	int cell;

	if( !font || !font->valid || y < -font->charHeight )
		return 0;

	if( cp <= 32 )
		return CL_DrawCharacter( x, y, (int)cp, color, font, flags & ~FONT_DRAW_UTF8 );

	if(( cell = CoF_Glyphs_AtlasCell( font, cp )) != 0 )
	{
		if( cellw > 0 )
			x += ( cellw - CoF_Glyphs_Width( font, cp )) / 2;
		return CL_DrawAtlasCell( x, y, cell, color, font, flags );
	}

	if( !CoF_Glyphs_On( ))
		return 0;

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	if( !FBitSet( flags, FONT_DRAW_NOCOLOR ))
		CL_SetFontColor( font, color );

	return CoF_Glyphs_Draw( x, y, cp, font, flags, cellw );
}

int CL_CodepointWidth( cl_font_t *font, uint32_t cp )
{
	if( !font || !font->valid )
		return 0;

	if( cp == '\t' )
		return CL_CalcTabStop( font, 0 );

	if( cp < 32 )
		return 0;

	return CoF_Glyphs_Width( font, cp );
}

/*
====================
CL_CoF_StringNext

Cry of Fear (cof_glyphs.c): the next character of a string the engine draws. FONT_DRAW_UTF8 text is UTF-8 when the
client accepts UTF-8 (cl_charset utf-8), with a byte that is no part of a sequence read in the text code page; any
other text is in the text code page (cof_text_codepage), and in cp1252 a whole UTF-8 sequence in it still reads as
UTF-8 (a Chinese host's text reaching an English player). Returns the bytes taken, at least 1.
====================
*/
int CL_CoF_StringNext( const char *s, int flags, uint32_t *cp )
{
	const byte *b = (const byte *)s;
	int codepage = CoF_Glyphs_Codepage(), n;

	if( b[0] < 0x80 )
	{
		*cp = b[0];
		return 1;
	}

	if( FBitSet( flags, FONT_DRAW_UTF8 ) && cls.accept_utf8 )
	{
		if(( n = CoF_Text_Utf8Seq( b, 4, cp )) > 0 )
			return n;

		*cp = CoF_Text_Byte( codepage == COF_CP_UTF8 ? 1252 : codepage, b[0] );
		return 1;
	}

	n = CoF_Text_Next( b, 4, codepage, true, cp );
	return n > 0 ? n : 1;
}

int CL_DrawString( float x, float y, const char *s, const rgba_t color, cl_font_t *font, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return 0;

	// Cry of Fear language packs: the captions compiled into client.dll and
	// hl.dll have no file slot of any kind, so they are substituted here,
	// at the last point before they become glyphs - but only for text the
	// client DLL hands in (FONT_DRAW_COF_LANG, set by the engfuncs), never
	// for the engine's own console and overlays. Returns s unchanged when
	// no pack is active or nothing in its table matches. The width query
	// below does the same, or the measured width would no longer match the
	// drawn text.
	if( FBitSet( flags, FONT_DRAW_COF_LANG ))
		s = CoF_Lang_StringEngineFont( s, FBitSet( flags, FONT_DRAW_UTF8 ) && cls.accept_utf8 );

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // clear utf state

	if( !FBitSet( flags, FONT_DRAW_NORENDERMODE ))
		CL_SetFontRendermode( font );

	CL_SetFontColor( font, color );

	SetBits( flags, FONT_DRAW_NOCOLOR | FONT_DRAW_NORENDERMODE );

	while( *s )
	{
		if( *s == '\n' )
		{
			s++;

			if( !*s )
				break;

			// some client functions ignore newlines
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				draw_len = 0;
				y += font->charHeight;
			}

			if( FBitSet( flags, FONT_DRAW_RESETCOLORONLF ))
				 CL_SetFontColor( font, color );
			continue;
		}

		if( IsColorString( s ))
		{
			// don't copy alpha
			if( !FBitSet( flags, FONT_DRAW_FORCECOL ))
				CL_SetFontColor( font, g_color_table[ColorIndex(*( s + 1 ))] );

			s += 2;
			continue;
		}

		// Cry of Fear (cof_glyphs.c): whole characters
		if( CoF_Glyphs_On( ))
		{
			uint32_t cp;
			int n = CL_CoF_StringNext( s, flags, &cp );

			if( cp )
				draw_len += CL_DrawCodepoint( x + draw_len, y, cp, NULL, font, flags, 0 );
			s += n;
			continue;
		}

		// skip setting rendermode, it was changed for this string already
		draw_len += CL_DrawCharacter( x + draw_len, y, (byte)*s, NULL, font, flags );

		s++;
	}

	return draw_len;
}

int CL_DrawStringf( cl_font_t *font, float x, float y, const rgba_t color, int flags, const char *fmt, ... )
{
	va_list va;
	char buf[MAX_VA_STRING];

	va_start( va, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, va );
	va_end( va );

	return CL_DrawString( x, y, buf, color, font, flags );
}

void CL_DrawCharacterLen( cl_font_t *font, int number, int *width, int *height )
{
	if( !font || !font->valid ) return;
	if( width )
	{
		if( number == '\t' )
			*width = CL_CalcTabStop( font, 0 ); // at least return max tabstop
		else if( number > 0xFF && CoF_Glyphs_On( ))
			*width = CL_CodepointWidth( font, (uint32_t)number ); // Cry of Fear: a code point (cof_glyphs.c)
		else *width = font->charWidths[number & 255];
	}
	if( height ) *height = font->charHeight;
}

void CL_DrawStringLen( cl_font_t *font, const char *s, int *width, int *height, int flags )
{
	int draw_len = 0;

	if( !font || !font->valid )
		return;

	if( height )
		*height = font->charHeight;

	if( width )
		*width = 0;

	if( COM_StringEmptyOrNULL( s ))
		return;

	// see CL_DrawString
	if( FBitSet( flags, FONT_DRAW_COF_LANG ))
		s = CoF_Lang_StringEngineFont( s, FBitSet( flags, FONT_DRAW_UTF8 ) && cls.accept_utf8 );

	if( FBitSet( flags, FONT_DRAW_UTF8 ))
		Con_UtfProcessChar( 0 ); // reset utf state

	while( *s )
	{
		int number;

		if( *s == '\n' )
		{
			// BUG: no check for end string here
			// but high chances somebody's relying on this
			s++;
			draw_len = 0;
			if( !FBitSet( flags, FONT_DRAW_NOLF ))
			{
				if( height )
					*height += font->charHeight;
			}
			continue;
		}
		else if( *s == '\t' )
		{
			draw_len += CL_CalcTabStop( font, 0 ); // at least return max tabstop
			s++;
			continue;
		}

		if( IsColorString( s ))
		{
			s += 2;
			continue;
		}

		// Cry of Fear (cof_glyphs.c): whole characters, measured as CL_DrawString draws them
		if( CoF_Glyphs_On( ))
		{
			uint32_t cp;
			int n = CL_CoF_StringNext( s, flags, &cp );

			draw_len += CL_CodepointWidth( font, cp );
			if( width && draw_len > *width )
				*width = draw_len;
			s += n;
			continue;
		}

		if( FBitSet( flags, FONT_DRAW_UTF8 ))
			number = Con_UtfProcessChar( (byte)*s );
		else number = (byte)*s;

		if( number )
		{
			draw_len += font->charWidths[number];

			if( width )
			{
				if( draw_len > *width )
					*width = draw_len;
			}
		}

		s++;
	}
}
