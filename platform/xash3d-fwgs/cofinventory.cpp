// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear story co-op: the 12-slot inventory grid inside the client's inventory
// (docs/superpowers/specs/2026-10-14-inventory-12-slots-design.md, section 2.2).
//
// On a map the server runs at 12 slots (vguiapi_t::CofInventory, COF_INV_OP_GRID)
// the left half of CRECBInventory - its six slot buttons and pictures, the hover
// picture and the item menu - is hidden every frame, and two panels of ours are
// added last to its 748x480 background, so they are painted and hit-tested above
// every client child: the grid (the generated inventory12_left art, 12 boxes, the
// examine picture) and the item menu beside it. The right half (QUICK SELECT,
// NOTES, CURRENT OBJECTIVE) stays the client's. The client's children are found at
// their offsets in the 1.6 client.dll's CRECBInventory and checked by class (RTTI)
// first; anything unexpected leaves the retail screen alone. Commands, sounds and
// images go through the engine. Layout and click rules: cofinventory_logic.h.
#include "support.h"
#include "cofinventory_logic.h"
#include "app.h"
#include "panel.h"
#include "signals.h"
#include "surface.h"
#include "controls/image.h"
#include <string.h>

using namespace vgui;

namespace vgui
{
bool CofMemReadable( const void *ptr, size_t size ); // cofmem.cpp
}

namespace
{
int Engine( int op, int arg, void *buf, int size )
{
	return g_engine && g_engine->CofInventory ? g_engine->CofInventory( op, arg, buf, size ) : 0;
}

void Send( const char *command )
{
	if( command[0] )
		Engine( COF_INV_OP_COMMAND, 0, (void *)command, (int)strlen( command ) + 1 );
}

void Sound( int which )
{
	const char *path = which == cofinv::kSoundToggle ? "inventory/inventory_toggle.wav" :
		which == cofinv::kSoundNo ? "inventory/inventory_no.wav" : nullptr;
	if( path )
		Engine( COF_INV_OP_SOUND, 0, (void *)path, (int)strlen( path ) + 1 );
}

// what the grid shows this frame
struct Model
{
	int visible = cofinv::kSlots;		// 12, or 3 in the story's three-slot sections
	cofinv::Item items[cofinv::kSlots + 1] = {};
	cof_vgui_inv_item_t detail[cofinv::kSlots + 1] = {};
	cofinv::State state;
	int hover = 0;				// the box under the cursor
	int blink = 0;				// painted frames, for the combine / dual wield source
	bool fullArt = false;			// the background shows the whole generated art this frame
};
Model g_model;

void Apply( const cofinv::Result &r )
{
	Send( r.command );
	Sound( r.sound );
}

// One VGUI texture per image path, made the first time it is drawn and then
// kept: the engine keeps VGUI textures until DrawShutdown.
struct Texture { char path[128]; int id; };
Texture g_textures[128];
int g_textureCount;

class Base : public Panel
{
public:
	Base() : Panel( 0, 0, 1, 1 ) { setPaintBackgroundEnabled( false ); }

	// the texture of a picture, made the first time (0: none)
	int load( const char *path ) { return texture( path ); }

	// the cursor in this panel's coordinates; false when it is outside
	bool cursor( int &x, int &y )
	{
		int w, h;
		App::getInstance()->getCursorPos( x, y );
		screenToLocal( x, y );
		getSize( w, h );
		return x >= 0 && y >= 0 && x < w && y < h;
	}

protected:
	int texture( const char *path )
	{
		if( !path || !path[0] ) return 0;
		for( int i = 0; i < g_textureCount; i++ )
			if( !strcmp( g_textures[i].path, path )) return g_textures[i].id;
		if( g_textureCount == (int)( sizeof( g_textures ) / sizeof( g_textures[0] ))) return 0;
		Texture &t = g_textures[g_textureCount++];
		strncpy( t.path, path, sizeof( t.path ) - 1 );
		t.path[sizeof( t.path ) - 1] = 0;
		t.id = 0; // tried once: a missing image is not asked for again
		cof_vgui_image_t image;
		memset( &image, 0, sizeof( image ));
		image.path = t.path;
		if( Engine( COF_INV_OP_IMAGE, 0, &image, sizeof( image )) != 1 || image.wide <= 0 || image.tall <= 0 ) return 0;
		image.size = image.wide * image.tall * 4;
		image.rgba = new unsigned char[image.size];
		if( Engine( COF_INV_OP_IMAGE, 0, &image, sizeof( image )) == 1 )
		{
			t.id = getSurfaceBase()->createNewTextureID();
			drawSetTextureRGBA( t.id, (const char *)image.rgba, image.wide, image.tall );
		}
		delete[] image.rgba;
		return t.id;
	}

	bool image( const char *path, int x, int y, int w, int h )
	{
		int id = texture( path );
		if( !id ) return false;
		drawSetTexture( id );
		drawSetColor( 255, 255, 255, 0 ); // VGUI alpha is inverted: 0 is opaque
		drawTexturedRect( x, y, x + w, y + h );
		return true;
	}

	void outline( const cofinv::Rect &r, int red, int green, int blue, int alpha )
	{
		drawSetColor( red, green, blue, alpha );
		drawFilledRect( r.x, r.y, r.x + r.w, r.y + 1 );
		drawFilledRect( r.x, r.y + r.h - 1, r.x + r.w, r.y + r.h );
		drawFilledRect( r.x, r.y, r.x + 1, r.y + r.h );
		drawFilledRect( r.x + r.w - 1, r.y, r.x + r.w, r.y + r.h );
	}
};

class Grid : public Base
{
public:
	void paint() override
	{
		// the background already shows the whole art (FullArt); else the left half over the retail picture
		const char *art = g_model.visible == 3 ? "gfx/vgui/inventory12_left_three.tga" : "gfx/vgui/inventory12_left.tga";
		if( !g_model.fullArt && !image( art, 0, 0, cofinv::kGridWide, cofinv::kGridTall ))
		{
			// the art was not generated (tools/make_inventory12_art.py): a plain panel and plain boxes
			drawSetColor( 8, 8, 8, 30 );
			drawFilledRect( 8, 64, 366, 466 );
			for( int s = 1; s <= g_model.visible; s++ ) outline( cofinv::SlotRect( s ), 100, 100, 100, 0 );
		}
		for( int s = 1; s <= g_model.visible; s++ )
		{
			cofinv::Rect r = cofinv::SlotRect( s );
			if( g_model.items[s].present ) image( g_model.detail[s].picture, r.x, r.y, r.w, r.h );
			if( s == g_model.state.combineFrom || s == g_model.state.dualFrom )
				outline( r, 200, 30, 30, ( g_model.blink / 15 ) % 2 ? 140 : 0 ); // blinks, as the retail highlight
			else if( s == g_model.state.menu || ( s == g_model.hover && g_model.items[s].present ))
				outline( r, 255, 255, 255, 120 );
		}
		int shown = g_model.hover ? g_model.hover : g_model.state.menu;
		if( shown && g_model.items[shown].present )
			image( g_model.detail[shown].check, cofinv::kCheckX, cofinv::kCheckY, cofinv::kCheckWide, cofinv::kCheckTall );
	}
};

class GridInput : public InputSignalAdapter
{
public:
	explicit GridInput( Grid *grid ) : m_grid( grid ) {}
	void cursorMoved( int, int, Panel * ) override
	{
		int x, y;
		g_model.hover = m_grid->cursor( x, y ) ? cofinv::SlotAt( x, y, g_model.visible ) : 0;
	}
	void cursorExited( Panel * ) override { g_model.hover = 0; }
	void mousePressed( MouseCode code, Panel * ) override
	{
		int x, y;
		if( code != vgui::MOUSE_LEFT || !m_grid->cursor( x, y )) return;
		int slot = cofinv::SlotAt( x, y, g_model.visible );
		if( slot ) Apply( cofinv::ClickSlot( g_model.state, slot, g_model.items ));
		else cofinv::ClickOutside( g_model.state );
	}
	// SDL reports a quick second click as a double press only
	void mouseDoublePressed( MouseCode code, Panel *panel ) override { mousePressed( code, panel ); }
private:
	Grid *m_grid;
};

class ItemMenu : public Base
{
public:
	cofinv::Menu menu = {};
	void paint() override
	{
		int w, h;
		getSize( w, h );
		image( menu.weapon ? "gfx/vgui/inventory_weapons.tga" : "gfx/vgui/inventory_items.tga", 0, 0, w, h );
	}
};

class MenuInput : public InputSignalAdapter
{
public:
	explicit MenuInput( ItemMenu *menu ) : m_menu( menu ) {}
	void mousePressed( MouseCode code, Panel * ) override
	{
		int x, y;
		if( code != vgui::MOUSE_LEFT || !m_menu->cursor( x, y )) return;
		cofinv::Rect art = cofinv::MenuArt( m_menu->menu );
		Apply( cofinv::ClickMenu( g_model.state, cofinv::MenuHit( m_menu->menu, x + art.x, y + art.y )));
	}
	void mouseDoublePressed( MouseCode code, Panel *panel ) override { mousePressed( code, panel ); }
private:
	ItemMenu *m_menu;
};

// The client's CRECBInventory children, at their offsets in the 1.6 client.dll.
struct Child { int offset; const char *rtti; };
const Child kBackground = { 0x150, ".?AVImagePanel@vgui@@" };
const Child kHide[] =
{
	{ 0x15C, ".?AVColorButton@@" }, { 0x160, ".?AVColorButton@@" }, { 0x164, ".?AVColorButton@@" },	// slot buttons 1..6
	{ 0x168, ".?AVColorButton@@" }, { 0x16C, ".?AVColorButton@@" }, { 0x170, ".?AVColorButton@@" },
	{ 0x120, ".?AVCImageLabel@@" }, { 0x124, ".?AVCImageLabel@@" }, { 0x128, ".?AVCImageLabel@@" },	// slot pictures 1..6
	{ 0x12C, ".?AVCImageLabel@@" }, { 0x130, ".?AVCImageLabel@@" }, { 0x134, ".?AVCImageLabel@@" },
	{ 0x140, ".?AVCImageLabel@@" },	// the hover (examine) picture
	{ 0x0DC, ".?AVCImageLabel@@" },	// the item menu's art
	{ 0x100, ".?AVColorButton@@" }, { 0x104, ".?AVColorButton@@" }, { 0x108, ".?AVColorButton@@" },	// its seven buttons
	{ 0x10C, ".?AVColorButton@@" }, { 0x110, ".?AVColorButton@@" }, { 0x114, ".?AVColorButton@@" },
	{ 0x118, ".?AVColorButton@@" },
	{ 0x0F8, ".?AVPanel@vgui@@" },	// the combine / dual wield highlight
};
const int kHideCount = (int)( sizeof( kHide ) / sizeof( kHide[0] ));

// The background's picture while the grid shows: the whole generated art
// (inventory12_full*.tga, our left half on the retail right half). The client draws
// the inventory see-through, so with the grid's art painted over the retail picture
// the retail left half (its POCKETS label) showed through (live 2026-10-14); one
// picture leaves nothing under the grid. Without the generated file it paints the
// retail picture, and the grid paints its left half over it as before.
class FullArt : public Image
{
public:
	Image *retail = nullptr;	// the client's picture, put back on a six-slot map
	Base *loader = nullptr;		// makes the texture (a panel of ours)
	void paint( Panel *panel ) override
	{
		int id = loader ? loader->load( g_model.visible == 3 ? "gfx/vgui/inventory12_full_three.tga" : "gfx/vgui/inventory12_full.tga" ) : 0;
		g_model.fullArt = id != 0;
		if( !id )
		{
			if( retail ) retail->doPaint( panel );
			return;
		}
		int x = 0, y = 0, w, h, r, g, b, a;
		Color c;
		if( retail ) { retail->getPos( x, y ); retail->getColor( c ); } // as the retail Bitmap paints
		else c = Color( 255, 255, 255, 0 );
		c.getColor( r, g, b, a );
		getSize( w, h );
		drawSetTexture( id );
		drawSetColor( r, g, b, a );
		drawTexturedRect( x, y, x + w, y + h );
	}
	void fit( int w, int h ) { setSize( w, h ); }
};
FullArt g_fullArt;

struct Ours
{
	Panel *inventory = nullptr;	// the CRECBInventory this was built for
	ImagePanel *background = nullptr;
	Panel *hide[kHideCount] = {};
	bool was[kHideCount] = {};	// their visibility before the grid hid them
	bool hidden = false;
	Grid *grid = nullptr;
	ItemMenu *menu = nullptr;
};
Ours g_ours;

Panel *ChildAt( Panel *inventory, const Child &c )
{
	char name[128];
	const char *field = (const char *)inventory + c.offset;
	if( !CofMemReadable( field, sizeof( Panel * ))) return nullptr;
	Panel *child = *(Panel * const *)field;
	if( !child || !CofMemReadable( child, sizeof( void * ))) return nullptr;
	return !strcmp( CofPanels_ClassName( child, name, sizeof( name )), c.rtti ) ? child : nullptr;
}

// the client's inventory panel below its viewport (root -> viewport -> panel), shown or not
Panel *FindInventory( Panel *root )
{
	if( !root ) return nullptr;
	for( int i = 0; i < root->getChildCount(); i++ )
	{
		Panel *viewport = root->getChild( i );
		if( !viewport ) continue;
		for( int j = 0; j < viewport->getChildCount(); j++ )
		{
			Panel *p = viewport->getChild( j );
			if( p && CofPanels_Class( p ) == COF_PANEL_INVENTORY ) return p;
		}
	}
	return nullptr;
}

bool Bind( Panel *inventory )
{
	Panel *hide[kHideCount];
	if( !CofMemReadable( inventory, 0x1C4 )) return false;
	Panel *background = ChildAt( inventory, kBackground );
	if( !background ) return false;
	for( int i = 0; i < kHideCount; i++ )
		if( !( hide[i] = ChildAt( inventory, kHide[i] ))) return false;
	memcpy( g_ours.hide, hide, sizeof( hide ));
	g_ours.background = static_cast<ImagePanel *>( background ); // its class checked above
	g_ours.grid = new Grid();
	g_ours.grid->setParent( background );	// added last: above every client child
	g_ours.grid->setBounds( 0, 0, cofinv::kGridWide, cofinv::kGridTall );
	g_ours.grid->addInputSignal( new GridInput( g_ours.grid ));
	g_ours.grid->setVisible( false );
	g_ours.menu = new ItemMenu();
	g_ours.menu->setParent( background );
	g_ours.menu->addInputSignal( new MenuInput( g_ours.menu ));
	g_ours.menu->setVisible( false );
	return true;
}

// The grid still hangs under this inventory's background: it is the grid's parent,
// and the grid is among its children (a background built again at the same address
// starts with a list of its own).
bool StillBound( Panel *inventory )
{
	Panel *background = ChildAt( inventory, kBackground );
	if( !background || background != CofPanelAccess::parent( g_ours.grid )) return false;
	for( int i = 0; i < background->getChildCount(); i++ )
		if( background->getChild( i ) == g_ours.grid ) return true;
	return false;
}

void HideRetail()
{
	for( int i = 0; i < kHideCount; i++ )
	{
		if( !g_ours.hidden ) g_ours.was[i] = g_ours.hide[i]->isVisible();
		g_ours.hide[i]->setVisible( false ); // every frame: Inventory messages re-show slot buttons 4..6
	}
	g_ours.hidden = true;
}

// The whole art in place of the background's picture (on), or the client's picture
// back (off). Checked every frame: the client may set another picture (the three-slot
// one), which is then the one to put back.
void ShowFullArt( bool on )
{
	ImagePanel *background = g_ours.background;
	Image *now = background ? background->getImage() : nullptr;
	if( !background ) return;
	if( on && now != &g_fullArt )
	{
		int w, h;
		if( now ) now->getSize( w, h );
		else background->getSize( w, h );
		g_fullArt.retail = now;
		g_fullArt.loader = g_ours.grid;
		g_fullArt.fit( w, h );
		background->setImage( &g_fullArt );
	}
	else if( !on && now == &g_fullArt )
	{
		background->setImage( g_fullArt.retail );
		g_model.fullArt = false;
	}
}

void RestoreRetail()
{
	ShowFullArt( false );
	if( !g_ours.hidden ) return;
	for( int i = 0; i < kHideCount; i++ ) g_ours.hide[i]->setVisible( g_ours.was[i] );
	g_ours.hidden = false;
}

bool g_wasDown; // the left button as of the last frame, for the outside-press edge

void Close()
{
	g_wasDown = false; // a press held across closing must not count later
	cofinv::Reset( g_model.state );
	g_model.hover = 0;
	g_ours.grid->setVisible( false );
	g_ours.menu->setVisible( false );
}
} // namespace

void vgui::CofInventory_Frame( Panel *root )
{
	Panel *inventory = FindInventory( root );
	// a new viewport (VidInit) builds a new inventory panel; ours went with the old one.
	// A new panel can also come back at the old address (freed and allocated again), and
	// then the grid no longer hangs under its background: build ours again then too.
	if( inventory != g_ours.inventory || ( inventory && g_ours.grid && !StillBound( inventory )))
	{
		if( g_ours.grid )
		{
			// ours, never freed by the client: off the screen wherever they still hang
			g_ours.grid->setVisible( false );
			g_ours.menu->setVisible( false );
		}
		g_ours = Ours();
		g_ours.inventory = inventory;
		if( inventory && !Bind( inventory ) && g_engine && g_engine->CofPrint )
			g_engine->CofPrint( "[cof-inv12] the client's inventory panel is not the expected one: the retail screen stays\n" );
	}
	if( !g_ours.grid ) return;

	if( Engine( COF_INV_OP_GRID, 0, nullptr, 0 ) != cofinv::kSlots )
	{
		Close();
		RestoreRetail(); // a map at six slots: the client's own screen, as it was
		return;
	}
	if( !inventory->isVisible())
	{
		Close();
		return;
	}
	HideRetail();
	ShowFullArt( true );

	g_model.visible = Engine( COF_INV_OP_THREE, 0, nullptr, 0 ) ? 3 : cofinv::kSlots;
	for( int s = 1; s <= cofinv::kSlots; s++ )
	{
		cof_vgui_inv_item_t &d = g_model.detail[s];
		cofinv::Item &it = g_model.items[s];
		it.present = Engine( COF_INV_OP_SLOT, s, &d, sizeof( d )) == 1;
		it.weapon = it.present && d.weapon;
		strncpy( it.combined, d.combined, sizeof( it.combined ) - 1 );
		it.combined[sizeof( it.combined ) - 1] = 0;
		strncpy( it.classname, d.classname, sizeof( it.classname ) - 1 );
		it.classname[sizeof( it.classname ) - 1] = 0;
	}
	cofinv::Sync( g_model.state, g_model.items, g_model.visible );

	// a press anywhere but the grid or the menu closes the menu, as the client's own does.
	// Edge of the held state: the per-tick pressed flag is already cleared by the time
	// this runs (App::externalTick comes first in XashPaint).
	bool down = App::getInstance()->isMouseDown( vgui::MOUSE_LEFT, nullptr );
	if( down && !g_wasDown )
	{
		int x, y;
		if( !g_ours.grid->cursor( x, y ) && !( g_ours.menu->isVisible() && g_ours.menu->cursor( x, y )))
			cofinv::ClickOutside( g_model.state );
	}
	g_wasDown = down;

	g_ours.grid->setVisible( true );
	if( g_model.state.menu )
	{
		cofinv::Menu m = cofinv::PlaceMenu( g_model.state.menu, g_model.state.weapon );
		cofinv::Rect a = cofinv::MenuArt( m );
		g_ours.menu->menu = m;
		g_ours.menu->setBounds( a.x, a.y, a.w, a.h );
		g_ours.menu->setVisible( true );
	}
	else g_ours.menu->setVisible( false );
	// added or moved after this frame's solve (App::internalTick): solve before painting
	g_ours.grid->solveTraverse();
	g_ours.menu->solveTraverse();
	g_model.blink++;
}
