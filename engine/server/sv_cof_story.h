/* Opt-in Cry of Fear story co-op adapter. GPL-3.0-or-later. */
#ifndef SV_COF_STORY_H
#define SV_COF_STORY_H
qboolean SV_CoF_StoryCoopEnabled( void );
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
// The co-op flashlight (sv_cof_light.c): cof_coop_flashlight, the host's choice, off by default. The client
// command hook drops a player's "flashlight" (true: handled); the frame switches off a torch not allowed.
void SV_CoF_LightInit( void );
void SV_CoF_LightFrame( void );
qboolean SV_CoF_CoopFlashlightAllowed( void );
qboolean SV_CoF_LightClientCommand( sv_client_t *cl );
// Small, verified changes to the retail hl.dll (sv_cof_gates.c), and whether players hurt each other.
void SV_CoF_GatesInit( void );
// Simon's voice (sv_cof_voice.c): the sample to send instead of a player's police sample (buf, or sample itself).
void SV_CoF_VoiceInit( void );
// hl.dll's one-player globals kept per player (sv_cof_globals.c).
void SV_CoF_GlobalsInit( void );
void SV_CoF_GlobalsEnter( sv_client_t *cl );
void SV_CoF_GlobalsLeave( sv_client_t *cl );
void SV_CoF_GlobalsFrame( void );
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
// Fog for every player, and per section in merged maps (sv_cof_fog.c): the game's message checked before it goes
// (true: held back), the replays after a reset or a section move.
qboolean SV_CoF_FogMessage( edict_t *to, int dest, const char *name, const byte *payload, int size );
void SV_CoF_FogFrame( void );
void SV_CoF_FogReset( void );
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
