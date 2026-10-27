/* Opt-in Cry of Fear story co-op adapter. GPL-3.0-or-later. */
#ifndef SV_COF_STORY_H
#define SV_COF_STORY_H
qboolean SV_CoF_StoryCoopEnabled( void );
// 1.3.3: Cry of Fear's own co-op campaign, manhunt and survival maps (cof_*, c_cof_*), where the story layer stays off;
// and the multiplayer rules' allocation zeroed for every multiplayer game, before each map's entities spawn.
qboolean SV_CoF_StoryOfficialMap( const char *map );
void SV_CoF_StoryRulesGuard( void );
// The donator level every command carries to the game (cof_donator; engine/common/cof_abi.h).
int SV_CoF_DonatorLevel( void );
// While on, the game DLL's own messages are not sent (an AI companion dropped for a level change: its "has left").
void SV_CoF_QuietGameMessages( qboolean on );
qboolean SV_CoF_GameMessagesQuiet( void );
void SV_CoF_StoryInit( void );
void SV_CoF_StoryShutdown( void );
void SV_CoF_StoryFrame( void );
void SV_CoF_StoryUserinfo( const sv_client_t *cl, char *info, size_t size );
// Call a thiscall member of the verified retail DLL that takes one pointer.
qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg );
qboolean SV_CoF_StoryDeployWeapon( void *self );
// The verified game DLL's load address, or 0.
uintptr_t SV_CoF_StoryBase( void );
// Fire a target name through the game's own FireTargets (USE_TOGGLE).
qboolean SV_CoF_StoryFireTargets( const char *name, void *activator, void *caller );
// The bonus room switch (sv_cof_bonus.c): cof_bonus_room, and the server's
// LoadFileForMe, which shows the game scriptsettings.dat in the session's state.
void SV_CoF_BonusInit( void );
byte *SV_CoF_BonusLoadFile( const char *filename, int *pLength );
// After 1.1.9: the bonus room is open for this game (cof_bonus_room 1, or -1 and the file opens its door); a chapter
// start lays its items out beside the party's start (once, its first story frame); cof_wardrobe opens the wardrobe's
// menu for whoever presses it, and the Close of that menu goes no further (client commands: true when handled).
qboolean SV_CoF_BonusRoomOpen( void );
// 1.3: the bonus kit item's (sv_cof_bonus_kit.h) own unlock line reads unlocked for this game
qboolean SV_CoF_BonusItemUnlocked( int item );
void SV_CoF_BonusKitChapterStart( const char *chapter );
qboolean SV_CoF_BonusClientCommand( sv_client_t *cl );
// The co-op flashlight (sv_cof_light.c): cof_coop_flashlight, the host's choice, off by default. The client
// command hook drops a player's "flashlight" (true: handled); the frame switches off a torch not allowed.
void SV_CoF_LightInit( void );
void SV_CoF_LightFrame( void );
qboolean SV_CoF_CoopFlashlightAllowed( void );
qboolean SV_CoF_LightClientCommand( sv_client_t *cl );
// What a story level change leaves behind (sv_cof_arrival.c): at a carried player's sign-on their HideWeapon and
// everyone's ScoreInfo; the co-op torch as each player switched it (around the game's F, every frame); an exit the
// party just arrived beside is not taken by standing in it (pfnChangeLevel: true, refused).
// the level-change notice told again to a guest still signing on to the map left (sv_client.c)
extern int sv_cof_notice_epoch;
void SV_CoF_StoryNotices( void );
void SV_CoF_ArrivalReset( void );
void SV_CoF_ArrivalLanded( const sv_client_t *cl );
qboolean SV_CoF_ArrivalTouchCounts( const sv_client_t *cl );
void SV_CoF_ArrivalSignon( sv_client_t *cl );
void SV_CoF_ArrivalBegin( sv_client_t *cl );
void SV_CoF_ArrivalReady( const vec3_t landmark );
void SV_CoF_ArrivalFrame( void );
void SV_CoF_ArrivalMessage( const char *name, const byte *payload, int size );
void SV_CoF_ArrivalCommandBegin( sv_client_t *cl );
void SV_CoF_ArrivalCommandEnd( sv_client_t *cl );
qboolean SV_CoF_ArrivalRefuses( const char *level, const char *landmark );
// A player this level change carried over (restored here), and whether they are still loading it (not signed on).
void SV_CoF_ArrivalRestored( const sv_client_t *cl );
qboolean SV_CoF_ArrivalLoading( const sv_client_t *cl );
// The story's screen blur, one for the whole party (sv_cof_blur.c): cof_coop_shared_blur, on by default. Leaving
// a map by a story level change, before the level is saved; a checkpoint loaded (the party starts off).
void SV_CoF_BlurInit( void );
void SV_CoF_BlurFrame( void );
void SV_CoF_BlurLeaving( const char *map );
void SV_CoF_BlurCheckpointLoaded( void );
// Small, verified changes to the retail hl.dll (sv_cof_gates.c), and whether players hurt each other.
void SV_CoF_GatesInit( void );
// Simon's voice (sv_cof_voice.c): the sample to send instead of a player's police sample (buf, or sample itself).
void SV_CoF_VoiceInit( void );
// hl.dll's one-player globals kept per player (sv_cof_globals.c).
void SV_CoF_GlobalsInit( void );
void SV_CoF_GlobalsEnter( sv_client_t *cl );
void SV_CoF_GlobalsLeave( sv_client_t *cl );
void SV_CoF_GlobalsFrame( void );
// 1.2.6: an engine error inside a player's command (longjmp past GlobalsLeave) left the swap open for good: shut down.
void SV_CoF_GlobalsReset( void );
qboolean SV_CoF_RoomTypeGet( const char *name, float *value );
qboolean SV_CoF_RoomTypeSet( const char *name, float value );
// Scripted weather for every player (sv_cof_weather.c).
void SV_CoF_WeatherInit( void );
void SV_CoF_WeatherFrame( void );
void SV_CoF_WeatherReset( void );
// The camera models a finished model cutscene leaves, removed (sv_cof_camera_models.c).
void SV_CoF_CameraModelsInit( void );
void SV_CoF_CameraModelsFrame( void );
// A merged map's view distance, each section its own (sv_cof_view.c).
void SV_CoF_ViewInit( void );
void SV_CoF_ViewFrame( void );
// A merged map's 3D skies, each section its own (sv_cof_sky.c).
void SV_CoF_SkyInit( void );
void SV_CoF_SkyFrame( void );
void SV_CoF_SkyReset( void );
qboolean SV_CoF_SkyMessage( edict_t *to, int dest, const char *name, const byte *payload, int size );
qboolean SV_CoF_SkyHiddenFor( const edict_t *viewer, const edict_t *ent, int renderfx );
// A merged map: the game's RainData to a player is held back for their section's own (true).
qboolean SV_CoF_RainMessage( edict_t *to, int dest, const char *name, const byte *payload, int size );
qboolean SV_CoF_WeatherOn( void );
const char *SV_CoF_VoiceSample( edict_t *ent, const char *sample, int flags, char *buf, size_t size );
void SV_CoF_VoicePrecache( const char *name );
void SV_CoF_GatesFrame( void );
void SV_CoF_GatesClose( void );
qboolean SV_CoF_FriendlyFire( void );
qboolean SV_CoF_MeleeWalls( void ); // cof_sp_melee in story co-op
// Fog for every player, and per section in merged maps (sv_cof_fog.c): the game's message checked before it goes
// (true: held back), the replays after a reset or a section move.
qboolean SV_CoF_FogMessage( edict_t *to, int dest, const char *name, const byte *payload, int size );
void SV_CoF_FogFrame( void );
void SV_CoF_FogReset( void );
// A merged map's ambient loops for its own section's players (sv_cof_loops.c): the game's ambient sound before it
// goes (the section it is for; -1: as the engine sends it; COF_LOOP_HELD: not now, the sign-on), a save's copy of a
// loop the restored game plays again (true: left out), and the players' moves between sections.
#define COF_LOOP_HELD -2
int SV_CoF_LoopSound( edict_t *ent, const float *pos, const char *sample, float vol, float attn, int flags, int pitch );
qboolean SV_CoF_LoopRestored( edict_t *ent, int channel );
void SV_CoF_LoopsFrame( void );
// Client commands the retail game trusts too much (sv_cof_guard.c): true when refused.
qboolean SV_CoF_GuardClientCommand( sv_client_t *cl );
// What the others see in a player's hands (sv_cof_weaponmodel.c): the generated
// models are precached at activation, picked each frame and sent in the player's state.
void SV_CoF_WeaponModelActivate( void );
void SV_CoF_WeaponModelFrame( void );
void SV_CoF_WeaponModelState( entity_state_t *state, int e );
// The other hand of player entity e's dual pair as a follow entity for viewer; 0: none.
int SV_CoF_WeaponModelFollower( const sv_client_t *viewer, int e, entity_state_t *out );
// The light of what player entity e holds (lantern, lit flare, book laser) for viewer; 0: none.
int SV_CoF_WeaponModelLight( const sv_client_t *viewer, int e, entity_state_t *out );
void SV_CoF_WeaponModelInit( void );
#endif
