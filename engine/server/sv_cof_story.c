/* Opt-in Cry of Fear story co-op adapter. GPL-3.0-or-later.
 * The disk game DLL stays stock. Only its multiplayer transition veto is
 * bypassed, after exact file and instruction verification. Landmark and
 * world restoration remain the engine's responsibility.
 */
#include "common.h"
#include "server.h"
#include "library.h"
#include "sv_cof_cheats.h"
#include "sv_cof_story.h"
#include "sv_cof_party.h"
#include "sv_cof_retail.h"
#include "sv_cof_share.h"
#include "sv_cof_trace.h"
#include "sv_cof_exit.h"
#include "sv_cof_state.h"
#include "sv_cof_recovery.h"
#include "sv_cof_session.h"
#include "sv_cof_scene.h"
#include "sv_cof_moments.h"
#include "sv_cof_rescue.h"
#include "sv_cof_companion.h"
#include "sv_cof_sections.h"
#include "sv_cof_testgun.h"
#include "sv_cof_inventory12.h"
#include "cof_abi.h"
#if XASH_WIN32
#include <windows.h>
#endif

static CVAR_DEFINE_AUTO( cof_donator, "1", 0, "Cry of Fear: the donator level every player's commands carry to the game (1: a donator, weapon_tmp pickups and TMP ammo kept in multiplayer; 0: retail's non-donor)" );

int SV_CoF_DonatorLevel( void )
{
	return CoF_DonatorLevel( cof_donator.value );
}

static int cof_quiet_messages;

void SV_CoF_QuietGameMessages( qboolean on )
{
	cof_quiet_messages += on ? 1 : -1;
	if( cof_quiet_messages < 0 ) cof_quiet_messages = 0;
}

qboolean SV_CoF_GameMessagesQuiet( void )
{
	return cof_quiet_messages > 0;
}

static const byte cof_gate_original[6] = { 0x0f, 0x85, 0xb5, 0x01, 0x00, 0x00 };
static const byte cof_gate_open[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
static byte *cof_gate;
static void *cof_gate_instance;
// The multiplayer refusal of `inventorydualwield` (ClientCommand: the jnz after
// IsMultiplayer at 1001B6DB), opened with the exit gate. The dual weapon the game
// then builds is deployed by the engine (SV_CoF_RetailDualDeploy): the multiplayer
// rules never switch to it. Research: evidence/tester-package-20261015/research/dualwield-mp-re.md
#define COF_DUAL_GATE 0x1B6E0
static const byte cof_dual_gate_original[6] = { 0x0f, 0x85, 0x5a, 0x15, 0x00, 0x00 };
static byte *cof_dual_gate;

static qboolean CoF_StoryWriteGate( byte *p, const byte *expect, const byte *value )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	DWORD protect, ignored;
	if( memcmp( p, expect, 6 ) || !VirtualProtect( p, 6, PAGE_EXECUTE_READWRITE, &protect )) return false;
	memcpy( p, value, 6 );
	FlushInstructionCache( GetCurrentProcess(), p, 6 );
	VirtualProtect( p, 6, protect, &ignored );
	return true;
#else
	return false;
#endif
}

// Retail InstallGameRules allocates the multiplayer rules with a plain
// operator new( 0xE4 ) and never initializes their vote-kick and lobby state.
// A fresh heap hides that on the first map; after a story map change the
// recycled block can hold a phantom "active" vote whose stale target-ID
// pointer crashes the next rules Think. Zero that one allocation, still
// through the DLL's own operator new so its delete stays matched.
#define COF_RULES_NEW_CALL 0xAF79E // call operator new; add esp, 4 follows
#define COF_OPERATOR_NEW 0x13E1E9
static const byte cof_rules_new_original[6] = { 0xe8, 0x46, 0xea, 0x08, 0x00, 0x83 };
static byte cof_rules_new_zeroed[6];
static byte *cof_rules_new;

static void CoF_StoryRestoreRules( void )
{
	if( cof_rules_new ) CoF_StoryWriteGate( cof_rules_new, cof_rules_new_zeroed, cof_rules_new_original );
	cof_rules_new = NULL;
}

#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
static void *( __cdecl *cof_operator_new )( size_t size );

static void * __cdecl CoF_StoryNewRules( size_t size )
{
	void *p = cof_operator_new( size );
	if( p ) memset( p, 0, size );
	return p;
}

static qboolean CoF_StoryZeroRules( byte *base )
{
	int32_t rel = (int32_t)((byte *)CoF_StoryNewRules - ( base + COF_RULES_NEW_CALL + 5 ));
	cof_operator_new = (void *)( base + COF_OPERATOR_NEW );
	cof_rules_new_zeroed[0] = 0xe8;
	memcpy( cof_rules_new_zeroed + 1, &rel, sizeof( rel ));
	cof_rules_new_zeroed[5] = cof_rules_new_original[5];
	if( !CoF_StoryWriteGate( base + COF_RULES_NEW_CALL, cof_rules_new_original, cof_rules_new_zeroed )) return false;
	cof_rules_new = base + COF_RULES_NEW_CALL;
	return true;
}
#endif

static void CoF_StoryCloseGate( void )
{
	SV_CoF_Inventory12Close();
	SV_CoF_GatesClose();
	if( cof_gate && cof_gate_instance == svgame.hInstance )
	{
		CoF_StoryWriteGate( cof_gate, cof_gate_open, cof_gate_original );
		if( cof_dual_gate ) CoF_StoryWriteGate( cof_dual_gate, cof_gate_open, cof_dual_gate_original );
		CoF_StoryRestoreRules();
	}
	cof_rules_new = NULL;
	cof_dual_gate = NULL;
	cof_gate = NULL;
	cof_gate_instance = NULL;
}

qboolean SV_CoF_StoryCoopEnabled( void )
{
	if( !Cvar_VariableInteger( "cof_story_coop" ) || !GI || Q_stricmp( GI->gamefolder, "cryoffear" ) || svs.maxclients <= 1 || sv.background )
	{
		CoF_StoryCloseGate();
		return false;
	}
	if( !SV_CoF_RetailDLLVerified() ) return false;
	if( cof_gate && cof_gate_instance == svgame.hInstance )
		return !memcmp( cof_gate, cof_gate_open, sizeof( cof_gate_open )) && cof_rules_new && !memcmp( cof_rules_new, cof_rules_new_zeroed, sizeof( cof_rules_new_zeroed ));
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	{
		dll_user_t *lib = (dll_user_t *)svgame.hInstance;
		byte *base;
		IMAGE_DOS_HEADER *dos;
		IMAGE_NT_HEADERS *nt;
		if( !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return false;
		base = (byte *)lib->hInstance;
		dos = (IMAGE_DOS_HEADER *)base;
		if( dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096 ) return false;
		nt = (IMAGE_NT_HEADERS *)( base + dos->e_lfanew );
		if( nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.SizeOfImage < COF_OPERATOR_NEW + 1 ) return false;
		if( !CoF_StoryWriteGate( base + 0x109D60, cof_gate_original, cof_gate_open )) return false;
		if( !CoF_StoryZeroRules( base ))
		{
			CoF_StoryWriteGate( base + 0x109D60, cof_gate_open, cof_gate_original );
			return false;
		}
		cof_gate = base + 0x109D60;
		cof_gate_instance = svgame.hInstance;
		// dual wield is a nicety: story co-op runs without it if the bytes differ
		cof_dual_gate = CoF_StoryWriteGate( base + COF_DUAL_GATE, cof_dual_gate_original, cof_gate_open ) ? base + COF_DUAL_GATE : NULL;
		Con_Printf( "cofstory phase=gate-enabled landmark=preserved rules=zeroed dualwield=%s\n", cof_dual_gate ? "open" : "closed" );
		return true;
	}
#else
	return false;
#endif
}

// A retail thiscall member taking one pointer (ret 4), at a known RVA of the
// hash-verified game DLL. __fastcall puts self in ecx and a dummy in edx; the
// argument goes on the stack and the callee pops it, exactly as thiscall does.
qboolean SV_CoF_StoryCallMember( unsigned int rva, void *self, void *arg )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	typedef void ( __fastcall *cof_member_t )( void *self, void *edx, void *arg );
	dll_user_t *lib = (dll_user_t *)svgame.hInstance;
	if( !self || !SV_CoF_StoryCoopEnabled() || !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return false;
	((cof_member_t)((byte *)lib->hInstance + rva ))( self, NULL, arg );
	return true;
#else
	return false;
#endif
}

// The verified game DLL's load address (0 without one): thinks and other
// function pointers it stores are compared against base + RVA.
uintptr_t SV_CoF_StoryBase( void )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	dll_user_t *lib = (dll_user_t *)svgame.hInstance;
	if( !SV_CoF_StoryCoopEnabled() || !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return 0;
	return (uintptr_t)lib->hInstance;
#else
	return 0;
#endif
}

// The game's FireTargets( name, activator, caller, USE_TOGGLE, 0 ) (cdecl,
// 10101FF0), as an entity of its own would call it.
qboolean SV_CoF_StoryFireTargets( const char *name, void *activator, void *caller )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	typedef void ( __cdecl *cof_fire_t )( const char *name, void *activator, void *caller, int use_type, float value );
	uintptr_t base = SV_CoF_StoryBase();
	if( !base || !name || !name[0] ) return false;
	((cof_fire_t)( base + 0x101FF0 ))( name, activator, caller, 3, 0.0f );
	return true;
#else
	return false;
#endif
}

qboolean SV_CoF_StoryDeployWeapon( void *self )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	typedef int ( __fastcall *deploy_t )( void *self, void *unused );
	dll_user_t *library = (dll_user_t *)svgame.hInstance;
	uintptr_t base, end, table, function;
	IMAGE_DOS_HEADER *dos;
	IMAGE_NT_HEADERS *headers;
	if( !self || !SV_CoF_StoryCoopEnabled() || !library || !library->hInstance || library->custom_loader || library->encrypted ) return false;
	base = (uintptr_t)library->hInstance;
	dos = (IMAGE_DOS_HEADER *)base;
	headers = (IMAGE_NT_HEADERS *)( base + dos->e_lfanew );
	end = base + headers->OptionalHeader.SizeOfImage;
	memcpy( &table, self, sizeof( table ));
	if( table < base || table > end - 82 * sizeof( void * )) return false;
	memcpy( &function, (const void *)( table + 81 * sizeof( void * )), sizeof( function ));
	if( function < base || function >= end ) return false;
	return ((deploy_t)function)( self, NULL ) != 0;
#else
	return false;
#endif
}

static void CoF_StoryStatus_f( void )
{
	int i, j;
	Con_Printf( "cofstory phase=status enabled=%d map=%s paused=%d generation=%d\n", SV_CoF_StoryCoopEnabled(), sv.name, sv.paused, svs.spawncount );
	if( sv.state != ss_active || !SV_CoF_RetailDLLVerified() ) return;
	for( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		edict_t *p = cl->edict;
		byte *pv;
		if( cl->state != cs_spawned || !SV_IsValidEdict( p )) continue;
		pv = SV_CoF_StoryEntityPrivate( p, 0x2250 );
		Con_Printf( "cofstory phase=player slot=%d userid=%d ready=1 origin=%.3f,%.3f,%.3f door=%d hidden=%d flags=%d health=%.3f fakeclient=%d frozen=%d\n", i, cl->userid, p->v.origin[0], p->v.origin[1], p->v.origin[2], pv ? pv[0x1EA4] : -1, !!( p->v.effects & EF_NODRAW ), p->v.flags, p->v.health, !!( p->v.flags & FL_FAKECLIENT ), !!( p->v.flags & FL_FROZEN ));
		if( pv )
		{
			for( j = 0; j <= 12; j++ ) // inventorySlot[0..12]; 1..12 hold items in story co-op (sv_cof_inventory12_patch.h)
			{
				string_t id;
				// Actual descriptor slots; index0 is unused by retail item assignment.
				memcpy( &id, pv + 0x21FC + j * sizeof( id ), sizeof( id ));
				Con_Printf( "cofstory phase=inventory-slot slot=%d userid=%d index=%d descriptor=\"%s\"\n", i, cl->userid, j, id ? SV_GetString( id ) : "" );
			}
		}
		for( j = svs.maxclients + 1; j < svgame.numEntities; j++ )
		{
			edict_t *ent = SV_EdictNum( j );
			const char *classname;
			if( !SV_IsValidEdict( ent ) || ( ent->v.owner != p && ent->v.aiment != p )) continue;
			classname = SV_GetString( ent->v.classname );
			if( Q_strncmp( classname, "weapon_", 7 ) && Q_strncmp( classname, "item_", 5 ) && Q_strncmp( classname, "ammo_", 5 )) continue;
			Con_Printf( "cofstory phase=inventory slot=%d userid=%d class=%s\n", i, cl->userid, classname );
		}
	}
	for( i = svs.maxclients + 1; i < svgame.numEntities; i++ )
	{
		edict_t *ent = SV_EdictNum( i );
		const char *name;
		if( !SV_IsValidEdict( ent )) continue;
		name = SV_GetString( ent->v.targetname );
		if( !Q_strcmp( SV_GetString( ent->v.classname ), "info_landmark" ))
			Con_Printf( "cofstory phase=landmark name=%s origin=%.3f,%.3f,%.3f\n", name, ent->v.origin[0], ent->v.origin[1], ent->v.origin[2] );
		if( Q_strncmp( name, "cof_probe_", 10 )) continue;
		Con_Printf( "cofstory phase=fixture name=%s class=%s health=%.3f\n", name, SV_GetString( ent->v.classname ), ent->v.health );
	}
	Con_Printf( "cofstory phase=status-end\n" );
}

void SV_CoF_StoryInit( void )
{
	Cvar_Get( "cof_story_coop", "0", 0, "experimental Cry of Fear story co-op world and party persistence" );
	Cvar_Get( "cof_build", "", 0, "story co-op: the tester build this server runs, sent as cofbuild in its info reply" );
	Cmd_AddCommand( "cof_story_status", CoF_StoryStatus_f, "report story co-op player and world state" );
	Cvar_RegisterVariable( &cof_donator );
	SV_CoF_RetailInit();
	SV_CoF_PartyInit();
	SV_CoF_CompanionInit();
	SV_CoF_TestGunInit();
	SV_CoF_SceneInit();
	SV_CoF_ShareInit();
	SV_CoF_TraceInit();
	SV_CoF_RecoveryInit();
	SV_CoF_Inventory12Init();
	SV_CoF_SessionInit();
	SV_CoF_SectionsInit();
	SV_CoF_BonusInit();
	SV_CoF_LightInit();
	SV_CoF_GatesInit();
	SV_CoF_VoiceInit();
}

void SV_CoF_StoryUserinfo( const sv_client_t *cl, char *info, size_t size )
{
	const char *model;
	char path[MAX_QPATH];
	char *cursor, *end, *value_start = NULL, *value_end = NULL;
	size_t length, model_length;
	int slot = (int)( cl - svs.clients );
	static const char *models[] = { "cof_s1", "cof_s2", "cof_s3", "cof_s4" };
	if( !info || !size || slot < 0 || slot >= svs.maxclients || !SV_CoF_StoryCoopEnabled() ) return;
	// the host Simon, then the costumes; a copy made before cof_s3/cof_s4 existed shows cof_s2
	model = models[slot < 4 ? slot : 1];
	Q_snprintf( path, sizeof( path ), "models/player/%s/%s.mdl", model, model );
	if( !FS_FileExists( path, false ) && slot > 1 )
	{
		model = models[1];
		Q_snprintf( path, sizeof( path ), "models/player/%s/%s.mdl", model, model );
	}
	if( !FS_FileExists( path, false )) return;
	length = Q_strnlen( info, size );
	if( length == size ) return;
	end = info + length;
	model_length = Q_strlen( model );
	cursor = info;
	while( cursor < end )
	{
		char *key, *value;
		size_t key_length;
		if( *cursor == '\\' ) cursor++;
		key = cursor;
		while( cursor < end && *cursor != '\\' ) cursor++;
		if( cursor == end ) return; // malformed pair; preserve the original
		key_length = cursor - key;
		value = ++cursor;
		while( cursor < end && *cursor != '\\' ) cursor++;
		if( !value_start && key_length == 5 && !memcmp( key, "model", 5 ))
		{
			value_start = value;
			value_end = cursor;
		}
	}
	// Do not use the generic setter's important-key eviction policy: a
	// cosmetic model replacement must leave all userinfo intact if it cannot
	// fit. Also match the whole key, preserving keys such as model_note.
	if( value_start )
	{
		if( length - (value_end - value_start) + model_length >= size ) return;
		memmove( value_start + model_length, value_end, end - value_end + 1 );
		memcpy( value_start, model, model_length );
	}
	else
	{
		if( length + 7 + model_length >= size ) return;
		memcpy( end, "\\model\\", 7 );
		memcpy( end + 7, model, model_length + 1 );
	}
}

void SV_CoF_StoryFrame( void )
{
	if( !SV_CoF_StoryCoopEnabled() || sv.state != ss_active ) return;
	SV_CoF_GatesFrame(); // first: the game's own rules as this frame wants them
	SV_CoF_RecoveryFrame();
	SV_CoF_SceneFrame();
	SV_CoF_MomentsFrame();
	SV_CoF_RescueFrame();
	SV_CoF_PartyFrame(); // first: a door's party move lands before the AI decides it lost the leader
	SV_CoF_SectionsFrame(); // merged maps: respawns, companions and gone exits, before the AI decides
	SV_CoF_FogFrame(); // after the section moves
	SV_CoF_CompanionFrame();
	SV_CoF_LightFrame(); // a co-op torch the host does not allow goes off
	SV_CoF_TestGunFrame();
	SV_CoF_ExitFrame();
	SV_CoF_ShareFrame();
	SV_CoF_StateFrame();
	SV_CoF_SessionFrame();
	SV_CoF_TraceFrame();
	SV_CoF_Inventory12Frame();
	SV_CoF_WeaponModelFrame();
}

void SV_CoF_StoryShutdown( void )
{
	SV_CoF_CommandSender( NULL ); // an engine error inside a command leaves it set
	SV_CoF_UsercmdSender( NULL );
	SV_CoF_PartyShutdown();
	SV_CoF_SceneReset();
	SV_CoF_MomentsReset();
	SV_CoF_RescueReset();
	SV_CoF_FogReset();
	SV_CoF_CompanionReset();
	SV_CoF_TestGunReset();
	SV_CoF_ExitReset();
	SV_CoF_ShareReset();
	SV_CoF_StateReset();
	SV_CoF_RecoveryReset();
	SV_CoF_SessionReset();
	CoF_StoryCloseGate();
	SV_CoF_ClearValidation();
}
