/*
ItemsHolder.cpp -- an item that can contain and operate with other items
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include "extdll_menu.h"
#include "BaseMenu.h"
#include "Utils.h"
#include "PicButton.h"
#include "ItemsHolder.h"
#include "Scissor.h"
#include "CoFOptions.h"
#include "SelectionPopup.h"
#include <string.h>

CMenuItemsHolder::CMenuItemsHolder() :
	BaseClass(), m_iCursor( 0 ), m_iCursorPrev( 0 ), m_pItems( ),
	m_events(), m_bInit( false ),
	m_bWrapCursor( true ), m_szResFile( 0 ), m_pItemAtCursorOnDown( 0 ),
	m_iHotKeyDown( 0 )
{
	;
}

bool CMenuItemsHolder::Key( const int key, const bool down )
{
	bool handled = false;

	if( !m_pItems.Count() )
		return false;

	if( UI::Key::IsMouseWheel( key ) )
	{
		FOR_EACH_VEC( m_pItems, i )
		{
			CMenuBaseItem *item = m_pItems[i];

			if( !item || FBitSet( item->iFlags, QMF_GRAYED | QMF_INACTIVE ) )
				continue;

			if( !item->IsVisible() )
				continue;

			if( !UI_CursorInRect( item->m_scPos, item->m_scSize ) )
				continue;

			if( down ? item->KeyDown( key ) : item->KeyUp( key ) )
				return true;
		}

		return false;
	}

	CMenuBaseItem *item = NULL;
	int cursorPrev;

	if( down )
	{
		item = ItemAtCursor();
		m_pItemAtCursorOnDown = item;
	}
	else
	{
		// don't send released event to item, which don't got pressed before!
		if( m_pItemAtCursorOnDown && m_pItemAtCursorOnDown->m_pParent == this )
			item = m_pItemAtCursorOnDown;
		m_pItemAtCursorOnDown = NULL;
	}

	if( item && item->IsVisible() && !(item->iFlags & (QMF_GRAYED|QMF_INACTIVE) ) )
	{
		// mouse keys must be checked for item bounds on press
		if( !UI::Key::IsMouse( key ) ||
			 (UI::Key::IsMouse( key ) && ( !down || UI_CursorInRect( item->m_scPos, item->m_scSize ))))
		{
			handled = down ? item->KeyDown( key ) : item->KeyUp( key );
			if( handled )
				return handled;
		}
	}

	// only mouse keys can call key events for items that's not in focus
	if( UI::Key::IsMouse( key ) || UI::Key::IsAlphaNum( key ))
	{
		FOR_EACH_VEC( m_pItems, i )
		{
			item = m_pItems[i];

			if( !item )
				continue;

			if( item == ItemAtCursor() )
				continue;

			if( item->iFlags & (QMF_GRAYED|QMF_INACTIVE) )
				continue;

			if( !item->IsVisible() )
				continue;

			down ? item->KeyDown( key ) : item->KeyUp( key );
		}
	}

	// original menu handles these on key down event
	// but this makes navigating confusing, since buttons can repeat
	// we will do it differently
	if( UI::Key::IsAlphaNum( key ))
	{
		FOR_EACH_VEC( m_pItems, i )
		{
			item = m_pItems[i];

			if( !item || FBitSet( item->iFlags, QMF_GRAYED|QMF_INACTIVE ))
				continue;

			if( !item->IsVisible( ))
				continue;

			// only respond to last pressed key, even if it's not handled
			if( down )
				m_iHotKeyDown = key;

			if( !item->HotKey( key ))
			{
				// something was probably pressed down
				// remove pressed button glowing effect
				if( m_iHotKeyDown != 0 )
					item->m_bPressed = false;
				continue;
			}

			// pressing acts as "go to"
			if( down )
			{
				// set focus to not let random key interfere
				if( item != ItemAtCursor( ))
				{
					m_pItemAtCursorOnDown = item;
					SetCursor( i, true );
				}

				item->_Event( QM_PRESSED );
			}
			else if( m_iHotKeyDown == key ) // only release from same key
			{
				// do not send released events to keys not in focus
				if( item == ItemAtCursor( ))
					item->_Event( QM_RELEASED );

				m_iHotKeyDown = 0;
			}
			break;
		}
	}

	// system keys are always wait for keys down and never keys up
	if( !down )
		return handled;

	// default handling -- items navigation
	if( UI::Key::IsNavigationKey( key ))
	{
		int direction;
		cursorPrev = m_iCursorPrev = m_iCursor;

		// Cry of Fear options layout: the focus follows where the controls
		// are on the screen, not the order they were added in
		if( UI_CoFOptionsLayout( ))
		{
			const int target = UI_CoFNavTarget( this, m_iCursor, key, m_bWrapCursor );

			if( target != COF_NAV_STOCK )
			{
				if( target < 0 || target == m_iCursor || !m_pItems.IsValidIndex( target ))
					return false;

				m_iCursor = target;
				CursorMoved();
				m_pItems[m_iCursor]->PlayLocalSound( uiStatic.sounds[SND_MOVE] );
				if( m_pItems.IsValidIndex( m_iCursorPrev ))
					ClearBits( m_pItems[m_iCursorPrev]->iFlags, QMF_HASKEYBOARDFOCUS );
				SetBits( m_pItems[m_iCursor]->iFlags, QMF_HASKEYBOARDFOCUS );
				return true;
			}
		}

		if( UI::Key::IsUpArrow( key ) || UI::Key::IsLeftArrow( key ))
			direction = -1;
		else // if( UI::Key::IsDownArrow( key ) || UI::Key::IsRightArrow( key ) || key == K_TAB )
			direction = 1;

		m_iCursor += direction;
		if( AdjustCursor( direction ) )
		{
			if( cursorPrev != m_iCursor )
			{
				CursorMoved();
				m_pItems[m_iCursor]->PlayLocalSound( uiStatic.sounds[SND_MOVE] );
				handled = true;

				ClearBits( m_pItems[m_iCursorPrev]->iFlags, QMF_HASKEYBOARDFOCUS );
				SetBits( m_pItems[m_iCursor]->iFlags, QMF_HASKEYBOARDFOCUS );
			}
		}
		else
		{
			handled = false;
		}
	}

	return handled;
}

bool CMenuItemsHolder::KeyDown( int key )
{
	return CMenuItemsHolder::Key( key, true );
}

bool CMenuItemsHolder::KeyUp( int key )
{
	return CMenuItemsHolder::Key( key, false );
}

void CMenuItemsHolder::Char( int ch )
{
	if( m_pItems.IsEmpty() )
		return;

	CMenuBaseItem *item = ItemAtCursor();

	if( item && item->IsVisible() && !(item->iFlags & (QMF_GRAYED|QMF_INACTIVE)) )
		item->Char( ch );
}

bool CMenuItemsHolder::MouseMove( int x, int y )
{
	int i = 0;
	// region test the active menu items
	// go in reverse direction, so last items will be first
	FOR_EACH_VEC_BACK( m_pItems, i )
	{
		CMenuBaseItem *item = m_pItems[i];

		// just in case
		if( !item )
			continue;

		// Invisible or inactive items will be skipped
		if( !item->IsVisible() || FBitSet( item->iFlags, QMF_INACTIVE ))
		{
			if( FBitSet( item->iFlags, QMF_HASMOUSEFOCUS ))
			{
				if( !UI_CursorInRect( item->m_scPos, item->m_scSize ) )
					ClearBits( item->iFlags, QMF_HASMOUSEFOCUS );
				else item->m_iLastFocusTime = uiStatic.realTime;
			}
			continue;
		}

		// simple region test
		if( !UI_CursorInRect( item->m_scPos, item->m_scSize ) || !item->MouseMove( x, y ))
		{
			ClearBits( item->iFlags, QMF_HASMOUSEFOCUS );
			continue;
		}

		if( m_iCursor != i )
		{
			SetCursor( i );
			// reset two focus states, because we are changed cursor
			if( m_iCursorPrev != -1 )
				ClearBits( m_pItems[m_iCursorPrev]->iFlags, QMF_HASMOUSEFOCUS|QMF_HASKEYBOARDFOCUS );

			m_pItems[m_iCursor]->PlayLocalSound( uiStatic.sounds[SND_MOVE] );
		}

		SetBits( m_pItems[m_iCursor]->iFlags, QMF_HASMOUSEFOCUS );
		m_pItems[m_iCursor]->m_iLastFocusTime = uiStatic.realTime;
		// Should we stop at first matched item?
		return true;
	}

	// out of any region
	if( !i && m_pItems.Count() > 0 )
	{
		ClearBits( m_pItems[m_iCursor]->iFlags, QMF_HASMOUSEFOCUS );

		// a mouse only item restores focus to the previous item
		if( FBitSet( m_pItems[m_iCursor]->iFlags, QMF_MOUSEONLY ) && m_iCursorPrev != -1 )
			m_iCursor = m_iCursorPrev;
	}

	return false;
}

void CMenuItemsHolder::Init()
{
	if( !WasInit() )
	{
		m_bInit = true;

		_Init();
		// m_pLayout->Init();

		if( m_szResFile )
			LoadRES( m_szResFile );
	}
}

void CMenuItemsHolder::VidInit()
{
	_VidInit();
	CalcPosition();
	CalcSizes();
	VidInitItems();
	// m_pLayout->VidInit();
}

void CMenuItemsHolder::Reload()
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->Reload();
}

void CMenuItemsHolder::VidInitItems()
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->VidInit();
}

void CMenuItemsHolder::ToggleInactive()
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->ToggleInactive();
}

void CMenuItemsHolder::SetInactive( bool inactive )
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->SetInactive( inactive );
}

void CMenuItemsHolder::Draw( )
{
	CMenuBaseItem *item;

	// draw contents
	FOR_EACH_VEC( m_pItems, i )
	{
		item = m_pItems[i];

		if( item->iFlags & QMF_HIDDENBYPARENT )
			continue;

		if( !item->IsVisible() )
			continue;

		item->Draw();

		if( ui_borderclip->value )
			UI_DrawRectangle( item->m_scPos, item->m_scSize, PackRGBA( 255, 0, 0, 255 ) );
	}
}

void CMenuItemsHolder::Think( void )
{
	FOR_EACH_VEC( m_pItems, i )
	{
		m_pItems[i]->Think();
	}
}

/*
=================
UI_CoFNavTarget

Cry of Fear options layout (theme on): pad and arrow-key focus follows where
the controls are drawn (their render rectangles), not the order they were
added in, which the pages never kept in step with their layout.

  * edge rows: a control lying wholly below (footer: the button row) or
    above (header) every control not on its own row, by at least 16 units,
    belongs to no column;
  * columns: the other controls whose horizontal extents overlap by at
    least 30 % of the narrower one, joined transitively (a two-column page
    has two, a full-width list joins everything);
  * up / down (Tab = down, Shift+Tab = up) stay in the column: the nearest
    control beyond the focused one by vertical gap (not on its row), ties to
    the closest left edge; past the column's end, the nearest edge row
    control that way (vertical gap + 2 x the distance between the left
    edges: the button row under either column); past that, the other end of the same column when
    the holder wraps. From an edge row control: the nearest control that way
    (the same measure), else (wrapping) the farthest one the other way;
  * left / right: a control on the same row, or one in another column (not
    an edge row) - the one at the nearest height (8 x vertical gap +
    horizontal gap, then the centre distance); never wrapping; nothing that
    way = the key is not used.

A focused slider, spin control, field or list keeps its own use of the keys
(value, caret, rows) - only keys it does not use get here. Candidates are
the items the stock order stops on: visible, not inactive, not mouse-only,
not greyed. Returns the index to focus, -1 for none, or COF_NAV_STOCK when
the stock creation order should handle the key (too many items).
=================
*/
struct cofNavBox_t
{
	int l, t, r, b;
};

static int UI_CoFNavOverlap( int a0, int a1, int b0, int b1 )
{
	const int o = Q_min( a1, b1 ) - Q_max( a0, b0 );
	return o > 0 ? o : 0;
}

static int UI_CoFNavGap( int a0, int a1, int b0, int b1 )
{
	const int g = Q_max( a0, b0 ) - Q_min( a1, b1 );
	return g > 0 ? g : 0;
}

static bool UI_CoFNavSameColumn( const cofNavBox_t &a, const cofNavBox_t &b )
{
	const int narrow = Q_min( a.r - a.l, b.r - b.l );
	return narrow > 0 && UI_CoFNavOverlap( a.l, a.r, b.l, b.r ) * 10 >= narrow * 3;
}

static bool UI_CoFNavSameRow( const cofNavBox_t &a, const cofNavBox_t &b )
{
	const int low = Q_min( a.b - a.t, b.b - b.t );
	return low > 0 && UI_CoFNavOverlap( a.t, a.b, b.t, b.b ) * 2 >= low;
}

bool UI_CoFNavUsable( CMenuBaseItem *item )
{
	return item && item->IsVisible() &&
		!FBitSet( item->iFlags, QMF_INACTIVE | QMF_MOUSEONLY | QMF_GRAYED | QMF_HIDDENBYPARENT ) &&
		item->GetRenderSize().w > 0 && item->GetRenderSize().h > 0;
}

#define COF_NAV_MAX	160

int UI_CoFNavTarget( CMenuItemsHolder *holder, int cur, int key, bool wrap )
{
	const int count = holder ? holder->ItemCount() : 0;
	int dx = 0, dy = 0;

	if( UI::Key::IsUpArrow( key )) dy = -1;
	else if( UI::Key::IsDownArrow( key )) dy = 1;
	else if( UI::Key::IsLeftArrow( key )) dx = -1;
	else if( UI::Key::IsRightArrow( key )) dx = 1;
	else if( key == K_TAB ) dy = EngFuncs::KEY_IsDown( K_SHIFT ) ? -1 : 1;
	else return COF_NAV_STOCK;

	if( count <= 0 || count > COF_NAV_MAX )
		return COF_NAV_STOCK;

	cofNavBox_t box[COF_NAV_MAX];
	bool use[COF_NAV_MAX], edge[COF_NAV_MAX];
	int group[COF_NAV_MAX];

	for( int i = 0; i < count; i++ )
	{
		CMenuBaseItem *item = holder->GetItemByIndex( i );

		use[i] = UI_CoFNavUsable( item );
		edge[i] = false;
		group[i] = i;
		if( item )
		{
			const Point p = item->GetRenderPosition();
			const Size sz = item->GetRenderSize();

			box[i].l = p.x;
			box[i].t = p.y;
			box[i].r = p.x + sz.w;
			box[i].b = p.y + sz.h;
		}
		else memset( &box[i], 0, sizeof( box[i] ));
	}

	// no focus yet (or on nothing that has a place): the top-left control
	if( cur < 0 || cur >= count || !holder->GetItemByIndex( cur ) ||
		box[cur].r <= box[cur].l || box[cur].b <= box[cur].t )
	{
		int best = -1;
		for( int i = 0; i < count; i++ )
		{
			if( use[i] && ( best < 0 || box[i].t < box[best].t ||
				( box[i].t == box[best].t && box[i].l < box[best].l )))
				best = i;
		}
		return best;
	}


    if(dy && UI_CoFSelectionLists())
    {
        int order[COF_NAV_MAX], n=0;
        for(int i=0;i<count;i++)if(use[i])order[n++]=i;
        // Sort by top, then left; insertion index is the stable tie breaker.
        for(int i=1;i<n;i++)for(int j=i;j>0;j--)
        {
            int a=order[j-1],b=order[j];
            if(box[a].t<box[b].t || (box[a].t==box[b].t &&
                (box[a].l<box[b].l || (box[a].l==box[b].l && a<b))))break;
            order[j-1]=b;order[j]=a;
        }
        if(!n)return -1;
        int at=-1;for(int i=0;i<n;i++)if(order[i]==cur)at=i;
        if(at<0)return dy>0?order[0]:order[n-1];
        at+=dy;
        if(at<0||at>=n){if(!wrap)return -1;at=at<0?n-1:0;}
        return order[at];
    }
	// edge rows (header / footer)
	const int edgeGap = Q_max( 1, (int)( 16 * uiStatic.scaleY ));
	for( int i = 0; i < count; i++ )
	{
		if( !use[i] && i != cur )
			continue;

		bool below = true, above = true, others = false;
		for( int j = 0; j < count && ( below || above ); j++ )
		{
			if( j == i || ( !use[j] && j != cur ) || UI_CoFNavSameRow( box[i], box[j] ))
				continue;
			others = true;
			if( box[j].b + edgeGap > box[i].t ) below = false;
			if( box[j].t < box[i].b + edgeGap ) above = false;
		}
		edge[i] = others && ( below || above );
	}

	// columns: union of the non-edge usable items (and the focused one) that
	// overlap horizontally, transitively
	for( int pass = 0; pass < count; pass++ )
	{
		bool changed = false;

		for( int i = 0; i < count; i++ )
		{
			if(( !use[i] && i != cur ) || edge[i] )
				continue;
			for( int j = i + 1; j < count; j++ )
			{
				if(( !use[j] && j != cur ) || edge[j] || group[i] == group[j] )
					continue;
				if( !UI_CoFNavSameColumn( box[i], box[j] ))
					continue;

				const int from = Q_max( group[i], group[j] ), to = Q_min( group[i], group[j] );
				for( int k = 0; k < count; k++ )
				{
					if( group[k] == from )
						group[k] = to;
				}
				changed = true;
			}
		}
		if( !changed )
			break;
	}

	const cofNavBox_t &c = box[cur];
	const int cx2 = c.l + c.r, cy2 = c.t + c.b;
	int best = -1;
	long bestA = 0, bestB = 0;

	auto better = [&]( int i, long a, long b ) -> void
	{
		if( best < 0 || a < bestA || ( a == bestA && b < bestB ))
		{
			best = i;
			bestA = a;
			bestB = b;
		}
	};
	auto beyond = [&]( int i ) -> bool
	{
		const int oy2 = box[i].t + box[i].b;
		return ( dy > 0 ? oy2 > cy2 : oy2 < cy2 ) && !UI_CoFNavSameRow( box[i], c );
	};

	if( dx )
	{
		for( int i = 0; i < count; i++ )
		{
			if( !use[i] || i == cur )
				continue;

			const cofNavBox_t &o = box[i];
			const int ox2 = o.l + o.r;

			if(( dx > 0 && ox2 <= cx2 ) || ( dx < 0 && ox2 >= cx2 ) || UI_CoFNavSameColumn( o, c ))
				continue;
			if( !UI_CoFNavSameRow( o, c ) && ( edge[cur] || edge[i] || group[i] == group[cur] ))
				continue;

			better( i, 8L * UI_CoFNavGap( c.t, c.b, o.t, o.b ) + UI_CoFNavGap( c.l, c.r, o.l, o.r ),
				abs(( o.t + o.b ) - cy2 ));
		}
		return best;
	}

	if( edge[cur] )
	{
		// from the button row (or a header): the nearest control that way
		for( int i = 0; i < count; i++ )
		{
			if( use[i] && i != cur && beyond( i ))
				better( i, UI_CoFNavGap( c.t, c.b, box[i].t, box[i].b ) + 2L * abs( box[i].l - c.l ), i );
		}
		if( best >= 0 || !wrap )
			return best;

		// wrapping: the farthest control the other way
		for( int i = 0; i < count; i++ )
		{
			if( !use[i] || i == cur || UI_CoFNavSameRow( box[i], c ))
				continue;
			better( i, dy > 0 ? box[i].t : -box[i].b, UI_CoFNavGap( c.l, c.r, box[i].l, box[i].r ));
		}
		return best;
	}

	// up / down in the column
	for( int i = 0; i < count; i++ )
	{
		if( use[i] && i != cur && !edge[i] && group[i] == group[cur] && beyond( i ))
			better( i, UI_CoFNavGap( c.t, c.b, box[i].t, box[i].b ), abs( box[i].l - c.l ));
	}
	if( best >= 0 )
		return best;

	// past the column's end: the edge row that way (the button row)
	for( int i = 0; i < count; i++ )
	{
		if( use[i] && i != cur && edge[i] && beyond( i ))
			better( i, UI_CoFNavGap( c.t, c.b, box[i].t, box[i].b ) + 2L * abs( box[i].l - c.l ), i );
	}
	if( best >= 0 || !wrap )
		return best;

	// past everything: the other end of the same column
	for( int i = 0; i < count; i++ )
	{
		if( !use[i] || i == cur || edge[i] || group[i] != group[cur] || UI_CoFNavSameRow( box[i], c ))
			continue;
		better( i, dy > 0 ? box[i].t : -box[i].b, abs( box[i].l - c.l ));
	}
	return best;
}

/*
=================
UI_AdjustCursor

This functiont takes the given menu, the direction, and attempts to
adjust the menu's cursor so that it's at the next available slot
=================
*/
bool CMenuItemsHolder::AdjustCursor( int dir )
{
	CMenuBaseItem *item;
	bool wrapped = false;

wrap:
	while( m_iCursor >= 0 && m_iCursor < m_pItems.Count() )
	{
		item = m_pItems[m_iCursor];
		// Cry of Fear theme: a greyed control is skipped as well - focus on
		// it is invisible and does nothing (pad and arrow-key navigation)
		if( !item->IsVisible() || item->iFlags & (QMF_INACTIVE|QMF_MOUSEONLY) ||
			( UI_ThemeActive() && ( item->iFlags & QMF_GRAYED )))
		{
			m_iCursor += dir;
		}
		else break;
	}

	if( dir > 0 )
	{
		if( m_iCursor >= m_pItems.Count() )
		{
			if( wrapped )
			{
				m_iCursor = m_iCursorPrev;
				return false;
			}
			m_iCursor = 0;
			wrapped = true;
			goto wrap;
		}
	}
	else if( dir < 0 )
	{
		if( m_iCursor < 0 )
		{
			if( wrapped )
			{
				m_iCursor = m_iCursorPrev;
				return false;
			}
			m_iCursor = m_pItems.Count() - 1;
			wrapped = true;
			goto wrap;
		}
	}

	if( wrapped )
		return m_bWrapCursor;
	return true;
}

CMenuBaseItem *CMenuItemsHolder::ItemAtCursor()
{
	if( m_iCursor < 0 || m_iCursor >= m_pItems.Count() )
		return 0;

	// inactive items can't be has focus
	if( m_pItems[m_iCursor]->iFlags & QMF_INACTIVE )
		return 0;

	return m_pItems[m_iCursor];
}

CMenuBaseItem *CMenuItemsHolder::ItemAtCursorPrev()
{
	if( m_iCursorPrev < 0 || m_iCursorPrev >= m_pItems.Count() )
		return 0;

	// inactive items can't be has focus
	if( m_pItems[m_iCursorPrev]->iFlags & QMF_INACTIVE )
		return 0;

	return m_pItems[m_iCursorPrev];
}

CMenuBaseItem *CMenuItemsHolder::FindItemByTag(const char *tag)
{
	if( !tag )
		return NULL;

	if( this->szTag && !strcmp( this->szTag, tag ) )
		return this;

	FOR_EACH_VEC( m_pItems, i )
	{
		if( !m_pItems[i]->szTag )
			continue;

		if( !strcmp( m_pItems[i]->szTag, tag ) )
			return m_pItems[i];
	}

	return NULL;
}

void CMenuItemsHolder::SetCursorToItem(CMenuBaseItem &item, bool notify )
{
	FOR_EACH_VEC( m_pItems, i )
	{
		if( m_pItems[i] == &item )
		{
			SetCursor( i, notify );
			return;
		}
	}
}

void CMenuItemsHolder::CalcItemsPositions()
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->CalcPosition();
}

void CMenuItemsHolder::CalcItemsSizes()
{
	FOR_EACH_VEC( m_pItems, i )
		m_pItems[i]->CalcSizes();
}

void CMenuItemsHolder::SetCursor( int newCursor, bool notify )
{
	if( newCursor < 0 || newCursor >= m_pItems.Count() )
		return;

	if( !m_pItems[newCursor]->IsVisible() || (m_pItems[newCursor]->iFlags & (QMF_INACTIVE)) )
		return;

	m_iCursorPrev = m_iCursor;
	m_iCursor = newCursor;

	if( notify )
		CursorMoved();
}

void CMenuItemsHolder::CursorMoved()
{
	CMenuBaseItem *item;

	if( m_iCursor == m_iCursorPrev )
		return;

	if( m_iCursorPrev >= 0 && m_iCursorPrev < m_pItems.Count() )
	{
		item = m_pItems[m_iCursorPrev];

		item->_Event( QM_LOSTFOCUS );

		// unconditionally remove pressed state, because item without focus can't be pressed anymore
		item->m_bPressed = false;
	}

	if( m_iCursor >= 0 && m_iCursor < m_pItems.Count() )
	{
		item = m_pItems[m_iCursor];

		item->_Event( QM_GOTFOCUS );

		if( item == m_pItemAtCursorOnDown )
			item->m_bPressed = true; // restore pressed state
	}
}

void CMenuItemsHolder::AddItem(CMenuBaseItem &item)
{
	m_pItems.AddToTail( &item );
	item.m_pParent = this; // U OWNED
	item.iFlags &= ~(QMF_HASMOUSEFOCUS|QMF_HIDDENBYPARENT);

	item.Init();
}

void CMenuItemsHolder::RemoveItem(CMenuBaseItem &item)
{
	if( m_pItems.FindAndRemove( &item ) )
	{
		item.m_pParent = NULL;
	}
}

bool RES_ExpectString( char **data, const char *expect, bool skip = true )
{
	char token[1024];
	char *tmp;

	if( !data || !*data )
		return true;

	tmp = EngFuncs::COM_ParseFile( *data, token, sizeof( token ) );

	if( skip )
		*data = tmp;

	if( !*data )
		return true;

	if( !strncmp( token, expect, 1024 ) )
	{
		*data = tmp; // skip anyway, if expected string was here
		return true;
	}

	return false;
}

inline bool FreeFile( byte *pfile, const bool retval )
{
	EngFuncs::COM_FreeFile( pfile );
	return retval;
}

bool CMenuItemsHolder::LoadRES(const char *filename)
{
	byte *pfile = EngFuncs::COM_LoadFile( filename );
	char *afile = (char*)pfile;
	char token[1024];

	if( !pfile )
		return false;

	afile = EngFuncs::COM_ParseFile( afile, token, sizeof( token ) );

	Con_DPrintf( "Loading res file from %s, name %s\n", filename, token );

	if( !afile )
		return FreeFile( pfile, false );

	if( !RES_ExpectString( &afile, "{" ) )
	{
		Con_DPrintf( "LoadRES: failed to parse, want '{'\n" );
		return FreeFile( pfile, false );
	}

	do
	{
		CMenuBaseItem *item;

		afile = EngFuncs::COM_ParseFile( afile, token, sizeof( token ) );

		if( !afile )
			return FreeFile( pfile, false );

		item = FindItemByTag( token );

		if( !RES_ExpectString( &afile, "{" ))
		{
			Con_DPrintf( "LoadRES: failed to parse, want '{', near %s\n", token );
			return FreeFile( pfile, false );
		}

		if( item )
		{
			do
			{
				char key[1024];
				char value[1024];

				afile = EngFuncs::COM_ParseFile( afile, key, sizeof( key ));
				if( !afile )
					return FreeFile( pfile, false );

				afile = EngFuncs::COM_ParseFile( afile, value, sizeof( value ));
				if( !afile )
					return FreeFile( pfile, false );

				item->KeyValueData( key, value );
			}
			while( !RES_ExpectString( &afile, "}", false ) );
		}
		else
		{
			Con_DPrintf( "LoadRES: cannot find item %s, skipping!\n", token );

			while( !RES_ExpectString( &afile, "}" ) );
		}
	}
	while( !RES_ExpectString( &afile, "}", false ) );

	if( !RES_ExpectString( &afile, "}" ))
	{
		Con_DPrintf( "LoadRES: failed to parse, want '{'\n" );
		return FreeFile( pfile, false );
	}

	return FreeFile( pfile, true );
}

void CMenuItemsHolder::RegisterNamedEvent(CEventCallback ev, const char *name)
{
	ASSERT( name );
	ASSERT( ev );

	ev.szName = name;
	m_events.AddToTail( ev );
}

CEventCallback CMenuItemsHolder::FindEventByName(const char *name)
{
	for( int i = 0; i < m_events.Count(); i++ )
	{
		if( !strcmp( m_events[i].szName, name ))
			return m_events[i];
	}

	return CEventCallback();
}

bool CMenuItemsHolder::KeyValueData(const char *key, const char *data)
{
	return BaseClass::KeyValueData( key, data );
}

Point CMenuItemsHolder::GetPositionOffset() const
{
	return m_scPos;
}
