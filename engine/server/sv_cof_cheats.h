/*
sv_cof_cheats.h - Cry of Fear: restored cheats (engine side)
Copyright (C) 2026 cofenhanced

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef SV_COF_CHEATS_H
#define SV_COF_CHEATS_H

// SV_ExecuteClientCommand: called with the command already tokenized, before
// the engine's own user commands. Returns true when the command was consumed
// (noclip, notarget, fly, give and the cof_* cheats while they are allowed),
// false to let the stock path run.
qboolean SV_CoF_CheatsClientCommand( sv_client_t *cl );

// SV_RunCmd: called right after the game DLL's PlayerPostThink. Re-asserts the
// latched cheats the retail game resets every frame.
void SV_CoF_CheatsPostThink( sv_client_t *cl );

// Shared validation for the opt-in story adapter. These do not enable cheats.
qboolean SV_CoF_RetailDLLVerified( void );
byte *SV_CoF_StoryEntityPrivate( edict_t *ent, size_t need );
qboolean SV_CoF_StoryGiveItem( sv_client_t *cl, const char *classname );
qboolean SV_CoF_StoryGiveItemEx( sv_client_t *cl, const char *classname, const char *message );
void SV_CoF_ClearValidation( void );

#endif // SV_COF_CHEATS_H
