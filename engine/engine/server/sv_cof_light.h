/*
sv_cof_light.h - Cry of Fear story co-op: the co-op flashlight's rules (sv_cof_light.c)
GPL-3.0-or-later.

In multiplayer the game's "flashlight" command (F) gives a living player a free torch: it sets pev->frags to 32,
and every client.dll draws a light for a player whose frags has that bit (docs/research/
2026-10-12-companion-weapons-items-re.md section 3.1, the launcher repo). It is Cry of Fear's co-op mode; the
single-player story has no such torch: the light is Simon's phone, later the flashlight, the Glock's taclight, the
lantern and flares, all the game's own and untouched here. cof_coop_flashlight is the host's choice, off by default.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_LIGHT_H
#define SV_COF_LIGHT_H

#include <string.h>

#if defined( _MSC_VER )
#define COF_LIGHT_FN static __inline
#else
#define COF_LIGHT_FN static inline
#endif

#define COF_LIGHT_TORCH 32     // frags while the co-op torch is on (the command toggles 0 and 32)
#define COF_LIGHT_RETRY 1.0    // seconds between tries to switch one off (the game refuses it in a camera)

// Night vision (the bonus room's item_nightvision): m_iHeadShieldOn is 0 off, 1 on; F asks PostThink to switch it with
// 101 (on) or 100 (off). The game's F does it only in single player (client.cpp:2133-2152, hl.dll 1001C253).
#define COF_NV_OFF     0
#define COF_NV_ON      1
#define COF_NV_ASK_OFF 100
#define COF_NV_ASK_ON  101
#define COF_NV_LOCKED  666.0f  // m_flNextNightVisionTime while a scene holds the night vision

// A player's command dropped before the game sees it: the co-op torch, in story co-op, unless the host allows it.
COF_LIGHT_FN int CoF_LightRefused( const char *cmd, int story, int allowed )
{
	return story && !allowed && cmd && !strcmp( cmd, "flashlight" );
}

// A torch on although not allowed (a save from before, a host who changed their mind) is switched off with the
// game's own command, at most once every COF_LIGHT_RETRY seconds.
COF_LIGHT_FN int CoF_LightSwitchOff( float frags, int allowed, double now, double next_try )
{
	return !allowed && (int)frags == COF_LIGHT_TORCH && now >= next_try;
}

// The request F writes into m_iHeadShieldOn, as the game's single-player branch does, or -1: a switch under way, the
// game's delay (m_flNextNightVisionTime) not over, or a scene's lock.
COF_LIGHT_FN int CoF_NightVisionRequest( int state, float next, double now )
{
	if( !( (float)now > next ) || next == COF_NV_LOCKED ) return -1;
	if( state == COF_NV_ON ) return COF_NV_ASK_OFF;
	if( state == COF_NV_OFF ) return COF_NV_ASK_ON;
	return -1;
}

#endif // SV_COF_LIGHT_H
