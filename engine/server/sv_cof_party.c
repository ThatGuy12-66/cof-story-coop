/* Opt-in Cry of Fear story party coordination. GPL-3.0-or-later.
 * Retail 1.6 player: inventorySlot[] at +21FC holds item descriptor paths,
 * slots 1..12 in story co-op (sv_cof_inventory12_patch.h); slot 0 is unused.
 * The legacy classname array at +2234 stays empty. Confirmed by item assignment at VA100DAE08 and live state.
 * b_GoingThroughADoor is +1EA4, m_vPreviousOrigin is +618.
 * inter_door EndSequence (10048070) resolves pev.target, moves its owner
 * to exit.origin - player.mins.z + 1, then fires targets which may remove
 * the door. Cache that event before it disappears; never keep a raw door
 * private-data pointer or invoke its one-shot callback a second time.
 */
#include "common.h"
#include "server.h"
#include "const.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_party.h"
#include "sv_cof_retail.h"
#include "sv_cof_exit.h"
#include "sv_cof_scene.h"

#define COF_PARTY_DOOR_FLAG 0x1EA4
#define COF_PARTY_PREVIOUS_ORIGIN 0x618
#define COF_PARTY_INVENTORY 0x21FC
#define COF_PARTY_SLOTS 13 // indices 0..12; 0 is never used
#define COF_PARTY_PLAYER_SIZE (COF_PARTY_INVENTORY + COF_PARTY_SLOTS * sizeof( string_t ))
// Retail PreThink (100EF2D0) makes a player SOLID_NOT while its absbox touches
// another player's; boxes are padded 1 unit, so centers 34 apart still touch.
#define COF_PARTY_JOIN_WAIT 30.0
#define COF_PARTY_JOIN_NEAR 128.0f
#define COF_PARTY_JOIN_WANDER 64.0f
#define COF_PARTY_TELEPORT_MIN 96.0f
#define COF_PARTY_TELEPORT_WAIT 10.0
#define COF_PARTY_WARP_COOLDOWN 2.0
#define COF_PARTY_CHANGELEVEL_USEONLY 2 // SF_CHANGELEVEL_USEONLY: Spawn (101105D0) sets no touch
// Retail multiplayer Killed (100E9230) sets both to time + 60: the first "press
// jump to respawn" prompt, and the moment jump respawns (PreThink 100EF569).
#define COF_PARTY_DEADNOTIFY_AT 0x3D0
#define COF_PARTY_RESPAWN_AT 0x3D4
// The ending door (c_village3, target "endingcheck"): EndSequence (100481AE)
// picks ending_exitN from its opener's RoofBossKilled / GivenP345 /
// DeliveredPackage bytes, which the shared story state keeps equal for all.
#define COF_PARTY_ENDING_FLAGS 0x4CD
#define COF_PARTY_ENDING_ROOM 40.0f // the rooms' level-change triggers are 80 x 92 and 125 apart

// Weapons every player gets once anyone has one: the c_start starter kit, and
// every weapon a story script hands out (cof_giveitems with a classname, Use
// 100A6BC0 gives it to its activator only, from one-shot sources: the glock in
// c_apartment1, the flashlight in c_asylumb, the camera in c_nightmare, the
// shotgun in c_sewer2). The list grows as maps with such gifts are visited.
#define COF_PARTY_SHARED 8
static const char *cof_party_starters[] = { "weapon_mobile", "weapon_switchblade" };
static struct
{
	char classname[32];
	char descriptor[64];
} cof_party_shared[COF_PARTY_SHARED];
static int cof_party_shared_count;
// Which shared weapons each player has had (by userid), kept across map
// changes: a weapon they dropped on purpose is not handed back on the next map.
static struct
{
	int userid;
	unsigned int seen;
} cof_party_seen[MAX_CLIENTS];

typedef struct
{
	int userid, serial;
	edict_t *edict;
	void *private_data;
} cof_party_identity_t;

typedef struct
{
	cof_party_identity_t identity;
	unsigned int seen_items;
	double retry_after[COF_PARTY_SHARED];
	qboolean was_alive, seen_dead;
	// Retail multiplayer PostThink sets NODRAW while the door flag is up and
	// never clears it; remember that this player's current hide is a door's.
	qboolean door_hidden;
	qboolean place_pending, place_regroup; // regroup: a story arrival, not a spawn
	double place_deadline;
	vec3_t place_start;
	qboolean teleported;
	vec3_t teleport_from, teleport_to;
	double warp_after; // the next moment this player's warp key works
} cof_party_player_t;

typedef struct
{
	qboolean active, completed, teleport;
	qboolean ending; // the ending door: everyone goes, frozen and faded like the opener
	qboolean froze[MAX_CLIENTS];
	// While held, a companion is out of the fight like the opener (whose door
	// flag makes them invulnerable): what we changed, to give it back.
	qboolean notarget_added[MAX_CLIENTS];
	float takedamage[MAX_CLIENTS];
	cof_party_identity_t owner;
	cof_party_identity_t companions[MAX_CLIENTS];
	qboolean pending[MAX_CLIENTS];
	vec3_t destination, angles, start;
	double deadline;
	edict_t *door; // the inter_door that began it, to see whether it removed itself
	int door_serial;
} cof_party_door_t;

// Who fired a story exit and who was elsewhere, recorded at capture for the
// arrival in the next map (epoch = that map's spawncount).
typedef struct
{
	unsigned int epoch;
	int leader; // slot + 1; 0 when nothing was recorded
	int userid[MAX_CLIENTS];
	qboolean stray[MAX_CLIENTS];
} cof_party_arrival_t;

static cof_party_player_t cof_party_players[MAX_CLIENTS];
static cof_party_door_t cof_party_doors[MAX_CLIENTS];
static cof_party_arrival_t cof_party_arrival;
static int cof_party_fresh[MAX_CLIENTS]; // userid of a new join awaiting cs_spawned
static int cof_party_regroup[MAX_CLIENTS]; // userid of an arrival still to be moved beside a partner
static int cof_party_hidden[MAX_CLIENTS]; // userid still owed a door-hide undo after a map change
static int cof_party_generation = -1;
static double cof_party_inventory_time;
static convar_t *cof_party_respawn_delay;

static qboolean CoF_PartyVectorValid( const vec3_t v )
{
	int i;
	for( i = 0; i < 3; i++ )
		if( !( v[i] >= -131072.0f && v[i] <= 131072.0f )) return false;
	return true;
}

static float CoF_PartyDistanceSquared( const vec3_t a, const vec3_t b )
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

static cof_party_identity_t CoF_PartyIdentity( sv_client_t *cl )
{
	cof_party_identity_t id;
	memset( &id, 0, sizeof( id ));
	if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) return id;
	id.userid = cl->userid;
	id.edict = cl->edict;
	id.serial = cl->edict->serialnumber;
	id.private_data = cl->edict->pvPrivateData;
	return id;
}

static qboolean CoF_PartySamePlayer( const cof_party_identity_t *id, sv_client_t *cl )
{
	return id->edict && cl->state == cs_spawned && cl->userid == id->userid &&
		cl->edict == id->edict && SV_IsValidEdict( cl->edict ) &&
		cl->edict->serialnumber == id->serial && cl->edict->pvPrivateData == id->private_data;
}

// A living player edict with verified private data, whatever its connection
// state: story arrivals are placed before their client signs on again.
static byte *CoF_PartyLivingPlayer( edict_t *ent )
{
	byte *pv;
	if( !SV_IsValidEdict( ent ) ||
		Q_strcmp( SV_GetString( ent->v.classname ), "player" ) ||
		!( ent->v.health > 0 ) || ent->v.deadflag != DEAD_NO ||
		( ent->v.flags & FL_KILLME ) || !CoF_PartyVectorValid( ent->v.origin )) return NULL;
	pv = SV_CoF_StoryEntityPrivate( ent, COF_PARTY_PLAYER_SIZE );
	if( !pv || pv[COF_PARTY_DOOR_FLAG] > 1 ) return NULL;
	return pv;
}

static byte *CoF_PartyPlayer( sv_client_t *cl )
{
	if( cl->state != cs_spawned ) return NULL;
	return CoF_PartyLivingPlayer( cl->edict );
}

// In ordinary play: no door, camera, freeze or noclip. NODRAW is checked by callers.
static qboolean CoF_PartyPlayable( sv_client_t *cl )
{
	byte *pv = CoF_PartyPlayer( cl );
	return pv && !pv[COF_PARTY_DOOR_FLAG] && cl->edict->v.movetype == MOVETYPE_WALK &&
		!( cl->edict->v.flags & FL_FROZEN ) &&
		( !cl->pViewEntity || cl->pViewEntity == cl->edict );
}

static qboolean CoF_PartyAvailable( sv_client_t *cl )
{
	return CoF_PartyPlayable( cl ) && !( cl->edict->v.effects & EF_NODRAW );
}

static int CoF_PartySlot( const sv_client_t *cl )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( cl == &svs.clients[i] ) return i;
	return -1;
}

void SV_CoF_PartyInit( void )
{
	cof_party_respawn_delay = Cvar_Get( "cof_story_respawn_delay", "10", 0, "story co-op: seconds a dead player waits before jump respawns (retail 60)" );
}

static void CoF_PartyShareWeapon( const char *classname )
{
	int i;
	if( Q_strncmp( classname, "weapon_", 7 ) || Q_strlen( classname ) >= sizeof( cof_party_shared[0].classname )) return;
	for( i = 0; i < cof_party_shared_count; i++ )
		if( !Q_strcmp( cof_party_shared[i].classname, classname )) return;
	if( cof_party_shared_count == COF_PARTY_SHARED ) return;
	Q_strncpy( cof_party_shared[i].classname, classname, sizeof( cof_party_shared[i].classname ));
	Q_snprintf( cof_party_shared[i].descriptor, sizeof( cof_party_shared[i].descriptor ), "inventoryitems/weapons/%s.txt", classname );
	cof_party_shared_count++;
	if( i >= (int)ARRAYSIZE( cof_party_starters )) Con_Printf( "cofstory phase=gift-shared class=%s\n", classname );
}

// The scripted weapon gifts of the map just loaded (not the descriptor kind,
// spawnflag 1: story items go through the item ledger).
static void CoF_PartyFindGifts( void )
{
	int i;
	for( i = 0; i < (int)ARRAYSIZE( cof_party_starters ); i++ ) CoF_PartyShareWeapon( cof_party_starters[i] );
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *gift = SV_EdictNum( i );
		if( !SV_IsValidEdict( gift ) || ( gift->v.flags & FL_KILLME ) || ( gift->v.spawnflags & 1 ) ||
			Q_strcmp( SV_GetString( gift->v.classname ), "cof_giveitems" )) continue;
		CoF_PartyShareWeapon( SV_GetString( gift->v.message ));
	}
}

void SV_CoF_PartyShutdown( void )
{
	int i;
	memset( cof_party_seen, 0, sizeof( cof_party_seen ));
	cof_party_shared_count = 0;
	memset( cof_party_shared, 0, sizeof( cof_party_shared ));
	for( i = 0; i < (int)ARRAYSIZE( cof_party_starters ); i++ ) CoF_PartyShareWeapon( cof_party_starters[i] );
	memset( cof_party_players, 0, sizeof( cof_party_players ));
	memset( cof_party_doors, 0, sizeof( cof_party_doors ));
	memset( cof_party_fresh, 0, sizeof( cof_party_fresh ));
	memset( cof_party_regroup, 0, sizeof( cof_party_regroup ));
	memset( cof_party_hidden, 0, sizeof( cof_party_hidden ));
	cof_party_generation = -1;
	cof_party_inventory_time = 0;
}

// Movement shared by doors, teleports, spawns and arrivals. Never calls game
// callbacks: the regular movement pass touches triggers on the next command.
static qboolean CoF_PartyMoveEdict( edict_t *ent, byte *pv, const vec3_t landing, const vec3_t angles )
{
	if( !pv || !CoF_PartyVectorValid( landing )) return false;
	VectorCopy( landing, ent->v.origin );
	memcpy( pv + COF_PARTY_PREVIOUS_ORIGIN, landing, sizeof( vec3_t ));
	if( angles )
	{
		VectorCopy( angles, ent->v.angles );
		VectorCopy( angles, ent->v.v_angle );
		ent->v.fixangle = 1;
	}
	VectorClear( ent->v.velocity );
	VectorClear( ent->v.basevelocity );
	ent->v.flags &= ~FL_ONGROUND;
	SV_LinkEdict( ent, false );
	return true;
}

static qboolean CoF_PartyMove( sv_client_t *cl, const vec3_t landing, const vec3_t angles )
{
	return CoF_PartyMoveEdict( cl->edict, CoF_PartyPlayer( cl ), landing, angles );
}

static void CoF_PartyCheckGeneration( void )
{
	int i, hidden[MAX_CLIENTS], shared_count = cof_party_shared_count;
	char shared[sizeof( cof_party_shared )], seen[sizeof( cof_party_seen )];
	if( cof_party_generation == svs.spawncount ) return;
	// A door can lead straight into a level change while its NODRAW is still
	// set; the snapshot keeps the flag, so keep the obligation to undo it.
	for( i = 0; i < MAX_CLIENTS; i++ )
		hidden[i] = cof_party_players[i].door_hidden ? cof_party_players[i].identity.userid : cof_party_hidden[i];
	memcpy( shared, cof_party_shared, sizeof( shared )); // the session's gifts outlive the map
	memcpy( seen, cof_party_seen, sizeof( seen ));
	SV_CoF_PartyShutdown();
	memcpy( cof_party_hidden, hidden, sizeof( hidden ));
	memcpy( cof_party_shared, shared, sizeof( shared ));
	memcpy( cof_party_seen, seen, sizeof( seen ));
	cof_party_shared_count = shared_count;
	cof_party_generation = svs.spawncount;
	CoF_PartyFindGifts();
}

static void CoF_PartySchedulePlacement( sv_client_t *cl, cof_party_player_t *p, qboolean regroup )
{
	p->place_pending = true;
	p->place_regroup = regroup;
	p->place_deadline = sv.time + COF_PARTY_JOIN_WAIT;
	VectorCopy( cl->edict->v.origin, p->place_start );
}

// fresh: the game DLL just ran ClientPutInServer (a new join at a map spawn
// point), as opposed to a player restored in place by a story transition.
// The engine calls this before the client reaches cs_spawned, so identity is
// usually adopted later by CoF_PartyTrackLife; carry the fresh mark by userid.
void SV_CoF_PartyPlayerReady( sv_client_t *cl, qboolean fresh )
{
	int i = CoF_PartySlot( cl ), k;
	cof_party_player_t *p;
	if( !SV_CoF_StoryCoopEnabled() || i < 0 ) return;
	CoF_PartyCheckGeneration();
	p = &cof_party_players[i];
	memset( p, 0, sizeof( *p ));
	cof_party_fresh[i] = fresh ? cl->userid : 0;
	p->identity = CoF_PartyIdentity( cl );
	p->was_alive = CoF_PartyPlayer( cl ) != NULL;
	if( cof_party_seen[i].userid == cl->userid ) p->seen_items = cof_party_seen[i].seen;
	for( k = 0; k < COF_PARTY_SHARED; k++ ) p->retry_after[k] = sv.time + 1.0;
	if( p->identity.edict )
	{
		if( cof_party_hidden[i] && cof_party_hidden[i] == cl->userid ) p->door_hidden = true;
		cof_party_hidden[i] = 0;
	}
	if(( fresh || ( cof_party_regroup[i] && cof_party_regroup[i] == cl->userid )) && p->identity.edict )
	{
		CoF_PartySchedulePlacement( cl, p, !fresh );
		cof_party_fresh[i] = cof_party_regroup[i] = 0;
	}
}

// pfnSetOrigin hook. Runs inside game callbacks: record only, act post-frame.
void SV_CoF_PartyNoteOrigin( edict_t *ent, const vec3_t previous )
{
	int i, index;
	// Called for every entity: keep the cheap player-range test first.
	if( sv.state != ss_active || !ent || !svgame.edicts ) return;
	index = (int)( ent - svgame.edicts );
	if( index < 1 || index > svs.maxclients || !SV_CoF_StoryCoopEnabled() ) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		byte *pv;
		if( cl->edict != ent ) continue;
		if( !( pv = CoF_PartyPlayer( cl )) || pv[COF_PARTY_DOOR_FLAG] ) return;
		if( CoF_PartyDistanceSquared( previous, ent->v.origin ) < COF_PARTY_TELEPORT_MIN * COF_PARTY_TELEPORT_MIN ) return;
		if( !p->teleported ) VectorCopy( previous, p->teleport_from );
		VectorCopy( ent->v.origin, p->teleport_to );
		p->teleported = true;
		return;
	}
}

static unsigned int CoF_PartyInventory( sv_client_t *cl )
{
	byte *pv = CoF_PartyPlayer( cl );
	unsigned int mask = 0;
	int i, j;
	if( !pv ) return 0;
	for( i = 1; i < COF_PARTY_SLOTS; i++ )
	{
		string_t name;
		memcpy( &name, pv + COF_PARTY_INVENTORY + i * sizeof( name ), sizeof( name ));
		if( !name ) continue;
		for( j = 0; j < cof_party_shared_count; j++ )
			if( !Q_strcmp( SV_GetString( name ), cof_party_shared[j].descriptor )) mask |= 1U << j;
	}
	return mask;
}

// A respawn gives back everything the player carried (sv_cof_recovery), so the
// retail minute of waiting only punishes. Bring the jump-respawn and its prompt
// forward; CPR revive and the retail auto-respawn are left as they are.
static void CoF_PartyRespawnSooner( sv_client_t *cl )
{
	float delay = cof_party_respawn_delay ? cof_party_respawn_delay->value : 60.0f, at, current;
	byte *pv;
	if( !( delay >= 0.0f && delay < 60.0f ) || !( pv = SV_CoF_StoryEntityPrivate( cl->edict, COF_PARTY_PLAYER_SIZE ))) return;
	at = (float)sv.time + delay;
	memcpy( &current, pv + COF_PARTY_RESPAWN_AT, sizeof( current ));
	if( !( current > at )) return; // already sooner
	memcpy( pv + COF_PARTY_RESPAWN_AT, &at, sizeof( at ));
	memcpy( &current, pv + COF_PARTY_DEADNOTIFY_AT, sizeof( current ));
	if( current > at ) memcpy( pv + COF_PARTY_DEADNOTIFY_AT, &at, sizeof( at ));
	Con_Printf( "cofstory phase=respawn-sooner userid=%d delay=%.0f\n", cl->userid, delay );
}

// Per-frame identity and life bookkeeping; everything else keys off it.
static void CoF_PartyTrackLife( void )
{
	int i, j;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		qboolean alive = CoF_PartyPlayer( cl ) != NULL, dead;
		if( !CoF_PartySamePlayer( &p->identity, cl ))
		{
			memset( p, 0, sizeof( *p ));
			p->identity = CoF_PartyIdentity( cl );
			if( !p->identity.edict ) continue;
			p->was_alive = alive;
			if( cof_party_seen[i].userid == cl->userid ) p->seen_items = cof_party_seen[i].seen;
			for( j = 0; j < COF_PARTY_SHARED; j++ ) p->retry_after[j] = sv.time + 1.0;
			if( cof_party_hidden[i] && cof_party_hidden[i] == cl->userid ) p->door_hidden = true;
			cof_party_hidden[i] = 0;
			if(( cof_party_fresh[i] && cof_party_fresh[i] == cl->userid ) ||
				( cof_party_regroup[i] && cof_party_regroup[i] == cl->userid ))
			{
				CoF_PartySchedulePlacement( cl, p, cof_party_regroup[i] == cl->userid );
				cof_party_fresh[i] = cof_party_regroup[i] = 0;
			}
			continue;
		}
		dead = SV_IsValidEdict( cl->edict ) &&
			( !( cl->edict->v.health > 0 ) || cl->edict->v.deadflag != DEAD_NO );
		if( dead && p->was_alive )
		{
			p->seen_dead = true;
			p->place_pending = false;
			CoF_PartyRespawnSooner( cl );
		}
		if( alive && !p->was_alive )
		{
			// A respawn may hand back what the death took; the recovery usually has.
			p->seen_items = 0;
			cof_party_seen[i].userid = cl->userid;
			cof_party_seen[i].seen = 0;
			for( j = 0; j < COF_PARTY_SHARED; j++ ) p->retry_after[j] = sv.time + 1.0;
			// Multiplayer respawns at the map's first spawn point, often far
			// behind (or inside) the partner; bring them back together.
			if( p->seen_dead )
			{
				p->seen_dead = false;
				CoF_PartySchedulePlacement( cl, p, false );
			}
		}
		p->was_alive = alive;
	}
}

static void CoF_PartyItems( void )
{
	unsigned int carried[MAX_CLIENTS] = { 0 }, party_items = 0;
	int i, j;
	if( sv.time < cof_party_inventory_time ) return;
	cof_party_inventory_time = sv.time + 0.5;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		if( !CoF_PartySamePlayer( &p->identity, cl )) continue;
		carried[i] = CoF_PartyInventory( cl );
		party_items |= carried[i];
		p->seen_items |= carried[i];
		cof_party_seen[i].userid = cl->userid;
		cof_party_seen[i].seen = p->seen_items;
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		if( !CoF_PartyAvailable( cl )) continue;
		for( j = 0; j < cof_party_shared_count; j++ )
		{
			unsigned int bit = 1U << j;
			qboolean accepted;
			if( !( party_items & bit ) || ( p->seen_items & bit ) || sv.time < p->retry_after[j] ) continue;
			p->retry_after[j] = sv.time + 5.0;
			accepted = SV_CoF_StoryGiveItem( cl, cof_party_shared[j].classname );
			if( CoF_PartyInventory( cl ) & bit )
			{
				p->seen_items |= bit;
				cof_party_seen[i].userid = cl->userid;
				cof_party_seen[i].seen = p->seen_items;
				Con_Printf( "cofstory phase=starter-shared userid=%d class=%s\n", cl->userid, cof_party_shared[j].classname );
			}
			else
				Con_Printf( "cofstory phase=starter-deferred userid=%d class=%s reason=%s\n", cl->userid, cof_party_shared[j].classname, accepted ? "inventory-unconfirmed" : "pickup-refused" );
		}
	}
}

static edict_t *CoF_PartyDoorExit( edict_t *door, qboolean *ending )
{
	int i;
	const char *target;
	char room[16];
	*ending = false;
	if( !door->v.target ) return NULL;
	target = SV_GetString( door->v.target );
	if( !target[0] ) return NULL;
	if( !Q_strcmp( target, "endingcheck" ))
	{
		// The room EndSequence will choose for this opener (same table, 100481AE).
		byte *pv = SV_IsValidEdict( door->v.owner ) ? SV_CoF_StoryEntityPrivate( door->v.owner, COF_PARTY_PLAYER_SIZE ) : NULL;
		int roof, p345, package;
		if( !pv ) return NULL;
		roof = pv[COF_PARTY_ENDING_FLAGS], p345 = pv[COF_PARTY_ENDING_FLAGS + 1], package = pv[COF_PARTY_ENDING_FLAGS + 2];
		if( roof > 1 || p345 > 1 || package > 1 ) return NULL; // the game finds no room either
		Q_snprintf( room, sizeof( room ), "ending_exit%d", package ? 5 : 1 + 2 * roof + p345 );
		target = room;
		*ending = true;
	}
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( !SV_IsValidEdict( ent ) || ( ent->v.flags & FL_KILLME ) ||
			Q_strcmp( SV_GetString( ent->v.targetname ), target )) continue;
		// Match the DLL's first target-name result, not an arbitrary later exit.
		if( Q_strcmp( SV_GetString( ent->v.classname ), "inter_door_exit" ) ||
			!CoF_PartyVectorValid( ent->v.origin ) || !CoF_PartyVectorValid( ent->v.angles )) { *ending = false; return NULL; }
		return ent;
	}
	*ending = false;
	return NULL;
}

// The retail door's own presentation for a companion of the ending door:
// black out and hold still now (BeginSequence 10048CB0), fade back in on arrival.
static void CoF_PartyEndingHold( cof_party_door_t *event, int slot, qboolean hold )
{
	sv_client_t *cl = &svs.clients[slot];
	edict_t *ent = cl->edict;
	if( hold )
	{
		SV_CoF_RetailScreenFade( cl, 0.5f, 2.0f, 5, 0, 0, 0, 255 );
		if( !( ent->v.flags & FL_FROZEN ))
		{
			ent->v.flags |= FL_FROZEN;
			VectorClear( ent->v.velocity );
			event->froze[slot] = true;
			event->notarget_added[slot] = !( ent->v.flags & FL_NOTARGET );
			ent->v.flags |= FL_NOTARGET;
			event->takedamage[slot] = ent->v.takedamage;
			ent->v.takedamage = DAMAGE_NO;
		}
		return;
	}
	SV_CoF_RetailScreenFade( cl, 0.5f, 0.0f, 0, 0, 0, 0, 255 );
	if( event->froze[slot] && SV_IsValidEdict( ent ))
	{
		ent->v.flags &= ~FL_FROZEN;
		if( event->notarget_added[slot] ) ent->v.flags &= ~FL_NOTARGET;
		if( ent->v.takedamage == DAMAGE_NO ) ent->v.takedamage = event->takedamage[slot];
	}
	event->froze[slot] = event->notarget_added[slot] = false;
}

// An ending event ends for a companion without arriving: never leave them held.
static void CoF_PartyEndingRelease( cof_party_door_t *event )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( event->froze[i] ) CoF_PartyEndingHold( event, i, false );
}

qboolean SV_CoF_PartyHolding( void )
{
	int slot, i;
	for( slot = 0; slot < MAX_CLIENTS; slot++ )
		for( i = 0; cof_party_doors[slot].active && i < MAX_CLIENTS; i++ )
			if( cof_party_doors[slot].froze[i] ) return true;
	return false;
}

static void CoF_PartyWatchDoors( void )
{
	int i, slot, j;
	qboolean ending;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *door = SV_EdictNum( i ), *exit;
		if( !SV_IsValidEdict( door ) || ( door->v.flags & FL_KILLME ) ||
			!door->v.owner || Q_strcmp( SV_GetString( door->v.classname ), "inter_door" )) continue;
		for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
		{
			sv_client_t *cl = &svs.clients[slot];
			cof_party_door_t *event = &cof_party_doors[slot];
			byte *pv;
			if(( event->active && !event->completed ) || cl->edict != door->v.owner ) continue;
			pv = CoF_PartyPlayer( cl );
			if( !pv || !pv[COF_PARTY_DOOR_FLAG] ) continue;
			exit = CoF_PartyDoorExit( door, &ending );
			if( !exit ) continue; // Menu/camera targets are not party doors.
			CoF_PartyEndingRelease( event ); // a finished event may still hold someone
			memset( event, 0, sizeof( *event ));
			event->active = true;
			event->owner = CoF_PartyIdentity( cl );
			VectorCopy( exit->v.origin, event->destination );
			VectorCopy( exit->v.angles, event->angles );
			VectorCopy( cl->edict->v.origin, event->start );
			event->deadline = sv.time + 8.0;
			event->door = door;
			event->door_serial = door->serialnumber;
			event->ending = ending;
			for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
			{
				if( j == slot || !CoF_PartyAvailable( &svs.clients[j] )) continue;
				// Only a partner right beside the opener goes through with them;
				// through the ending door, everyone does: one ending for the party.
				if( !ending && CoF_PartyDistanceSquared( svs.clients[j].edict->v.origin, cl->edict->v.origin ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
				event->companions[j] = CoF_PartyIdentity( &svs.clients[j] );
				event->pending[j] = true;
				if( ending ) CoF_PartyEndingHold( event, j, true );
			}
			Con_Printf( "cofstory phase=door-begin userid=%d target=%s%s\n", cl->userid, SV_GetString( door->v.target ), ending ? " ending=1" : "" );
		}
	}
}

static qboolean CoF_PartyLandingWithin( sv_client_t *cl, edict_t *owner, const vec3_t exit, vec3_t result, float limit );

static qboolean CoF_PartyLanding( sv_client_t *cl, edict_t *owner, const vec3_t exit, vec3_t result )
{
	return CoF_PartyLandingWithin( cl, owner, exit, result, 1000.0f );
}

// One candidate spot beside center (feet height already applied): reachable
// from center without crossing a wall, free for the hull, over a floor.
static qboolean CoF_PartyLandingPoint( sv_client_t *cl, edict_t *owner, const vec3_t center, vec3_t point, vec3_t result )
{
	vec3_t down, zero = { 0, 0, 0 };
	edict_t *ent = cl->edict;
	trace_t tr;
	// Do not place a companion through the next wall into a different room.
	tr = SV_Move( center, zero, zero, point, MOVE_NOMONSTERS, owner, false );
	if( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) return false;
	tr = SV_Move( point, ent->v.mins, ent->v.maxs, point, MOVE_NORMAL, ent, false );
	if( tr.startsolid || tr.allsolid ) return false;
	VectorCopy( point, down ); down[2] -= 64.0f;
	tr = SV_Move( point, ent->v.mins, ent->v.maxs, down, MOVE_NORMAL, ent, false );
	if( tr.startsolid || tr.allsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.7f ) return false;
	VectorCopy( tr.endpos, point ); point[2] += 1.0f;
	tr = SV_Move( point, ent->v.mins, ent->v.maxs, point, MOVE_NORMAL, ent, false );
	if( tr.startsolid || tr.allsolid || !CoF_PartyVectorValid( point )) return false;
	VectorCopy( point, result );
	return true;
}

static qboolean CoF_PartyLandingWithin( sv_client_t *cl, edict_t *owner, const vec3_t exit, vec3_t result, float limit )
{
	static const float offsets[][2] = {
		// 35 leaves a 1-unit gap between the padded absboxes, so retail PreThink
		// keeps both players solid. The hahaland loading rooms only fit a second
		// hull at 34 (touching; both go SOLID_NOT until one steps away), so it
		// stays as the last resort. Keep the full collision/LOS/floor tests below.
		{ 35, 0 }, { -35, 0 }, { 0, 35 }, { 0, -35 },
		{ 40, 0 }, { -40, 0 }, { 0, 40 }, { 0, -40 },
		{ 40, 40 }, { -40, 40 }, { 40, -40 }, { -40, -40 },
		{ 72, 0 }, { -72, 0 }, { 0, 72 }, { 0, -72 },
		{ 34, 0 }, { -34, 0 }, { 0, 34 }, { 0, -34 }
	};
	vec3_t center, point;
	edict_t *ent = cl->edict;
	int i;
	if( !CoF_PartyVectorValid( ent->v.mins ) || !CoF_PartyVectorValid( ent->v.maxs )) return false;
	VectorCopy( exit, center );
	center[2] -= ent->v.mins[2];
	center[2] += 1.0f;
	for( i = 0; i < (int)ARRAYSIZE( offsets ); i++ )
	{
		if( fabs( offsets[i][0] ) > limit || fabs( offsets[i][1] ) > limit ) continue;
		VectorCopy( center, point );
		point[0] += offsets[i][0]; point[1] += offsets[i][1];
		if( CoF_PartyLandingPoint( cl, owner, center, point, result )) return true;
	}
	return false;
}

// The AI companion catching up: a spot behind the leader (out of their view
// where possible), else anywhere beside them; facing the way they face.
qboolean SV_CoF_PartyPlaceBehind( sv_client_t *mover, edict_t *leader )
{
	static const float behind[][2] = { { -64, 0 }, { -64, 40 }, { -64, -40 }, { -96, 0 }, { -40, 48 }, { -40, -48 } };
	vec3_t feet, center, point, landing, angles = { 0, 0, 0 };
	float yaw, c, s;
	byte *pv = CoF_PartyPlayer( mover );
	int i;
	if( !pv || !SV_IsValidEdict( leader ) || !CoF_PartyVectorValid( leader->v.origin ) ||
		!CoF_PartyVectorValid( mover->edict->v.mins ) || !CoF_PartyVectorValid( mover->edict->v.maxs )) return false;
	yaw = leader->v.angles[1] / 57.2957795f;
	c = cosf( yaw ); s = sinf( yaw );
	VectorCopy( leader->v.origin, feet );
	feet[2] += mover->edict->v.mins[2] - 1.0f;
	VectorCopy( feet, center );
	center[2] -= mover->edict->v.mins[2];
	center[2] += 1.0f;
	angles[1] = leader->v.angles[1];
	for( i = 0; i < (int)ARRAYSIZE( behind ); i++ )
	{
		VectorCopy( center, point );
		point[0] += behind[i][0] * c - behind[i][1] * s;
		point[1] += behind[i][0] * s + behind[i][1] * c;
		if( CoF_PartyLandingPoint( mover, leader, center, point, landing ))
			return CoF_PartyMoveEdict( mover->edict, pv, landing, angles );
	}
	if( CoF_PartyLanding( mover, leader, feet, landing ))
		return CoF_PartyMoveEdict( mover->edict, pv, landing, angles );
	return false;
}

// Mirrors retail CBaseEntity::Intersects on engine-padded absboxes (VA 10017D80).
static qboolean CoF_PartyBoxesTouch( const edict_t *a, const edict_t *b )
{
	int i;
	for( i = 0; i < 3; i++ )
	{
		if( a->v.origin[i] + a->v.mins[i] - 1.0f > b->v.origin[i] + b->v.maxs[i] + 1.0f ) return false;
		if( b->v.origin[i] + b->v.mins[i] - 1.0f > a->v.origin[i] + a->v.maxs[i] + 1.0f ) return false;
	}
	return true;
}

// A floor point under a standing player, in the inter_door_exit convention
// CoF_PartyLanding expects (it adds -mins.z + 1 back).
static void CoF_PartyFeet( const sv_client_t *mover, const edict_t *at, vec3_t feet )
{
	VectorCopy( at->v.origin, feet );
	feet[2] += mover->edict->v.mins[2] - 1.0f;
}

// The door that began this event is still there to be used again.
static qboolean CoF_PartyDoorRemains( const cof_party_door_t *event )
{
	return SV_IsValidEdict( event->door ) && event->door->serialnumber == event->door_serial &&
		!( event->door->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( event->door->v.classname ), "inter_door" );
}

// The owner went where the others cannot walk after them. Tell every living
// player who was not taken along and is not already there how to follow.
static void CoF_PartyCutOff( int slot, const cof_party_door_t *event, const char *kind )
{
	sv_client_t *owner = &svs.clients[slot];
	char text[96];
	int i;
	Q_snprintf( text, sizeof( text ), "%s went somewhere you can't follow. Press J to join.", owner->name );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( i == slot || event->pending[i] || ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl )) continue;
		if( CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
		SV_CoF_RetailProFont( cl, 1, text, true );
		Con_Printf( "cofstory phase=cut-off userid=%d owner=%d kind=%s\n", cl->userid, owner->userid, kind );
	}
}

static void CoF_PartyFinishDoors( void )
{
	int slot, i;
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		cof_party_door_t *event = &cof_party_doors[slot];
		sv_client_t *owner = &svs.clients[slot];
		const char *kind = event->teleport ? "teleport" : "door";
		byte *pv;
		qboolean pending = false;
		vec3_t expected;
		if( !event->active ) continue;
		if( !CoF_PartySamePlayer( &event->owner, owner ) || !( pv = CoF_PartyPlayer( owner )))
		{
			CoF_PartyEndingRelease( event );
			event->active = false;
			continue;
		}
		if( sv.time > event->deadline )
		{
			Con_Printf( "cofstory phase=%s-expired userid=%d reason=%s\n", kind, owner->userid, event->completed ? "party-not-ready" : "sequence-incomplete" );
			CoF_PartyEndingRelease( event );
			event->active = false;
			continue;
		}
		if( !event->completed )
		{
			if( pv[COF_PARTY_DOOR_FLAG] ) continue;
			VectorCopy( event->destination, expected );
			expected[2] -= owner->edict->v.mins[2]; expected[2] += 1.0f;
			// The cleared flag plus standing at the exit proves completion, even
			// when the player used the door from its own exit (start == exit).
			// Visibility is handled per player by CoF_PartyDoorVisibility.
			if( CoF_PartyDistanceSquared( owner->edict->v.origin, expected ) > 32.0f * 32.0f )
			{
				Con_Printf( "cofstory phase=door-cancelled userid=%d reason=exit-not-confirmed\n", owner->userid );
				CoF_PartyEndingRelease( event );
				event->active = false;
				continue;
			}
			event->completed = true;
			event->deadline = sv.time + 5.0;
			// A one-shot door is gone now: nobody else can take it.
			if( !CoF_PartyDoorRemains( event )) CoF_PartyCutOff( slot, event, "door" );
		}
		for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		{
			sv_client_t *cl = &svs.clients[i];
			vec3_t landing;
			if( !event->pending[i] ) continue;
			if( !CoF_PartySamePlayer( &event->companions[i], cl ) ||
				!( cl->edict->v.health > 0 ) || cl->edict->v.deadflag != DEAD_NO ||
				( cl->edict->v.flags & FL_KILLME ))
			{
				Con_Printf( "cofstory phase=%s-party-skipped userid=%d reason=player-state-changed\n", kind, event->companions[i].userid );
				event->pending[i] = false;
				if( event->froze[i] ) CoF_PartyEndingHold( event, i, false );
				continue;
			}
			// EndSequence may start a camera/freeze before this post-frame hook.
			// Keep the captured follower until the scene releases them, subject to
			// the original bounded completion deadline; do not clear scene state.
			// A partner watching the owner's own scene moves now, still watching;
			// one the ending door holds (frozen by us) moves now too.
			if( !CoF_PartyAvailable( cl ) && !SV_CoF_SceneWatching( owner->userid, cl->userid ) && !event->froze[i] )
			{
				pending = true;
				continue;
			}
			// The ending rooms sit side by side: stay inside the opener's. Only two
			// partners fit beside the opener; more share the opener's spot (retail
			// PreThink lets overlapping players pass through each other). A scripted
			// teleport's room may fit only one (c_intro's last, `kunitei`, 86 units
			// wide, holds the exit to c_nightmare): share the spot there too.
			if( !CoF_PartyLandingWithin( cl, owner->edict, event->destination, landing, event->ending ? COF_PARTY_ENDING_ROOM : 1000.0f ))
			{
				if( !event->ending && !event->teleport )
				{
					pending = true;
					continue;
				}
				if( event->teleport )
				{
					// where the owner landed, standing, not where they walked since
					VectorCopy( event->destination, landing );
					landing[2] += 1.0f - cl->edict->v.mins[2];
				}
				else VectorCopy( owner->edict->v.origin, landing );
			}
			// The regular movement pass touches triggers; do not invoke game callbacks
			// in this loop, which could remove entities or change the map underneath it.
			if( !CoF_PartyMove( cl, landing, event->angles ))
			{
				event->pending[i] = false;
				if( event->froze[i] ) CoF_PartyEndingHold( event, i, false );
				continue;
			}
			event->pending[i] = false;
			if( event->froze[i] ) CoF_PartyEndingHold( event, i, false );
			Con_Printf( "cofstory phase=%s-party-moved userid=%d owner=%d origin=%.3f,%.3f,%.3f\n", kind, cl->userid, owner->userid, landing[0], landing[1], landing[2] );
		}
		if( !pending ) event->active = false;
	}
}

// info_teleport_destination at the player's new spot that a cof_teleport (a
// scripted, activator-only move) targets. trigger_teleport and ladders are
// excluded: the partner can walk into those themselves.
static edict_t *CoF_PartyScriptedDestination( const vec3_t origin )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *dest = SV_EdictNum( i );
		const char *name;
		if( !SV_IsValidEdict( dest ) || ( dest->v.flags & FL_KILLME ) ||
			Q_strcmp( SV_GetString( dest->v.classname ), "info_teleport_destination" ) ||
			!CoF_PartyVectorValid( dest->v.origin ) ||
			fabs( dest->v.origin[0] - origin[0] ) > 8.0f || fabs( dest->v.origin[1] - origin[1] ) > 8.0f ||
			fabs( dest->v.origin[2] - origin[2] ) > 96.0f ) continue;
		name = SV_GetString( dest->v.targetname );
		if( !name[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *tele = SV_EdictNum( j );
			if( SV_IsValidEdict( tele ) && !( tele->v.flags & FL_KILLME ) &&
				!Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" ) &&
				!Q_strcmp( SV_GetString( tele->v.target ), name )) return dest;
		}
	}
	return NULL;
}

static void CoF_PartyTeleports( void )
{
	int i, j, count;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		cof_party_door_t *event = &cof_party_doors[i];
		edict_t *dest;
		byte *pv;
		if( !p->teleported ) continue;
		p->teleported = false;
		if( !CoF_PartySamePlayer( &p->identity, cl ) || !( pv = CoF_PartyPlayer( cl )) ||
			pv[COF_PARTY_DOOR_FLAG] || p->place_pending || ( event->active && !event->completed )) continue;
		if( CoF_PartyDistanceSquared( p->teleport_from, p->teleport_to ) < COF_PARTY_TELEPORT_MIN * COF_PARTY_TELEPORT_MIN ) continue;
		// The player may already have moved on this frame; match the landing spot.
		if( !( dest = CoF_PartyScriptedDestination( p->teleport_to ))) continue;
		CoF_PartyEndingRelease( event );
		memset( event, 0, sizeof( *event ));
		event->active = event->completed = event->teleport = true;
		event->owner = p->identity;
		CoF_PartyFeet( cl, cl->edict, event->destination );
		event->angles[1] = cl->edict->v.angles[1];
		VectorCopy( p->teleport_from, event->start );
		event->deadline = sv.time + COF_PARTY_TELEPORT_WAIT;
		for( j = count = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *other = &svs.clients[j];
			if( j == i || !CoF_PartyPlayer( other ) || cof_party_players[j].place_pending ||
				!CoF_PartySamePlayer( &cof_party_players[j].identity, other )) continue;
			// A partner already beside the destination was part of the scene.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, cl->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			// Only a partner who stood beside the activator, or watched the
			// activator's cutscene with them, is taken along.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, p->teleport_from ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR &&
				!SV_CoF_SceneWatching( cl->userid, other->userid )) continue;
			event->companions[j] = cof_party_players[j].identity;
			event->pending[j] = true;
			count++;
		}
		Con_Printf( "cofstory phase=teleport-begin userid=%d target=%s companions=%d\n", cl->userid, SV_GetString( dest->v.targetname ), count );
		CoF_PartyCutOff( i, event, "teleport" );
		if( !count ) event->active = false;
	}
}

// Retail multiplayer PostThink (VA 100EBD75) ORs EF_NODRAW into a living
// player while b_GoingThroughADoor is set and nothing ever clears it. Undo
// exactly that hide once the door is over and the player is back in play.
static void CoF_PartyDoorVisibility( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		byte *pv = CoF_PartyPlayer( cl );
		if( !pv || !CoF_PartySamePlayer( &p->identity, cl )) continue;
		if( pv[COF_PARTY_DOOR_FLAG] )
		{
			p->door_hidden = true;
			continue;
		}
		// A camera/freeze scene right after the door keeps the mark until it ends.
		if( !p->door_hidden || !CoF_PartyPlayable( cl )) continue;
		p->door_hidden = false;
		if( cl->edict->v.effects & EF_NODRAW )
		{
			cl->edict->v.effects &= ~EF_NODRAW;
			Con_Printf( "cofstory phase=door-visibility-restored userid=%d\n", cl->userid );
		}
	}
}

// New joins and respawns appear at the map's spawn point, which in story maps
// is frequently the arrival spot of the previous transition: inside or far
// behind the partner. Move them next to an available partner once, early.
static void CoF_PartyPlace( void )
{
	int i, j;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i], *partner = NULL;
		cof_party_player_t *p = &cof_party_players[i];
		float best = 0.0f, d;
		vec3_t feet, landing, angles = { 0, 0, 0 };
		if( !p->place_pending ) continue;
		if( !CoF_PartySamePlayer( &p->identity, cl ) || !CoF_PartyPlayer( cl ) || sv.time > p->place_deadline )
		{
			p->place_pending = false;
			continue;
		}
		if( !CoF_PartyAvailable( cl )) continue; // intro camera, freeze, door
		// A spawn that walked off chose its own way; a story arrival that walked
		// off may just be pacing a sealed room, so it still regroups.
		if( !p->place_regroup && CoF_PartyDistanceSquared( cl->edict->v.origin, p->place_start ) > COF_PARTY_JOIN_WANDER * COF_PARTY_JOIN_WANDER )
		{
			p->place_pending = false;
			Con_Printf( "cofstory phase=spawn-kept userid=%d reason=player-moved\n", cl->userid );
			continue;
		}
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *other = &svs.clients[j];
			if( j == i || cof_party_players[j].place_pending || !CoF_PartyAvailable( other )) continue;
			d = CoF_PartyDistanceSquared( cl->edict->v.origin, other->edict->v.origin );
			if( !partner || d < best ) { partner = other; best = d; }
		}
		if( !partner ) continue; // wait for a partner to leave its scene
		if( best <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR && !CoF_PartyBoxesTouch( cl->edict, partner->edict ))
		{
			p->place_pending = false;
			Con_Printf( "cofstory phase=spawn-kept userid=%d reason=partner-near\n", cl->userid );
			continue;
		}
		CoF_PartyFeet( cl, partner->edict, feet );
		if( !CoF_PartyLanding( cl, partner->edict, feet, landing )) continue; // retry until deadline
		angles[1] = partner->edict->v.angles[1];
		if( !CoF_PartyMove( cl, landing, angles )) continue;
		p->place_pending = false;
		Con_Printf( "cofstory phase=spawn-placed userid=%d partner=%d origin=%.3f,%.3f,%.3f\n", cl->userid, partner->userid, landing[0], landing[1], landing[2] );
	}
}

static qboolean CoF_PartyAbsTouch( const edict_t *a, const edict_t *b )
{
	int i;
	for( i = 0; i < 3; i++ )
		if( a->v.absmin[i] > b->v.absmax[i] || b->v.absmin[i] > a->v.absmax[i] ) return false;
	return true;
}

// One of the trigger_changelevel entities of the transition being captured,
// matched by brush model. Without entity data, any trigger_changelevel.
static qboolean CoF_PartyIsExit( edict_t *ent, const int *models, int count )
{
	const char *model;
	int i, number;
	if( !SV_IsValidEdict( ent ) || ( ent->v.flags & FL_KILLME ) ||
		Q_strcmp( SV_GetString( ent->v.classname ), "trigger_changelevel" )) return false;
	if( count <= 0 ) return true;
	model = SV_GetString( ent->v.model );
	if( model[0] != '*' ) return false;
	number = Q_atoi( model + 1 );
	for( i = 0; i < count; i++ )
		if( models[i] == number ) return true;
	return false;
}

// Standing in this transition's exit: its trigger_changelevel, or a trigger
// that fires it (the c_apartment exits are use-only, fired by trigger_multiple).
qboolean SV_CoF_PartyInExit( const edict_t *player, const int *models, int count )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *trigger = SV_EdictNum( i );
		const char *target;
		qboolean exit;
		if( !SV_IsValidEdict( trigger ) || ( trigger->v.flags & FL_KILLME ) ||
			Q_strncmp( SV_GetString( trigger->v.classname ), "trigger_", 8 ) ||
			!CoF_PartyAbsTouch( player, trigger )) continue;
		exit = CoF_PartyIsExit( trigger, models, count );
		target = SV_GetString( trigger->v.target );
		for( j = svs.maxclients + 1; !exit && target[0] && j < svgame.numEntities; j++ )
		{
			edict_t *level = SV_EdictNum( j );
			exit = CoF_PartyIsExit( level, models, count ) &&
				!Q_strcmp( SV_GetString( level->v.targetname ), target );
		}
		if( exit ) return true;
	}
	return false;
}

// Whether anything can still ask for this exit by touch: its trigger_changelevel
// takes touches itself, or a live trigger fires it by name. A once-only
// trigger removes itself after firing; if it was the only way in, the exit is
// never asked for again.
qboolean SV_CoF_PartyExitRefires( const int *models, int count )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *level = SV_EdictNum( i );
		const char *name;
		if( !CoF_PartyIsExit( level, models, count )) continue;
		if( !( level->v.spawnflags & COF_PARTY_CHANGELEVEL_USEONLY )) return true;
		name = SV_GetString( level->v.targetname );
		for( j = svs.maxclients + 1; name[0] && j < svgame.numEntities; j++ )
		{
			edict_t *trigger = SV_EdictNum( j );
			if( SV_IsValidEdict( trigger ) && !( trigger->v.flags & FL_KILLME ) &&
				!Q_strncmp( SV_GetString( trigger->v.classname ), "trigger_", 8 ) &&
				!Q_strcmp( SV_GetString( trigger->v.target ), name )) return true;
		}
	}
	return false;
}

// Called once a story transition has captured the party, before teardown.
// A story transition carries every player at its offset from the exit's
// landmark. The one who fired the exit arrives where the map expects; a
// partner who was elsewhere lands wherever that offset points in the new map:
// inside a wall, outside the world or in a sealed room. Record who is who.
void SV_CoF_PartyTransitionCapture( unsigned int epoch, const vec3_t landmark, const int *exit_models, int exit_count )
{
	int i, leader = -1, strays = 0;
	qboolean leader_in_exit = false;
	float best = 0.0f;
	memset( &cof_party_arrival, 0, sizeof( cof_party_arrival ));
	if( !SV_CoF_StoryCoopEnabled() ) return;
	// The transition save carries every player's flags: no ending hold may travel.
	for( i = 0; i < MAX_CLIENTS; i++ ) CoF_PartyEndingRelease( &cof_party_doors[i] );
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		qboolean in_exit;
		float d;
		if( ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl )) continue;
		in_exit = SV_CoF_PartyInExit( cl->edict, exit_models, exit_count );
		d = CoF_PartyDistanceSquared( cl->edict->v.origin, landmark );
		// Whoever stands in the exit fired it; otherwise (a scripted exit)
		// the player nearest its landmark is the one the scene was about.
		if( leader < 0 || ( in_exit && !leader_in_exit ) || ( in_exit == leader_in_exit && d < best ))
		{
			leader = i;
			best = d;
			leader_in_exit = in_exit;
		}
	}
	if( leader < 0 ) return;
	cof_party_arrival.epoch = epoch;
	cof_party_arrival.leader = leader + 1;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl )) continue;
		cof_party_arrival.userid[i] = cl->userid;
		cof_party_arrival.stray[i] = i != leader && CoF_PartyDistanceSquared( cl->edict->v.origin,
			svs.clients[leader].edict->v.origin ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR;
		if( cof_party_arrival.stray[i] ) strays++;
	}
	Con_Printf( "cofstory phase=exit-leader userid=%d in_exit=%d strays=%d exits=%d\n", svs.clients[leader].userid, leader_in_exit, strays, exit_count );
}

// The player's hull at its current spot overlaps the world or a brush entity.
static qboolean CoF_PartyInSolid( edict_t *ent )
{
	trace_t tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NOMONSTERS, ent, false );
	return tr.startsolid || tr.allsolid;
}

// Free and standing over a floor within reach; not a spot over the void.
static qboolean CoF_PartyStandable( edict_t *ent )
{
	vec3_t down;
	trace_t tr;
	if( CoF_PartyInSolid( ent )) return false;
	VectorCopy( ent->v.origin, down );
	down[2] -= 512.0f;
	tr = SV_Move( ent->v.origin, ent->v.mins, ent->v.maxs, down, MOVE_NOMONSTERS, ent, false );
	return !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f;
}

static byte *CoF_PartyArrived( sv_client_t *cl, int userid )
{
	byte *pv;
	if( !userid || cl->userid != userid || !( pv = CoF_PartyLivingPlayer( cl->edict )) || pv[COF_PARTY_DOOR_FLAG] ) return NULL;
	return pv;
}

// Called by the restore, after every carried player exists in the new map and
// before any client runs a command there, so nobody falls or sticks first.
void SV_CoF_PartyTransitionArrive( unsigned int epoch )
{
	cof_party_arrival_t arrival = cof_party_arrival;
	int i, anchor = -1;
	memset( &cof_party_arrival, 0, sizeof( cof_party_arrival ));
	if( !SV_CoF_StoryCoopEnabled() || !arrival.leader || arrival.epoch != epoch ) return;
	CoF_PartyCheckGeneration();
	// Everyone gathers at the leader, who arrives where the map expects. Only if
	// the leader landed inside solid, gather at a player whose spot is fine. (No
	// floor below is normal for an exit taken while falling down a shaft.)
	i = arrival.leader - 1;
	if( i < svs.maxclients && CoF_PartyArrived( &svs.clients[i], arrival.userid[i] )) anchor = i;
	if( anchor < 0 || CoF_PartyInSolid( svs.clients[anchor].edict ))
	{
		for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		{
			if( i == anchor || !CoF_PartyArrived( &svs.clients[i], arrival.userid[i] ) ||
				!CoF_PartyStandable( svs.clients[i].edict )) continue;
			if( anchor >= 0 ) arrival.stray[anchor] = true; // move the leader instead
			anchor = i;
			break;
		}
	}
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *ent = cl->edict, *beside = anchor >= 0 ? svs.clients[anchor].edict : NULL;
		byte *pv = CoF_PartyArrived( cl, arrival.userid[i] );
		vec3_t feet, landing, angles = { 0, 0, 0 };
		if( !pv || i == anchor || ( !arrival.stray[i] && !CoF_PartyInSolid( ent ))) continue;
		if( beside )
		{
			angles[1] = beside->v.angles[1];
			CoF_PartyFeet( cl, beside, feet );
			if( CoF_PartyLanding( cl, beside, feet, landing ) && CoF_PartyMoveEdict( ent, pv, landing, angles ))
			{
				Con_Printf( "cofstory phase=arrival-placed userid=%d beside=%d origin=%.3f,%.3f,%.3f\n", cl->userid, svs.clients[anchor].userid, landing[0], landing[1], landing[2] );
				continue;
			}
		}
		// Not placed yet: once both play, CoF_PartyPlace keeps a partner that is
		// already near or moves it beside one. A standable spot can still be a
		// sealed room, so it gets the same second chance.
		cof_party_regroup[i] = cl->userid;
		if( CoF_PartyStandable( ent ))
		{
			Con_Printf( "cofstory phase=arrival-kept userid=%d reason=%s\n", cl->userid, beside ? "no-room-beside-leader" : "leader-missing" );
			continue;
		}
		// The carried spot is solid or over the void: share the anchor's spot
		// (retail PreThink lets overlapping players pass through each other),
		// unless the anchor is stuck in solid itself; then leave it to regroup.
		if( beside && !CoF_PartyInSolid( beside ) && CoF_PartyMoveEdict( ent, pv, beside->v.origin, angles ))
			Con_Printf( "cofstory phase=arrival-overlapped userid=%d beside=%d\n", cl->userid, svs.clients[anchor].userid );
		else Con_Printf( "cofstory phase=arrival-stranded userid=%d\n", cl->userid );
	}
}

// A refusal starts the cooldown too, so a mashed key logs one line per cooldown.
static void CoF_PartyWarpRefused( sv_client_t *cl, int slot, const char *reason, const char *text )
{
	cof_party_players[slot].warp_after = sv.time + COF_PARTY_WARP_COOLDOWN;
	Con_Printf( "cofstory phase=warp-refused userid=%d reason=%s\n", cl->userid, reason );
	if( text ) SV_CoF_RetailProFont( cl, 1, text, false );
}

// cof_warp (bound to J): join the partner, wherever they are.
static void CoF_PartyWarp( sv_client_t *cl )
{
	int i = CoF_PartySlot( cl ), j, waiter = SV_CoF_ExitWaiter();
	sv_client_t *partner = NULL;
	float best = 0.0f, d;
	vec3_t feet, landing, angles = { 0, 0, 0 };
	char text[96];
	byte *pv;
	if( i < 0 ) return;
	CoF_PartyCheckGeneration();
	if( sv.time < cof_party_players[i].warp_after ) return;
	if( !( pv = CoF_PartyPlayer( cl )))
	{
		CoF_PartyWarpRefused( cl, i, "dead", NULL ); // the game's respawn prompt already speaks to them
		return;
	}
	if( pv[COF_PARTY_DOOR_FLAG] )
	{
		CoF_PartyWarpRefused( cl, i, "door", NULL ); // the door sequence moves them itself
		return;
	}
	// A cutscene, ladder climb or puzzle view holds them; a warp would move the
	// body and leave that camera and freeze behind.
	if( cl->edict->v.flags & FL_FROZEN )
	{
		CoF_PartyWarpRefused( cl, i, "frozen", NULL );
		return;
	}
	for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
	{
		sv_client_t *other = &svs.clients[j];
		byte *opv = CoF_PartyPlayer( other );
		if( j == i || !opv || opv[COF_PARTY_DOOR_FLAG] || ( other->flags & FCL_FAKECLIENT )) continue;
		// During an exit hold the one waiting at the exit is the place to be.
		if( waiter && other->userid == waiter )
		{
			partner = other;
			break;
		}
		d = CoF_PartyDistanceSquared( cl->edict->v.origin, other->edict->v.origin );
		if( !partner || d < best ) { partner = other; best = d; }
	}
	if( !partner )
	{
		CoF_PartyWarpRefused( cl, i, "no-partner", "Nobody to join right now." );
		return;
	}
	CoF_PartyFeet( cl, partner->edict, feet );
	angles[1] = partner->edict->v.angles[1];
	if( !CoF_PartyLanding( cl, partner->edict, feet, landing ) || !CoF_PartyMove( cl, landing, angles ))
	{
		Q_snprintf( text, sizeof( text ), "No room next to %s.", partner->name );
		CoF_PartyWarpRefused( cl, i, "no-room", text );
		return;
	}
	cof_party_players[i].warp_after = sv.time + COF_PARTY_WARP_COOLDOWN;
	// Black, fading in over half a second: the retail co-op door's blink. The
	// warp does not depend on it being seen.
	SV_CoF_RetailScreenFade( cl, 0.5f, 0.0f, 0, 0, 0, 0, 255 );
	Con_Printf( "cofstory phase=warp userid=%d partner=%d origin=%.3f,%.3f,%.3f\n", cl->userid, partner->userid, landing[0], landing[1], landing[2] );
}

// Client commands the story layer answers itself, before the game DLL sees them.
qboolean SV_CoF_PartyClientCommand( sv_client_t *cl )
{
	if( Q_strcmp( Cmd_Argv( 0 ), "cof_warp" ) || sv.state != ss_active || !SV_CoF_StoryCoopEnabled() ) return false;
	// A held exit's waiter uses the same key as the escape hatch.
	if( cl->state == cs_spawned && !SV_CoF_ExitWarpKey( cl )) CoF_PartyWarp( cl );
	return true;
}

void SV_CoF_PartyFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	CoF_PartyCheckGeneration();
	CoF_PartyTrackLife();
	CoF_PartyWatchDoors();
	CoF_PartyTeleports();
	CoF_PartyFinishDoors();
	CoF_PartyDoorVisibility();
	CoF_PartyPlace();
	CoF_PartyItems();
}
