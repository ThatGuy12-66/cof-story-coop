// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#ifndef VGUI_PANEL_H
#define VGUI_PANEL_H

#include "vgui.h"
#include "scheme.h"
#include "image.h"
#include "input.h"

namespace vgui
{
class App;
class BuildGroup;
class SurfaceBase;
class Panel;
class Border;
class InputSignal;
class RepaintSignal;
class Layout;
class FocusChangeSignal;
class LayoutInfo;
class FocusNavGroup;

// Cry of Fear in-game UI scaling (milestone 4): the anchor pass in the platform
// layer works on solved coordinates directly. Panel's size is ABI-fixed - the
// game library allocates its own subclasses - so the class carries no state of
// its own for this and the pass reaches the solved members through here.
struct CofPanelAccess
{
	static int *screenOrigin( Panel *panel );
	static int *clipRect( Panel *panel );
	static const int *size( Panel *panel );
	static Panel *parent( Panel *panel );
};

class CLASSEXPORT Panel
{
	friend class App;
	friend class Image;
	friend class SurfaceBase;
	friend struct CofPanelAccess;
public:
	Panel( int x, int y, int w, int h );
	Panel();
	virtual void setPos( int x, int y );
	virtual void getPos( int &x, int &y );
	virtual void setSize( int w, int h );
	virtual void getSize( int &w, int &h );
	virtual void setBounds( int x, int y, int w, int h );
	virtual void getBounds( int &x, int &y, int &w, int &h );
	virtual int getWide();
	virtual int getTall();
	virtual Panel *getParent();
	virtual void setVisible( bool state );
	virtual bool isVisible();
	virtual bool isVisibleUp();
	virtual void repaint();
	virtual void repaintAll();
	virtual void getAbsExtents( int &x0, int &y0, int &x1, int &y1 );
	virtual void getClipRect( int &x0, int &y0, int &x1, int &y1 );
	virtual void setParent( Panel *newParent );
	virtual void addChild( Panel *child );
	virtual void insertChildAt( Panel *child, int i );
	virtual void removeChild( Panel *child );
	virtual bool wasMousePressed( MouseCode );
	virtual bool wasMouseDoublePressed( MouseCode );
	virtual bool isMouseDown( MouseCode );
	virtual bool wasMouseReleased( MouseCode );
	virtual bool wasKeyPressed( KeyCode );
	virtual bool isKeyDown( KeyCode );
	virtual bool wasKeyTyped( KeyCode );
	virtual bool wasKeyReleased( KeyCode );
	virtual void addInputSignal( InputSignal *is );
	virtual void removeInputSignal( InputSignal *is );
	virtual void addRepaintSignal( RepaintSignal *rs );
	virtual void removeRepaintSignal( RepaintSignal *rs );
	virtual bool isWithin( int x, int y );
	virtual Panel *isWithinTraverse( int x, int y );
	virtual void localToScreen( int &x, int &y );
	virtual void screenToLocal( int &x, int &y );
	virtual void setCursor( Cursor *newCursor );
	virtual void setCursor( Scheme::SchemeCursor scu );
	virtual Cursor *getCursor();
	virtual void setMinimumSize( int w, int h );
	virtual void getMinimumSize( int &w, int &h );
	virtual void requestFocus();
	virtual bool hasFocus();
	virtual int getChildCount();
	virtual Panel *getChild( int i );
	virtual void setLayout( Layout *newLayout );
	virtual void invalidateLayout( bool layoutNow );
	virtual void setFocusNavGroup( FocusNavGroup *fng );
	virtual void requestFocusPrev();
	virtual void requestFocusNext();
	virtual void addFocusChangeSignal( FocusChangeSignal *fcs );
	virtual bool isAutoFocusNavEnabled();
	virtual void setAutoFocusNavEnabled( bool enable );
	virtual void setBorder( Border *newBorder );
	virtual void setPaintBorderEnabled( bool enable );
	virtual void setPaintBackgroundEnabled( bool enable );
	virtual void setPaintEnabled( bool enable );
	virtual void getInset( int &left, int &top, int &right, int &bottom );
	virtual void getPaintSize( int &w, int &h );
	virtual void setPreferredSize( int w, int h );
	virtual void getPreferredSize( int &w, int &h );
	virtual SurfaceBase *getSurfaceBase();
	virtual bool isEnabled();
	virtual void setEnabled( bool enable );
	virtual void setBuildGroup( BuildGroup *, const char * );
	virtual bool isBuildGroupEnabled();
	virtual void removeAllChildren();
	virtual void repaintParent();
	virtual Panel* createPropertyPanel();
	virtual void getPersistanceText( char *buf, int len );
	virtual void applyPersistanceText( const char * );
	virtual void setFgColor( Scheme::SchemeColor sc );
	virtual void setBgColor( Scheme::SchemeColor sc );
	virtual void setFgColor( int r, int g, int b, int a );
	virtual void setBgColor( int r, int g, int b, int a );
	virtual void getFgColor( int &r, int &g, int &b, int &a );
	virtual void getBgColor( int &r, int &g, int &b, int &a );
	virtual void setBgColor( Color c );
	virtual void setFgColor( Color c );
	virtual void getBgColor( Color &c );
	virtual void getFgColor( Color &c );
	virtual void setAsMouseCapture( bool );
	virtual void setAsMouseArena( bool );
	virtual App *getApp();
	virtual void getVirtualSize( int &w, int &h );
	virtual void setLayoutInfo( LayoutInfo *li );
	virtual LayoutInfo *getLayoutInfo();
	virtual bool isCursorNone();
	virtual void solveTraverse();
	virtual void paintTraverse();
	virtual void setSurfaceBaseTraverse( SurfaceBase *sb );

protected:
	virtual void performLayout();
	virtual void internalPerformLayout();
	virtual void drawSetColor( Scheme::SchemeColor );
	virtual void drawSetColor( int, int, int, int );
	virtual void drawFilledRect( int, int, int, int );
	virtual void drawOutlinedRect( int, int, int, int );
	virtual void drawSetTextFont( Scheme::SchemeFont );
	virtual void drawSetTextFont( Font* );
	virtual void drawSetTextColor( Scheme::SchemeColor );
	virtual void drawSetTextColor( int, int, int, int );
	virtual void drawSetTextPos( int, int);
	virtual void drawPrintText( const char *, int );
	virtual void drawPrintText( int, int, const char *, int );
	virtual void drawPrintChar( char);
	virtual void drawPrintChar( int, int, char);
	virtual void drawSetTextureRGBA( int, const char *, int, int );
	virtual void drawSetTexture( int);
	virtual void drawTexturedRect( int, int, int, int );
	virtual void solve();
	virtual void paintTraverse( bool repaint );
	virtual void paintBackground();
	virtual void paint();
	virtual void paintBuildOverlay();
	virtual void internalCursorMoved( int x, int y );
	virtual void internalCursorEntered();
	virtual void internalCursorExited();
	virtual void internalMousePressed( MouseCode );
	virtual void internalMouseDoublePressed( MouseCode );
	virtual void internalMouseReleased( MouseCode );
	virtual void internalMouseWheeled( int );
	virtual void internalKeyPressed( KeyCode );
	virtual void internalKeyTyped( KeyCode );
	virtual void internalKeyReleased( KeyCode );
	virtual void internalKeyFocusTicked();
	virtual void internalFocusChanged( bool );
	virtual void internalSetCursor();

	int origin[2];
	int size[2];
	int screenOrigin[2];
	int minimumSize[2];
	int preferredSize[2];

	Dar<Panel*> children;
	Panel *parent;
	SurfaceBase *surfaceBase;

	Dar<InputSignal*> inputSignals;
	Dar<RepaintSignal*> repaintSignals;

	int clipRect[4];

	Cursor *cursor;
	Scheme::SchemeCursor schemeCursor;

	bool visible;

	Layout *layout;
	bool pendingLayout;

	FocusNavGroup *focusNavGroup;
	Dar<FocusChangeSignal*> focusChangeSignals;
	bool autoFocusNavEnabled;

	Border *border;
private:
	void init( int x, int y, int w, int h );

	bool pendingRepaint;
	bool enabled;
	BuildGroup *buildGroup;
	Color foregroundColor;
	Color backgroundColor;
	LayoutInfo *layoutInfo;
	bool paintBorderEnabled;
	bool paintBackgroundEnabled;
	bool paintEnabled;
};
CHECK_STRUCT_SIZE( Panel, 188, 264, 264 );

// Cry of Fear in-game UI scaling (milestone 4). The platform layer may install
// a hook that runs at the end of Panel::solve(), after screenOrigin and
// clipRect have been computed and before any child is solved, and may move the
// panel so it stays anchored to the screen edge it was laid out against.
// Because solve() is the single place absolute coordinates are produced, and
// both painting (getAbsExtents) and hit testing (screenToLocal) read them, the
// drawn position and the mouse target can never disagree.
// Panel's size is ABI-fixed, so the hook works on the public members directly
// instead of the class carrying any new state.
typedef void ( *PanelSolveFn )( Panel *panel );
void Panel_SetSolveHook( PanelSolveFn fn );

// Cry of Fear (cof_panel_transparent): the platform layer may skip painting a
// visible panel's whole subtree (skipTraverse, checked at the top of
// paintTraverse) or only its background fill (skipBackground, checked in
// Panel::paintBackground). Both return true to skip. The panel stays visible,
// laid out and hit-testable; only its pixels are left out.
typedef bool ( *PanelPaintFilterFn )( Panel *panel );
void Panel_SetPaintFilters( PanelPaintFilterFn skipTraverse, PanelPaintFilterFn skipBackground );

class CLASSEXPORT FocusNavGroup
{
	friend class Panel;

public:
	FocusNavGroup();
protected:
	virtual void addPanel( Panel *panel );
	virtual void requestFocusPrev();
	virtual void requestFocusNext();
	virtual void setCurrentPanel( Panel *panel );

	Dar<Panel*> panels;
	int index;
};
CHECK_STRUCT_SIZE( FocusNavGroup, 20, 32, 32 );

}

#endif // VGUI_PANEL_H
