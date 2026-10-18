/*
cof_coop_hud.h - Cry of Fear story co-op: client.dll's co-op HUD on the story maps (cof_coop_hud.c)
GPL-3.0-or-later.

CHUDControl::solve (client.dll 1003BF20) shows the chat lines, the player list (name, health, ping), the follow and
syringe icons, the name of the player looked at and "<name> died." only when the level name contains "cof_", and
hides them every frame elsewhere (HUDControl_guess.cpp:1967 in the decompilation), so on the story's c_* maps chat
reached only the console. The gate removes that test's jump (strstr returned NULL -> hide).
Research: docs/research/2026-10-18-cof-decompilation-survey.md section 4 (the launcher repo).

Plain C (standard library only, with sv_cof_gates.h): the offline tests share it.
*/
#ifndef COF_COOP_HUD_H
#define COF_COOP_HUD_H

static const cof_gate_t cof_coop_hud_gate =
	{ "coop-hud", 0x3C682, 6, { 0x0F, 0x84, 0x17, 0x04, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } };

// Only in a multiplayer game: single player keeps client.dll's own HUD.
COF_GATES_FN int CoF_CoopHudWanted( int on, int maxclients )
{
	return on && maxclients > 1;
}

#endif // COF_COOP_HUD_H
