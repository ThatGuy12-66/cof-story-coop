/*
cof_panel_transparency.c - Cry of Fear: the live world behind the in-play panels

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

====================================================================

cof_panel_transparent  (saved, default 1)
cof_panel_plate_alpha  (saved, default 200)
cof_panel_hide_hud     (saved, default 2)

The engine renders the live 3D world every frame behind every Cry of Fear
panel; what the player sees as black is an extra full-screen opaque panel the
client puts behind the art (the inventory's backdrop child, the note's own
background, the tape recorder page's four masks, and the same pattern for the
phone, keypad, computer, statue, landline, documents list and billboard). With
cof_panel_transparent 1 the VGUI support library skips exactly those fills
while such a panel is open (3rdparty/freevgui/platform/xash3d-fwgs/
cofbackdrop.cpp), so the world shows around the art. Telescope masks, cutscene
masks and letterbox bars belong to other panels and are never touched; the
note's half-transparent layer stays.

What is left to decide is handed to the library through
vguiapi_t::CofPanelConfig:

  * the rusty 800x600 plate gfx/vgui/backgroundtoall.tga, which is opaque, is
    drawn at cof_panel_plate_alpha (0-255 opacity) so it stays a readable
    frame without hiding the world;
  * the HUD container (health and stamina bars, hint bar, message strip) was
    hidden by the black before and now shows. cof_panel_hide_hud: 0 = keep all
    of it, 1 = hide all of it while a panel is open, 2 = hide only the pieces
    that overlap the open panel's art (the default).

The plate is recognised by name: the client loads every VGUI picture through
the engine's COM_LoadFile (cl_game.c, CL_CoF_LoadFile) immediately before it
builds the BitmapTGA from the bytes, so the file name latched here names the
bitmap the library constructs next (vguiapi_t::CofImageLatch), the same
technique the Inter font roles use for the font strips.

Research: stage1/controller-research-20260923/RESULTS.md, Q1.
*/

#include "common.h"
#include "client.h"
#include "vgui_api.h"

static CVAR_DEFINE_AUTO( cof_panel_transparent, "1", FCVAR_ARCHIVE, "Cry of Fear: show the live world behind the inventory, notes, tape recorder page and the other in-play panels instead of their black backdrop (0 = black, as the original)" );
static CVAR_DEFINE_AUTO( cof_panel_plate_alpha, "200", FCVAR_ARCHIVE, "Cry of Fear: opacity 0-255 of the rusty plate (backgroundtoall.tga) behind the in-play panels while cof_panel_transparent is on" );
static CVAR_DEFINE_AUTO( cof_panel_hide_hud, "2", FCVAR_ARCHIVE, "Cry of Fear: while an in-play panel shows the world behind it, 0 = keep the HUD, 1 = hide the HUD, 2 = hide only HUD pieces that overlap the panel" );

static string	cof_panel_image;	// the last gfx/vgui/*.tga the client loaded

/*
====================
CL_CoF_PanelImageLoad

Called for every COM_LoadFile the client makes. Remembers the base name of a
gfx/vgui TGA, forgets it for anything else, so a latch never outlives the
load it belongs to.
====================
*/
void CL_CoF_PanelImageLoad( const char *filename )
{
	const char	*name;

	cof_panel_image[0] = '\0';

	if( COM_StringEmptyOrNULL( filename ) || !Q_stristr( filename, "vgui" ))
		return;

	name = COM_FileWithoutPath( filename );

	if( !name || Q_stricmp( COM_FileExtension( name ), "tga" ))
		return;

	Q_strncpy( cof_panel_image, name, sizeof( cof_panel_image ));
	COM_StripExtension( cof_panel_image );
}

/*
====================
CL_CoF_PanelImageLatch

vguiapi_t::CofImageLatch: consume the latched name. One shot.
====================
*/
int CL_CoF_PanelImageLatch( char *buf, int size )
{
	if( !buf || size <= 0 || !cof_panel_image[0] )
		return 0;

	Q_strncpy( buf, cof_panel_image, size );
	cof_panel_image[0] = '\0';

	return 1;
}

/*
====================
CL_CoF_PanelConfig

vguiapi_t::CofPanelConfig: the transparency settings, read once per painted
frame. Returns 0 while the feature is off.
====================
*/
int CL_CoF_PanelConfig( cof_vgui_panel_cfg_t *out )
{
	if( !out )
		return 0;

	out->transparent = cof_panel_transparent.value != 0.0f;
	out->plateAlpha = bound( 0, (int)cof_panel_plate_alpha.value, 255 );
	out->hideHud = bound( 0, (int)cof_panel_hide_hud.value, 2 );
	out->trace = (int)Cvar_VariableValue( "cof_panel_trace" );

	return out->transparent;
}

void CL_CoF_PanelTransparencyInit( void )
{
	Cvar_RegisterVariable( &cof_panel_transparent );
	Cvar_RegisterVariable( &cof_panel_plate_alpha );
	Cvar_RegisterVariable( &cof_panel_hide_hud );
}
