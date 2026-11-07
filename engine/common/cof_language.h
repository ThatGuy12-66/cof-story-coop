/*
cof_language.h - Cry of Fear native language packs (cof_language)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef COF_LANGUAGE_H
#define COF_LANGUAGE_H

// CL_DrawString / CL_DrawStringLen flag: the text comes from the client DLL
// (pfnDrawConsoleString, pfnDrawString, ...) and may be substituted from the
// active pack's table. Engine-owned text (console, notify, net graph) never
// carries it, so a console line that happens to match a table entry is left
// alone. Far above the stock FONT_DRAW_* bits in client.h (0..6).
#define FONT_DRAW_COF_LANG BIT( 15 )

// registered from FS_Init, so the cvar exists before any config is exec'd
void CoF_Lang_Init( void );

// called from Host_Main before the game libraries are loaded: applies the
// pack cof_language will name once the command line and config.cfg run, so
// text the client reads at init already comes from the pack
void CoF_Lang_EarlyApply( void );

// checked once per Host_Frame: applies the pack when cof_language changed
void CoF_Lang_Frame( void );

// (re-)mount the active pack's engine-loaded assets. Called at the end of
// FS_Rescan_f, because a rescan rebuilds the whole search path from gameinfo
// and would otherwise drop us.
void CoF_Lang_Remount( void );

// is a pack active, and which
qboolean CoF_Lang_Active( void );
const char *CoF_Lang_Code( void );

// patch the KERNEL32!CreateFileW import of a just-loaded game DLL (client or
// server), in memory. Safe with a NULL handle and on non-Windows builds; the
// shim is installed unconditionally and passes everything through until a
// pack is active.
void CoF_Lang_HookModule( void *hInstance, const char *name );

// put a hooked module's original import back; called from COM_FreeLibrary
// with the raw module handle of every library just before it is unloaded
void CoF_Lang_UnhookModule( void *module );

// the game DLLs' own txtfiles/, notes/ and inventoryitems/ reads through the
// engine filesystem (COM_LoadFileForMe): the pack's file for a gamedir-relative
// name, when the pack has one
qboolean CoF_Lang_GameFile( const char *name, char *out, size_t size );

// "maps/c_subway.ent" -> "languages/polish/maps/c_subway.ent" when the pack
// carries that override. Returns false and leaves `out` alone otherwise.
qboolean CoF_Lang_EntPatch( const char *entfilename, char *out, size_t size );

// maps/<map>.entpatch of the active pack (only when the pack has no
// maps/<map>.ent of its own) applied to `entdata`, the entity string the map
// would load otherwise. Returns a Mem_Malloc'd (host.mempool), NUL-terminated
// string and its length, or NULL when there is nothing to apply or the patch
// cannot be used (logged). See cof_entpatch.c for the format.
char *CoF_Lang_ApplyEntityPatch( const char *entfilename, const char *entdata, size_t entdatasize, size_t *outsize );

// a studio model just read into memory (`data`: the engine's own copy of the
// .mdl, or its T.mdl, `size` bytes): every embedded texture the active pack
// repaints (models/<model path without .mdl>/<texture name>, an 8-bit BMP of
// the same size) gets the pack's pixels and palette before the renderer
// uploads it. See cof_language.c.
void CoF_Lang_StudioTextures( const char *modelname, void *data, size_t size );

// "c2_bussback" -> "languages/polish/textures/c2_bussback.tga" when the pack
// carries that sign override.
qboolean CoF_Lang_TextureReplacement( char *out, size_t size, const char *texname, const char *type );

// draw-time substitution of the strings compiled into client.dll / hl.dll.
// Returns `str` itself when there is nothing to substitute, so the result is
// always safe to draw. A rebuilt (formatted) result lives in a small ring of
// static buffers: copy it before the fourth next call.
//
// CoF_Lang_String gives the pack's code-page bytes, for the VGUI text path,
// whose Inter font layer decodes them with cof_text_codepage.
// CoF_Lang_StringEngineFont gives the engine's own bitmap fonts a variant
// they can draw: UTF-8 (utf8) or code-page bytes, with letters those sheets
// have no cell for folded to their base letter.
const char *CoF_Lang_String( const char *str );
const char *CoF_Lang_StringEngineFont( const char *str, qboolean utf8 );

// the characters the active pack's text uses beyond ASCII (strings/glyphs.txt): their count, the list in *out
// (valid until the pack changes) and a number that changes whenever the list does. 0 with no pack or no list.
int CoF_Lang_Glyphs( const uint32_t **out, int *generation );

#endif // COF_LANGUAGE_H
