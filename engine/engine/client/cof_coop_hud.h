/*
cof_coop_hud.h - Cry of Fear story co-op: client.dll's single-player assumptions in a multiplayer game (cof_coop_hud.c)
GPL-3.0-or-later.

- coop-hud: CHUDControl::solve (client.dll 1003BF20) shows the chat lines, the player list (name, health, ping), the
  follow and syringe icons, the name of the player looked at and "<name> died." only when the level name contains
  "cof_", and hides them every frame elsewhere (HUDControl_guess.cpp:1967 in the decompilation), so on the story's c_*
  maps chat reached only the console. The gate removes that test's jump (strstr returned NULL -> hide).
- save-labels: CSaveLoad::WriteSaveInfo (10032340, thiscall, ret 4) writes the tape recorder's slot label
  cryoffear/SAVE/saveinfoN.cof on the machine that clicked, before the save, so every co-op tape save overwrote that
  machine's single-player label while its cofsaveN.sav stayed: the single-player Load menu showed the co-op area and
  loaded an unrelated save. A multiplayer game cannot load those slots anyway (hl.dll's cofload is single player
  only); the gate returns at once.
- simon-arms: CStudioModelRenderer::GL_DrawPoints (10065EF0) dresses the first-person arms as the police officer of
  CoF's own co-op campaign in any multiplayer game (bMultiplayer = GetMaxClients() > 1, gl_studiomodel.cpp:1476 in the
  decompilation): police/police_sleeve.tga on the sleeves and police/police_hands.tga on the fingers, never the
  costume's. The gate drops that cmovg (10065F20), so bMultiplayer stays 0, its only two readers being the sleeve and
  finger textures (100668B9, 10066992): Simon's own arms, or the costume's, as in single player. Not on maps named
  cof_ (that campaign's police officers).
Research: docs/research/2026-10-18-cof-decompilation-survey.md sections 4 and 5 (the launcher repo).

Plain C (standard library only, with sv_cof_gates.h): the offline tests share it.
*/
#ifndef COF_COOP_HUD_H
#define COF_COOP_HUD_H

static const cof_gate_t cof_coop_hud_gate =
	{ "coop-hud", 0x3C682, 6, { 0x0F, 0x84, 0x17, 0x04, 0x00, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } };
static const cof_gate_t cof_save_label_gate =
	{ "save-labels", 0x32340, 3, { 0x55, 0x8B, 0xEC }, { 0xC2, 0x04, 0x00 } };

static const cof_gate_t cof_simon_arms_gate =
	{ "simon-arms", 0x65F20, 3, { 0x0F, 0x4F, 0xCA }, { 0x90, 0x90, 0x90 } };

#define COF_CLIENT_GATES 3
#define COF_CLIENT_GATE_SIMON_ARMS 2
static const cof_gate_t *const cof_client_gates[COF_CLIENT_GATES] = { &cof_coop_hud_gate, &cof_save_label_gate, &cof_simon_arms_gate };

// Only in a multiplayer game: single player keeps client.dll's own behaviour.
COF_GATES_FN int CoF_CoopHudWanted( int on, int maxclients )
{
	return on && maxclients > 1;
}

// A client gate in this game: the arms also keep the police officer's on CoF's own co-op maps (named cof_).
COF_GATES_FN int CoF_ClientGateWanted( int gate, int on, int maxclients, const char *map )
{
	if( !CoF_CoopHudWanted( on, maxclients )) return 0;
	return gate != COF_CLIENT_GATE_SIMON_ARMS || !map || !strstr( map, "cof_" );
}

#endif // COF_COOP_HUD_H
