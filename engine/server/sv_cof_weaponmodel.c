/* Cry of Fear story co-op: the weapon other players see in a player's hands. GPL-3.0-or-later.
 * The rules are in sv_cof_weaponmodel.h; this file precaches the generated models
 * a copy has, works out each player's pick once a frame, puts it in the state the
 * player's entity is sent with, and adds the other hand of a dual pair as a follow
 * entity (no edict: a number at the top of the edict range the game has not
 * reached, sent only to clients that see the player).
 * 1.1.9: the night vision mask on a wearer's head (a follow entity too), a climber's body
 * on the ladder instead of floating hands, and a rower's body left out (P5). */
#include "common.h"
#include "server.h"
#include "const.h"
#include "entity_state.h"
#include "sv_cof_story.h"
#include "sv_cof_retail.h"
#include "sv_cof_weaponmodel.h"
#include "sv_cof_heldlight.h"
#include "sv_cof_cheats.h"

static CVAR_DEFINE_AUTO( cof_coop_heldlights, "1", 0, "Cry of Fear story co-op: a held lantern, lit flare or book laser lights the room for the other players too; 0: only for its holder, as the game does" );
static CVAR_DEFINE_AUTO( cof_coop_gasmask, "1", 0, "Cry of Fear story co-op: the others see the night vision mask on a player wearing it; 0: only its wearer knows, as in the game" );

#define COF_WEAPONMODEL_VIEW_ENT 0x2038 // int m_iViewEntIndex: kept by the retail SET_VIEW wrapper (10117180)
#define COF_WEAPONMODEL_ATTACHED_CAMERA 0x0388 // EHANDLE m_pAttachedCamera: a ladder, the strangle, a model cutscene, a keypad

// sv_cof_camera_models.c (1.1.9): the ladder hands model player entity e climbs with, or NULL
edict_t *SV_CoF_CameraModelLadder( int e );

static struct
{
	int game; // the model index the game sends for this player's weapon
	int model; // what to send instead (0: the game's)
	int sequence; // the body sequence to send (-1: the game's)
	int offhand; // the other hand's model (0: none)
	int light, renderamt, rgb[3]; // a held light the others see (sv_cof_heldlight.h)
	vec3_t light_origin;
	int mask; // 1.1.9: the gas mask the others see (0: none)
	qboolean camera; // 1.1.9: as a viewer, a camera holds this player's view (client.dll then draws no player)
	qboolean climb; // 1.1.9: on a ladder, drawn for the others at climb_origin, climb_angles
	vec3_t climb_origin, climb_angles;
	qboolean boat; // 1.1.9 (P5): rowing the boat, not sent to the others
} cof_weaponmodel_players[MAX_CLIENTS];

// 1.1.9 (P5): a rower's protection by slot (CoF_WeaponModelBoatLeft). It outlives a level change (the transition save
// carries the flags along), not the slot's next player.
static struct { qboolean on; int userid; } cof_weaponmodel_boat[MAX_CLIENTS];

void SV_CoF_WeaponModelInit( void )
{
	Cvar_RegisterVariable( &cof_coop_heldlights );
	Cvar_RegisterVariable( &cof_coop_gasmask );
}

// 1.1.9: client.dll draws no player model while a view entity or an attached camera holds its view (StudioDrawPlayer,
// StudioModelRenderer.cpp:1666-1668), so a follow entity sent then would merge onto whatever bones were drawn last: the
// engine's view entity, the game's (m_iViewEntIndex) or its attached camera (an EHANDLE, checked against the edicts).
static qboolean CoF_WeaponModelOnCamera( sv_client_t *cl )
{
	edict_t *ent = cl->edict, *camera;
	byte *pv;
	int view, serial;
	if( cl->pViewEntity && cl->pViewEntity != ent ) return true;
	if( !( pv = SV_CoF_StoryEntityPrivate( ent, COF_WEAPONMODEL_VIEW_ENT + sizeof( view )))) return false;
	memcpy( &view, pv + COF_WEAPONMODEL_VIEW_ENT, sizeof( view ));
	if( view && view != NUM_FOR_EDICT( ent )) return true;
	memcpy( &camera, pv + COF_WEAPONMODEL_ATTACHED_CAMERA, sizeof( camera ));
	memcpy( &serial, pv + COF_WEAPONMODEL_ATTACHED_CAMERA + sizeof( camera ), sizeof( serial ));
	if( !camera || camera < svgame.edicts || camera >= svgame.edicts + svgame.numEntities ||
		(size_t)((byte *)camera - (byte *)svgame.edicts ) % sizeof( edict_t ))
		return false;
	return !camera->free && camera->serialnumber == serial;
}

static qboolean CoF_WeaponModelViewerOnCamera( const sv_client_t *viewer )
{
	int slot = (int)( viewer - svs.clients );
	if( slot >= 0 && slot < svs.maxclients && slot < MAX_CLIENTS ) return cof_weaponmodel_players[slot].camera;
	return viewer->pViewEntity && viewer->pViewEntity != viewer->edict;
}

// 1.1.9 (ladders): where the others see a climber (CoF_WeaponModelClimbPose).
static void CoF_WeaponModelClimb( int slot, edict_t *ent )
{
	edict_t *hands;
	vec3_t eye, ahead;
	float heading[2];
	if( ent->v.deadflag != DEAD_NO || !( hands = SV_CoF_CameraModelLadder( slot + 1 ))) return;
	VectorCopy( hands->v.origin, eye );
	VectorCopy( hands->v.origin, ahead );
	Mod_StudioGetAttachment( hands, 0, eye, NULL );
	Mod_StudioGetAttachment( hands, 1, ahead, NULL );
	VectorClear( cof_weaponmodel_players[slot].climb_angles );
	if( CoF_WeaponModelClimbPose( eye, ahead, ent->v.view_ofs[2], cof_weaponmodel_players[slot].climb_origin, heading ))
		cof_weaponmodel_players[slot].climb_angles[YAW] = (float)RAD2DEG( atan2( heading[1], heading[0] ));
	else cof_weaponmodel_players[slot].climb_angles[YAW] = hands->v.angles[YAW];
	cof_weaponmodel_players[slot].climb = true;
}

static void CoF_WeaponModelBoatRestore( edict_t *ent )
{
	if( !SV_IsValidEdict( ent )) return;
	ClearBits( ent->v.flags, FL_NOTARGET );
	if( ent->v.takedamage == DAMAGE_NO ) ent->v.takedamage = DAMAGE_AIM; // a player's own (CBasePlayer::Spawn)
}

// 1.1.9 (P5): while the player rows (m_pBoat names a live boat), the others are not sent the body it froze on the
// shore, and it is no target and takes no damage; once the boat lets go, it is put back (CoF_WeaponModelBoatLeft).
static void CoF_WeaponModelBoat( sv_client_t *cl, int slot )
{
	edict_t *ent = cl->edict;
	int client = cl->state == cs_spawned ? 2 : cl->state == cs_connected ? 1 : 0;
	if( client == 2 && SV_IsValidEdict( ent ) && SV_CoF_RetailInBoat( cl ))
	{
		if( !cof_weaponmodel_boat[slot].on || cof_weaponmodel_boat[slot].userid != cl->userid )
			Con_Printf( "cofstory phase=boat-rower-hidden userid=%d\n", cl->userid );
		cof_weaponmodel_boat[slot].on = true;
		cof_weaponmodel_boat[slot].userid = cl->userid;
		cof_weaponmodel_players[slot].boat = true;
		SetBits( ent->v.flags, FL_NOTARGET );
		ent->v.takedamage = DAMAGE_NO;
		return;
	}
	if( !cof_weaponmodel_boat[slot].on ) return;
	switch( CoF_WeaponModelBoatLeft( client, cl->userid == cof_weaponmodel_boat[slot].userid,
		client == 2 && SV_IsValidEdict( ent ) && CoF_WeaponModelOnCamera( cl ), SV_CoF_TransitionActive( )))
	{
	case COF_BOAT_RESTORE:
		CoF_WeaponModelBoatRestore( ent );
		cof_weaponmodel_boat[slot].on = false;
		Con_Printf( "cofstory phase=boat-rower-restored userid=%d\n", cl->userid );
		break;
	case COF_BOAT_RESTORE_AGAIN:
		CoF_WeaponModelBoatRestore( ent );
		break;
	case COF_BOAT_FORGET:
		cof_weaponmodel_boat[slot].on = false;
		break;
	default:
		break;
	}
}

// A follow entity merged onto player entity e (the client draws the player first, then merges the bones by name).
static void CoF_WeaponModelFollowState( entity_state_t *out, int number, int e, const edict_t *player, int model )
{
	memset( out, 0, sizeof( *out ));
	out->number = number;
	out->entityType = ENTITY_NORMAL;
	out->modelindex = model;
	out->movetype = MOVETYPE_FOLLOW;
	out->aiment = e;
	out->solid = SOLID_NOT;
	VectorCopy( player->v.origin, out->origin );
	VectorCopy( player->v.angles, out->angles );
	out->rendermode = kRenderNormal;
	out->renderamt = 255;
	out->scale = 1.0f;
	out->animtime = (float)sv.time;
}

// The light of what the player holds, for the others (sv_cof_heldlight.h).
static void CoF_WeaponModelHeldLight( sv_client_t *cl, int slot, int id, int right, int left )
{
	edict_t *ent = cl->edict;
	vec3_t forward, side, up;
	int flags = SV_CoF_RetailFlashFlags( cl );
	if( cof_coop_heldlights.value == 0.0f || flags < 0 || FBitSet( ent->v.effects, EF_NODRAW ) || ent->v.deadflag != DEAD_NO ) return;
	if( !( cof_weaponmodel_players[slot].light = CoF_HeldLight( id, right, left, flags ))) return;
	CoF_HeldLightLook( cof_weaponmodel_players[slot].light, COM_RandomLong( 0, 999 ), COM_RandomLong( 0, 999 ), COM_RandomLong( 0, 999 ),
		&cof_weaponmodel_players[slot].renderamt, cof_weaponmodel_players[slot].rgb );
	AngleVectors( ent->v.v_angle, forward, side, up );
	CoF_HeldLightOrigin( ent->v.origin, ent->v.view_ofs, forward, side, cof_weaponmodel_players[slot].light_origin );
}

// A precached model's index; never registers one.
static int CoF_WeaponModelIndex( const char *path )
{
	int i;
	if( !path || !path[0] ) return 0;
	for( i = 1; i < MAX_MODELS && sv.model_precache[i][0]; i++ )
		if( CoF_WeaponModelSame( sv.model_precache[i], path )) return i;
	return 0;
}

static int CoF_WeaponModelPrecached( const char *path, void *ctx )
{
	(void)ctx;
	return CoF_WeaponModelIndex( path ) > 0;
}

void SV_CoF_WeaponModelActivate( void )
{
	int i, n = 0;
	memset( cof_weaponmodel_players, 0, sizeof( cof_weaponmodel_players ));
	if( !SV_CoF_StoryCoopEnabled( )) return;
	// only the models this copy has, while the map loads: not fatal for a client
	// without one (it sees the game's own model), unlike the game's precache
	for( i = 0; CoF_WeaponModelPath( i ); i++ )
	{
		const char *path = CoF_WeaponModelPath( i );
		if( !path[0] || CoF_WeaponModelIndex( path ) || !FS_FileExists( path, false )) continue;
		SV_ModelIndex( path );
		n++;
	}
	Con_Printf( "cofstory phase=weapon-models precached=%d\n", n );
}

void SV_CoF_WeaponModelFrame( void )
{
	int i, mask = cof_coop_gasmask.value != 0.0f ? CoF_WeaponModelIndex( COF_WEAPONMODEL_MASK ) : 0;
	for( i = 0; i < svs.maxclients && i < MAX_CLIENTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_weaponmodel_pick_t pick;
		int id = 0, right = 0, left = 0;
		const char *game;
		edict_t *ent;
		memset( &cof_weaponmodel_players[i], 0, sizeof( cof_weaponmodel_players[i] ));
		cof_weaponmodel_players[i].sequence = COF_WEAPONMODEL_KEEP;
		CoF_WeaponModelBoat( cl, i ); // first: a rower who dropped is put back too
		if( cl->state != cs_spawned || !SV_IsValidEdict( cl->edict )) continue;
		ent = cl->edict;
		cof_weaponmodel_players[i].camera = CoF_WeaponModelOnCamera( cl );
		CoF_WeaponModelClimb( i, ent );
		// read every frame (a save, a death, a late joiner, the wheelchair all end up in the flags)
		if( mask && CoF_WeaponModelMasked( SV_CoF_RetailFlashFlags( cl ), ent->v.deadflag, FBitSet( ent->v.effects, EF_NODRAW ),
			cof_weaponmodel_players[i].climb ))
			cof_weaponmodel_players[i].mask = mask;
		if( !SV_CoF_RetailHeld( cl, &id, &right, &left )) continue;
		if( !cof_weaponmodel_players[i].climb ) CoF_WeaponModelHeldLight( cl, i, id, right, left );
		game = cl->edict->v.weaponmodel ? SV_GetString( cl->edict->v.weaponmodel ) : "";
		if( !CoF_WeaponModelPick( game, id, right, left, CoF_WeaponModelPrecached, NULL, &pick )) continue;
		cof_weaponmodel_players[i].game = CoF_WeaponModelIndex( game );
		cof_weaponmodel_players[i].model = CoF_WeaponModelIndex( pick.model );
		if( CoF_WeaponModelStanding( cl->edict->v.deadflag, cl->edict->v.fuser3, FBitSet( cl->edict->v.flags, FL_DUCKING ),
			cl->edict->v.movetype == MOVETYPE_WALK, cl->edict->v.waterlevel, SV_CoF_RetailOwnAnimation( cl ),
			CoF_WeaponModelSprinting( cl->edict->v.flags, cl->edict->v.velocity[0], cl->edict->v.velocity[1] )))
			cof_weaponmodel_players[i].sequence = pick.sequence;
		if( !cof_weaponmodel_players[i].climb ) cof_weaponmodel_players[i].offhand = CoF_WeaponModelIndex( pick.offhand );
	}
}

void SV_CoF_WeaponModelState( entity_state_t *state, int e )
{
	int slot = e - 1;
	if( !state || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return;
	// only while the game still sends what the pick was made for (a switch this frame waits a frame)
	if( state->weaponmodel != cof_weaponmodel_players[slot].game ) return;
	if( cof_weaponmodel_players[slot].model ) state->weaponmodel = cof_weaponmodel_players[slot].model;
	if( cof_weaponmodel_players[slot].sequence >= 0 && ( cof_weaponmodel_players[slot].model || cof_weaponmodel_players[slot].offhand ))
		state->sequence = cof_weaponmodel_players[slot].sequence;
}

int SV_CoF_WeaponModelFollower( const sv_client_t *viewer, int e, entity_state_t *out )
{
	int slot = e - 1, number;
	edict_t *player;
	if( !viewer || !out || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return 0;
	if( !cof_weaponmodel_players[slot].offhand ) return 0;
	// CoF hides every player while the viewer watches a camera: a follower would merge
	// onto whatever was drawn last (1.1.9: the game's view entity and attached camera too)
	if( CoF_WeaponModelViewerOnCamera( viewer )) return 0;
	player = svs.clients[slot].edict;
	if( !SV_IsValidEdict( player ) || FBitSet( player->v.effects, EF_NODRAW )) return 0;
	number = CoF_WeaponModelNumber( COF_WEAPONMODEL_OFFHAND, slot, svgame.globals->maxEntities, svs.maxclients, svgame.numEntities );
	if( !number ) return 0; // the game uses that many edicts
	CoF_WeaponModelFollowState( out, number, e, player, cof_weaponmodel_players[slot].offhand );
	return 1;
}

// 1.1.9: the gas mask of player entity e, merged on its head, for every viewer but its wearer (whose client shows the
// game's own) and none on a camera (as the other hand); 0: none.
int SV_CoF_WeaponModelMask( const sv_client_t *viewer, int e, entity_state_t *out )
{
	int slot = e - 1, number;
	edict_t *player;
	if( !viewer || !out || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return 0;
	if( !cof_weaponmodel_players[slot].mask || viewer == &svs.clients[slot] || CoF_WeaponModelViewerOnCamera( viewer )) return 0;
	player = svs.clients[slot].edict;
	if( !SV_IsValidEdict( player ) || FBitSet( player->v.effects, EF_NODRAW )) return 0;
	number = CoF_WeaponModelNumber( COF_WEAPONMODEL_GASMASK, slot, svgame.globals->maxEntities, svs.maxclients, svgame.numEntities );
	if( !number ) return 0;
	CoF_WeaponModelFollowState( out, number, e, player, cof_weaponmodel_players[slot].mask );
	return 1;
}

// 1.1.9 (P5): player entity e is not sent to viewer: they row the boat (its model shows the rower). Their own client
// still gets it, as before.
qboolean SV_CoF_WeaponModelHidden( const edict_t *viewer, int e )
{
	int slot = e - 1;
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return false;
	return cof_weaponmodel_players[slot].boat && viewer != svs.clients[slot].edict;
}

// 1.1.9 (ladders): player entity e climbs a ladder and viewer is someone else: the body is drawn on the ladder, away
// from where it stands (the caller sends it without the visibility test of where it stands).
qboolean SV_CoF_WeaponModelClimbing( const edict_t *viewer, int e )
{
	int slot = e - 1;
	if( slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return false;
	return cof_weaponmodel_players[slot].climb && viewer != svs.clients[slot].edict;
}

// 1.1.9 (ladders): a climber's state for another viewer: on the ladder, empty-handed, in the climbing pose. The
// climber's own state is never changed (their view is the hands model's).
void SV_CoF_WeaponModelClimb( entity_state_t *state, const edict_t *viewer )
{
	int slot;
	if( !state || !SV_CoF_WeaponModelClimbing( viewer, state->number )) return;
	slot = state->number - 1;
	VectorCopy( cof_weaponmodel_players[slot].climb_origin, state->origin );
	VectorCopy( cof_weaponmodel_players[slot].climb_angles, state->angles );
	state->weaponmodel = 0;
	state->sequence = COF_WEAPONMODEL_CLIMB_SEQUENCE;
	state->gaitsequence = 0;
}

// The light of a held lantern, flare or book laser of player entity e, for every viewer but its holder (whose client
// draws its own); a model-less renderfx 71 entity at the top of the edict range, below the followers. 0: none.
int SV_CoF_WeaponModelLight( const sv_client_t *viewer, int e, entity_state_t *out )
{
	int slot = e - 1, number;
	edict_t *player;
	if( !viewer || !out || slot < 0 || slot >= svs.maxclients || slot >= MAX_CLIENTS || !SV_CoF_StoryCoopEnabled( )) return 0;
	if( !cof_weaponmodel_players[slot].light || viewer == &svs.clients[slot] ) return 0;
	if( viewer->pViewEntity && viewer->pViewEntity != viewer->edict ) return 0; // as the followers: hidden in a camera
	player = svs.clients[slot].edict;
	if( !SV_IsValidEdict( player ) || !player->v.modelindex ) return 0;
	number = CoF_WeaponModelNumber( COF_WEAPONMODEL_LIGHT, slot, svgame.globals->maxEntities, svs.maxclients, svgame.numEntities );
	if( !number ) return 0;
	memset( out, 0, sizeof( *out ));
	out->number = number;
	out->entityType = ENTITY_NORMAL;
	out->modelindex = player->v.modelindex; // never drawn: renderfx 71 is light only
	out->solid = SOLID_NOT;
	VectorCopy( cof_weaponmodel_players[slot].light_origin, out->origin );
	out->rendermode = kRenderNormal;
	out->renderfx = COF_HELDLIGHT_RENDERFX;
	out->renderamt = cof_weaponmodel_players[slot].renderamt;
	out->rendercolor.r = (byte)cof_weaponmodel_players[slot].rgb[0];
	out->rendercolor.g = (byte)cof_weaponmodel_players[slot].rgb[1];
	out->rendercolor.b = (byte)cof_weaponmodel_players[slot].rgb[2];
	out->scale = 0.0f; // not a spotlight
	out->animtime = (float)sv.time;
	return 1;
}
