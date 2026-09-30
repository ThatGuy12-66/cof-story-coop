// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear in-game UI scaling (cof_ui_scale, unified UI milestone 4)

#include "vgui_internal.h"
#include "support.h"

using namespace vgui;

/*
The engine scales every VGUI quad from the client's layout pixels to device
pixels (VGUI_DrawQuad in engine/client/vgui/vgui_draw.c) and applies the exact
inverse to the cursor. That alone is not enough: a pure scale about the origin
walks the centred inventory panel off the right edge of the screen, and a pure
scale about the screen centre walks the bottom-left HUD bars off the bottom.

So this file supplies the other half. Each top-level client UI surface is
anchored, per axis, to whichever screen edge it was laid out against, and the
offset that implements that anchor is written into the panel's solved screen
origin - which is the one place absolute coordinates come from, so painting and
hit testing move together and cannot disagree.

	device = ( layout + offset ) * scale

	offset, per axis, for a surface laid out in a `layout` wide screen that is
	now being shown on a `screen` wide one at `scale`:

	  anchored low     (left / top)        0
	  anchored centre                      screen / ( 2 * scale ) - layout / 2
	  anchored high    (right / bottom)    screen / scale - layout

An axis is centred when the surface sits (nearly) symmetrically in the layout,
otherwise it is anchored to the nearer edge. The surface's whole subtree
inherits the offset for free, because Panel::solve() adds the parent's screen
origin to its own.

Which panel is a "surface": the outermost one, below the root, that does not
cover the whole layout screen. Everything above it is a container - see the
measured tree above CofAnchorOwner() below.

Note that the offsets are recomputed from the *current* render size against the
*pinned* layout size on every solve. That is what fixes the frozen-layout
shift: the client lays its panels out once, at the first VidInit, and a later
video mode change would otherwise leave them at the old screen centre.
*/

namespace
{

struct CofTransform
{
	bool	active;
	float	scale;
	int	layout[2];
	int	screen[2];
};

CofTransform g_cof = { false, 1.0f, { 0, 0 }, { 0, 0 } };

enum CofAnchor
{
	COF_ANCHOR_LOW = 0,	// left or top
	COF_ANCHOR_CENTER,
	COF_ANCHOR_HIGH		// right or bottom
};

int CofRound( float v )
{
	return (int)( v >= 0.0f ? v + 0.5f : v - 0.5f );
}

bool CofIsFullScreen( Panel *p )
{
	const int *size = CofPanelAccess::size( p );

	return size[0] >= g_cof.layout[0] && size[1] >= g_cof.layout[1];
}

// A full-screen page: a full-screen panel with no full-screen child of its own
// (so not the viewport, the HUD container or the inventory with its backdrop)
// whose visible children cover the whole layout screen between them.
//
// MEASURED, the tape recorder's SAVED GAMES page (client.dll panel at viewport
// +0x1464, constructor 100309E0; tree dump stage1/fix-ads-tape-20260922/
// evidence/tape7-tree.txt, 1920x1080 at 150 percent): a transparent full-screen
// panel holding the 640x480 save_load.tga image centred at 640,300 and FOUR
// OPAQUE BLACK MASK PANELS around it - 0,0 640x1080, 1280,0 640x1080, 640,0
// 640x300 and 640,780 640x300 - so the page shows the picture on black. Anchored
// one by one, the left and right masks went to their own edges while the
// picture stayed centred, and at any scale above 1 each mask slid over half of
// the picture: the recorder showed a black screen and nothing could be clicked.
// As one page the masks frame the picture exactly as the game laid them out.
bool CofIsPage( Panel *p )
{
	enum { COF_PAGE_MAX_KIDS = 32 };
	int	rect[COF_PAGE_MAX_KIDS][4];
	int	xs[2 * COF_PAGE_MAX_KIDS + 2], ys[2 * COF_PAGE_MAX_KIDS + 2];
	int	count = p->getChildCount();
	int	n = 0, nx = 0, ny = 0;
	const int	lw = g_cof.layout[0], lh = g_cof.layout[1];
	long long	uncovered = 0;

	if( count < 2 || count > COF_PAGE_MAX_KIDS )
		return false;

	for( int i = 0; i < count; i++ )
	{
		Panel	*c = p->getChild( i );
		int	x, y;
		const int	*size;

		if( !c )
			continue;

		if( CofIsFullScreen( c ))
			return false; // a container of containers or a backdrop

		if( !c->isVisible( ))
			continue;

		c->getPos( x, y );
		size = CofPanelAccess::size( c );

		rect[n][0] = Q_max( 0, x );
		rect[n][1] = Q_max( 0, y );
		rect[n][2] = Q_min( lw, x + size[0] );
		rect[n][3] = Q_min( lh, y + size[1] );

		if( rect[n][2] > rect[n][0] && rect[n][3] > rect[n][1] )
			n++;
	}

	if( n < 2 )
		return false;

	// coordinate compression: the cells between consecutive edges are either
	// wholly inside some child or wholly outside all of them
	xs[nx++] = 0; xs[nx++] = lw;
	ys[ny++] = 0; ys[ny++] = lh;

	for( int i = 0; i < n; i++ )
	{
		xs[nx++] = rect[i][0]; xs[nx++] = rect[i][2];
		ys[ny++] = rect[i][1]; ys[ny++] = rect[i][3];
	}

	for( int i = 1; i < nx; i++ ) // insertion sorts, a few dozen values
		for( int j = i; j > 0 && xs[j - 1] > xs[j]; j-- ) { int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
	for( int i = 1; i < ny; i++ )
		for( int j = i; j > 0 && ys[j - 1] > ys[j]; j-- ) { int t = ys[j]; ys[j] = ys[j - 1]; ys[j - 1] = t; }

	for( int i = 0; i + 1 < nx; i++ )
	{
		if( xs[i + 1] <= xs[i] )
			continue;

		for( int j = 0; j + 1 < ny; j++ )
		{
			bool covered = false;

			if( ys[j + 1] <= ys[j] )
				continue;

			for( int k = 0; k < n && !covered; k++ )
			{
				covered = rect[k][0] <= xs[i] && rect[k][2] >= xs[i + 1]
					&& rect[k][1] <= ys[j] && rect[k][3] >= ys[j + 1];
			}

			if( !covered )
				uncovered += (long long)( xs[i + 1] - xs[i] ) * ( ys[j + 1] - ys[j] );
		}
	}

	// a stray pixel row between two hand-placed pieces is still a page
	return uncovered * 200 <= (long long)lw * lh;
}

// how many parents there are between this panel and the root
int CofDepth( Panel *p )
{
	int depth = 0;

	for( Panel *q = p; CofPanelAccess::parent( q ); q = CofPanelAccess::parent( q ))
	{
		if( ++depth > 64 ) // a cycle would hang the paint traversal anyway
			break;
	}

	return depth;
}

Panel *CofAncestor( Panel *p, int depth, int want )
{
	for( int i = depth; i > want && p; i-- )
		p = CofPanelAccess::parent( p );

	return p;
}

// The panel that owns the anchor decision for p's subtree, or null for none:
// walking down from the root, the outermost panel that does NOT cover the whole
// layout screen.
//
// Measured tree, Cry of Fear at 2560x1440 (evidence/tree2-2560-best.txt):
//
//   d0 root                      2560x1440
//    d1 client viewport          2560x1440
//     d2 (overlay container)     2560x1440
//     d2 HUD container           2560x1440
//      d3 health bar group         32x160  at   32,1280   -> left + bottom
//      d3 stamina bar group        32x160  at   64,1280   -> left + bottom
//      d3 message strip          2560x64   at    0,720    -> centre + centre
//      d3 letterbox strips       2560x75   at    0,-75 / 0,1440
//     d2 inventory container     2560x1440
//      d3 backdrop               2560x1440           (full screen, no anchor)
//      d3 inventory outer frame   800x600  at  880,420   -> centre + centre
//      d3 inventory inner frame   748x480  at  906,480   -> centre + centre
//       d4 slots, icons, labels                          (inherit the centre)
//
// Every full-screen panel here is a container, and every real surface hangs off
// one. Stopping the descent any earlier - at a fixed depth, say - gives the HUD
// bars the inventory's centre anchor and walks them off the bottom left corner.
//
// The one exception is a full-screen PAGE (CofIsPage above): a full-screen
// panel whose pieces tile the whole screen between them. It owns the anchor
// for its whole subtree, so the pieces keep their places relative to each
// other and the page is scaled as one picture about the screen centre.
Panel *CofAnchorOwner( Panel *p, int depth )
{
	for( int d = 1; d <= depth; d++ )
	{
		Panel *a = CofAncestor( p, depth, d );

		if( !a )
			continue;

		if( !CofIsFullScreen( a ) || CofIsPage( a ))
			return a;
	}

	return nullptr;
}

CofAnchor CofPickAnchor( int low, int high, int layoutSize )
{
	// generous, because "centred" in a hand-placed 2012 layout means "within a
	// few pixels": the client's message strip sits 32 px below the exact centre
	int	tolerance = Q_max( 8, layoutSize / 20 ); // 5 percent
	int	diff = low - high;

	if( diff < 0 )
		diff = -diff;

	if( diff <= tolerance )
		return COF_ANCHOR_CENTER;

	return low < high ? COF_ANCHOR_LOW : COF_ANCHOR_HIGH;
}

int CofAnchorOffset( CofAnchor anchor, int layoutSize, int screenSize )
{
	switch( anchor )
	{
	case COF_ANCHOR_CENTER:
		return CofRound(( screenSize / ( 2.0f * g_cof.scale )) - layoutSize * 0.5f );
	case COF_ANCHOR_HIGH:
		return CofRound(( screenSize / g_cof.scale ) - layoutSize );
	default:
		return 0;
	}
}

} // anonymous namespace

/*
================
CofUI_Refresh

Re-read the transform from the engine. Called once per painted frame, before
the panel tree is solved, and whenever the surface is (re)started.
================
*/
void vgui::CofUI_Refresh( void )
{
	float	scale = 1.0f;
	int	lw = 0, lh = 0, sw = 0, sh = 0;

	g_cof.active = false;

	if( !g_engine || !g_engine->CofUITransform )
		return;

	if( !g_engine->CofUITransform( &scale, &lw, &lh, &sw, &sh ))
		return;

	if( scale <= 0.0f || lw <= 0 || lh <= 0 || sw <= 0 || sh <= 0 )
		return;

	g_cof.active = true;
	g_cof.scale = scale;
	g_cof.layout[0] = lw;
	g_cof.layout[1] = lh;
	g_cof.screen[0] = sw;
	g_cof.screen[1] = sh;
}

/*
================
CofUI_RootSize

How big the root panel has to be for nothing the anchor pass moved to fall
outside it, and for the cursor to stay routable across the whole screen: the
client's own layout, or the part of the screen one layout pixel covers,
whichever is larger. Returns false when the transform is inactive.
================
*/
bool vgui::CofUI_RootSize( int &wide, int &tall )
{
	if( !g_cof.active )
		return false;

	wide = Q_max( g_cof.layout[0], (int)( g_cof.screen[0] / g_cof.scale + 0.999f ));
	tall = Q_max( g_cof.layout[1], (int)( g_cof.screen[1] / g_cof.scale + 0.999f ));

	return true;
}

/*
================
CofUI_ScreenSize / CofUI_ToDeviceY

Read-only views of the transform, for the cof_hud_text_trace log: where a line
the library drew in layout pixels actually lands on the display.
================
*/
void vgui::CofUI_ScreenSize( int *wide, int *tall )
{
	if( wide ) *wide = g_cof.active ? g_cof.screen[0] : 0;
	if( tall ) *tall = g_cof.active ? g_cof.screen[1] : 0;
}

int vgui::CofUI_ToDeviceY( int layoutY )
{
	if( !g_cof.active )
		return layoutY;

	return CofRound( layoutY * g_cof.scale );
}

int vgui::CofUI_ToLayoutY( int deviceY )
{
	if( !g_cof.active || g_cof.scale <= 0.0f )
		return deviceY;

	return CofRound( deviceY / g_cof.scale );
}

/*
================
CofUI_SolvePanel

Panel::solve() hook: anchor the panel if it owns an anchor decision, then
re-derive its clip rectangle from the position it ended up at.
================
*/
void vgui::CofUI_SolvePanel( Panel *p )
{
	if( !g_cof.active || !p )
		return;

	int		depth = CofDepth( p );
	int		*origin = CofPanelAccess::screenOrigin( p );
	int		*clip = CofPanelAccess::clipRect( p );
	const int	*size = CofPanelAccess::size( p );
	Panel		*parent = CofPanelAccess::parent( p );
	if( CofAnchorOwner( p, depth ) == p )
	{
		CofAnchor ax = CofPickAnchor( origin[0],
			g_cof.layout[0] - ( origin[0] + size[0] ), g_cof.layout[0] );
		CofAnchor ay = CofPickAnchor( origin[1],
			g_cof.layout[1] - ( origin[1] + size[1] ), g_cof.layout[1] );

		origin[0] += CofAnchorOffset( ax, g_cof.layout[0], g_cof.screen[0] );
		origin[1] += CofAnchorOffset( ay, g_cof.layout[1], g_cof.screen[1] );
	}

	// a panel the client laid out to cover the whole screen must not clip a
	// child the anchor pass moved outside that old rectangle - the HUD bars
	// after a mode change to a taller screen are exactly that case
	if( CofIsFullScreen( p ) && parent )
	{
		memcpy( clip, CofPanelAccess::clipRect( parent ), sizeof( int ) * 4 );
		return;
	}

	clip[0] = origin[0];
	clip[1] = origin[1];
	clip[2] = origin[0] + size[0];
	clip[3] = origin[1] + size[1];

	if( parent )
	{
		const int *pclip = CofPanelAccess::clipRect( parent );

		clip[0] = Q_max( clip[0], pclip[0] );
		clip[1] = Q_max( clip[1], pclip[1] );
		clip[2] = Q_min( clip[2], pclip[2] );
		clip[3] = Q_min( clip[3], pclip[3] );
	}
}
