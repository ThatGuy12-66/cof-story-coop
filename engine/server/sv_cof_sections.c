/*
sv_cof_sections.c - Cry of Fear story co-op: merged story maps on the server
GPL-3.0-or-later.

A merged map file holds several door-joined story maps stacked in height, one "section" each (cof_sections.h). Its
level doors keep the original trigger_changelevel entities; a change between two of its sections is done here as
an instant move of the players in that exit and those beside them, by the landmark offset, with no exit hold, save
or load (docs/superpowers/specs/2026-10-16-merged-maps-design.md). A door to a map in another merged file, or out
of one, is resolved to the file to load and the landmark's name there. cof_merged_maps (the host's setting, kept
by each session) decides whether an original map name loads its merged file.
*/
#include "common.h"
#include "server.h"
#include "cof_merged.h"
#include "sv_cof_sections.h"
#include "sv_cof_party.h"
#include "sv_cof_companion.h"
#include "sv_cof_session.h"

#define COF_SECTION_COOLDOWN 1.5    // a player just moved is not moved again: no ping-pong between two exits
#define COF_SECTION_NEAR 128.0f     // partners this close to someone in the exit come along, as at doors
#define COF_SECTION_OPEN_EXITS 32

static CVAR_DEFINE_AUTO( cof_merged_maps, "0", 0, "Cry of Fear story co-op: door-joined story maps load as one map (fewer loading screens); the host's setting, a session keeps its own" );

static cof_sections_t cof_sect_loaded;    // the loaded merged map's rules
static cof_sections_t cof_sect_other;     // scratch: another merged file's rules
static qboolean cof_sect_loaded_ok;
static int cof_sect_generation = -1;
static double cof_sect_moved_at[MAX_CLIENTS];
static int cof_sect_alive[MAX_CLIENTS];   // userid while alive, -userid once dead (a respawn follows)
static int cof_sect_last[MAX_CLIENTS];    // the section a player was last in, alive
static qboolean cof_sect_entered[COF_SECT_MAX];
static int cof_sect_entry = -1;           // the section a map command asked for
static char cof_sect_entry_map[MAX_QPATH];
static struct
{
	char map[MAX_QPATH], landmark[MAX_QPATH];      // what is loaded and the landmark's name there
	char level[MAX_QPATH], source[MAX_QPATH];      // the map the exit names and the landmark's name where asked
} cof_sect_request;
// Exits that led to a section move whose trigger is gone since (a once-only exit): walking into the place it was
// still moves a player, so the party is never cut off by the first one through.
static struct
{
	int entity, serial;
	vec3_t absmin, absmax;
	char level[MAX_QPATH], landmark[MAX_QPATH];
} cof_sect_open[COF_SECTION_OPEN_EXITS];
static int cof_sect_open_count;

void SV_CoF_SectionsInit( void )
{
	Cvar_RegisterVariable( &cof_merged_maps );
}

static const cof_sections_t *CoF_SectLoaded( void )
{
	int i;
	if( cof_sect_generation != svs.spawncount )
	{
		cof_sect_generation = svs.spawncount;
		cof_sect_loaded_ok = sv.state != ss_dead && COM_CoF_LoadSections( sv.name, &cof_sect_loaded );
		for( i = 0; i < MAX_CLIENTS; i++ )
		{
			cof_sect_moved_at[i] = -1000.0;
			cof_sect_alive[i] = 0;
			cof_sect_last[i] = -1;
		}
		memset( cof_sect_entered, 0, sizeof( cof_sect_entered ));
		cof_sect_open_count = 0;
	}
	return cof_sect_loaded_ok ? &cof_sect_loaded : NULL;
}

int SV_CoF_PlayerSection( const edict_t *ent )
{
	const cof_sections_t *s = CoF_SectLoaded();
	return s && ent ? CoF_SectionsAt( s, ent->v.origin[2] ) : -1;
}

// A "play everywhere" sound (no attenuation) of a merged map is for its own section's players only: in the
// original game nobody in another map heard it. -1: everyone as usual (attenuated, stops, sign-on, not merged).
int SV_CoF_SoundSection( float attn, int flags, const vec3_t origin )
{
	const cof_sections_t *s;
	if( attn > 0.0f || FBitSet( flags, SND_STOP | SND_SPAWNING ) || !origin ) return -1;
	s = CoF_SectLoaded();
	return s ? CoF_SectionsAt( s, origin[2] ) : -1;
}

// merged.txt of a story save: the session's mode and the original map its first player is in (the launcher's
// Continue label). Its length, 0 when it does not fit.
size_t SV_CoF_SectionsSessionRecord( char *out, size_t size )
{
	const cof_sections_t *s = CoF_SectLoaded();
	const char *section = sv.name;
	int i;
	for( i = 0; s && i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict )) continue;
		section = s->sec[CoF_SectionsAt( s, cl->edict->v.origin[2] )].map;
		break;
	}
	return CoF_SectionsRecord( out, size, cof_merged_maps.value != 0.0f, section );
}

// A loaded session keeps the mode it was saved with; one saved before merged maps (no record) plays unmerged.
void SV_CoF_SectionsSessionRestore( const byte *record, size_t length )
{
	char section[COF_SECT_NAME];
	int merged = 0;
	CoF_SectionsRecordParse( (const char *)record, record ? length : 0, &merged, section, sizeof( section ));
	Cvar_DirectSet( &cof_merged_maps, merged ? "1" : "0" );
	COM_CoF_SectionsNote( "[cof-sections] session merged=%d\n", merged );
}

// The merged file (with merging on) that holds this original map; its rules in out.
static qboolean CoF_SectFileOf( const char *level, cof_sections_t *out )
{
	search_t *t;
	qboolean found = false;
	int i;
	if( !cof_merged_maps.value || COM_StringEmptyOrNULL( level )) return false;
	if( !( t = FS_Search( "maps/cm_*.sections", true, true ))) return false;
	for( i = 0; i < t->numfilenames && !found; i++ )
	{
		char name[MAX_QPATH];
		COM_FileBase( t->filenames[i], name, sizeof( name ));
		found = FS_FileExists( va( "maps/%s.bsp", name ), false ) && COM_CoF_LoadSections( name, out ) &&
			CoF_SectionsFind( out, level ) >= 0;
	}
	Mem_Free( t );
	return found;
}

static edict_t *CoF_SectStartSpot( const cof_sections_t *s, int section )
{
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( SV_IsValidEdict( ent ) && !Q_strcmp( SV_GetString( ent->v.classname ), "info_player_start" ) &&
			CoF_SectionsAt( s, ent->v.origin[2] ) == section ) return ent;
	}
	return NULL;
}

static void CoF_SectEnter( const cof_sections_t *s, int section, qboolean save )
{
	if( section < 0 || section >= s->count || cof_sect_entered[section] ) return;
	cof_sect_entered[section] = true;
	if( !save ) return;
	Con_Printf( "[cof-story-transition] phase=section-entered map=%s section=%s\n", sv.name, s->sec[section].map );
	SV_CoF_SessionAutosaveSoon();
}

static qboolean CoF_SectAbsTouch( const edict_t *e, const vec3_t absmin, const vec3_t absmax )
{
	int i;
	for( i = 0; i < 3; i++ )
		if( e->v.absmin[i] > absmax[i] || absmin[i] > e->v.absmax[i] ) return false;
	return true;
}

static qboolean CoF_SectIsExit( edict_t *ent, const int *models, int count )
{
	const char *model;
	int i;
	if( !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.classname ), "trigger_changelevel" )) return false;
	model = SV_GetString( ent->v.model );
	for( i = 0; model[0] == '*' && i < count; i++ )
		if( models[i] == Q_atoi( model + 1 )) return true;
	return false;
}

// Remember where this exit's triggers are: its trigger_changelevel entities and the triggers that fire them.
static void CoF_SectRememberExit( const int *models, int count, const char *level, const char *landmark )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_sect_open_count < COF_SECTION_OPEN_EXITS; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		const char *target;
		qboolean exit;
		if( !SV_IsValidEdict( ent ) || Q_strncmp( SV_GetString( ent->v.classname ), "trigger_", 8 )) continue;
		exit = CoF_SectIsExit( ent, models, count );
		target = SV_GetString( ent->v.target );
		for( j = svs.maxclients + 1; !exit && target[0] && j < svgame.numEntities; j++ )
		{
			edict_t *level_ent = SV_EdictNum( j );
			exit = CoF_SectIsExit( level_ent, models, count ) && !Q_strcmp( SV_GetString( level_ent->v.targetname ), target );
		}
		for( j = 0; exit && j < cof_sect_open_count; j++ )
			if( cof_sect_open[j].entity == i && cof_sect_open[j].serial == ent->serialnumber ) exit = false;
		if( !exit ) continue;
		cof_sect_open[cof_sect_open_count].entity = i;
		cof_sect_open[cof_sect_open_count].serial = ent->serialnumber;
		VectorCopy( ent->v.absmin, cof_sect_open[cof_sect_open_count].absmin );
		VectorCopy( ent->v.absmax, cof_sect_open[cof_sect_open_count].absmax );
		Q_strncpy( cof_sect_open[cof_sect_open_count].level, level, sizeof( cof_sect_open[0].level ));
		Q_strncpy( cof_sect_open[cof_sect_open_count].landmark, landmark, sizeof( cof_sect_open[0].landmark ));
		cof_sect_open_count++;
	}
}

static qboolean CoF_SectAlive( const edict_t *e )
{
	return e->v.health > 0 && e->v.deadflag == DEAD_NO;
}

static void CoF_SectMoveOne( const cof_sections_t *s, const cof_sect_route_t *r, int slot, const vec3_t landing, sv_client_t *anchor )
{
	sv_client_t *cl = &svs.clients[slot];
	int from = CoF_SectionsAt( s, cl->edict->v.origin[2] );
	if( !SV_CoF_PartySectionMove( cl, landing, anchor ))
	{
		Con_Printf( S_WARN "[cof-story-transition] phase=section-move-refused userid=%d to=%s reason=no-room\n", cl->userid, s->sec[r->to_section].map );
		return;
	}
	cof_sect_moved_at[slot] = sv.time;
	// original map and landmark names: the run report counts this exit as the original game's
	Con_Printf( "[cof-story-transition] phase=section-move userid=%d from=%s to=%s landmark=%s origin=%.3f,%.3f,%.3f\n", cl->userid,
		s->sec[from].map, s->sec[r->to_section].map, CoF_SectionsOriginalName( &s->sec[r->to_section], r->dest_landmark ),
		cl->edict->v.origin[0], cl->edict->v.origin[1], cl->edict->v.origin[2] );
	CoF_SectEnter( s, r->to_section, true );
}

// The section move itself. forced >= 0: that player is the one in the exit (an exit whose trigger is gone).
static void CoF_SectMove( const cof_sections_t *s, const cof_sect_route_t *r, const char *level, const char *landmark, int forced )
{
	cof_sect_player_t p[MAX_CLIENTS];
	int models[16], movers[MAX_CLIENTS], count, n = Q_min( svs.maxclients, MAX_CLIENTS ), i, anchor = -1;
	vec3_t src, dst, offset, landing;
	qboolean landmarks;
	edict_t *start = NULL;
	count = SV_CoF_MapExitModels( sv.name, level, landmark, models, ARRAYSIZE( models ), NULL, NULL );
	for( i = 0; i < n; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		memset( &p[i], 0, sizeof( p[i] ));
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		p[i].alive = CoF_SectAlive( cl->edict );
		p[i].human = !FBitSet( cl->flags, FCL_FAKECLIENT ) && !SV_CoF_CompanionAway( cl );
		p[i].moved_recently = sv.time < cof_sect_moved_at[i] + COF_SECTION_COOLDOWN;
		p[i].in_exit = forced >= 0 ? i == forced : count > 0 && SV_CoF_PartyInExit( cl->edict, models, count );
		VectorCopy( cl->edict->v.origin, p[i].origin );
	}
	if( !CoF_SectionMovers( p, n, movers, COF_SECTION_NEAR ))
	{
		Con_Printf( "[cof-story-transition] phase=section-move-nobody to=%s landmark=%s\n", level, landmark ? landmark : "" );
		return;
	}
	for( i = 0; i < n && anchor < 0; i++ ) if( movers[i] && p[i].in_exit ) anchor = i;
	for( i = 0; i < n && anchor < 0; i++ ) if( movers[i] ) anchor = i;
	landmarks = r->source_landmark[0] && SV_CoF_LiveLandmark( r->source_landmark, src ) && SV_CoF_LiveLandmark( r->dest_landmark, dst );
	if( landmarks ) VectorSubtract( dst, src, offset );
	else if( !( start = CoF_SectStartSpot( s, r->to_section )))
	{
		Con_Printf( S_ERROR "[cof-story-transition] section-move to %s: no landmark %s there and no start spot\n", s->sec[r->to_section].map, r->dest_landmark );
		return;
	}
	else Con_Printf( "[cof-story-transition] phase=section-move-start to=%s landmark=%s reason=no-landmark\n", s->sec[r->to_section].map, r->dest_landmark );
	// the anchor first; the others land by the offset too, or beside the anchor when that spot is taken
	if( landmarks ) VectorAdd( svs.clients[anchor].edict->v.origin, offset, landing );
	else
	{
		VectorCopy( start->v.origin, landing );
		landing[2] += 1.0f;
	}
	CoF_SectMoveOne( s, r, anchor, landing, NULL );
	for( i = 0; i < n; i++ )
	{
		if( !movers[i] || i == anchor ) continue;
		if( landmarks ) VectorAdd( svs.clients[i].edict->v.origin, offset, landing );
		else VectorCopy( svs.clients[anchor].edict->v.origin, landing );
		CoF_SectMoveOne( s, r, i, landing, &svs.clients[anchor] );
	}
	if( forced < 0 && count ) CoF_SectRememberExit( models, count, level, landmark );
}

qboolean SV_CoF_SectionChangeLevel( const char *level, const char *landmark )
{
	const cof_sections_t *s = CoF_SectLoaded();
	cof_sect_route_t r;
	if( !s || sv.state != ss_active ) return false;
	CoF_SectionsRoute( s, level, landmark, NULL, &r );
	if( !r.section_move ) return false;
	CoF_SectMove( s, &r, level, landmark, -1 );
	return true;
}

qboolean SV_CoF_SectionsResolve( const char *level, const char *landmark, char *file, size_t filesize, char *dest, size_t destsize )
{
	const cof_sections_t *s = CoF_SectLoaded();
	qboolean other = CoF_SectFileOf( level, &cof_sect_other );
	cof_sect_route_t r;
	memset( &cof_sect_request, 0, sizeof( cof_sect_request ));
	if( !s && !other ) return false;
	CoF_SectionsRoute( s, level, landmark ? landmark : "", other ? &cof_sect_other : NULL, &r );
	if( r.section_move ) return false; // not from an exit (a console changelevel): as asked
	Q_strncpy( file, r.file, filesize );
	Q_strncpy( dest, r.dest_landmark, destsize );
	Q_strncpy( cof_sect_request.map, r.file, sizeof( cof_sect_request.map ));
	Q_strncpy( cof_sect_request.landmark, r.dest_landmark, sizeof( cof_sect_request.landmark ));
	Q_strncpy( cof_sect_request.level, level, sizeof( cof_sect_request.level ));
	Q_strncpy( cof_sect_request.source, r.source_landmark, sizeof( cof_sect_request.source ));
	{
		// original names too: the run report counts the exit as the original game's
		int from = s && r.source_landmark[0] ? CoF_SectionsLandmarkSection( s, r.source_landmark ) : -1;
		COM_CoF_SectionsNote( "[cof-sections] changelevel from=%s to=%s landmark=%s file=%s dest=%s\n",
			from >= 0 ? s->sec[from].map : sv.name, level,
			from >= 0 ? CoF_SectionsOriginalName( &s->sec[from], r.source_landmark ) : r.source_landmark, r.file, r.dest_landmark );
	}
	return true;
}

static qboolean CoF_SectRequestIs( const char *map, const char *landmark )
{
	return cof_sect_request.map[0] && !Q_stricmp( map, cof_sect_request.map ) && !Q_strcmp( landmark ? landmark : "", cof_sect_request.landmark );
}

const char *SV_CoF_TransitionSourceLandmark( const char *map, const char *landmark )
{
	return CoF_SectRequestIs( map, landmark ) ? cof_sect_request.source : landmark;
}

const char *SV_CoF_TransitionRequestedLevel( const char *map, const char *landmark )
{
	return CoF_SectRequestIs( map, landmark ) ? cof_sect_request.level : map;
}

void SV_CoF_SectionsMapCommand( char *mapname, size_t size )
{
	cof_sect_entry = -1;
	if( !CoF_SectFileOf( mapname, &cof_sect_other )) return;
	cof_sect_entry = CoF_SectionsFind( &cof_sect_other, mapname );
	Q_strncpy( cof_sect_entry_map, cof_sect_other.name, sizeof( cof_sect_entry_map ));
	COM_CoF_SectionsNote( "[cof-sections] map %s -> %s, section %d\n", mapname, cof_sect_other.name, cof_sect_entry );
	Q_strncpy( mapname, cof_sect_other.name, size );
}

// The game puts a new player at the first info_player_start: make that the asked-for section's.
void SV_CoF_SectionsEntry( void )
{
	const cof_sections_t *s;
	edict_t *first = NULL, *want;
	vec3_t origin, angles;
	int entry = cof_sect_entry, i;
	cof_sect_entry = -1;
	if( entry <= 0 || Q_stricmp( sv.name, cof_sect_entry_map ) || !( s = CoF_SectLoaded()) || !( want = CoF_SectStartSpot( s, entry ))) return;
	for( i = svs.maxclients + 1; i < svgame.numEntities && !first; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( SV_IsValidEdict( ent ) && !Q_strcmp( SV_GetString( ent->v.classname ), "info_player_start" )) first = ent;
	}
	if( !first || first == want ) return;
	VectorCopy( first->v.origin, origin );
	VectorCopy( first->v.angles, angles );
	VectorCopy( want->v.origin, first->v.origin );
	VectorCopy( want->v.angles, first->v.angles );
	VectorCopy( origin, want->v.origin );
	VectorCopy( angles, want->v.angles );
	COM_CoF_SectionsNote( "[cof-sections] %s starts in section %s\n", sv.name, s->sec[entry].map );
}

void SV_CoF_SectionsFrame( void )
{
	const cof_sections_t *s = CoF_SectLoaded();
	int i, j, n = Q_min( svs.maxclients, MAX_CLIENTS );
	if( !s || sv.state != ss_active ) return;
	for( i = 0; i < n; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		int section;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ))
		{
			cof_sect_alive[i] = 0;
			continue;
		}
		section = CoF_SectionsAt( s, cl->edict->v.origin[2] );
		if( !CoF_SectAlive( cl->edict ))
		{
			if( cof_sect_alive[i] == cl->userid ) cof_sect_alive[i] = -cl->userid;
			continue;
		}
		// a respawn puts the player at the map's first start; back to the section they died in
		if( cof_sect_alive[i] == -cl->userid && cof_sect_last[i] >= 0 && section != cof_sect_last[i] )
		{
			edict_t *start = CoF_SectStartSpot( s, cof_sect_last[i] );
			vec3_t landing;
			if( start )
			{
				VectorCopy( start->v.origin, landing );
				landing[2] += 1.0f;
				if( SV_CoF_PartySectionMove( cl, landing, NULL ))
				{
					Con_Printf( "[cof-story-transition] phase=section-respawn userid=%d section=%s\n", cl->userid, s->sec[cof_sect_last[i]].map );
					section = cof_sect_last[i];
				}
			}
		}
		cof_sect_alive[i] = cl->userid;
		cof_sect_last[i] = section;
		CoF_SectEnter( s, section, false );
	}
	// AI companions follow their person into another section
	for( i = 0; i < n; i++ )
	{
		sv_client_t *bot = &svs.clients[i], *leader = NULL;
		int userid;
		if( bot->state != cs_spawned || !FBitSet( bot->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( bot->edict ) ||
			!CoF_SectAlive( bot->edict ) || sv.time < cof_sect_moved_at[i] + COF_SECTION_COOLDOWN ||
			!( userid = SV_CoF_CompanionLeaderUserid( bot ))) continue;
		for( j = 0; j < n && !leader; j++ )
			if( svs.clients[j].userid == userid && svs.clients[j].state == cs_spawned && SV_IsValidEdict( svs.clients[j].edict )) leader = &svs.clients[j];
		if( !leader || !CoF_SectAlive( leader->edict ) ||
			CoF_SectionsAt( s, bot->edict->v.origin[2] ) == CoF_SectionsAt( s, leader->edict->v.origin[2] )) continue;
		cof_sect_moved_at[i] = sv.time;
		if( SV_CoF_PartySectionMove( bot, leader->edict->v.origin, leader ))
			Con_Printf( "[cof-story-transition] phase=section-companion userid=%d leader=%d\n", bot->userid, leader->userid );
	}
	// once-only exits that are gone: the place they were still moves a player
	for( j = 0; j < cof_sect_open_count; j++ )
	{
		edict_t *ent = SV_EdictNum( cof_sect_open[j].entity );
		if( SV_IsValidEdict( ent ) && ent->serialnumber == cof_sect_open[j].serial && !( ent->v.flags & FL_KILLME )) continue;
		for( i = 0; i < n; i++ )
		{
			sv_client_t *cl = &svs.clients[i];
			cof_sect_route_t r;
			if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
				!CoF_SectAlive( cl->edict ) || sv.time < cof_sect_moved_at[i] + COF_SECTION_COOLDOWN ||
				!CoF_SectAbsTouch( cl->edict, cof_sect_open[j].absmin, cof_sect_open[j].absmax )) continue;
			CoF_SectionsRoute( s, cof_sect_open[j].level, cof_sect_open[j].landmark, NULL, &r );
			if( !r.section_move ) continue;
			Con_Printf( "[cof-story-transition] phase=section-open-exit userid=%d to=%s\n", cl->userid, cof_sect_open[j].level );
			CoF_SectMove( s, &r, cof_sect_open[j].level, cof_sect_open[j].landmark, i );
		}
	}
}
