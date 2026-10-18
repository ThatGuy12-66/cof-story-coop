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
 *   padlock is the first player while its think runs.
 * - The c_ending4b wheelchair (CWheelchair::TakeDamage 10086EB0): a lethal hit keeps the rider at 1 hp, frozen and
 *   unarmed in a dead chair, and its death think opens the game over only in single player; the chair's own exit and
 *   cof_stopwheelchair both refuse a dead chair. As single player's game over, the party starts the checkpoint again:
 *   the rider is let out as the chair's Disembark would (10086200) and everyone alive dies, so the game's team wipe
 *   restarts the map. */
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
#define COF_PL_WHEELCHAIR 0x0390 // EHANDLE m_pWheelchair
#define COF_PL_HIDEHUD 0x2500 // int m_iHideHUD
#define COF_PL_SUICIDE 0x22E8 // float m_fNextSuicideTime (ClientKill)
#define COF_PL_ENABLE_CONTROL 0xDEFF0 // CBasePlayer::EnableControl( BOOL ), thiscall
#define COF_HIDEHUD_HEALTH 8
#define COF_CHAIR_SIZE 0x388
#define COF_CHAIR_DEAD 0x368 // bool m_bDead
#define COF_CHAIR_RIDER 0x384 // raw CBasePlayer *m_pPlayer
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

// A dead wheelchair still carrying its rider.
static void CoF_RescueWheelchair( void )
{
	int i, j;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *chair = SV_EdictNum( i ), *rider_ent = NULL;
		sv_client_t *rider = NULL;
		byte *chair_pv, *rider_pv;
		uint32_t carried, none = 0;
		static const byte out[3] = { 0xFF, 0xFF, 0 }; // Wheelchair( short -1, byte 0 ), as Disembark
		int hud;
		float never = 0.0f;
		if( !CoF_RescueClass( chair, "wheelchair" ) || !( chair_pv = SV_CoF_StoryEntityPrivate( chair, COF_CHAIR_SIZE ))) continue;
		memcpy( &carried, chair_pv + COF_CHAIR_RIDER, sizeof( carried ));
		if( !chair_pv[COF_CHAIR_DEAD] || !carried ) continue;
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS && !rider; j++ )
			if( svs.clients[j].state == cs_spawned && SV_IsValidEdict( svs.clients[j].edict ) &&
				(uint32_t)(uintptr_t)svs.clients[j].edict->pvPrivateData == carried )
				rider = &svs.clients[j];
		memcpy( chair_pv + COF_CHAIR_RIDER, &none, sizeof( none ));
		if( !rider || !( rider_pv = SV_CoF_StoryEntityPrivate( rider_ent = rider->edict, COF_PL_SIZE ))) continue;
		SV_CoF_SendUserMessage( rider_ent, "Wheelchair", out, sizeof( out ), true );
		SV_CoF_StoryCallMember( COF_PL_ENABLE_CONTROL, rider_pv, (void *)1 );
		memcpy( &hud, rider_pv + COF_PL_HIDEHUD, sizeof( hud ));
		hud &= ~COF_HIDEHUD_HEALTH;
		memcpy( rider_pv + COF_PL_HIDEHUD, &hud, sizeof( hud ));
		memset( rider_pv + COF_PL_WHEELCHAIR, 0, sizeof( cof_rescue_handle_t ));
		SV_CoF_SetPhysicsKey( rider_ent, "chr", "1" );
		Con_Printf( "cofstory phase=wheelchair-death-restart userid=%d\n", rider->userid );
		// everyone alive dies: the game's team wipe restarts the map
		for( j = 0; j < svs.maxclients && j < MAX_CLIENTS; j++ )
		{
			edict_t *player = svs.clients[j].edict;
			byte *pv;
			if( svs.clients[j].state != cs_spawned || !SV_IsValidEdict( player ) || !CoF_RescueAlive( player ) || !svgame.dllFuncs.pfnClientKill ) continue;
			if(( pv = SV_CoF_StoryEntityPrivate( player, COF_PL_SIZE )) != NULL )
				memcpy( pv + COF_PL_SUICIDE, &never, sizeof( never ));
			svgame.dllFuncs.pfnClientKill( player );
		}
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
	CoF_RescueWheelchair();
}
