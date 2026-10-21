/* Opt-in Cry of Fear story co-op: what a door's +use met. GPL-3.0-or-later.
 * The frame a player starts an inter_door (the door's owner becomes them,
 * CInterDoor::Use), logs the ray the game's PlayerUse traced: from the eye
 * 80 units along v_angle. Every brush or body on it, nearest first (the door
 * just used included: from now on the engine's own trace skips it, its owner
 * being this player), then each inter_door within the 80 units PlayerUse's
 * fallback searches when the ray finds nothing usable, with the dot that
 * fallback ranks them by (best above 0.7 wins, the first of equals). Live
 * 2026-10-19, cm_college2: from inside the screwdriver classroom both doors
 * took the player back in, the far door brush of the pair, and the log could
 * not say why. Everywhere when cof_use_probe is non-zero (1.1.8: off by default; it went with cof_story_trace, which
 * every release session has on, and logged 3-12 lines per door use there). 1.1.9 (R1): with it 0, for the humans in
 * c_college1 only (the map, or its section of a merged map: cm_college2), where the loop was reported, so a repeat
 * comes with its lines. */
#include "common.h"
#include "server.h"
#include "cof_merged.h"
#include "sv_cof_sections.h"
#include "sv_cof_useprobe.h"

#define COF_PROBE_REACH 80.0f // PLAYER_SEARCH_RADIUS
#define COF_PROBE_HITS 8
#define COF_PROBE_MAP "c_college1"

typedef struct
{
	int door, serial; // the inter_door this player held last frame (edict number; 0 none)
} cof_probe_slot_t;

static cof_probe_slot_t cof_probe_last[MAX_CLIENTS];
static int cof_probe_spawncount;
static int cof_probe_where; // this map: 0 not c_college1, -1 c_college1 itself, else 1 + its section in the merged map
static convar_t *cof_use_probe;

// Where c_college1 is on the loaded map (cof_probe_where).
static int CoF_ProbeWhere( void )
{
	cof_sections_t *s;
	int i, where = 0;
	if( !Q_stricmp( sv.name, COF_PROBE_MAP )) return -1;
	if( !COM_CoF_IsMergedMap( sv.name ) || !( s = Mem_Calloc( host.mempool, sizeof( *s )))) return 0;
	if( COM_CoF_LoadSections( sv.name, s ))
		for( i = 0; i < s->count && !where; i++ )
			if( !Q_stricmp( s->sec[i].map, COF_PROBE_MAP )) where = 1 + i;
	Mem_Free( s );
	return where;
}

static int CoF_ProbeNum( const edict_t *e )
{
	return SV_IsValidEdict( e ) ? NUM_FOR_EDICT( e ) : -1;
}

// PlayerUse's fallback rank: the box centre seen from the eye, moved to the
// box's nearest face (UTIL_ClampVectorToBox), against the view direction.
static float CoF_ProbeDot( const edict_t *door, const vec3_t eye, const vec3_t forward )
{
	vec3_t los;
	int i;

	for( i = 0; i < 3; i++ )
	{
		float half = door->v.size[i] * 0.5f;

		los[i] = door->v.absmin[i] + half - eye[i];
		if( los[i] > half ) los[i] -= half;
		else if( los[i] < -half ) los[i] += half;
		else los[i] = 0.0f;
	}
	VectorNormalize( los );
	return DotProduct( los, forward );
}

// UTIL_FindEntityInSphere's test: the nearest point of the box within reach.
static qboolean CoF_ProbeInReach( const edict_t *e, const vec3_t org )
{
	float distance = 0.0f;
	int i;

	for( i = 0; i < 3; i++ )
	{
		float d = org[i] < e->v.absmin[i] ? org[i] - e->v.absmin[i] : org[i] > e->v.absmax[i] ? org[i] - e->v.absmax[i] : 0.0f;
		distance += d * d;
	}
	return distance <= COF_PROBE_REACH * COF_PROBE_REACH;
}

static void CoF_Probe( sv_client_t *cl, edict_t *door )
{
	edict_t *ent = cl->edict;
	vec3_t eye, forward, end, boxmin, boxmax, zero = { 0.0f, 0.0f, 0.0f };
	globalvars_t saved = *svgame.globals;
	trace_t tr, hits[COF_PROBE_HITS];
	int i, j, count = 0, more = 0;

	VectorAdd( ent->v.origin, ent->v.view_ofs, eye );
	AngleVectors( ent->v.v_angle, forward, NULL, NULL );
	VectorMA( eye, COF_PROBE_REACH, forward, end );
	tr = SV_Move( eye, zero, zero, end, MOVE_NORMAL, ent, false );
	Con_Printf( "cofstory phase=use-probe userid=%d door=%d model=%s target=%s eye=%.0f,%.0f,%.0f view=%.0f,%.0f ofs=%.0f trace=%.3f hit=%d\n",
		cl->userid, NUM_FOR_EDICT( door ), SV_GetString( door->v.model ), SV_GetString( door->v.target ),
		eye[0], eye[1], eye[2], ent->v.v_angle[0], ent->v.v_angle[1], ent->v.view_ofs[2], tr.fraction, CoF_ProbeNum( tr.ent ));

	for( i = 0; i < 3; i++ )
	{
		boxmin[i] = Q_min( eye[i], end[i] ) - 1.0f;
		boxmax[i] = Q_max( eye[i], end[i] ) + 1.0f;
	}
	for( i = 0; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );
		trace_t t;

		if( !SV_IsValidEdict( e ) || e == ent ) continue;
		if( i && ( e->v.solid == SOLID_NOT || e->v.solid == SOLID_TRIGGER || !BoundsIntersect( boxmin, boxmax, e->v.absmin, e->v.absmax ))) continue;
		SV_ClipMoveToEntity( e, eye, zero, zero, end, &t );
		if( t.fraction >= 1.0f && !t.startsolid ) continue;
		t.ent = e;
		if( count == COF_PROBE_HITS ) { more++; continue; }
		for( j = count++; j > 0 && hits[j - 1].fraction > t.fraction; j-- ) hits[j] = hits[j - 1];
		hits[j] = t;
	}
	for( i = 0; i < count; i++ )
		Con_Printf( "cofstory phase=use-probe-hit userid=%d ent=%d class=%s model=%s fraction=%.3f start=%d owner=%d\n",
			cl->userid, NUM_FOR_EDICT( hits[i].ent ), SV_GetString( hits[i].ent->v.classname ), SV_GetString( hits[i].ent->v.model ),
			hits[i].fraction, hits[i].startsolid ? 1 : 0, hits[i].ent->v.owner ? CoF_ProbeNum( hits[i].ent->v.owner ) : 0 );
	if( more ) Con_Printf( "cofstory phase=use-probe-hit userid=%d more=%d\n", cl->userid, more );

	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *e = SV_EdictNum( i );

		if( !SV_IsValidEdict( e ) || Q_strcmp( SV_GetString( e->v.classname ), "inter_door" ) || !CoF_ProbeInReach( e, ent->v.origin )) continue;
		Con_Printf( "cofstory phase=use-probe-door userid=%d ent=%d model=%s target=%s dot=%.3f\n",
			cl->userid, i, SV_GetString( e->v.model ), SV_GetString( e->v.target ), CoF_ProbeDot( e, eye, forward ));
	}
	*svgame.globals = saved; // SV_Move wrote the trace globals
}

void SV_CoF_UseProbeFrame( void )
{
	qboolean on;
	int slot, i;

	if( !cof_use_probe )
		cof_use_probe = Cvar_Get( "cof_use_probe", "0", 0, "Cry of Fear story co-op: log what each door's +use met (a diagnostic); 0 only in c_college1" );
	on = cof_use_probe->value != 0.0f;

	// a level change would otherwise compare against the last level's edicts
	if( cof_probe_spawncount != svs.spawncount )
	{
		memset( cof_probe_last, 0, sizeof( cof_probe_last ));
		cof_probe_spawncount = svs.spawncount;
		cof_probe_where = CoF_ProbeWhere();
	}
	if( !on && !cof_probe_where )
	{
		memset( cof_probe_last, 0, sizeof( cof_probe_last ));
		return;
	}
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *cl = &svs.clients[slot];
		cof_probe_slot_t *last = &cof_probe_last[slot];
		edict_t *held = NULL;

		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			( !on && cof_probe_where > 0 && SV_CoF_PlayerSection( cl->edict ) != cof_probe_where - 1 ))
		{
			last->door = 0;
			continue;
		}
		for( i = svs.maxclients + 1; i < svgame.numEntities && !held; i++ )
		{
			edict_t *e = SV_EdictNum( i );

			if( SV_IsValidEdict( e ) && e->v.owner == cl->edict && !Q_strcmp( SV_GetString( e->v.classname ), "inter_door" ))
				held = e;
		}
		if( held && ( last->door != NUM_FOR_EDICT( held ) || last->serial != held->serialnumber ))
			CoF_Probe( cl, held );
		last->door = held ? NUM_FOR_EDICT( held ) : 0;
		last->serial = held ? held->serialnumber : 0;
	}
}
