// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COF_SELECTION_POPUP_H
#define COF_SELECTION_POPUP_H
class CMenuBaseItem;
bool UI_CoFSelectionLists();
void UI_CoFDrawSelection( CMenuBaseItem *, const char * );
typedef void (*cofSelectionText_t)( CMenuBaseItem *, int, char *, int );
typedef void (*cofSelectionCommit_t)( CMenuBaseItem *, int );
void UI_CoFOpenSelection( CMenuBaseItem *, int, int, cofSelectionText_t, cofSelectionCommit_t );
#endif
