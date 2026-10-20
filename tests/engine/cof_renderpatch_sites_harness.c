/* client.dll's Paranoia renderer: the 64 -> 256 lightmap page patch and the
 * brush-entity limits (cof_renderer_patch.h), against the real retail
 * client.dll, mapped (never initialised) by the Windows loader at a base other
 * than its preferred one, as ASLR may load it. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "patch_under_test.h"

static void Poke( unsigned char *p, unsigned char value )
{
	DWORD protect, ignored;
	assert( VirtualProtect( p, 1, PAGE_EXECUTE_READWRITE, &protect ));
	*p = value;
	VirtualProtect( p, 1, protect, &ignored );
}

static size_t ImageSize( unsigned char *base )
{
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)( base + dos->e_lfanew );
	return nt->OptionalHeader.SizeOfImage;
}

static unsigned Abs32( const unsigned char *p )
{
	unsigned v;
	memcpy( &v, p, 4 );
	return v;
}

int main( int argc, char **argv )
{
	cof_render_patch_t patch;
	unsigned char *base, *image;
	unsigned char *pages, *chains, *ents;
	size_t size;
	void *taken;
	int i, j;
	assert( argc == 2 );
	memset( &patch, 0, sizeof( patch ));
	taken = VirtualAlloc( (void *)0x10000000, 0x1000, MEM_RESERVE, PAGE_NOACCESS ); /* push the DLL off its preferred base */
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base );
	assert( !taken || base != (unsigned char *)0x10000000 );
	size = ImageSize( base );
	image = (unsigned char *)malloc( size );
	assert( image );
	memcpy( image, base, size );
	pages = (unsigned char *)VirtualAlloc( NULL, COF_RP_PAGE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	chains = (unsigned char *)VirtualAlloc( NULL, COF_RP_CHAIN_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	ents = (unsigned char *)VirtualAlloc( NULL, COF_RP_ENTITY_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	assert( pages && chains && ents );

	/* the table: 31 code edits and three data words */
	assert( COF_RP_SITES == 31 );

	/* applied: every site holds its new code, with the real addresses of the new storage */
	assert( CoF_RenderPatchApply( &patch, base, pages, chains, ents ));
	assert( !CoF_RenderPatchApply( &patch, base, pages, chains, ents )); /* once */
	for( i = 0; i < COF_RP_SITES; i++ )
	{
		const cof_rp_site_t *s = &cof_rp_sites[i];
		unsigned char *p = base + s->rva;
		for( j = 0; j < s->len; j++ )
		{
			if( s->target != COF_RP_NONE && j >= s->at && j < s->at + 4 ) continue;
			assert( p[j] == s->code[j] );
		}
		if( s->target == COF_RP_PAGES ) assert( Abs32( p + s->at ) == (unsigned)(size_t)pages );
		if( s->target == COF_RP_PAGES2 ) assert( Abs32( p + s->at ) == (unsigned)(size_t)pages + 2 );
		if( s->target == COF_RP_CHAINS ) assert( Abs32( p + s->at ) == (unsigned)(size_t)chains );
		if( s->target == COF_RP_ENTLIST ) assert( Abs32( p + s->at ) == (unsigned)(size_t)ents );
	}
	/* the three lightmap sets 256 GL names apart, clear of the engine's and the client's other names */
	assert( Abs32( base + COF_RP_SET1 ) == 0x03000000u );
	assert( Abs32( base + COF_RP_SET1 + 4 ) == 0x03000100u );
	assert( Abs32( base + COF_RP_SET1 + 8 ) == 0x03000200u );
	/* a few sites decoded by hand: the bounds and the stub */
	assert( !memcmp( base + 0x5A30A, "\x80\xFD\x00\x74\x14", 5 ));           /* cmp ch,0 / je: page < 256 */
	assert( !memcmp( base + 0x62781, "\xE9\x39\x08\x00\x00", 5 ));           /* jmp to the stub */
	assert( !memcmp( base + 0x62FBF, "\x81\xFF\x00\x01\x00\x00\x0F\x8C\x85\xF7\xFF\xFF\xE9\xB6\xF7\xFF\xFF", 17 ));
	assert( !memcmp( base + 0x60AF5, "\x81\xFB\x00\x20\x00\x00", 6 ));       /* entity indices up to 8191 */
	assert( !memcmp( base + 0x60ACE, "\x81\xFF\x00\x08\x00\x00", 6 ));       /* 2048 brush entities */
	assert( !memcmp( base + 0x83210, "\x81\xFE\x00\x20\x00\x00", 6 ));       /* particle sources up to 8191 */
	assert( !memcmp( base + 0x87047, "\x81\xFE\x00\x20\x00\x00", 6 ));       /* Cg water: its three loops */
	assert( !memcmp( base + 0x8718B, "\x81\xFE\x00\x20\x00\x00", 6 ));
	assert( !memcmp( base + 0x878FB, "\x81\xFE\x00\x20\x00\x00", 6 ));
	/* the night vision light: test [flash flags],40h / jz / push -100 / call MY_AllocDlight. Key 0 took a new slot
	 * every frame, so a frame under its 10 ms life drew last frame's light as well: twice the green, every other frame */
	assert( !memcmp( base + 0xB14AB, "\x40\x74\x59\x6A\x9C\xE8\x3B\xAC\xFA\xFF", 10 ));

	/* removed: the whole image is back byte for byte */
	CoF_RenderPatchRemove( &patch );
	assert( !patch.base );
	assert( !memcmp( base, image, size ));
	CoF_RenderPatchRemove( &patch ); /* twice is harmless */

	/* any unexpected byte at any site: refuse and write nothing */
	for( i = 0; i < COF_RP_SITES; i++ )
	{
		const cof_rp_site_t *s = &cof_rp_sites[i];
		for( j = 0; j < s->len; j++ )
		{
			unsigned char was = base[s->rva + j];
			Poke( base + s->rva + j, (unsigned char)( was ^ 0x5a ));
			memcpy( image + s->rva + j, base + s->rva + j, 1 );
			assert( !CoF_RenderPatchApply( &patch, base, pages, chains, ents ));
			assert( !patch.base && !memcmp( base, image, size ));
			Poke( base + s->rva + j, was );
			image[s->rva + j] = was;
		}
	}
	/* a set base already written (the allocator ran with other code): refuse too */
	{
		unsigned char was = base[COF_RP_SET1];
		Poke( base + COF_RP_SET1, 0x01 );
		assert( !CoF_RenderPatchApply( &patch, base, pages, chains, ents ));
		Poke( base + COF_RP_SET1, was );
	}

	/* and after all that it still applies */
	assert( CoF_RenderPatchApply( &patch, base, pages, chains, ents ));
	CoF_RenderPatchRemove( &patch );
	assert( !memcmp( base, image, size ));

	printf( "renderer patch sites passed\n" );
	return 0;
}
