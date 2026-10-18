/*
sv_cof_gates.h - Cry of Fear story co-op: small, verified changes to the retail hl.dll (sv_cof_gates.c)
GPL-3.0-or-later.

Each gate replaces a few bytes at an RVA of the hash-verified hl.dll (SV_CoF_StoryBase) while story co-op runs and
its cvar wants it, and only where the bytes are exactly the retail ones (or already ours); they go back when story
co-op ends. Research: docs/research/2026-10-18-cof-decompilation-survey.md (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_GATES_H
#define SV_COF_GATES_H

#if defined( _MSC_VER )
#define COF_GATES_FN static __inline
#else
#define COF_GATES_FN static inline
#endif

#define COF_GATE_MAX 16

typedef struct
{
	const char *name;
	unsigned int rva;
	int size;
	byte original[COF_GATE_MAX];
	byte changed[COF_GATE_MAX];
} cof_gate_t;

enum
{
	// CHalfLifeMultiplay::FPlayerCanTakeDamage (100C8700) refuses a player's damage only to a victim without
	// FL_FAKECLIENT: humans hurt the AI companions, and the companions each other. Without the jne after the flag
	// test, a player's damage is refused to any player (cof_coop_friendly_fire 0).
	COF_GATE_FRIENDLY_FIRE,
	COF_GATES
};

static const cof_gate_t cof_gates[COF_GATES] =
{
	{ "friendly-fire", 0xC8722, 2, { 0x75, 0x06 }, { 0x90, 0x90 } },
};

// Brings one gate to the wanted state through write (which changes executable bytes); 1 when it is in that state now,
// 0 when the bytes are neither the retail ones nor ours (another DLL build: left alone).
COF_GATES_FN int CoF_GateSet( byte *base, const cof_gate_t *g, int on, int ( *write )( byte *p, const byte *value, int size ))
{
	byte *p = base + g->rva;
	const byte *want = on ? g->changed : g->original, *other = on ? g->original : g->changed;
	if( !memcmp( p, want, g->size )) return 1;
	if( memcmp( p, other, g->size )) return 0;
	return write( p, want, g->size );
}

#endif // SV_COF_GATES_H
