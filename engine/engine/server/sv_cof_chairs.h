/* Cry of Fear story co-op 1.3.4: everyone in a wheelchair at the end of Ending 4 (c_ending4b). GPL-3.0-or-later. */
#ifndef SV_COF_CHAIRS_H
#define SV_COF_CHAIRS_H
void SV_CoF_ChairsInit( void );
void SV_CoF_ChairsReset( void );
// Once a frame, after the rescue's (sv_cof_story.c): seats every person beside the first rider, ends every ride with
// the map's.
void SV_CoF_ChairsFrame( void );
// Anyone rides a wheelchair now: a checkpoint waits (a chair does not save its rider).
qboolean SV_CoF_ChairsRiding( void );
// Before a team wipe reloads: every rider of a living chair let out (sv_cof_rescue.c).
void SV_CoF_ChairsReleaseAll( void );
// The first player a book Simon's think should find (sv_cof_rescue.c): the rider whose chair is nearest, while it
// has nobody to fight yet; NULL otherwise.
edict_t *SV_CoF_ChairsSimonTarget( edict_t *simon );
#endif
