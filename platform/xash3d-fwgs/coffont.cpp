// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear engine-rasterised VGUI fonts (cof_ui_inter_fonts, milestone 4)

#include "vgui_internal.h"
#include "support.h"
#include "font.h"
#include "platform/common/font.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

using namespace vgui;

/*
The game's VGUI text comes from bitmap strips at
gfx/vgui/fonts/<res>_<scheme>.tga whose resolution table caps at 1600 and whose
contents are byte-identical from the 1024 bucket upwards - the `inventory`
scheme never scaled at all. So above 1024 px wide every label in the inventory,
the phone and the notes has one fixed pixel size whatever the display is.

This file replaces them with glyphs rasterised from the Inter faces the project
already ships, at a size the engine derives from the render height
(CL_CoF_FontDesc in engine/client/cl_main.c). Three things make it work:

* THE ROLE. The client builds every one of its fonts as
  Font( "Arial", <tga bytes>, tall = 20, weight = 700 ), so nothing about the
  object says whether it is the inventory font or the chapter title. What does
  is the strip it loaded a moment earlier: the engine latches the scheme name
  out of that COM_LoadFile path and the Font constructor hook below consumes
  it.

* THE DEVICE/LOGICAL SPLIT. cof_ui_scale multiplies every VGUI quad on the way
  out, so a font that reported its real pixel size would be magnified twice.
  Glyphs are rasterised at `devicePx` and every metric is reported at
  `logicalPx = devicePx / scale`, so the quad the surface emits lands on
  exactly the device pixels the glyph was drawn for. That is the difference
  between crisp text and the magnified bitmap this replaces.

* THE CODE PAGE. Text reaches drawPrintText one byte at a time and the atlas is
  256 cells, which is the whole 8-bit pipeline the game and the language packs
  use. Rather than widen that, each byte is decoded to Unicode here, with the
  pack's code page, immediately before the glyph lookup. One pack is one code
  page; cp1250 carries Polish, cp1251 Cyrillic, cp1252 the Western default.
*/
// Windows-1250, the 0x80..0xFF half (0x00..0x7F is ASCII)
static const unsigned short g_cp1250[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0000, 0x201E, 0x2026, 0x2020, 0x2021,
	0x0000, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
	0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
	0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
	0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
	0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
	0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
	0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
	0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
	0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
	0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
	0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

// Windows-1251, the 0x80..0xFF half (0x00..0x7F is ASCII)
static const unsigned short g_cp1251[128] =
{
	0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
	0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
	0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
	0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
	0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
	0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
	0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

// Windows-1252, the 0x80..0xFF half (0x00..0x7F is ASCII)
static const unsigned short g_cp1252[128] =
{
	0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
	0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
	0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
	0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
	0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
	0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
	0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
	0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
	0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
	0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
	0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
	0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
	0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
	0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};


#include "coffont.h"

namespace vgui
{

static int CofDecode( int codepage, int ch )
{
	const unsigned short *table;

	ch &= 0xFF;

	if( ch < 0x80 )
		return ch; // every one of the three code pages is ASCII down here

	switch( codepage )
	{
	case 1250: table = g_cp1250; break;
	case 1251: table = g_cp1251; break;
	default:   table = g_cp1252; break;
	}

	return table[ch - 0x80];
}

// One face at one size. Reports DEVICE metrics through BaseFontPlat, because
// that is what the atlas is packed and rasterised in; CofFont converts to
// logical for everything the client and the draw path see.
class FontPlat_Inter : public BaseFontPlat
{
public:
	static FontPlat_Inter *Create( const unsigned char *ttf, int ttfSize, int devicePx, int logicalPx, int codepage )
	{
		FontPlat_Inter *self = new FontPlat_Inter;

		if( !stbtt_InitFont( &self->info, ttf, stbtt_GetFontOffsetForIndex( ttf, 0 )))
		{
			delete self;
			return nullptr;
		}

		self->devicePx = Q_max( 4, devicePx );
		self->logicalPx = Q_max( 4, logicalPx );
		self->codepage = codepage;
		self->scaleDevice = stbtt_ScaleForPixelHeight( &self->info, (float)self->devicePx );
		self->scaleLogical = stbtt_ScaleForPixelHeight( &self->info, (float)self->logicalPx );

		int ascent, descent, lineGap;

		stbtt_GetFontVMetrics( &self->info, &ascent, &descent, &lineGap );
		self->baselineDevice = (int)( ascent * self->scaleDevice + 0.5f );

		for( int i = 0; i < 256; i++ )
			self->glyph[i].resolved = false;

		return self;
	}

	virtual ~FontPlat_Inter() override { }

	virtual bool equals( const char *, int, int, float, int, bool, bool, bool, bool ) override
	{
		return false; // matched by role and size in the cache below, never by these
	}

	virtual void getCharRGBA( int ch, int rgbaX, int rgbaY, int rgbaWide, int rgbaTall, unsigned char *rgba ) override
	{
		const Glyph &g = resolve( ch );

		if( !rgba || g.index <= 0 || g.deviceW <= 0 || g.deviceH <= 0 )
			return;

		unsigned char *bitmap = (unsigned char *)malloc( (size_t)g.deviceW * g.deviceH );

		if( !bitmap )
			return;

		memset( bitmap, 0, (size_t)g.deviceW * g.deviceH );
		stbtt_MakeGlyphBitmap( &info, bitmap, g.deviceW, g.deviceH, g.deviceW,
			scaleDevice, scaleDevice, g.index );

		for( int y = 0; y < g.deviceH; y++ )
		{
			int outY = rgbaY + g.deviceTop + y;

			if( outY < 0 || outY >= rgbaTall )
				continue;

			for( int x = 0; x < g.deviceW; x++ )
			{
				int outX = rgbaX + x;

				if( outX < 0 || outX >= rgbaWide )
					continue;

				// the same layout the bitmap sheets produce: white, alpha is
				// the coverage, so the draw path's colour tint works unchanged
				memset( &rgba[( outY * rgbaWide + outX ) * 4], bitmap[y * g.deviceW + x], 4 );
			}
		}

		free( bitmap );
	}

	virtual void getCharABCwide( int ch, int &a, int &b, int &c ) override
	{
		const Glyph &g = resolve( ch );

		a = g.deviceA;
		b = g.deviceW;
		c = g.deviceC;
	}

	virtual int getTall() override { return devicePx; }
	virtual int getWide() override { return devicePx / 2; }
	virtual void drawSetTextFont( SurfacePlat * ) override { }

	void logicalABCwide( int ch, int &a, int &b, int &c )
	{
		const Glyph &g = resolve( ch );

		a = g.logicalA;
		b = g.logicalW;
		c = g.logicalC;
	}

	int logicalTall() const { return logicalPx; }
	int deviceTall() const { return devicePx; }

private:
	struct Glyph
	{
		bool	resolved;
		int	index;
		int	deviceA, deviceW, deviceC, deviceH, deviceTop;
		int	logicalA, logicalW, logicalC;
	};

	FontPlat_Inter() : devicePx( 0 ), logicalPx( 0 ), codepage( 1252 ),
		scaleDevice( 0.0f ), scaleLogical( 0.0f ), baselineDevice( 0 ) { }

	const Glyph &resolve( int ch )
	{
		ch &= 0xFF;

		Glyph &g = glyph[ch];

		if( g.resolved )
			return g;

		g.resolved = true;
		g.index = stbtt_FindGlyphIndex( &info, CofDecode( codepage, ch ));

		if( g.index <= 0 )
		{
			// nothing in the face for this byte: no ink, no advance. The atlas
			// builder skips it and the draw path steps over it
			g.deviceA = g.deviceW = g.deviceC = g.deviceH = g.deviceTop = 0;
			g.logicalA = g.logicalW = g.logicalC = 0;
			return g;
		}

		measure( g.index, scaleDevice, baselineDevice, g.deviceA, g.deviceW, g.deviceC, &g.deviceH, &g.deviceTop );
		measure( g.index, scaleLogical, 0, g.logicalA, g.logicalW, g.logicalC, nullptr, nullptr );

		return g;
	}

	void measure( int index, float scale, int baseline, int &a, int &w, int &c, int *h, int *top )
	{
		int advance, lsb, x0, y0, x1, y1;

		stbtt_GetGlyphHMetrics( &info, index, &advance, &lsb );
		stbtt_GetGlyphBitmapBox( &info, index, scale, scale, &x0, &y0, &x1, &y1 );

		int total = (int)( advance * scale + 0.5f );

		a = x0;
		w = x1 - x0;
		c = total - a - w;

		if( h ) *h = y1 - y0;
		if( top ) *top = baseline + y0;
	}

	stbtt_fontinfo	info;
	int		devicePx, logicalPx, codepage;
	float		scaleDevice, scaleLogical;
	int		baselineDevice;
	Glyph		glyph[256];
};

/*
================
CofFont - the metric conversion, and nothing else

The base Font's size is ABI-fixed and its init() runs from the base
constructor, so the base is built normally (it makes a throwaway system font,
which the library's own font list owns and frees) and the rasteriser is swapped
into `impl` afterwards. `impl` and `id` are protected members of Font, which is
why this derives rather than wraps.
================
*/
CofFont::CofFont( FontPlat_Inter *newPlat ) :
	Font( "CofInter", 1, 0, 0.0f, 400, false, false, false, false ), plat( newPlat )
{
	impl = plat;

	// Font::init() reuses a platform font whose constructor arguments match,
	// and when it does it never allocates an id - so every CofFont after the
	// first would share id -1, and the surface's atlas cache is keyed by id.
	// That is one atlas for every size and code page at once
	id = Font_AllocId();
}

void CofFont::getCharABCwide( int ch, int &a, int &b, int &c )
{
	plat->logicalABCwide( ch, a, b, c );
}

int CofFont::getTall()
{
	return plat->logicalTall();
}

int CofFont::deviceTall() const
{
	return plat->deviceTall();
}

void CofFont::deviceABCwide( int ch, int &a, int &b, int &c )
{
	plat->getCharABCwide( ch, a, b, c );
}

} // namespace vgui

namespace
{

using vgui::CofFont;
using vgui::FontPlat_Inter;

// one cache entry per (role, size, code page, face)
struct CofEntry
{
	char		role[64];
	int		devicePx, logicalPx, codepage, semibold;
	int		backing;	// milestone 4b, alpha of this role's backing strip
	CofFont		*font;
};

enum { COF_FONT_CACHE = 24 };

CofEntry g_cache[COF_FONT_CACHE];
int g_cacheCount;

// what role each client font id was built for, from the strip it loaded
struct CofRole
{
	int	id;
	char	role[64];
};

enum { COF_ROLE_MAX = 64 };

CofRole g_roles[COF_ROLE_MAX];
int g_roleCount;

const char *CofRoleForId( int id )
{
	for( int i = 0; i < g_roleCount; i++ )
	{
		if( g_roles[i].id == id )
			return g_roles[i].role;
	}

	return nullptr;
}

CofFont *CofFindOrCreate( const char *role, const cof_vgui_font_t &desc )
{
	for( int i = 0; i < g_cacheCount; i++ )
	{
		if( g_cache[i].devicePx == desc.devicePx && g_cache[i].logicalPx == desc.logicalPx &&
			g_cache[i].codepage == desc.codepage && g_cache[i].semibold == desc.semibold &&
			!strcmp( g_cache[i].role, role ))
			return g_cache[i].font;
	}

	if( g_cacheCount >= COF_FONT_CACHE )
		return nullptr; // a session that changed resolution two dozen times; fall back

	int size = 0;
	const unsigned char *ttf = g_engine->CofFontFile( desc.semibold, &size );

	if( !ttf || size <= 0 )
		return nullptr;

	FontPlat_Inter *plat = FontPlat_Inter::Create( ttf, size, desc.devicePx, desc.logicalPx, desc.codepage );

	if( !plat )
		return nullptr;

	CofEntry &e = g_cache[g_cacheCount++];

	vgui_strcpy( e.role, sizeof( e.role ), role );
	e.devicePx = desc.devicePx;
	e.logicalPx = desc.logicalPx;
	e.codepage = desc.codepage;
	e.semibold = desc.semibold;
	e.backing = desc.backing;
	e.font = new CofFont( plat );

	return e.font;
}

// Font constructor hook: consume the role the engine latched from the strip
// load that came immediately before this font was built.
void CofFontCreated( Font *font, bool fromFile )
{
	char role[64];

	// Every font the client builds from a strip is one of its VGUI text fonts
	// and gets an engine face; the scheme name only chooses the SIZE. A font
	// built before any strip load has no role and takes the body size, which
	// is also how the two schemes that have no strip at any resolution -
	// "Default Text" and "impact", which render nothing in the stock game -
	// start working at all.
	if( !fromFile || !g_engine || !g_engine->CofFontRoleLatch )
		return;

	if( !g_engine->CofFontRoleLatch( role, sizeof( role )))
		role[0] = 0;

	int id = font->getId();

	for( int i = 0; i < g_roleCount; i++ )
	{
		if( g_roles[i].id == id )
		{
			vgui_strcpy( g_roles[i].role, sizeof( g_roles[i].role ), role );
			return;
		}
	}

	if( g_roleCount >= COF_ROLE_MAX )
		return;

	g_roles[g_roleCount].id = id;
	vgui_strcpy( g_roles[g_roleCount].role, sizeof( g_roles[g_roleCount].role ), role );
	g_roleCount++;
}

Font *CofFontSubstitute( Font *font )
{
	if( !font || !g_engine || !g_engine->CofFontDesc )
		return font;

	const char *role = CofRoleForId( font->getId( ));

	if( !role )
		return font; // not one of the client's strip fonts

	if( !role[0] )
		role = "inventory"; // no scheme was latched: the body size

	cof_vgui_font_t desc;

	memset( &desc, 0, sizeof( desc ));

	if( !g_engine->CofFontDesc( role, &desc ))
		return font; // cof_ui_inter_fonts 0, or not this game

	CofFont *sub = CofFindOrCreate( role, desc );

	return sub ? (Font *)sub : font;
}

// Cry of Fear language packs: the engine owns the table, this only
// routes the call. A missing entry point (an older engine against a
// newer support library) leaves the text alone.
static const char *CofLangSubstitute( const char *str )
{
	if( !str || !g_engine || !g_engine->CofLangString )
		return str;

	return g_engine->CofLangString( str );
}

} // anonymous namespace

void vgui::CofFont_Install( void )
{
	Font_SetSubstituteHook( CofFontSubstitute );
	Font_SetCreatedHook( CofFontCreated );
	Text_SetSubstituteHook( CofLangSubstitute );
}

vgui::Font *vgui::CofFont_ForRole( const char *role )
{
	cof_vgui_font_t desc;

	if( !role || !g_engine || !g_engine->CofFontDesc )
		return nullptr;

	memset( &desc, 0, sizeof( desc ));

	if( !g_engine->CofFontDesc( role, &desc ))
		return nullptr;

	return (Font *)CofFindOrCreate( role, desc );
}

// milestone 4b: the backing-strip alpha the engine assigns to this font's
// role. Non-zero only for the notes/document reader, so nothing else in the
// client's VGUI layer - not the inventory, not the boss bar - can grow a strip.
//
// Asked of the engine on every call rather than read out of the cache entry:
// the fonts are built once per level load, and cof_hud_text_backing has to be
// live the way its engine-side twin is. The entry's own copy is only the value
// the font was created with, kept for the trace.
bool vgui::CofFont_TraceOn( void )
{
	cof_vgui_font_t desc;

	if( !g_engine || !g_engine->CofFontDesc )
		return false;

	memset( &desc, 0, sizeof( desc ));

	// any role will do: the trace flag is global, it is carried on the
	// descriptor only because that is the channel that already exists
	if( !g_engine->CofFontDesc( "pager", &desc ))
		return false;

	return desc.trace != 0;
}

bool vgui::CofFont_Info( Font *font, int *backing, int *ytarget, int *trace, const char **role )
{
	if( backing ) *backing = 0;
	if( ytarget ) *ytarget = -1;
	if( trace )   *trace = 0;
	if( role )    *role = "";

	for( int i = 0; i < g_cacheCount; i++ )
	{
		if( (Font *)g_cache[i].font != font )
			continue;

		if( role )
			*role = g_cache[i].role;

		if( g_engine && g_engine->CofFontDesc )
		{
			cof_vgui_font_t desc;

			memset( &desc, 0, sizeof( desc ));

			if( g_engine->CofFontDesc( g_cache[i].role, &desc ))
			{
				if( backing ) *backing = desc.backing;
				if( ytarget ) *ytarget = desc.ytarget;
				if( trace )   *trace = desc.trace;
				return true;
			}
		}

		// the engine went quiet (cof_ui_inter_fonts 0 mid-session): fall back
		// to what the font was built with, which is what is on screen anyway
		if( backing ) *backing = g_cache[i].backing;
		return true;
	}

	return false;
}

int vgui::CofFont_Backing( Font *font )
{
	int backing = 0;

	CofFont_Info( font, &backing, nullptr, nullptr, nullptr );

	return backing;
}

vgui::CofFont *vgui::CofFont_Cast( Font *font )
{
	for( int i = 0; i < g_cacheCount; i++ )
	{
		if( (Font *)g_cache[i].font == font )
			return g_cache[i].font;
	}

	return nullptr;
}

bool vgui::CofFont_MessageStyle( Font *font )
{
	for( int i = 0; i < g_cacheCount; i++ )
	{
		if( (Font *)g_cache[i].font != font ) continue;
		cof_vgui_font_t desc;
		memset( &desc, 0, sizeof( desc ));
		return g_engine && g_engine->CofFontDesc &&
			g_engine->CofFontDesc( g_cache[i].role, &desc ) && desc.semibold == 2;
	}
	return false;
}
