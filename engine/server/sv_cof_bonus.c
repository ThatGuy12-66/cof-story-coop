/* Cry of Fear story co-op: the bonus room switch. GPL-3.0-or-later.
 * cof_bonus_room 1 opens the bonus room of c_start and c_bridge (the door and its
 * items) for this session, 0 keeps it shut, -1 (the default) leaves the game
 * folder's scriptsettings.dat to decide; cof_costumes the same for the wardrobe's outfits (1, the default:
 * all open). The file itself is never written: only the buffer the server hands the game is (sv_cof_bonus.h).
 * After 1.1.9, with the room open for this game: a chapter start lays the room's items out where the party starts
 * (SV_CoF_BonusKitChapterStart, sv_cof_bonus_kit.h), and cof_wardrobe (the wardrobe key) opens the wardrobe's menu
 * for whoever presses it, anywhere (SV_CoF_BonusClientCommand).
 * Logs: phase=bonus-room, phase=bonus-kit (once a chapter start), phase=bonus-kit-item (once an item laid out),
 * phase=wardrobe, phase=wardrobe-refused (once a press, a second apart at most), phase=wardrobe-closed. */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_story.h"
#include "sv_cof_bonus.h"
#include "sv_cof_bonus_kit.h"
#include "sv_cof_pickups.h"
#include "sv_cof_retail.h"
#include "sv_cof_scene.h"
#include "sv_cof_sections.h"

static convar_t *cof_bonus_room, *cof_costumes;
static int cof_bonus_logged = -1; // the spawncount the state was last logged for

void SV_CoF_BonusInit( void )
{
	cof_bonus_room = Cvar_Get( "cof_bonus_room", "-1", 0,
		"story co-op: the bonus room of c_start and c_bridge and its items this session: 1 open, 0 shut, -1 as scriptsettings.dat says" );
	cof_costumes = Cvar_Get( "cof_costumes", "1", 0,
		"story co-op: the wardrobe's twelve outfits this session: 1 all open, 0 all shut, -1 as the host's scriptsettings.dat says" );
}

static int CoF_BonusState( const convar_t *cvar )
{
	return !cvar || cvar->value < 0.0f ? -1 : cvar->value > 0.0f;
}

byte *GAME_EXPORT SV_CoF_BonusLoadFile( const char *filename, int *pLength )
{
	int length = 0, bonus, costumes;
	byte *data = COM_LoadFileForMe( filename, &length );
	size_t made;
	if( pLength ) *pLength = length;
	if( !CoF_BonusIsSettings( filename ) || !SV_CoF_StoryCoopEnabled( )) return data;
	bonus = CoF_BonusState( cof_bonus_room );
	costumes = CoF_BonusState( cof_costumes );
	if( bonus < 0 && costumes < 0 ) return data;
	if( cof_bonus_logged != svs.spawncount )
	{
		cof_bonus_logged = svs.spawncount;
		Con_Printf( "cofstory phase=bonus-room state=%s costumes=%s file=%s\n", bonus < 0 ? "file" : bonus ? "open" : "shut",
			costumes < 0 ? "file" : costumes ? "open" : "shut", data && length > 0 ? "rewritten" : "none" );
	}
	if( data && length > 0 )
	{
		if( bonus >= 0 ) CoF_BonusRewrite( data, (size_t)length, bonus );
		if( costumes >= 0 ) CoF_CostumeRewrite( data, (size_t)length, costumes );
		return data;
	}
	if( data ) free( data );
	if( pLength ) *pLength = 0;
	if( bonus <= 0 && costumes <= 0 ) return NULL; // no file reads as locked
	// no file in this game folder: one with the asked lines open (freed by COM_FreeFile)
	data = malloc( 2048 );
	if( !data ) return NULL;
	made = CoF_BonusSynthesize( data, 2048, bonus, costumes );
	if( !made ) { free( data ); return NULL; }
	if( pLength ) *pLength = (int)made;
	return data;
}

// After 1.1.9: the bonus room is open for this game: cof_bonus_room 1, or -1 and the room's door would open (any of the
// five endings in scriptsettings.dat as the game reads it, CoF_InterDoor.cpp:248).
qboolean SV_CoF_BonusRoomOpen( void )
{
	int state = CoF_BonusState( cof_bonus_room ), length = 0;
	qboolean open;
	byte *data;
	if( !SV_CoF_StoryCoopEnabled( )) return false;
	if( state >= 0 ) return state > 0;
	data = SV_CoF_BonusLoadFile( "scriptsettings.dat", &length );
	open = data && length > 0 && CoF_BonusDoorOpen( data, (size_t)length );
	if( data ) COM_FreeFile( data );
	return open;
}

// 1.3: the bonus kit item's own line reads unlocked in scriptsettings.dat as the server hands it the game (with
// cof_bonus_room's rewrite), as the kit decides at a chapter start (SV_CoF_BonusKitChapterStart); an item with no line
// of its own (the MP5: the game's other rule) is. The room's own night vision is per-player only then (sv_cof_pickups.c
// CoF_PickupsRoomNightVision): locked, the game removes it at a player's first update, inside their command.
qboolean SV_CoF_BonusItemUnlocked( int item )
{
	const cof_bonus_line_t *gate;
	int length = 0;
	qboolean open;
	byte *settings;
	if( item < 0 || item >= COF_BONUS_KIT_ITEMS || !SV_CoF_StoryCoopEnabled( )) return false;
	gate = cof_bonus_kit[item].gate ? CoF_BonusLine( cof_bonus_kit[item].gate ) : NULL;
	if( !gate ) return true;
	settings = SV_CoF_BonusLoadFile( "scriptsettings.dat", &length );
	open = settings && length > 0 && CoF_BonusUnlocked( settings, (size_t)length, gate );
	if( settings ) COM_FreeFile( settings );
	return open;
}

/*
=============================================================================
The bonus kit at a chapter start (sv_cof_bonus_kit.h)
=============================================================================
*/
#define COF_BONUS_KIT_SETS 4      // with cof_coop_pickups off: a set for each of the party's places (2-4)
#define COF_BONUS_KIT_MOST ( COF_BONUS_KIT_ITEMS * COF_BONUS_KIT_SETS )
#define COF_BONUS_KIT_NEAR 512.0f // the triggers read: those this near the start (the farthest spot is 224 out)
#define COF_BONUS_KIT_DANGERS 128
#define COF_BONUS_KIT_TELEPORTS 64

static struct
{
	int count;
	qboolean full; // more than the list holds this near: every spot counts as in one
	vec3_t absmin[COF_BONUS_KIT_DANGERS], absmax[COF_BONUS_KIT_DANGERS];
} cof_bonus_dangers;

// The triggers near the start the kit keeps clear of (CoF_BonusKitDanger), live or switched off for now (a hurt a
// script turns on later is one too).
static void CoF_BonusReadDangers( const vec3_t start )
{
	char names[COF_BONUS_KIT_TELEPORTS][64];
	int i, k, n = 0;
	memset( &cof_bonus_dangers, 0, sizeof( cof_bonus_dangers ));
	for( i = svs.maxclients + 1; i < svgame.numEntities && n < COF_BONUS_KIT_TELEPORTS; i++ )
	{
		edict_t *tele = SV_EdictNum( i );
		const char *name;
		if( !SV_IsValidEdict( tele ) || Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" )) continue;
		name = SV_GetString( tele->v.targetname );
		if( name[0] && Q_strlen( name ) < sizeof( names[0] )) Q_strncpy( names[n++], name, sizeof( names[0] ));
	}
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *trigger = SV_EdictNum( i );
		const char *classname, *target;
		qboolean teleport = false;
		if( !SV_IsValidEdict( trigger ) || FBitSet( trigger->v.flags, FL_KILLME )) continue;
		classname = SV_GetString( trigger->v.classname );
		if( Q_strncmp( classname, "trigger_", 8 )) continue;
		for( k = 0; k < 3; k++ )
			if( trigger->v.absmin[k] > start[k] + COF_BONUS_KIT_NEAR || trigger->v.absmax[k] < start[k] - COF_BONUS_KIT_NEAR ) break;
		if( k < 3 ) continue;
		target = SV_GetString( trigger->v.target );
		for( k = 0; target[0] && k < n && !teleport; k++ ) teleport = !Q_strcmp( names[k], target );
		if( !CoF_BonusKitDanger( classname, teleport )) continue;
		if( cof_bonus_dangers.count == COF_BONUS_KIT_DANGERS )
		{
			cof_bonus_dangers.full = true;
			return;
		}
		VectorCopy( trigger->v.absmin, cof_bonus_dangers.absmin[cof_bonus_dangers.count] );
		VectorCopy( trigger->v.absmax, cof_bonus_dangers.absmax[cof_bonus_dangers.count] );
		cof_bonus_dangers.count++;
	}
}

// The probes CoF_BonusKitPlace asks: the world and its brush entities (doors), solid props and monsters for a
// standing player's room, the map's water, the triggers read above, the merged map's sections.
static int CoF_BonusReach( void *ctx, const float *from, const float *to )
{
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	trace_t tr = SV_Move( from, zero, zero, to, MOVE_NOMONSTERS, NULL, false );
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

static int CoF_BonusFloor( void *ctx, const float *point, float *z )
{
	vec3_t zero = { 0.0f, 0.0f, 0.0f }, end;
	trace_t tr;
	VectorCopy( point, end );
	end[2] -= COF_BONUS_KIT_EYE + 2.0f * COF_BONUS_KIT_STEP + 32.0f; // a standing player's height down, a step and more
	tr = SV_Move( point, zero, zero, end, MOVE_NOMONSTERS, NULL, false );
	if( tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f ) return 0;
	*z = tr.endpos[2];
	return 1;
}

static int CoF_BonusStand( void *ctx, const float *feet )
{
	vec3_t mins = { -16.0f, -16.0f, -COF_BONUS_KIT_EYE }, maxs = { 16.0f, 16.0f, COF_BONUS_KIT_EYE }, centre;
	trace_t tr;
	VectorCopy( feet, centre );
	centre[2] += COF_BONUS_KIT_EYE;
	tr = SV_Move( centre, mins, maxs, centre, MOVE_NORMAL, NULL, false );
	if( tr.startsolid || tr.allsolid ) return 0;
	return SV_PointContents( feet ) == CONTENTS_EMPTY && SV_PointContents( centre ) == CONTENTS_EMPTY;
}

static int CoF_BonusDanger( void *ctx, const float *feet )
{
	int i;
	if( cof_bonus_dangers.full ) return 1;
	for( i = 0; i < cof_bonus_dangers.count; i++ )
		if( CoF_BonusKitTouches( feet, cof_bonus_dangers.absmin[i], cof_bonus_dangers.absmax[i] )) return 1;
	return 0;
}

static int CoF_BonusSection( void *ctx, const float *point )
{
	return SV_CoF_PointSection( point );
}

// Where the party starts: the first info_player_start, in a merged map the chapter's own section's (CoF_BonusKitStart).
static edict_t *CoF_BonusStartSpot( const char *chapter, int *section )
{
	edict_t *starts[64];
	int sections[64], count = 0, i, pick;
	for( i = svs.maxclients + 1; i < svgame.numEntities && count < (int)ARRAYSIZE( starts ); i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.classname ), "info_player_start" )) continue;
		starts[count] = ent;
		sections[count] = SV_CoF_PointSection( ent->v.origin );
		count++;
	}
	pick = CoF_BonusKitStart( sections, count, SV_CoF_SectionOfMap( chapter ));
	*section = pick >= 0 ? sections[pick] : -1;
	return pick >= 0 ? starts[pick] : NULL;
}

// After 1.1.9: the chapter start's first story frame (sv_cof_story.c CoF_StoryChapterStart, once: cof_chapter_start
// is cleared after it, so a checkpoint load, a team wipe's reload, a revisit or Continue never lays it out again).
// With the bonus room open for this game, the room's items lie on the floor beside the start, before anybody's first
// update: each item's own unlock check (its SendInitMessage) runs at that update, as for the room's. An item whose
// line reads locked is not laid out at all: the game would remove it at once, and the night vision's removal in a
// command would read as a take (sv_cof_pickups_state.h). With the per-player pickups on, one of each
// (sv_cof_pickups.c copies it for every player, late joiners too); off, a set for each place (the retail rules
// track nobody, and the places are fixed for the session: a late joiner takes a free one).
void SV_CoF_BonusKitChapterStart( const char *chapter )
{
	cof_bonus_kit_probe_t probe;
	float spots[COF_BONUS_KIT_MOST][3], yaw;
	int items[COF_BONUS_KIT_MOST], placed[COF_BONUS_KIT_MOST];
	int length = 0, sets, count = 0, made = 0, close = 0, locked = 0, section, i, k;
	vec3_t start, forward, angles;
	byte *settings;
	edict_t *spot;
	if( !SV_CoF_StoryCoopEnabled() || !chapter || !chapter[0] ) return;
	if( !CoF_BonusKitWanted( chapter ))
	{
		Con_Printf( "cofstory phase=bonus-kit map=%s chapter=%s result=own-room\n", sv.name, chapter );
		return;
	}
	if( !SV_CoF_BonusRoomOpen( ))
	{
		Con_Printf( "cofstory phase=bonus-kit map=%s chapter=%s result=shut\n", sv.name, chapter );
		return;
	}
	if(( spot = CoF_BonusStartSpot( chapter, &section )) == NULL )
	{
		Con_Printf( "cofstory phase=bonus-kit map=%s chapter=%s result=no-start\n", sv.name, chapter );
		return;
	}
	sets = SV_CoF_PickupsEnabled() ? 1 : bound( 1, svs.maxclients, COF_BONUS_KIT_SETS );
	settings = SV_CoF_BonusLoadFile( "scriptsettings.dat", &length );
	for( i = 0; i < COF_BONUS_KIT_ITEMS; i++ )
	{
		const cof_bonus_line_t *gate = cof_bonus_kit[i].gate ? CoF_BonusLine( cof_bonus_kit[i].gate ) : NULL;
		if( gate && !( settings && length > 0 && CoF_BonusUnlocked( settings, (size_t)length, gate )))
		{
			Con_Printf( "cofstory phase=bonus-kit-item class=%s result=locked\n", cof_bonus_kit[i].classname );
			locked++;
			continue;
		}
		for( k = 0; k < sets; k++ ) items[count++] = i;
	}
	if( settings ) COM_FreeFile( settings );
	VectorCopy( spot->v.origin, start );
	yaw = (float)DEG2RAD( spot->v.angles[1] );
	forward[0] = cosf( yaw ); forward[1] = sinf( yaw ); forward[2] = 0.0f;
	CoF_BonusReadDangers( start );
	probe.ctx = NULL;
	probe.reach = CoF_BonusReach;
	probe.ground = CoF_BonusFloor;
	probe.stand = CoF_BonusStand;
	probe.danger = CoF_BonusDanger;
	probe.section = CoF_BonusSection;
	if( count ) CoF_BonusKitPlace( &probe, start, forward, count, spots, placed );
	for( i = 0; i < count; i++ )
	{
		const cof_bonus_kit_item_t *kit = &cof_bonus_kit[items[i]];
		edict_t *ent;
		if( !placed[i] )
		{
			Con_Printf( "cofstory phase=bonus-kit-item class=%s result=no-spot\n", kit->classname );
			continue;
		}
		VectorSet( angles, 0.0f, kit->yaw, 0.0f );
		if(( ent = SV_CoF_PickupsSpawnBonus( items[i], spots[i], angles, false )) == NULL )
		{
			Con_Printf( "cofstory phase=bonus-kit-item class=%s result=failed\n", kit->classname );
			continue;
		}
		made++;
		if( placed[i] > 1 ) close++;
		Con_Printf( "cofstory phase=bonus-kit-item class=%s entity=%d origin=%.1f,%.1f,%.1f%s\n", kit->classname, NUM_FOR_EDICT( ent ),
			ent->v.origin[0], ent->v.origin[1], ent->v.origin[2], placed[i] > 1 ? " close=1" : "" );
	}
	Con_Printf( "cofstory phase=bonus-kit map=%s chapter=%s result=placed sets=%d placed=%d of=%d close=%d locked=%d section=%d start=%.1f,%.1f,%.1f\n",
		sv.name, chapter, sets, made, count, close, locked, section, start[0], start[1], start[2] );
}

/*
=============================================================================
The wardrobe key (cof_wardrobe)
=============================================================================
*/
#define COF_WARDROBE_WAIT 1.0 // seconds: a press within this of the last answered one gets no answer

static struct
{
	int userid;
	double next;      // a press before this is too soon (sv.time of the map it was answered on)
	qboolean opened;  // the key opened their menu and its Close has not come back yet
} cof_wardrobe[MAX_CLIENTS];
static const char *cof_wardrobe_reasons[] = { "open", "shut", "dead", "scene", "ladder", "boat", "soon" };

// The wardrobe's menu for this player, wherever they are: the outfits the map's wardrobe would offer them (its
// CheckingEndingOrUnlockables of lines 51-62, on scriptsettings.dat as the server hands it the game, cof_costumes
// applied) and the outfit they wear. No camera, no posed Simon: their own view stays. Costumes stay the client's
// texture swap on every Simon it draws (setclothes from the menu goes to the game as from the wardrobe).
static void CoF_Wardrobe( sv_client_t *cl, int slot )
{
	edict_t *ent = cl->edict;
	byte message[COF_WARDROBE_MESSAGE], *settings;
	unsigned int flags = 0;
	int answer, any = 0, length = 0, outfit;
	qboolean soon, camera;
	const char *text = NULL;
	if( cl->state != cs_spawned || !SV_IsValidEdict( ent ) || FBitSet( cl->flags, FCL_FAKECLIENT )) return;
	if( cof_wardrobe[slot].userid != cl->userid )
	{
		memset( &cof_wardrobe[slot], 0, sizeof( cof_wardrobe[slot] ));
		cof_wardrobe[slot].userid = cl->userid;
	}
	// a map's time starts again: a mark from an earlier map is never "soon"
	soon = sv.time < cof_wardrobe[slot].next && sv.time >= cof_wardrobe[slot].next - COF_WARDROBE_WAIT;
	if( soon ) return; // a held or mashed key: one answer a second (nothing read, nothing logged)
	camera = ( cl->pViewEntity && cl->pViewEntity != ent ) || SV_CoF_RetailCameraHolds( cl ) || SV_CoF_RetailAttachedCamera( cl );
	answer = CoF_WardrobeAnswer( SV_CoF_BonusRoomOpen(), ent->v.health > 0 && ent->v.deadflag == DEAD_NO, camera,
		FBitSet( ent->v.flags, FL_FROZEN ) != 0, ent->v.movetype == MOVETYPE_FLY, SV_CoF_RetailInBoat( cl ), soon );
	cof_wardrobe[slot].next = sv.time + COF_WARDROBE_WAIT;
	if( answer != COF_WARDROBE_OPEN )
	{
		Con_Printf( "cofstory phase=wardrobe-refused userid=%d reason=%s\n", cl->userid, cof_wardrobe_reasons[answer] );
		if( answer == COF_WARDROBE_SHUT ) text = "The wardrobe is locked: the bonus room is shut in this game.";
		else if( answer != COF_WARDROBE_DEAD ) text = "The wardrobe cannot be opened right now."; // dead: the respawn prompt speaks
		if( text ) SV_CoF_RetailProFont( cl, 1, text, false );
		return;
	}
	settings = SV_CoF_BonusLoadFile( "scriptsettings.dat", &length );
	if( settings && length > 0 ) flags = CoF_WardrobeFlags( settings, (size_t)length, &any );
	if( settings ) COM_FreeFile( settings );
	outfit = SV_CoF_RetailOutfit( cl );
	CoF_WardrobeMessage( message, flags, any, outfit < 0 ? 0 : outfit );
	if( !SV_CoF_SendUserMessage( ent, "Clothes", message, (int)sizeof( message ), true ))
	{
		Con_Printf( "cofstory phase=wardrobe-refused userid=%d reason=no-message\n", cl->userid );
		return;
	}
	cof_wardrobe[slot].opened = true;
	Con_Printf( "cofstory phase=wardrobe userid=%d outfits=0x%x outfit=%d\n", cl->userid, flags, outfit );
}

// Client commands the bonus room answers before the game: cof_wardrobe, and the Close of a menu it opened.
qboolean SV_CoF_BonusClientCommand( sv_client_t *cl )
{
	int slot = cl ? (int)( cl - svs.clients ) : -1;
	const char *cmd = Cmd_Argv( 0 );
	qboolean opened;
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || sv.state != ss_active || !SV_CoF_StoryCoopEnabled( )) return false;
	if( !Q_strcmp( cmd, "cof_wardrobe" ))
	{
		CoF_Wardrobe( cl, slot );
		return true;
	}
	if( Q_strcmp( cmd, "closeclothes" ) || cof_wardrobe[slot].userid != cl->userid ) return false;
	opened = cof_wardrobe[slot].opened;
	cof_wardrobe[slot].opened = false;
	if( !CoF_WardrobeKeepsClose( opened, SV_CoF_SceneWardrobeUser( cl ))) return false;
	// the game's closeclothes would end whatever wardrobe camera runs here, someone else's session (sv_cof_bonus.h)
	Con_Printf( "cofstory phase=wardrobe-closed userid=%d\n", cl->userid );
	return true;
}
