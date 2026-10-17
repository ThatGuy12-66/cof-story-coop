/*
cof_merged.h - Cry of Fear story co-op: merged story maps (cof_sections.c, cof_sections.h)
GPL-3.0-or-later.
*/
#ifndef COF_MERGED_H
#define COF_MERGED_H

#include "cof_sections.h"

qboolean COM_CoF_IsMergedMap( const char *map );
qboolean COM_CoF_LoadSections( const char *map, cof_sections_t *out );
char *COM_CoF_MergedEntities( const char *map, const char *world, size_t worldsize, size_t *outsize );
// A [cof-sections] log line that reaches the log even while the console is still off (map loads at start-up).
void COM_CoF_SectionsNote( const char *fmt, ... ) FORMAT_CHECK( 1 );

#endif // COF_MERGED_H
