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
	if( !ok ) Con_Printf( S_ERROR "[cof-sections] maps/%s.sections is not a valid sections file\n", map );
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
	char *members[COF_SECT_MAX] = { 0 }, *worldtext, *composed, *out = NULL;
	size_t len = 0;
	int i;
	if( !COM_CoF_LoadSections( map, &com_cof_sections )) return NULL;
	worldtext = Z_Calloc( worldsize + 1 );
	memcpy( worldtext, world, worldsize );
	for( i = 0; i < com_cof_sections.count; i++ )
	{
		if(( members[i] = COM_CoF_MemberEntities( com_cof_sections.sec[i].map )) == NULL )
		{
			Con_Printf( S_ERROR "[cof-sections] %s: the entities of %s cannot be read\n", map, com_cof_sections.sec[i].map );
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
		Con_Printf( "[cof-sections] %s: entities of %d maps, %u bytes\n", map, com_cof_sections.count, (unsigned)len );
	}
	else if( i == com_cof_sections.count )
		Con_Printf( S_ERROR "[cof-sections] %s: its entities cannot be put together\n", map );
	for( i = 0; i < COF_SECT_MAX; i++ )
		if( members[i] ) Mem_Free( members[i] );
	Mem_Free( worldtext );
	return out;
}
