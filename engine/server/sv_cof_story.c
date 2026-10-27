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
#include "sv_cof_useprobe.h"
#include "sv_cof_exit.h"
#include "sv_cof_state.h"
#include "sv_cof_recovery.h"
#include "sv_cof_session.h"
#include "sv_cof_scene.h"
#include "sv_cof_moments.h"
#include "sv_cof_rescue.h"
#include "sv_cof_chairs.h"
#include "sv_cof_companion.h"
#include "sv_cof_sections.h"
#include "sv_cof_testgun.h"
#include "sv_cof_inventory12.h"
#include "sv_cof_chapter_kit.h"
#include "sv_cof_pickups.h"
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
// The quick-slot double tap (two quick-slot keys within a quarter second dual-wield
// their items) is refused in multiplayer twice: QuickSelectItem (100F07F0, the jnz
// after IsMultiplayer at 100F087F) and the CheckQuickDualWield it calls (100DCB10,
// the jnz after IsMultiplayer at 100DCB40). Both open with the command's gate, or
// neither; the last slot pressed, one global of the game's, is kept per player
// (sv_cof_globals.h), and the dual weapon is deployed like the command's.
#define COF_QUICK_DUAL_GATE 0xF0884
static const byte cof_quick_dual_gate_original[6] = { 0x0f, 0x85, 0x5c, 0x01, 0x00, 0x00 };
#define COF_QUICK_DUAL_CHECK 0xDCB45
static const byte cof_quick_dual_check_original[6] = { 0x0f, 0x85, 0x0b, 0x03, 0x00, 0x00 };
static byte *cof_quick_dual_gate, *cof_quick_dual_check;

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
static void *cof_rules_instance; // 1.3.3: the DLL instance cof_rules_new is in

static void CoF_StoryRestoreRules( void )
{
	if( cof_rules_new ) CoF_StoryWriteGate( cof_rules_new, cof_rules_new_zeroed, cof_rules_new_original );
	cof_rules_new = NULL;
	cof_rules_instance = NULL;
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

#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
// The verified game DLL's image, checked enough to patch at our RVAs, or NULL.
static byte *CoF_StoryImage( void )
{
	dll_user_t *lib = (dll_user_t *)svgame.hInstance;
	byte *base;
	IMAGE_DOS_HEADER *dos;
	IMAGE_NT_HEADERS *nt;
	if( !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return NULL;
	base = (byte *)lib->hInstance;
	dos = (IMAGE_DOS_HEADER *)base;
	if( dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096 ) return NULL;
	nt = (IMAGE_NT_HEADERS *)( base + dos->e_lfanew );
	if( nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.SizeOfImage < COF_OPERATOR_NEW + 1 ) return NULL;
	return base;
}
#endif

// 1.3.3: the multiplayer rules' allocation is zeroed in every multiplayer game of the verified DLL, not only with the
// story layer on: Cry of Fear's own co-op campaign (the story layer is off on its maps) installs the same rules on
// every map and meets the same recycled vote-kick state. A single-player map spawned afterwards gets the call back
// (a single-player save loaded afterwards keeps it, harmlessly: its rules come from another allocation). Before each
// map's entities spawn (SV_SpawnEntities: InstallGameRules runs in the world's precache) and with the story gate;
// loaded saves and changelevel2 revisits (LoadGameState) are reached only after a guarded map in the same process.
static qboolean CoF_StoryRulesGuard( void )
{
	if( cof_rules_new && cof_rules_instance != svgame.hInstance )
	{
		cof_rules_new = NULL; // another instance of the DLL: the patched one is gone with the last
		cof_rules_instance = NULL;
	}
	if( !GI || Q_stricmp( GI->gamefolder, "cryoffear" ) || svs.maxclients <= 1 || !SV_CoF_RetailDLLVerified() )
	{
		CoF_StoryRestoreRules();
		return false;
	}
	if( cof_rules_new && !memcmp( cof_rules_new, cof_rules_new_zeroed, sizeof( cof_rules_new_zeroed ))) return true;
	cof_rules_new = NULL; // not (any more) ours: the bytes are looked at again below
	cof_rules_instance = NULL;
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	{
		byte *base = CoF_StoryImage();
		if( !base || !CoF_StoryZeroRules( base )) return false;
		cof_rules_instance = svgame.hInstance;
		Con_Printf( "cofstory phase=rules-zeroed map=%s\n", sv.name );
		return true;
	}
#else
	return false;
#endif
}

void SV_CoF_StoryRulesGuard( void )
{
	CoF_StoryRulesGuard();
}

// 1.3.3: Cry of Fear's own co-op campaign, manhunt and survival maps (cof_campaign_01, cof_manhunt_campaign,
// cof_suicide1-4, c_cof_*): the game's multiplayer rules play them as they are, never the story layer
qboolean SV_CoF_StoryOfficialMap( const char *map )
{
	return map && ( !Q_strnicmp( map, "cof_", 4 ) || !Q_strnicmp( map, "c_cof_", 6 ));
}

static void CoF_StoryCloseGate( void )
{
	SV_CoF_Inventory12Close();
	SV_CoF_GatesClose();
	if( cof_gate && cof_gate_instance == svgame.hInstance )
	{
		CoF_StoryWriteGate( cof_gate, cof_gate_open, cof_gate_original );
		if( cof_dual_gate ) CoF_StoryWriteGate( cof_dual_gate, cof_gate_open, cof_dual_gate_original );
		if( cof_quick_dual_gate ) CoF_StoryWriteGate( cof_quick_dual_gate, cof_gate_open, cof_quick_dual_gate_original );
		if( cof_quick_dual_check ) CoF_StoryWriteGate( cof_quick_dual_check, cof_gate_open, cof_quick_dual_check_original );
	}
	cof_dual_gate = NULL;
	cof_quick_dual_gate = NULL;
	cof_quick_dual_check = NULL;
	cof_gate = NULL;
	cof_gate_instance = NULL;
}

qboolean SV_CoF_StoryCoopEnabled( void )
{
	// 1.3.3: never on Cry of Fear's own co-op maps (SV_CoF_StoryOfficialMap)
	if( !Cvar_VariableInteger( "cof_story_coop" ) || !GI || Q_stricmp( GI->gamefolder, "cryoffear" ) || svs.maxclients <= 1 || sv.background ||
		SV_CoF_StoryOfficialMap( sv.name ))
	{
		CoF_StoryCloseGate();
		return false;
	}
	if( !SV_CoF_RetailDLLVerified() ) return false;
	if( cof_gate && cof_gate_instance == svgame.hInstance )
		return !memcmp( cof_gate, cof_gate_open, sizeof( cof_gate_open )) && CoF_StoryRulesGuard();
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	{
		byte *base = CoF_StoryImage();
		if( !base || !CoF_StoryRulesGuard()) return false;
		if( !CoF_StoryWriteGate( base + 0x109D60, cof_gate_original, cof_gate_open )) return false;
		cof_gate = base + 0x109D60;
		cof_gate_instance = svgame.hInstance;
		// dual wield is a nicety: story co-op runs without it if the bytes differ
		cof_dual_gate = CoF_StoryWriteGate( base + COF_DUAL_GATE, cof_dual_gate_original, cof_gate_open ) ? base + COF_DUAL_GATE : NULL;
		if( cof_dual_gate && CoF_StoryWriteGate( base + COF_QUICK_DUAL_CHECK, cof_quick_dual_check_original, cof_gate_open ))
		{
			if( CoF_StoryWriteGate( base + COF_QUICK_DUAL_GATE, cof_quick_dual_gate_original, cof_gate_open ))
			{
				cof_quick_dual_check = base + COF_QUICK_DUAL_CHECK;
				cof_quick_dual_gate = base + COF_QUICK_DUAL_GATE;
			}
			else CoF_StoryWriteGate( base + COF_QUICK_DUAL_CHECK, cof_gate_open, cof_quick_dual_check_original );
		}
		Con_Printf( "cofstory phase=gate-enabled landmark=preserved rules=zeroed dualwield=%s quickdual=%s\n",
			cof_dual_gate ? "open" : "closed", cof_quick_dual_gate ? "open" : "closed" );
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

// 1.3.4: a retail thiscall member that takes nothing and returns with a plain ret (CBasePlayer::StartWheelchairMode
// 100F5B30, StopWheelchairMode 100F5D50). __fastcall with no stack argument: self in ecx, a dummy in edx, nothing
// for either side to pop.
qboolean SV_CoF_StoryCallMember0( unsigned int rva, void *self )
{
#if XASH_WIN32 && XASH_X86 && defined( XASH_COF_ENTVARS_LEGACY ) && XASH_COF_ENTVARS_LEGACY
	typedef void ( __fastcall *cof_member0_t )( void *self, void *edx );
	dll_user_t *lib = (dll_user_t *)svgame.hInstance;
	if( !self || !SV_CoF_StoryCoopEnabled() || !lib || !lib->hInstance || lib->custom_loader || lib->encrypted ) return false;
	((cof_member0_t)((byte *)lib->hInstance + rva ))( self, NULL );
	return true;
#else
	(void)rva; (void)self;
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
	// 1.2.6: most fixes run in the host's game, so a guest's app says when the host runs an older release (cofver)
	Cvar_Get( "cof_label", "", 0, "story co-op: the app release this server runs (1.2.6 and later), sent as cofver in its info reply" );
	Cvar_Get( "cof_chapter_start", "", 0, "story co-op: the chapter map the app started (not New game or Continue); its kit is handed out once" );
	Cvar_Get( "cof_maps", "", 0, "story co-op: a hash of the host's merged maps, sent as cofmaps in its info reply (the app compares it before a join)" );
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
	SV_CoF_BlurInit();
	SV_CoF_GatesInit();
	SV_CoF_VoiceInit();
	SV_CoF_WeatherInit();
	SV_CoF_GlobalsInit();
	SV_CoF_WeaponModelInit();
	SV_CoF_SkyInit();
	SV_CoF_ViewInit();
	SV_CoF_CameraModelsInit();
	SV_CoF_PickupsInit();
	SV_CoF_ChairsInit();
}

// 1.1.9 (I7): the chapter start's kit until every person in the game carries it; sv_cof_session.inc keeps the
// checkpoints back meanwhile, COF_STORY_KIT_WAIT at most. One taken before the kit landed (the first autosave waits
// out the opening scenes of c_basement, c_subwaysick2 and c_ending5, and the kit goes out every half second to the
// players no scene holds) reloaded an empty-handed party after a team wipe, and the kit is handed out once: Chapter 2
// had no phone for the c_basement door, Chapter 8 no gun for c_endingNc's kills.
#define COF_STORY_KIT_WAIT 30.0 // from the first checkpoint it held back
static const cof_chapter_kit_t *cof_story_kit;
static int cof_story_kit_generation;
static double cof_story_kit_until;

qboolean SV_CoF_StoryKitDue( void ); // asked by the checkpoint save (sv_cof_session.inc)
qboolean SV_CoF_StoryKitDue( void )
{
	char slots[COF_RETAIL_SLOTS][COF_RETAIL_ITEM], descriptor[COF_RETAIL_ITEM];
	int i, w, j, missing = 0;
	if( !cof_story_kit ) return false;
	if( cof_story_kit_generation != svs.spawncount )
	{
		cof_story_kit = NULL; // another map, or a checkpoint loaded: not this start's any more
		return false;
	}
	if( cof_story_kit_until < 0 ) cof_story_kit_until = sv.time + COF_STORY_KIT_WAIT;
	for( i = 0; i < svs.maxclients && !missing; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_CoF_RetailReadSlots( cl, slots )) continue;
		for( w = 0; w < COF_CHAPTER_WEAPONS && cof_story_kit->weapons[w] && !missing; w++ )
		{
			// a weapon's inventory descriptor, as the party's gifts read it (sv_cof_party.c CoF_PartyShareWeapon)
			Q_snprintf( descriptor, sizeof( descriptor ), "inventoryitems/weapons/%s.txt", cof_story_kit->weapons[w] );
			for( j = 0; j < COF_RETAIL_SLOTS; j++ ) if( !Q_strcmp( slots[j], descriptor )) break;
			if( j == COF_RETAIL_SLOTS ) missing = 1;
		}
	}
	if( missing && sv.time < cof_story_kit_until ) return true;
	Con_Printf( "cofstory phase=chapter-kit-%s\n", missing ? "wait-over" : "delivered" );
	cof_story_kit = NULL;
	return false;
}

// 1.1.9 (C1): a story global the chapter's own scene needs, switched on the way the game does it: an env_global with
// SF_GLOBAL_SET adds its global in its initial state when the table lacks it (CEnvGlobal::Spawn). The global table
// keeps it (and the saves); the helper entity goes again at once.
static void CoF_StoryGlobalOn( const char *name )
{
	char classname[] = "env_global", key_state[] = "globalstate", key_initial[] = "initialstate", on[] = "1", value[64];
	KeyValueData kvd;
	edict_t *ent;
	if( !svgame.dllFuncs.pfnKeyValue || !svgame.dllFuncs.pfnSpawn ) return;
	ent = SV_CreateNamedEntity( NULL, SV_AllocString( classname ));
	if( !SV_IsValidEdict( ent )) return;
	if( !ent->pvPrivateData )
	{
		SV_FreeEdict( ent );
		return;
	}
	Q_strncpy( value, name, sizeof( value ));
	kvd.szClassName = classname; kvd.szKeyName = key_state; kvd.szValue = value; kvd.fHandled = false;
	svgame.dllFuncs.pfnKeyValue( ent, &kvd );
	kvd.szKeyName = key_initial; kvd.szValue = on; kvd.fHandled = false;
	svgame.dllFuncs.pfnKeyValue( ent, &kvd );
	ent->v.spawnflags = 1; // SF_GLOBAL_SET
	svgame.dllFuncs.pfnSpawn( ent );
	if( !ent->free ) SV_FreeEdict( ent );
	Con_Printf( "cofstory phase=chapter-global state=%s\n", name );
}

// 1.1.9 (I3): a player who joins or comes back after the story took the bag (c_bridge, or a chapter start past it),
// and one a recovery restore gave an older record, is put in three-slot mode before anything is handed to them this
// frame. The state frame (sv_cof_state.c) did it only after the party's gifts and the shared items, which could land
// in slots 4+ first: hidden there in three-slot mode, and counted as carried, they never came again.
static void CoF_StoryThreeSlots( void )
{
	static cof_state_data_t party; // the party's story values, read when someone is not in three slots
	qboolean known = false;
	int i;
	for( i = 0; i < svs.maxclients; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !SV_IsValidEdict( cl->edict ) ||
			!( cl->edict->v.health > 0 ) || cl->edict->v.deadflag != DEAD_NO || SV_CoF_RetailInThreeSlots( cl )) continue;
		if( !known )
		{
			SV_CoF_StateExport( &party );
			known = true;
		}
		if( !party.values.three_slots ) return;
		if( SV_CoF_RetailThreeSlots( cl )) Con_Printf( "cofstory phase=three-slots userid=%d\n", cl->userid );
	}
}

// 1.1.8 (chapter starts): the first frame of the map a chapter start loaded hands out what the story would have
// given by then (sv_cof_chapter_kit.h), once: the cvar is cleared, so later maps, deaths and checkpoints are untouched.
static void CoF_StoryChapterStart( void )
{
	const char *map = Cvar_VariableString( "cof_chapter_start" );
	const cof_chapter_kit_t *kit;
	int i;
	if( !map[0] ) return;
	kit = CoF_ChapterKit( map );
	Con_Printf( "cofstory phase=chapter-start map=%s kit=%d\n", map, kit != NULL );
	if( kit )
	{
		for( i = 0; i < COF_CHAPTER_WEAPONS && kit->weapons[i]; i++ ) SV_CoF_PartyGrant( kit->weapons[i] );
		if( kit->doctor_weapon || kit->three_slots ) SV_CoF_StatePreset( kit->doctor_weapon, kit->three_slots );
		if( kit->global ) CoF_StoryGlobalOn( kit->global );
		cof_story_kit = kit->weapons[0] ? kit : NULL;
		cof_story_kit_generation = svs.spawncount;
		cof_story_kit_until = -1; // set by the first checkpoint it holds back
	}
	// after 1.1.9: the bonus room's items beside the party's start, when the room is open for this game (sv_cof_bonus.c)
	SV_CoF_BonusKitChapterStart( map );
	Cvar_Set( "cof_chapter_start", "" );
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
	CoF_StoryChapterStart(); // before the party and state frames: they hand the kit out
	SV_CoF_GatesFrame(); // first: the game's own rules as this frame wants them
	SV_CoF_UseProbeFrame(); // a door just started: the player where the game's +use saw them
	SV_CoF_RecoveryFrame();
	CoF_StoryThreeSlots(); // after a restore, before the party's gifts and the shared items (1.1.9, I3)
	SV_CoF_SceneFrame();
	SV_CoF_MomentsFrame();
	SV_CoF_RescueFrame();
	SV_CoF_ChairsFrame(); // 1.3.4: after the rescue (a team wipe lets riders out), before the party's moves
	SV_CoF_PartyFrame(); // first: a door's party move lands before the AI decides it lost the leader
	SV_CoF_SectionsFrame(); // merged maps: respawns, companions and gone exits, before the AI decides
	SV_CoF_FogFrame(); // after the section moves
	SV_CoF_LoopsFrame();
	SV_CoF_WeatherFrame();
	SV_CoF_GlobalsFrame();
	SV_CoF_SkyFrame();
	SV_CoF_ViewFrame();
	SV_CoF_CameraModelsFrame();
	SV_CoF_PickupsFrame(); // 1.1.9: map pickups taken settle, copies made, before the AI companions' commands
	SV_CoF_CompanionFrame();
	SV_CoF_LightFrame(); // a co-op torch the host does not allow goes off
	SV_CoF_ArrivalFrame(); // after it: the torches the host allows, as their players switched them
	SV_CoF_BlurFrame(); // a story blur flipped for one player is everyone's
	SV_CoF_TestGunFrame();
	SV_CoF_ExitFrame();
	SV_CoF_ShareFrame();
	SV_CoF_StateFrame();
	SV_CoF_RetailPlayersFrame();
	SV_CoF_SessionFrame();
	SV_CoF_TraceFrame();
	SV_CoF_Inventory12Frame();
	SV_CoF_WeaponModelFrame();
}

void SV_CoF_StoryShutdown( void )
{
	SV_CoF_CommandSender( NULL ); // an engine error inside a command leaves it set
	SV_CoF_UsercmdSender( NULL );
	SV_CoF_GlobalsReset(); // 1.2.6: the same for the per-player globals swap
	SV_CoF_PartyShutdown();
	SV_CoF_SceneReset();
	SV_CoF_MomentsReset();
	SV_CoF_RescueReset();
	SV_CoF_ChairsReset();
	SV_CoF_FogReset();
	SV_CoF_WeatherReset();
	SV_CoF_SkyReset();
	SV_CoF_CompanionReset();
	SV_CoF_TestGunReset();
	SV_CoF_ExitReset();
	SV_CoF_ShareReset();
	SV_CoF_StateReset();
	SV_CoF_RecoveryReset();
	SV_CoF_SessionReset();
	SV_CoF_ArrivalReset();
	cof_story_kit = NULL;
	SV_CoF_PickupsReset();
	CoF_StoryCloseGate();
	SV_CoF_ClearValidation();
}
