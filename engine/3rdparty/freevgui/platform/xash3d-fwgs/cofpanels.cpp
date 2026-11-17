// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear panel identification (cof_panel_pause)

#include <string.h>
#include "vgui_internal.h"
#include "support.h"

using namespace vgui;

/*
Which of the client's in-play panels is on screen, told apart by class.

Every Cry of Fear panel is a VGUI1 panel the client (client.dll, built with
MSVC and run-time type information) creates below its viewport:

	d0 root (this library's)
	 d1 client viewport          TeamFortressViewport
	  d2 inventory               CRECBInventory
	  d2 HUD container           CHUDControl
	  d2 tape recorder page      CSaveLoad
	  d2 ...                     one per panel, 57 in all

A panel's class is read from the complete object locator MSVC places in front
of every vtable (vtable[-1] -> signature 0, offset, cdOffset, type descriptor;
the type descriptor holds the decorated name ".?AVCRECBInventory@@"). Nothing
here depends on an address inside client.dll, and every pointer is checked for
readability before it is followed, so an object without type information (or a
future client) simply is not recognised. Results are cached per vtable.

The walk runs once per painted frame, before the tree is painted, and hands
the engine one COF_PANEL_* bit per visible recognised class
(vguiapi_t::CofPanelReport). A panel counts only when it and every panel above
it are visible; the walk stops at depth 3.
*/

namespace vgui
{
bool CofMemReadable( const void *ptr, size_t size ); // cofmem.cpp
}

namespace
{

struct CofClassName
{
	const char	*decorated;
	unsigned int	bit;
};

const CofClassName g_cofClasses[] =
{
	{ ".?AVCRECBInventory@@",     COF_PANEL_INVENTORY },
	{ ".?AVCDocuments@@",         COF_PANEL_DOCUMENTS },
	{ ".?AVCNoteDocument@@",      COF_PANEL_NOTE },
	{ ".?AVCUserDocument@@",      COF_PANEL_USERDOC },
	{ ".?AVCVGUIBillboard@@",     COF_PANEL_BILLBOARD },
	{ ".?AVCYesno@@",             COF_PANEL_YESNO },
	{ ".?AVCSaveLoad@@",          COF_PANEL_SAVELOAD },
	{ ".?AVCSave@@",              COF_PANEL_SAVECONFIRM },
	{ ".?AVCPhone@@",             COF_PANEL_PHONE },
	{ ".?AVCTelephone@@",         COF_PANEL_TELEPHONE },
	{ ".?AVCComputer@@",          COF_PANEL_COMPUTER },
	{ ".?AVCPadlock@@",           COF_PANEL_PADLOCK },
	{ ".?AVCPuzzleBar@@",         COF_PANEL_PUZZLEBAR },
	{ ".?AVCKeypad@@",            COF_PANEL_KEYPAD },
	{ ".?AVCAnimalStatue@@",      COF_PANEL_STATUE },
	{ ".?AVCParanoiaTextPanel@@", COF_PANEL_TEXTWINDOW },
	// the remaining modal panels of TeamFortressViewport::UpdateCursorState:
	// menu-map pages the engine menu replaced, the old game over page, the
	// gallery - never paused, but they must veto a pause if they are up
	{ ".?AVCDifficulty@@",        COF_PANEL_OTHER },
	{ ".?AVCGameMenu@@",          COF_PANEL_OTHER },
	{ ".?AVCGameOver@@",          COF_PANEL_OTHER },
	{ ".?AVCLanguage@@",          COF_PANEL_OTHER },
	{ ".?AVCClothesMenu@@",       COF_PANEL_OTHER },
	{ ".?AVCUnlockables@@",       COF_PANEL_OTHER },
	{ ".?AVCBookMenu@@",          COF_PANEL_OTHER },
	{ ".?AVCVGUIExtraMenu@@",     COF_PANEL_OTHER },
	{ ".?AVCCommandMenu@@",       COF_PANEL_OTHER },
	{ ".?AVCHUDControl@@",        COF_PANEL_HUD },
	// this library's own plain panel, which the client uses for its backdrops
	{ ".?AVPanel@vgui@@",         COF_CLASS_PLAIN_PANEL },
};

struct CofVtableEntry
{
	const void	*vtable;
	unsigned int	bits;
	const char	*name;	// decorated name in the owning module, for the trace
};

enum { COF_VTABLE_CACHE = 256 };

CofVtableEntry	g_cofVtables[COF_VTABLE_CACHE];
int		g_cofVtableCount = 0;

struct CofPanelFrame
{
	unsigned int	visible;
	int		trace;
};

CofPanelFrame	g_cofFrame = { 0, 0 };

// copy a NUL-terminated string that lives in another module, checking every
// page it touches; false when it is unreadable or does not end in time
bool CofCopyName( const char *src, char *dst, size_t size )
{
	for( size_t i = 0; i < size; i++ )
	{
		if( i == 0 || (((size_t)( src + i )) & 0xFFF ) == 0 )
		{
			if( !CofMemReadable( src + i, 1 ))
				return false;
		}

		dst[i] = src[i];

		if( !dst[i] )
			return true;
	}

	return false;
}

// the decorated class name of a polymorphic object built by an MSVC module
// with run-time type information, or null
const char *CofRttiName( const void *object, char *buf, size_t size )
{
#if defined( _MSC_VER ) && defined( _M_IX86 )
	const void * const	*vtable;
	const unsigned int	*locator;
	const char		*descriptor;

	if( !CofMemReadable( object, sizeof( void * )))
		return nullptr;

	vtable = *(const void * const * const *)object;

	if( !CofMemReadable( vtable - 1, sizeof( void * )))
		return nullptr;

	locator = (const unsigned int *)vtable[-1];

	// signature, offset, cdOffset, pTypeDescriptor, pClassDescriptor
	if( !CofMemReadable( locator, 5 * sizeof( unsigned int )) || locator[0] != 0 )
		return nullptr;

	descriptor = (const char *)(size_t)locator[3];

	// pVFTable, spare, name[]
	if( !CofMemReadable( descriptor, 2 * sizeof( void * )))
		return nullptr;

	if( !CofCopyName( descriptor + 2 * sizeof( void * ), buf, size ))
		return nullptr;

	if( strncmp( buf, ".?AV", 4 ) && strncmp( buf, ".?AU", 4 ))
		return nullptr;

	return buf;
#else
	(void)object;
	(void)buf;
	(void)size;
	return nullptr;
#endif
}

const CofVtableEntry *CofLookup( Panel *panel )
{
	static CofVtableEntry	unknown = { nullptr, 0, nullptr };
	const void	*vtable;
	char		name[128];
	const char	*rtti;
	CofVtableEntry	entry;

	if( !panel )
		return &unknown;

	vtable = *(const void * const *)panel;

	for( int i = 0; i < g_cofVtableCount; i++ )
	{
		if( g_cofVtables[i].vtable == vtable )
			return &g_cofVtables[i];
	}

	entry.vtable = vtable;
	entry.bits = 0;
	entry.name = nullptr;

	rtti = CofRttiName( panel, name, sizeof( name ));

	if( rtti )
	{
		for( size_t i = 0; i < sizeof( g_cofClasses ) / sizeof( g_cofClasses[0] ); i++ )
		{
			if( !strcmp( rtti, g_cofClasses[i].decorated ))
			{
				entry.bits = g_cofClasses[i].bit;
				entry.name = g_cofClasses[i].decorated;
				break;
			}
		}

		if( g_cofFrame.trace >= 2 && g_engine && g_engine->CofPrint )
		{
			char line[192];

			snprintf( line, sizeof( line ), "[cof-panel] class %s vtable %p%s\n",
				rtti, vtable, entry.bits ? "" : " (not a Cry of Fear panel of interest)" );
			g_engine->CofPrint( line );
		}
	}

	if( g_cofVtableCount >= COF_VTABLE_CACHE )
	{
		static CofVtableEntry scratch;

		scratch = entry;
		return &scratch;
	}

	g_cofVtables[g_cofVtableCount] = entry;
	return &g_cofVtables[g_cofVtableCount++];
}

void CofWalk( Panel *panel, int depth )
{
	int count = panel->getChildCount();

	for( int i = 0; i < count; i++ )
	{
		Panel *child = panel->getChild( i );

		if( !child || !child->isVisible( ))
			continue;

		g_cofFrame.visible |= CofLookup( child )->bits & ~COF_CLASS_PLAIN_PANEL;

		if( depth < 3 )
			CofWalk( child, depth + 1 );
	}
}

} // anonymous namespace

/*
================
CofPanels_Class

The COF_PANEL_* bit of a panel's class (COF_CLASS_PLAIN_PANEL for this
library's own vgui::Panel), 0 when it is none of them.
================
*/
unsigned int vgui::CofPanels_Class( Panel *panel )
{
	return CofLookup( panel )->bits;
}

/*
================
CofPanels_ClassName

The decorated class name of any panel, for developer traces only (not cached).
================
*/
const char *vgui::CofPanels_ClassName( Panel *panel, char *buf, size_t size )
{
	const char *name = panel ? CofRttiName( panel, buf, size ) : nullptr;

	return name ? name : "?";
}

/*
================
CofPanels_Visible

The bits the last CofPanels_Frame found on screen.
================
*/
unsigned int vgui::CofPanels_Visible( void )
{
	return g_cofFrame.visible;
}

/*
================
CofPanels_Frame

Once per painted frame, before the tree is painted: find the visible
recognised panels and tell the engine.
================
*/
void vgui::CofPanels_Frame( Panel *root )
{
	g_cofFrame.visible = 0;

	if( root && root->isVisible( ))
		CofWalk( root, 1 );

	if( g_engine && g_engine->CofPanelReport )
	{
		int trace = g_engine->CofPanelReport( g_cofFrame.visible );

		// the class names are printed on a cache miss: forget the cache when
		// the level-2 trace is switched on, so every class on screen is named
		if( trace >= 2 && g_cofFrame.trace < 2 )
			g_cofVtableCount = 0;

		g_cofFrame.trace = trace;
	}
}

/*
================================================================================
Cry of Fear gamepad (cof-gamepad-input): the open panel's own buttons

Every button of the client's in-play panels is a vgui::Button (this library's
class; the client derives ColorButton, CommandButton... from it) with one
ActionSignal of the panel's own handler class, C<Panel>Handler_Command
{ vtable, panel, command }, whose actionPerformed calls the panel's
ActionSignal( command ) switch. What each command does was read from the
shipped client.dll (stage1/gamepad2-20260923/tools/switches.txt); the table
below names the command that hides the panel the way its own Back / Close /
Cancel / No button does. The button is found by its handler, not by its
position or picture, and fired through Button::doClick, which runs the same
ActionSignals a mouse click runs (MomentaryButtonController::mouseReleased ->
fireActionSignal).

The tape recorder page (CSaveLoad) acts on a slot only on a double click (its
handler compares the client clock with a per-slot time stamp, 0.5 s, client
100316BD): a gamepad A on a slot is followed by the second click here, so pick
slot -> A -> saved / loaded. The mouse path is untouched.
================================================================================
*/

#include "controls/button.h"

namespace
{

struct CofButtonAccess : public Button
{
	static Dar<ActionSignal*> &signals( Button *b ) { return static_cast<CofButtonAccess *>( b )->actionSignals; }
	static const char *label( Button *b ) { return static_cast<CofButtonAccess *>( b )->text; }
};

// true when an MSVC-built polymorphic object has `base` among its classes
// (the class hierarchy descriptor behind the complete object locator)
bool CofRttiDerives( const void *object, const char *base )
{
#if defined( _MSC_VER ) && defined( _M_IX86 )
	const void * const	*vtable;
	const unsigned int	*locator, *hierarchy, *bases;
	char			name[128];

	if( !CofMemReadable( object, sizeof( void * )))
		return false;

	vtable = *(const void * const * const *)object;

	if( !CofMemReadable( vtable - 1, sizeof( void * )))
		return false;

	locator = (const unsigned int *)vtable[-1];

	if( !CofMemReadable( locator, 5 * sizeof( unsigned int )) || locator[0] != 0 )
		return false;

	// signature, attributes, numBaseClasses, pBaseClassArray
	hierarchy = (const unsigned int *)(size_t)locator[4];

	if( !CofMemReadable( hierarchy, 4 * sizeof( unsigned int )) || hierarchy[2] == 0 || hierarchy[2] > 64 )
		return false;

	bases = (const unsigned int *)(size_t)hierarchy[3];

	if( !CofMemReadable( bases, hierarchy[2] * sizeof( unsigned int )))
		return false;

	for( unsigned int i = 0; i < hierarchy[2]; i++ )
	{
		const unsigned int *descriptor = (const unsigned int *)(size_t)bases[i];
		const char *type;

		if( !CofMemReadable( descriptor, sizeof( unsigned int )))
			return false;

		type = (const char *)(size_t)descriptor[0];

		if( !CofMemReadable( type, 2 * sizeof( void * )) || !CofCopyName( type + 2 * sizeof( void * ), name, sizeof( name )))
			continue;

		if( !strcmp( name, base ))
			return true;
	}
#else
	(void)object;
	(void)base;
#endif
	return false;
}

bool CofIsButton( Panel *panel )
{
	return panel && CofRttiDerives( panel, ".?AVButton@vgui@@" );
}

// a client handler: its class and the ints at +4 and +8
struct CofHandler
{
	char	cls[96];
	int	at4, at8;
};

bool CofReadHandler( ActionSignal *signal, CofHandler &h )
{
	if( !signal || !CofRttiName( signal, h.cls, sizeof( h.cls )))
		return false;

	if( !CofMemReadable( (const char *)signal + 4, 8 ))
		return false;

	h.at4 = *(const int *)((const char *)signal + 4);
	h.at8 = *(const int *)((const char *)signal + 8);
	return true;
}

// what B does on each panel: the handler command of its own Back / Close /
// Cancel / No button (the first open panel of the list wins; the dialogs come
// first, since they open over another panel)
struct CofBackRule
{
	const char	*panel;		// decorated class of the panel
	const char	*handler;	// decorated class of its buttons' ActionSignal
	int		offset;		// where the handler keeps its command (8; 4 for CMenuHandler_TextWindow)
	int		cmds[2];	// accepted commands, -1 = unused
	const char	*what;
};

const CofBackRule g_cofBack[] =
{
	{ ".?AVCSave@@",              ".?AVCSaveHandler_Command@@",         8, {  3,  1 }, "save confirm: No / Cancel" },
	{ ".?AVCYesno@@",             ".?AVCYesnoHandler_Command@@",        8, {  3, -1 }, "YES/NO: No (yesnono)" },
	{ ".?AVCNoteDocument@@",      ".?AVCNoteDocumentHandler_Command@@", 8, {  3, -1 }, "note: Close (closedocument)" },
	{ ".?AVCUserDocument@@",      ".?AVCUserDocumentHandler_Command@@", 8, {  1, -1 }, "user document: Close (closedocument)" },
	{ ".?AVCDocuments@@",         ".?AVCDocumentsHandler_Command@@",    8, { 25, -1 }, "notes list: Back to the inventory" },
	{ ".?AVCSaveLoad@@",          ".?AVCSaveLoadHandler_Command@@",     8, {  1, -1 }, "tape recorder page: Cancel" },
	{ ".?AVCVGUIBillboard@@",     ".?AVCBillboardHandler_Command@@",    8, {  1, -1 }, "billboard: Close" },
	{ ".?AVCKeypad@@",            ".?AVCKeypadHandler_Command@@",       8, {  1, -1 }, "keypad: Close" },
	{ ".?AVCPadlock@@",           ".?AVCPadlockHandler_Command@@",      8, {  5, -1 }, "padlock: Cancel (padlockcancel)" },
	{ ".?AVCTelephone@@",         ".?AVCTelephoneHandler_Command@@",    8, { 12, -1 }, "landline: Hang up" },
	{ ".?AVCComputer@@",          ".?AVCComputerHandler_Command@@",     8, {  2, -1 }, "computer: Close" },
	{ ".?AVCPuzzleBar@@",         ".?AVCPuzzleBarHandler_Command@@",    8, {  5, -1 }, "boiler puzzle: Close (boilercheck 5)" },
	{ ".?AVCAnimalStatue@@",      ".?AVCAnimalStatueHandler_Command@@", 8, {  1, -1 }, "statue: Close" },
	{ ".?AVCClothesMenu@@",       ".?AVCClothesMenuHandler_Command@@",  8, { 13, -1 }, "clothes: Close (closeclothes)" },
	{ ".?AVCParanoiaTextPanel@@", ".?AVCMenuHandler_TextWindow@@",      4, {  0, -1 }, "text window: Close" },
};

// the visible panel of this class below the root (the viewport's panels), or null
Panel *CofFindPanel( Panel *panel, const char *cls, int depth )
{
	char name[128];

	for( int i = 0; i < panel->getChildCount(); i++ )
	{
		Panel *child = panel->getChild( i );

		if( !child || !child->isVisible( ))
			continue;

		if( CofPanels_Class( child ) & ~COF_CLASS_PLAIN_PANEL )
		{
			const char *rtti = CofRttiName( child, name, sizeof( name ));

			if( rtti && !strcmp( rtti, cls ))
				return child;
		}

		if( depth < 3 )
		{
			Panel *found = CofFindPanel( child, cls, depth + 1 );

			if( found )
				return found;
		}
	}

	return nullptr;
}

// the visible button below `panel` with a handler of that class and command
Button *CofFindButton( Panel *panel, const CofBackRule &rule, int depth, bool includeHidden = false )
{
	for( int i = 0; i < panel->getChildCount(); i++ )
	{
		Panel *child = panel->getChild( i );

		if( !child || ( !includeHidden && !child->isVisible( )))
			continue;

		if( CofIsButton( child ))
		{
			Button *button = static_cast<Button *>( child );
			Dar<ActionSignal*> &signals = CofButtonAccess::signals( button );

			for( int s = 0; s < signals.getCount(); s++ )
			{
				CofHandler h;

				if( !CofReadHandler( signals[s], h ) || strcmp( h.cls, rule.handler ))
					continue;

				const int cmd = rule.offset == 4 ? h.at4 : h.at8;

				if( cmd == rule.cmds[0] || ( rule.cmds[1] >= 0 && cmd == rule.cmds[1] ))
					return button;
			}
		}

		if( depth < 6 )
		{
			Button *found = CofFindButton( child, rule, depth + 1, includeHidden );

			if( found )
				return found;
		}
	}

	return nullptr;
}

int CofPanelBack( char *info, int infoSize )
{
	Panel *root = g_surface ? g_surface->getPanel() : nullptr;

	if( !root || !root->isVisible( ))
		return 0;

	for( size_t r = 0; r < sizeof( g_cofBack ) / sizeof( g_cofBack[0] ); r++ )
	{
		Panel *panel = CofFindPanel( root, g_cofBack[r].panel, 1 );

		if( !panel )
			continue;

		Button *button = CofFindButton( panel, g_cofBack[r], 1 );

        // Retail notes may hide their X while retaining the native close signal.
        // Only these exact close commands may bypass child visibility; the
        // identified parent and its ancestry were verified visible above.
        const CofBackRule &rule = g_cofBack[r];
        const bool hiddenClose =
            ( !strcmp(rule.panel,".?AVCNoteDocument@@") && !strcmp(rule.handler,".?AVCNoteDocumentHandler_Command@@") && rule.offset==8 && rule.cmds[0]==3 ) ||
            ( !strcmp(rule.panel,".?AVCUserDocument@@") && !strcmp(rule.handler,".?AVCUserDocumentHandler_Command@@") && rule.offset==8 && rule.cmds[0]==1 );
        if( !button && hiddenClose ) button = CofFindButton( panel, rule, 1, true );

		if( !button )
		{
			snprintf( info, infoSize, "%s: open, but no visible button with %s command %d", g_cofBack[r].what,
				g_cofBack[r].handler, g_cofBack[r].cmds[0] );
			return 0;
		}

		const int *origin = CofPanelAccess::screenOrigin( button );
		const int *size = CofPanelAccess::size( button );

		snprintf( info, infoSize, "%s: clicked its button at %d,%d %dx%d", g_cofBack[r].what, origin[0], origin[1], size[0], size[1] );
		button->doClick();
		return 1;
	}

	snprintf( info, infoSize, "no panel with a known back button is open" );
	return 0;
}

// after a gamepad left click: a tape recorder slot gets its second click
int CofPanelConfirm( char *info, int infoSize )
{
	XashApp *app = static_cast<XashApp *>( App::getInstance( ));
	Panel *p = app ? app->cofMouseFocus() : nullptr;
	char name[128];

	// the focus may be the button or a picture / label inside it
	for( int up = 0; p && up < 3 && !CofIsButton( p ); up++ )
		p = CofPanelAccess::parent( p );

	if( !p || !CofIsButton( p ))
		return 0;

	Button *button = static_cast<Button *>( p );
	Dar<ActionSignal*> &signals = CofButtonAccess::signals( button );
	int cmd = 0;

	for( int s = 0; s < signals.getCount(); s++ )
	{
		CofHandler h;

		if( CofReadHandler( signals[s], h ) && !strcmp( h.cls, ".?AVCSaveLoadHandler_Command@@" ) && h.at8 >= 2 && h.at8 <= 6 )
			cmd = h.at8;
	}

	if( !cmd )
		return 0;

	for( Panel *page = CofPanelAccess::parent( button ); page; page = CofPanelAccess::parent( page ))
	{
		const char *rtti = CofRttiName( page, name, sizeof( name ));

		if( !rtti || strcmp( rtti, ".?AVCSaveLoad@@" ))
			continue;

		// the first click may already have acted (it was the second click of
		// a double click): then the page is gone and nothing more is sent
		if( !page->isVisible( ) || !button->isVisible( ))
		{
			snprintf( info, infoSize, "tape recorder slot %d: the page already closed, no second click", cmd - 1 );
			return 0;
		}

		snprintf( info, infoSize, "tape recorder slot %d: second click (the page acts on a double click)", cmd - 1 );
		button->doClick();
		return 1;
	}

	return 0;
}

void CofDumpButtons( Panel *panel, int depth )
{
	char name[128], line[512];

	for( int i = 0; i < panel->getChildCount(); i++ )
	{
		Panel *child = panel->getChild( i );

		if( !child || !child->isVisible( ))
			continue;

		if( CofIsButton( child ))
		{
			Button *button = static_cast<Button *>( child );
			Dar<ActionSignal*> &signals = CofButtonAccess::signals( button );
			const int *origin = CofPanelAccess::screenOrigin( button );
			const int *size = CofPanelAccess::size( button );
			const char *text = CofButtonAccess::label( button );
			char handlers[256], label[48];
			size_t len = 0;

			// the button's own caption, when it is plain text
			label[0] = 0;
			if( text )
			{
				if( !CofCopyName( text, label, sizeof( label )))
					snprintf( label, sizeof( label ), "(unreadable)" );
				else
				{
					for( len = 0; label[len]; len++ )
					{
						if( (unsigned char)label[len] < 0x20 || (unsigned char)label[len] > 0x7E )
							break;
					}
					if( label[len] )
						snprintf( label, sizeof( label ), "(binary)" );
				}
			}

			handlers[0] = 0;
			for( int s = 0; s < signals.getCount(); s++ )
			{
				CofHandler h;
				char one[128];

				if( CofReadHandler( signals[s], h ))
					snprintf( one, sizeof( one ), " %s(+4 %d, +8 %d)", h.cls, h.at4, h.at8 );
				else snprintf( one, sizeof( one ), " ?" );
				strncat( handlers, one, sizeof( handlers ) - strlen( handlers ) - 1 );
			}

			snprintf( line, sizeof( line ), "[cof-pad]   %*s%s at %d,%d %dx%d text \"%s\" signals:%s\n", depth * 2, "",
				CofPanels_ClassName( button, name, sizeof( name )), origin[0], origin[1], size[0], size[1],
				label, handlers[0] ? handlers : " none" );
			g_engine->CofPrint( line );
		}

		if( depth < 6 )
			CofDumpButtons( child, depth + 1 );
	}
}

int CofPanelDump( void )
{
	Panel *root = g_surface ? g_surface->getPanel() : nullptr;
	char name[128], line[256];
	int n = 0;

	if( !root || !g_engine || !g_engine->CofPrint )
		return 0;

	for( int i = 0; i < root->getChildCount(); i++ )
	{
		Panel *viewport = root->getChild( i );

		if( !viewport || !viewport->isVisible( ))
			continue;

		for( int j = 0; j < viewport->getChildCount(); j++ )
		{
			Panel *panel = viewport->getChild( j );
			unsigned int bits;

			if( !panel || !panel->isVisible( ))
				continue;

			bits = CofPanels_Class( panel ) & ~( COF_CLASS_PLAIN_PANEL | COF_PANEL_HUD );

			if( !bits )
				continue;

			snprintf( line, sizeof( line ), "[cof-pad] panel %s (0x%x) buttons:\n", CofPanels_ClassName( panel, name, sizeof( name )), bits );
			g_engine->CofPrint( line );
			CofDumpButtons( panel, 1 );
			n++;
		}
	}

	return n;
}

// developer: the centre of the visible button with this handler and command
int CofPanelLocate( char *info, int infoSize )
{
	Panel *root = g_surface ? g_surface->getPanel() : nullptr;
	CofBackRule rule = { "", "", 8, { -1, -1 }, "" };
	char handler[96];
	int cmd;

	if( !root || sscanf( info, "%95s %d", handler, &cmd ) != 2 )
		return 0;

	rule.handler = handler;
	rule.cmds[0] = cmd;

	Button *button = CofFindButton( root, rule, 1 );

	if( !button )
	{
		snprintf( info, infoSize, "none" );
		return 0;
	}

	const int *origin = CofPanelAccess::screenOrigin( button );
	const int *size = CofPanelAccess::size( button );

	snprintf( info, infoSize, "%d %d", origin[0] + size[0] / 2, origin[1] + size[1] / 2 );
	return 1;
}

} // anonymous namespace

/*
================
CofPanels_Action

vguiapi_t::CofPanelAction, see engine/vgui_api.h.
================
*/
int vgui::CofPanels_Action( int action, char *info, int infoSize )
{
	char scratch[8];

	if( !info || infoSize <= 0 )
	{
		info = scratch;
		infoSize = sizeof( scratch );
		info[0] = 0;
	}

	if( action == COF_VGUI_PANEL_LOCATE )
		return CofPanelLocate( info, infoSize );

	info[0] = 0;

	switch( action )
	{
	case COF_VGUI_PANEL_BACK:
		return CofPanelBack( info, infoSize );
	case COF_VGUI_PANEL_CONFIRM:
		return CofPanelConfirm( info, infoSize );
	case COF_VGUI_PANEL_DUMP:
		snprintf( info, infoSize, "%d panel(s) listed", CofPanelDump( ));
		return 1;
	}

	return 0;
}
