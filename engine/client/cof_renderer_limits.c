/*
cof_renderer_limits.c - Cry of Fear story co-op: client.dll's renderer limits raised
GPL-3.0-or-later.

client.dll's Paranoia renderer keeps its own copy of the lightmaps in 64 pages
of 128x128 and draws brush entities only at edicts 1..899, 512 a frame
(cof_renderer_patch.h). A map with more lightmap pages (a merged map) gets wrong
light on everything past page 63. When the file is the retail 1.6 one, the
engine moves that storage to buffers of its own and raises the bounds: 256
pages (the engine's own limit at 128 blocks), 2048 brush entities a frame,
edicts up to 8191, 1024 of a level's own decals (256: a merged map lost some).
Maps within the old limits draw exactly as before.
*/
#include "common.h"
#include "client.h"
#include "library.h"
// Code patching only where the rest of the story co-op does it (sv_cof_story.c)
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
#define COF_RP_PATCH 1
#include "cof_renderer_patch.h"
#else
#define COF_RP_PATCH 0
#endif

#define COF_RP_CLIENT_SHA256 "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00" // client.dll, Cry of Fear 1.6

#if COF_RP_PATCH
static struct
{
	cof_render_patch_t patch;
	void *pages, *chains, *entities, *decals; // VirtualAlloc'd, zeroed
} cof_rp;

static void CL_CoF_RendererFree( void )
{
	if( cof_rp.pages ) VirtualFree( cof_rp.pages, 0, MEM_RELEASE );
	if( cof_rp.chains ) VirtualFree( cof_rp.chains, 0, MEM_RELEASE );
	if( cof_rp.entities ) VirtualFree( cof_rp.entities, 0, MEM_RELEASE );
	if( cof_rp.decals ) VirtualFree( cof_rp.decals, 0, MEM_RELEASE );
	cof_rp.pages = cof_rp.chains = cof_rp.entities = cof_rp.decals = NULL;
}

// Straight to Sys_Print: client.dll loads while host.allow_console is still
// false, and Con_Printf drops everything then (as in cof_language.c).
static void CL_CoF_RendererNote( const char *fmt, ... ) FORMAT_CHECK( 1 );
static void CL_CoF_RendererNote( const char *fmt, ... )
{
	char buf[256];
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, args );
	va_end( args );

	Sys_Print( buf );
}
#endif

// cl_game.c CL_LoadProgs: after the module is loaded, before its Initialize/Init
void CL_CoF_RendererLimitsLoad( void *module )
{
#if COF_RP_PATCH
	dll_user_t *lib = (dll_user_t *)module;
	if( !lib || !lib->hInstance || lib->custom_loader || cof_rp.patch.base ) return;
	if( !COM_CoF_ModuleSha256Is( lib, COF_RP_CLIENT_SHA256 ))
	{
		CL_CoF_RendererNote( "[cof-renderer] client.dll is not the retail Cry of Fear 1.6 one: its renderer keeps 64 lightmap pages\n" );
		return;
	}
	cof_rp.pages = VirtualAlloc( NULL, COF_RP_PAGE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	cof_rp.chains = VirtualAlloc( NULL, COF_RP_CHAIN_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	cof_rp.entities = VirtualAlloc( NULL, COF_RP_ENTITY_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	cof_rp.decals = VirtualAlloc( NULL, COF_RP_DECAL_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	if( !cof_rp.pages || !cof_rp.chains || !cof_rp.entities || !cof_rp.decals )
	{
		CL_CoF_RendererNote( S_WARN "[cof-renderer] no memory for the renderer's lightmap pages: it keeps 64\n" );
		CL_CoF_RendererFree( );
		return;
	}
	if( !CoF_RenderPatchApply( &cof_rp.patch, (unsigned char *)lib->hInstance, cof_rp.pages, cof_rp.chains, cof_rp.entities, cof_rp.decals ))
	{
		CL_CoF_RendererNote( S_WARN "[cof-renderer] client.dll's renderer has unexpected bytes: it keeps 64 lightmap pages\n" );
		CL_CoF_RendererFree( );
		return;
	}
	CL_CoF_RendererNote( "[cof-renderer] client.dll's renderer: %d lightmap pages, %d brush entities a frame, edicts up to 8191, "
		"%d static decals, one night vision light\n", COF_RP_PAGES_MAX, COF_RP_ENTITIES, COF_RP_DECALS_MAX );
#else
	(void)module;
#endif
}

// cl_game.c CL_UnloadProgs: before the module is freed
void CL_CoF_RendererLimitsUnload( void )
{
#if COF_RP_PATCH
	CoF_RenderPatchRemove( &cof_rp.patch );
	CL_CoF_RendererFree( );
#endif
}
