/*
cof_sections.c - Cry of Fear story co-op: merged story maps, loading a merged map's entities
GPL-3.0-or-later.

A merged map file (maps/cm_*.bsp, made by setup) carries only geometry and its worldspawn; maps/<name>.sections
lists its members. Its entities are put together here, at load, from each member's own entity string exactly as
the engine would load it for the original map (a language pack's .ent or .entpatch included), through the rules
(cof_sections.h). Mod_LoadEntities (the map that spawns, client and server) and SV_ReadEntityScript (map checks,
exits, landmarks) both use it, so everything sees the same entities.
*/
#include "common.h"
#include "cof_language.h"
#include "cof_merged.h"

static cof_sections_t com_cof_sections;   // large: one at a time

// Straight to Sys_Print: a map given on the command line loads while host.allow_console is still false, and
// Con_Printf drops everything then (as in cof_language.c and cof_renderer_limits.c).
void COM_CoF_SectionsNote( const char *fmt, ... )
{
	char buf[512];
	va_list args;

	va_start( args, fmt );
	Q_vsnprintf( buf, sizeof( buf ), fmt, args );
	va_end( args );

	Sys_Print( buf );
}

qboolean COM_CoF_IsMergedMap( const char *map )
{
	return !COM_StringEmptyOrNULL( map ) && FS_FileExists( va( "maps/%s.sections", map ), true );
}

qboolean COM_CoF_LoadSections( const char *map, cof_sections_t *out )
{
	byte *text;
	qboolean ok;
	if( !COM_CoF_IsMergedMap( map )) return false;
	text = FS_LoadFile( va( "maps/%s.sections", map ), NULL, true );
	if( !text ) return false;
	ok = CoF_SectionsParse( (const char *)text, out ) && !Q_stricmp( out->name, map );
	Mem_Free( text );
	if( !ok ) COM_CoF_SectionsNote( S_ERROR "[cof-sections] maps/%s.sections is not a valid sections file\n", map );
	return ok;
}

// the entity string the engine would load for an original map (Mod_LoadEntities' rules), NUL-terminated
static char *COM_CoF_MemberEntities( const char *map )
{
	char bspname[MAX_QPATH], entname[MAX_QPATH], packent[MAX_QPATH];
	char *ents = NULL, *patched;
	size_t patchedsize;
	Q_snprintf( bspname, sizeof( bspname ), "maps/%s.bsp", map );
	Q_snprintf( entname, sizeof( entname ), "maps/%s.ent", map );
	if( CoF_Lang_EntPatch( entname, packent, sizeof( packent )))
		ents = (char *)FS_LoadFile( packent, NULL, true );
	if( !ents )
	{
		int ft1 = FS_FileTime( bspname, false ), ft2 = FS_FileTime( entname, true );
		if( ft2 != -1 && ft1 <= ft2 )
			ents = (char *)FS_LoadFile( entname, NULL, true );
	}
	if( !ents )
	{
		file_t *f = FS_Open( bspname, "rb", false );
		int header[3];
		if( !f ) return NULL;
		if( FS_Read( f, header, sizeof( header )) == sizeof( header ) && header[0] == 30 && header[1] > 0 && header[2] > 0 )
		{
			ents = Z_Calloc( header[2] + 1 );
			FS_Seek( f, header[1], SEEK_SET );
			if( FS_Read( f, ents, header[2] ) != header[2] )
			{
				Mem_Free( ents );
				ents = NULL;
			}
		}
		FS_Close( f );
		if( !ents ) return NULL;
	}
	patched = CoF_Lang_ApplyEntityPatch( entname, ents, Q_strlen( ents ), &patchedsize );
	if( patched )
	{
		Mem_Free( ents );
		ents = patched;
	}
	return ents;
}

/*
The entity string of a merged map: world is the merged file's own entity lump (its worldspawn), worldsize its size.
NULL when map is not merged (or on an error, logged); otherwise a Z_Malloc'd NUL-terminated string.
*/
char *COM_CoF_MergedEntities( const char *map, const char *world, size_t worldsize, size_t *outsize )
{
	// the last composition, reused while nothing it came from changed (exits and holds read it often)
	static char *cache, cache_key[MAX_QPATH * 3];
	static size_t cache_len;
	char *members[COF_SECT_MAX] = { 0 }, *worldtext, *composed, *out = NULL, key[sizeof( cache_key )];
	size_t len = 0;
	int i;
	if( !COM_CoF_LoadSections( map, &com_cof_sections )) return NULL;
	Q_snprintf( key, sizeof( key ), "%s|%s|%d|%d", map, Cvar_VariableString( "cof_language" ),
		FS_FileTime( va( "maps/%s.sections", map ), true ), FS_FileTime( va( "maps/%s.bsp", map ), false ));
	if( cache && !Q_strcmp( key, cache_key ))
	{
		out = Z_Malloc( cache_len + 1 );
		memcpy( out, cache, cache_len + 1 );
		if( outsize ) *outsize = cache_len;
		return out;
	}
	worldtext = Z_Calloc( worldsize + 1 );
	memcpy( worldtext, world, worldsize );
	for( i = 0; i < com_cof_sections.count; i++ )
	{
		if(( members[i] = COM_CoF_MemberEntities( com_cof_sections.sec[i].map )) == NULL )
		{
			COM_CoF_SectionsNote( S_ERROR "[cof-sections] %s: the entities of %s cannot be read\n", map, com_cof_sections.sec[i].map );
			break;
		}
	}
	composed = i == com_cof_sections.count ? CoF_ComposeEntities( worldtext, (const char *const *)members, i, &com_cof_sections, &len ) : NULL;
	if( composed )
	{
		out = Z_Malloc( len + 1 );
		memcpy( out, composed, len + 1 );
		free( composed );
		if( outsize ) *outsize = len;
		COM_CoF_SectionsNote( "[cof-sections] %s: entities of %d maps, %u bytes\n", map, com_cof_sections.count, (unsigned)len );
		if( cache ) Mem_Free( cache );
		cache = Z_Malloc( len + 1 );
		memcpy( cache, out, len + 1 );
		cache_len = len;
		Q_strncpy( cache_key, key, sizeof( cache_key ));
	}
	else if( i == com_cof_sections.count )
		COM_CoF_SectionsNote( S_ERROR "[cof-sections] %s: its entities cannot be put together\n", map );
	for( i = 0; i < COF_SECT_MAX; i++ )
		if( members[i] ) Mem_Free( members[i] );
	Mem_Free( worldtext );
	return out;
}
