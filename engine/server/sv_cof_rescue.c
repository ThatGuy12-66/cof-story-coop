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
 *   restarts the map.
 * - The crazybitch (monster_crazybitch MonsterThink 10055860) picks her moment from her enemy, then makes
 *   INDEXENT( 1 ), the host, turn his gun on himself wherever he is; an AI companion she fights set it off, and its
 *   own struggle never ran. While she thinks, edict 1 is her enemy when that is a living player.
 * - Bosses bound to "the first player": the c_roofs2 boss looks for it every think and attacks only while it is in
 *   sight (CRoofBoss::MonsterThink 10068CD0); the c_attic doctor caches it (+0x368) and aims at, shoots and hurts
 *   only that one (CoF_DoctorBoss.cpp). Both go for a living player they see, nearest first, and keep them while
 *   they stay so. A player who leaves is taken out of every boss's raw pointer (the doctor's, the c_ending4b book
 *   Simons' +0x38C/+0x388). */
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
#define COF_MONSTER_SIZE 0x368
#define COF_MONSTER_ENEMY 0x184 // EHANDLE m_hEnemy
#define COF_DOCTOR_SIZE 0x3A8
#define COF_DOCTOR_PLAYER 0x368 // raw CBasePlayer *pPlayer (1002E853)
#define COF_SIMON_SIZE 0x390
#define COF_SIMON_KILLER 0x388 // raw CBasePlayer *pPlayerWhoKilledMe
#define COF_SIMON_PLAYER 0x38C // raw CBasePlayer *pPlayer (1006F4EA)
#define COF_RESCUE_ROOFBOSSES 2
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
// The player each roof boss goes for.
static struct { int index, serial, userid; } cof_rescue_roof[COF_RESCUE_ROOFBOSSES];
static int cof_rescue_generation = -1;

void SV_CoF_RescueReset( void )
{
	cof_rescue_generation = -1;
	cof_rescue_boss_count = 0;
	memset( cof_rescue_bosses, 0, sizeof( cof_rescue_bosses ));
	memset( cof_rescue_kills, 0, sizeof( cof_rescue_kills ));
	memset( cof_rescue_padlocks, 0, sizeof( cof_rescue_padlocks ));
	memset( cof_rescue_roof, 0, sizeof( cof_rescue_roof ));
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

// The participant a boss bound to "the first player" should fight: the one it has while alive and in its sight,
// else a living player it sees (nearest), else the nearest living one; NULL when nobody lives.
static sv_client_t *CoF_RescueBossTarget( edict_t *boss, sv_client_t *current )
{
	sv_client_t *seen = NULL, *any = NULL;
	float seen_d = 0.0f, any_d = 0.0f;
	int i;
	if( current && CoF_RescueAlive( current->edict ) && SV_CoF_Sees( boss, current->edict )) return current;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		float d;
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict ) || !CoF_RescueAlive( cl->edict )) continue;
		d = VectorDistance( boss->v.origin, cl->edict->v.origin );
		if( !any || d < any_d ) { any = cl; any_d = d; }
		if( SV_CoF_Sees( boss, cl->edict ) && ( !seen || d < seen_d )) { seen = cl; seen_d = d; }
	}
	return seen ? seen : any;
}

static sv_client_t *CoF_RescueClientOfPrivate( uint32_t pv )
{
	int i;
	for( i = 0; pv && i < svs.maxclients && i < MAX_CLIENTS; i++ )
		if( svs.clients[i].state == cs_spawned && SV_IsValidEdict( svs.clients[i].edict ) &&
			(uint32_t)(uintptr_t)svs.clients[i].edict->pvPrivateData == pv )
			return &svs.clients[i];
	return NULL;
}

static edict_t *CoF_RescueRoofBoss( edict_t *boss )
{
	int i, slot = -1;
	sv_client_t *target;
	for( i = 0; i < COF_RESCUE_ROOFBOSSES; i++ )
	{
		if( cof_rescue_roof[i].index == NUM_FOR_EDICT( boss ) && cof_rescue_roof[i].serial == boss->serialnumber ) { slot = i; break; }
		if( slot < 0 && !cof_rescue_roof[i].index ) slot = i;
	}
	if( slot < 0 ) slot = 0;
	if( cof_rescue_roof[slot].index != NUM_FOR_EDICT( boss ) || cof_rescue_roof[slot].serial != boss->serialnumber ) cof_rescue_roof[slot].userid = 0;
	target = CoF_RescueBossTarget( boss, CoF_RescueClientByUserid( cof_rescue_roof[slot].userid ));
	cof_rescue_roof[slot].index = NUM_FOR_EDICT( boss );
	cof_rescue_roof[slot].serial = boss->serialnumber;
	cof_rescue_roof[slot].userid = target ? target->userid : 0;
	return target ? target->edict : NULL;
}

// The doctor's cached player becomes the one it should fight (written before its think reads it).
static edict_t *CoF_RescueDoctor( edict_t *doctor )
{
	byte *pv = SV_CoF_StoryEntityPrivate( doctor, COF_DOCTOR_SIZE );
	uint32_t cached, wanted;
	sv_client_t *current, *target;
	if( !pv ) return NULL;
	memcpy( &cached, pv + COF_DOCTOR_PLAYER, sizeof( cached ));
	current = CoF_RescueClientOfPrivate( cached );
	if( !( target = CoF_RescueBossTarget( doctor, current ))) return NULL;
	if( target != current )
	{
		wanted = (uint32_t)(uintptr_t)target->edict->pvPrivateData;
		memcpy( pv + COF_DOCTOR_PLAYER, &wanted, sizeof( wanted ));
		Con_Printf( "cofstory phase=boss-target class=monster_doctorboss userid=%d\n", target->userid );
	}
	return target->edict;
}

edict_t *SV_CoF_RescueThinkFirst( edict_t *thinker )
{
	int i;
	sv_client_t *solver;
	if( !thinker || !SV_CoF_StoryCoopEnabled() ) return NULL;
	if( CoF_RescueClass( thinker, "monster_roofboss" )) return CoF_RescueRoofBoss( thinker );
	if( CoF_RescueClass( thinker, "monster_doctorboss" )) return CoF_RescueDoctor( thinker );
	if( !CoF_RescueClass( thinker, "item_padlock" ) || thinker->v.body != 1 ) return NULL;
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

edict_t *SV_CoF_RescueThinkSlotOne( edict_t *thinker )
{
	cof_rescue_handle_t enemy;
	int index;
	if( !thinker || !SV_CoF_StoryCoopEnabled() || !CoF_RescueClass( thinker, "monster_crazybitch" )) return NULL;
	enemy = CoF_RescueHandleIn( thinker, COF_MONSTER_SIZE, COF_MONSTER_ENEMY );
	if( !enemy.edict || !SV_IsValidEdict( enemy.edict ) || enemy.edict->serialnumber != enemy.serial ) return NULL;
	index = NUM_FOR_EDICT( enemy.edict );
	if( index <= 1 || index > svs.maxclients || index > MAX_CLIENTS ) return NULL; // the host is edict 1 already
	if( svs.clients[index - 1].state != cs_spawned || svs.clients[index - 1].edict != enemy.edict || !CoF_RescueAlive( enemy.edict ))
		return NULL;
	return enemy.edict;
}

void SV_CoF_RescueThinkBegin( edict_t *thinker )
{
	SV_CoF_ThinkFirst( SV_CoF_RescueThinkFirst( thinker ));
	SV_CoF_ThinkSlotOne( SV_CoF_RescueThinkSlotOne( thinker ));
}

void SV_CoF_RescueThinkEnd( void )
{
	SV_CoF_ThinkFirst( NULL );
	SV_CoF_ThinkSlotOne( NULL );
}

void SV_CoF_RescueDrop( sv_client_t *cl )
{
	static const struct { const char *classname; size_t size; int offset; } holders[] =
	{
		{ "monster_doctorboss", COF_DOCTOR_SIZE, COF_DOCTOR_PLAYER },
		{ "monster_booksimon", COF_SIMON_SIZE, COF_SIMON_PLAYER },
		{ "monster_booksimon", COF_SIMON_SIZE, COF_SIMON_KILLER },
	};
	uint32_t leaver, held, none = 0;
	int i, k;
	if( !cl || !SV_IsValidEdict( cl->edict ) || !cl->edict->pvPrivateData || !svgame.edicts || !SV_CoF_StoryCoopEnabled() ) return;
	leaver = (uint32_t)(uintptr_t)cl->edict->pvPrivateData;
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		for( k = 0; k < (int)( sizeof( holders ) / sizeof( holders[0] )); k++ )
		{
			byte *pv;
			if( !CoF_RescueClass( ent, holders[k].classname ) || !( pv = SV_CoF_StoryEntityPrivate( ent, holders[k].size ))) continue;
			memcpy( &held, pv + holders[k].offset, sizeof( held ));
			if( held == leaver ) memcpy( pv + holders[k].offset, &none, sizeof( none ));
		}
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
