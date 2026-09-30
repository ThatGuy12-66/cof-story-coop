// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear: the live world behind the in-play panels (cof_panel_transparent)

#include <string.h>
#include "vgui_internal.h"
#include "support.h"
#include "image.h"

using namespace vgui;

/*
The engine renders the live world every frame behind every Cry of Fear panel.
What hides it is black the client paints on purpose, measured per panel
(stage1/controller-research-20260923/RESULTS.md, Q1.2):

	inventory, phone, keypad, computer,     a child plain vgui::Panel covering
	statue, landline, documents list,       the whole panel, background
	billboard                               (0,0,0,0) = opaque black
	note, user document                     the panel's own background,
	                                        (0,0,0,0), full screen
	tape recorder page (CSaveLoad)          four plain vgui::Panel masks
	                                        around the picture, (0,0,0,0)

(VGUI1 colours: alpha 0 is opaque, 255 invisible.) While one of those panels
is visible and cof_panel_transparent is on, exactly those fills are skipped:
a background fill is left out only when it is opaque black AND it is either
the owner panel's own full-screen background or a plain vgui::Panel directly
below the owner that covers the owner (or any plain mask of the tape page).
Everything else - the telescope's and the cutscene's masks, the letterbox
bars (other panels), the note's half-transparent (0,0,0,128) layer, the
save slot strips (0,0,0,1) - is drawn as before.

Two things the black used to hide need a decision, both taken from the engine
through vguiapi_t::CofPanelConfig:

  * the 800x600 rusty plate gfx/vgui/backgroundtoall.tga is opaque; it is
    drawn at cofPanelConfig.plateAlpha. It is recognised by name when the
    client builds it (vguiapi_t::CofImageLatch) and by size when it is drawn.
  * the HUD container (CHUDControl: bars, hint bar, message strip) is painted
    before the panels and now shows. hideHud 0 keeps it, 1 hides it, 2 hides
    only the pieces whose rectangle overlaps the open panel's art. Measured at
    1920x1080 (cof_panel_trace 3): the health and stamina bars (32x160 at
    32,920 and 64,920) and the top hint bar never overlap, the message strip
    (1920x64 at 0,540: pickups and subtitles) always does. The letterbox bars
    (plain black panels of the same container) are never hidden.
*/

namespace
{

// panels whose black this file may skip
const unsigned int COF_BACKDROP_OWNERS = COF_PANEL_INVENTORY | COF_PANEL_DOCUMENTS | COF_PANEL_NOTE
	| COF_PANEL_USERDOC | COF_PANEL_BILLBOARD | COF_PANEL_SAVELOAD | COF_PANEL_PHONE
	| COF_PANEL_TELEPHONE | COF_PANEL_COMPUTER | COF_PANEL_KEYPAD | COF_PANEL_STATUE;

enum
{
	COF_MAX_OWNERS = 8,
	COF_MAX_ART = 32,
	COF_MAX_PLATES = 64,
};

struct CofRect
{
	int x0, y0, x1, y1;
};

struct CofBackdropState
{
	cof_vgui_panel_cfg_t	cfg;
	bool		active;			// a backdrop owner is up and the feature is on
	Panel		*owners[COF_MAX_OWNERS];
	int		numOwners;
	Panel		*hud;			// the HUD container, when found
	CofRect		art[COF_MAX_ART];	// what the open panels draw, solved coordinates
	int		numArt;

	// per-frame counters, for the trace
	int		skippedFills;
	int		platesDrawn;
	int		hudHidden;

	// what the trace printed last
	bool		loggedActive;
	unsigned int	loggedOwners;
	int		loggedFills, loggedPlates, loggedHud;
	unsigned int	dumpedOwners;
};

CofBackdropState g_bd;

// every bitmap built from backgroundtoall.tga; a bitmap is never freed while
// its panel exists, and a stale entry is caught by the size check anyway
const Bitmap	*g_plates[COF_MAX_PLATES];
int		g_numPlates = 0;

bool CofCovers( Panel *panel, Panel *outer )
{
	int	pw, ph, ow, oh;

	panel->getSize( pw, ph );
	outer->getSize( ow, oh );

	return pw >= ow && ph >= oh;
}

bool CofIsOwner( Panel *panel )
{
	for( int i = 0; i < g_bd.numOwners; i++ )
	{
		if( g_bd.owners[i] == panel )
			return true;
	}

	return false;
}

bool CofIsOpaqueBlack( Panel *panel )
{
	int	r, g, b, a;

	panel->getBgColor( r, g, b, a );

	return r == 0 && g == 0 && b == 0 && a == 0;
}

// the client's black behind an open owner panel
bool CofIsBackdrop( Panel *panel )
{
	Panel *parent;

	if( !CofIsOpaqueBlack( panel ))
		return false;

	// the owner's own full-screen background: the note, the user document
	parent = panel->getParent();

	if( CofIsOwner( panel ))
		return parent && CofCovers( panel, parent );

	// a plain panel right below the owner: the full-screen backdrop child,
	// or one of the tape recorder page's masks
	if( !parent || !CofIsOwner( parent ) || CofPanels_Class( panel ) != COF_CLASS_PLAIN_PANEL )
		return false;

	return CofCovers( panel, parent ) || CofPanels_Class( parent ) == COF_PANEL_SAVELOAD;
}

void CofSolvedRect( Panel *panel, CofRect &r )
{
	const int *origin = CofPanelAccess::screenOrigin( panel );
	const int *size = CofPanelAccess::size( panel );

	r.x0 = origin[0];
	r.y0 = origin[1];
	r.x1 = origin[0] + size[0];
	r.y1 = origin[1] + size[1];
}

bool CofOverlapsArt( Panel *panel )
{
	CofRect r;

	CofSolvedRect( panel, r );

	for( int i = 0; i < g_bd.numArt; i++ )
	{
		const CofRect &a = g_bd.art[i];

		if( r.x0 < a.x1 && a.x0 < r.x1 && r.y0 < a.y1 && a.y0 < r.y1 )
			return true;
	}

	return false;
}

// the HUD pieces cof_panel_hide_hud leaves out
bool CofHudPieceHidden( Panel *panel )
{
	if( !g_bd.active || !g_bd.cfg.hideHud || !g_bd.hud )
		return false;

	if( CofPanelAccess::parent( panel ) != g_bd.hud )
		return false;

	// the cutscene letterbox bars (plain black panels of the HUD container)
	// are never touched, whatever the mode
	if( CofPanels_Class( panel ) == COF_CLASS_PLAIN_PANEL && CofIsOpaqueBlack( panel ))
		return false;

	if( g_bd.cfg.hideHud == 2 && !CofOverlapsArt( panel ))
		return false;

	return true;
}

// skipTraverse filter
bool CofSkipTraverse( Panel *panel )
{
	if( !CofHudPieceHidden( panel ))
		return false;

	g_bd.hudHidden++;
	return true;
}

// skipBackground filter: the black itself
bool CofSkipBackground( Panel *panel )
{
	if( !g_bd.active || !CofIsBackdrop( panel ))
		return false;

	g_bd.skippedFills++;
	return true;
}

// case-insensitive, ASCII: the client's own file names mix cases
bool CofNameIs( const char *name, const char *want )
{
	for( ; *name && *want; name++, want++ )
	{
		char a = *name, b = *want;

		if( a >= 'A' && a <= 'Z' ) a += 'a' - 'A';
		if( b >= 'A' && b <= 'Z' ) b += 'a' - 'A';
		if( a != b )
			return false;
	}

	return !*name && !*want;
}

bool CofIsPlate( Bitmap *bitmap )
{
	int w, h;

	for( int i = 0; i < g_numPlates; i++ )
	{
		if( g_plates[i] != bitmap )
			continue;

		bitmap->getSize( w, h );
		return w == 800 && h == 600;
	}

	return false;
}

void CofBitmapCreated( Bitmap *bitmap )
{
	char name[64];

	if( !g_engine || !g_engine->CofImageLatch )
		return;

	if( !g_engine->CofImageLatch( name, sizeof( name )))
		return;

	if( !CofNameIs( name, "backgroundtoall" ))
		return;

	for( int i = 0; i < g_numPlates; i++ )
	{
		if( g_plates[i] == bitmap )
			return;
	}

	if( g_numPlates < COF_MAX_PLATES )
		g_plates[g_numPlates++] = bitmap;
	else
		g_plates[COF_MAX_PLATES - 1] = bitmap; // newest wins
}

void CofBitmapPaint( Bitmap *bitmap, Panel *panel, int &r, int &g, int &b, int &a )
{
	Panel	*owner;
	int	opacity;

	(void)r; (void)g; (void)b;

	if( !g_bd.active || !panel || !CofIsPlate( bitmap ))
		return;

	// the plate hangs right below an open owner panel
	owner = CofPanelAccess::parent( panel );

	if( !owner || !CofIsOwner( owner ))
		return;

	// VGUI1 alpha: 0 opaque, 255 invisible
	opacity = ( 255 - a ) * g_bd.cfg.plateAlpha / 255;
	a = 255 - opacity;

	g_bd.platesDrawn++;
}

// cof_panel_trace 3: the open panels' trees and the HUD container's pieces,
// once per change of the open panels, to see what is skipped and why
void CofDumpTree( Panel *panel, int depth, int maxDepth )
{
	char		name[128], line[320];
	const int	*origin = CofPanelAccess::screenOrigin( panel );
	const int	*size = CofPanelAccess::size( panel );
	int		r, g, b, a;

	panel->getBgColor( r, g, b, a );

	snprintf( line, sizeof( line ), "[cof-panel] %*sd%d %s vis %d rect %d,%d %dx%d bg %d,%d,%d,%d%s%s\n",
		depth * 2, "", depth, CofPanels_ClassName( panel, name, sizeof( name )), panel->isVisible( ) ? 1 : 0,
		origin[0], origin[1], size[0], size[1], r, g, b, a,
		CofIsBackdrop( panel ) ? " BACKDROP" : "",
		( CofPanelAccess::parent( panel ) != g_bd.hud || !panel->isVisible( )) ? ""
			: CofHudPieceHidden( panel ) ? " HUD-HIDDEN" : " HUD-KEPT" );
	g_engine->CofPrint( line );

	if( depth >= maxDepth )
		return;

	for( int i = 0; i < panel->getChildCount(); i++ )
	{
		Panel *child = panel->getChild( i );

		if( child && child->isVisible( ))
			CofDumpTree( child, depth + 1, maxDepth );
	}
}

void CofTrace( void )
{
	unsigned int	owners = 0;

	for( int i = 0; i < g_bd.numOwners; i++ )
		owners |= CofPanels_Class( g_bd.owners[i] );

	if( g_bd.active == g_bd.loggedActive && owners == g_bd.loggedOwners
		&& g_bd.skippedFills == g_bd.loggedFills && g_bd.platesDrawn == g_bd.loggedPlates
		&& g_bd.hudHidden == g_bd.loggedHud )
		return;

	g_bd.loggedActive = g_bd.active;
	g_bd.loggedOwners = owners;
	g_bd.loggedFills = g_bd.skippedFills;
	g_bd.loggedPlates = g_bd.platesDrawn;
	g_bd.loggedHud = g_bd.hudHidden;

	if( g_bd.cfg.trace <= 0 || !g_engine || !g_engine->CofPrint )
		return;

	if( g_bd.cfg.trace >= 3 && g_bd.active && owners != g_bd.dumpedOwners )
	{
		for( int i = 0; i < g_bd.numOwners; i++ )
			CofDumpTree( g_bd.owners[i], 2, 4 );
		if( g_bd.hud )
			CofDumpTree( g_bd.hud, 2, 3 );
		for( int i = 0; i < g_bd.numArt; i++ )
		{
			char art[128];

			snprintf( art, sizeof( art ), "[cof-panel] art rect %d: %d,%d - %d,%d\n", i,
				g_bd.art[i].x0, g_bd.art[i].y0, g_bd.art[i].x1, g_bd.art[i].y1 );
			g_engine->CofPrint( art );
		}
	}
	g_bd.dumpedOwners = g_bd.active ? owners : 0;

	char line[256];

	snprintf( line, sizeof( line ),
		"[cof-panel] world behind panels: %s owners 0x%x, per frame %d black fills skipped, %d plates at alpha %d, %d HUD pieces hidden (mode %d, %d art rects)\n",
		g_bd.active ? "ON" : "off", owners, g_bd.skippedFills, g_bd.platesDrawn,
		g_bd.cfg.plateAlpha, g_bd.hudHidden, g_bd.cfg.hideHud, g_bd.numArt );
	g_engine->CofPrint( line );
}

} // anonymous namespace

/*
================
CofBackdrop_Install

Called once from the support API entry point.
================
*/
void vgui::CofBackdrop_Install( void )
{
	Panel_SetPaintFilters( CofSkipTraverse, CofSkipBackground );
	Bitmap_SetHooks( CofBitmapCreated, CofBitmapPaint );
}

/*
================
CofBackdrop_Frame

Once per painted frame, after CofPanels_Frame and before the tree is painted:
read the settings, find the open owner panels, the HUD container and the
rectangles the open panels draw, and report what the previous frame did.
================
*/
void vgui::CofBackdrop_Frame( Panel *root )
{
	// what the previous frame did, then start counting again
	CofTrace();
	g_bd.skippedFills = g_bd.platesDrawn = g_bd.hudHidden = 0;

	g_bd.active = false;
	g_bd.numOwners = 0;
	g_bd.numArt = 0;
	g_bd.hud = nullptr;
	memset( &g_bd.cfg, 0, sizeof( g_bd.cfg ));

	if( !g_engine || !g_engine->CofPanelConfig || !g_engine->CofPanelConfig( &g_bd.cfg ))
		return;

	if( !root || !( CofPanels_Visible() & COF_BACKDROP_OWNERS ))
		return;

	// root -> client viewport -> panels
	for( int i = 0; i < root->getChildCount(); i++ )
	{
		Panel *viewport = root->getChild( i );

		if( !viewport || !viewport->isVisible( ))
			continue;

		for( int j = 0; j < viewport->getChildCount(); j++ )
		{
			Panel		*panel = viewport->getChild( j );
			unsigned int	cls;

			if( !panel )
				continue;

			cls = CofPanels_Class( panel );

			if( cls == COF_PANEL_HUD )
			{
				g_bd.hud = panel;
				continue;
			}

			if( !( cls & COF_BACKDROP_OWNERS ) || !panel->isVisible( ) || g_bd.numOwners >= COF_MAX_OWNERS )
				continue;

			g_bd.owners[g_bd.numOwners++] = panel;
		}
	}

	g_bd.active = g_bd.numOwners > 0;

	// the art: every visible child of an owner that is not full-screen (the
	// backdrop, the note's dimming layer) - the plate, the frames, the picture.
	// Full-width children are left out too: they are the caption labels
	// (1920x64 at the bottom of the note and the billboard, transparent,
	// their text centred), and counting their whole width would hide the
	// health bars in the corner under an empty stretch of label.
	for( int i = 0; i < g_bd.numOwners; i++ )
	{
		Panel	*owner = g_bd.owners[i];
		int	ownerWide, ownerTall;

		owner->getSize( ownerWide, ownerTall );

		for( int j = 0; j < owner->getChildCount() && g_bd.numArt < COF_MAX_ART; j++ )
		{
			Panel	*child = owner->getChild( j );
			int	childWide, childTall;

			// the black itself (the tape page's masks do not cover the
			// page, but they are not art)
			if( !child || !child->isVisible( ) || CofCovers( child, owner ) || CofIsBackdrop( child ))
				continue;

			child->getSize( childWide, childTall );

			if( childWide >= ownerWide )
				continue;

			CofSolvedRect( child, g_bd.art[g_bd.numArt] );

			if( g_bd.art[g_bd.numArt].x1 > g_bd.art[g_bd.numArt].x0 && g_bd.art[g_bd.numArt].y1 > g_bd.art[g_bd.numArt].y0 )
				g_bd.numArt++;
		}
	}
}
