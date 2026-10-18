/* cof_renderer_patch.h - Cry of Fear story co-op: client.dll's Paranoia renderer
 * limits raised. GPL-3.0-or-later.
 *
 * Win32 x86 only; included by cof_renderer_limits.c and by the offline harness,
 * which runs it against the real DLL. client.dll D2A04641...9C00 (image base
 * 0x10000000, ASLR-relocatable) draws the world and brush entities itself with
 * its own copy of the lightmaps (Paranoia 1.2 gl_lightmap.cpp UpdateLightmaps,
 * 1005A110): 64 pages of 128x128 RGB in three sets of GL names (base, bump
 * add-light from style 62, light vectors from style 63). A surface on page 64 or
 * more is skipped at build time and drawn with the next set's page: wrong light.
 * It also collects only brush entities at edict 1..899, at most 512 a frame, and
 * tells the engine not to draw the opaque ones itself.
 *
 * The patch: the page buffer, the chain array of the fewer-than-4-units path and
 * the brush-entity list move to storage the engine allocates; the bounds become
 * 256 pages (the engine's own MAX_LIGHTMAPS at 128 blocks), 2048 entities and
 * edict 8191; the three sets get GL names 0x03000000 + 256 * set (the engine
 * uses 1..8191, the client's counter starts at 0x02000000). 26 code edits, one
 * of them a jump to a 17-byte stub in int3 padding, and three data words; and
 * four more bounds: a particle system's source entity and Cg water's water
 * entities, up to edict 8191 (docs/research/2026-10-18-cof-decompilation-survey.md
 * section 3, the launcher repo).
 * Inventory and checks: evidence/map-merge-spike-20261016/client-renderer-patch-plan.md.
 *
 * Every edit compares its stock bytes first (absolute operands relocated to the
 * real base) and the patch is all or nothing. Lightmap blocks must stay 128x128
 * (no r_large_lightmaps, ENGINE_LARGE_LIGHTMAPS or BSP2): the client hard-codes
 * them. */
#ifndef COF_RENDERER_PATCH_H
#define COF_RENDERER_PATCH_H
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define COF_RP_PAGES_MAX   256                          /* lightmap pages per set */
#define COF_RP_PAGE_BYTES  ( COF_RP_PAGES_MAX * 128 * 128 * 3 )
#define COF_RP_CHAIN_BYTES ( COF_RP_PAGES_MAX * 4 )
#define COF_RP_ENTITIES    2048                         /* brush entities a frame */
#define COF_RP_ENTITY_BYTES ( COF_RP_ENTITIES * 4 )
#define COF_RP_LINKED      0x10000000u                  /* the image base the absolute operands were linked for */
#define COF_RP_SET1        0x1CBF70                     /* RVA of the three set bases (GL names) */
#define COF_RP_SITES       30

enum { COF_RP_NONE, COF_RP_PAGES, COF_RP_PAGES2, COF_RP_CHAINS, COF_RP_ENTLIST }; /* what a patched operand points at */

typedef struct
{
	uint32_t rva;
	uint8_t len;
	uint8_t at;                  /* offset of a 4-byte absolute operand in stock (and code), when there is one */
	uint8_t reloc;               /* stock has an absolute operand at `at` (relocated by the loader) */
	uint8_t target;              /* code's operand at `at`: COF_RP_NONE (code as written) or new storage */
	unsigned char stock[17];     /* absolute operands as linked at 0x10000000 */
	unsigned char code[17];
} cof_rp_site_t;

#define RP_ABS( a ) (( a ) & 0xFFu ), ((( a ) >> 8 ) & 0xFFu ), ((( a ) >> 16 ) & 0xFFu ), ((( a ) >> 24 ) & 0xFFu )
#define RP_PB  0x101CC098u   /* the old page buffer, 64 * 128 * 128 * 3 bytes */
#define RP_CH  0x104CDE80u   /* the old chain array, 64 dwords */
#define RP_EL  0x104CD540u   /* the old brush-entity list, 512 dwords */
#define RP_NOP5 0x90, 0x90, 0x90, 0x90, 0x90

static const cof_rp_site_t cof_rp_sites[COF_RP_SITES] =
{
	/* UpdateLightmaps: clear, bound, block copy, upload */
	{ 0x5A25C, 5, 0, 0, COF_RP_NONE, { 0x68, 0x00, 0x00, 0x30, 0x00 }, { 0x68, 0x00, 0x00, 0xC0, 0x00 } },           /* push 0x300000 -> 0xC00000 (memset size) */
	{ 0x5A263, 5, 1, 1, COF_RP_PAGES, { 0x68, RP_ABS( RP_PB ) }, { 0x68 } },                                           /* push pages (memset) */
	{ 0x5A30A, 5, 0, 0, COF_RP_NONE, { 0x83, 0xF9, 0x40, 0x7C, 0x14 }, { 0x80, 0xFD, 0x00, 0x74, 0x14 } },           /* cmp ecx,64 / jl -> cmp ch,0 / je */
	{ 0x5A402, 6, 0, 0, COF_RP_NONE, { 0x83, 0x78, 0x38, 0x40, 0x7D, 0x20 }, { 0x80, 0x78, 0x39, 0x00, 0x75, 0x20 } }, /* pass 2: [surf+38h] < 256 */
	{ 0x5A470, 6, 0, 0, COF_RP_NONE, { 0x83, 0x78, 0x38, 0x40, 0x7D, 0x1B }, { 0x80, 0x78, 0x39, 0x00, 0x75, 0x1B } }, /* pass 3 */
	{ 0x5A524, 7, 3, 1, COF_RP_PAGES2, { 0x8D, 0x3C, 0x45, RP_ABS( RP_PB + 2 ) }, { 0x8D, 0x3C, 0x45 } },             /* lea edi,[eax*2+pages+2] */
	{ 0x5A589, 5, 1, 1, COF_RP_PAGES, { 0xBF, RP_ABS( RP_PB ) }, { 0xBF } },                                           /* mov edi,pages (upload) */
	/* the GL name allocator (once, CHud init) no longer stores the set bases: the engine writes them */
	{ 0x5A0B5, 5, 1, 1, COF_RP_NONE, { 0xA3, RP_ABS( 0x101CBF70u ) }, { RP_NOP5 } },
	{ 0x5A0BD, 5, 1, 1, COF_RP_NONE, { 0xA3, RP_ABS( 0x101CBF74u ) }, { RP_NOP5 } },
	{ 0x5A0C5, 5, 1, 1, COF_RP_NONE, { 0xA3, RP_ABS( 0x101CBF78u ) }, { RP_NOP5 } },
	/* the chain array of the fewer-than-4-texture-units path */
	{ 0x62326, 5, 0, 0, COF_RP_NONE, { 0xB9, 0x40, 0x00, 0x00, 0x00 }, { 0xB9, 0x00, 0x01, 0x00, 0x00 } },           /* rep stosd count 64 -> 256 */
	{ 0x6232D, 5, 1, 1, COF_RP_CHAINS, { 0xBF, RP_ABS( RP_CH ) }, { 0xBF } },
	{ 0x60D43, 7, 3, 1, COF_RP_CHAINS, { 0x8B, 0x04, 0x8D, RP_ABS( RP_CH ) }, { 0x8B, 0x04, 0x8D } },
	{ 0x60D4D, 7, 3, 1, COF_RP_CHAINS, { 0x89, 0x14, 0x8D, RP_ABS( RP_CH ) }, { 0x89, 0x14, 0x8D } },                 /* the unchecked chain write */
	{ 0x62750, 7, 3, 1, COF_RP_CHAINS, { 0x8B, 0x34, 0xBD, RP_ABS( RP_CH ) }, { 0x8B, 0x34, 0xBD } },
	{ 0x62781, 5, 0, 0, COF_RP_NONE, { 0x83, 0xFF, 0x40, 0x7C, 0xCA }, { 0xE9, 0x39, 0x08, 0x00, 0x00 } },           /* cmp edi,64 / jl -> jmp stub */
	{ 0x62FBF, 17, 0, 0, COF_RP_NONE,                                                                                  /* stub: cmp edi,256 / jl 10062750 / jmp 10062786 */
		{ 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC },
		{ 0x81, 0xFF, 0x00, 0x01, 0x00, 0x00, 0x0F, 0x8C, 0x85, 0xF7, 0xFF, 0xFF, 0xE9, 0xB6, 0xF7, 0xFF, 0xFF } },
	{ 0x62792, 7, 3, 1, COF_RP_CHAINS, { 0x8B, 0x3C, 0x9D, RP_ABS( RP_CH ) }, { 0x8B, 0x3C, 0x9D } },
	{ 0x627CB, 5, 0, 0, COF_RP_NONE, { 0x83, 0xFB, 0x40, 0x7C, 0xC2 }, { 0x80, 0xFF, 0x01, 0x75, 0xC2 } },           /* cmp ebx,64 / jl -> cmp bh,1 / jne (ebx counts 0..256) */
	/* the brush-entity list: 512 -> 2048 a frame, edicts 1..899 -> 1..8191 */
	{ 0x60ACE, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFF, 0x00, 0x02, 0x00, 0x00 }, { 0x81, 0xFF, 0x00, 0x08, 0x00, 0x00 } },
	{ 0x5B4F2, 7, 3, 1, COF_RP_ENTLIST, { 0xFF, 0x34, 0xB5, RP_ABS( RP_EL ) }, { 0xFF, 0x34, 0xB5 } },
	{ 0x5B509, 7, 3, 1, COF_RP_ENTLIST, { 0x8B, 0x3C, 0xB5, RP_ABS( RP_EL ) }, { 0x8B, 0x3C, 0xB5 } },
	{ 0x5EF70, 7, 3, 1, COF_RP_ENTLIST, { 0x8B, 0x3C, 0xB5, RP_ABS( RP_EL ) }, { 0x8B, 0x3C, 0xB5 } },
	{ 0x5EFF0, 7, 3, 1, COF_RP_ENTLIST, { 0x8B, 0x3C, 0xB5, RP_ABS( RP_EL ) }, { 0x8B, 0x3C, 0xB5 } },
	{ 0x60AE3, 7, 3, 1, COF_RP_ENTLIST, { 0x89, 0x34, 0xBD, RP_ABS( RP_EL ) }, { 0x89, 0x34, 0xBD } },
	{ 0x60AF5, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFB, 0x84, 0x03, 0x00, 0x00 }, { 0x81, 0xFB, 0x00, 0x20, 0x00, 0x00 } },
	/* a particle system's source entity (UTIL_GetClientEntityWithServerIndex 100831E0): edicts 1..899 -> 1..8191; the
	 * scan still stops at the first index the engine has no entity for */
	{ 0x83210, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFE, 0x84, 0x03, 0x00, 0x00 }, { 0x81, 0xFE, 0x00, 0x20, 0x00, 0x00 } },
	/* Cg water's three loops over the water entities (CgWater_guess.cpp:240, 322, 507): edicts 0..1023 -> 0..8191 */
	{ 0x87047, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFE, 0x00, 0x04, 0x00, 0x00 }, { 0x81, 0xFE, 0x00, 0x20, 0x00, 0x00 } },
	{ 0x8718B, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFE, 0x00, 0x04, 0x00, 0x00 }, { 0x81, 0xFE, 0x00, 0x20, 0x00, 0x00 } },
	{ 0x878FB, 6, 0, 0, COF_RP_NONE, { 0x81, 0xFE, 0x00, 0x04, 0x00, 0x00 }, { 0x81, 0xFE, 0x00, 0x20, 0x00, 0x00 } },
};

/* GL names of the three lightmap sets, 256 apart */
static const uint32_t cof_rp_sets[3] = { 0x03000000u, 0x03000100u, 0x03000200u };

typedef struct
{
	unsigned char *base;                       /* the patched module, NULL when not patched */
	unsigned char saved[COF_RP_SITES][17];     /* each site's bytes before */
	uint32_t saved_sets[3];
} cof_render_patch_t;

/* The bytes a site holds in a module loaded at `base` (stock) or should hold after the patch. */
static void CoF_RP_Expected( const cof_rp_site_t *s, unsigned char *base, int patched, const uint32_t *storage, unsigned char *out )
{
	uint32_t value;
	memcpy( out, patched ? s->code : s->stock, s->len );
	if( patched ? s->target == COF_RP_NONE : !s->reloc ) return;
	if( patched ) value = storage[s->target];
	else
	{
		memcpy( &value, s->stock + s->at, 4 );
		value = value - COF_RP_LINKED + (uint32_t)(uintptr_t)base;
	}
	memcpy( out + s->at, &value, 4 );
}

static int CoF_RP_Write( unsigned char *p, const void *bytes, size_t len )
{
	DWORD protect, ignored;
	if( !VirtualProtect( p, len, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, bytes, len );
	FlushInstructionCache( GetCurrentProcess( ), p, len );
	VirtualProtect( p, len, protect, &ignored );
	return 1;
}

/* Patch the module at `base` to use `pages` (COF_RP_PAGE_BYTES), `chains`
 * (COF_RP_CHAIN_BYTES) and `entities` (COF_RP_ENTITY_BYTES). Before the client's
 * HUD init (the set bases still zero). 1 when patched; 0 and nothing written
 * when already patched or any byte differs from the stock DLL. */
static int CoF_RenderPatchApply( cof_render_patch_t *rp, unsigned char *base, void *pages, void *chains, void *entities )
{
	uint32_t storage[5], sets[3];
	unsigned char want[17];
	int i;
	if( rp->base || !base || !pages || !chains || !entities ) return 0;
	storage[COF_RP_NONE] = 0;
	storage[COF_RP_PAGES] = (uint32_t)(uintptr_t)pages;
	storage[COF_RP_PAGES2] = (uint32_t)(uintptr_t)pages + 2;
	storage[COF_RP_CHAINS] = (uint32_t)(uintptr_t)chains;
	storage[COF_RP_ENTLIST] = (uint32_t)(uintptr_t)entities;
	for( i = 0; i < COF_RP_SITES; i++ )
	{
		CoF_RP_Expected( &cof_rp_sites[i], base, 0, storage, want );
		if( memcmp( base + cof_rp_sites[i].rva, want, cof_rp_sites[i].len )) return 0;
	}
	memcpy( sets, base + COF_RP_SET1, sizeof( sets ));
	if( sets[0] || sets[1] || sets[2] ) return 0; /* the allocator already ran: too late */
	for( i = 0; i < COF_RP_SITES; i++ )
	{
		const cof_rp_site_t *s = &cof_rp_sites[i];
		memcpy( rp->saved[i], base + s->rva, s->len );
		CoF_RP_Expected( s, base, 1, storage, want );
		if( !CoF_RP_Write( base + s->rva, want, s->len ))
		{
			while( --i >= 0 ) CoF_RP_Write( base + cof_rp_sites[i].rva, rp->saved[i], cof_rp_sites[i].len );
			return 0;
		}
	}
	memcpy( rp->saved_sets, sets, sizeof( sets ));
	CoF_RP_Write( base + COF_RP_SET1, cof_rp_sets, sizeof( cof_rp_sets ));
	rp->base = base;
	return 1;
}

/* Put every byte back (before the module is freed, or in a test). */
static void CoF_RenderPatchRemove( cof_render_patch_t *rp )
{
	int i;
	if( !rp->base ) return;
	for( i = 0; i < COF_RP_SITES; i++ )
		CoF_RP_Write( rp->base + cof_rp_sites[i].rva, rp->saved[i], cof_rp_sites[i].len );
	CoF_RP_Write( rp->base + COF_RP_SET1, rp->saved_sets, sizeof( rp->saved_sets ));
	rp->base = NULL;
}

#undef RP_ABS
#undef RP_PB
#undef RP_CH
#undef RP_EL
#undef RP_NOP5
#endif /* COF_RENDERER_PATCH_H */
