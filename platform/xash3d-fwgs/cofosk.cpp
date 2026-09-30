// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear on-screen keyboard, the support library's part
// (cof-osk-engine.patch; the engine half is engine/client/cof_osk.c, the
// keyboard itself 3rdparty/mainui/menus/CoFOsk.cpp)
//
// client.dll builds its text fields (the computer's user name and password)
// from this library's TextEntry. The engine needs to know when one of them
// takes the key focus - so a gamepad player gets the menu's keyboard - and
// needs to put the typed text into it afterwards:
//
//   TextEntry (controls/text.cpp) -> CofOsk_Hook -> vguiapi_t::CofOskEntry
//   vguiapi_t::CofOskCall (filled in here) -> CofOsk_Call:
//     op 0  the text into the focused entry (TextEntry::setText, the member
//           the game itself uses)
//     op 1  test hook: click the n-th visible entry on screen (the entry's
//           own mouse-press handler: the focus and the "clicked" report)
//     op 2  the focused entry's text; returns 2 when it hides its text
//     op 3  next field in the same form: 1 advanced, 0 last, -1 unavailable
//     op 4  active field context: (count << 8) | one-based index, or 0
//
// Only entries the hook has seen constructed are ever touched, and a stored
// pointer is only used while App::getFocus() still returns it, or after it
// has been found again in the live panel tree.

#include <string.h>
#include <stdlib.h>
#include "vgui_internal.h"
#include "support.h"
#include "controls/text.h"

using namespace vgui;

#define COF_OSK_REGISTRY 64

static struct
{
	TextEntry *entry;
	bool hidden;
} s_registry[COF_OSK_REGISTRY];			// the most recent entries built
static int s_registryNext;
static TextEntry *s_focused;			// the entry that reported the key focus last

static int CofOsk_Call( int op, char *buf, int size );

static int CofOsk_Slot( Panel *p )
{
	for( int i = 0; i < COF_OSK_REGISTRY; i++ )
	{
		if( s_registry[i].entry && (Panel *)s_registry[i].entry == p )
			return i;
	}
	return -1;
}

static void CofOsk_Hook( TextEntry *entry, int event, bool hidden )
{
	if( g_engine )
		g_engine->CofOskCall = CofOsk_Call;	// the engine may call back from now on

	if( event == 3 )
	{
		// a new entry may reuse a freed one's address: one slot per address
		int slot = CofOsk_Slot( entry );

		if( slot < 0 )
		{
			slot = s_registryNext;
			s_registryNext = ( s_registryNext + 1 ) % COF_OSK_REGISTRY;
		}
		s_registry[slot].entry = entry;
		s_registry[slot].hidden = hidden;
		return;
	}

	int slot = CofOsk_Slot( entry );

	if( slot >= 0 )
		s_registry[slot].hidden = hidden;

	if( event == 4 )
		return;

	if( event == 0 )
	{
		if( s_focused == entry )
			s_focused = nullptr;
	}
	else s_focused = entry;

	if( !g_engine || !g_engine->CofOskEntry )
		return;

	char text[256];

	text[0] = 0;
	if( event != 0 )
		entry->getText( 0, text, sizeof( text ));

	g_engine->CofOskEntry( event, hidden ? 1 : 0, text );
}

static TextEntry *CofOsk_FindVisible( Panel *panel, int &index );

static TextEntry *CofOsk_Focused( void )
{
	App *app = App::getInstance();

	if( !s_focused || !app || app->getFocus() != (Panel *)s_focused )
		return nullptr;
	// Focus can outlive a destroyed/hidden form. Never dereference the
	// remembered address until it has been rediscovered in the live tree.
	for( int i = 0; i < COF_OSK_REGISTRY; i++ )
	{
		int index = i;
		TextEntry *entry = CofOsk_FindVisible( g_rootPanel, index );
		if( !entry ) break;
		if( entry == s_focused ) return entry;
	}
	return nullptr;
}

// depth-first over the live tree; visible panels only
static TextEntry *CofOsk_FindVisible( Panel *panel, int &index )
{
	if( !panel || !panel->isVisible( ))
		return nullptr;

	if( CofOsk_Slot( panel ) >= 0 )
	{
		if( index == 0 )
			return (TextEntry *)panel;
		index--;
	}

	for( int i = 0; i < panel->getChildCount(); i++ )
	{
		TextEntry *found = CofOsk_FindVisible( panel->getChild( i ), index );

		if( found )
			return found;
	}
	return nullptr;
}

// Done visits sibling entries in reading order, never another form or a
// hidden/disabled field. Check the live tree before dereferencing focus.
static int CofOsk_NextField( void )
{
	TextEntry *current = CofOsk_Focused();
	if( !current ) return -1;
	for( Panel *p = current; p; p = p->getParent() )
		if( !p->isEnabled() ) return -1;

	Panel *parent = current->getParent();
	if( !parent ) return -1;
	int cx, cy, currentIndex = -1;
	current->getPos( cx, cy );
	for( int i = 0; i < parent->getChildCount(); i++ )
		if( parent->getChild( i ) == current ) { currentIndex = i; break; }
	if( currentIndex < 0 ) return -1;

	TextEntry *next = nullptr;
	int nx = 0, ny = 0, nextIndex = -1;
	for( int i = 0; i < parent->getChildCount(); i++ )
	{
		Panel *child = parent->getChild( i );
		if( child == current || CofOsk_Slot( child ) < 0 ||
			!child->isVisible() || !child->isEnabled() ) continue;
		int x, y;
		child->getPos( x, y );
		if( y < cy || ( y == cy && ( x < cx || ( x == cx && i <= currentIndex )))) continue;
		if( !next || y < ny || ( y == ny && ( x < nx || ( x == nx && i < nextIndex ))))
		{
			next = (TextEntry *)child;
			nx = x; ny = y; nextIndex = i;
		}
	}
	if( !next ) return 0;
	// Same internal entry-click path as the measured cof_osk_probe flow.
	// App::requestFocus is deferred until externalTick; the engine reads
	// the focused entry again after its existing two-frame delay.
	static_cast<InputSignal *>( next )->mousePressed( vgui::MOUSE_LEFT, next );
	static_cast<InputSignal *>( next )->mouseReleased( vgui::MOUSE_LEFT, next );
	return 1;
}

// Safe display metadata: order within the same visible enabled form.
// Never inspect entry text to guess whether this is a name or a password.
static int CofOsk_FieldContext( void )
{
	TextEntry *current = CofOsk_Focused();
	if( !current ) return 0;
	for( Panel *p = current; p; p = p->getParent() )
		if( !p->isEnabled() ) return 0;
	Panel *parent = current->getParent();
	if( !parent ) return 0;
	int cx, cy, currentIndex = -1;
	current->getPos( cx, cy );
	for( int i = 0; i < parent->getChildCount(); i++ )
		if( parent->getChild( i ) == current ) { currentIndex = i; break; }
	if( currentIndex < 0 ) return 0;
	int index = 1, count = 0;
	for( int i = 0; i < parent->getChildCount(); i++ )
	{
		Panel *child = parent->getChild( i );
		if( CofOsk_Slot( child ) < 0 || !child->isVisible() || !child->isEnabled() ) continue;
		int x, y;
		child->getPos( x, y );
		count++;
		if( y < cy || ( y == cy && ( x < cx || ( x == cx && i < currentIndex )))) index++;
	}
	return ( count << 8 ) | index;
}

static int CofOsk_Call( int op, char *buf, int size )
{
	TextEntry *entry;

	switch( op )
	{
	case 3:
		return CofOsk_NextField();
	case 4:
		return CofOsk_FieldContext();
	case 0:	// put buf into the focused entry
		if( !buf || !( entry = CofOsk_Focused( )))
			return 0;
		entry->setText( buf, (int)strlen( buf ));
		entry->repaint();
		return 1;
	case 1:	// test hook: click the visible entry number atoi( buf )
	{
		int index = buf ? atoi( buf ) : 0;

		if( !g_rootPanel || !( entry = CofOsk_FindVisible( g_rootPanel, index )))
			return 0;
		// the entry's own press handler, as a left click on it runs it:
		// requestFocus() and the "clicked" report
		static_cast<InputSignal *>( entry )->mousePressed( vgui::MOUSE_LEFT, entry );
		static_cast<InputSignal *>( entry )->mouseReleased( vgui::MOUSE_LEFT, entry );
		return 1;
	}
	case 2:	// the focused entry's text
	{
		if( !buf || size <= 0 || !( entry = CofOsk_Focused( )))
			return 0;
		entry->getText( 0, buf, size );

		const int slot = CofOsk_Slot( entry );
		return slot >= 0 && s_registry[slot].hidden ? 2 : 1;
	}
	}
	return 0;
}

static struct CofOskInstall
{
	CofOskInstall() { TextEntry_SetCofOskHook( CofOsk_Hook ); }
} s_cofOskInstall;
