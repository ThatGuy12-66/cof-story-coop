// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear story co-op: the 12-slot inventory grid's layout and click rules
// (cofinventory.cpp). Pure: no VGUI, no engine; the offline harness compiles it
// as it is. Coordinates are the client's own layout space, the pixels of the
// 748x480 inventory background. The retail menu rules it follows are the
// client's SlotClicked (10044670) and popup actions (10043130):
// evidence/inventory-grid-20261013/client-inventory-re.md section 3.
#ifndef COFINVENTORY_LOGIC_H
#define COFINVENTORY_LOGIC_H
#include <stdio.h>
#include <string.h>

namespace cofinv
{
const int kSlots = 12;
const int kBox = 72, kStride = 78, kLeft = 36;
const int kRowTop[3] = { 68, 146, 262 };	// BAG: slots 1..8; POCKETS: 9..12
const int kGridWide = 373, kGridTall = 480;	// the generated left-half art (tools/make_inventory12_art.py)
const int kCheckX = 31, kCheckY = 352, kCheckWide = 303, kCheckTall = 99; // the examine picture
const int kMenuX = 350;				// the item menu opens right of the grid: it never covers a box
const int kMenuButton = 42, kMenuWide = 126;
const int kArtItems = 126, kArtWeapons = 210;	// inventory_items.tga / inventory_weapons.tga, drawn 5 px up and left

struct Rect
{
	int x, y, w, h;
	bool contains( int px, int py ) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

inline Rect SlotRect( int slot )
{
	int i = slot - 1;
	Rect r = { kLeft + ( i % 4 ) * kStride, kRowTop[i / 4], kBox, kBox };
	return r;
}

// the box under a point, or 0; only the first `visible` boxes exist (3 in the story's three-slot sections)
inline int SlotAt( int px, int py, int visible )
{
	for( int s = 1; s <= visible && s <= kSlots; s++ )
		if( SlotRect( s ).contains( px, py )) return s;
	return 0;
}

struct Item { bool present, weapon; char combined[64], classname[64]; };

enum Action { kNone = 0, kEquip, kCombine, kDrop, kDualWield, kQuick1, kQuick2, kQuick3 };

struct Menu { int slot; bool weapon; int x, y; };

inline Menu PlaceMenu( int slot, bool weapon )
{
	int art = weapon ? kArtWeapons : kArtItems;
	int y = SlotRect( slot ).y;
	if( y - 5 + art > kGridTall ) y = kGridTall - art + 5; // keep the art on the background
	Menu m = { slot, weapon, kMenuX, y };
	return m;
}

inline Rect MenuArt( const Menu &m )
{
	Rect r = { m.x - 5, m.y - 5, kMenuWide, m.weapon ? kArtWeapons : kArtItems };
	return r;
}

// EQUIP (USE for items), COMBINE, DROP stacked 42 px apart; for weapons DUAL
// WIELD below them, then the quick-select 1/2/3 buttons in one row of 42x42
inline Action MenuHit( const Menu &m, int px, int py )
{
	int dx = px - m.x, dy = py - m.y;
	if( dx < 0 || dy < 0 || dx >= kMenuWide ) return kNone;
	if( dy < 3 * kMenuButton ) return (Action)( kEquip + dy / kMenuButton );
	if( !m.weapon ) return kNone;
	if( dy < 4 * kMenuButton ) return kDualWield;
	if( dy < 5 * kMenuButton && dx < 3 * kMenuButton ) return (Action)( kQuick1 + dx / kMenuButton );
	return kNone;
}

struct State
{
	int menu = 0;		// the box whose menu is open
	bool weapon = false;	// that box holds a weapon
	int combineFrom = 0;	// COMBINE was chosen here; the next box completes it
	int dualFrom = 0;	// DUAL WIELD was chosen here
};

enum { kSoundNone = 0, kSoundToggle, kSoundNo };

struct Result
{
	char command[96];	// for the server, "" for none
	int sound;		// kSound*
};

inline Result Nothing()
{
	Result r;
	r.command[0] = 0;
	r.sound = kSoundNone;
	return r;
}

inline Result ClickSlot( State &s, int slot, const Item items[kSlots + 1] )
{
	Result r = Nothing();
	if( s.dualFrom )
	{
		int from = s.dualFrom;
		s.dualFrom = 0;
		if( slot == from ) r.sound = kSoundNo;
		else if( items[slot].present && items[slot].classname[0] )
			snprintf( r.command, sizeof( r.command ), "inventorydualwield %d %d", from, slot );
		return r;
	}
	if( s.combineFrom )
	{
		int from = s.combineFrom;
		s.combineFrom = 0;
		if( slot != from && items[slot].present && items[slot].combined[0] )
			snprintf( r.command, sizeof( r.command ), "inventorycombine %d %d %s", from, slot, items[from].combined );
		else r.sound = kSoundNo;
		return r;
	}
	if( !items[slot].present )
	{
		s.menu = 0;
		return r;
	}
	s.menu = slot; // a box beside an open menu opens its own: the retail menu covered it
	s.weapon = items[slot].weapon;
	r.sound = kSoundToggle;
	return r;
}

inline Result ClickMenu( State &s, Action a )
{
	Result r = Nothing();
	int slot = s.menu;
	if( !slot || a == kNone ) return r;
	s.menu = 0;
	switch( a )
	{
	case kEquip: snprintf( r.command, sizeof( r.command ), "inventoryequip %d", slot ); break;
	case kDrop: snprintf( r.command, sizeof( r.command ), "inventorydrop %d", slot ); break;
	case kCombine: s.combineFrom = slot; break;
	case kDualWield: if( s.weapon ) s.dualFrom = slot; break;
	case kQuick1: case kQuick2: case kQuick3:
		if( s.weapon ) snprintf( r.command, sizeof( r.command ), "quickselset%d %d", (int)( a - kQuick1 ) + 1, slot );
		break;
	default: break;
	}
	return r;
}

inline void ClickOutside( State &s ) { s.menu = 0; }

// every frame: a menu or a pending combine / dual wield whose item went away (used,
// shared, dropped by the server) or that a three-slot section hides is cancelled
inline void Sync( State &s, const Item items[kSlots + 1], int visible )
{
	if( s.menu && ( s.menu > visible || !items[s.menu].present )) s.menu = 0;
	if( s.combineFrom && ( s.combineFrom > visible || !items[s.combineFrom].present )) s.combineFrom = 0;
	if( s.dualFrom && ( s.dualFrom > visible || !items[s.dualFrom].present )) s.dualFrom = 0;
}

inline void Reset( State &s ) { s = State(); }
} // namespace cofinv

#endif // COFINVENTORY_LOGIC_H
