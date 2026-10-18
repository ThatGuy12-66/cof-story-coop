/* Cry of Fear story co-op: merged story maps on the server (sv_cof_sections.c). GPL-3.0-or-later. */
#ifndef SV_COF_SECTIONS_H
#define SV_COF_SECTIONS_H
void SV_CoF_SectionsInit( void );
void SV_CoF_SectionsFrame( void );
// pfnChangeLevel, before the exit hold: a change between two sections of the loaded merged map is an instant
// move of the players in the exit and those beside them. True when it was one (handled).
qboolean SV_CoF_SectionChangeLevel( const char *level, const char *landmark );
// SV_QueueChangeLevel: the map file to load and the landmark's name there, for an original map name (a member of
// a merged map when merging is on, or a door out of the loaded merged map). False: load level as asked.
qboolean SV_CoF_SectionsResolve( const char *level, const char *landmark, char *file, size_t filesize, char *dest, size_t destsize );
// The story transition of a resolved request: the landmark's live name where it was asked and the original map
// the exit names (else landmark and map themselves).
const char *SV_CoF_TransitionSourceLandmark( const char *map, const char *landmark );
const char *SV_CoF_TransitionRequestedLevel( const char *map, const char *landmark );
// The map command (and chapter starts): a member of a merged map loads the merged file, starting in its section.
void SV_CoF_SectionsMapCommand( char *mapname, size_t size );
// SV_ExecLoadLevel, after the entities spawned: the section a map command asked for is where players start.
void SV_CoF_SectionsEntry( void );
// merged.txt of a story save (the session's mode, the first player's original map), and restoring it at load.
size_t SV_CoF_SectionsSessionRecord( char *out, size_t size );
void SV_CoF_SectionsSessionRestore( const byte *record, size_t length );
// The section a sound is for: a "play everywhere" one goes to its section's players only (-1: everyone).
int SV_CoF_SoundSection( float attn, int flags, const vec3_t origin );
// The section of the loaded merged map at this player (-1: the map is not merged).
int SV_CoF_PlayerSection( const edict_t *ent );
// The section of the loaded merged map at this point (-1: the map is not merged).
int SV_CoF_PointSection( const vec3_t origin );
// The view distance of a section's own map (its worldspawn MaxRange), or 0 when the map is not merged.
float SV_CoF_SectionMaxRange( int section );
#endif
