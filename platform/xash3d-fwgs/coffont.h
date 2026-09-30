// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear engine-rasterised VGUI fonts (cof_ui_inter_fonts, milestone 4)

#ifndef VGUI_XASH3D_COFFONT_H
#define VGUI_XASH3D_COFFONT_H

#include "font.h"

namespace vgui
{
class FontPlat_Inter;

// The engine-owned replacement for one of the game's bitmap fonts.
//
// Its public metrics are LOGICAL - the size the client should lay out in,
// given that cof_ui_scale multiplies every VGUI quad on the way out. The atlas
// builder needs the DEVICE size the glyphs were actually rasterised at, and
// asks for it through deviceTall() / deviceABCwide(). Reporting one where the
// other belongs is the difference between crisp text, text scaled twice, and a
// magnified bitmap.
class CofFont : public Font
{
public:
	CofFont( FontPlat_Inter *newPlat );

	virtual void getCharABCwide( int ch, int &a, int &b, int &c ) override;
	virtual int getTall() override;

	int deviceTall() const;
	void deviceABCwide( int ch, int &a, int &b, int &c );

private:
	FontPlat_Inter *plat;
};

// installs the Font substitute and Font-created hooks
void CofFont_Install( void );

// the CofFont this pointer really is, or null for any other font
CofFont *CofFont_Cast( Font *font );

// an engine font for a role, with no client font involved: the cof_font_probe
// developer string draws with this
Font *CofFont_ForRole( const char *role );

// milestone 4b/4c: everything the engine says about how this font's text is
// to be placed and decorated, asked of the engine on every call so the cvars
// behind it stay live (the client builds its fonts once per level load):
//
//   backing  alpha 0-255 of the translucent strip behind each line, 0 = none
//   ytarget  device y the first line of a run belongs at, -1 = leave it
//   trace    log this role's lines
//   role     the scheme name, for the trace; never null, "" when unknown
//
// Any out pointer may be null. Returns false for a font the engine did not
// supply, with every out left at its neutral value.
bool CofFont_Info( Font *font, int *backing, int *ytarget, int *trace, const char **role );

// developer: is cof_hud_text_trace on? Asked without a font, so text drawn with
// a font the engine did NOT substitute is traced too - which is the only way to
// see that it was not substituted.
bool CofFont_TraceOn( void );
bool CofFont_MessageStyle( Font *font ); // live engine-selected transient style

// milestone 4b: alpha 0-255 of the translucent strip to draw behind each line
// of text in this font, 0 for every role that does not want one
int CofFont_Backing( Font *font );
}

#endif // VGUI_XASH3D_COFFONT_H
