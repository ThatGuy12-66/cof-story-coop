// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#include "vgui_internal.h"
#include "support.h"

using namespace vgui;

vguiapi_t *vgui::g_engine = nullptr;
Panel *vgui::g_rootPanel = nullptr;
XashSurface *vgui::g_surface = nullptr;

// declare as global so it installs itself as App::getInstance singleton
static XashApp staticApp;

static void XashStartup( int width, int height )
{
	CofUI_Refresh();

	if( g_rootPanel )
	{
		g_rootPanel->setSize( width, height );
		return;
	}

	g_rootPanel = new Panel( 0, 0, width, height );
	g_rootPanel->setPaintBorderEnabled( false );
	g_rootPanel->setPaintBackgroundEnabled( false );
	g_rootPanel->setVisible( true );
	g_rootPanel->setCursor( new Cursor( Cursor::DC_NONE ));

	staticApp.start();
	staticApp.setMinimumTickMillisInterval( 0 );

	g_surface = new XashSurface( g_rootPanel );
	g_rootPanel->setSurfaceBaseTraverse( g_surface );
	g_engine->DrawInit();
}

static void XashShutdown( void )
{
	staticApp.stop();

	delete g_rootPanel;
	g_rootPanel = nullptr;

	delete g_surface;
	g_surface = nullptr;
}

static void *XashGetPanel( void )
{
	return (void *)g_rootPanel;
}

static void XashPaint( void )
{
	if( !g_engine->IsInGame() || !g_rootPanel || !g_surface )
		return;

	Panel *panel = g_surface->getPanel();

	if( !panel )
		return;

	int width, height;

	// Cry of Fear in-game UI scaling: re-read the transform before the tree is
	// solved, and keep the root large enough to hold everything the anchor pass
	// may move. It only ever grows, so the engine's own initial size - which is
	// what the client read when it laid itself out - is never taken away
	CofUI_Refresh();

	g_rootPanel->getSize( width, height );

	if( CofUI_RootSize( width, height ))
	{
		int currentWide, currentTall;

		g_rootPanel->getSize( currentWide, currentTall );
		width = Q_max( width, currentWide );
		height = Q_max( height, currentTall );
		g_rootPanel->setSize( width, height );
	}

	g_scissor.enable();

	staticApp.externalTick();

	// Cry of Fear panel pause: which client panels are on screen this frame
	// (cofpanels.cpp), after input has had its chance to open or close one
	CofPanels_Frame( panel );

	// Cry of Fear: which black to leave out, how to draw the plate and which
	// HUD pieces to hide while such a panel is open (cofbackdrop.cpp)
	CofBackdrop_Frame( panel );

	panel->setBounds( 0, 0, width, height );
	panel->repaint();
	panel->paintTraverse();

	// cof_font_probe draws last, over everything, so it is visible whatever the
	// client has on screen
	g_surface->cofDrawProbe();

	// Cry of Fear, milestone 4b: nothing may be left in the backing-text
	// collector when the frame ends. popMakeCurrent flushes what a panel drew;
	// this closes out the probe above, which draws outside any push/pop pair
	g_surface->flushBackingText();

	g_scissor.disable();
}

#if defined( INTERNAL_VGUI_SUPPORT )
// what the engine probes in the client library when we're linked into it statically
#define VGUI_SUPPORT_INIT InitVGUISupportAPI
#else
#define VGUI_SUPPORT_INIT InitAPI
#endif

extern "C" void EXPORT VGUI_SUPPORT_INIT( vguiapi_t *api )
{
	// the structure is owned by the engine, keep the pointer, not a copy
	g_engine = api;

	api->Startup = XashStartup;
	api->Shutdown = XashShutdown;
	api->GetPanel = XashGetPanel;
	api->Paint = XashPaint;
	api->Mouse = XashMouse;
	api->Key = XashKey;
	api->MouseMove = XashMouseMove;
	api->TextInput = XashTextInput;

	// Cry of Fear in-game UI scaling: anchor each top-level client surface as
	// the panel tree is solved (cofscale.cpp)
	Panel_SetSolveHook( CofUI_SolvePanel );

	// Cry of Fear engine-rasterised VGUI fonts (coffont.cpp)
	CofFont_Install();

	// Cry of Fear: the live world behind the in-play panels (cofbackdrop.cpp)
	CofBackdrop_Install();

	// Cry of Fear gamepad: B = the open panel's own back button, A on a tape
	// recorder slot also confirms it (cofpanels.cpp)
	api->CofPanelAction = CofPanels_Action;
}
