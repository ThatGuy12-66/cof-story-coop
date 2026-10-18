/*
sv_cof_guard.h - Cry of Fear story co-op: client commands the retail game trusts too much (sv_cof_guard.c)
GPL-3.0-or-later.

docs/research/2026-10-18-cof-decompilation-survey.md sections 4 and 7 (the launcher repo):
- `votekick <name>` / `vote 0|1` (CHalfLifeMultiplay): any player can start a kick vote; the immunity covers only
  authids this engine never returns, and one `vote` fills every free vote slot, so a single yes kicks a partner.
- `subtitleset N` and `hinttoggle` write server-wide cvars (cof_subtitlelanguage, sv_disablehints): a guest would
  change the host's own settings.
Chat and names need nothing here: the network layer turns every '%' a client sends into '.' (net_buffer.c).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_GUARD_H
#define SV_COF_GUARD_H

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
	return NULL;
}

#endif // SV_COF_GUARD_H
