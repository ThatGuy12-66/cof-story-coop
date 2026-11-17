/*
cof_hud_health.h - Cry of Fear: the remade health bar's maximum (cof_hud_remake.c)
GPL-3.0-or-later.

The game's HealthCap message (one byte) is a type: 0 none, 1 health held at 80, 2 held at 66 (hl.dll
CCofMaxHealthChange::Use writes it, CBasePlayer's PostThink clamps health to it). client.dll keeps its bar out of 100
and only swaps the bar's art (640_healthbar2/3.tga); docs/research/2026-10-18-cof-decompilation-survey.md 1.7, the
launcher repo.

Plain C: the offline tests share it.
*/
#ifndef COF_HUD_HEALTH_H
#define COF_HUD_HEALTH_H

#if defined( _MSC_VER )
#define COF_HUD_HEALTH_FN static __inline
#else
#define COF_HUD_HEALTH_FN static inline
#endif

COF_HUD_HEALTH_FN int CoF_HudHealthMaximum( int cap_type )
{
	return cap_type == 1 ? 80 : cap_type == 2 ? 66 : 100;
}

// How much of the bar is filled (0..1, out of 100 as in the game), never past the cap.
COF_HUD_HEALTH_FN float CoF_HudHealthFill( int health, int cap_type )
{
	int maximum = CoF_HudHealthMaximum( cap_type );
	if( health < 0 ) health = 0;
	if( health > maximum ) health = maximum;
	return health / 100.0f;
}

#endif // COF_HUD_HEALTH_H
