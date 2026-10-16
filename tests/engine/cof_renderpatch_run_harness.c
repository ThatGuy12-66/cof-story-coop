/* client.dll's Paranoia renderer, run for real: UpdateLightmaps (1005A110), the
 * brush-entity collector UpdateEntityList (10060A10) and the chain write
 * (10060D30), stock and with cof_renderer_patch.h, on the retail client.dll
 * mapped (never initialised) off its preferred base. The engine, the GL calls and
 * paranoia_log are stand-ins; the new storage has a no-access page right after
 * it, so a write past the end crashes the test. */
#include <windows.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "patch_under_test.h"

#define RVA( x ) ( base + ( x ))
#define PAGES_IN_TEST 80          /* 16 past the stock 64 */
#define BLOCKS 4
#define ENTS 1500

static unsigned char *base;

/* ---- stand-ins ---------------------------------------------------------- */
static struct { const char *name, *string; int flags; float value; } cv_bump = { "gl_bump", "0", 0, 0.0f },
	cv_gamma = { "gl_gamma", "1", 0, 1.0f }, cv_bright = { "gl_brightness", "0", 0, 0.0f }, cv_contrast = { "gl_contrast", "1", 0, 1.0f },
	cv_renderer = { "gl_renderer", "1", 0, 1.0f }, cv_brushents = { "gl_brushents", "1", 0, 1.0f };

static int warnings, loaded = -1;
static void __cdecl LogStub( const char *fmt, ... )
{
	va_list args;
	va_start( args, fmt );
	if( !strncmp( fmt, "WARNING! surface", 16 )) warnings++;
	if( !strncmp( fmt, "Loaded %d", 9 )) loaded = va_arg( args, int );
	va_end( args );
}

/* uploads by GL name: three sets of up to 256 pages */
static unsigned bound, set_names[3];
static unsigned char *uploads[3][256];
static void __stdcall BindStub( unsigned target, unsigned name ) { assert( target == 0x0DE1 ); bound = name; }
static void __stdcall TexParamStub( unsigned target, unsigned pname, float value ) { (void)target; (void)pname; (void)value; }
static void __stdcall TexImageStub( unsigned target, int level, int internal, int w, int h, int border, unsigned format, unsigned type, const void *pixels )
{
	int set, page = -1;
	assert( target == 0x0DE1 && !level && internal == 3 && w == 128 && h == 128 && !border && format == 0x1907 && type == 0x1401 );
	for( set = 2; set >= 0; set-- ) /* the nearest base below: stock sets are 64 names apart, patched 256 */
		if( bound >= set_names[set] ) { page = (int)( bound - set_names[set] ); break; }
	assert( set >= 0 && page >= 0 && page < 256 );
	if( !uploads[set][page] ) uploads[set][page] = (unsigned char *)malloc( 128 * 128 * 3 );
	memcpy( uploads[set][page], pixels, 128 * 128 * 3 );
}
static void __cdecl SetValueStub( const char *name, float value ) { (void)name; (void)value; }
static void __cdecl ConPrintfStub( const char *fmt, ... ) { (void)fmt; }

/* the world: model_t (numsurfaces +B0, surfaces +B4) and msurface_t (stride 5C) */
static unsigned char world_ent[0xBB8], world_model[0x100];
static unsigned char *surfaces;
static unsigned char samples[PAGES_IN_TEST][BLOCKS][18 * 18 * 3 * 4]; /* four style slots */
static const int block[BLOCKS][4] = { { 0, 0, 18, 18 }, { 20, 0, 10, 5 }, { 40, 60, 32, 8 }, { 110, 110, 18, 18 } }; /* s, t, smax, tmax */

static unsigned char ents[ENTS + 1][0xBB8], local_ent[0xBB8], view_ent[0xBB8], brush_model[0x100], studio_model[0x100];
static int ents_on;
static void * __cdecl EntityStub( int index )
{
	if( !index ) return world_ent;
	if( ents_on && index >= 1 && index <= ENTS ) return ents[index];
	return NULL;
}
static void * __cdecl LocalStub( void ) { return local_ent; }
static void * __cdecl ViewStub( void ) { return view_ent; }
static const char * __cdecl LevelStub( void ) { return "maps/c_apt34.bsp"; }

static void Put32( unsigned char *p, unsigned v ) { memcpy( p, &v, 4 ); }
static unsigned Get32( const unsigned char *p ) { unsigned v; memcpy( &v, p, 4 ); return v; }
static void Jump( unsigned rva, const void *to )
{
	unsigned char code[5];
	DWORD protect, ignored;
	int rel = (int)( (const unsigned char *)to - ( RVA( rva ) + 5 ));
	code[0] = 0xE9;
	memcpy( code + 1, &rel, 4 );
	assert( VirtualProtect( RVA( rva ), 5, PAGE_EXECUTE_READWRITE, &protect ));
	memcpy( RVA( rva ), code, 5 );
	VirtualProtect( RVA( rva ), 5, protect, &ignored );
}

/* storage with a no-access page right after its end */
static unsigned char *Guarded( size_t bytes )
{
	size_t span = ( bytes + 0xFFF ) & ~(size_t)0xFFF;
	DWORD ignored;
	unsigned char *p = (unsigned char *)VirtualAlloc( NULL, span + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	assert( p );
	assert( VirtualProtect( p + span, 0x1000, PAGE_NOACCESS, &ignored ));
	return p + span - bytes;
}

/* surfaces for `count` pages starting at `first`, renumbered from `to`. Bump: styles 0, 61, 62, 63
 * (the Paranoia base, add-light and light vectors, as on every Cry of Fear face), blocks 0-1 flagged
 * for bump mapping (pass 1 path b, passes 2-3), blocks 2-3 not (path c); otherwise style 0 only. */
static void World( int first, int count, int to, int bump )
{
	int page, b, n = 0;
	memset( surfaces, 0, (size_t)PAGES_IN_TEST * BLOCKS * 0x5C );
	for( page = first; page < first + count; page++ )
		for( b = 0; b < BLOCKS; b++, n++ )
		{
			unsigned char *s = surfaces + n * 0x5C;
			short e0 = (short)(( block[b][2] - 1 ) * 16 ), e1 = (short)(( block[b][3] - 1 ) * 16 );
			memcpy( s + 0x18, &e0, 2 );
			memcpy( s + 0x1A, &e1, 2 );
			Put32( s + 0x1C, (unsigned)block[b][0] );
			Put32( s + 0x20, (unsigned)block[b][1] );
			Put32( s + 0x38, (unsigned)( page - first + to ));
			s[0x3C] = 0;
			if( bump ) { s[0x3D] = 61; s[0x3E] = 62; s[0x3F] = 63; if( b < 2 ) Put32( s + 8, 0x200 ); }
			else s[0x3D] = s[0x3E] = s[0x3F] = 0xFF;
			Put32( s + 0x54, (unsigned)(size_t)samples[page][b] );
		}
	Put32( world_model + 0xB0, (unsigned)n );
	Put32( world_model + 0xB4, (unsigned)(size_t)surfaces );
}

static void Clear( void )
{
	int set, page;
	for( set = 0; set < 3; set++ )
		for( page = 0; page < 256; page++ ) { free( uploads[set][page] ); uploads[set][page] = NULL; }
	warnings = 0;
	loaded = -1;
}

typedef void ( __cdecl *fn_t )( void );
typedef void ( __cdecl *chain_t )( void *surf );

static unsigned char *Run( void )
{
	Clear( );
	Put32( RVA( 0x1CBF7C ), 0 ); /* rebuild */
	( (fn_t)RVA( 0x5A110 ))( );
	return NULL;
}

static int Collect( void )
{
	ents_on = 1;
	( (fn_t)RVA( 0x60A10 ))( );
	ents_on = 0;
	return (int)Get32( RVA( 0x4CDD40 ));
}

int main( int argc, char **argv )
{
	static unsigned char *stock[2][3][256];
	cof_render_patch_t patch;
	unsigned char *pages, *chains, *list;
	void *taken;
	int i, page, set, bump, expect, qualifying;
	assert( argc == 2 );
	setvbuf( stdout, NULL, _IONBF, 0 ); /* what was printed survives a failed assert */
	memset( &patch, 0, sizeof( patch ));
	taken = VirtualAlloc( (void *)0x10000000, 0x1000, MEM_RESERVE, PAGE_NOACCESS );
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base && ( !taken || base != (unsigned char *)0x10000000 ));
	surfaces = (unsigned char *)calloc( PAGES_IN_TEST * BLOCKS, 0x5C );
	for( page = 0; page < PAGES_IN_TEST; page++ )
		for( i = 0; i < BLOCKS; i++ )
		{
			int k;
			for( k = 0; k < 18 * 18 * 3 * 4; k++ ) samples[page][i][k] = (unsigned char)(( page * 131 + i * 71 + k * 7 ) & 0xFF );
		}

	/* the engine's side: function table slots, cvars, GL, the GL manager state, the logs */
	Put32( RVA( 0x1B26DC ), (unsigned)(size_t)EntityStub );
	Put32( RVA( 0x1B2730 ), (unsigned)(size_t)LevelStub );
	Put32( RVA( 0x1B269C ), (unsigned)(size_t)SetValueStub );
	Put32( RVA( 0x1B26D4 ), (unsigned)(size_t)LocalStub );
	Put32( RVA( 0x1B26D8 ), (unsigned)(size_t)ViewStub );
	Put32( RVA( 0x1B26A8 ), (unsigned)(size_t)ConPrintfStub );
	Put32( RVA( 0x1BE64C ), (unsigned)(size_t)BindStub );
	Put32( RVA( 0x1BE73C ), (unsigned)(size_t)TexParamStub );
	Put32( RVA( 0x1BE734 ), (unsigned)(size_t)TexImageStub );
	Put32( RVA( 0x4CD50C ), (unsigned)(size_t)&cv_bump );
	Put32( RVA( 0x4CD528 ), (unsigned)(size_t)&cv_gamma );
	Put32( RVA( 0x4CD52C ), (unsigned)(size_t)&cv_bright );
	Put32( RVA( 0x4CD530 ), (unsigned)(size_t)&cv_contrast );
	Put32( RVA( 0x4CD508 ), (unsigned)(size_t)&cv_renderer );
	Put32( RVA( 0x4CDD88 ), (unsigned)(size_t)&cv_brushents );
	Put32( RVA( 0x1BE80C ), 2 );
	Jump( 0x7D400, (const void *)LogStub );
	Jump( 0x7D4D0, (const void *)LogStub );
	Put32( world_ent + 0xB94, (unsigned)(size_t)world_model );

	/* brush entities 1..1500: opaque brush models, except one of each kind the collector skips */
	Put32( local_ent + 0x2BC, 7 );
	Put32( brush_model + 0x44, 0 );
	Put32( studio_model + 0x44, 3 );
	qualifying = 0;
	for( i = 1; i <= ENTS; i++ )
	{
		Put32( ents[i] + 0xB94, (unsigned)(size_t)brush_model );
		Put32( ents[i] + 0x2BC, 7 );
		if( i == 100 ) Put32( ents[i] + 0x2BC, 6 );                                 /* not in this frame */
		else if( i == 200 ) Put32( ents[i] + 0x2EC, 0x80 );                         /* EF_NODRAW */
		else if( i == 300 ) Put32( ents[i] + 0x2F8, 2 );                            /* rendermode 2: the engine draws it */
		else if( i == 400 ) Put32( ents[i] + 0xB94, (unsigned)(size_t)studio_model );
		else if( i == 1200 ) Put32( ents[i] + 0x2F8, 4 ), qualifying++;            /* rendermode 4: collected */
		else qualifying++;
	}

	/* ---- stock ---- */
	set_names[0] = 0x02000000; set_names[1] = 0x02000040; set_names[2] = 0x02000080;
	Put32( RVA( COF_RP_SET1 ), set_names[0] );
	Put32( RVA( COF_RP_SET1 + 4 ), set_names[1] );
	Put32( RVA( COF_RP_SET1 + 8 ), set_names[2] );
	for( bump = 0; bump < 2; bump++ )
	{
		cv_bump.value = (float)bump;
		World( 0, PAGES_IN_TEST, 0, bump );
		Run( );
		printf( "stock%s: %d warnings, loaded %d\n", bump ? " bump" : "", warnings, loaded );
		assert( warnings == ( PAGES_IN_TEST - 64 ) * BLOCKS && loaded == 64 );
		for( set = 0; set < 3; set++ )
			for( page = 0; page < 256; page++ )
			{
				if( !uploads[set][page] != ( page >= 64 || ( set && !bump )))
					printf( "stock%s: set %d page %d %s\n", bump ? " bump" : "", set, page, uploads[set][page] ? "uploaded" : "missing" );
				assert( !uploads[set][page] == ( page >= 64 || ( set && !bump )));
				stock[bump][set][page] = uploads[set][page];
				uploads[set][page] = NULL;
			}
	}
	assert( Collect( ) == 512 ); /* the first 512 that qualify */

	/* ---- patched ---- */
	Put32( RVA( COF_RP_SET1 ), 0 );
	Put32( RVA( COF_RP_SET1 + 4 ), 0 );
	Put32( RVA( COF_RP_SET1 + 8 ), 0 );
	pages = Guarded( COF_RP_PAGE_BYTES );
	chains = Guarded( COF_RP_CHAIN_BYTES );
	list = Guarded( COF_RP_ENTITY_BYTES );
#ifndef RP_HARNESS_NO_PATCH
	assert( CoF_RenderPatchApply( &patch, base, pages, chains, list ));
#endif
	set_names[0] = Get32( RVA( COF_RP_SET1 ));
	set_names[1] = Get32( RVA( COF_RP_SET1 + 4 ));
	set_names[2] = Get32( RVA( COF_RP_SET1 + 8 ));

	/* every page of every set built and uploaded, no warning; pages 0..63 exactly as stock; pages
	 * 64..79 exactly what pages 0..15 hold when the same surfaces are numbered 0..15 */
	for( bump = 0; bump < 2; bump++ )
	{
		static unsigned char *high[3][16];
		cv_bump.value = (float)bump;
		World( 0, PAGES_IN_TEST, 0, bump );
		Run( );
		printf( "patched%s: %d warnings, loaded %d\n", bump ? " bump" : "", warnings, loaded );
		assert( !warnings && loaded == PAGES_IN_TEST );
		for( set = 0; set < 3; set++ )
			for( page = 0; page < 256; page++ )
			{
				assert( !uploads[set][page] == ( page >= PAGES_IN_TEST || ( set && !bump )));
				if( !uploads[set][page] ) continue;
				if( page < 64 ) assert( !memcmp( uploads[set][page], stock[bump][set][page], 128 * 128 * 3 ));
				else { high[set][page - 64] = uploads[set][page]; uploads[set][page] = NULL; }
			}
		World( 64, PAGES_IN_TEST - 64, 0, bump );
		Run( );
		assert( !warnings && loaded == PAGES_IN_TEST - 64 );
		for( set = 0; set < ( bump ? 3 : 1 ); set++ )
			for( page = 0; page < PAGES_IN_TEST - 64; page++ )
			{
				int nonzero = 0, k;
				assert( !memcmp( high[set][page], uploads[set][page], 128 * 128 * 3 ));
				for( k = 0; k < 128 * 128 * 3; k++ ) nonzero |= high[set][page][k];
				assert( nonzero );
				free( high[set][page] );
				high[set][page] = NULL;
			}
	}
	/* the three sets 256 names apart */
	assert( set_names[0] == 0x03000000u && set_names[1] == 0x03000100u && set_names[2] == 0x03000200u );

	/* all 1495 qualifying brush entities collected, in edict order, into the new list */
	expect = qualifying;
	assert( Collect( ) == expect );
	for( i = 1, set = 0; i <= ENTS; i++ )
	{
		if( i == 100 || i == 200 || i == 300 || i == 400 ) continue;
		assert( Get32( list + 4 * set ) == (unsigned)(size_t)ents[i] );
		set++;
	}
	printf( "patched: %d brush entities\n", expect );

	/* the chain write of the fewer-than-4-units path: chains[page] = surface for every page */
	for( page = 0; page < COF_RP_PAGES_MAX; page++ )
	{
		unsigned char *s = surfaces + ( page % ( PAGES_IN_TEST * BLOCKS )) * 0x5C;
		Put32( s + 0x38, (unsigned)page );
		Put32( s + 0x28, 0xDEADBEEF );
		Put32( chains + 4 * page, 0 );
		( (chain_t)RVA( 0x60D30 ))( s );
		assert( Get32( chains + 4 * page ) == (unsigned)(size_t)s && Get32( s + 0x28 ) == 0 );
	}

	CoF_RenderPatchRemove( &patch );
	printf( "renderer patch run passed\n" );
	return 0;
}
