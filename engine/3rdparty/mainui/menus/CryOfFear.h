/*
CryOfFear.h -- Cry of Fear specific menu pages

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef MENU_CRYOFFEAR_H
#define MENU_CRYOFFEAR_H

// True when the running game directory is Cry of Fear. Everything this header
// declares is inert for every other game; the same runtime check the save
// integration uses (menus/LoadGame.cpp) so no build flag is needed.
bool UI_IsCryOfFear( void );

// Difficulty page: Easy / Medium / Difficult / Nightmare, the four skillsets
// of the original c_difficulty_settings panel. Entered for the base game or,
// through UI_CoFCampaign_Menu, for one of maps/*.custom.
void UI_CoFDifficulty_Menu( void );
void UI_CoFDifficultyForCampaign( const char *firstmap, const char *title );

void UI_CoFCampaign_Menu( void );
void UI_CoFLanguage_Menu( void );
void UI_CoFExtras_Menu( void );

// The Credits page: who made Cry of Fear, who makes this project, and who
// made the Polish localisation, each section with a link to its own page.
void UI_CoFCredits_Menu( void );

// The subtitle language, shared by the Language page and the Game options
// page. Index is 1-based, matching the game DLL's "subtitleset N".
int         UI_CoFSubtitleLanguageCount( void );
const char *UI_CoFSubtitleLanguageName( int index ); // 0-based, for a list model
int         UI_CoFGetSubtitleLanguage( void );
void        UI_CoFSetSubtitleLanguage( int index );

// The Unlockables page: the 34 flags Cry of Fear keeps in
// cryoffear/scriptsettings.dat, with a real status column. Each flag is one
// 20-byte line of that file, and client.dll carries the two possible tokens
// for it as a compiled-in pair; comparing the line to the pair is the whole
// decode. 27 of the 34 have a known name, the other 7 are listed unnamed.
// See docs/cof-ui-m3-theme.md and
// stage1/ui-data-research-20260921/unlockables-token-table.md.
void UI_CoFUnlockables_Menu( void );
int  UI_CoFUnlockableStatus( int index ); // -1 unknown, 0 locked, 1 unlocked

// Re-read cryoffear/scriptsettings.dat. Cheap (2 KiB) and done whenever a page
// that shows unlock state opens, so the menu never shows a stale answer after
// the player unlocks something and returns to the menu.
void UI_CoFUnlockablesReload( void );

// Line 83 of scriptsettings.dat, the Nightmare difficulty flag. Returns the
// same tri-state as UI_CoFUnlockableStatus; the difficulty page treats
// anything other than 1 as locked.
int  UI_CoFNightmareStatus( void );

// The old route: run the client's own entry command, which loads the
// c_unlockables gallery map. Kept as the page's "Open original gallery".
void UI_CoFUnlockables( void );

// The death page. Opened by the engine (command "menu_cofdeath") when the game
// DLL sends the GAME OVER user message - "VGUIMenu" with a first byte of 35 -
// which is the only signal Cry of Fear produces on death. See
// docs/cof-ui-death-flow.md.
void UI_CoFDeath_Menu( void );

// The co-op bridge (menus/CoFCoop.cpp): Co-op > Original co-op and Join co-op (Extras until 1.3.6).
// Host co-op starts a multiplayer listen server on a co-op entry map with the
// original settings.scr options and no "disconnect"; Join co-op connects by
// address. See docs/cof-coop-bridge.md.
void UI_CoFHost_Menu( void );
void UI_CoFJoin_Menu( void );

// 1.3.6: story co-op from the main menu (menus/CoFStoryCoop.cpp): the main list's Co-op item opens a second list
// and a CO-OP tabbed window: Host co-op, Join co-op (above), Continue co-op, Play with AI and Original co-op (the
// Host co-op page above). Each start sends what the Cry of Fear Co-op app sends (menus/CoFStoryCoopLogic.h).
void UI_CoFStoryHost_Menu( void );
void UI_CoFStorySolo_Menu( void );
void UI_CoFStoryContinue_Menu( void );

#endif // MENU_CRYOFFEAR_H
