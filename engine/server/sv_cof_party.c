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
#include "sv_cof_companion.h"
#include "sv_cof_sections.h"
#include "sv_cof_doorroom.h"

#define COF_PARTY_DOOR_FLAG 0x1EA4
#define COF_PARTY_PREVIOUS_ORIGIN 0x618
#define COF_PARTY_INVENTORY 0x21FC
#define COF_PARTY_SLOTS 13 // indices 0..12; 0 is never used
#define COF_PARTY_PLAYER_SIZE (COF_PARTY_INVENTORY + COF_PARTY_SLOTS * sizeof( string_t ))
// Retail PreThink (100EF2D0) makes a player SOLID_NOT while its absbox touches
// another player's; boxes are padded 1 unit, so centers 34 apart still touch.
#define COF_PARTY_JOIN_WAIT 30.0
#define COF_PARTY_JOIN_NEAR 128.0f
#define COF_PARTY_UNSEEN_ANGLE 75.0f // an AI companion is placed at least this far off where the player looks...
#define COF_PARTY_UNSEEN_MIN 48.0f // ...and never closer than this
#define COF_PARTY_SEEN_LEAST 30.0f // with no spot out of sight (after a wait): at least this far off the view axis
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
// 1.1.8 (M3 2c): the "Press J to join" line again this often while its reader stays cut off, this many times at most.
#define COF_PARTY_CUTOFF_EVERY 10.0
#define COF_PARTY_CUTOFF_REPEATS 3
// 1.1.8 (E1 F6): a shared weapon the game would not take is offered again after 5 s, then 10, 20, 40, 60, 60...
#define COF_PARTY_GIVE_RETRY 5.0
#define COF_PARTY_GIVE_RETRY_MAX 60.0
// 1.1.8 (M1 2, 6): a live trigger_hurt doing this much kills; a scripted teleport into one is nobody else's.
#define COF_PARTY_DEADLY_HURT 100.0f
#define COF_PARTY_TELEPORT_NAMES 64 // cof_teleport names kept per map (story maps, merged too: 12 at most); more are followed
// 1.1.9 (P1): a one-shot teleport (see CoF_PartyReadOneShot) moving its activator this far takes every partner along.
#define COF_PARTY_CHAIN_NAMES 96 // cof_teleport and multi_manager names followed up a one-shot chain
#define COF_PARTY_CHAIN_FIRERS 8 // multi_managers firing one name
#define COF_PARTY_CHAIN_PASSES 8 // multi_managers deep
#define COF_PARTY_ONEWAY_MIN 400.0f // shorter: a step through a wall the partner may walk round (the chain audit's round trips)
#define COF_PARTY_HURT_NEAR 256.0f // a deadly trigger_hurt this near the landing: maybe a kill room, not everyone's
// 1.1.9 (S6, K8): the music the game plays a player, sent by its UpdateClientData while the update flag is set
// (player.cpp:5516-5544; offsets of the decompilation's CBasePlayer layout, which the 2026-10-18 offsets check found
// to agree): m_strCurrentMp3, m_iMp3NeedsUpdate, m_fMp3FadeTime, bool m_bMp3ShouldLoop, m_fMp3FadeInTime.
#define COF_PARTY_MP3 0x2310
#define COF_PARTY_MP3_UPDATE 0x2314
#define COF_PARTY_MP3_FADE_OUT 0x2318
#define COF_PARTY_MP3_LOOP 0x231C
#define COF_PARTY_MP3_FADE_IN 0x2320
#define COF_PARTY_MP3_SIZE 0x2324

// Weapons every player gets once anyone has one: the c_start starter kit, and
// every weapon a story script hands out (cof_giveitems with a classname, Use
// 100A6BC0 gives it to its activator only, from one-shot sources: the glock in
// c_apartment1, the flashlight in c_asylumb, the camera in c_nightmare, the
// shotgun in c_sewer2). The list grows as maps with such gifts are visited.
#define COF_PARTY_SHARED 12 // 1.1.8: room for a chapter start's kit too (sv_cof_chapter_kit.h)
static const char *cof_party_starters[] = { "weapon_mobile", "weapon_switchblade" };
static struct
{
	char classname[32];
	char descriptor[64];
} cof_party_shared[COF_PARTY_SHARED];
static int cof_party_shared_count;
// 1.1.8 (chapter starts): the weapons a chapter start hands out, shared as if somebody carried them, on the map the
// chapter starts on; from the next map on the players who carry them share them as usual.
static unsigned int cof_party_granted;
// 1.1.9 (S8): shared weapons the story took back (CoF_PartyWithdraw): never handed out again this session.
static unsigned int cof_party_withdrawn;
// Which shared weapons each player has had (by userid), kept across map
// changes: a weapon they dropped on purpose is not handed back on the next map.
// held (1.1.9, S8): what they carried, alive, at the last look, kept across a level change too.
static struct
{
	int userid;
	unsigned int seen, held;
} cof_party_seen[MAX_CLIENTS];
// 1.1.9 (S8): the loaded map's cof_clearitems that take a weapon (message weapon_*), read once a map.
#define COF_PARTY_CLEARERS 8
static struct
{
	int generation, count;
	int entity[COF_PARTY_CLEARERS], serial[COF_PARTY_CLEARERS];
} cof_party_clearers = { -1, 0, { 0 }, { 0 } };

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
	byte refused[COF_PARTY_SHARED]; // gives the game refused this map (1.1.8, E1 F6): asked less often, logged once
	qboolean was_alive, seen_dead;
	// Retail multiplayer PostThink sets NODRAW while the door flag is up and
	// never clears it; remember that this player's current hide is a door's.
	qboolean door_hidden;
	qboolean place_pending, place_regroup; // regroup: a story arrival, not a spawn
	double place_deadline, place_checked; // checked: the last frame that looked (time held in a scene is added back)
	vec3_t place_start;
	qboolean teleported;
	vec3_t teleport_from, teleport_to;
	double warp_after; // the next moment this player's warp key works
	// The last "went somewhere you can't follow" line this player was sent, said again while they stay cut off
	// (1.1.8, M3 2c): owner = slot + 1 (0: none) and userid, repeats left, when next.
	int cutoff_owner, cutoff_userid, cutoff_left;
	double cutoff_next;
	const char *cutoff_kind;
	edict_t *door_through; // the last inter_door this player went through in this map (1.1.9, S17)
	int door_through_serial;
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
	// Where each door companion stood when the door began, and in which section of a merged map (1.1.8, E1 F5).
	vec3_t captured[MAX_CLIENTS];
	int section[MAX_CLIENTS];
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
// 1.1.9 (S17, M1 9): a door its opener went through, still there then, watched for a while (by the opener's slot).
#define COF_PARTY_DOOR_WATCH 30.0
static struct
{
	cof_party_identity_t owner;
	edict_t *door;
	int serial;
	char exit[64]; // what the door's target named
	double until;
	int through[MAX_CLIENTS]; // userids who went through it after the opener, or with them
} cof_party_watch[MAX_CLIENTS];
static cof_party_arrival_t cof_party_arrival;
static int cof_party_fresh[MAX_CLIENTS]; // userid of a new join awaiting cs_spawned
static int cof_party_regroup[MAX_CLIENTS]; // userid of an arrival still to be moved beside a partner
// 1.1.9 (I6): userid of a player a checkpoint load restored, whose gifts are still to be read (CoF_PartyRestoreGifts),
// and the generation a story level change arrived in (its restores are no checkpoint load).
static int cof_party_restored[MAX_CLIENTS];
static int cof_party_arrived_generation = -1;
static int cof_party_hidden[MAX_CLIENTS]; // userid still owed a door-hide undo after a map change
// Where each player came into this map (a level change's arrival) or section (a merged map's section move), for a
// respawn (SV_CoF_PartyRespawned); generation = svs.spawncount it was recorded in.
static struct
{
	int userid, generation;
	vec3_t origin;
} cof_party_entry[MAX_CLIENTS];
// The loaded map's cof_teleports by name, read once a map from its entity script (1.1.8, M3 4): walkin = only
// reusable trigger_multiples fire it (a per-player bounce, like trigger_teleport), so nobody is taken along.
static struct
{
	int generation; // svs.spawncount it was read in; -1: not read
	int count;
	char name[COF_PARTY_TELEPORT_NAMES][64];
	qboolean walkin[COF_PARTY_TELEPORT_NAMES];
	qboolean oneshot[COF_PARTY_TELEPORT_NAMES]; // 1.1.9 (P1): only a one-shot chain fires it (CoF_PartyReadOneShot)
} cof_party_teleports = { -1, 0, { { 0 } }, { 0 }, { 0 } };
// 1.1.9 (P3): the map's trigger_multiples that fire a cof_teleport (the "wrong way" bounces), read once a map.
#define COF_PARTY_BOUNCES 128
static struct
{
	int generation, count;
	int entity[COF_PARTY_BOUNCES], serial[COF_PARTY_BOUNCES];
} cof_party_bounces = { -1, 0, { 0 }, { 0 } };
// 1.2.3: svs.spawncount the map's own cof_teleports were last checked in (CoF_PartyRepairTeleports)
static int cof_party_repaired_generation = -1;
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
	cof_party_granted = 0;
	cof_party_withdrawn = 0;
	cof_party_clearers.generation = -1;
	memset( cof_party_shared, 0, sizeof( cof_party_shared ));
	for( i = 0; i < (int)ARRAYSIZE( cof_party_starters ); i++ ) CoF_PartyShareWeapon( cof_party_starters[i] );
	memset( cof_party_players, 0, sizeof( cof_party_players ));
	memset( cof_party_doors, 0, sizeof( cof_party_doors ));
	memset( cof_party_watch, 0, sizeof( cof_party_watch ));
	memset( cof_party_fresh, 0, sizeof( cof_party_fresh ));
	memset( cof_party_regroup, 0, sizeof( cof_party_regroup ));
	memset( cof_party_restored, 0, sizeof( cof_party_restored ));
	memset( cof_party_hidden, 0, sizeof( cof_party_hidden ));
	cof_party_generation = -1;
	cof_party_teleports.generation = -1;
	cof_party_bounces.generation = -1;
	cof_party_repaired_generation = -1;
	cof_party_inventory_time = 0;
}

// 1.1.9 (S6, K8): music is per player in the game. A door plays its music to its opener only
// (CoF_InterDoor.cpp:351-375, 536-545), so a partner the party took through kept the old track (chase music in a
// quiet room, or silence); a respawn clears it (Spawn) and replays sv_coop_music, the track of whatever map last
// set it. Such a player hears what from hears now: the same track, loop and fade-in, sent once. Nothing when they
// hear it already (restarting it would be heard).
static void CoF_PartyCopyMusic( sv_client_t *to, sv_client_t *from, const char *why )
{
	byte *dst, *src;
	string_t track, mine;
	int update = 1;
	float fade;
	if( !to || !from || to == from || ( to->flags & FCL_FAKECLIENT ) ||
		!( dst = SV_CoF_StoryEntityPrivate( to->edict, COF_PARTY_MP3_SIZE )) ||
		!( src = SV_CoF_StoryEntityPrivate( from->edict, COF_PARTY_MP3_SIZE ))) return;
	memcpy( &track, src + COF_PARTY_MP3, sizeof( track ));
	memcpy( &mine, dst + COF_PARTY_MP3, sizeof( mine ));
	if( track == mine || !Q_strcmp( track ? SV_GetString( track ) : "", mine ? SV_GetString( mine ) : "" )) return;
	memcpy( dst + COF_PARTY_MP3, &track, sizeof( track ));
	dst[COF_PARTY_MP3_LOOP] = src[COF_PARTY_MP3_LOOP];
	memcpy( dst + COF_PARTY_MP3_FADE_IN, src + COF_PARTY_MP3_FADE_IN, sizeof( fade ));
	if( !track )
	{
		fade = 1.0f; // stopped: faded out over a second, as a door into a silent room does (PlayerStopMP3( 1 ))
		memcpy( dst + COF_PARTY_MP3_FADE_OUT, &fade, sizeof( fade ));
	}
	memcpy( dst + COF_PARTY_MP3_UPDATE, &update, sizeof( update ));
	Con_Printf( "cofstory phase=music-shared userid=%d from=%d track=%s reason=%s\n", to->userid, from->userid,
		track ? SV_GetString( track ) : "", why );
}

// A player's track is the cvar's own text (MP PlayerSpawn plays MAKE_STRING( CVAR_GET_STRING( "sv_coop_music" ))):
// changing the cvar would free it under them. The restore of a story level change gives every player a copy.
static qboolean CoF_PartyMusicOwned( const char *text )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		byte *pv = svs.clients[i].state >= cs_connected ? SV_CoF_StoryEntityPrivate( svs.clients[i].edict, COF_PARTY_MP3_SIZE ) : NULL;
		string_t track;
		if( !pv ) continue;
		memcpy( &track, pv + COF_PARTY_MP3, sizeof( track ));
		if( track && SV_GetString( track ) == text ) return true;
	}
	return false;
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

static void CoF_PartyEntered( sv_client_t *cl )
{
	int i = CoF_PartySlot( cl );
	if( i < 0 ) return;
	cof_party_entry[i].userid = cl->userid;
	cof_party_entry[i].generation = svs.spawncount;
	VectorCopy( cl->edict->v.origin, cof_party_entry[i].origin );
}

static void CoF_PartyCheckGeneration( void )
{
	int i, hidden[MAX_CLIENTS], shared_count = cof_party_shared_count;
	unsigned int withdrawn = cof_party_withdrawn;
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
	cof_party_withdrawn = withdrawn;
	cof_party_generation = svs.spawncount;
	CoF_PartyFindGifts();
}

static void CoF_PartySchedulePlacement( sv_client_t *cl, cof_party_player_t *p, qboolean regroup )
{
	p->place_pending = true;
	p->place_regroup = regroup;
	p->place_deadline = sv.time + COF_PARTY_JOIN_WAIT;
	p->place_checked = sv.time;
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
	// 1.1.9 (I6): restored in place, and no story level change arrived in this map: a checkpoint load
	cof_party_restored[i] = !fresh && cof_party_arrived_generation != svs.spawncount ? cl->userid : 0;
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
		if( fresh ) SV_CoF_ArrivalLanded( cl ); // set down on the spawn point (1.1.9, X5: see CoF_PartyTrackLife)
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
				// 1.1.9 (X5, CH common 8): a fresh join (a chapter start, New game, a late joiner) appears on the map's
				// spawn point, which can stand in the exit back: like an arrival or a respawn set down there, a touch
				// of that exit does not count until they have stepped out of it (sv_cof_arrival.c).
				if( cof_party_fresh[i] == cl->userid ) SV_CoF_ArrivalLanded( cl );
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
			// The respawn's own SetOrigin onto the spawn spot is no teleport to follow (CoF_PartyTeleports no longer
			// skips a player whose placement is pending, 1.1.8).
			p->teleported = false;
			// A respawn may hand back what the death took; the recovery usually has.
			p->seen_items = 0;
			cof_party_seen[i].userid = cl->userid;
			cof_party_seen[i].seen = cof_party_seen[i].held = 0;
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

// 1.1.9 (S8, M1 9): the map's cof_clearitems taking a weapon by classname. c_start's `removecamera` (fired at the
// start of its opening, multiscene1) takes the camera c_nightmare handed out, from its activator only
// (effects.cpp, CCofClearItems::Use): the partners kept theirs, and the activator got it back from them after a
// death. Single player has no camera from there on until it finds one.
static void CoF_PartyReadClearers( void )
{
	int i;
	cof_party_clearers.generation = svs.spawncount;
	cof_party_clearers.count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_party_clearers.count < COF_PARTY_CLEARERS; i++ )
	{
		edict_t *clear = SV_EdictNum( i );
		if( !SV_IsValidEdict( clear ) || Q_strcmp( SV_GetString( clear->v.classname ), "cof_clearitems" ) ||
			Q_strncmp( SV_GetString( clear->v.message ), "weapon_", 7 )) continue;
		cof_party_clearers.entity[cof_party_clearers.count] = i;
		cof_party_clearers.serial[cof_party_clearers.count] = clear->serialnumber;
		cof_party_clearers.count++;
	}
}

// The live cof_clearitems of this map that takes shared weapon j, or NULL.
static edict_t *CoF_PartyClearer( int j )
{
	int k;
	if( cof_party_clearers.generation != svs.spawncount ) CoF_PartyReadClearers();
	for( k = 0; k < cof_party_clearers.count; k++ )
	{
		edict_t *clear = SV_EdictNum( cof_party_clearers.entity[k] );
		if( SV_IsValidEdict( clear ) && clear->serialnumber == cof_party_clearers.serial[k] && !( clear->v.flags & FL_KILLME ) &&
			!Q_strcmp( SV_GetString( clear->v.message ), cof_party_shared[j].classname )) return clear;
	}
	return NULL;
}

// Shared weapons a player lost, alive, in a map with a cof_clearitems taking them: that cof_clearitems took them.
// They are taken from everyone else too, by the same entity's own Use with each of them as its activator, and no
// longer handed out this session (the party's shared list keeps the entry: its bits index every player's record).
static void CoF_PartyWithdraw( unsigned int lost, int loser, const unsigned int *carried )
{
	int j, k;
	for( j = 0; j < cof_party_shared_count; j++ )
	{
		unsigned int bit = 1U << j;
		edict_t *clear;
		if( !( lost & bit ) || ( cof_party_withdrawn & bit ) || !( clear = CoF_PartyClearer( j ))) continue;
		cof_party_withdrawn |= bit;
		Con_Printf( "cofstory phase=gift-withdrawn class=%s userid=%d\n", cof_party_shared[j].classname, loser );
		for( k = 0; svgame.dllFuncs.pfnUse && k < svs.maxclients && k < MAX_CLIENTS; k++ )
		{
			sv_client_t *other = &svs.clients[k];
			if( !( carried[k] & bit ) || !CoF_PartyPlayer( other ) || !SV_IsValidEdict( clear ) || ( clear->v.flags & FL_KILLME )) continue;
			svgame.dllFuncs.pfnUse( clear, other->edict );
			Con_Printf( "cofstory phase=gift-cleared userid=%d class=%s\n", other->userid, cof_party_shared[j].classname );
		}
	}
}

// 1.1.9 (I6, M1 9): the story's scripted weapon gifts (cof_giveitems with a classname: c_apartment1's glock,
// c_sewer2's shotgun, c_asylumb's flashlight). The party learns them on the gift's own map (CoF_PartyFindGifts) and
// keeps them for the session, but a checkpoint is no part of that: after Continue the list held only the starters,
// so a new joiner or a respawn got no glock or shotgun. A player a checkpoint load restored who carries one shares it
// again. Not c_nightmare's camera: c_start takes it back (CoF_PartyWithdraw), and its own map finds it again.
static const char *cof_party_gifts[] = { "weapon_glock", "weapon_shotgun", "weapon_flashlight" };

static void CoF_PartyRestoreGifts( sv_client_t *cl )
{
	byte *pv = CoF_PartyPlayer( cl );
	char descriptor[64];
	int g, slot, k;
	for( g = 0; pv && g < (int)ARRAYSIZE( cof_party_gifts ); g++ )
	{
		Q_snprintf( descriptor, sizeof( descriptor ), "inventoryitems/weapons/%s.txt", cof_party_gifts[g] );
		for( k = 0; k < cof_party_shared_count && Q_strcmp( cof_party_shared[k].classname, cof_party_gifts[g] ); k++ );
		if( k < cof_party_shared_count ) continue; // shared already
		for( slot = 1; slot < COF_PARTY_SLOTS; slot++ )
		{
			string_t name;
			memcpy( &name, pv + COF_PARTY_INVENTORY + slot * sizeof( name ), sizeof( name ));
			if( !name || Q_strcmp( SV_GetString( name ), descriptor )) continue;
			Con_Printf( "cofstory phase=gift-restored userid=%d class=%s\n", cl->userid, cof_party_gifts[g] );
			CoF_PartyShareWeapon( cof_party_gifts[g] );
			break;
		}
	}
}

static void CoF_PartyItems( void )
{
	unsigned int carried[MAX_CLIENTS] = { 0 }, party_items = 0, lost = 0;
	int i, j, loser = 0;
	if( sv.time < cof_party_inventory_time ) return;
	cof_party_inventory_time = sv.time + 0.5;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		qboolean alive;
		if( !CoF_PartySamePlayer( &p->identity, cl )) continue;
		alive = CoF_PartyPlayer( cl ) != NULL;
		if( alive && cof_party_restored[i] && cof_party_restored[i] == cl->userid )
		{
			CoF_PartyRestoreGifts( cl ); // 1.1.9 (I6): what a checkpoint load gave back, before this look counts it
			cof_party_restored[i] = 0;
		}
		carried[i] = CoF_PartyInventory( cl );
		// 1.1.9 (S8): what this player carried at the last look (in the last map, for the first look here) and no
		// longer does, alive all along: maybe taken by a cof_clearitems (CoF_PartyWithdraw checks the map has one)
		for( j = 0; alive && cof_party_seen[i].userid == cl->userid && j < cof_party_shared_count; j++ )
			if(( cof_party_seen[i].held & ~carried[i] & ~cof_party_withdrawn & ( 1U << j )) && CoF_PartyClearer( j ))
			{
				lost |= 1U << j;
				loser = cl->userid;
			}
		party_items |= carried[i];
		p->seen_items |= carried[i];
		cof_party_seen[i].userid = cl->userid;
		cof_party_seen[i].seen = p->seen_items;
		cof_party_seen[i].held = alive ? carried[i] : 0;
	}
	if( lost ) CoF_PartyWithdraw( lost, loser, carried );
	party_items |= cof_party_granted;
	party_items &= ~cof_party_withdrawn; // 1.1.9 (S8)
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
			// 1.1.8 (E1 F6): a player who cannot take it (a full inventory) was asked, and logged, every 5 s for the
			// whole session: each refusal this map doubles the wait (up to a minute), and only the first is logged.
			p->retry_after[j] = sv.time + ( p->refused[j] >= 4 ? COF_PARTY_GIVE_RETRY_MAX : COF_PARTY_GIVE_RETRY * ( 1 << p->refused[j] ));
			accepted = SV_CoF_StoryGiveItem( cl, cof_party_shared[j].classname );
			if( CoF_PartyInventory( cl ) & bit )
			{
				p->seen_items |= bit;
				cof_party_seen[i].userid = cl->userid;
				cof_party_seen[i].seen = p->seen_items;
				Con_Printf( "cofstory phase=starter-shared userid=%d class=%s\n", cl->userid, cof_party_shared[j].classname );
			}
			else if( p->refused[j]++ == 0 )
				Con_Printf( "cofstory phase=starter-deferred userid=%d class=%s reason=%s\n", cl->userid, cof_party_shared[j].classname, accepted ? "inventory-unconfirmed" : "pickup-refused" );
			else if( p->refused[j] > 4 ) p->refused[j] = 4; // the wait stays at its longest
		}
	}
}

void SV_CoF_PartyGrant( const char *classname )
{
	int i;
	if( !SV_CoF_StoryCoopEnabled() || !classname ) return;
	CoF_PartyCheckGeneration(); // the grant is this map's: the map's first look must not wipe it afterwards
	CoF_PartyShareWeapon( classname );
	for( i = 0; i < cof_party_shared_count; i++ )
		if( !Q_strcmp( cof_party_shared[i].classname, classname ))
		{
			cof_party_granted |= 1U << i;
			Con_Printf( "cofstory phase=chapter-kit class=%s\n", classname );
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
		// Match the DLL's first target-name result, not an arbitrary later exit. 1.1.9 (S15, M1 9): EndSequence moves
		// its opener to whatever the target names; c_subwaysick2's inter_door #34 names an info_teleport_destination
		// (`exitforever`), so its partner was never taken along.
		if(( Q_strcmp( SV_GetString( ent->v.classname ), "inter_door_exit" ) &&
			Q_strcmp( SV_GetString( ent->v.classname ), "info_teleport_destination" )) ||
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
				VectorCopy( svs.clients[j].edict->v.origin, event->captured[j] );
				event->section[j] = SV_CoF_PlayerSection( svs.clients[j].edict );
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

// 1.1.9 (P3, M3 F4 residual): the map's trigger_multiples whose target is a cof_teleport. They bounce whoever walks
// in (c_village3's teleportse1_1N beside each ending room's level change send the toucher back to the door), so a
// partner set down inside one is sent away alone by their first move.
static void CoF_PartyReadBounces( void )
{
	char names[COF_PARTY_TELEPORT_NAMES][64];
	int i, k, n = 0;
	cof_party_bounces.generation = svs.spawncount;
	cof_party_bounces.count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && n < COF_PARTY_TELEPORT_NAMES; i++ )
	{
		edict_t *tele = SV_EdictNum( i );
		const char *name;
		if( !SV_IsValidEdict( tele ) || Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" )) continue;
		name = SV_GetString( tele->v.targetname );
		if( name[0] && Q_strlen( name ) < sizeof( names[0] )) Q_strncpy( names[n++], name, sizeof( names[0] ));
	}
	for( i = svs.maxclients + 1; n && i < svgame.numEntities && cof_party_bounces.count < COF_PARTY_BOUNCES; i++ )
	{
		edict_t *trigger = SV_EdictNum( i );
		const char *target;
		if( !SV_IsValidEdict( trigger ) || Q_strcmp( SV_GetString( trigger->v.classname ), "trigger_multiple" )) continue;
		target = SV_GetString( trigger->v.target );
		for( k = 0; target[0] && k < n && Q_strcmp( names[k], target ); k++ );
		if( !target[0] || k == n ) continue;
		cof_party_bounces.entity[cof_party_bounces.count] = i;
		cof_party_bounces.serial[cof_party_bounces.count] = trigger->serialnumber;
		cof_party_bounces.count++;
	}
}

// ent's hull at this spot touches a live bounce trigger.
static qboolean CoF_PartyInBounce( const edict_t *ent, const vec3_t at )
{
	int i, k;
	if( cof_party_bounces.generation != svs.spawncount ) CoF_PartyReadBounces();
	for( i = 0; i < cof_party_bounces.count; i++ )
	{
		edict_t *trigger = SV_EdictNum( cof_party_bounces.entity[i] );
		if( !SV_IsValidEdict( trigger ) || trigger->serialnumber != cof_party_bounces.serial[i] ||
			( trigger->v.flags & FL_KILLME ) || trigger->v.solid != SOLID_TRIGGER ) continue;
		for( k = 0; k < 3; k++ ) // the engine pads a player's box by a unit for its touches
			if( at[k] + ent->v.mins[k] - 1.0f > trigger->v.absmax[k] || at[k] + ent->v.maxs[k] + 1.0f < trigger->v.absmin[k] ) break;
		if( k == 3 ) return true;
	}
	return false;
}

// One candidate spot beside center (feet height already applied): reachable
// from center without crossing a wall, free for the hull, over a floor, out of
// the bounce triggers (1.1.9, P3).
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
	if( tr.startsolid || tr.allsolid || !CoF_PartyVectorValid( point ) || CoF_PartyInBounce( ent, point )) return false;
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

// Where a player looks from, and which way: their eyes, or the camera they watch.
static void CoF_PartyView( edict_t *leader, vec3_t eye, float *yaw )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->edict != leader || !cl->pViewEntity || cl->pViewEntity == leader || !SV_IsValidEdict( cl->pViewEntity )) continue;
		VectorCopy( cl->pViewEntity->v.origin, eye );
		*yaw = cl->pViewEntity->v.angles[1];
		return;
	}
	VectorAdd( leader->v.origin, leader->v.view_ofs, eye );
	*yaw = leader->v.v_angle[1];
}

// How far off the view axis a spot is, in degrees (0 dead ahead, 180 behind).
static float CoF_PartyOffView( const vec3_t eye, float yaw, const vec3_t spot )
{
	float off = atan2f( spot[1] - eye[1], spot[0] - eye[0] ) * 57.2957795f - yaw;
	while( off > 180.0f ) off -= 360.0f;
	while( off < -180.0f ) off += 360.0f;
	return fabsf( off );
}

// The eye sees a body standing at spot (its head or its feet).
static qboolean CoF_PartySees( const vec3_t eye, const vec3_t spot, edict_t *ignore )
{
	vec3_t zero = { 0, 0, 0 }, end;
	trace_t tr;
	int i;
	for( i = 0; i < 2; i++ )
	{
		VectorCopy( spot, end );
		end[2] += i ? -28.0f : 28.0f;
		tr = SV_Move( eye, zero, zero, end, MOVE_NOMONSTERS, ignore, false );
		if( !tr.startsolid && tr.fraction >= 1.0f ) return true;
	}
	return false;
}

// The AI companion catching up (or coming out of a door after the leader: exit_at is
// where the game put them): a spot out of the player's sight, never on top of them.
// Behind the leader first (rank k starts k rows further back), then a ring round
// them; around exit_at first when given. Nothing out of sight: false, and the caller
// asks again; with anywhere, the least seen spot, never in the middle of the view.
// Facing the way the leader faces.
qboolean SV_CoF_PartyPlaceUnseen( sv_client_t *mover, edict_t *leader, const vec3_t exit_at, int rank, qboolean anywhere )
{
	static const float behind[][2] = { { -64, 0 }, { -64, 40 }, { -64, -40 }, { -96, 0 }, { -40, 48 }, { -40, -48 } };
	static const float ring_angle[] = { 180, 150, -150, 120, -120, 90, -90, 60, -60 };
	static const float ring_radius[] = { 64, 96, 128, 160 };
	const int behinds = (int)ARRAYSIZE( behind ), angles_n = (int)ARRAYSIZE( ring_angle );
	vec3_t eye, center, point, landing, best_spot, angles = { 0, 0, 0 };
	float yaw, view, c, s, best = -1.0f;
	byte *pv = CoF_PartyPlayer( mover );
	int pass, i;
	if( !pv || !SV_IsValidEdict( leader ) || !CoF_PartyVectorValid( leader->v.origin ) ||
		!CoF_PartyVectorValid( mover->edict->v.mins ) || !CoF_PartyVectorValid( mover->edict->v.maxs )) return false;
	if( exit_at && !CoF_PartyVectorValid( exit_at )) exit_at = NULL;
	CoF_PartyView( leader, eye, &view );
	yaw = leader->v.angles[1] / 57.2957795f;
	c = cosf( yaw ); s = sinf( yaw );
	angles[1] = leader->v.angles[1];
	for( pass = exit_at ? 0 : 1; pass < 2; pass++ )
	{
		VectorCopy( pass ? leader->v.origin : exit_at, center );
		for( i = 0; i < behinds + angles_n * (int)ARRAYSIZE( ring_radius ); i++ )
		{
			float x, y, off;
			if( i < behinds ) { x = behind[i][0] - 40.0f * (float)rank; y = behind[i][1]; }
			else
			{
				int k = i - behinds;
				float a = ring_angle[k % angles_n] / 57.2957795f, r = ring_radius[k / angles_n];
				if( fabsf( ring_angle[k % angles_n] ) < 90.0f && !anywhere ) continue; // ahead of them: only as a last resort
				x = r * cosf( a ); y = r * sinf( a );
			}
			VectorCopy( center, point );
			point[0] += x * c - y * s;
			point[1] += x * s + y * c;
			if( !CoF_PartyLandingPoint( mover, leader, center, point, landing )) continue;
			if( CoF_PartyDistanceSquared( landing, leader->v.origin ) < COF_PARTY_UNSEEN_MIN * COF_PARTY_UNSEEN_MIN ) continue;
			off = CoF_PartyOffView( eye, view, landing );
			if( off >= COF_PARTY_UNSEEN_ANGLE || !CoF_PartySees( eye, landing, leader ))
				return CoF_PartyMoveEdict( mover->edict, pv, landing, angles );
			if( anywhere && off >= COF_PARTY_SEEN_LEAST && off > best )
			{
				best = off;
				VectorCopy( landing, best_spot );
			}
		}
	}
	if( best >= 0.0f ) return CoF_PartyMoveEdict( mover->edict, pv, best_spot, angles );
	return false;
}

qboolean SV_CoF_PartyInDoor( const sv_client_t *cl )
{
	byte *pv = cl ? CoF_PartyPlayer( (sv_client_t *)cl ) : NULL;
	return pv && pv[COF_PARTY_DOOR_FLAG];
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

// Tell player i how to follow the owner (slot). The first time (repeat 0) also arms the repeats of
// CoF_PartyCutOffRepeat. Not the one waiting at a held exit: their J leaves by it (1.1.8, M3 4).
static void CoF_PartyTellCutOff( int slot, int i, const char *kind, int repeat )
{
	sv_client_t *owner = &svs.clients[slot], *cl = &svs.clients[i];
	cof_party_player_t *p = &cof_party_players[i];
	int waiter = SV_CoF_ExitWaiter();
	char text[96];
	if( waiter && cl->userid == waiter ) return;
	Q_snprintf( text, sizeof( text ), "%s went somewhere you can't follow. Press J to join.", owner->name );
	SV_CoF_RetailProFont( cl, 1, text, true );
	if( repeat ) Con_Printf( "cofstory phase=cut-off userid=%d owner=%d kind=%s repeat=%d\n", cl->userid, owner->userid, kind, repeat );
	else
	{
		Con_Printf( "cofstory phase=cut-off userid=%d owner=%d kind=%s\n", cl->userid, owner->userid, kind );
		p->cutoff_owner = slot + 1;
		p->cutoff_userid = owner->userid;
		p->cutoff_kind = kind;
		p->cutoff_left = COF_PARTY_CUTOFF_REPEATS;
	}
	p->cutoff_next = sv.time + COF_PARTY_CUTOFF_EVERY;
}

// The owner went where the others cannot walk after them. Tell every living
// player who was not taken along and is not already there how to follow.
static void CoF_PartyCutOff( int slot, const cof_party_door_t *event, const char *kind )
{
	sv_client_t *owner = &svs.clients[slot];
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( i == slot || event->pending[i] || ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl )) continue;
		if( CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
		CoF_PartyTellCutOff( slot, i, kind, 0 );
	}
}

// 1.1.8 (M3 2c): the line came once, often while its reader was frozen in a cutscene, where J is refused, and was
// gone by the time they could act on it. Say it again every COF_PARTY_CUTOFF_EVERY seconds, a few times at most,
// once they can press J, for as long as they and the owner live and they are not with the owner. An exit hold's
// own "Press J" line speaks meanwhile.
static void CoF_PartyCutOffRepeat( void )
{
	int i, slot;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i], *owner;
		cof_party_player_t *p = &cof_party_players[i];
		if( p->cutoff_left <= 0 || sv.time < p->cutoff_next ) continue;
		slot = p->cutoff_owner - 1;
		owner = slot >= 0 && slot < svs.maxclients && slot < MAX_CLIENTS && slot != i ? &svs.clients[slot] : NULL;
		if( !CoF_PartySamePlayer( &p->identity, cl ) || !CoF_PartyPlayer( cl ) || !owner ||
			owner->userid != p->cutoff_userid || !CoF_PartyPlayer( owner ) ||
			CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR )
		{
			p->cutoff_left = 0; // followed, dead, or the owner is gone
			continue;
		}
		// not now: frozen, in a door or a camera; an exit hold; or about to be brought over by the owner's event
		if( !CoF_PartyPlayable( cl ) || SV_CoF_ExitWaiter() || ( cof_party_doors[slot].active && cof_party_doors[slot].pending[i] )) continue;
		p->cutoff_left--;
		CoF_PartyTellCutOff( slot, i, p->cutoff_kind ? p->cutoff_kind : "door", COF_PARTY_CUTOFF_REPEATS - p->cutoff_left );
	}
}

// A door companion going through an inter_door of their own, not the one the event began with (1.1.8, E1 F5).
static qboolean CoF_PartyOwnDoor( sv_client_t *cl, const cof_party_door_t *event )
{
	byte *pv = CoF_PartyPlayer( cl );
	int i;
	if( !pv || !pv[COF_PARTY_DOOR_FLAG] ) return false;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *door = SV_EdictNum( i );
		if( SV_IsValidEdict( door ) && door != event->door && door->v.owner == cl->edict && !( door->v.flags & FL_KILLME ) &&
			!Q_strcmp( SV_GetString( door->v.classname ), "inter_door" )) return true;
	}
	return false;
}

// A companion this event leaves behind is told how to follow, as the others were (CoF_PartyCutOff), once nobody
// can take the way the owner went (1.1.8, E1 F5 and F7).
static void CoF_PartyLeftBehind( int slot, const cof_party_door_t *event, int i, const char *kind )
{
	sv_client_t *owner = &svs.clients[slot], *cl = &svs.clients[i];
	if( i == slot || !event->completed || ( !event->teleport && CoF_PartyDoorRemains( event )) ||
		!CoF_PartySamePlayer( &event->companions[i], cl ) || ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl ) ||
		CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) return;
	CoF_PartyTellCutOff( slot, i, kind, 0 );
}

// 1.1.9 (S17, M1 9): a door can be killed later by something past it: c_city2's apdoor1 pair, by a trigger_once 142
// units beyond. A partner who was not taken through with the opener and did not go through themselves is cut off
// then, with no word: the line a door gone at once gives (CoF_PartyCutOff) was never said. Each door an opener
// completes while it is still there is watched for COF_PARTY_DOOR_WATCH seconds, with who went through it.
static void CoF_PartyDoorMark( int watch, int userid )
{
	int i;
	for( i = 0; userid && i < MAX_CLIENTS && cof_party_watch[watch].through[i] && cof_party_watch[watch].through[i] != userid; i++ );
	if( userid && i < MAX_CLIENTS ) cof_party_watch[watch].through[i] = userid;
}

// Player slot i went through this door (its opener, or taken along).
static void CoF_PartyDoorThrough( edict_t *door, int serial, int i )
{
	int slot;
	cof_party_players[i].door_through = door;
	cof_party_players[i].door_through_serial = serial;
	for( slot = 0; door && slot < MAX_CLIENTS; slot++ )
		if( cof_party_watch[slot].door == door && cof_party_watch[slot].serial == serial ) CoF_PartyDoorMark( slot, svs.clients[i].userid );
}

static void CoF_PartyDoorWatch( int slot, const cof_party_door_t *event )
{
	const char *exit = SV_GetString( event->door->v.target );
	int i;
	memset( &cof_party_watch[slot], 0, sizeof( cof_party_watch[slot] ));
	if( !exit[0] || Q_strlen( exit ) >= sizeof( cof_party_watch[slot].exit )) return;
	cof_party_watch[slot].owner = event->owner;
	cof_party_watch[slot].door = event->door;
	cof_party_watch[slot].serial = event->door_serial;
	Q_strncpy( cof_party_watch[slot].exit, exit, sizeof( cof_party_watch[slot].exit ));
	cof_party_watch[slot].until = sv.time + COF_PARTY_DOOR_WATCH;
	// whoever went through it before the opener did
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( cof_party_players[i].door_through == event->door && cof_party_players[i].door_through_serial == event->door_serial &&
			CoF_PartySamePlayer( &cof_party_players[i].identity, &svs.clients[i] )) CoF_PartyDoorMark( slot, svs.clients[i].userid );
}

// A watched door is gone, and no other inter_door leads to its exit (a swap for an equivalent door is no cut-off):
// tell the living players who did not go through it and are not with the opener how to follow.
static void CoF_PartyDoorGone( void )
{
	int slot, i, j;
	for( slot = 0; slot < svs.maxclients && slot < MAX_CLIENTS; slot++ )
	{
		sv_client_t *owner = &svs.clients[slot];
		edict_t *door = cof_party_watch[slot].door;
		qboolean other = false;
		if( !door ) continue;
		if( sv.time > cof_party_watch[slot].until || !CoF_PartySamePlayer( &cof_party_watch[slot].owner, owner ) || !CoF_PartyPlayer( owner ))
		{
			cof_party_watch[slot].door = NULL;
			continue;
		}
		if( SV_IsValidEdict( door ) && door->serialnumber == cof_party_watch[slot].serial && !( door->v.flags & FL_KILLME ) &&
			!Q_strcmp( SV_GetString( door->v.classname ), "inter_door" )) continue;
		cof_party_watch[slot].door = NULL;
		for( i = svs.maxclients + 1; !other && i < svgame.numEntities; i++ )
		{
			edict_t *ent = SV_EdictNum( i );
			other = SV_IsValidEdict( ent ) && ent != door && !( ent->v.flags & FL_KILLME ) &&
				!Q_strcmp( SV_GetString( ent->v.classname ), "inter_door" ) && !Q_strcmp( SV_GetString( ent->v.target ), cof_party_watch[slot].exit );
		}
		if( other ) continue;
		for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		{
			sv_client_t *cl = &svs.clients[i];
			if( i == slot || ( cl->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( cl ) ||
				CoF_PartyDistanceSquared( cl->edict->v.origin, owner->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			for( j = 0; j < MAX_CLIENTS && cof_party_watch[slot].through[j] != cl->userid; j++ );
			if( j == MAX_CLIENTS ) CoF_PartyTellCutOff( slot, i, "door-gone", 0 );
		}
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
			// 1.1.8 (E1 F7): a companion still waiting (no room beside the exit, or held in a scene to the end) was
			// dropped without a word.
			for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
				if( event->pending[i] ) CoF_PartyLeftBehind( slot, event, i, kind );
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
			CoF_PartyDoorThrough( event->door, event->door_serial, slot ); // through a door another opened (1.1.9, S17)
			// A one-shot door is gone now: nobody else can take it.
			if( !CoF_PartyDoorRemains( event )) CoF_PartyCutOff( slot, event, "door" );
			else if( !event->ending ) CoF_PartyDoorWatch( slot, event ); // it may still be killed behind them (1.1.9, S17)
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
			// 1.1.8 (E1 F5): a door companion was moved to the opener's exit up to seconds later whatever they did
			// meanwhile. One who went their own way since the door began (a door of their own, another section of
			// a merged map, or walked off) stays where they are. The ending door holds its companions still.
			if( !event->teleport && !event->ending )
			{
				const char *reason = NULL;
				if( CoF_PartyOwnDoor( cl, event )) reason = "own-door";
				else if( SV_CoF_PlayerSection( cl->edict ) != event->section[i] ) reason = "section";
				else if( CoF_PartyDistanceSquared( cl->edict->v.origin, event->captured[i] ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) reason = "moved";
				if( reason )
				{
					Con_Printf( "cofstory phase=door-party-skipped userid=%d reason=%s\n", cl->userid, reason );
					event->pending[i] = false;
					CoF_PartyLeftBehind( slot, event, i, kind );
					continue;
				}
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
			cof_party_players[i].place_pending = false; // placed beside the owner: a teleport supersedes a placement (1.1.8)
			Con_Printf( "cofstory phase=%s-party-moved userid=%d owner=%d origin=%.3f,%.3f,%.3f\n", kind, cl->userid, owner->userid, landing[0], landing[1], landing[2] );
			if( !event->teleport )
			{
				CoF_PartyCopyMusic( cl, owner, "door" ); // the door's music (1.1.9, S6)
				CoF_PartyDoorThrough( event->door, event->door_serial, i ); // 1.1.9 (S17)
			}
			SV_CoF_CompanionMoved( cl ); // a companion bot starts afresh beside its person
		}
		if( !pending ) event->active = false;
	}
}

// 1.1.9 (P1, M1 F5 residual, M2 F7): which of the map's cof_teleports (the first n names read) only a one-shot chain
// fires: a trigger_once, or a trigger_multiple or func_button that waits -1 (gone, or pressed for good, after one
// use), directly or through multi_managers that only such chains fire, at any depth. Nothing can fire such a
// teleport again, so a partner left behind cannot follow through it. A killtarget naming a link stops the chain
// rather than firing it. Any other firer (another class, a reusable trigger, a trigger moving with a train that
// fires on whoever it hits), any other key naming a link, a cycle, or more names than the table keeps leaves the
// teleport out.
static void CoF_PartyReadOneShot( int n )
{
	static char names[COF_PARTY_CHAIN_NAMES][64];
	static int firers[COF_PARTY_CHAIN_NAMES][COF_PARTY_CHAIN_FIRERS], firer_count[COF_PARTY_CHAIN_NAMES];
	static qboolean fired[COF_PARTY_CHAIN_NAMES], bad[COF_PARTY_CHAIN_NAMES], ok[COF_PARTY_CHAIN_NAMES];
	static byte target_ref[COF_PARTY_CHAIN_NAMES], manager_ref[COF_PARTY_CHAIN_NAMES], other_ref[COF_PARTY_CHAIN_NAMES];
	char *cursor, key[256], value[1024], classname[64], targetname[64], stripped[256];
	qboolean once, moves, added, changed;
	int count = n, current, pass, k, d, m;
	for( k = 0; k < n; k++ ) Q_strncpy( names[k], cof_party_teleports.name[k], sizeof( names[k] ));
	for( pass = 0, added = true; added && pass < COF_PARTY_CHAIN_PASSES; pass++ )
	{
		// every pass reads every entity for every name known when it began; names it adds are read by the next
		added = false;
		memset( fired, 0, sizeof( fired ));
		memset( bad, 0, sizeof( bad ));
		memset( firer_count, 0, sizeof( firer_count ));
		cursor = sv.worldmodel->entities;
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] == '{' )
		{
			current = count;
			classname[0] = targetname[0] = 0;
			once = moves = false;
			memset( target_ref, 0, sizeof( target_ref ));
			memset( manager_ref, 0, sizeof( manager_ref ));
			memset( other_ref, 0, sizeof( other_ref ));
			while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
			{
				if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
				if( !Q_strcmp( key, "classname" ))
				{
					Q_strncpy( classname, value, sizeof( classname ));
					continue;
				}
				if( !Q_strcmp( key, "targetname" ))
				{
					if( Q_strlen( value ) < sizeof( targetname )) Q_strncpy( targetname, value, sizeof( targetname ));
					continue;
				}
				if( !Q_strcmp( key, "wait" ) && Q_atof( value ) < 0.0f ) once = true;
				else if( !Q_strcmp( key, "movewith" ) && value[0] ) moves = true;
				Q_strncpy( stripped, key, sizeof( stripped ));
				for( d = 0; stripped[d] && stripped[d] != '#'; d++ ); // a multi_manager's "name#2" fires "name"
				stripped[d] = 0;
				for( k = 0; k < current; k++ )
				{
					if( !Q_strcmp( value, names[k] ))
					{
						if( !Q_strcmp( key, "target" )) target_ref[k] = 1;
						else if( Q_strcmp( key, "killtarget" )) other_ref[k] = 1;
					}
					if( !Q_strcmp( stripped, names[k] )) manager_ref[k] = 1;
				}
			}
			for( k = 0; k < current; k++ )
			{
				if( other_ref[k] ) bad[k] = true;
				if( target_ref[k] )
				{
					if( !moves && ( !Q_strcmp( classname, "trigger_once" ) ||
						( once && ( !Q_strcmp( classname, "trigger_multiple" ) || !Q_strcmp( classname, "func_button" )))))
						fired[k] = true;
					else bad[k] = true;
				}
				if( !manager_ref[k] ) continue;
				if( Q_strcmp( classname, "multi_manager" ) || !targetname[0] )
				{
					bad[k] = true;
					continue;
				}
				for( m = 0; m < count && Q_strcmp( names[m], targetname ); m++ );
				if( m == count )
				{
					if( count == COF_PARTY_CHAIN_NAMES )
					{
						bad[k] = true;
						continue;
					}
					Q_strncpy( names[count++], targetname, sizeof( names[0] ));
					added = true;
				}
				if( firer_count[k] == COF_PARTY_CHAIN_FIRERS ) bad[k] = true;
				else firers[k][firer_count[k]++] = m;
			}
		}
	}
	if( added ) return; // deeper than the passes go: none counts as one-shot
	// a link is one-shot once every multi_manager firing it is (and it has a firer); a cycle never is
	memset( ok, 0, sizeof( ok ));
	do
	{
		changed = false;
		for( k = 0; k < count; k++ )
		{
			if( ok[k] || bad[k] || ( !fired[k] && !firer_count[k] )) continue;
			for( d = 0; d < firer_count[k] && ok[firers[k][d]]; d++ );
			if( d < firer_count[k] ) continue;
			ok[k] = changed = true;
		}
	} while( changed );
	for( k = 0; k < n; k++ ) cof_party_teleports.oneshot[k] = ok[k] && !cof_party_teleports.walkin[k];
}

// Read the loaded map's entity script (sv.worldmodel->entities: what spawned, a merged map's put together) for
// who fires each cof_teleport. One fired only by trigger_multiple targets that do not wait -1 is a walk-in bounce;
// any other reference (a multi_manager key, which may carry a #n suffix; a trigger_once, button or relay target;
// a killtarget or any other key naming it) keeps it scripted, as does a name the table cannot keep.
static void CoF_PartyReadTeleports( void )
{
	char *cursor, key[256], value[1024], classname[64], stripped[256];
	qboolean once, target_ref[COF_PARTY_TELEPORT_NAMES], other_ref[COF_PARTY_TELEPORT_NAMES], scripted[COF_PARTY_TELEPORT_NAMES];
	int k, n;
	memset( &cof_party_teleports, 0, sizeof( cof_party_teleports ));
	cof_party_teleports.generation = svs.spawncount;
	if( !sv.worldmodel || !sv.worldmodel->entities ) return;
	// first the names
	cursor = sv.worldmodel->entities;
	while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] == '{' )
	{
		char name[64];
		classname[0] = name[0] = 0;
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
			if( !Q_strcmp( key, "classname" )) Q_strncpy( classname, value, sizeof( classname ));
			else if( !Q_strcmp( key, "targetname" ) && Q_strlen( value ) < sizeof( name )) Q_strncpy( name, value, sizeof( name ));
		}
		if( Q_strcmp( classname, "cof_teleport" ) || !name[0] ) continue;
		for( k = 0; k < cof_party_teleports.count && Q_strcmp( cof_party_teleports.name[k], name ); k++ );
		if( k < cof_party_teleports.count || k == COF_PARTY_TELEPORT_NAMES ) continue;
		Q_strncpy( cof_party_teleports.name[k], name, sizeof( cof_party_teleports.name[k] ));
		cof_party_teleports.count++;
	}
	n = cof_party_teleports.count;
	if( !n ) return;
	memset( scripted, 0, sizeof( scripted ));
	// then everything that names them
	cursor = sv.worldmodel->entities;
	while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] == '{' )
	{
		classname[0] = 0;
		once = false;
		memset( target_ref, 0, sizeof( target_ref ));
		memset( other_ref, 0, sizeof( other_ref ));
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
			if( !Q_strcmp( key, "classname" )) Q_strncpy( classname, value, sizeof( classname ));
			else if( !Q_strcmp( key, "wait" ) && Q_atof( value ) < 0.0f ) once = true; // removes itself after firing
			if( !Q_strcmp( key, "targetname" )) continue; // sharing a name fires nothing
			Q_strncpy( stripped, key, sizeof( stripped ));
			for( k = 0; stripped[k] && stripped[k] != '#'; k++ ); // a multi_manager's "name#2" fires "name"
			stripped[k] = 0;
			for( k = 0; k < n; k++ )
			{
				if( !Q_strcmp( value, cof_party_teleports.name[k] ))
				{
					if( !Q_strcmp( key, "target" )) target_ref[k] = true;
					else other_ref[k] = true;
				}
				if( !Q_strcmp( stripped, cof_party_teleports.name[k] )) other_ref[k] = true;
			}
		}
		for( k = 0; k < n; k++ )
		{
			if( other_ref[k] || ( target_ref[k] && ( once || Q_strcmp( classname, "trigger_multiple" )))) scripted[k] = true;
			else if( target_ref[k] ) cof_party_teleports.walkin[k] = true;
		}
	}
	for( k = 0; k < n; k++ )
		if( scripted[k] ) cof_party_teleports.walkin[k] = false;
	CoF_PartyReadOneShot( n );
}

// A cof_teleport fired only by reusable trigger_multiples (1.1.8, M3 4).
static qboolean CoF_PartyWalkInTeleport( const char *name )
{
	int k;
	if( cof_party_teleports.generation != svs.spawncount ) CoF_PartyReadTeleports();
	for( k = 0; name[0] && k < cof_party_teleports.count; k++ )
		if( !Q_strcmp( cof_party_teleports.name[k], name )) return cof_party_teleports.walkin[k];
	return false;
}

// A cof_teleport only a one-shot chain fires (1.1.9, P1).
static qboolean CoF_PartyOneShotTeleport( const char *name )
{
	int k;
	if( cof_party_teleports.generation != svs.spawncount ) CoF_PartyReadTeleports();
	for( k = 0; name[0] && k < cof_party_teleports.count; k++ )
		if( !Q_strcmp( cof_party_teleports.name[k], name )) return cof_party_teleports.oneshot[k];
	return false;
}

// The player's hull at this spot stands in a live trigger_hurt that kills (1.1.8, M1 2 and 6): c_roofs' fall
// teleports its faller into one, and c_city's flower trap's stays on after the first.
static qboolean CoF_PartyDeadlySpot( const edict_t *player, const vec3_t at )
{
	int i, k;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *hurt = SV_EdictNum( i );
		if( !SV_IsValidEdict( hurt ) || ( hurt->v.flags & FL_KILLME ) || hurt->v.solid != SOLID_TRIGGER ||
			!( hurt->v.dmg >= COF_PARTY_DEADLY_HURT ) || Q_strcmp( SV_GetString( hurt->v.classname ), "trigger_hurt" )) continue;
		for( k = 0; k < 3; k++ )
			if( at[k] + player->v.mins[k] > hurt->v.absmax[k] || at[k] + player->v.maxs[k] < hurt->v.absmin[k] ) break;
		if( k == 3 ) return true;
	}
	return false;
}

// A live trigger_hurt that kills lies within margin of this point (1.1.9, P1): a one-shot teleport landing there may
// be a kill room (c_roofs' fall, c_subway2st3's train) even when the activator's own hull is not inside it.
static qboolean CoF_PartyDeadlyNear( const vec3_t at, float margin )
{
	int i, k;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *hurt = SV_EdictNum( i );
		if( !SV_IsValidEdict( hurt ) || ( hurt->v.flags & FL_KILLME ) || hurt->v.solid != SOLID_TRIGGER ||
			!( hurt->v.dmg >= COF_PARTY_DEADLY_HURT ) || Q_strcmp( SV_GetString( hurt->v.classname ), "trigger_hurt" )) continue;
		for( k = 0; k < 3; k++ )
			if( at[k] < hurt->v.absmin[k] - margin || at[k] > hurt->v.absmax[k] + margin ) break;
		if( k == 3 ) return true;
	}
	return false;
}

// The activator of a scripted teleport is in a scene (1.1.9, P1): watching a camera, or a partner in their section
// watches one with them or did until just now (SV_CoF_SceneWatching).
static qboolean CoF_PartyInScene( int slot, int section )
{
	sv_client_t *cl = &svs.clients[slot];
	int j;
	if( cl->pViewEntity && cl->pViewEntity != cl->edict ) return true;
	for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		if( j != slot && CoF_PartyPlayer( &svs.clients[j] ) && SV_CoF_PlayerSection( svs.clients[j].edict ) == section &&
			SV_CoF_SceneWatching( cl->userid, svs.clients[j].userid )) return true;
	return false;
}

// info_teleport_destination at the player's new spot that a cof_teleport (a
// scripted, activator-only move) targets. trigger_teleport and ladders are
// excluded: the partner can walk into those themselves; so is a cof_teleport
// only reusable trigger_multiples fire (1.1.8: *walkin set, the "wrong way"
// bounces beside exits). A boat_exit there is
// c_lake's boat setting its one rider on the far dock (CBoat::Disembark). The
// boat carries one player and stays on that side, so no partner left behind
// could cross: every partner comes along, wherever they are.
// *oneshot (1.1.9, P1): every cof_teleport sending players here is fired only by a one-shot chain.
static edict_t *CoF_PartyScriptedDestination( const vec3_t origin, qboolean *walkin, qboolean *oneshot )
{
	int i, j;
	*walkin = *oneshot = false;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *dest = SV_EdictNum( i );
		const char *name;
		qboolean dock, found = false, all = true;
		if( !SV_IsValidEdict( dest ) || ( dest->v.flags & FL_KILLME ) || !CoF_PartyVectorValid( dest->v.origin ) ||
			fabs( dest->v.origin[0] - origin[0] ) > 8.0f || fabs( dest->v.origin[1] - origin[1] ) > 8.0f ||
			fabs( dest->v.origin[2] - origin[2] ) > 96.0f ) continue;
		dock = !Q_strcmp( SV_GetString( dest->v.classname ), "boat_exit" );
		if( dock ) return dest;
		if( Q_strcmp( SV_GetString( dest->v.classname ), "info_teleport_destination" )) continue;
		name = SV_GetString( dest->v.targetname );
		if( !name[0] ) continue;
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *tele = SV_EdictNum( j );
			if( !SV_IsValidEdict( tele ) || ( tele->v.flags & FL_KILLME ) ||
				Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" ) ||
				Q_strcmp( SV_GetString( tele->v.target ), name )) continue;
			if( !CoF_PartyWalkInTeleport( SV_GetString( tele->v.targetname )))
			{
				found = true;
				if( !CoF_PartyOneShotTeleport( SV_GetString( tele->v.targetname ))) all = false;
				continue;
			}
			*walkin = true; // unless another cof_teleport sends players here by script
			all = false;
		}
		if( found )
		{
			*oneshot = all;
			return dest;
		}
	}
	return NULL;
}

static void CoF_PartyTeleports( void )
{
	int i, j, count, waiter;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_party_player_t *p = &cof_party_players[i];
		cof_party_door_t *event = &cof_party_doors[i];
		edict_t *dest;
		byte *pv;
		qboolean boat, walkin, oneshot, everyone, carried[MAX_CLIENTS];
		int section;
		if( !p->teleported ) continue;
		p->teleported = false;
		// A pending placement does not stop the follow (1.1.8, M1 F7: a scene teleport early in a fresh chapter or
		// soon after a respawn was not mirrored); the respawn's own SetOrigin is dropped in CoF_PartyTrackLife.
		if( !CoF_PartySamePlayer( &p->identity, cl ) || !( pv = CoF_PartyPlayer( cl )) ||
			pv[COF_PARTY_DOOR_FLAG] || ( event->active && !event->completed )) continue;
		if( CoF_PartyDistanceSquared( p->teleport_from, p->teleport_to ) < COF_PARTY_TELEPORT_MIN * COF_PARTY_TELEPORT_MIN ) continue;
		// The player may already have moved on this frame; match the landing spot.
		if( !( dest = CoF_PartyScriptedDestination( p->teleport_to, &walkin, &oneshot )))
		{
			if( walkin ) Con_Printf( "cofstory phase=teleport-own userid=%d reason=walk-in\n", cl->userid );
			continue;
		}
		if( CoF_PartyDeadlySpot( cl->edict, p->teleport_to ))
		{
			Con_Printf( "cofstory phase=teleport-own userid=%d reason=deadly\n", cl->userid );
			continue;
		}
		boat = !Q_strcmp( SV_GetString( dest->v.classname ), "boat_exit" );
		// The scene put the activator where the story wants them; a pending placement would move them back (1.1.8).
		p->place_pending = false;
		// 1.1.8 (M3 2b): partners still waiting to follow the activator's previous teleport (frozen in its scene)
		// come along to this one instead of being forgotten.
		for( j = 0; j < MAX_CLIENTS; j++ )
			carried[j] = j < svs.maxclients && event->active && event->teleport && event->pending[j] &&
				CoF_PartySamePlayer( &event->owner, cl ) && CoF_PartySamePlayer( &event->companions[j], &svs.clients[j] );
		CoF_PartyEndingRelease( event );
		memset( event, 0, sizeof( *event ));
		event->active = event->completed = event->teleport = true;
		event->owner = p->identity;
		CoF_PartyFeet( cl, cl->edict, event->destination );
		event->angles[1] = cl->edict->v.angles[1];
		VectorCopy( p->teleport_from, event->start );
		event->deadline = sv.time + COF_PARTY_TELEPORT_WAIT;
		waiter = SV_CoF_ExitWaiter();
		section = SV_CoF_PointSection( p->teleport_from );
		// 1.1.9 (P1, M1 F5 residual, M2 F7): a one-way move by a one-shot chain with no scene to watch (c_apartmentsick's
		// teleportusus after the boss, the c_trainride foot button, c_bridge's telemia before its camera starts) took
		// only a partner within 128 units; the rest had to press J. Nothing can fire it again, so every living partner
		// in the activator's section comes along, as off the boat. A short move, one into or beside a kill room, and
		// one inside a scene keep the rules below.
		everyone = !boat && oneshot && CoF_PartyDistanceSquared( p->teleport_from, p->teleport_to ) > COF_PARTY_ONEWAY_MIN * COF_PARTY_ONEWAY_MIN &&
			!CoF_PartyDeadlyNear( p->teleport_to, COF_PARTY_HURT_NEAR ) && !CoF_PartyInScene( i, section );
		for( j = count = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *other = &svs.clients[j];
			if( j == i || !CoF_PartyPlayer( other ) ||
				!CoF_PartySamePlayer( &cof_party_players[j].identity, other )) continue;
			// 1.1.8 (M3 4): the one waiting at a held exit stays there; moving them gives the exit up.
			if( waiter && other->userid == waiter ) continue;
			// A partner already beside the destination was part of the scene.
			if( CoF_PartyDistanceSquared( other->edict->v.origin, cl->edict->v.origin ) <= COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR ) continue;
			// Only a partner who stood beside the activator, or watched the
			// activator's cutscene with them, is taken along; off the boat, everyone.
			// Every camera holds every player, so in a merged map only one in the
			// activator's section watches with them (1.1.8, K 4).
			if( !boat && !carried[j] && !( everyone && SV_CoF_PlayerSection( other->edict ) == section ) &&
				CoF_PartyDistanceSquared( other->edict->v.origin, p->teleport_from ) > COF_PARTY_JOIN_NEAR * COF_PARTY_JOIN_NEAR &&
				!( SV_CoF_SceneWatching( cl->userid, other->userid ) && SV_CoF_PlayerSection( other->edict ) == section )) continue;
			event->companions[j] = cof_party_players[j].identity;
			event->pending[j] = true;
			count++;
		}
		Con_Printf( "cofstory phase=teleport-begin userid=%d target=%s companions=%d%s\n", cl->userid,
			SV_GetString( dest->v.targetname )[0] ? SV_GetString( dest->v.targetname ) : SV_GetString( dest->v.classname ), count,
			everyone ? " one-way=1" : "" );
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

// The placement of slot waits for a scene: the player is held (camera, freeze, door), or no partner is free and a
// living one is held (1.1.8, M1 F7).
static qboolean CoF_PartyPlaceHeld( int slot )
{
	int j;
	qboolean held = false;
	if( !CoF_PartyAvailable( &svs.clients[slot] )) return true;
	for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
	{
		if( j == slot || cof_party_players[j].place_pending || !CoF_PartyPlayer( &svs.clients[j] )) continue;
		if( CoF_PartyAvailable( &svs.clients[j] )) return false;
		held = true;
	}
	return held;
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
		if( !CoF_PartySamePlayer( &p->identity, cl ) || !CoF_PartyPlayer( cl ))
		{
			p->place_pending = false;
			continue;
		}
		// 1.1.8 (M1 F7): the wait ran out during long scenes (c_roofs' is six minutes), leaving a late joiner or
		// a respawn at the map's entry. Time held in a scene, theirs or the partners', does not count.
		if( sv.time > p->place_checked && CoF_PartyPlaceHeld( i )) p->place_deadline += sv.time - p->place_checked;
		p->place_checked = sv.time;
		if( sv.time > p->place_deadline )
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

// The section move's last resort: the destination landmark itself (it stands in the doorway it names), or a free
// spot beside it.
static qboolean CoF_PartySectionFallback( sv_client_t *cl, const vec3_t fallback )
{
	vec3_t feet, spot;
	if( !CoF_PartyMove( cl, fallback, NULL )) return false;
	if( !CoF_PartyInSolid( cl->edict )) return true;
	VectorCopy( fallback, feet );
	feet[2] += cl->edict->v.mins[2] - 1.0f; // CoF_PartyLanding adds -mins.z + 1 back
	return CoF_PartyLanding( cl, cl->edict, feet, spot ) && CoF_PartyMove( cl, spot, NULL );
}

// A merged map's section move (sv_cof_sections.c): cl to landing, its view kept. Inside something solid there, or
// on top of anchor, a free spot beside anchor's new place (or beside the landing when anchor is cl or NULL); then,
// with a fallback (the destination landmark), there or beside it: the landing can lie deep in a wall when the door
// was entered off its usual spot, and every spot beside a point inside a wall fails too. None: cl stays where it
// was. Half a second of black fading in, like the retail co-op door. True when moved.
qboolean SV_CoF_PartySectionMove( sv_client_t *cl, const vec3_t landing, sv_client_t *anchor, const float *fallback )
{
	edict_t *at;
	vec3_t old, feet, spot;
	if( !CoF_PartyPlayer( cl )) return false;
	VectorCopy( cl->edict->v.origin, old );
	if( !CoF_PartyMove( cl, landing, NULL )) return false;
	at = anchor && anchor != cl && SV_IsValidEdict( anchor->edict ) ? anchor->edict : cl->edict;
	if( CoF_PartyInSolid( cl->edict ) || ( at != cl->edict && CoF_PartyBoxesTouch( cl->edict, at )))
	{
		CoF_PartyFeet( cl, at, feet );
		if(( !CoF_PartyLanding( cl, at, feet, spot ) || !CoF_PartyMove( cl, spot, NULL )) &&
			( !fallback || !CoF_PartySectionFallback( cl, fallback )))
		{
			CoF_PartyMove( cl, old, NULL );
			return false;
		}
	}
	if( !( cl->flags & FCL_FAKECLIENT ))
		SV_CoF_RetailScreenFade( cl, 0.5f, 0.0f, 0, 0, 0, 0, 255 );
	CoF_PartyEntered( cl );
	SV_CoF_ArrivalLanded( cl ); // set down in an exit: it does not count until they step out
	return true;
}

// Dead as the game counts it: a respawn is a PreThink that begins dead and ends alive.
qboolean SV_CoF_PartyDead( edict_t *ent )
{
	return SV_IsValidEdict( ent ) && ( !( ent->v.health > 0 ) || ent->v.deadflag != DEAD_NO );
}

// 1.1.9 (P4, M3 F6a): what to land beside for this partner: c_ending4b's wheelchair while they ride it. The rider's
// body stays frozen (FL_FROZEN, FL_NOTARGET) where they sat down, since no story map has a wheelchair_hide_spot,
// while the chair, which the game creates with the rider as its owner (StartWheelchairMode, CBaseEntity::Create),
// carries their view about (CoF_WheelchairPlayer.cpp Embark). The body is left as it is: the game's PreThink sets
// the solidity of players every frame and a hide would travel in a checkpoint taken during the ride.
static edict_t *CoF_PartyStandIn( sv_client_t *partner )
{
	int i;
	if( !SV_CoF_RetailInWheelchair( partner )) return partner->edict;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *chair = SV_EdictNum( i );
		if( SV_IsValidEdict( chair ) && chair->v.owner == partner->edict && !( chair->v.flags & FL_KILLME ) &&
			CoF_PartyVectorValid( chair->v.origin ) && !Q_strcmp( SV_GetString( chair->v.classname ), "wheelchair" )) return chair;
	}
	return partner->edict;
}

// cl beside partner, as a section move does (a fade, the way in recorded): beside the wheelchair they ride (1.1.9, P4).
static qboolean CoF_PartyMoveBeside( sv_client_t *cl, sv_client_t *partner )
{
	edict_t *at = CoF_PartyStandIn( partner );
	vec3_t spot;
	if( at == partner->edict ) return SV_CoF_PartySectionMove( cl, partner->edict->v.origin, partner, NULL );
	return CoF_PartyLanding( cl, at, at->v.origin, spot ) && SV_CoF_PartySectionMove( cl, spot, NULL, NULL ); // a chair stands on the floor
}

// The game respawns a player on the landmark the map was entered by (EntSelectSpawnPoint, gpGlobals->startspot),
// and nearly every story landmark stands in the trigger of its exit back, which their first move fires (live
// 2026-10-19, cm_sewer3: the level change back crashed the game). Called after the PreThink that respawned them,
// before that move: back where they came into this map or section (one carried in dead: where a partner did). The
// placement beside a partner follows.
void SV_CoF_PartyRespawned( sv_client_t *cl )
{
	int i = CoF_PartySlot( cl ), j, k, tried = 0;
	sv_client_t *partner = NULL;
	float best = 0.0f, d;
	byte *opv;
	if( i < 0 || !SV_CoF_StoryCoopEnabled() || !CoF_PartyPlayer( cl )) return;
	// Their own way in first, then a partner's. 1.1.8 (E1 F9): the first can be solid by now (a door or wall
	// brush closed on it), so try every recorded way in, then beside a living partner.
	for( k = -1; k < svs.maxclients && k < MAX_CLIENTS; k++ )
	{
		j = k < 0 ? i : k;
		if(( k >= 0 && j == i ) || cof_party_entry[j].generation != svs.spawncount || !cof_party_entry[j].userid ||
			cof_party_entry[j].userid != svs.clients[j].userid ) continue;
		tried++;
		if( SV_CoF_PartySectionMove( cl, cof_party_entry[j].origin, NULL, NULL )) break;
	}
	if( k < svs.maxclients && k < MAX_CLIENTS )
		Con_Printf( "cofstory phase=respawn-moved userid=%d\n", cl->userid );
	else if( !tried )
		Con_Printf( "cofstory phase=respawn-kept userid=%d reason=no-entry\n", cl->userid );
	else
	{
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			sv_client_t *other = &svs.clients[j];
			if( j == i || !( opv = CoF_PartyPlayer( other )) || opv[COF_PARTY_DOOR_FLAG] ) continue;
			d = CoF_PartyDistanceSquared( cl->edict->v.origin, CoF_PartyStandIn( other )->v.origin );
			if( !partner || d < best ) { partner = other; best = d; }
		}
		if( partner && CoF_PartyMoveBeside( cl, partner ))
			Con_Printf( "cofstory phase=respawn-moved userid=%d beside=%d\n", cl->userid, partner->userid );
		else Con_Printf( "cofstory phase=respawn-kept userid=%d reason=no-room\n", cl->userid );
	}
	// 1.1.9 (S6, K8): the music the nearest partner hears, not silence or a stale sv_coop_music track
	for( j = 0, partner = NULL; j < svs.maxclients && j < MAX_CLIENTS; j++ )
	{
		sv_client_t *other = &svs.clients[j];
		if( j == i || ( other->flags & FCL_FAKECLIENT ) || !CoF_PartyPlayer( other )) continue;
		d = CoF_PartyDistanceSquared( cl->edict->v.origin, other->edict->v.origin );
		if( !partner || d < best ) { partner = other; best = d; }
	}
	if( partner ) CoF_PartyCopyMusic( cl, partner, "respawn" );
	// back where they came in, or on the start landmark: both stand in the exit back (180 of 195 story landmarks)
	SV_CoF_ArrivalLanded( cl );
}

// A standable spot for cl at a landmark, read as a standing player's origin: on it, else the first free one
// beside it (1.1.8, E1 F2).
static qboolean CoF_PartyLandmarkLanding( sv_client_t *cl, const vec3_t mark, vec3_t result )
{
	vec3_t point, feet;
	if( !CoF_PartyVectorValid( mark ) || !CoF_PartyVectorValid( cl->edict->v.mins ) || !CoF_PartyVectorValid( cl->edict->v.maxs )) return false;
	VectorCopy( mark, point );
	if( CoF_PartyLandingPoint( cl, cl->edict, mark, point, result )) return true;
	VectorCopy( mark, feet );
	feet[2] += cl->edict->v.mins[2] - 1.0f; // CoF_PartyLanding adds -mins.z + 1 back
	return CoF_PartyLanding( cl, cl->edict, feet, result );
}

static byte *CoF_PartyArrived( sv_client_t *cl, int userid )
{
	byte *pv;
	if( !userid || cl->userid != userid || !( pv = CoF_PartyLivingPlayer( cl->edict )) || pv[COF_PARTY_DOOR_FLAG] ) return NULL;
	return pv;
}

// 1.2.3: the black rooms of the doors between maps (sv_cof_doorroom.h).

// A live cof_teleport named name stands at origin (within two units: a merged map's sections move by whole units).
static qboolean CoF_PartyTeleportAt( const char *name, const vec3_t origin )
{
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *tele = SV_EdictNum( i );
		if( !SV_IsValidEdict( tele ) || ( tele->v.flags & FL_KILLME ) || Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" ) ||
			Q_strcmp( SV_GetString( tele->v.targetname ), name )) continue;
		if( CoF_PartyDistanceSquared( tele->v.origin, origin ) <= 4.0f ) return true;
	}
	return false;
}

// Something live in the loaded map is named name (skip aside).
static qboolean CoF_PartyNamed( const char *name, const edict_t *skip )
{
	int i;
	if( !name[0] ) return false;
	for( i = 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		if( ent != skip && SV_IsValidEdict( ent ) && !( ent->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( ent->v.targetname ), name )) return true;
	}
	return false;
}

// A live cof_teleport named name with somewhere to send its activator in this map.
static qboolean CoF_PartyLiveTeleport( const char *name )
{
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *tele = SV_EdictNum( i );
		if( SV_IsValidEdict( tele ) && !( tele->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( tele->v.classname ), "cof_teleport" ) &&
			!Q_strcmp( SV_GetString( tele->v.targetname ), name ) && CoF_PartyNamed( SV_GetString( tele->v.target ), tele )) return true;
	}
	return false;
}

// 1.2.3: the map's own cof_teleports put back where a level change took them away. A black room's teleport lies in its
// landmark's view, and the game lets it cross a level change with the party (CBaseEntity's FCAP_ACROSS_TRANSITION); a
// change into a merged map matched none of the connections the game had marked (the exit names the original map, the
// story transition the merged file), so it never came back: c_buscity's teleportbc2 stayed in c_city and the black
// room at c_buscity's landbc had no way out (host log 2026-10-21 22:13-22:16). SV_CoF_TeleportStaysHome keeps them
// from leaving now; this mends saves made before. No story map removes a cof_teleport (nothing names one in a
// killtarget), so one missing from the map's entity script went with a level change. Once a map.
static void CoF_PartyRepairTeleports( void )
{
	char *cursor, *block, key[256], value[1024], name[64], target[64];
	qboolean teleport;
	vec3_t origin;
	int repaired = 0;
	if( cof_party_repaired_generation == svs.spawncount ) return;
	cof_party_repaired_generation = svs.spawncount;
	if( !sv.worldmodel || !sv.worldmodel->entities ) return;
	cursor = sv.worldmodel->entities;
	while( cursor )
	{
		block = cursor;
		if(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) == NULL || key[0] != '{' ) break;
		teleport = false;
		name[0] = target[0] = 0;
		VectorClear( origin );
		while(( cursor = COM_ParseFile( cursor, key, sizeof( key ))) != NULL && key[0] != '}' )
		{
			if(( cursor = COM_ParseFile( cursor, value, sizeof( value ))) == NULL ) break;
			if( !Q_strcmp( key, "classname" )) teleport = !Q_strcmp( value, "cof_teleport" );
			else if( !Q_strcmp( key, "targetname" )) Q_strncpy( name, value, sizeof( name ));
			else if( !Q_strcmp( key, "target" )) Q_strncpy( target, value, sizeof( target ));
			else if( !Q_strcmp( key, "origin" )) Q_atov( origin, value, 3 );
		}
		if( !cursor ) break;
		// one with nowhere to go in this map is one that crosses, as in single player (CoF_DoorRoomTeleportStays)
		if( !teleport || !name[0] || CoF_PartyTeleportAt( name, origin ) || !CoF_PartyNamed( target, NULL )) continue;
		if( SV_CoF_SpawnMapBlock( block ) == NULL ) continue;
		repaired++;
		Con_Printf( "cofstory phase=teleport-restored name=%s target=%s origin=%.0f,%.0f,%.0f\n", name, target, origin[0], origin[1], origin[2] );
	}
	if( repaired ) cof_party_bounces.generation = -1; // read again with them in
}

// 1.2.3: whether this entity stays in its map across a story level change (sv_save.c clears the game's marks on it).
qboolean SV_CoF_TeleportStaysHome( edict_t *ent )
{
	const char *target;
	if( !SV_CoF_StoryCoopEnabled() || !SV_IsValidEdict( ent ) || Q_strcmp( SV_GetString( ent->v.classname ), "cof_teleport" )) return false;
	target = SV_GetString( ent->v.target );
	return CoF_DoorRoomTeleportStays( true, target[0] != 0, target[0] && CoF_PartyNamed( target, ent ));
}

// The live walk-in trigger of the black room whose landmark is at mark, or NULL: a trigger_multiple that fires a
// walk-in cof_teleport present in the map, its box within COF_DOORROOM_WALKIN_NEAR of the landmark (the nearest).
static edict_t *CoF_PartyDoorRoomWalkIn( const vec3_t mark )
{
	edict_t *best = NULL;
	float best_d = 0.0f, d;
	int i;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *trigger = SV_EdictNum( i );
		const char *target;
		if( !SV_IsValidEdict( trigger ) || ( trigger->v.flags & FL_KILLME ) || trigger->v.solid != SOLID_TRIGGER ||
			Q_strcmp( SV_GetString( trigger->v.classname ), "trigger_multiple" ) ||
			!CoF_DoorRoomWalkInNear( mark, trigger->v.absmin, trigger->v.absmax )) continue;
		target = SV_GetString( trigger->v.target );
		if( !target[0] || !CoF_PartyWalkInTeleport( target ) || !CoF_PartyLiveTeleport( target )) continue;
		d = CoF_DoorRoomBoxDistance2( mark, trigger->v.absmin, trigger->v.absmax );
		if( !best || d < best_d )
		{
			best = trigger;
			best_d = d;
		}
	}
	return best;
}

// A spot for cl well inside the walk-in, on its floor, in the landmark's room (no wall between). Players may share it,
// as on a shared warp: retail PreThink lets overlapping players pass through each other, and the walk-in sends each
// away at their first move.
static qboolean CoF_PartyDoorRoomLanding( sv_client_t *cl, const vec3_t mark, edict_t *walkin, int which, vec3_t result )
{
	edict_t *ent = cl->edict;
	vec3_t point, down, zero = { 0, 0, 0 };
	trace_t tr;
	if( !CoF_PartyVectorValid( mark ) || !CoF_PartyVectorValid( ent->v.mins ) || !CoF_PartyVectorValid( ent->v.maxs ) ||
		!CoF_DoorRoomSpot( mark, walkin->v.absmin, walkin->v.absmax, ent->v.mins, ent->v.maxs, which, point )) return false;
	tr = SV_Move( mark, zero, zero, point, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) return false;
	VectorCopy( point, down );
	down[2] -= 256.0f;
	tr = SV_Move( point, ent->v.mins, ent->v.maxs, down, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.allsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.7f ) return false;
	VectorCopy( tr.endpos, point );
	point[2] += 1.0f;
	tr = SV_Move( point, ent->v.mins, ent->v.maxs, point, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.allsolid || !CoF_DoorRoomInside( point, ent->v.mins, ent->v.maxs, walkin->v.absmin, walkin->v.absmax )) return false;
	VectorCopy( point, result );
	return true;
}

// 1.2.3: an arrival by a door's black room goes on through it, as in single player. The retail arrival stands in the
// room's walk-in at once; one set down elsewhere in the dark room (beside the leader, kept out of the bounce triggers
// by the placement; on the landmark after everyone came in inside solid, as when c_city's taller room let a waiter
// fall to its floor during the hold) stood between the walk-in and the level change back, and a step into that took
// the party back to the map it had left (host log 2026-10-21 22:13-22:16, c_buscity's landbc). Everyone carried into
// that room is set down in its walk-in, which sends each on at their first move.
static void CoF_PartyDoorRoomArrive( const cof_party_arrival_t *arrival )
{
	edict_t *walkin;
	vec3_t mark, landing, zero = { 0, 0, 0 };
	int i, which;
	if( !sv.startspot[0] || !SV_CoF_LiveLandmark( sv.startspot, mark ) || ( walkin = CoF_PartyDoorRoomWalkIn( mark )) == NULL ) return;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *ent = cl->edict;
		byte *pv = CoF_PartyArrived( cl, arrival->userid[i] );
		trace_t tr;
		if( !pv || CoF_DoorRoomInside( ent->v.origin, ent->v.mins, ent->v.maxs, walkin->v.absmin, walkin->v.absmax ) ||
			CoF_PartyDistanceSquared( ent->v.origin, mark ) > COF_DOORROOM_ROOM * COF_DOORROOM_ROOM ) continue;
		tr = SV_Move( mark, zero, zero, ent->v.origin, MOVE_NOMONSTERS, ent, false );
		if( tr.startsolid || tr.allsolid || tr.fraction < 1.0f ) continue; // not in that room
		for( which = 0; which < 2; which++ )
		{
			if( !CoF_PartyDoorRoomLanding( cl, mark, walkin, which, landing ) || !CoF_PartyMoveEdict( ent, pv, landing, NULL )) continue;
			Con_Printf( "cofstory phase=arrival-walk-in userid=%d trigger=%d origin=%.3f,%.3f,%.3f\n", cl->userid, NUM_FOR_EDICT( walkin ), landing[0], landing[1], landing[2] );
			break;
		}
	}
}

// Called by the restore, after every carried player exists in the new map and
// before any client runs a command there, so nobody falls or sticks first.
void SV_CoF_PartyTransitionArrive( unsigned int epoch )
{
	cof_party_arrival_t arrival = cof_party_arrival;
	convar_t *music;
	int i, anchor = -1;
	memset( &cof_party_arrival, 0, sizeof( cof_party_arrival ));
	if( !SV_CoF_StoryCoopEnabled() ) return;
	cof_party_arrived_generation = svs.spawncount; // 1.1.9 (I6): the players restored here are no checkpoint load
	// 1.1.9 (S6, K8): sv_coop_music is the track the game replays to a respawn (multiplay_gamerules.cpp:1570-1576);
	// only a trigger_mp3 or cof_killmp3 set it, so in the next map a respawn heard the last map's. The players' own
	// music travels with them (the restore replays it).
	if(( music = Cvar_FindVar( "sv_coop_music" )) != NULL && Q_strlen( music->string ) > 1 && !CoF_PartyMusicOwned( music->string ))
	{
		Con_Printf( "cofstory phase=music-cleared track=%s\n", music->string );
		Cvar_DirectSet( music, "" );
	}
	CoF_PartyCheckGeneration();
	CoF_PartyRepairTeleports(); // 1.2.3: before the placement reads the map's teleports and bounce triggers
	if( !arrival.leader || arrival.epoch != epoch ) return;
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
	// 1.1.8 (E1 F2): everyone came in inside solid (a change released far from its exit carries the whole party by
	// the landmark offset into the walls, where J fails too): the leader stands beside the destination landmark,
	// which marks the doorway the map expects them in, and everyone else beside the leader. One arrival out of solid
	// (falling down a shaft, as an exit can be taken) keeps things as they were.
	if( anchor < 0 || CoF_PartyInSolid( svs.clients[anchor].edict ))
	{
		int mover = anchor, outside = 0;
		vec3_t mark, landing;
		byte *pv;
		for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		{
			if( !CoF_PartyArrived( &svs.clients[i], arrival.userid[i] )) continue;
			if( mover < 0 ) mover = i;
			if( !CoF_PartyInSolid( svs.clients[i].edict )) outside++;
		}
		if( !outside && mover >= 0 && sv.startspot[0] && SV_CoF_LiveLandmark( sv.startspot, mark ) &&
			( pv = CoF_PartyArrived( &svs.clients[mover], arrival.userid[mover] )) != NULL &&
			CoF_PartyLandmarkLanding( &svs.clients[mover], mark, landing ) &&
			CoF_PartyMoveEdict( svs.clients[mover].edict, pv, landing, NULL ))
		{
			Con_Printf( "cofstory phase=arrival-landmark userid=%d origin=%.3f,%.3f,%.3f\n", svs.clients[mover].userid, landing[0], landing[1], landing[2] );
			anchor = mover;
			for( i = 0; i < MAX_CLIENTS; i++ ) arrival.stray[i] = true;
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
	CoF_PartyDoorRoomArrive( &arrival );
	// where each came in, for a respawn (through a door too, its flag still up)
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( arrival.userid[i] && svs.clients[i].userid == arrival.userid[i] &&
			CoF_PartyLivingPlayer( svs.clients[i].edict ) && !CoF_PartyInSolid( svs.clients[i].edict ))
			CoF_PartyEntered( &svs.clients[i] );
}

// The partner's own spot, feet on the partner's floor, for a room only one fits (c_nightmare's last, `teleea`,
// 64 units wide, holds the exit to c_start): retail PreThink lets overlapping players pass through each other.
// Not where the caller's hull meets the world (a partner crouched in a vent) or no floor is in reach (a ladder).
static qboolean CoF_PartySharedSpot( sv_client_t *cl, edict_t *at, vec3_t result )
{
	edict_t *ent = cl->edict;
	vec3_t down;
	trace_t tr;
	if( !CoF_PartyVectorValid( at->v.origin ) || !CoF_PartyVectorValid( at->v.mins ) ||
		!CoF_PartyVectorValid( ent->v.mins ) || !CoF_PartyVectorValid( ent->v.maxs )) return false;
	VectorCopy( at->v.origin, result );
	result[2] += at->v.mins[2] - ent->v.mins[2];
	tr = SV_Move( result, ent->v.mins, ent->v.maxs, result, MOVE_NOMONSTERS, ent, false );
	if( tr.startsolid || tr.allsolid ) return false;
	VectorCopy( result, down );
	down[2] -= 64.0f;
	tr = SV_Move( result, ent->v.mins, ent->v.maxs, down, MOVE_NOMONSTERS, ent, false );
	return !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f;
}

// A refusal starts the cooldown too, so a mashed key logs one line per cooldown.
static void CoF_PartyWarpRefused( sv_client_t *cl, int slot, const char *reason, const char *text )
{
	cof_party_players[slot].warp_after = sv.time + COF_PARTY_WARP_COOLDOWN;
	Con_Printf( "cofstory phase=warp-refused userid=%d reason=%s\n", cl->userid, reason );
	if( text ) SV_CoF_RetailProFont( cl, 1, text, false );
}

// cof_warp (bound to J): join the partner, wherever they are: beside them, or on their spot when nothing beside
// them is free.
static void CoF_PartyWarp( sv_client_t *cl )
{
	int i = CoF_PartySlot( cl ), j, waiter = SV_CoF_ExitWaiter();
	sv_client_t *partner = NULL;
	edict_t *at;
	qboolean shared = false;
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
		d = CoF_PartyDistanceSquared( cl->edict->v.origin, CoF_PartyStandIn( other )->v.origin );
		if( !partner || d < best ) { partner = other; best = d; }
	}
	if( !partner )
	{
		CoF_PartyWarpRefused( cl, i, "no-partner", "Nobody to join right now." );
		return;
	}
	// beside the partner, or the wheelchair they ride (1.1.9, P4); never on a chair's spot
	at = CoF_PartyStandIn( partner );
	if( at == partner->edict ) CoF_PartyFeet( cl, partner->edict, feet );
	else VectorCopy( at->v.origin, feet ); // a chair stands on the floor
	angles[1] = at->v.angles[1];
	if(( !CoF_PartyLanding( cl, at, feet, landing ) && ( at != partner->edict || !( shared = CoF_PartySharedSpot( cl, partner->edict, landing )))) ||
		!CoF_PartyMove( cl, landing, angles ))
	{
		Q_snprintf( text, sizeof( text ), "No room next to %s.", partner->name );
		CoF_PartyWarpRefused( cl, i, "no-room", text );
		return;
	}
	cof_party_players[i].warp_after = sv.time + COF_PARTY_WARP_COOLDOWN;
	cof_party_players[i].cutoff_left = 0; // they joined: no more "Press J" lines (1.1.8)
	// Black, fading in over half a second: the retail co-op door's blink. The
	// warp does not depend on it being seen.
	SV_CoF_RetailScreenFade( cl, 0.5f, 0.0f, 0, 0, 0, 0, 255 );
	Con_Printf( "cofstory phase=warp userid=%d partner=%d origin=%.3f,%.3f,%.3f%s\n", cl->userid, partner->userid, landing[0], landing[1], landing[2], shared ? " shared=1" : "" );
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
	CoF_PartyRepairTeleports(); // 1.2.3: a checkpoint or a new game's map too, once
	CoF_PartyTrackLife();
	CoF_PartyWatchDoors();
	CoF_PartyTeleports();
	CoF_PartyFinishDoors();
	CoF_PartyDoorGone();
	CoF_PartyCutOffRepeat();
	CoF_PartyDoorVisibility();
	CoF_PartyPlace();
	CoF_PartyItems();
}
