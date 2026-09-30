// SPDX-License-Identifier: GPL-3.0-or-later
#include "BaseMenu.h"
#include "BaseWindow.h"
#include "CoFOptions.h"
#include "SelectionPopup.h"
#include "Scissor.h"
#include "Utils.h"

bool UI_CoFSelectionLists()
{
    return UI_CoFOptionsLayout() && EngFuncs::GetCvarFloat( "ui_cof_dropdowns" ) != 0;
}

void UI_CoFDrawSelection(CMenuBaseItem *item,const char *value)
{
    const Point p=item->GetRenderPosition();const Size sz=item->GetRenderSize();
    const bool disabled=(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))!=0;
    const bool hot=!disabled && UI_THEME_HOT(item);
    const int tall=Q_max(1,(int)(THEME_BODY_TALL*uiStatic.scaleY));
    UI_DrawString(uiStatic.hThemeLabel,p.x,p.y-(int)(THEME_LABEL_TALL*1.5f*uiStatic.scaleY),sz.w,
        (int)(THEME_LABEL_TALL*uiStatic.scaleY),item->szName?item->szName:"",THEME_TEXT_DIM,
        (int)(THEME_LABEL_TALL*uiStatic.scaleY),QM_LEFT,ETF_FORCECOL|ETF_NOSIZELIMIT);
    UI_FillRect(p,sz,THEME_INPUT_BG);
    UI_DrawHairline(p,sz,disabled?THEME_DISABLED:(hot?THEME_ACCENT:THEME_INPUT_BORDER));
    const int gutter=sz.h,inset=gutter/3;
    UI_ThemeDrawTriangle(Point(p.x+sz.w-gutter+inset,p.y+inset),Size(gutter-inset*2,gutter-inset*2),3,disabled?THEME_DISABLED:THEME_TEXT);
    UI::Scissor::PushScissor(Point(p.x+4,p.y),Size(Q_max(1,sz.w-gutter-8),sz.h));
    UI_DrawString(uiStatic.hThemeBody,p.x+8,p.y,Q_max(1,sz.w-gutter-16),sz.h,value?value:"",
        disabled?THEME_DISABLED:THEME_TEXT,tall,QM_LEFT,ETF_FORCECOL|ETF_NO_WRAP);
    UI::Scissor::PopScissor();
}

// A top window owns input while open: no arrow, tab, mouse or cancel event
// reaches the underlying form. Only Finish(true) invokes its existing setter.
class CCoFSelectionPopup : public CMenuBaseWindow
{
public:
    CCoFSelectionPopup() : CMenuBaseWindow("Selection"), owner(NULL), count(0), selected(0), first(0), rows(0), armed(0) {}
    void Open(CMenuBaseItem *item,int n,int current,cofSelectionText_t text,cofSelectionCommit_t commit)
    {
        owner=item;count=n;selected=bound(0,current,n-1);first=0;armed=0;textFn=text;commitFn=commit;
        Link(item->Parent());Show();DisableTransition();
    }
    void VidInit() override { Layout(); }
    bool MouseMove(int x,int y) override { return true; }
    bool KeyDown(int key) override
    {
        if(UI::Key::IsEnter(key)||UI::Key::IsEscape(key)||UI::Key::IsLeftMouse(key)) { armed=key;return true; }
        if(UI::Key::IsUpArrow(key)||key==K_MWHEELUP) selected=Q_max(0,selected-1);
        else if(UI::Key::IsDownArrow(key)||key==K_MWHEELDOWN) selected=Q_min(count-1,selected+1);
        else if(UI::Key::IsHome(key)) selected=0;
        else if(UI::Key::IsEnd(key)) selected=count-1;
        Layout();return true;
    }
    bool KeyUp(int key) override
    {
        if(armed!=key)return true;
        armed=0;
        if(UI::Key::IsEscape(key)) Finish(false);
        else if(UI::Key::IsEnter(key)) Finish(true);
        else if(UI::Key::IsLeftMouse(key))
        {
            const int hit=Hit();
            if(hit>=0){selected=hit;Finish(true);}else Finish(false);
        }
        return true;
    }
    void Draw() override
    {
        if(!owner) return;
        if(!UI_CoFSelectionLists() || !owner->IsVisible() || (owner->iFlags&(QMF_GRAYED|QMF_INACTIVE))) { Finish(false);return; }
        for(CMenuItemsHolder *p=owner->Parent();p;p=p->Parent())
            if(!p->IsVisible() || (p->iFlags&QMF_CLOSING)) { Finish(false);return; }
        Layout();
        UI_FillRect(m_scPos,m_scSize,THEME_PANEL);
        UI_DrawHairline(m_scPos,m_scSize,THEME_ACCENT);
        UI::Scissor::PushScissor(m_scPos,m_scSize);
        for(int row=0;row<rows;row++)
        {
            const int index=first+row;
            Point p(m_scPos.x+2,m_scPos.y+row*rowHeight+2);
            Size sz(m_scSize.w-4,rowHeight-4);
            if(index==selected)UI_FillRect(p,sz,THEME_HOVER_FILL);
            char text[512];textFn(owner,index,text,sizeof(text));
            const int tall=Q_max(1,(int)(THEME_BODY_TALL*uiStatic.scaleY));
            UI_DrawString(uiStatic.hThemeBody,p.x+8,p.y,sz.w-24,sz.h,text,
                index==selected?THEME_TEXT_HI:THEME_TEXT,tall,QM_LEFT,ETF_FORCECOL|ETF_NO_WRAP);
        }
        UI::Scissor::PopScissor();
        const int mark=Q_max(2,rowHeight/6);
        if(first>0)UI_ThemeDrawTriangle(Point(m_scPos.x+m_scSize.w-mark*2,m_scPos.y+2),Size(mark,mark),2,THEME_TEXT_DIM);
        if(first+rows<count)UI_ThemeDrawTriangle(Point(m_scPos.x+m_scSize.w-mark*2,m_scPos.y+m_scSize.h-mark-2),Size(mark,mark),3,THEME_TEXT_DIM);
    }
private:
    void Layout()
    {
        if(!owner)return;
        Point p=owner->GetRenderPosition();Size sz=owner->GetRenderSize();
        const int screenW=ScreenWidth,screenH=ScreenHeight,pad=4;
        rowHeight=Q_max(18,sz.h);
        const int below=Q_max(0,screenH-pad-p.y-sz.h),above=Q_max(0,p.y-pad);
        const bool dropUp=below<Q_min(count,8)*rowHeight && above>below;
        rows=Q_max(1,Q_min(Q_min(count,8),(dropUp?above:below)/rowHeight));
        if(selected<first)first=selected;
        if(selected>=first+rows)first=selected-rows+1;
        first=bound(0,first,Q_max(0,count-rows));
        m_scSize=Size(Q_min(Q_max(sz.w,180),screenW-pad*2),rows*rowHeight);
        m_scPos=Point(bound(pad,p.x,screenW-pad-m_scSize.w),
            bound(pad,dropUp?p.y-m_scSize.h:p.y+sz.h,screenH-pad-m_scSize.h));
    }
    int Hit()
    {
        if(!UI_CursorInRect(m_scPos,m_scSize))return -1;
        return first+bound(0,(uiStatic.cursorY-m_scPos.y)/rowHeight,rows-1);
    }
    void Finish(bool commit)
    {
        CMenuBaseItem *item=owner;cofSelectionCommit_t apply=commitFn;const int choice=selected;
        owner=NULL;Hide();DisableTransition();
        if(item && item->Parent())
        {
            item->Parent()->SetCursorToItem(*item);
            item->iFlags |= QMF_HASKEYBOARDFOCUS;
        }
        if(commit && item && item->IsVisible() && !(item->iFlags&(QMF_GRAYED|QMF_INACTIVE))) apply(item,choice);
    }
    CMenuBaseItem *owner;
    int count,selected,first,rows,rowHeight,armed;
    cofSelectionText_t textFn;
    cofSelectionCommit_t commitFn;
};

void UI_CoFOpenSelection(CMenuBaseItem *owner,int count,int selected,cofSelectionText_t text,cofSelectionCommit_t commit)
{
    if(!owner || count<=0 || !text || !commit || (owner->iFlags&(QMF_GRAYED|QMF_INACTIVE)))return;
    static CCoFSelectionPopup popup;
    popup.Open(owner,count,selected,text,commit);
}
