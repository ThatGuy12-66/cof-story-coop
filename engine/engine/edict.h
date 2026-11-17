/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#ifndef EDICT_H
#define EDICT_H

#define MAX_ENT_LEAFS_32	24		// Orignally was 16
#define MAX_ENT_LEAFS_16	48

#include "progdefs.h"
#include "const.h"

#if defined(XASH_COF_ENTVARS_LEGACY) && XASH_COF_ENTVARS_LEGACY
#include <stddef.h>
#endif

struct edict_s
{
	qboolean		free;
	int		serialnumber;

	link_t		area;		// linked to a division node or leaf
	int		headnode;		// -1 to use normal leaf check

	int		num_leafs;
	union
	{
		int   leafnums32[MAX_ENT_LEAFS_32];
		short leafnums16[MAX_ENT_LEAFS_16];
	};

	float		freetime;		// sv.time when the object was freed

	void*		pvPrivateData;	// Alloced and freed by engine, used by DLLs
	entvars_t		v;		// C exported fields from progs

#if defined(XASH_COF_ENTVARS_LEGACY) && XASH_COF_ENTVARS_LEGACY
	/* Experimental CoF ABI: original field identity is unknown. */
	int			cof_legacy_edict_tail;
#endif

	// other fields from progs come immediately after
};

#if defined(XASH_COF_ENTVARS_LEGACY) && XASH_COF_ENTVARS_LEGACY
#define XASH_COF_EDICT_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
XASH_COF_EDICT_ASSERT(xash_cof_entvars_sequence_offset,
	offsetof(entvars_t, sequence) == 0x12C);
XASH_COF_EDICT_ASSERT(xash_cof_entvars_containing_offset,
	offsetof(entvars_t, pContainingEntity) == 0x20C);
XASH_COF_EDICT_ASSERT(xash_cof_edict_private_offset,
	offsetof(struct edict_s, pvPrivateData) == 0x7C);
XASH_COF_EDICT_ASSERT(xash_cof_edict_vars_offset,
	offsetof(struct edict_s, v) == 0x80);
XASH_COF_EDICT_ASSERT(xash_cof_edict_stride,
	sizeof(struct edict_s) == 0x32C);
#undef XASH_COF_EDICT_ASSERT
#endif

#endif//EDICT_H
