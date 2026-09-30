// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#ifndef VGUI_FONT_H
#define VGUI_FONT_H

#include "vgui.h"

namespace vgui
{
class BaseFontPlat;
void Font_Reset( void );

class CLASSEXPORT Font
{
	friend class Surface;

protected:
	const char *name;
	BaseFontPlat *impl;
	int id;
public:
	Font( const char *name, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol );
	Font( const char *name, void *pFileData, int fileDataLen, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol );

private:
	virtual void init( const char *newName, void *pFileData, int fileDataLen, int tall, int wide, float rotation, int weight, bool italic, bool underline, bool strikeout, bool symbol );

public:
	BaseFontPlat *getPlat();
	virtual void getCharRGBA( int ch, int rgbaX, int rgbaY, int rgbaWide, int rgbaTall, unsigned char *rgba );
	virtual void getCharABCwide( int ch, int &a, int &b, int &c );
	virtual void getTextSize( const char *str, int &wide, int &tall );
	virtual int getTall();
#ifndef _WIN32
	virtual int getWide();
#endif
	virtual int getId();
};

CHECK_STRUCT_SIZE( Font, 16, 32, 32 );

// Cry of Fear engine-rasterised VGUI fonts (milestone 4). The platform layer
// may install a substitute for the game's own bitmap fonts - see
// platform/xash3d-fwgs/coffont.cpp - and everything inside this library that
// measures or draws text has to go through Font_Active() to see it.
//
// The indirection is not optional: the game overwrites the vtable of the font
// objects it builds, so a virtual call on one of them reaches ITS
// getCharABCwide and its shipped .chw width table, not ours. Font_Active()
// returns the engine's font when there is one, and the client's own font
// otherwise, which is what makes cof_ui_inter_fonts 0 exactly stock.
typedef Font *( *FontSubstituteFn )( Font *font );
typedef void ( *FontCreatedFn )( Font *font, bool fromFile );

// A font id nobody else will use. Font::init() only allocates one when it
// actually builds a new platform font, and it reuses an existing one whenever
// the constructor arguments match - so a family of fonts that differ in
// something init() cannot see (an engine face's size or code page, say) would
// otherwise all end up sharing id -1, and with it one atlas.
int Font_AllocId( void );

void Font_SetSubstituteHook( FontSubstituteFn fn );
void Font_SetCreatedHook( FontCreatedFn fn );
Font *Font_Active( Font *font );

// Cry of Fear language packs (cof_language). The captions compiled into
// the game DLLs have no file slot at all, so they are swapped at draw
// time from the active pack's table. The hook lives with the font hooks
// above because it is installed from the same place and consumed by the
// same text path; the platform layer routes it into the engine.
//
// TextImage::setText is the chokepoint: it runs before getTextSize() and
// before wrapping, so the layout is computed from the text that is
// actually drawn. Text_Substitute() returns its argument unchanged when
// there is no hook, no pack, or no match.
typedef const char *( *TextSubstituteFn )( const char *str );

void Text_SetSubstituteHook( TextSubstituteFn fn );
const char *Text_Substitute( const char *str );

}

#endif // VGUI_FONT_H
