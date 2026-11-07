/*
cof_glyphs.c - Cry of Fear: the engine fonts' characters beyond their cp1252 atlas
GPL-3.0-or-later.

The engine draws three kinds of text with bitmap atlases: the console and chat (fonts/cof_console*.fnt), the client's
HUD text through pfnDrawCharacter and the centre print (fonts/cof_hudtext*.fnt), each 256 cells of cp1252. A language
pack in another code page used to come out folded to ASCII there (Polish without its letters) or as wrong glyphs
(Cyrillic), and a UTF-8 pack (Simplified Chinese) not at all.

Here every character the atlas lacks is drawn from TrueType instead: Inter (the face the atlases were made from) for
Latin, Greek and Cyrillic, the bundled CJK face (gfx/fonts/CoFSansSC-*.otf, a subset of Noto Sans SC, SIL OFL 1.1)
for Chinese. Glyphs are rasterised at the size they are drawn at into 1024x1024 pages, the active pack's whole
character set at once the first time a face is used at a size (its strings/glyphs.txt), anything else when it first
appears; a page is uploaded again only when a glyph it gained is about to be drawn.

Sizing: the cell is the atlas's line height. The Latin face is scaled so its ascent-to-descent fills it, as the atlas
generator did; the CJK face gets the same em, so a Chinese character is as tall as the Latin text it sits in.

cof_text_glyphs 0 turns all of it off: one atlas cell per byte, the stock behaviour.
*/
#include "common.h"
#include "client.h"
#include "cof_glyphs.h"
#include "cof_language.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../3rdparty/freevgui/platform/xash3d-fwgs/stb_truetype.h"

static CVAR_DEFINE_AUTO( cof_text_glyphs, "1", FCVAR_ARCHIVE, "Cry of Fear: the engine fonts draw every character of the language pack's code page, the ones their cp1252 atlas lacks from TrueType (Polish, Cyrillic, Chinese); 0 is one atlas cell per byte" );

#define COF_GLYPH_PAGE		1024
#define COF_GLYPH_PAGES		12
#define COF_GLYPH_MAX		24576
#define COF_GLYPH_HASH		8192

enum
{
	COF_GFACE_LATIN = 0,	// + COF_FACE_*: Inter Regular, SemiBold, Bold
	COF_GFACE_CJK = 3,	// + 0 Regular, + 1 Bold
	COF_GFACE_COUNT = 5
};

typedef struct cof_glyph_s
{
	uint32_t	cp;
	short		px;		// the cell height it was rasterised for, device pixels
	byte		face;
	byte		page;
	short		x, y, w, h;	// in the page
	short		left, top;	// from the pen, and from the top of the cell
	short		advance;	// device pixels
	struct cof_glyph_s	*next;
} cof_glyph_t;

typedef struct
{
	int		tex;
	byte		*rgba;
	int		cx, cy, shelf;	// packing cursor and the current shelf's height
	qboolean	dirty;
} cof_glyph_page_t;

typedef struct
{
	qboolean	tried;
	qboolean	ok;
	stbtt_fontinfo	info;
	int		ascent, descent, upm;
} cof_glyph_face_t;

static struct
{
	cof_glyph_face_t	face[COF_GFACE_COUNT];
	byte		*cjk[2];
	int		cjk_size[2];
	qboolean	cjk_tried[2];

	cof_glyph_t	*hash[COF_GLYPH_HASH];
	cof_glyph_t	*glyphs;
	int		count;
	cof_glyph_page_t	page[COF_GLYPH_PAGES];
	int		pages;
	qboolean	reported_full;

	// (style, px) pairs the active pack's characters were already rasterised for (CoF_Glyphs_Warm)
	int		warmed_face[64], warmed_px[64], warmed;
	int		warmed_generation;
} cof_glyphs;

qboolean CoF_Glyphs_On( void )
{
	return cof_text_glyphs.value != 0.0f;
}

int CoF_Glyphs_Codepage( void )
{
	return CoF_Text_Codepage((int)cof_text_codepage.value );
}

qboolean CoF_Glyphs_Utf8Hud( void )
{
	return CoF_Glyphs_On() && CoF_Glyphs_Codepage() == COF_CP_UTF8;
}

const byte *CoF_Glyphs_CJKFile( int bold, int *size )
{
	int slot = bold ? 1 : 0;

	if( size )
		*size = 0;

	if( !cof_glyphs.cjk_tried[slot] )
	{
		fs_offset_t length = 0;
		const char *path = slot ? "gfx/fonts/CoFSansSC-Bold.otf" : "gfx/fonts/CoFSansSC-Regular.otf";

		cof_glyphs.cjk_tried[slot] = true;
		cof_glyphs.cjk[slot] = FS_LoadFile( path, &length, false );
		cof_glyphs.cjk_size[slot] = (int)length;

		if( !cof_glyphs.cjk[slot] )
			Con_Reportf( "[cof-glyphs] %s is not installed: no Chinese characters\n", path );
	}

	if( !cof_glyphs.cjk[slot] )
		return slot ? CoF_Glyphs_CJKFile( 0, size ) : NULL;

	if( size )
		*size = cof_glyphs.cjk_size[slot];

	return cof_glyphs.cjk[slot];
}

static cof_glyph_face_t *CoF_Glyphs_Face( int index )
{
	cof_glyph_face_t *f = &cof_glyphs.face[index];
	const byte *data;
	int size = 0, lineGap;

	if( f->tried )
		return f->ok ? f : NULL;

	f->tried = true;

	if( index >= COF_GFACE_CJK )
		data = CoF_Glyphs_CJKFile( index - COF_GFACE_CJK, &size );
	else data = CL_CoF_FontFile( index - COF_GFACE_LATIN, &size );

	if( !data || size <= 0 || !stbtt_InitFont( &f->info, data, stbtt_GetFontOffsetForIndex( data, 0 )))
		return NULL;

	stbtt_GetFontVMetrics( &f->info, &f->ascent, &f->descent, &lineGap );
	f->upm = (int)( 1.0f / stbtt_ScaleForMappingEmToPixels( &f->info, 1.0f ) + 0.5f );
	if( f->ascent - f->descent <= 0 || f->upm <= 0 )
		return NULL;

	f->ok = true;
	return f;
}

// The Latin face of a font (its atlas was made from it) and the scale that makes its line fill px.
static cof_glyph_face_t *CoF_Glyphs_Latin( int style, int px, float *scale, int *baseline )
{
	cof_glyph_face_t *f = CoF_Glyphs_Face( COF_GFACE_LATIN + bound( 0, style, 2 ));

	if( !f )
		f = CoF_Glyphs_Face( COF_GFACE_LATIN );
	if( !f )
		return NULL;

	*scale = (float)px / (float)( f->ascent - f->descent );
	*baseline = Q_rint( f->ascent * *scale );
	return f;
}

// Which face draws cp for a font of this style at px, and the scale: the CJK face at the Latin face's em when the
// character is Chinese (or the Latin face lacks it), else the Latin face.
static int CoF_Glyphs_Pick( uint32_t cp, int style, int px, float *scale, int *baseline, int *glyph )
{
	float latinScale;
	cof_glyph_face_t *latin = CoF_Glyphs_Latin( style, px, &latinScale, baseline ), *cjk;
	int index = latin ? stbtt_FindGlyphIndex( &latin->info, (int)cp ) : 0;
	int utf8 = CoF_Glyphs_Codepage() == COF_CP_UTF8;

	if( !latin )
		return -1;

	if( index > 0 && !CoF_Text_PreferCJK( cp, utf8 ))
	{
		*scale = latinScale;
		*glyph = index;
		return COF_GFACE_LATIN + bound( 0, style, 2 );
	}

	cjk = CoF_Glyphs_Face( COF_GFACE_CJK + ( style > 0 ? 1 : 0 ));
	if( cjk )
	{
		int c = stbtt_FindGlyphIndex( &cjk->info, (int)cp );

		if( c > 0 )
		{
			*scale = latinScale * (float)latin->upm / (float)cjk->upm;
			*glyph = c;
			return COF_GFACE_CJK + ( style > 0 ? 1 : 0 );
		}
	}

	if( index > 0 )
	{
		*scale = latinScale;
		*glyph = index;
		return COF_GFACE_LATIN + bound( 0, style, 2 );
	}

	return -1;
}

static void CoF_Glyphs_Reset( void )
{
	int i;

	for( i = 0; i < cof_glyphs.pages; i++ )
	{
		cof_glyph_page_t *p = &cof_glyphs.page[i];

		if( p->tex && ref.initialized )
			ref.dllFuncs.GL_FreeTexture( p->tex );
		if( p->rgba )
			Mem_Free( p->rgba );
		memset( p, 0, sizeof( *p ));
	}

	cof_glyphs.pages = 0;
	cof_glyphs.count = 0;
	cof_glyphs.warmed = 0;
	memset( cof_glyphs.hash, 0, sizeof( cof_glyphs.hash ));
}

void CoF_Glyphs_Shutdown( void )
{
	CoF_Glyphs_Reset();
}

static uint CoF_Glyphs_Key( uint32_t cp, int face, int px )
{
	return ( cp * 2654435761u ^ (uint)face * 40503u ^ (uint)px * 9176u ) & ( COF_GLYPH_HASH - 1 );
}

// A place for a w x h bitmap: on the current page, or a new one. -1 when every page is full.
static int CoF_Glyphs_Place( int w, int h, int *x, int *y )
{
	cof_glyph_page_t *p;

	if( w > COF_GLYPH_PAGE - 2 || h > COF_GLYPH_PAGE - 2 )
		return -1;

	if( cof_glyphs.pages > 0 )
	{
		p = &cof_glyphs.page[cof_glyphs.pages - 1];

		if( p->cx + w + 1 > COF_GLYPH_PAGE )
		{
			p->cx = 1;
			p->cy += p->shelf + 1;
			p->shelf = 0;
		}

		if( p->cy + h + 1 <= COF_GLYPH_PAGE )
		{
			*x = p->cx;
			*y = p->cy;
			p->cx += w + 1;
			p->shelf = Q_max( p->shelf, h );
			return cof_glyphs.pages - 1;
		}
	}

	if( cof_glyphs.pages >= COF_GLYPH_PAGES )
		return -1;

	p = &cof_glyphs.page[cof_glyphs.pages++];
	memset( p, 0, sizeof( *p ));
	p->rgba = Mem_Calloc( host.mempool, COF_GLYPH_PAGE * COF_GLYPH_PAGE * 4 );
	p->cx = 1 + w + 1;
	p->cy = 1;
	p->shelf = h;
	*x = 1;
	*y = 1;
	return cof_glyphs.pages - 1;
}

static cof_glyph_t *CoF_Glyphs_Find( uint32_t cp, int face, int px )
{
	cof_glyph_t *g;

	for( g = cof_glyphs.hash[CoF_Glyphs_Key( cp, face, px )]; g; g = g->next )
	{
		if( g->cp == cp && g->face == face && g->px == px )
			return g;
	}

	return NULL;
}

// Rasterise one character for this style and cell height; NULL when no face has it or the cache is full.
static cof_glyph_t *CoF_Glyphs_Make( uint32_t cp, int style, int px )
{
	float scale;
	int baseline, index, face, x0, y0, x1, y1, advance, lsb, page, px_, py_, w, h, x, y;
	cof_glyph_t *g;
	byte *bitmap;
	cof_glyph_page_t *p;

	face = CoF_Glyphs_Pick( cp, style, px, &scale, &baseline, &index );
	if( face < 0 )
		return NULL;

	if(( g = CoF_Glyphs_Find( cp, face, px )) != NULL )
		return g;

	if( !cof_glyphs.glyphs )
		cof_glyphs.glyphs = Mem_Calloc( host.mempool, sizeof( cof_glyph_t ) * COF_GLYPH_MAX );

	if( cof_glyphs.count >= COF_GLYPH_MAX )
	{
		// a session that met more characters at more sizes than fit: start over, the next frames rasterise again
		if( !cof_glyphs.reported_full )
		{
			cof_glyphs.reported_full = true;
			Con_Reportf( "[cof-glyphs] cache full (%i glyphs, %i pages): starting over\n", cof_glyphs.count, cof_glyphs.pages );
		}
		CoF_Glyphs_Reset();
	}

	stbtt_GetGlyphHMetrics( &cof_glyphs.face[face].info, index, &advance, &lsb );
	stbtt_GetGlyphBitmapBox( &cof_glyphs.face[face].info, index, scale, scale, &x0, &y0, &x1, &y1 );
	w = x1 - x0;
	h = y1 - y0;

	px_ = py_ = 0;
	page = -1;

	if( w > 0 && h > 0 )
	{
		page = CoF_Glyphs_Place( w, h, &px_, &py_ );

		if( page < 0 )
		{
			CoF_Glyphs_Reset();
			page = CoF_Glyphs_Place( w, h, &px_, &py_ );
			if( page < 0 )
				return NULL; // a glyph larger than a page
		}
	}

	g = &cof_glyphs.glyphs[cof_glyphs.count++];
	g->cp = cp;
	g->face = (byte)face;
	g->px = (short)px;
	g->page = (byte)( page < 0 ? 0 : page );
	g->x = (short)px_;
	g->y = (short)py_;
	g->w = (short)( page < 0 ? 0 : w );
	g->h = (short)( page < 0 ? 0 : h );
	g->left = (short)x0;
	g->top = (short)( baseline + y0 );
	g->advance = (short)Q_rint( advance * scale );

	{
		uint key = CoF_Glyphs_Key( cp, face, px );

		g->next = cof_glyphs.hash[key];
		cof_glyphs.hash[key] = g;
	}

	if( page < 0 )
		return g; // a space or a mark with no ink

	bitmap = Mem_Calloc( host.mempool, w * h );
	stbtt_MakeGlyphBitmap( &cof_glyphs.face[face].info, bitmap, w, h, w, scale, scale, index );

	p = &cof_glyphs.page[page];

	for( y = 0; y < h; y++ )
	{
		byte *row = p->rgba + (( py_ + y ) * COF_GLYPH_PAGE + px_ ) * 4;

		for( x = 0; x < w; x++ )
			memset( row + x * 4, bitmap[y * w + x], 4 ); // white with coverage, as the atlases and VGUI's pages
	}

	Mem_Free( bitmap );
	p->dirty = true;

	return g;
}

// The first time a style is drawn at a size: the active pack's whole character set, so its pages are uploaded once.
static void CoF_Glyphs_Warm( int style, int px )
{
	const uint32_t *list = NULL;
	int i, count, generation = 0;

	count = CoF_Lang_Glyphs( &list, &generation );

	if( generation != cof_glyphs.warmed_generation )
	{
		cof_glyphs.warmed_generation = generation; // another pack: every size again
		cof_glyphs.warmed = 0;
	}

	if( count <= 0 || !list )
		return;

	for( i = 0; i < cof_glyphs.warmed; i++ )
	{
		if( cof_glyphs.warmed_face[i] == style && cof_glyphs.warmed_px[i] == px )
			return;
	}

	if( cof_glyphs.warmed >= (int)ARRAYSIZE( cof_glyphs.warmed_face ))
		return;

	cof_glyphs.warmed_face[cof_glyphs.warmed] = style;
	cof_glyphs.warmed_px[cof_glyphs.warmed] = px;
	cof_glyphs.warmed++;

	for( i = 0; i < count; i++ )
		CoF_Glyphs_Make( list[i], style, px );

	Con_Reportf( "[cof-glyphs] prepared %i characters, style %i, %i px (%i glyphs on %i pages)\n",
		count, style, px, cof_glyphs.count, cof_glyphs.pages );
}

static void CoF_Glyphs_Upload( cof_glyph_page_t *p, int index )
{
	rgbdata_t pic;
	char name[32];

	if( !p->dirty && p->tex )
		return;

	memset( &pic, 0, sizeof( pic ));
	pic.width = COF_GLYPH_PAGE;
	pic.height = COF_GLYPH_PAGE;
	pic.type = PF_RGBA_32;
	pic.size = COF_GLYPH_PAGE * COF_GLYPH_PAGE * 4;
	pic.flags = IMAGE_HAS_COLOR|IMAGE_HAS_ALPHA;
	pic.buffer = p->rgba;

	Q_snprintf( name, sizeof( name ), "*cof_glyphs%i", index );
	p->tex = ref.dllFuncs.GL_LoadTextureFromBuffer( name, &pic, TF_IMAGE, p->tex != 0 );
	p->dirty = false;
}

static int CoF_Glyphs_AtlasWidth( const cl_font_t *font, int cell )
{
	return font->cofAtlasKept ? font->cofAtlasWidths[cell & 255] : font->charWidths[cell & 255];
}

// The atlas cell for a character: cp1252 (the stock console fonts in cp1251 are that page instead), 0 when the atlas
// lacks it.
int CoF_Glyphs_AtlasCell( const cl_font_t *font, uint32_t cp )
{
	int cell = 0, i;

	if( cp < 0x80 )
		return (int)cp;

	if( font->cofAtlasPage == 1251 )
	{
		for( i = 0; i < 128; i++ )
		{
			if( cof_text_cp1251[i] == cp )
			{
				cell = 0x80 + i;
				break;
			}
		}
	}
	else cell = CoF_Text_To1252( cp );

	// a cell the atlas left empty is no glyph (cp1252 has holes, and the generator skips characters Inter lacks)
	if( cell && !CoF_Glyphs_AtlasWidth( font, cell ))
		return 0;

	return cell;
}

// The advance of a character drawn from TrueType at a cell height, in pixels of that height; no rasterising.
static int CoF_Glyphs_Advance( uint32_t cp, int style, int px )
{
	float scale;
	int baseline, index, face, advance, lsb;

	if(( face = CoF_Glyphs_Pick( cp, style, px, &scale, &baseline, &index )) < 0 )
		return 0;

	stbtt_GetGlyphHMetrics( &cof_glyphs.face[face].info, index, &advance, &lsb );
	return Q_rint( advance * scale );
}

int CoF_Glyphs_Width( cl_font_t *font, uint32_t cp )
{
	int cell;

	if( !font || !font->valid )
		return 0;

	if( cp < 0x80 )
		return font->charWidths[cp];

	if(( cell = CoF_Glyphs_AtlasCell( font, cp )) != 0 )
		return CoF_Glyphs_AtlasWidth( font, cell );

	if( !CoF_Glyphs_On( ))
		return 0;

	return CoF_Glyphs_Advance( cp, font->cofFace, Q_max( 4, font->charHeight ));
}

int CoF_Glyphs_Draw( float x, float y, uint32_t cp, cl_font_t *font, int flags, int cellw )
{
	float w, h, scale, dx;
	int px, style, advance;
	cof_glyph_t *g;
	cof_glyph_page_t *p;

	if( !font || !font->valid || !ref.initialized )
		return 0;

	style = font->cofFace;
	advance = CoF_Glyphs_Width( font, cp ); // in the font's pixels, what the caller advances by

	// the device size the quad lands at: the HUD path scales the font's pixels to the screen
	w = (float)advance;
	h = (float)font->charHeight;
	if( FBitSet( flags, FONT_DRAW_HUD ))
		SPR_AdjustSize( &x, &y, &w, &h );

	px = Q_max( 4, Q_rint( h ));
	scale = h / (float)Q_max( 1, font->charHeight ); // font pixels -> device pixels

	CoF_Glyphs_Warm( style, px );

	if(( g = CoF_Glyphs_Make( cp, style, px )) == NULL || g->w <= 0 || g->h <= 0 )
		return advance;

	p = &cof_glyphs.page[g->page];
	CoF_Glyphs_Upload( p, g->page );
	if( !p->tex )
		return advance;

	dx = (float)g->left;
	if( cellw > 0 )
		dx += ( cellw * scale - g->advance ) * 0.5f; // centred in the client's cell

	ref.dllFuncs.R_DrawStretchPic( Q_rint( x + dx ), Q_rint( y + g->top ), g->w, g->h,
		(float)g->x / COF_GLYPH_PAGE, (float)g->y / COF_GLYPH_PAGE,
		(float)( g->x + g->w ) / COF_GLYPH_PAGE, (float)( g->y + g->h ) / COF_GLYPH_PAGE, p->tex );

	return advance;
}

/*
====================
CoF_Glyphs_FontWidths

What the client lays its HUD text out with: the bytes 0x80..0xFF of the width table. Their atlas widths are kept
(cofAtlasWidths) and the table gets the text code page's: a single-byte page's characters at their widths (the
atlas's where it has the character, TrueType's where not); UTF-8's per-byte cells. Off, or in cp1252, the atlas's own.
====================
*/
void CoF_Glyphs_FontWidths( cl_font_t *font )
{
	int b, codepage, full;

	if( !font || !font->valid )
		return;

	// the first call after a load: the table still is the atlas's
	if( !font->cofAtlasKept )
	{
		memcpy( font->cofAtlasWidths, font->charWidths, sizeof( font->cofAtlasWidths ));
		font->cofAtlasKept = true;
	}

	memcpy( font->charWidths + 0x80, font->cofAtlasWidths + 0x80, 0x80 );

	codepage = CoF_Glyphs_Codepage();
	font->cofCodepage = codepage;

	if( !CoF_Glyphs_On( ) || ( codepage == 1252 && font->cofAtlasPage != 1251 ))
		return;

	if( codepage == COF_CP_UTF8 )
	{
		full = CoF_Glyphs_Width( font, 0x4E00 ); // 一: the width of every Chinese character
		if( full <= 0 )
			full = font->charHeight;

		for( b = 0x80; b <= 0xFF; b++ )
		{
			switch( CoF_Text_Utf8ByteCell( b ))
			{
			case 2: font->charWidths[b] = (byte)bound( 1, full, 255 ); break;
			case 1: font->charWidths[b] = (byte)bound( 1, ( full + 1 ) / 2, 255 ); break;
			default: font->charWidths[b] = 0; break;
			}
		}
		return;
	}

	for( b = 0x80; b <= 0xFF; b++ )
	{
		uint32_t cp = CoF_Text_Byte( codepage, b );

		font->charWidths[b] = cp ? (byte)bound( 0, CoF_Glyphs_Width( font, cp ), 255 ) : 0;
	}
}

void CoF_Glyphs_CodepageChanged( void )
{
	// the HUD font and the client's copy of its widths; the console fonts are reloaded by Con_CheckTextScale
	if( cls.creditsFont.valid )
	{
		CoF_Glyphs_FontWidths( &cls.creditsFont );

		clgame.scrInfo.iCharHeight = cls.creditsFont.charHeight;
		memcpy( clgame.scrInfo.charWidths, cls.creditsFont.charWidths, sizeof( clgame.scrInfo.charWidths ));
	}

	Con_CoF_FontsCodepageChanged();
}

void CoF_Glyphs_Init( void )
{
	Cvar_RegisterVariable( &cof_text_glyphs );
}
