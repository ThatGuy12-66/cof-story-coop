/*
cof_entpatch.h - Cry of Fear language packs: entity patches (maps/<map>.entpatch)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#ifndef COF_ENTPATCH_H
#define COF_ENTPATCH_H

#include <stddef.h>

typedef struct cof_entpatch_stats_s
{
	int	blocks;		// blocks in the patch file
	int	edited;		// values replaced or appended
	int	removed;	// entities removed
	int	added;		// entities added
	int	skipped;	// blocks or keys not applied (each one is logged)
} cof_entpatch_stats_t;

// receives one line per skipped block or key, and the reason a whole patch
// was rejected (malformed base or patch)
typedef void (*cof_entpatch_log_t)( void *ctx, const char *msg );

// Apply an entity patch (format: docs/cof-language-pack-format.md, reference
// implementation scripts/polish/entpatch.py) to the entity string `base`.
// `base` may carry a trailing NUL, which is not part of the result. Returns
// a malloc'd, NUL-terminated result (free() it) with its length (without the
// NUL) in *outlen, or NULL when the base or the patch cannot be parsed.
// Standalone: plain C, no engine dependencies, so it can be unit-tested
// against the Python applier.
char *CoF_EntPatch_Apply( const char *base, size_t baselen, const char *patch, size_t patchlen,
	size_t *outlen, cof_entpatch_stats_t *stats, cof_entpatch_log_t log, void *ctx );

#endif // COF_ENTPATCH_H
