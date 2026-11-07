/*
cof_glyphs.h - Cry of Fear: the engine fonts' characters beyond their cp1252 atlas (cof_glyphs.c)
GPL-3.0-or-later.
*/
#ifndef COF_GLYPHS_H
#define COF_GLYPHS_H

#include "cof_text_logic.h"

// faces an engine font draws its missing characters with (cl_font_t::cofFace)
#define COF_FACE_REGULAR	0
#define COF_FACE_SEMIBOLD	1
#define COF_FACE_BOLD		2

void CoF_Glyphs_Init( void );
void CoF_Glyphs_Shutdown( void );	// the renderer is going: every page texture with it

// cof_text_glyphs is on: the engine fonts draw every character of the text code page, the ones their atlas lacks
// from TrueType. Off, the stock behaviour: one atlas cell per byte.
qboolean CoF_Glyphs_On( void );

// cof_text_codepage, normalised (1250, 1251, 1252 or COF_CP_UTF8)
int CoF_Glyphs_Codepage( void );

// UTF-8 text through the client's byte-by-byte HUD path (pfnDrawCharacter): glyphs on and the code page 65001
qboolean CoF_Glyphs_Utf8Hud( void );

// the CJK face file (gfx/fonts/CoFSansSC-*.otf), engine-owned, valid until shutdown; NULL when not installed.
// The VGUI library draws its CJK characters from the same bytes.
const byte *CoF_Glyphs_CJKFile( int bold, int *size );

// the width table bytes 0x80..0xFF of a cp1252 atlas get for the text code page: a single-byte page's character
// widths, or UTF-8's cells (a lead byte the whole character, continuation bytes nothing). Called after a font is
// loaded and scaled; again when the code page changes.
void CoF_Glyphs_FontWidths( cl_font_t *font );
void CoF_Glyphs_CodepageChanged( void );

// The width of one character in the font's pixels: the atlas cell's when the atlas has the character, else the
// TrueType advance at the font's size.
int CoF_Glyphs_Width( cl_font_t *font, uint32_t cp );

// The atlas cell for a character (1..255), or 0 when the atlas lacks it and TrueType draws it.
int CoF_Glyphs_AtlasCell( const cl_font_t *font, uint32_t cp );

// Draw a character the atlas lacks, at (x, y) in the font's pixels (before the HUD scale, like CL_DrawCharacter),
// with the colour and render mode already set. cellw > 0: centred in a cell of that width (the client's byte-wise
// layout); else at the pen. Returns the advance in the font's pixels.
int CoF_Glyphs_Draw( float x, float y, uint32_t cp, cl_font_t *font, int flags, int cellw );

#endif // COF_GLYPHS_H
