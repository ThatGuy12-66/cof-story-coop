/* Cry of Fear story co-op: ways back from soft-locks the retail game makes with more than one player.
 * GPL-3.0-or-later. Research: docs/research/2026-10-18-cof-decompilation-survey.md section 2 (the launcher repo).
 *
 * - The chainsaw boss (c_apartmentsick, monster_bosschainsaw): any swing that hits someone makes the kill animation
 *   (third_person_interaction: victim EHANDLE +0x36C, the boss +0x374; HandleAnimEvent 10027B60) and the boss removes
 *   itself without dying, so its TriggerTarget (multikilled1: the wall and the teleport on) never fires. In single
 *   player the hit is game over; in co-op the survivors were left with no boss and no way on. Once the victim has
 *   died, while anyone lives, the animation goes and the map's own boss is spawned again where it stood.
 * - The c_start padlock (item_padlock): the dial turn that opens it runs in the solver's command, but the release 2 s
 *   later (Finished 100B60B0, a think) frees "the first player": the host, while a guest who solved it stayed frozen,
 *   black and in the padlock camera. Whoever's padlockrotate (their P+0x2350 EHANDLE m_pPadlock) last turned an open
 *   padlock is the first player while its think runs. */
#include "common.h"
#include "server.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_rescue.h"

#define COF_TPI_SIZE 0x37C
#define COF_TPI_VICTIM 0x36C // EHANDLE m_pPlayer
#define COF_TPI_MONSTER 0x374 // EHANDLE m_pMonster
#define COF_PL_SIZE 0x281C
#define COF_PL_PADLOCK 0x2350 // EHANDLE m_pPadlock: the padlock this player is turning
#define COF_RESCUE_BOSSES 4
#define COF_RESCUE_KILLS 4
#define COF_RESCUE_PADLOCKS 4
#define COF_RESCUE_AFTER_DEATH 1.0 // seconds after the victim died
#define COF_RESCUE_AT_MOST 15.0 // seconds after the grab, whatever the animation did

typedef struct
{
	edict_t *edict;
	int serial;
} cof_rescue_handle_t; // the retail EHANDLE on this 32-bit build

// Chainsaw bosses alive last frame: an animation's boss handle is matched against them (the boss is gone by then).
static struct { int index, serial; vec3_t angles; char targetname[64]; } cof_rescue_bosses[COF_RESCUE_BOSSES];
static int cof_rescue_boss_count;
static struct { int index, serial; double seen, victim_dead; vec3_t angles; char targetname[64]; qboolean active, told; } cof_rescue_kills[COF_RESCUE_KILLS];
// The last player to turn each padlock's dials.
static struct { int index, serial, userid; } cof_rescue_padlocks[COF_RESCUE_PADLOCKS];
static int cof_rescue_generation = -1;

void SV_CoF_RescueReset( void )
{
	cof_rescue_generation = -1;
	cof_rescue_boss_count = 0;
	memset( cof_rescue_bosses, 0, sizeof( cof_rescue_bosses ));
	memset( cof_rescue_kills, 0, sizeof( cof_rescue_kills ));
	memset( cof_rescue_padlocks, 0, sizeof( cof_rescue_padlocks ));
}

static qboolean CoF_RescueClass( const edict_t *ent, const char *classname )
{
	return SV_IsValidEdict( ent ) && !( ent->v.flags & FL_KILLME ) && !Q_strcmp( SV_GetString( ent->v.classname ), classname );
}

static qboolean CoF_RescueAlive( const edict_t *ent )
{
	return ent->v.health > 0 && ent->v.deadflag == DEAD_NO;
}

static qboolean CoF_RescueAnyoneAlive( void )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && SV_IsValidEdict( svs.clients[i].edict ) && CoF_RescueAlive( svs.clients[i].edict ))
			return true;
	return false;
}

static cof_rescue_handle_t CoF_RescueHandleIn( edict_t *ent, size_t size, int offset )
{
	cof_rescue_handle_t h = { NULL, 0 };
	byte *pv = SV_CoF_StoryEntityPrivate( ent, size );
	if( pv ) memcpy( &h, pv + offset, sizeof( h ));
	if( h.edict && ( h.edict < svgame.edicts || h.edict >= svgame.edicts + svgame.numEntities )) h.edict = NULL;
	return h;
}

static cof_rescue_handle_t CoF_RescueHandle( edict_t *ent, int offset )
{
	return CoF_RescueHandleIn( ent, COF_TPI_SIZE, offset );
}

static sv_client_t *CoF_RescueClientByUserid( int userid )
{
	int i;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && svs.clients[i].userid == userid && SV_IsValidEdict( svs.clients[i].edict ))
			return &svs.clients[i];
	return NULL;
}

// Observed before the game runs a client's command; never consumes it.
qboolean SV_CoF_RescueClientCommand( sv_client_t *cl )
{
	cof_rescue_handle_t lock;
	int i, slot = -1;
	if( !SV_CoF_StoryCoopEnabled() || !cl || Q_strcmp( Cmd_Argv( 0 ), "padlockrotate" ) || !SV_IsValidEdict( cl->edict )) return false;
	lock = CoF_RescueHandleIn( cl->edict, COF_PL_SIZE, COF_PL_PADLOCK );
	if( !lock.edict || !CoF_RescueClass( lock.edict, "item_padlock" ) || lock.edict->serialnumber != lock.serial ) return false;
	for( i = 0; i < COF_RESCUE_PADLOCKS; i++ )
	{
		if( cof_rescue_padlocks[i].index == NUM_FOR_EDICT( lock.edict ) && cof_rescue_padlocks[i].serial == lock.serial ) { slot = i; break; }
		if( slot < 0 && !cof_rescue_padlocks[i].index ) slot = i;
	}
	if( slot < 0 ) slot = 0;
	cof_rescue_padlocks[slot].index = NUM_FOR_EDICT( lock.edict );
	cof_rescue_padlocks[slot].serial = lock.serial;
	cof_rescue_padlocks[slot].userid = cl->userid;
	return false;
}

edict_t *SV_CoF_RescueThinkFirst( edict_t *thinker )
{
	int i;
	sv_client_t *solver;
	if( !thinker || !SV_CoF_StoryCoopEnabled() || !CoF_RescueClass( thinker, "item_padlock" ) || thinker->v.body != 1 ) return NULL;
	for( i = 0; i < COF_RESCUE_PADLOCKS; i++ )
	{
		if( cof_rescue_padlocks[i].index != NUM_FOR_EDICT( thinker ) || cof_rescue_padlocks[i].serial != thinker->serialnumber ) continue;
		solver = CoF_RescueClientByUserid( cof_rescue_padlocks[i].userid );
		return solver ? solver->edict : NULL;
	}
	return NULL;
}

// A new kill animation whose boss was a chainsaw boss alive last frame.
static void CoF_RescueNewKills( void )
{
	int i, j, k;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *kill = SV_EdictNum( i );
		cof_rescue_handle_t boss;
		if( !CoF_RescueClass( kill, "third_person_interaction" )) continue;
		for( k = 0; k < COF_RESCUE_KILLS; k++ )
			if( cof_rescue_kills[k].active && cof_rescue_kills[k].index == i && cof_rescue_kills[k].serial == kill->serialnumber ) break;
		if( k < COF_RESCUE_KILLS ) continue;
		boss = CoF_RescueHandle( kill, COF_TPI_MONSTER );
		if( !boss.edict ) continue;
		for( j = 0; j < cof_rescue_boss_count; j++ )
			if( cof_rescue_bosses[j].index == NUM_FOR_EDICT( boss.edict ) && cof_rescue_bosses[j].serial == boss.serial ) break;
		if( j == cof_rescue_boss_count ) continue;
		for( k = 0; k < COF_RESCUE_KILLS && cof_rescue_kills[k].active; k++ );
		if( k == COF_RESCUE_KILLS ) continue;
		memset( &cof_rescue_kills[k], 0, sizeof( cof_rescue_kills[k] ));
		cof_rescue_kills[k].active = true;
		cof_rescue_kills[k].index = i;
		cof_rescue_kills[k].serial = kill->serialnumber;
		cof_rescue_kills[k].seen = sv.time;
		VectorCopy( cof_rescue_bosses[j].angles, cof_rescue_kills[k].angles );
		Q_strncpy( cof_rescue_kills[k].targetname, cof_rescue_bosses[j].targetname, sizeof( cof_rescue_kills[k].targetname ));
		Con_Printf( "cofstory phase=boss-grab class=monster_bosschainsaw name=%s animation=%d\n", cof_rescue_kills[k].targetname, i );
	}
}

static void CoF_RescueRememberBosses( void )
{
	int i;
	cof_rescue_boss_count = 0;
	for( i = svs.maxclients + 1; i < svgame.numEntities && cof_rescue_boss_count < COF_RESCUE_BOSSES; i++ )
	{
		edict_t *boss = SV_EdictNum( i );
		if( !CoF_RescueClass( boss, "monster_bosschainsaw" )) continue;
		cof_rescue_bosses[cof_rescue_boss_count].index = i;
		cof_rescue_bosses[cof_rescue_boss_count].serial = boss->serialnumber;
		VectorCopy( boss->v.angles, cof_rescue_bosses[cof_rescue_boss_count].angles );
		Q_strncpy( cof_rescue_bosses[cof_rescue_boss_count].targetname, SV_GetString( boss->v.targetname ), sizeof( cof_rescue_bosses[0].targetname ));
		cof_rescue_boss_count++;
	}
}

static void CoF_RescueBossBack( void )
{
	int k;
	for( k = 0; k < COF_RESCUE_KILLS; k++ )
	{
		edict_t *kill, *boss;
		cof_rescue_handle_t victim;
		if( !cof_rescue_kills[k].active ) continue;
		kill = SV_EdictNum( cof_rescue_kills[k].index );
		victim = CoF_RescueHandle( kill, COF_TPI_VICTIM );
		if( !cof_rescue_kills[k].victim_dead && victim.edict && SV_IsValidEdict( victim.edict ) &&
			victim.edict->serialnumber == victim.serial && !CoF_RescueAlive( victim.edict ))
			cof_rescue_kills[k].victim_dead = sv.time;
		if(!( cof_rescue_kills[k].victim_dead && sv.time >= cof_rescue_kills[k].victim_dead + COF_RESCUE_AFTER_DEATH ) &&
			sv.time < cof_rescue_kills[k].seen + COF_RESCUE_AT_MOST )
			continue;
		if( !CoF_RescueAnyoneAlive( ))
		{
			// the team wipe starts the checkpoint again; a respawn before that brings the boss back here
			if( !cof_rescue_kills[k].told )
				Con_Printf( "cofstory phase=boss-respawn-skipped reason=nobody-alive\n" );
			cof_rescue_kills[k].told = true;
			continue;
		}
		boss = SV_CoF_SpawnMapEntity( "monster_bosschainsaw", cof_rescue_kills[k].targetname, kill->v.origin, cof_rescue_kills[k].angles );
		if( SV_IsValidEdict( kill ) && kill->serialnumber == cof_rescue_kills[k].serial )
			SetBits( kill->v.flags, FL_KILLME );
		Con_Printf( "cofstory phase=boss-respawned class=monster_bosschainsaw name=%s ok=%d\n", cof_rescue_kills[k].targetname, boss != NULL );
		cof_rescue_kills[k].active = false;
	}
}

void SV_CoF_RescueFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	if( cof_rescue_generation != svs.spawncount )
	{
		SV_CoF_RescueReset();
		cof_rescue_generation = svs.spawncount;
	}
	CoF_RescueNewKills();
	CoF_RescueRememberBosses();
	CoF_RescueBossBack();
}
