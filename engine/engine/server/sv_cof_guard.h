/*
sv_cof_guard.h - Cry of Fear story co-op: client commands the retail game trusts too much (sv_cof_guard.c)
GPL-3.0-or-later.

docs/research/2026-10-18-cof-decompilation-survey.md sections 4 and 7 (the launcher repo):
- `votekick <name>` / `vote 0|1` (CHalfLifeMultiplay): any player can start a kick vote; the immunity covers only
  authids this engine never returns, and one `vote` fills every free vote slot, so a single yes kicks a partner.
- `subtitleset N` and `hinttoggle` write server-wide cvars (cof_subtitlelanguage, sv_disablehints): a guest would
  change the host's own settings.
- `pause` (Xash's pausable 1; a guest's Backspace or Pause key is bound to it): a guest froze the host's game for 14 s
  (host log, 2026-10-20). Only the host pauses the party.
- `unfreeze 1024`: client.dll sends it at every viewport reset (map load, HUD_VidInit; CTelephone::Initialize), which
  let a player walk out of a scene a trigger_camera holds them in. While a camera holds the view it is not needed:
  the camera's end gives every living player control back (CTriggerCamera, triggers.cpp:5565-5583, 5890-5905).
- 1.1.9: c_asylumb's puzzle bars once the puzzle is solved, and a telephone dial that would answer another player.
- 1.2.5: `aitest` (client.cpp:1065: every monster stops) and `simonmode` (client.cpp:1629: spawns a book Simon, or
  kills the first monster_booksimon, c_ending4b's fight) are developer commands with no developer check: a guest's
  would change the host's game. The host keeps them, as the single-player console does.
Chat and names need nothing here: the network layer turns every '%' a client sends into '.' (net_buffer.c).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_GUARD_H
#define SV_COF_GUARD_H

#include <stdlib.h>
#include <string.h>

#if defined( _MSC_VER )
#define COF_GUARD_FN static __inline
#else
#define COF_GUARD_FN static inline
#endif

// Why a client's command must not reach the game (the game compares case-sensitively), or NULL.
COF_GUARD_FN const char *CoF_GuardRefusal( const char *cmd, int story, int host )
{
	if( !story || !cmd ) return NULL;
	if( !strcmp( cmd, "vote" ) || !strcmp( cmd, "votekick" )) return "vote";
	if( !host && ( !strcmp( cmd, "subtitleset" ) || !strcmp( cmd, "hinttoggle" ))) return "host-setting";
	if( !host && !strcmp( cmd, "pause" )) return "host-pause";
	if( !host && ( !strcmp( cmd, "aitest" ) || !strcmp( cmd, "simonmode" ))) return "host-developer";
	return NULL;
}

// A command refused while a trigger_camera holds this player's view (held), or NULL.
COF_GUARD_FN const char *CoF_GuardSceneRefusal( const char *cmd, const char *arg, int held )
{
	if( !held || !cmd || !arg ) return NULL;
	if( !strcmp( cmd, "unfreeze" ) && !strcmp( arg, "1024" )) return "scene-holds";
	return NULL;
}

// 1.1.9 (K9): c_asylumb's bar puzzle. Its bars' think fires restore_power whenever all five are closed and has no
// solved test (CPuzzleBar::BarCheck, CoF_PuzzleBars.cpp); its cof_puzzlebarbutton keeps iuser1 1 once solved. A
// partner still in the puzzle, or the console's puzzlebara/b/c, closed bars again and toggled the boiler room's
// lights, sound and steam back off. Once solved (solved), the bar commands are refused for the rest of the map;
// "boilercheck 5" (leave the puzzle, client.cpp:1905-1923) stays.
COF_GUARD_FN int CoF_GuardPuzzleCommand( const char *cmd )
{
	return cmd && ( !strcmp( cmd, "boilercheck" ) || !strcmp( cmd, "puzzlebara" ) || !strcmp( cmd, "puzzlebarb" ) || !strcmp( cmd, "puzzlebarc" ));
}

COF_GUARD_FN const char *CoF_GuardPuzzleRefusal( const char *cmd, const char *arg, int solved )
{
	if( !solved || !CoF_GuardPuzzleCommand( cmd )) return NULL;
	if( !strcmp( cmd, "boilercheck" ) && arg && atoi( arg ) == 5 ) return NULL;
	return "puzzle-solved";
}

// 1.1.9 (M2 F10, K): a cof_telephone answers its last opener only (CCofTelephone::Use keeps one user); a dial from
// a player whose telephone someone else opened since (taken) is refused: they pick it up again to dial.
COF_GUARD_FN int CoF_GuardTelephoneCommand( const char *cmd )
{
	return cmd && ( !strcmp( cmd, "telephoneright" ) || !strcmp( cmd, "telephonewrong" ));
}

COF_GUARD_FN const char *CoF_GuardTelephoneRefusal( const char *cmd, int taken )
{
	return taken && CoF_GuardTelephoneCommand( cmd ) ? "telephone-taken" : NULL;
}

#endif // SV_COF_GUARD_H
