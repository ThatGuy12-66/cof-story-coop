/* Opt-in Cry of Fear story co-op adapter. GPL-3.0-or-later. */
#ifndef SV_COF_STORY_H
#define SV_COF_STORY_H
qboolean SV_CoF_StoryCoopEnabled( void );
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
// What the others see in a player's hands (sv_cof_weaponmodel.c): the generated
// models are precached at activation, picked each frame and sent in the player's state.
void SV_CoF_WeaponModelActivate( void );
void SV_CoF_WeaponModelFrame( void );
void SV_CoF_WeaponModelState( entity_state_t *state, int e );
#endif
