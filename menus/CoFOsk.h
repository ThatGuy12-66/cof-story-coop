/*
CoFOsk.h -- Cry of Fear on-screen keyboard: a themed key grid for gamepad
players, for the menu's own text fields and, through the engine, for the
game's text fields (the computer login)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#pragma once
#ifndef COF_OSK_H
#define COF_OSK_H

class CMenuField;

// controls/Field.cpp: a field got the focus / had a key pressed on it. The
// keyboard opens on A, and on a focus change made by a pad direction key
// while a pad is the last input device. KeyDown returns true when it opened.
void UI_CoFOsk_FieldFocus( CMenuField *field );
bool UI_CoFOsk_FieldKey( CMenuField *field, int key );

// menus/CoFOptions.cpp input hooks (UI_CoFInputEvent, UI_CoFInputMouse)
void UI_CoFOsk_NoteKey( int key, int down );
void UI_CoFOsk_NoteMouse( void );

// the keyboard is on screen (controls/Framework.cpp leaves the page's own
// prompts line out while it is, the keyboard draws its own)
bool UI_CoFOsk_Active( void );

#endif // COF_OSK_H
