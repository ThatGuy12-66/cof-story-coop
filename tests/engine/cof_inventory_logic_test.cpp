// The 12-slot grid's layout and click rules (cofinventory_logic.h).
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "logic_under_test.h"
using namespace cofinv;

static Item items[kSlots + 1];

static void Put( int slot, bool weapon, const char *combined, const char *classname )
{
	items[slot].present = true;
	items[slot].weapon = weapon;
	strcpy( items[slot].combined, combined );
	strcpy( items[slot].classname, classname );
}
static bool Overlap( const Rect &a, const Rect &b ) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

int main()
{
	// layout: 4x3 in the left panel (x 6..366), clear of each other, of the description box and of the menu column
	assert( SlotRect( 1 ).x == 36 && SlotRect( 4 ).x == 270 && SlotRect( 5 ).y == 146 && SlotRect( 9 ).y == 262 && SlotRect( 12 ).x == 270 );
	for( int s = 1; s <= kSlots; s++ )
	{
		Rect r = SlotRect( s );
		assert( r.w == kBox && r.h == kBox && r.x >= 8 && r.x + r.w <= 366 && r.y + r.h <= kCheckY );
		assert( r.x + r.w <= kMenuX - 5 );
		for( int t = s + 1; t <= kSlots; t++ ) assert( !Overlap( r, SlotRect( t )));
	}
	assert( SlotAt( 40, 70, 12 ) == 1 && SlotAt( 300, 300, 12 ) == 12 && SlotAt( 300, 300, 3 ) == 0 );
	assert( SlotAt( 110, 100, 12 ) == 0 ); // the gap between boxes 1 and 2

	// the menu: beside the grid, never over a box, the art on the background
	for( int s = 1; s <= kSlots; s++ )
		for( int w = 0; w < 2; w++ )
		{
			Menu m = PlaceMenu( s, w != 0 );
			Rect a = MenuArt( m );
			assert( a.y >= 0 && a.y + a.h <= kGridTall && a.x + a.w <= 748 );
			for( int t = 1; t <= kSlots; t++ ) assert( !Overlap( a, SlotRect( t )));
		}
	Menu m = PlaceMenu( 6, true );
	assert( MenuHit( m, m.x + 10, m.y + 10 ) == kEquip && MenuHit( m, m.x + 10, m.y + 50 ) == kCombine );
	assert( MenuHit( m, m.x + 10, m.y + 90 ) == kDrop && MenuHit( m, m.x + 10, m.y + 130 ) == kDualWield );
	assert( MenuHit( m, m.x + 10, m.y + 180 ) == kQuick1 && MenuHit( m, m.x + 100, m.y + 180 ) == kQuick3 );
	assert( MenuHit( m, m.x + 130, m.y + 180 ) == kNone && MenuHit( m, m.x - 1, m.y ) == kNone );
	Menu item = PlaceMenu( 3, false );
	assert( MenuHit( item, item.x + 10, item.y + 130 ) == kNone ); // items have no DUAL WIELD or quick row

	// the slot-6 bug: with slot 5's menu open, a click on slot 6 selects slot 6
	Put( 5, false, "", "item_key" );
	Put( 6, true, "glockwithtaclight", "weapon_glock" );
	State s;
	Result r = ClickSlot( s, 5, items );
	assert( s.menu == 5 && r.sound == kSoundToggle && !r.command[0] );
	r = ClickSlot( s, 6, items );
	assert( s.menu == 6 && s.weapon && r.sound == kSoundToggle );
	r = ClickMenu( s, kEquip );
	assert( !strcmp( r.command, "inventoryequip 6" ) && s.menu == 0 );

	// slots 7..12 work like the rest
	Put( 11, false, "", "item_key" );
	ClickSlot( s, 11, items );
	r = ClickMenu( s, kDrop );
	assert( !strcmp( r.command, "inventorydrop 11" ));
	Put( 9, true, "", "weapon_shotgun" );
	ClickSlot( s, 9, items );
	r = ClickMenu( s, kQuick2 );
	assert( !strcmp( r.command, "quickselset2 9" ));
	ClickSlot( s, 5, items ); // an item: no quick select
	r = ClickMenu( s, kQuick1 );
	assert( !r.command[0] && s.menu == 0 );

	// combine across the 6/7 line: the target needs a result, the source's is sent
	Put( 7, false, "glockwithtaclight", "item_glocktaclight" );
	ClickSlot( s, 6, items );
	r = ClickMenu( s, kCombine );
	assert( s.combineFrom == 6 && s.menu == 0 && !r.command[0] );
	r = ClickSlot( s, 7, items );
	assert( !strcmp( r.command, "inventorycombine 6 7 glockwithtaclight" ) && !s.combineFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	r = ClickSlot( s, 6, items ); // onto itself
	assert( r.sound == kSoundNo && !r.command[0] && !s.combineFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	r = ClickSlot( s, 5, items ); // no combined_result
	assert( r.sound == kSoundNo && !r.command[0] );

	// dual wield (the server refuses it in multiplayer; the screen still asks, as the retail one)
	ClickSlot( s, 6, items );
	ClickMenu( s, kDualWield );
	r = ClickSlot( s, 9, items );
	assert( !strcmp( r.command, "inventorydualwield 6 9" ) && !s.dualFrom );
	ClickSlot( s, 6, items ); ClickMenu( s, kDualWield );
	r = ClickSlot( s, 6, items );
	assert( r.sound == kSoundNo && !r.command[0] );

	// an empty box closes the menu without a sound
	ClickSlot( s, 6, items );
	r = ClickSlot( s, 10, items );
	assert( s.menu == 0 && r.sound == kSoundNone && !r.command[0] );

	// Sync: the item under an open menu or a pending combine goes away, or a three-slot section hides it
	ClickSlot( s, 11, items );
	items[11].present = false;
	Sync( s, items, kSlots );
	assert( s.menu == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	Sync( s, items, 3 );
	assert( s.combineFrom == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kDualWield );
	items[6].present = false;
	Sync( s, items, kSlots );
	assert( s.dualFrom == 0 );
	items[6].present = true;

	// a click outside closes the menu; Reset clears everything
	ClickSlot( s, 6, items );
	ClickOutside( s );
	assert( s.menu == 0 );
	ClickSlot( s, 6, items ); ClickMenu( s, kCombine );
	Reset( s );
	assert( !s.menu && !s.combineFrom && !s.dualFrom );
	puts( "inventory grid logic passed" );
	return 0;
}
