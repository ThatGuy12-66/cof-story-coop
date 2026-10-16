/*
sv_save.c - save\restore implementation
Copyright (C) 2008 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "server.h"
#include "library.h"
#include "const.h"
#include "render_api.h"	// decallist_t
#include "sound.h"		// S_GetDynamicSounds
#include "ref_common.h" // decals
#include "sv_cof_story.h"
#include "sv_cof_party.h"
#include "sv_cof_share.h"
#include "sv_cof_state.h"
#include "sv_cof_recovery.h"
#include "sv_cof_session.h"
#include "sv_cof_sections.h"
#include "sv_cof_scene.h"
#include "sv_cof_archive.h"
#include "sv_story_transition_state.h"

// Reserved outside the SDK's low 16 landmark bits and high four entity bits.
// Only our session snapshots use it; destination world loads skip old inventory.
#define FENTTABLE_COF_INVENTORY 0x08000000
#define COF_STORY_MAP_HISTORY 256
static struct
{
	cof_transition_state_t tickets;
	char source[MAX_QPATH], destination[MAX_QPATH], landmark[MAX_QPATH];
	vec3_t source_origin, destination_origin;
	int *inventory_owner; // source entity id -> captured player's one-based slot
	int entity_count, connection;
	qboolean incoming;
} cof_transition;
static char cof_story_maps[COF_STORY_MAP_HISTORY][MAX_QPATH];
static int cof_story_map_count;
static qboolean cof_session_saving, cof_session_loading;
static void CoF_SessionSavedEntity( ENTITYTABLE *entry, int index );
static void CoF_SessionCreatePlayers( SAVERESTOREDATA *saved );
static void CoF_SessionPlayersRestored( SAVERESTOREDATA *saved );
static qboolean CoF_SessionRunLoad( void );

static qboolean CoF_StoryMapSaved( const char *map )
{
	for( int i = 0; i < cof_story_map_count; i++ )
		if( !Q_stricmp( map, cof_story_maps[i] )) return true;
	return false;
}

qboolean SV_CoF_TransitionActive( void )
{
	return cof_transition.tickets.active;
}

void SV_CoF_TransitionReset( qboolean clear_history )
{
	if( cof_transition.inventory_owner ) Mem_Free( cof_transition.inventory_owner );
	memset( &cof_transition, 0, sizeof( cof_transition ));
	CoF_TransitionReset( &cof_transition.tickets );
	if( clear_history )
	{
		// A new session (map, save load, non-story changelevel, shutdown):
		// nobody should be handed the previous party's items.
		cof_story_map_count = 0;
		SV_CoF_ShareReset();
		SV_CoF_StateReset();
		SV_CoF_RecoveryReset();
		SV_CoF_SessionReset();
	}
}

static void CoF_TransitionResume( void )
{
	if( !SV_CoF_TransitionActive() || CoF_TransitionPending( &cof_transition.tickets )) return;
	sv.loadgame = false;
	sv.paused = false;
	MSG_BeginServerCmd( &sv.reliable_datagram, svc_setpause );
	MSG_WriteOneBit( &sv.reliable_datagram, false );
	Con_Printf( "[cof-story-transition] phase=ready generation=%u map=%s\n", cof_transition.tickets.epoch, sv.name );
	SV_CoF_TransitionReset( false );
}

qboolean SV_CoF_TransitionRestoredClient( const sv_client_t *cl )
{
	return CoF_TransitionNeedsSignon( &cof_transition.tickets, svs.spawncount, (int)(cl - svs.clients), cl->userid );
}

void SV_CoF_TransitionClientReady( sv_client_t *cl )
{
	int slot = (int)(cl - svs.clients);
	if( CoF_TransitionReady( &cof_transition.tickets, svs.spawncount, slot, cl->userid ))
	{
		Con_Printf( "[cof-story-transition] phase=signon generation=%u slot=%d userid=%d map=%s\n", svs.spawncount, slot + 1, cl->userid, sv.name );
		CoF_TransitionResume();
	}
}

void SV_CoF_TransitionClientDrop( sv_client_t *cl )
{
	if( CoF_TransitionDrop( &cof_transition.tickets, (int)(cl - svs.clients), cl->userid )) CoF_TransitionResume();
}

qboolean SV_CoF_LiveLandmark( const char *name, vec3_t origin )
{
	int count = 0;
	for( int i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.classname ), "info_landmark" ) || Q_strcmp( SV_GetString( ent->v.targetname ), name )) continue;
		VectorCopy( ent->v.origin, origin );
		count++;
	}
	return count == 1;
}

qboolean SV_CoF_TransitionValidate( const char *map, const char *landmark )
{
	vec3_t origin;
	// a merged map's landmark can have another name where the change was asked (sv_cof_sections.c)
	if( COM_StringEmptyOrNULL( landmark ) || !SV_CoF_LiveLandmark( SV_CoF_TransitionSourceLandmark( map, landmark ), origin ) ||
		!SV_CoF_MapLandmark( map, landmark, origin ))
	{
		Con_Printf( S_ERROR "[cof-story-transition] rejected: unique info_landmark '%s' required in both %s and %s\n", landmark ? landmark : "", sv.name, map );
		return false;
	}
	if( CoF_TransitionPending( &cof_transition.tickets ))
	{
		Con_Printf( S_WARN "[cof-story-transition] rejected: waiting for party sign-on\n" );
		return false;
	}
	return true;
}

static qboolean CoF_BeginStoryTransition( const char *map, const char *landmark )
{
	if( !SV_CoF_TransitionValidate( map, landmark )) return false;
	if( !CoF_StoryMapSaved( sv.name ) && cof_story_map_count >= COF_STORY_MAP_HISTORY )
	{
		Con_Printf( S_ERROR "[cof-story-transition] rejected: session map history full\n" );
		return false;
	}
	SV_CoF_TransitionReset( false );
	CoF_TransitionBegin( &cof_transition.tickets, svs.spawncount + 1 );
	Q_strncpy( cof_transition.source, sv.name, sizeof( cof_transition.source ));
	Q_strncpy( cof_transition.destination, map, sizeof( cof_transition.destination ));
	Q_strncpy( cof_transition.landmark, landmark, sizeof( cof_transition.landmark ));
	SV_CoF_LiveLandmark( SV_CoF_TransitionSourceLandmark( map, landmark ), cof_transition.source_origin );
	SV_CoF_MapLandmark( map, landmark, cof_transition.destination_origin );
	cof_transition.entity_count = svgame.numEntities;
	cof_transition.inventory_owner = Mem_Calloc( host.mempool, sizeof( int ) * svgame.numEntities );
	for( int i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
		if( !CoF_TransitionCapture( &cof_transition.tickets, i, cl->userid ))
		{
			SV_CoF_TransitionReset( false );
			return false;
		}
		Con_Printf( "[cof-story-transition] phase=capture generation=%u slot=%d userid=%d source=%s destination=%s landmark=%s\n", cof_transition.tickets.epoch, i + 1, cl->userid, sv.name, map, landmark );
	}
	if( !CoF_TransitionPending( &cof_transition.tickets ))
	{
		SV_CoF_TransitionReset( false );
		return false;
	}
	// Inventory entities may have an owner or be attached to their player.
	// Never follow arbitrary owner links: inter_door uses owner as activator.
	for( int i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		const char *classname;
		if( !SV_IsValidEdict( ent )) continue;
		classname = SV_GetString( ent->v.classname );
		if( Q_strncmp( classname, "weapon_", 7 ) && Q_strncmp( classname, "item_", 5 )) continue;
		cof_transition.inventory_owner[i] = CoF_TransitionInventoryOwner( &cof_transition.tickets,
			SV_IsValidEdict( ent->v.owner ) ? NUM_FOR_EDICT( ent->v.owner ) : 0,
			SV_IsValidEdict( ent->v.aiment ) ? NUM_FOR_EDICT( ent->v.aiment ) : 0 );
	}
	{
		int exits[16], exit_count = SV_CoF_MapExitModels( sv.name, SV_CoF_TransitionRequestedLevel( map, landmark ),
			SV_CoF_TransitionSourceLandmark( map, landmark ), exits, ARRAYSIZE( exits ), NULL, NULL );
		SV_CoF_PartyTransitionCapture( cof_transition.tickets.epoch, cof_transition.source_origin, exits, exit_count );
	}
	// The transition save carries every player's flags: no camera survives the
	// map change, so its freeze and invulnerability must not travel with them.
	SV_CoF_SceneSettle( "transition" );
	return true;
}

/*
==============================================================================
SAVE FILE

half-life implementation of saverestore system
==============================================================================
*/
#define SAVEFILE_HEADER		(('V'<<24)+('L'<<16)+('A'<<8)+'V')	// little-endian "VALV"
#define SAVEGAME_HEADER		(('V'<<24)+('A'<<16)+('S'<<8)+'J')	// little-endian "JSAV"
#define SAVEGAME_VERSION		0x0071				// Version 0.71 GoldSrc compatible
#define CLIENT_SAVEGAME_VERSION	0x0067				// Version 0.67

#define SAVE_HEAPSIZE		0x400000				// reserve 4Mb for now
#define SAVE_HASHSTRINGS		0xFFF				// 4095 unique strings

/*
 * Cry of Fear's stock installation keeps SAVE at the runtime root while
 * Xash game-directory I/O is rooted at cryoffear/.  Keep the compatibility
 * path opt-in and translate only save paths; generic filesystem searches
 * remain unchanged.  Reads use the static root search path (SAVE/...), while
 * writes/deletes/renames use ../SAVE/... relative to the active game path.
 */
static qboolean cofMenuSaveTracking;
static qboolean cofMenuSaveIOFailed;

static qboolean SV_CoFRootSaveCompat( void )
{
	return Cvar_VariableInteger( "cof_save_root_compat" ) && GI &&
		!Q_stricmp( GI->gamefolder, "cryoffear" );
}

static qboolean SV_IsSavePath( const char *path )
{
	const char *relative;

	if( !path )
		return false;

	if( !Q_strnicmp( path, DEFAULT_SAVE_DIRECTORY, 5 ) || !Q_strnicmp( path, "SAVE/", 5 ))
		relative = path + 5;
	else return false;

	/* Direct-path writes must remain confined to the runtime-root SAVE tree. */
	return relative[0] && !Q_strchr( relative, '\\' ) && !COM_CheckNastyPath( relative );
}

static const char *SV_SaveCompatPath( const char *path, qboolean write, char *buffer, size_t size )
{
	if( !SV_CoFRootSaveCompat( ) || !SV_IsSavePath( path ))
		return path;

	if( write )
		Q_snprintf( buffer, size, "../SAVE/%s", path + 5 );
	else
		Q_snprintf( buffer, size, "SAVE/%s", path + 5 );

	return buffer;
}

static qboolean SV_SaveWriteMode( const char *mode )
{
	return mode && ( mode[0] == 'w' || mode[0] == 'a' || mode[0] == 'e' || Q_strchr( mode, '+' ));
}

static file_t *SV_SaveFSOpen( const char *path, const char *mode, qboolean gamedironly )
{
	char mapped[MAX_SYSPATH];
	const qboolean write = SV_SaveWriteMode( mode );
	const qboolean translated = SV_CoFRootSaveCompat( ) && SV_IsSavePath( path );
	const char *name = SV_SaveCompatPath( path, write, mapped, sizeof( mapped ));
	file_t *file;

	if( write && translated )
		FS_AllowDirectPaths( true );
	file = FS_Open( name, mode, write ? gamedironly : (name != path ? false : gamedironly) );
	if( cofMenuSaveTracking && write && !file ) cofMenuSaveIOFailed = true;
	if( write && translated )
		FS_AllowDirectPaths( false );
	return file;
}

static int SV_SaveFSFileExists( const char *path, int gamedironly )
{
	char mapped[MAX_SYSPATH];
	const char *name = SV_SaveCompatPath( path, false, mapped, sizeof( mapped ));

	return FS_FileExists( name, name != path ? false : gamedironly );
}

static int SV_SaveFSFileTime( const char *path, qboolean gamedironly )
{
	char mapped[MAX_SYSPATH];
	const char *name = SV_SaveCompatPath( path, false, mapped, sizeof( mapped ));

	return FS_FileTime( name, name != path ? false : gamedironly );
}

static search_t *SV_SaveFSSearch( const char *pattern, int caseinsensitive, int gamedironly )
{
	char mapped[MAX_SYSPATH];
	const char *name = SV_SaveCompatPath( pattern, false, mapped, sizeof( mapped ));

	return FS_Search( name, caseinsensitive, name != pattern ? false : gamedironly );
}

static qboolean SV_SaveFSDelete( const char *path )
{
	char mapped[MAX_SYSPATH];
	const qboolean translated = SV_CoFRootSaveCompat( ) && SV_IsSavePath( path );
	const char *name = SV_SaveCompatPath( path, true, mapped, sizeof( mapped ));
	qboolean result;

	if( translated )
		FS_AllowDirectPaths( true );
	result = FS_Delete( name );
	if( translated )
		FS_AllowDirectPaths( false );
	return result;
}

static qboolean SV_SaveFSRename( const char *oldpath, const char *newpath )
{
	char oldmapped[MAX_SYSPATH], newmapped[MAX_SYSPATH];
	const qboolean translated = SV_CoFRootSaveCompat( ) && SV_IsSavePath( oldpath ) && SV_IsSavePath( newpath );
	const char *oldname = SV_SaveCompatPath( oldpath, true, oldmapped, sizeof( oldmapped ));
	const char *newname = SV_SaveCompatPath( newpath, true, newmapped, sizeof( newmapped ));
	qboolean result;

	if( translated )
		FS_AllowDirectPaths( true );
	result = FS_Rename( oldname, newname );
	if( translated )
		FS_AllowDirectPaths( false );
	return result;
}

qboolean SV_CoFSaveDelete( const char *path )
{
	return SV_SaveFSDelete( path );
}

/* Native save writers do not consistently propagate I/O errors. Track every
 * write/copy/close during the optional menu transaction before discarding its
 * recovery copy, including intermediate HL files written by SaveGameState. */
static fs_offset_t SV_SaveFSWrite( file_t *file, const void *data, size_t size )
{
	fs_offset_t written = g_fsapi.Write( file, data, size );
	if( cofMenuSaveTracking && written != (fs_offset_t)size ) cofMenuSaveIOFailed = true;
	return written;
}

static int SV_SaveFSClose( file_t *file )
{
	int result = g_fsapi.Close( file );
	if( cofMenuSaveTracking && result != 0 ) cofMenuSaveIOFailed = true;
	return result;
}

static qboolean SV_SaveFSFileCopy( file_t *output, file_t *input, int size )
{
	char buffer[8192];
	if( !cofMenuSaveTracking )
		return output && input && size >= 0 && g_fsapi.FileCopy( output, input, size );
	if( !output || !input || size < 0 )
	{
		cofMenuSaveIOFailed = true;
		return false;
	}
	/* The filesystem's generic FileCopy also ignores short writes. */
	while( size > 0 )
	{
		int chunk = Q_min( size, (int)sizeof( buffer ));
		fs_offset_t count = g_fsapi.Read( input, buffer, chunk );
		if( count <= 0 || count > chunk || g_fsapi.Write( output, buffer, count ) != count )
		{
			cofMenuSaveIOFailed = true;
			return false;
		}
		size -= (int)count;
	}
	return true;
}

/* Keep the save implementation's existing calls scoped to this adapter. */
#undef FS_Open
#undef FS_FileExists
#undef FS_FileTime
#undef FS_Search
#undef FS_Delete
#undef FS_Rename
#define FS_Open       SV_SaveFSOpen
#define FS_FileExists SV_SaveFSFileExists
#define FS_FileTime   SV_SaveFSFileTime
#define FS_Search     SV_SaveFSSearch
#define FS_Delete     SV_SaveFSDelete
#define FS_Rename     SV_SaveFSRename
#undef FS_Write
#undef FS_Close
#undef FS_FileCopy
#define FS_Write      SV_SaveFSWrite
#define FS_Close      SV_SaveFSClose
#define FS_FileCopy   SV_SaveFSFileCopy

// savedata headers
typedef struct
{
	char	mapName[32];
	char	comment[80];
	int	mapCount;
} GAME_HEADER;

typedef struct
{
	int	skillLevel;
	int	entityCount;
	int	connectionCount;
	int	lightStyleCount;
	float	time;
	char	mapName[32];
	char	skyName[32];
	int	skyColor_r;
	int	skyColor_g;
	int	skyColor_b;
	float	skyVec_x;
	float	skyVec_y;
	float	skyVec_z;
} SAVE_HEADER;

typedef struct
{
	int	decalCount;	// render decals count
	int	entityCount;	// static entity count
	int	soundCount;	// sounds count
	int	tempEntsCount;	// not used
	char	introTrack[64];
	char	mainTrack[64];
	int	trackPosition;
	short	viewentity;	// Xash3D added
	float	wateralpha;
	float	wateramp;		// world waves
} SAVE_CLIENT;

typedef struct
{
	int	index;
	char	style[256];
	float	time;
} SAVE_LIGHTSTYLE;

#if XASH_WIN32
static void (__cdecl *pfnSaveGameComment)( char *buffer, int max_length ) = NULL;
#else // XASH_WIN32
static void (*pfnSaveGameComment)( char *buffer, int max_length ) = NULL;
#endif // XASH_WIN32

static TYPEDESCRIPTION gGameHeader[] =
{
	DEFINE_ARRAY( GAME_HEADER, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( GAME_HEADER, comment, FIELD_CHARACTER, 80 ),
	DEFINE_FIELD( GAME_HEADER, mapCount, FIELD_INTEGER ),
};

static TYPEDESCRIPTION gSaveHeader[] =
{
	DEFINE_FIELD( SAVE_HEADER, skillLevel, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, entityCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, connectionCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, lightStyleCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, time, FIELD_TIME ),
	DEFINE_ARRAY( SAVE_HEADER, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( SAVE_HEADER, skyName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_r, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_g, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyColor_b, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_x, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_y, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_HEADER, skyVec_z, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gAdjacency[] =
{
	DEFINE_ARRAY( LEVELLIST, mapName, FIELD_CHARACTER, 32 ),
	DEFINE_ARRAY( LEVELLIST, landmarkName, FIELD_CHARACTER, 32 ),
	DEFINE_FIELD( LEVELLIST, pentLandmark, FIELD_EDICT ),
	DEFINE_FIELD( LEVELLIST, vecLandmarkOrigin, FIELD_VECTOR ),
};

static TYPEDESCRIPTION gLightStyle[] =
{
	DEFINE_FIELD( SAVE_LIGHTSTYLE, index, FIELD_INTEGER ),
	DEFINE_ARRAY( SAVE_LIGHTSTYLE, style, FIELD_CHARACTER, 256 ),
	DEFINE_FIELD( SAVE_LIGHTSTYLE, time, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gEntityTable[] =
{
	DEFINE_FIELD( ENTITYTABLE, id, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, location, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, size, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, flags, FIELD_INTEGER ),
	DEFINE_FIELD( ENTITYTABLE, classname, FIELD_STRING ),
};

static TYPEDESCRIPTION gSaveClient[] =
{
	DEFINE_FIELD( SAVE_CLIENT, decalCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, entityCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, soundCount, FIELD_INTEGER ),
	DEFINE_FIELD( SAVE_CLIENT, tempEntsCount, FIELD_INTEGER ),
	DEFINE_ARRAY( SAVE_CLIENT, introTrack, FIELD_CHARACTER, 64 ),
	DEFINE_ARRAY( SAVE_CLIENT, mainTrack, FIELD_CHARACTER, 64 ),
	DEFINE_FIELD( SAVE_CLIENT, trackPosition, FIELD_INTEGER ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( SAVE_CLIENT, viewentity, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( SAVE_CLIENT, wateralpha, FIELD_FLOAT ),
	DEFINE_FIELD( SAVE_CLIENT, wateramp, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gDecalEntry[] =
{
	DEFINE_FIELD( decallist_t, position, FIELD_VECTOR ),
	DEFINE_ARRAY( decallist_t, name, FIELD_CHARACTER, 64 ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( decallist_t, entityIndex, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( decallist_t, depth, FIELD_CHARACTER ),
	DEFINE_FIELD( decallist_t, flags, FIELD_CHARACTER ),
	DEFINE_FIELD( decallist_t, scale, FIELD_FLOAT ),
	DEFINE_FIELD( decallist_t, impactPlaneNormal, FIELD_VECTOR ),
	DEFINE_ARRAY( decallist_t, studio_state, FIELD_CHARACTER, sizeof( modelstate_t )),
};

// Can use any FIELD type here because only Xash3D games will spawn static entities
static TYPEDESCRIPTION gStaticEntry[] =
{
	DEFINE_FIELD( entity_state_t, messagenum, FIELD_MODELNAME ), // HACKHACK: store model into messagenum
	DEFINE_FIELD( entity_state_t, origin, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, angles, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, sequence, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, frame, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, colormap, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, skin, FIELD_SHORT ),
	DEFINE_FIELD( entity_state_t, body, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, scale, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, effects, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, framerate, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, mins, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, maxs, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, startpos, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, rendermode, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, renderamt, FIELD_FLOAT ),
	DEFINE_ARRAY( entity_state_t, rendercolor, FIELD_CHARACTER, sizeof( color24 )),
	DEFINE_FIELD( entity_state_t, renderfx, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, controller, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, blending, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, solid, FIELD_SHORT ),
	DEFINE_FIELD( entity_state_t, animtime, FIELD_TIME ),
	DEFINE_FIELD( entity_state_t, movetype, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, vuser1, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser2, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser3, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, vuser4, FIELD_VECTOR ),
	DEFINE_FIELD( entity_state_t, iuser1, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser2, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser3, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, iuser4, FIELD_INTEGER ),
	DEFINE_FIELD( entity_state_t, fuser1, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser2, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser3, FIELD_FLOAT ),
	DEFINE_FIELD( entity_state_t, fuser4, FIELD_FLOAT ),
};

static TYPEDESCRIPTION gSoundEntry[] =
{
	DEFINE_ARRAY( soundlist_t, name, FIELD_CHARACTER, 64 ),
	// mods based on HLU SDK disallow usage of FIELD_SHORT
	DEFINE_ARRAY( soundlist_t, entnum, FIELD_CHARACTER, sizeof( short )),
	DEFINE_FIELD( soundlist_t, origin, FIELD_VECTOR ),
	DEFINE_FIELD( soundlist_t, volume, FIELD_FLOAT ),
	DEFINE_FIELD( soundlist_t, attenuation, FIELD_FLOAT ),
	DEFINE_FIELD( soundlist_t, looping, FIELD_BOOLEAN ),
	DEFINE_FIELD( soundlist_t, channel, FIELD_CHARACTER ),
	DEFINE_FIELD( soundlist_t, pitch, FIELD_CHARACTER ),
	DEFINE_FIELD( soundlist_t, wordIndex, FIELD_CHARACTER ),
	DEFINE_ARRAY( soundlist_t, samplePos, FIELD_CHARACTER, sizeof( double )),
	DEFINE_ARRAY( soundlist_t, forcedEnd, FIELD_CHARACTER, sizeof( double )),
};

static TYPEDESCRIPTION gTempEntvars[] =
{
	DEFINE_ENTITY_FIELD( classname, FIELD_STRING ),
	DEFINE_ENTITY_GLOBAL_FIELD( globalname, FIELD_STRING ),
};

static const struct
{
	const char *mapname;
	const char *titlename;
} gTitleComments[] =
{
	// default Half-Life map titles
	// ordering is important
	// strings hw.so| grep T0A0TITLE -B 50 -A 150
	{ "T0A0", "#T0A0TITLE" },
	{ "C0A0", "#C0A0TITLE" },
	{ "C1A0", "#C0A1TITLE" },
	{ "C1A1", "#C1A1TITLE" },
	{ "C1A2", "#C1A2TITLE" },
	{ "C1A3", "#C1A3TITLE" },
	{ "C1A4", "#C1A4TITLE" },
	{ "C2A1", "#C2A1TITLE" },
	{ "C2A2", "#C2A2TITLE" },
	{ "C2A3", "#C2A3TITLE" },
	{ "C2A4D", "#C2A4TITLE2" },
	{ "C2A4E", "#C2A4TITLE2" },
	{ "C2A4F", "#C2A4TITLE2" },
	{ "C2A4G", "#C2A4TITLE2" },
	{ "C2A4", "#C2A4TITLE1" },
	{ "C2A5", "#C2A5TITLE" },
	{ "C3A1", "#C3A1TITLE" },
	{ "C3A2", "#C3A2TITLE" },
	{ "C4A1A", "#C4A1ATITLE" },
	{ "C4A1B", "#C4A1ATITLE" },
	{ "C4A1C", "#C4A1ATITLE" },
	{ "C4A1D", "#C4A1ATITLE" },
	{ "C4A1E", "#C4A1ATITLE" },
	{ "C4A1", "#C4A1TITLE" },
	{ "C4A2", "#C4A2TITLE" },
	{ "C4A3", "#C4A3TITLE" },
	{ "C5A1", "#C5TITLE" },
	{ "OFBOOT", "#OF_BOOT0TITLE" },
	{ "OF0A", "#OF1A1TITLE" },
	{ "OF1A1", "#OF1A3TITLE" },
	{ "OF1A2", "#OF1A3TITLE" },
	{ "OF1A3", "#OF1A3TITLE" },
	{ "OF1A4", "#OF1A3TITLE" },
	{ "OF1A", "#OF1A5TITLE" },
	{ "OF2A1", "#OF2A1TITLE" },
	{ "OF2A2", "#OF2A1TITLE" },
	{ "OF2A3", "#OF2A1TITLE" },
	{ "OF2A", "#OF2A4TITLE" },
	{ "OF3A1", "#OF3A1TITLE" },
	{ "OF3A2", "#OF3A1TITLE" },
	{ "OF3A", "#OF3A3TITLE" },
	{ "OF4A1", "#OF4A1TITLE" },
	{ "OF4A2", "#OF4A1TITLE" },
	{ "OF4A3", "#OF4A1TITLE" },
	{ "OF4A", "#OF4A4TITLE" },
	{ "OF5A", "#OF5A1TITLE" },
	{ "OF6A1", "#OF6A1TITLE" },
	{ "OF6A2", "#OF6A1TITLE" },
	{ "OF6A3", "#OF6A1TITLE" },
	{ "OF6A4b", "#OF6A4TITLE" },
	{ "OF6A4", "#OF6A4TITLE" },
	{ "OF6A5", "#OF6A4TITLE" },
	{ "OF6A", "#OF6A4TITLE" },
	{ "OF7A", "#OF7A0TITLE" },
	{ "ba_tram", "#BA_TRAMTITLE" },
	{ "ba_security", "#BA_SECURITYTITLE" },
	{ "ba_main", "#BA_SECURITYTITLE" },
	{ "ba_elevator", "#BA_SECURITYTITLE" },
	{ "ba_canal", "#BA_CANALSTITLE" },
	{ "ba_yard", "#BA_YARDTITLE" },
	{ "ba_xen", "#BA_XENTITLE" },
	{ "ba_hazard", "#BA_HAZARD" },
	{ "ba_power", "#BA_POWERTITLE" },
	{ "ba_teleport1", "#BA_POWERTITLE" },
	{ "ba_teleport", "#BA_TELEPORTTITLE" },
	{ "ba_outro", "#BA_OUTRO" },
};

/*
=============
SaveBuildComment

build commentary for each savegame
typically it writes world message and level time
=============
*/
static void SaveBuildComment( char *text, int maxlength )
{
	string      comment;
	const char *pName = NULL;

	text[0] = '\0'; // clear

	if( pfnSaveGameComment != NULL )
	{
		// get save comment from gamedll
		pfnSaveGameComment( comment, MAX_STRING );
		pName = comment;
	}
	else
	{
		size_t i;
		const char *mapname = SV_GetString( svgame.globals->mapname );

		for( i = 0; i < ARRAYSIZE( gTitleComments ); i++ )
		{
			// compare if strings are equal at beginning
			size_t len = strlen( gTitleComments[i].mapname );
			if( !Q_strnicmp( mapname, gTitleComments[i].mapname, len ))
			{
				pName = gTitleComments[i].titlename;
				break;
			}
		}

		if( !pName )
		{
			if( svgame.edicts->v.message != 0 )
			{
				// trying to extract message from the world
				pName = SV_GetString( svgame.edicts->v.message );
			}
			else
			{
				// or use mapname
				pName = SV_GetString( svgame.globals->mapname );
			}
		}
	}

	Q_snprintf( text, maxlength, "%-64.64s %02d:%02d", pName, (int)(sv.time / 60.0 ), (int)fmod( sv.time, 60.0 ));
}

/*
=============
DirectoryCount

counting all the files with HL1-HL3 extension
in save folder
=============
*/
static int DirectoryCount( const char *pPath )
{
	int	count;
	search_t	*t;

	t = FS_Search( pPath, true, true );	// lookup only in gamedir
	if( !t ) return 0; // empty

	count = t->numfilenames;
	Mem_Free( t );

	return count;
}

/*
=============
InitEntityTable

reserve space for ETABLE's
=============
*/
static void InitEntityTable( SAVERESTOREDATA *pSaveData, int entityCount )
{
	pSaveData->pTable = Mem_Calloc( host.mempool, sizeof( ENTITYTABLE ) * entityCount );
	pSaveData->tableCount = entityCount;

	// setup entitytable
	for( int i = 0; i < entityCount; i++ )
	{
		ENTITYTABLE *pTable = &pSaveData->pTable[i];
		pTable->pent = SV_EdictNum( i );
		pTable->id = i;
	}
}

/*
=============
EntryInTable

check level in transition list
=============
*/
static int EntryInTable( SAVERESTOREDATA *pSaveData, const char *pMapName, int index )
{
	for( int i = index + 1; i < pSaveData->connectionCount; i++ )
	{
		if ( !Q_stricmp( pSaveData->levelList[i].mapName, pMapName ))
			return i;
	}

	return -1;
}

/*
=============
EdictFromTable

get edict from table
=============
*/
static edict_t *EdictFromTable( SAVERESTOREDATA *pSaveData, int entityIndex )
{
	if( pSaveData && pSaveData->pTable )
	{
		entityIndex = bound( 0, entityIndex, pSaveData->tableCount - 1 );
		return pSaveData->pTable[entityIndex].pent;
	}

	return NULL;
}

/*
=============
LandmarkOrigin

find global offset for a given landmark
=============
*/
static void LandmarkOrigin( SAVERESTOREDATA *pSaveData, vec3_t output, const char *pLandmarkName )
{
	for( int i = 0; i < pSaveData->connectionCount; i++ )
	{
		if( !Q_strcmp( pSaveData->levelList[i].landmarkName, pLandmarkName ))
		{
			VectorCopy( pSaveData->levelList[i].vecLandmarkOrigin, output );
			return;
		}
	}

	VectorClear( output );
}

/*
=============
EntityInSolid

some moved edicts on a next level cause stuck
outside of world. Find them and remove
=============
*/
static int EntityInSolid( edict_t *pent )
{
	edict_t	*aiment = pent->v.aiment;
	vec3_t	point;

	// if you're attached to a client, always go through
	if( pent->v.movetype == MOVETYPE_FOLLOW && SV_IsValidEdict( aiment ) && FBitSet( aiment->v.flags, FL_CLIENT ))
		return 0;

	VectorAverage( pent->v.absmin, pent->v.absmax, point );
	svs.groupmask = pent->v.groupinfo;

	return (SV_PointContents( point ) == CONTENTS_SOLID);
}

/*
=============
ClearSaveDir

remove all the temp files HL1-HL3
(it will be extracted again from another .sav file)
=============
*/
static void ClearSaveDir( void )
{
	search_t	*t;

	// just delete all HL? files
	t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.HL?", true, true );
	if( !t ) return; // already empty

	for( int i = 0; i < t->numfilenames; i++ )
		FS_Delete( t->filenames[i] );

	Mem_Free( t );
}

/*
=============
IsValidSave

savegame is allowed?
=============
*/
static int IsValidSave( void )
{
	if( !svs.initialized || sv.state != ss_active )
	{
		Con_Printf( "Not playing a local game.\n" );
		return 0;
	}

	// ignore autosave during background
	if( sv.background || UI_CreditsActive( ))
		return 0;

	if( svgame.physFuncs.SV_AllowSaveGame != NULL )
	{
		if( !svgame.physFuncs.SV_AllowSaveGame( ))
		{
			Con_Printf( "Savegame is not allowed.\n" );
			return 0;
		}
	}

	if( !CL_Active( ))
	{
		Con_Printf( "Can't save if not active.\n" );
		return 0;
	}

	if( CL_IsIntermission( ))
	{
		Con_Printf( "Can't save during intermission.\n" );
		return 0;
	}

	if( svs.maxclients != 1 )
	{
		Con_Printf( "Can't save multiplayer games.\n" );
		return 0;
	}

	if( svs.clients && svs.clients[0].state == cs_spawned )
	{
		edict_t	*pl = svs.clients[0].edict;

		if( !pl )
		{
			Con_Printf( "Can't savegame without a player!\n" );
			return 0;
		}

		if( pl->v.deadflag || pl->v.health <= 0.0f )
		{
			Con_Printf( "Can't savegame with a dead player\n" );
			return 0;
		}

		// Passed all checks, it's ok to save
		return 1;
	}

	Con_Printf( "Can't savegame without a client!\n" );

	return 0;
}

/*
=============
AgeSaveList

scroll the name list down
=============
*/
static void AgeSaveList( const char *pName, int count )
{
	char	newName[MAX_OSPATH], oldName[MAX_OSPATH];
	char	newShot[MAX_OSPATH], oldShot[MAX_OSPATH];

	// delete last quick/autosave (e.g. quick05.sav)
	Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
	Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );

	// only delete from game directory, basedir is read-only
	FS_Delete( newName );
	FS_Delete( newShot );

#if !XASH_DEDICATED
	// unloading the shot footprint
	GL_FreeImage( newShot );
#endif // XASH_DEDICATED

	while( count > 0 )
	{
		if( count == 1 )
		{
			// quick.sav
			Q_snprintf( oldName, sizeof( oldName ), DEFAULT_SAVE_DIRECTORY "%s.sav", pName );
			Q_snprintf( oldShot, sizeof( oldShot ), DEFAULT_SAVE_DIRECTORY "%s.bmp", pName );
		}
		else
		{
			// quick04.sav, etc.
			Q_snprintf( oldName, sizeof( oldName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count - 1 );
			Q_snprintf( oldShot, sizeof( oldShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count - 1 );
		}

		Q_snprintf( newName, sizeof( newName ), DEFAULT_SAVE_DIRECTORY "%s%02d.sav", pName, count );
		Q_snprintf( newShot, sizeof( newShot ), DEFAULT_SAVE_DIRECTORY "%s%02d.bmp", pName, count );

#if !XASH_DEDICATED
		// unloading the oldshot footprint too
		GL_FreeImage( oldShot );
#endif // XASH_DEDICATED

		// scroll the name list down (e.g. rename quick04.sav to quick05.sav)
		FS_Rename( oldName, newName );
		FS_Rename( oldShot, newShot );
		count--;
	}
}

/*
=============
DirectoryCopy

put the HL1-HL3 files into .sav file
=============
*/
static void DirectoryCopy( const char *pPath, file_t *pFile )
{
	search_t	*t;

	t = FS_Search( pPath, true, true );
	if( !t ) return; // nothing to copy ?

	for( int i = 0; i < t->numfilenames; i++ )
	{
		char	szName[MAX_OSPATH];
		file_t	*pCopy = FS_Open( t->filenames[i], "rb", true );
		if( !pCopy )
		{
			if( cofMenuSaveTracking ) cofMenuSaveIOFailed = true;
			Con_Printf( S_ERROR "could not read save component %s\n", t->filenames[i] );
			continue;
		}
		int	fileSize = FS_FileLength( pCopy );

		memset( szName, 0, sizeof( szName )); // clearing the string to prevent garbage in output file
		Q_strncpy( szName, COM_FileWithoutPath( t->filenames[i] ), sizeof( szName ));
		FS_Write( pFile, szName, MAX_OSPATH );
		FS_Write( pFile, &fileSize, sizeof( int ));
		FS_FileCopy( pFile, pCopy, fileSize );
		FS_Close( pCopy );
	}
	Mem_Free( t );
}

/*
=============
DirectoryExtract

extract the HL1-HL3 files from the .sav file
=============
*/
static qboolean DirectoryExtract( file_t *pFile, int fileCount )
{
	for( int i = 0; i < fileCount; i++ )
	{
		char	szName[MAX_OSPATH];
		char	fileName[MAX_OSPATH];
		int	fileSize;
		file_t	*pCopy;

		// filename can only be as long as a map name + extension
		FS_Read( pFile, szName, MAX_OSPATH );
		FS_Read( pFile, &fileSize, sizeof( int ));
		Q_snprintf( fileName, sizeof( fileName ), DEFAULT_SAVE_DIRECTORY "%s", szName );
		COM_FixSlashes( fileName );

		pCopy = FS_Open( fileName, "wb", true );
		if( !pCopy )
		{
			Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, fileName );
			return false;
		}

		FS_FileCopy( pCopy, pFile, fileSize );
		FS_Close( pCopy );
	}

	return true;
}

/*
=============
SaveInit

initialize global save-restore buffer
=============
*/
static SAVERESTOREDATA *SaveInit( int size, int tokenCount )
{
	SAVERESTOREDATA	*pSaveData;

	pSaveData = Mem_Calloc( host.mempool, sizeof( SAVERESTOREDATA ) + size );
	pSaveData->pTokens = (char **)Mem_Calloc( host.mempool, tokenCount * sizeof( char* ));
	pSaveData->tokenCount = tokenCount;

	pSaveData->pBaseData = (char *)(pSaveData + 1); // skip the save structure);
	pSaveData->pCurrentData = pSaveData->pBaseData; // reset the pointer
	pSaveData->bufferSize = size;

	pSaveData->time = svgame.globals->time;	// Use DLL time

	// shared with dlls
	svgame.globals->pSaveData = pSaveData;

	return pSaveData;
}

/*
=============
SaveClear

clearing buffer for reuse
=============
*/
static void SaveClear( SAVERESTOREDATA *pSaveData )
{
	memset( pSaveData->pTokens, 0, pSaveData->tokenCount * sizeof( char* ));

	pSaveData->pBaseData = (char *)(pSaveData + 1); // skip the save structure);
	pSaveData->pCurrentData = pSaveData->pBaseData; // reset the pointer
	pSaveData->time = svgame.globals->time;	// Use DLL time
	pSaveData->tokenSize = 0;	// reset the hashtable
	pSaveData->size = 0;	// reset the pointer

	// shared with dlls
	svgame.globals->pSaveData = pSaveData;
}

/*
=============
SaveFinish

release global save-restore buffer
=============
*/
static void SaveFinish( SAVERESTOREDATA *pSaveData )
{
	if( !pSaveData ) return;

	if( pSaveData->pTokens )
	{
		Mem_Free( pSaveData->pTokens );
		pSaveData->pTokens = NULL;
		pSaveData->tokenCount = 0;
	}

	if( pSaveData->pTable )
	{
		Mem_Free( pSaveData->pTable );
		pSaveData->pTable = NULL;
		pSaveData->tableCount = 0;
	}

	svgame.globals->pSaveData = NULL;
	Mem_Free( pSaveData );
}

/*
=============
StoreHashTable

write the stringtable into file
=============
*/
static char *StoreHashTable( SAVERESTOREDATA *pSaveData )
{
	char	*pTokenData = pSaveData->pCurrentData;

	// Write entity string token table
	if( pSaveData->pTokens )
	{
		for( int i = 0; i < pSaveData->tokenCount; i++ )
		{
			const char *pszToken = pSaveData->pTokens[i] ? pSaveData->pTokens[i] : "";

			// just copy the token byte-by-byte
			while( *pszToken )
				*pSaveData->pCurrentData++ = *pszToken++;
			*pSaveData->pCurrentData++ = 0; // Write the term
		}
	}

	pSaveData->tokenSize = pSaveData->pCurrentData - pTokenData;

	return pTokenData;
}

/*
=============
BuildHashTable

build the stringtable from buffer
=============
*/
static void BuildHashTable( SAVERESTOREDATA *pSaveData, file_t *pFile )
{
	char	*pszTokenList = pSaveData->pBaseData;

	// Parse the symbol table
	if( pSaveData->tokenSize > 0 )
	{
		FS_Read( pFile, pszTokenList, pSaveData->tokenSize );

		// make sure the token strings pointed to by the pToken hashtable.
		for( int i = 0; i < pSaveData->tokenCount; i++ )
		{
			pSaveData->pTokens[i] = *pszTokenList ? pszTokenList : NULL;
			while( *pszTokenList++ );	// Find next token (after next null)
		}
	}

	// rebase the data pointer
	pSaveData->pBaseData = pszTokenList;	// pszTokenList now points after token data
	pSaveData->pCurrentData = pSaveData->pBaseData;
}

/*
=============
GetClientDataSize

g-cont: this routine is redundant
i'm write it just for more readable code
=============
*/
static int GetClientDataSize( const char *level )
{
	int	tokenCount, tokenSize;
	int	size, id, version;
	char	name[MAX_QPATH];
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return 0;

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != CLIENT_SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));
	FS_Close( pFile );

	return ( size + tokenSize );
}

/*
=============
LoadSaveData

fill the save resore buffer
parse hash strings
=============
*/
static SAVERESTOREDATA *LoadSaveData( const char *level )
{
	int		tokenSize, tableCount;
	int		size, tokenCount;
	char		name[MAX_OSPATH];
	int		id, version;
	int		clientSize;
	SAVERESTOREDATA	*pSaveData;
	int		totalSize;
	file_t		*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL1", level );
	Con_Printf( "Loading game from %s...\n", name );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
	{
		Con_Printf( S_ERROR "Couldn't open save data file %s.\n", name );
		return NULL;
	}

	// Read the header
	FS_Read( pFile, &id, sizeof( int ));
	FS_Read( pFile, &version, sizeof( int ));

	// is this a valid save?
	if( id != SAVEFILE_HEADER || version != SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return NULL;
	}

	// Read the sections info and the data
	FS_Read( pFile, &size, sizeof( int ));		// total size of all data to initialize read buffer
	FS_Read( pFile, &tableCount, sizeof( int ));	// entities count to right initialize entity table
	FS_Read( pFile, &tokenCount, sizeof( int ));	// num hash tokens to prepare token table
	FS_Read( pFile, &tokenSize, sizeof( int ));	// total size of hash tokens

	// determine highest size of seve-restore buffer
	// because it's used twice: for HL1 and HL2 restore
	clientSize = GetClientDataSize( level );
	totalSize = Q_max( clientSize, ( size + tokenSize ));

	// init the read buffer
	pSaveData = SaveInit( totalSize, tokenCount );

	Q_strncpy( pSaveData->szCurrentMapName, level, sizeof( pSaveData->szCurrentMapName ));
	pSaveData->tableCount = tableCount;		// count ETABLE entries
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	// Set up the restore basis
	pSaveData->fUseLandmark = true;
	pSaveData->time = 0.0f;

	// now reading all the rest of data
	FS_Read( pFile, pSaveData->pBaseData, size );
	FS_Close( pFile ); // data is sucessfully moved into SaveRestore buffer (ETABLE will be init later)

	return pSaveData;
}

/*
=============
ParseSaveTables

reading global data, setup ETABLE's
=============
*/
static void ParseSaveTables( SAVERESTOREDATA *pSaveData, SAVE_HEADER *pHeader, int updateGlobals )
{
	SAVE_LIGHTSTYLE	light;
	int		i;

	// Re-base the savedata since we re-ordered the entity/table / restore fields
	InitEntityTable( pSaveData, pSaveData->tableCount );

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ETABLE", &pSaveData->pTable[i], gEntityTable, ARRAYSIZE( gEntityTable ));
		pSaveData->pTable[i].pent = NULL;
	}

	pSaveData->pBaseData = pSaveData->pCurrentData;
	pSaveData->size = 0;

	// process SAVE_HEADER
	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "Save Header", pHeader, gSaveHeader, ARRAYSIZE( gSaveHeader ));

	pSaveData->connectionCount = pHeader->connectionCount;
	VectorClear( pSaveData->vecLandmarkOffset );
	pSaveData->time = pHeader->time;
	pSaveData->fUseLandmark = true;

	// read adjacency list
	for( i = 0; i < pSaveData->connectionCount; i++ )
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ADJACENCY", &pSaveData->levelList[i], gAdjacency, ARRAYSIZE( gAdjacency ));

	if( updateGlobals )
		memset( sv.lightstyles, 0, sizeof( sv.lightstyles ));

	for( i = 0; i < pHeader->lightStyleCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "LIGHTSTYLE", &light, gLightStyle, ARRAYSIZE( gLightStyle ));
		if( updateGlobals ) SV_SetLightStyle( light.index, light.style, light.time );
	}
}

/*
=============
EntityPatchWrite

write out the list of entities that are no longer in the save file for this level
(they've been moved to another level)
=============
*/
static qboolean EntityPatchWrite( SAVERESTOREDATA *pSaveData, const char *level )
{
	char	name[MAX_QPATH];
	int	i, size = 0;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		return false;
	}

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		if( FBitSet( pSaveData->pTable[i].flags, FENTTABLE_REMOVED ))
			size++;
	}

	// patch count
	FS_Write( pFile, &size, sizeof( int ));

	for( i = 0; i < pSaveData->tableCount; i++ )
	{
		if( FBitSet( pSaveData->pTable[i].flags, FENTTABLE_REMOVED ))
			FS_Write( pFile, &i, sizeof( int ));
	}

	FS_Close( pFile );

	return true;
}

/*
=============
EntityPatchRead

read the list of entities that are no longer in the save file for this level
(they've been moved to another level)
=============
*/
static void EntityPatchRead( SAVERESTOREDATA *pSaveData, const char *level )
{
	char	name[MAX_QPATH];
	int	size;
	file_t	*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL3", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return;

	// patch count
	FS_Read( pFile, &size, sizeof( int ));

	for( int i = 0; i < size; i++ )
	{
		int	entityId;

		FS_Read( pFile, &entityId, sizeof( int ));
		pSaveData->pTable[entityId].flags = FENTTABLE_REMOVED;
	}

	FS_Close( pFile );
}

/*
=============
RestoreDecal

restore decal\move across transition
=============
*/
static void RestoreDecal( SAVERESTOREDATA *pSaveData, decallist_t *entry, qboolean adjacent )
{
	int	decalIndex, entityIndex = 0;
	int	flags = entry->flags;
	int	modelIndex = 0;
	edict_t	*pEdict;

	// never move permanent decals
	if( adjacent && FBitSet( flags, FDECAL_PERMANENT ))
		return;

	// restore entity and model index
	pEdict = EdictFromTable( pSaveData, entry->entityIndex );

	if( SV_RestoreCustomDecal( entry, pEdict, adjacent ))
		return; // decal was sucessfully restored at the game-side

	// studio decals are handled at game-side
	if( FBitSet( flags, FDECAL_STUDIO ))
		return;

	if( SV_IsValidEdict( pEdict ))
		modelIndex = pEdict->v.modelindex;

	if( SV_IsValidEdict( pEdict ))
		entityIndex = NUM_FOR_EDICT( pEdict );

	decalIndex = pfnDecalIndex( entry->name );

	// this can happens if brush entity from previous level was turned into world geometry
	if( adjacent && entry->entityIndex != 0 && !SV_IsValidEdict( pEdict ))
	{
		trace_t	tr;

		Con_Printf( S_ERROR "RestoreDecal: couldn't restore entity index %i\n", entry->entityIndex );

		vec3_t testspot = Vec3( entry->position );
		VectorMA( testspot, 5.0f, entry->impactPlaneNormal, testspot );

		vec3_t testend = Vec3( entry->position );
		VectorMA( testend, -5.0f, entry->impactPlaneNormal, testend );

		tr = SV_Move( testspot, vec3_origin, vec3_origin, testend, MOVE_NOMONSTERS, NULL, false );

		// NOTE: this code may does wrong result on moving brushes e.g. func_tracktrain
		if( tr.fraction != 1.0f && !tr.allsolid )
		{
			// check impact plane normal
			float	dot = DotProduct( entry->impactPlaneNormal, tr.plane.normal );

			if( dot >= 0.95f )
			{
				entityIndex = pfnIndexOfEdict( tr.ent );
				if( entityIndex > 0 ) modelIndex = tr.ent->v.modelindex;
				SV_CreateDecal( &sv.signon, tr.endpos, decalIndex, entityIndex, modelIndex, flags, entry->scale );
			}
		}
	}
	else
	{
		// global entity is exist on new level so we can apply decal in local space
		SV_CreateDecal( &sv.signon, entry->position, decalIndex, entityIndex, modelIndex, flags, entry->scale );
	}
}

/*
=============
RestoreSound

continue playing sound from saved position
=============
*/
static void RestoreSound( SAVERESTOREDATA *pSaveData, soundlist_t *snd )
{
	edict_t	*ent = EdictFromTable( pSaveData, snd->entnum );
	int	flags = SND_RESTORE_POSITION;

	// this can happens if serialized map contain 4096 static decals...
	if( MSG_GetNumBytesLeft( &sv.signon ) < 36 )
		return;

	if( !snd->looping )
		SetBits( flags, SND_STOP_LOOPING );

	if( SV_BuildSoundMsg( &sv.signon, ent, snd->channel, snd->name, snd->volume * 255, snd->attenuation, flags, snd->pitch, snd->origin ))
	{
		// write extradata for svc_restoresound
		MSG_WriteByte( &sv.signon, snd->wordIndex );
		MSG_WriteBytes( &sv.signon, &snd->samplePos, sizeof( snd->samplePos ));
		MSG_WriteBytes( &sv.signon, &snd->forcedEnd, sizeof( snd->forcedEnd ));
	}
}

/*
=============
SaveClientState

write out the list of premanent decals for this level
=============
*/
static qboolean SaveClientState( SAVERESTOREDATA *pSaveData, const char *level, int changelevel )
{
	soundlist_t	soundInfo[MAX_CHANNELS];
	sv_client_t	*cl = svs.clients;
	char		name[MAX_QPATH];
	int		i;
	char		*pTokenData;
	decallist_t	*decalList = NULL;
	SAVE_CLIENT	header = { 0 };
	file_t		*pFile;

	// clearing the saving buffer to reuse
	SaveClear( pSaveData );

	header.entityCount = sv.num_static_entities;

	// initialize client header
#if !XASH_DEDICATED
	if( !Host_IsDedicated( ))
	{
		// g-cont. add space for studiodecals if present
		decalList = (decallist_t *)Mem_Calloc( host.mempool, sizeof( decallist_t ) * MAX_RENDER_DECALS * 2 );

		header.decalCount = ref.dllFuncs.R_CreateDecalList( decalList );

		if( !changelevel ) // sounds won't going across transition
		{
			header.soundCount = S_GetCurrentDynamicSounds( soundInfo, MAX_CHANNELS );

			// music not reqiured to save position: it's just continue playing on a next level
			S_StreamGetCurrentState(
				header.introTrack, sizeof( header.introTrack ),
				header.mainTrack, sizeof( header.mainTrack ),
				&header.trackPosition );
		}
	}
#endif // XASH_DEDICATED

	// save viewentity to allow camera works after save\restore
	if( svs.maxclients == 1 && SV_IsValidEdict( cl->pViewEntity ) && cl->pViewEntity != cl->edict )
		header.viewentity = NUM_FOR_EDICT( cl->pViewEntity );

	header.wateralpha = sv_wateralpha.value;
	header.wateramp = sv_wateramp.value;

	// Store the client header
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ClientHeader", &header, gSaveClient, ARRAYSIZE( gSaveClient ));

	// store decals
	for( i = 0; decalList != NULL && i < header.decalCount; i++ )
	{
		// NOTE: apply landmark offset only for brush entities without origin brushes
		if( pSaveData->fUseLandmark && FBitSet( decalList[i].flags, FDECAL_USE_LANDMARK ))
			VectorSubtract( decalList[i].position, pSaveData->vecLandmarkOffset, decalList[i].position );

		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "DECALLIST", &decalList[i], gDecalEntry, ARRAYSIZE( gDecalEntry ));
	}

	if( decalList )
		Mem_Free( decalList );

	// write client entities
	for( i = 0; i < header.entityCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "STATICENTITY", &svs.static_entities[i], gStaticEntry, ARRAYSIZE( gStaticEntry ));

	// write sounds
	for( i = 0; i < header.soundCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "SOUNDLIST", &soundInfo[i], gSoundEntry, ARRAYSIZE( gSoundEntry ));

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		return false;
	}

	int version = CLIENT_SAVEGAME_VERSION;
	int id = SAVEGAME_HEADER;

	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));
	FS_Write( pFile, &pSaveData->size, sizeof( int )); // does not include token table

	// write out the tokens first so we can load them before we load the entities
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );
	FS_Write( pFile, pSaveData->pBaseData, pSaveData->size ); // header and globals
	FS_Close( pFile );

	return true;
}

/*
=============
LoadClientState

read the list of decals and reapply them again
=============
*/
static void LoadClientState( SAVERESTOREDATA *pSaveData, const char *level, qboolean changelevel, qboolean adjacent )
{
	int		tokenCount, tokenSize;
	int		i, size, id, version;
	sv_client_t	*cl = svs.clients;
	char		name[MAX_QPATH];
	soundlist_t	soundEntry;
	decallist_t	decalEntry;
	SAVE_CLIENT	header;
	file_t		*pFile;

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL2", level );

	if(( pFile = FS_Open( name, "rb", true )) == NULL )
		return; // something bad is happens

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != CLIENT_SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));

	// sanity check
	ASSERT( pSaveData->bufferSize >= ( size + tokenSize ));

	// clearing the restore buffer to reuse
	SaveClear( pSaveData );
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	FS_Read( pFile, pSaveData->pBaseData, size );
	FS_Close( pFile );

	// Read the client header
	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ClientHeader", &header, gSaveClient, ARRAYSIZE( gSaveClient ));

	// restore decals
	for( i = 0; i < header.decalCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "DECALLIST", &decalEntry, gDecalEntry, ARRAYSIZE( gDecalEntry ));

		// NOTE: apply landmark offset only for brush entities without origin brushes
		if( pSaveData->fUseLandmark && FBitSet( decalEntry.flags, FDECAL_USE_LANDMARK ))
			VectorAdd( decalEntry.position, pSaveData->vecLandmarkOffset, decalEntry.position );
		RestoreDecal( pSaveData, &decalEntry, adjacent );
	}

	// clear old entities
	if( !adjacent )
	{
		memset( svs.static_entities, 0, sizeof( entity_state_t ) * MAX_STATIC_ENTITIES );
		sv.num_static_entities = 0;
	}

	// restore client entities
	for( i = 0; i < header.entityCount; i++ )
	{
		id = sv.num_static_entities;
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "STATICENTITY", &svs.static_entities[id], gStaticEntry, ARRAYSIZE( gStaticEntry ));
		if( adjacent ) continue; // static entities won't loading from adjacent levels

		if( SV_CreateStaticEntity( &sv.signon, id ))
			sv.num_static_entities++;
	}

	// restore sounds
	for( i = 0; i < header.soundCount; i++ )
	{
		svgame.dllFuncs.pfnSaveReadFields( pSaveData, "SOUNDLIST", &soundEntry, gSoundEntry, ARRAYSIZE( gSoundEntry ));
		if( adjacent ) continue; // sounds don't going across the levels

		RestoreSound( pSaveData, &soundEntry );
	}

	if( !adjacent )
	{
		// restore camera view here
		edict_t	*pent = pSaveData->pTable[bound( 0, (word)header.viewentity, pSaveData->tableCount - 1 )].pent;

		if( !COM_StringEmpty( header.introTrack ))
		{
			// NOTE: music is automatically goes across transition, never restore it on changelevel
			MSG_BeginServerCmd( &sv.signon, svc_stufftext );
			MSG_WriteStringf( &sv.signon, "music \"%s\" \"%s\" %i\n", header.introTrack, header.mainTrack, header.trackPosition );
		}

		// don't go camera across the levels
		if( svs.maxclients == 1 && header.viewentity > svs.maxclients && !changelevel )
			cl->pViewEntity = pent;

		// restore some client cvars
		Cvar_SetValue( "sv_wateralpha", header.wateralpha );
		Cvar_SetValue( "sv_wateramp", header.wateramp );
	}
}

/*
=============
CreateEntitiesInRestoreList

alloc private data for restored entities
=============
*/
static void CreateEntitiesInRestoreList( SAVERESTOREDATA *pSaveData, int levelMask, qboolean create_world )
{
	int		i, active;
	ENTITYTABLE	*pTable;
	edict_t		*pent;

	// create entity list
	if( svgame.physFuncs.pfnCreateEntitiesInRestoreList != NULL && !SV_CoF_TransitionActive() )
	{
		svgame.physFuncs.pfnCreateEntitiesInRestoreList( pSaveData, levelMask, create_world );
	}
	else
	{
		for( i = 0; i < pSaveData->tableCount; i++ )
		{
			pTable = &pSaveData->pTable[i];
			pent = NULL;

			if( SV_CoF_TransitionActive() )
			{
				qboolean player = FBitSet( pTable->flags, FENTTABLE_PLAYER ) || (pTable->id > 0 && pTable->id <= svs.maxclients);
				// A hub snapshot owns its world, never another connection's player
				// or the inventory that player carried away on a previous visit.
				if( CoF_TransitionSkipSavedEntity( create_world, cof_transition.incoming, player, FBitSet( pTable->flags, FENTTABLE_COF_INVENTORY )))
				{
					pTable->pent = NULL;
					continue;
				}
				if( player )
				{
					int slot = pTable->id - 1;
					if( slot < 0 || slot >= svs.maxclients || !CoF_TransitionMatches( &cof_transition.tickets, svs.spawncount, slot, svs.clients[slot].userid ))
					{
						pTable->pent = NULL;
						continue;
					}
				}
			}

			if( pTable->classname && pTable->size && ( !FBitSet( pTable->flags, FENTTABLE_REMOVED ) || !create_world ))
			{
				if( !create_world )
					active = FBitSet( pTable->flags, levelMask ) ? 1 : 0;
				else active = 1;

				if( pTable->id == 0 && create_world ) // worldspawn
				{
					pent = SV_EdictNum( 0 );
					SV_InitEdict( pent );
					pent = SV_CreateNamedEntity( pent, pTable->classname );
				}
				else if(( pTable->id > 0 ) && ( pTable->id < svs.maxclients + 1 ))
				{
					edict_t	*ed = SV_EdictNum( pTable->id );

					if( !FBitSet( pTable->flags, FENTTABLE_PLAYER ))
						Con_Printf( S_ERROR "ENTITY IS NOT A PLAYER: %d\n", i );

					// create the player
					if( active && SV_IsValidEdict( ed ))
						pent = SV_CreateNamedEntity( ed, pTable->classname );
				}
				else if( active )
				{
					pent = SV_CreateNamedEntity( NULL, pTable->classname );
				}
			}

			pTable->pent = pent;
		}
	}
}

/*
=============
SaveGameState

save current game state
=============
*/
static SAVERESTOREDATA *SaveGameState( int changelevel )
{
	char		name[MAX_QPATH];
	int		i, id, version;
	char		*pTableData;
	char		*pTokenData;
	SAVERESTOREDATA	*pSaveData;
	int		tableSize;
	int		dataSize;
	ENTITYTABLE	*pTable;
	SAVE_HEADER	header;
	SAVE_LIGHTSTYLE	light;
	file_t		*pFile;

	if( !svgame.dllFuncs.pfnParmsChangeLevel )
		return NULL;

	pSaveData = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.HL1", sv.name );
	COM_FixSlashes( name );

	// initialize entity table to count moved entities
	InitEntityTable( pSaveData, svgame.numEntities );

	// Build the adjacent map list
	svgame.dllFuncs.pfnParmsChangeLevel();

	if( SV_CoF_TransitionActive() )
	{
		int connection;
		for( connection = 0; connection < pSaveData->connectionCount; connection++ )
			if( !Q_stricmp( pSaveData->levelList[connection].mapName, cof_transition.destination ) && !Q_strcmp( pSaveData->levelList[connection].landmarkName, cof_transition.landmark )) break;
		if( connection == pSaveData->connectionCount )
		{
			// A one-way map need not publish a reciprocal transition. Reserve a
			// real landmark entry rather than falling back to a destructive load.
			if( connection >= MAX_LEVEL_CONNECTIONS )
			{
				Con_Printf( S_ERROR "[cof-story-transition] rejected: no free landmark connection\n" );
				SaveFinish( pSaveData );
				return NULL;
			}
			pSaveData->connectionCount++;
			Q_strncpy( pSaveData->levelList[connection].mapName, cof_transition.destination, sizeof( pSaveData->levelList[connection].mapName ));
			Q_strncpy( pSaveData->levelList[connection].landmarkName, cof_transition.landmark, sizeof( pSaveData->levelList[connection].landmarkName ));
		}
		cof_transition.connection = connection;
		VectorCopy( cof_transition.source_origin, pSaveData->levelList[connection].vecLandmarkOrigin );
	}

	// Write the global data
	header.skillLevel = (int)skill.value;	// this is created from an int even though it's a float
	header.entityCount = pSaveData->tableCount;
	header.connectionCount = pSaveData->connectionCount;
	header.time = svgame.globals->time;	// use DLL time
	Q_strncpy( header.mapName, sv.name, sizeof( header.mapName ));
	Q_strncpy( header.skyName, sv_skyname.string, sizeof( header.skyName ));
	header.skyColor_r = sv_skycolor_r.value;
	header.skyColor_g = sv_skycolor_g.value;
	header.skyColor_b = sv_skycolor_b.value;
	header.skyVec_x = sv_skyvec_x.value;
	header.skyVec_y = sv_skyvec_y.value;
	header.skyVec_z = sv_skyvec_z.value;
	header.lightStyleCount = 0;

	// counting the lightstyles
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		if( sv.lightstyles[i].pattern[0] )
			header.lightStyleCount++;
	}

	// Write the main header
	pSaveData->time = 0.0f; // prohibits rebase of header.time (keep compatibility with old saves)
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "Save Header", &header, gSaveHeader, ARRAYSIZE( gSaveHeader ));
	pSaveData->time = header.time;

	// Write the adjacency list
	for( i = 0; i < pSaveData->connectionCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ADJACENCY", &pSaveData->levelList[i], gAdjacency, ARRAYSIZE( gAdjacency ));

	// Write the lightstyles
	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		if( !sv.lightstyles[i].pattern[0] )
			continue;

		Q_strncpy( light.style, sv.lightstyles[i].pattern, sizeof( light.style ));
		light.time = sv.lightstyles[i].time;
		light.index = i;

		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "LIGHTSTYLE", &light, gLightStyle, ARRAYSIZE( gLightStyle ));
	}

	// build the table of entities
	// this is used to turn pointers into savable indices
	// build up ID numbers for each entity, for use in pointer conversions
	// if an entity requires a certain edict number upon restore, save that as well
	for( i = 0; i < svgame.numEntities; i++ )
	{
		pTable = &pSaveData->pTable[i];
		pTable->location = pSaveData->size;
		pSaveData->currentIndex = i;
		pTable->size = 0;

		if( !SV_IsValidEdict( pTable->pent ))
			continue;

		svgame.dllFuncs.pfnSave( pTable->pent, pSaveData );
		CoF_SessionSavedEntity( pTable, i );

		if( FBitSet( pTable->pent->v.flags, FL_CLIENT ))
			SetBits( pTable->flags, FENTTABLE_PLAYER );

		if( SV_CoF_TransitionActive() && i < cof_transition.entity_count )
		{
			qboolean player = i > 0 && i <= svs.maxclients && CoF_TransitionMatches( &cof_transition.tickets, cof_transition.tickets.epoch, i - 1, svs.clients[i - 1].userid );
			if( player || cof_transition.inventory_owner[i] )
			{
				if( !pTable->size )
				{
					Con_Printf( S_ERROR "[cof-story-transition] rejected: entity %d (%s) has no saved state\n", i, SV_GetString( pTable->pent->v.classname ));
					SaveFinish( pSaveData );
					return NULL;
				}
				SetBits( pTable->flags, BIT( cof_transition.connection ));
				if( cof_transition.inventory_owner[i] ) SetBits( pTable->flags, FENTTABLE_COF_INVENTORY );
			}
		}
	}

	// total data what includes:
	if( SV_CoF_TransitionActive() )
	{
		for( i = 0; i < svs.maxclients; i++ )
		{
			if( cof_transition.tickets.players[i].phase != COF_T_CAPTURED ) continue;
			if( i + 1 >= pSaveData->tableCount || !pSaveData->pTable[i + 1].size || !FBitSet( pSaveData->pTable[i + 1].flags, FENTTABLE_PLAYER ))
			{
				Con_Printf( S_ERROR "[cof-story-transition] rejected: player %d not serialized\n", i + 1 );
				SaveFinish( pSaveData );
				return NULL;
			}
		}
	}

	// total data what includes:
	// 1. save header
	// 2. adjacency list
	// 3. lightstyles
	// 4. all the entity data
	dataSize = pSaveData->size;

	// Write entity table
	pTableData = pSaveData->pCurrentData;

	for( i = 0; i < pSaveData->tableCount; i++ )
		svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "ETABLE", &pSaveData->pTable[i], gEntityTable, ARRAYSIZE( gEntityTable ));

	tableSize = pSaveData->size - dataSize;

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		SaveFinish( pSaveData );
		return NULL;
	}

	// Write the header -- THIS SHOULD NEVER CHANGE STRUCTURE, USE SAVE_HEADER FOR NEW HEADER INFORMATION
	// THIS IS ONLY HERE TO IDENTIFY THE FILE AND GET IT'S SIZE.
	version = SAVEGAME_VERSION;
	id = SAVEFILE_HEADER;

	// write the header
	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));

	// Write out the tokens and table FIRST so they are loaded in the right order, then write out the rest of the data in the file.
	FS_Write( pFile, &pSaveData->size, sizeof( int ));	// total size of all data to initialize read buffer
	FS_Write( pFile, &pSaveData->tableCount, sizeof( int ));	// entities count to right initialize entity table
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));	// num hash tokens to prepare token table
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));	// total size of hash tokens
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );	// write tokens into the file
	FS_Write( pFile, pTableData, tableSize );		// dump ETABLE structures
	FS_Write( pFile, pSaveData->pBaseData, dataSize );	// and finally store all the other data
	FS_Close( pFile );

	if( !EntityPatchWrite( pSaveData, sv.name ))
	{
		SaveFinish( pSaveData );
		return NULL;
	}

	if( !SaveClientState( pSaveData, sv.name, changelevel ))
	{
		SaveFinish( pSaveData );
		return NULL;
	}

	return pSaveData;
}

/*
=============
LoadGameState

load current game state
=============
*/
static int LoadGameState( char const *level, qboolean changelevel )
{
	SAVERESTOREDATA	*pSaveData;
	SAVE_HEADER	header;

	pSaveData = LoadSaveData( level );
	if( !pSaveData ) return 0; // couldn't load the file

	// must set mapname before calling into DLL
	Q_strncpy( sv.name, level, sizeof( sv.name ));
	svgame.globals->mapname = SV_MakeString( sv.name );

	ParseSaveTables( pSaveData, &header, true );
	EntityPatchRead( pSaveData, level );

	// pause until all clients connect
	sv.loadgame = sv.paused = true;

	Cvar_SetValue( "skill", header.skillLevel );
	Cvar_Set( "sv_skyname", header.skyName );

	// restore sky parms
	Cvar_SetValue( "sv_skycolor_r", header.skyColor_r );
	Cvar_SetValue( "sv_skycolor_g", header.skyColor_g );
	Cvar_SetValue( "sv_skycolor_b", header.skyColor_b );
	Cvar_SetValue( "sv_skyvec_x", header.skyVec_x );
	Cvar_SetValue( "sv_skyvec_y", header.skyVec_y );
	Cvar_SetValue( "sv_skyvec_z", header.skyVec_z );

	// create entity list
	CreateEntitiesInRestoreList( pSaveData, 0, true );
	if( cof_session_loading ) CoF_SessionCreatePlayers( pSaveData );

	// now spawn entities
	for( int i = 0; i < pSaveData->tableCount; i++ )
	{
		ENTITYTABLE	*pTable = &pSaveData->pTable[i];
		edict_t		*pent;

		pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
		pSaveData->size = pTable->location;
		pSaveData->currentIndex = i;
		pent = pTable->pent;

		if( pent != NULL )
		{
			if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 0 ) < 0 )
			{
				SetBits( pent->v.flags, FL_KILLME );
				pTable->pent = NULL;
			}
			else
			{
				// force the entity to be relinked
//				SV_LinkEdict( pent, false );
			}
		}
	}

	if( cof_session_loading ) CoF_SessionPlayersRestored( pSaveData );
	LoadClientState( pSaveData, level, changelevel, false );

	SaveFinish( pSaveData );

	// restore server time
	sv.time = header.time;

	return 1;
}

/*
=============
SaveGameSlot

do a save game
=============
*/
static qboolean SaveGameSlot( const char *pSaveName, const char *pSaveComment )
{
	char		hlPath[MAX_QPATH];
	char		name[MAX_QPATH];
	char		*pTokenData;
	SAVERESTOREDATA	*pSaveData;
	GAME_HEADER	gameHeader;
	file_t		*pFile;

	pSaveData = SaveGameState( false );
	if( !pSaveData )
		return false;

	SaveFinish( pSaveData );
	pSaveData = SaveInit( SAVE_HEAPSIZE, SAVE_HASHSTRINGS ); // re-init the buffer

	Q_strncpy( hlPath, DEFAULT_SAVE_DIRECTORY "*.HL?", sizeof( hlPath ) );
	Q_strncpy( gameHeader.mapName, sv.name, sizeof( gameHeader.mapName )); // get the name of level where a player
	Q_strncpy( gameHeader.comment, pSaveComment, sizeof( gameHeader.comment ));
	gameHeader.mapCount = DirectoryCount( hlPath ); // counting all the adjacency maps

	// Store the game header
	svgame.dllFuncs.pfnSaveWriteFields( pSaveData, "GameHeader", &gameHeader, gGameHeader, ARRAYSIZE( gGameHeader ));

	// Write the game globals
	svgame.dllFuncs.pfnSaveGlobalState( pSaveData );

	// Write entity string token table
	pTokenData = StoreHashTable( pSaveData );

	Q_snprintf( name, sizeof( name ), DEFAULT_SAVE_DIRECTORY "%s.sav", pSaveName );
	COM_FixSlashes( name );

	// output to disk
	if( !Q_stricmp( pSaveName, "quick" ))
		AgeSaveList( pSaveName, GI->quicksave_aged_count );
	else if( !Q_stricmp( pSaveName, "autosave" ))
		AgeSaveList( pSaveName, GI->autosave_aged_count );

	// output to disk
	if(( pFile = FS_Open( name, "wb", true )) == NULL )
	{
		Con_Printf( S_ERROR "%s: can't open %s for write\n", __func__, name );
		SaveFinish( pSaveData );
		return false;
	}

	// pending the preview image for savegame
	Cbuf_AddTextf( "saveshot \"%s\"\n", pSaveName );
	Con_Printf( "Saving game to %s...\n", name );

	int version = SAVEGAME_VERSION;
	int id = SAVEGAME_HEADER;

	FS_Write( pFile, &id, sizeof( id ));
	FS_Write( pFile, &version, sizeof( version ));
	FS_Write( pFile, &pSaveData->size, sizeof( int )); // does not include token table

	// write out the tokens first so we can load them before we load the entities
	FS_Write( pFile, &pSaveData->tokenCount, sizeof( int ));
	FS_Write( pFile, &pSaveData->tokenSize, sizeof( int ));
	FS_Write( pFile, pTokenData, pSaveData->tokenSize );
	FS_Write( pFile, pSaveData->pBaseData, pSaveData->size ); // header and globals

	DirectoryCopy( hlPath, pFile );
	SaveFinish( pSaveData );
	FS_Close( pFile );

	return true;
}

/*
=============
SaveReadHeader

read header of .sav file
=============
*/
static int SaveReadHeader( file_t *pFile, GAME_HEADER *pHeader )
{
	int		tokenCount, tokenSize;
	int		size, id, version;
	SAVERESTOREDATA	*pSaveData;

	FS_Read( pFile, &id, sizeof( id ));
	if( id != SAVEGAME_HEADER )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &version, sizeof( version ));
	if( version != SAVEGAME_VERSION )
	{
		FS_Close( pFile );
		return 0;
	}

	FS_Read( pFile, &size, sizeof( int ));
	FS_Read( pFile, &tokenCount, sizeof( int ));
	FS_Read( pFile, &tokenSize, sizeof( int ));

	pSaveData = SaveInit( size + tokenSize, tokenCount );
	pSaveData->tokenCount = tokenCount;
	pSaveData->tokenSize = tokenSize;

	// Parse the symbol table
	BuildHashTable( pSaveData, pFile );

	// Set up the restore basis
	pSaveData->fUseLandmark = false;
	pSaveData->time = 0.0f;

	FS_Read( pFile, pSaveData->pBaseData, size );

	svgame.dllFuncs.pfnSaveReadFields( pSaveData, "GameHeader", pHeader, gGameHeader, ARRAYSIZE( gGameHeader ));

	svgame.dllFuncs.pfnRestoreGlobalState( pSaveData );

	SaveFinish( pSaveData );

	return 1;
}

/*
=============
CreateEntityTransitionList

moving edicts to another level
=============
*/
static int CreateEntityTransitionList( SAVERESTOREDATA *pSaveData, int levelMask )
{
	int		movedCount;

	movedCount = 0;

	// create entity list
	CreateEntitiesInRestoreList( pSaveData, levelMask, false );

	// now spawn entities
	for( int i = 0; i < pSaveData->tableCount; i++ )
	{
		ENTITYTABLE	*pTable = &pSaveData->pTable[i];
		edict_t		*pent;

		pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
		pSaveData->size = pTable->location;
		pSaveData->currentIndex = i;
		pent = pTable->pent;

		if( SV_IsValidEdict( pent ) && FBitSet( pTable->flags, levelMask )) // screen out the player if he's not to be spawned
		{
			if( FBitSet( pTable->flags, FENTTABLE_GLOBAL ))
			{
				entvars_t	tmpVars;
				edict_t	*pNewEnt;

				// NOTE: we need to update table pointer so decals on the global entities with brush models can be
				// correctly moved. found the classname and the globalname for our globalentity
				svgame.dllFuncs.pfnSaveReadFields( pSaveData, "ENTVARS", &tmpVars, gTempEntvars, ARRAYSIZE( gTempEntvars ));

				// reset the save pointers, so dll can read this too
				pSaveData->pCurrentData = pSaveData->pBaseData + pTable->location;
				pSaveData->size = pTable->location;

				// IMPORTANT: we should find the already spawned or local restored global entity
				pNewEnt = SV_FindGlobalEntity( tmpVars.classname, tmpVars.globalname );

				Con_DPrintf( "Merging changes for global: %s\n", SV_GetString( pTable->classname ));

				// -------------------------------------------------------------------------
				// Pass the "global" flag to the DLL to indicate this entity should only override
				// a matching entity, not be spawned
				if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 1 ) > 0 )
				{
					movedCount++;
				}
				else
				{
					if( SV_IsValidEdict( pNewEnt )) // update the table so decals can find parent entity
						pTable->pent = pNewEnt;
					SetBits( pent->v.flags, FL_KILLME );
				}
			}
			else
			{
				Con_Reportf( "Transferring %s (%d)\n", SV_GetString( pTable->classname ), NUM_FOR_EDICT( pent ));

				if( svgame.dllFuncs.pfnRestore( pent, pSaveData, 0 ) < 0 )
				{
					if( SV_CoF_TransitionActive() && cof_transition.incoming && FBitSet( pTable->flags, FENTTABLE_PLAYER|FENTTABLE_COF_INVENTORY ))
						Host_Error( "Story transition failed to restore player/inventory entity %d; source snapshot retained\n", pTable->id );
					SetBits( pent->v.flags, FL_KILLME );
				}
				else
				{
					if( !FBitSet( pTable->flags, FENTTABLE_PLAYER|FENTTABLE_COF_INVENTORY ) && EntityInSolid( pent ))
					{
						// this can happen during normal processing - PVS is just a guess,
						// some map areas won't exist in the new map
						Con_Reportf( "Suppressing %s\n", SV_GetString( pTable->classname ));
						SetBits( pent->v.flags, FL_KILLME );
					}
					else
					{
						if( SV_CoF_TransitionActive() && cof_transition.incoming && FBitSet( pTable->flags, FENTTABLE_PLAYER ))
						{
							int slot = pTable->id - 1;
							if( slot >= 0 && slot < svs.maxclients && CoF_TransitionRestored( &cof_transition.tickets, svs.spawncount, slot, svs.clients[slot].userid ))
								Con_Printf( "[cof-story-transition] phase=restore generation=%u slot=%d userid=%d map=%s origin=%.3f,%.3f,%.3f\n", svs.spawncount, slot + 1, svs.clients[slot].userid, sv.name, pent->v.origin[0], pent->v.origin[1], pent->v.origin[2] );
						}
						pTable->flags = FENTTABLE_REMOVED;
						movedCount++;
					}
				}
			}

			// remove any entities that were removed using UTIL_Remove()
			// as a result of the above calls to UTIL_RemoveImmediate()
			SV_FreeOldEntities ();
		}
	}

	return movedCount;
}

/*
=============
LoadAdjacentEnts

loading edicts from adjacency levels
=============
*/
static void LoadAdjacentEnts( const char *pOldLevel, const char *pLandmarkName )
{
	SAVE_HEADER	header;
	SAVERESTOREDATA	currentLevelData, *pSaveData;
	int		i, test, flags, index, movedCount = 0;
	qboolean		foundprevious = false;
	vec3_t		landmarkOrigin;

	memset( &currentLevelData, 0, sizeof( SAVERESTOREDATA ));
	svgame.globals->pSaveData = &currentLevelData;
	sv.loadgame = sv.paused = true;

	// build the adjacent map list
	svgame.dllFuncs.pfnParmsChangeLevel();

	for( i = 0; i < currentLevelData.connectionCount + (SV_CoF_TransitionActive() ? 1 : 0); i++ )
	{
		const char *adjacent;
		if( i == currentLevelData.connectionCount )
		{
			if( foundprevious ) break;
			adjacent = pOldLevel; // validated one-way edge: still bring the party
		}
		else adjacent = currentLevelData.levelList[i].mapName;
		// make sure the previous level is in the connection list so we can
		// bring over the player.
		if( !Q_stricmp( adjacent, pOldLevel ))
			foundprevious = true;

		for( test = 0; test < i; test++ )
		{
			// only do maps once
			if( !Q_stricmp( adjacent, currentLevelData.levelList[test].mapName ))
				break;
		}

		// map was already in the list
		if( test < i ) continue;
		if( SV_CoF_TransitionActive() && !CoF_StoryMapSaved( adjacent )) continue;
		cof_transition.incoming = SV_CoF_TransitionActive() && !Q_stricmp( adjacent, pOldLevel );

		pSaveData = LoadSaveData( adjacent );
		if( !pSaveData && cof_transition.incoming )
			Host_Error( "Story transition cannot read source snapshot for %s\n", pOldLevel );

		if( pSaveData )
		{
			ParseSaveTables( pSaveData, &header, false );
			EntityPatchRead( pSaveData, adjacent );

			pSaveData->time = sv.time; // - header.time;
			pSaveData->fUseLandmark = true;
			flags = movedCount = 0;
			index = -1;

			// calculate landmark offset
			if( cof_transition.incoming )
			{
				VectorCopy( cof_transition.destination_origin, landmarkOrigin );
				VectorCopy( cof_transition.source_origin, pSaveData->vecLandmarkOffset );
				SetBits( flags, BIT( cof_transition.connection ));
			}
			else
			{
				LandmarkOrigin( &currentLevelData, landmarkOrigin, pLandmarkName );
				LandmarkOrigin( pSaveData, pSaveData->vecLandmarkOffset, pLandmarkName );
			}
			VectorSubtract( landmarkOrigin, pSaveData->vecLandmarkOffset, pSaveData->vecLandmarkOffset );

			if( !Q_stricmp( adjacent, pOldLevel ))
				SetBits( flags, FENTTABLE_PLAYER );

			while( 1 )
			{
				index = EntryInTable( pSaveData, sv.name, index );
				if( index < 0 ) break;
				SetBits( flags, BIT( index ));
			}

			if( flags ) movedCount = CreateEntityTransitionList( pSaveData, flags );

			// if ents were moved, rewrite entity table to save file
			if( movedCount )
			{
				if( !EntityPatchWrite( pSaveData, adjacent ))
				{
					SaveFinish( pSaveData );

					Host_Error( "Level transition ERROR\nCan't write entity table for %s while transitioning to %s from %s\n",
						adjacent, pOldLevel, sv.name );
				}
			}

			// move the decals from another level
			LoadClientState( pSaveData, adjacent, true, true );

			SaveFinish( pSaveData );
		}
	}

	svgame.globals->pSaveData = NULL;
	cof_transition.incoming = false;
	if( SV_CoF_TransitionActive() )
		for( i = 0; i < svs.maxclients; i++ )
			if( cof_transition.tickets.players[i].phase == COF_T_CAPTURED )
				Host_Error( "Story transition did not restore player slot %d userid %d; source snapshot retained\n", i + 1, cof_transition.tickets.players[i].userid );
	if( SV_CoF_TransitionActive() )
		SV_CoF_PartyTransitionArrive( cof_transition.tickets.epoch );

	if( !foundprevious )
		Host_Error( "Level transition ERROR\nCan't find connection to %s from %s\n", pOldLevel, sv.name );
}

/*
=============
SV_LoadGameState

loading entities from the savegame
=============
*/
int SV_LoadGameState( char const *level )
{
	SV_CoF_TransitionReset( true );
	return LoadGameState( level, false );
}

/*
=============
SV_ClearGameState

clear current game state
=============
*/
void SV_ClearGameState( void )
{
	SV_CoF_TransitionReset( true );
	ClearSaveDir();

	if( svgame.dllFuncs.pfnResetGlobalState != NULL )
		svgame.dllFuncs.pfnResetGlobalState();
}

/*
=============
SV_ChangeLevel
=============
*/
void SV_ChangeLevel( qboolean loadfromsavedgame, const char *mapname, const char *start, qboolean background )
{
	char		level[MAX_QPATH];
	char		oldlevel[MAX_QPATH];
	char		_startspot[MAX_QPATH];
	char		*startspot = NULL;
	SAVERESTOREDATA	*pSaveData = NULL;
	qboolean story = loadfromsavedgame && SV_CoF_StoryCoopEnabled();
	if( CoF_SessionRunLoad() ) return;

	if( sv.state != ss_active )
	{
		Con_Printf( S_ERROR "server not running\n");
		return;
	}

	if( start )
	{
		Q_strncpy( _startspot, start, sizeof( _startspot ));
		startspot = _startspot;
	}

	Q_strncpy( level, mapname, sizeof( level ));
	Q_strncpy( oldlevel, sv.name, sizeof( oldlevel ));

	if( story )
	{
		if( !CoF_BeginStoryTransition( level, startspot )) return;
	}
	else SV_CoF_TransitionReset( true );

	if( loadfromsavedgame )
	{
		// smooth transition in-progress
		svgame.globals->changelevel = true;

		// save the current level's state
		pSaveData = SaveGameState( true );

		if( !pSaveData )
		{
			// make user notice the error
			// do not use Host_Error, so the game progress won't be lost
			Sys_Warn( "Can't write save file for performaing change level; check permissions" );
			svgame.globals->changelevel = false;
			SV_CoF_TransitionReset( false );
			return;
		}
		if( story && !CoF_StoryMapSaved( oldlevel ))
			Q_strncpy( cof_story_maps[cof_story_map_count++], oldlevel, MAX_QPATH );
	}

	if( story )
	{
		// Verify both inputs before destroying the current world. A known hub
		// with an unreadable snapshot must not silently respawn as a fresh map.
		SAVERESTOREDATA *check = LoadSaveData( oldlevel );
		qboolean readable = check != NULL;
		SaveFinish( check );
		if( readable && CoF_StoryMapSaved( level ))
		{
			check = LoadSaveData( level );
			readable = check != NULL;
			SaveFinish( check );
		}
		if( !readable )
		{
			Con_Printf( S_ERROR "[cof-story-transition] rejected: required snapshot unreadable; current world preserved\n" );
			SaveFinish( pSaveData );
			svgame.globals->changelevel = false;
			SV_CoF_TransitionReset( false );
			return;
		}
	}

	SV_InactivateClients ();
	SV_FinalMessage( "", true );
	SV_DeactivateServer ();

	// Deactivation released every client's delta frames; normally the client's
	// fresh connect reallocates them. cof_story_changelevel keeps the connection
	// instead, and SV_ReadPackets ignores a client without frames: its "new"
	// would be acknowledged but never run (black loading screen, no sign-on).
	for( int i = 0; story && i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state >= cs_connected && !FBitSet( cl->flags, FCL_FAKECLIENT ) && !cl->frames )
			cl->frames = Mem_Calloc( host.mempool, sizeof( client_frame_t ) * SV_UPDATE_BACKUP );
	}

	if( !SV_SpawnServer( level, startspot, background ))
	{
		SaveFinish( pSaveData );
		SV_CoF_TransitionReset( false );
		return;
	}

	if( loadfromsavedgame )
	{
		// finish saving gamestate
		SaveFinish( pSaveData );

		if( story && CoF_StoryMapSaved( level ))
		{
			if( !LoadGameState( level, true )) Host_Error( "Story hub snapshot became unreadable: %s\n", level );
		}
		else if( story || !LoadGameState( level, true )) SV_SpawnEntities( level );
		svgame.globals->time = sv.time;
		LoadAdjacentEnts( oldlevel, startspot );

		if( sv_newunit.value && !story )
			ClearSaveDir();
		SV_ActivateServer( false );
	}
	else
	{
		// classic quake changelevel
		svgame.dllFuncs.pfnResetGlobalState();
		SV_SpawnEntities( level );
		SV_ActivateServer( true );
	}
}

/*
=============
SV_LoadGame
=============
*/
qboolean SV_LoadGame( const char *pPath )
{
	qboolean		validload = false;
	GAME_HEADER	gameHeader;
	file_t		*pFile;
	uint		flags;
	qboolean	cofsave = ( pPath && !Q_strnicmp( pPath, DEFAULT_SAVE_DIRECTORY "cofsave", 12 ) &&
		pPath[12] >= '1' && pPath[12] <= '5' );

	if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" ))
		Con_Reportf( "[cof-trace] SV_LoadGame entry: %s\n", pPath );

	if( Host_IsDedicated() )
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" )) Con_Reportf( "[cof-trace] load rejected: dedicated\n" );
		return false;
	}

	if( UI_CreditsActive( ))
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" )) Con_Reportf( "[cof-trace] load rejected: credits\n" );
		return false;
	}

	if( COM_StringEmptyOrNULL( pPath ))
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" )) Con_Reportf( "[cof-trace] load rejected: empty path\n" );
		return false;
	}

	// silently ignore if missed
	if( !FS_FileExists( pPath, true ))
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" )) Con_Reportf( "[cof-trace] load rejected: file missing\n" );
		return false;
	}

	// initialize game if needs
	pFile = FS_Open( pPath, "rb", true );
	if( pFile )
	{
		uint32_t magic = 0;
		FS_Read( pFile, &magic, sizeof( magic )); FS_Close( pFile );
		if( magic == COF_ARCHIVE_MAGIC )
		{
			Con_Printf( "Story co-op checkpoints require cof_load in a story co-op session.\n" );
			return false;
		}
	}
	if( !SV_InitGame( false ))
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" )) Con_Reportf( "[cof-trace] load rejected: SV_InitGame failed\n" );
		return false;
	}

	svs.initialized = true;
	pFile = FS_Open( pPath, "rb", true );

	if( pFile )
	{
		SV_ClearGameState();

		if( SaveReadHeader( pFile, &gameHeader ))
			validload = DirectoryExtract( pFile, gameHeader.mapCount );

		FS_Close( pFile );

		if( validload )
		{
			// now check for map problems
			flags = SV_MapIsValid( gameHeader.mapName, NULL );

			if( FBitSet( flags, MAP_INVALID_VERSION ))
			{
				Con_Printf( S_ERROR "map %s is invalid or not supported\n", gameHeader.mapName );
				validload = false;
			}

			if( !FBitSet( flags, MAP_IS_EXIST ))
			{
				Con_Printf( S_ERROR "map %s doesn't exist\n", gameHeader.mapName );
				validload = false;
			}
		}
	}

	if( !validload )
	{
		if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" ))
			Con_Reportf( "[cof-trace] load rejected: invalid header, extraction, or map\n" );
		Con_Printf( S_ERROR "Couldn't load %s\n", pPath );
		return false;
	}

	if( cofsave && Cvar_VariableInteger( "cof_trace_menu_load" ))
		Con_Reportf( "[cof-trace] load accepted: map=%s\n", gameHeader.mapName );
	Con_Printf( "Loading game from %s...\n", pPath );
	Cvar_FullSet( "maxplayers", "1", FCVAR_LATCH );
	Cvar_SetValue( "deathmatch", 0 );
	Cvar_SetValue( "coop", 0 );
	COM_LoadGame( gameHeader.mapName );

	return true;
}

/*
==================
SV_SaveGame
==================
*/
qboolean SV_SaveGame( const char *pName )
{
	char   comment[80];
	string savename;

	if( COM_StringEmptyOrNULL( pName ))
		return false;

	// can we save at this point?
	if( !IsValidSave( ))
		return false;

	if( !Q_stricmp( pName, "new" ))
	{
		int n;

		// scan for a free filename
		for( n = 0; n < 1000; n++ )
		{
			Q_snprintf( savename, sizeof( savename ), "save%03d", n );

			if( !FS_FileExists( va( DEFAULT_SAVE_DIRECTORY "%s.sav", savename ), true ))
				break;
		}

		if( n == 1000 )
		{
			Con_Printf( S_ERROR "no free slots for savegame\n" );
			return false;
		}
	}
	else Q_strncpy( savename, pName, sizeof( savename ));

#if !XASH_DEDICATED
	// unload previous image from memory (it's will be overwritten)
	GL_FreeImage( va( DEFAULT_SAVE_DIRECTORY "%s.bmp", savename ) );
#endif // XASH_DEDICATED

	SaveBuildComment( comment, sizeof( comment ));
	return SaveGameSlot( savename, comment );
}

/* Menu saves share CoF's five slots, but never enter the tape-use path.
 * The quick save (cof_quicksave) is one more file written the same way, so the
 * transaction is shared: name = the .sav base name, label = its display label
 * in cryoffear/SAVE, title = the label text before " - <timestamp>", what =
 * how the messages name it. */
static qboolean SV_CoFSaveTransaction( const char *name, const char *label, const char *title, const char *what )
{
	char save[MAX_QPATH], saveBackup[MAX_QPATH];
	char labelTemp[MAX_QPATH], labelBackup[MAX_QPATH];
	char stamp[64], text[160];
	qboolean hadSave, hadLabel, saved = false, labelMoved = false;
	file_t *file;
	time_t now;
	struct tm *local;
	fs_offset_t written;
	int length, closeResult;

	/* The original client does not always register game_menu in Xash. */
	if( !Q_stricmp( sv.name, "c_game_menu1" ) || Cvar_VariableInteger( "game_menu" ))
	{
		Con_Printf( "Can't save the Cry of Fear menu map.\n" );
		return false;
	}
	if( !IsValidSave() )
		return false;

	Q_snprintf( save, sizeof( save ), "save/%s.sav", name );
	Q_snprintf( saveBackup, sizeof( saveBackup ), "%s.coffix-backup", save );
	Q_snprintf( labelTemp, sizeof( labelTemp ), "%s.coffix-temp", label );
	Q_snprintf( labelBackup, sizeof( labelBackup ), "%s.coffix-backup", label );
	/* Raw filesystem calls intentionally keep metadata in cryoffear/SAVE. */
	if( SV_SaveFSFileExists( saveBackup, true ) || g_fsapi.FileExists( labelTemp, true ) ||
		g_fsapi.FileExists( labelBackup, true ))
	{
		Con_Printf( S_ERROR "%s has a pending recovery file; resolve it before saving again.\n", what );
		return false;
	}

	now = time( NULL );
	local = localtime( &now );
	if( !local || !strftime( stamp, sizeof( stamp ), "%a %b %d %H:%M:%S %Y", local ))
		return false;
	Q_snprintf( text, sizeof( text ), "%s - %s", title, stamp );
	length = Q_strlen( text );
	file = g_fsapi.Open( labelTemp, "wb", true );
	if( !file )
	{
		Con_Printf( S_ERROR "could not prepare save label %s\n", labelTemp );
		return false;
	}
	written = g_fsapi.Write( file, text, length );
	closeResult = g_fsapi.Close( file );
	if( written != length || closeResult != 0 )
	{
		g_fsapi.Delete( labelTemp );
		Con_Printf( S_ERROR "could not write save label %s\n", labelTemp );
		return false;
	}

	hadSave = SV_SaveFSFileExists( save, true );
	hadLabel = g_fsapi.FileExists( label, true );
	if( hadSave && !SV_SaveFSRename( save, saveBackup ))
	{
		g_fsapi.Delete( labelTemp );
		Con_Printf( S_ERROR "could not preserve existing save %s\n", save );
		return false;
	}
	cofMenuSaveIOFailed = false;
	cofMenuSaveTracking = true;
	saved = SV_SaveGame( name );
	cofMenuSaveTracking = false;
	if( !saved || cofMenuSaveIOFailed ) goto rollback;
	if( hadLabel )
	{
		if( !g_fsapi.Rename( label, labelBackup )) goto rollback;
		labelMoved = true;
	}
	if( !g_fsapi.Rename( labelTemp, label )) goto rollback;
	if( hadSave && !SV_SaveFSDelete( saveBackup ))
		Con_Printf( S_WARN "saved, but retained recovery file %s\n", saveBackup );
	if( labelMoved && !g_fsapi.Delete( labelBackup ))
		Con_Printf( S_WARN "saved, but retained recovery file %s\n", labelBackup );
	return true;

rollback:
	g_fsapi.Delete( labelTemp );
	if( labelMoved && !g_fsapi.Rename( labelBackup, label ))
		Con_Printf( S_ERROR "restore save label manually from %s\n", labelBackup );
	if( saved || SV_SaveFSFileExists( save, true )) SV_SaveFSDelete( save );
	if( hadSave && !SV_SaveFSRename( saveBackup, save ))
		Con_Printf( S_ERROR "restore save manually from %s\n", saveBackup );
	Con_Printf( S_ERROR "CoF %s failed; recovery of the previous files was attempted.\n", what );
	return false;
}

qboolean SV_CoFMenuSave( int slot )
{
	char name[32], label[MAX_QPATH], title[96], what[32];

	if( !SV_CoFRootSaveCompat() || !Cvar_VariableInteger( "cof_pause_menu_saves" ) || slot < 1 || slot > 5 )
	{
		Con_Printf( "Cry of Fear menu saving is disabled or the slot is invalid.\n" );
		return false;
	}

	Q_snprintf( name, sizeof( name ), "cofsave%d", slot );
	Q_snprintf( label, sizeof( label ), "SAVE/saveinfo%d.cof", slot );
	Q_snprintf( title, sizeof( title ), "Pause Save (%.64s)", sv.name );
	Q_snprintf( what, sizeof( what ), "menu save to slot %d", slot );
	if( !SV_CoFSaveTransaction( name, label, title, what ))
		return false;

	Con_Printf( "CoF menu save committed to slot %d.\n", slot );
	return true;
}

/* The quick save: SAVE/cofquick.sav (+ its .bmp) at the runtime root and the
 * label cryoffear/SAVE/saveinfoquick.cof "Quick save (<map>) - <timestamp>".
 * Whether a quick save may happen now (the option, single player, in play, no
 * menu or panel) is decided by the caller, engine/client/cof_quicksave.c. */
qboolean SV_CoFQuickSave( void )
{
	char title[96];

	if( !SV_CoFRootSaveCompat( ))
	{
		Con_Printf( "Quick saves need the Cry of Fear root SAVE folder (cof_save_root_compat 1).\n" );
		return false;
	}

	Q_snprintf( title, sizeof( title ), "Quick save (%.64s)", sv.name );
	if( !SV_CoFSaveTransaction( COF_QUICKSAVE_NAME, "SAVE/saveinfoquick.cof", title, "quick save" ))
		return false;

	Con_Reportf( "CoF quick save committed (%s).\n", sv.name );
	return true;
}

qboolean SV_CoFQuickSaveExists( void )
{
	return SV_CoFRootSaveCompat( ) && SV_SaveFSFileExists( DEFAULT_SAVE_DIRECTORY COF_QUICKSAVE_NAME ".sav", true );
}

static int SV_CompareFileTime( int ft1, int ft2 )
{
	if( ft1 < ft2 )
	{
		return -1;
	}
	else if( ft1 > ft2 )
	{
		return 1;
	}
	return 0;
}

/*
==================
SV_GetLatestSave

used for reload game after player death
==================
*/
const char *SV_GetLatestSave( void )
{
	static char	savename[MAX_QPATH];
	int		newest = 0;
	int		found = 0;
	search_t		*t;

	if(( t = FS_Search( DEFAULT_SAVE_DIRECTORY "*.sav" , true, true )) == NULL )
		return NULL;

	for( int i = 0; i < t->numfilenames; i++ )
	{
		int	ft = FS_FileTime( t->filenames[i], true );

		// found a match?
		if( ft > 0 )
		{
			// should we use the matched?
			if( !found || SV_CompareFileTime( newest, ft ) < 0 )
			{
				Q_strncpy( savename, t->filenames[i], sizeof( savename ));
				newest = ft;
				found = 1;
			}
		}
	}

	Mem_Free( t ); // release search

	if( found )
		return savename;
	return NULL;
}

/*
==================
SV_GetSaveComment

check savegame for valid
==================
*/
int GAME_EXPORT SV_GetSaveComment( const char *savename, char *comment )
{
	int	i, tag, size, nNumberOfFields, nFieldSize, tokenSize, tokenCount;
	char	*pData, *pSaveData, *pFieldName, **pTokenList;
	string	mapName, description;
	file_t	*f;

	/* Route comments through the existing CoF root-save read adapter. */
	if(( f = SV_SaveFSOpen( savename, "rb", true )) == NULL )
	{
		// just not exist - clear comment
		comment[0] = '\0';
		return 0;
	}

	FS_Read( f, &tag, sizeof( int ));
	if( tag != SAVEGAME_HEADER )
	{
		// invalid header
		Q_strncpy( comment, "<corrupted>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	FS_Read( f, &tag, sizeof( int ));

	if( tag == 0x0065 )
	{
		Q_strncpy( comment, "<old version "XASH_ENGINE_NAME" unsupported>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tag < SAVEGAME_VERSION )
	{
		Q_strncpy( comment, "<old version>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tag > SAVEGAME_VERSION )
	{
		// old xash version ?
		Q_strncpy( comment, "<invalid version>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	mapName[0] = '\0';
	comment[0] = '\0';

	FS_Read( f, &size, sizeof( int ));
	FS_Read( f, &tokenCount, sizeof( int ));	// These two ints are the token list
	FS_Read( f, &tokenSize, sizeof( int ));
	size += tokenSize;

	// sanity check.
	if( tokenCount < 0 || tokenCount > SAVE_HASHSTRINGS )
	{
		Q_strncpy( comment, "<corrupted hashtable>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	if( tokenSize < 0 || tokenSize > SAVE_HEAPSIZE )
	{
		Q_strncpy( comment, "<corrupted hashtable>", MAX_STRING );
		FS_Close( f );
		return 0;
	}

	pSaveData = (char *)Mem_Malloc( host.mempool, size );
	FS_Read( f, pSaveData, size );
	pData = pSaveData;

	// allocate a table for the strings, and parse the table
	if( tokenSize > 0 )
	{
		pTokenList = Mem_Calloc( host.mempool, tokenCount * sizeof( char* ));

		// make sure the token strings pointed to by the pToken hashtable.
		for( i = 0; i < tokenCount; i++ )
		{
			pTokenList[i] = *pData ? pData : NULL;	// point to each string in the pToken table
			while( *pData++ );			// find next token (after next null)
		}
	}
	else pTokenList = NULL;

	// short, short (size, index of field name)
	nFieldSize = *(short *)pData;
	pData += sizeof( short );
	pFieldName = pTokenList[*(short *)pData];

	if( Q_stricmp( pFieldName, "GameHeader" ))
	{
		Q_strncpy( comment, "<missing GameHeader>", MAX_STRING );
		if( pTokenList ) Mem_Free( pTokenList );
		if( pSaveData ) Mem_Free( pSaveData );
		FS_Close( f );
		return 0;
	}

	// int (fieldcount)
	pData += sizeof( short );
	nNumberOfFields = (int)*pData;
	pData += nFieldSize;

	// each field is a short (size), short (index of name), binary string of "size" bytes (data)
	for( i = 0; i < nNumberOfFields; i++ )
	{
		size_t size;
		// Data order is:
		// Size
		// szName
		// Actual Data
		nFieldSize = *(short *)pData;
		pData += sizeof( short );

		pFieldName = pTokenList[*(short *)pData];
		pData += sizeof( short );

		size = Q_min( nFieldSize, MAX_STRING );

		if( !Q_stricmp( pFieldName, "comment" ))
		{
			Q_strncpy( description, pData, size );
		}
		else if( !Q_stricmp( pFieldName, "mapName" ))
		{
			Q_strncpy( mapName, pData, size );
		}

		// move to start of next field.
		pData += nFieldSize;
	}

	// delete the string table we allocated
	if( pTokenList ) Mem_Free( pTokenList );
	if( pSaveData ) Mem_Free( pSaveData );
	FS_Close( f );

	// at least mapname should be filled
	if( !COM_StringEmpty( mapName ))
	{
		time_t		fileTime;
		const struct tm	*file_tm;
		string		timestring;
		uint		flags;

		// now check for map problems
		flags = SV_MapIsValid( mapName, NULL );

		if( FBitSet( flags, MAP_INVALID_VERSION ))
		{
			Q_snprintf( comment, MAX_STRING, "<map %s has invalid format>", mapName );
			return 0;
		}

		if( !FBitSet( flags, MAP_IS_EXIST ))
		{
			Q_snprintf( comment, MAX_STRING, "<map %s is missed>", mapName );
			return 0;
		}

		fileTime = FS_FileTime( savename, true );
		file_tm = localtime( &fileTime );

		// split comment to sections
		if( Q_strstr( savename, "quick" ))
			Q_snprintf( comment, CS_SIZE, "[quick]%s", description );
		else if( Q_strstr( savename, "autosave" ))
			Q_snprintf( comment, CS_SIZE, "[autosave]%s", description );
		else Q_strncpy( comment, description, CS_SIZE );
		strftime( timestring, sizeof ( timestring ), "%b%d %Y", file_tm );
		Q_strncpy( comment + CS_SIZE, timestring, CS_TIME );
		strftime( timestring, sizeof( timestring ), "%H:%M", file_tm );
		Q_strncpy( comment + CS_SIZE + CS_TIME, timestring, CS_TIME );
		Q_strncpy( comment + CS_SIZE + (CS_TIME * 2), description + CS_SIZE, CS_SIZE );

		return 1;
	}

	Q_strncpy( comment, "<unknown version>", MAX_STRING );

	return 0;
}

void SV_InitSaveRestore( void )
{
	pfnSaveGameComment = COM_GetProcAddress( svgame.hInstance, "SV_SaveGameComment" );
}

#include "sv_cof_snapshot_io.inc"
#include "sv_cof_session.inc"
