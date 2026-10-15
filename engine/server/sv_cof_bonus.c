/* Cry of Fear story co-op: the bonus room switch. GPL-3.0-or-later.
 * cof_bonus_room 1 opens the bonus room of c_start and c_bridge (the door and its
 * items) for this session, 0 keeps it shut, -1 (the default) leaves the game
 * folder's scriptsettings.dat to decide. The file itself is never written:
 * only the buffer the server hands the game is (sv_cof_bonus.h). */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_bonus.h"

static convar_t *cof_bonus_room;
static int cof_bonus_logged = -1; // the spawncount the state was last logged for

void SV_CoF_BonusInit( void )
{
	cof_bonus_room = Cvar_Get( "cof_bonus_room", "-1", 0,
		"story co-op: the bonus room of c_start and c_bridge and its items this session: 1 open, 0 shut, -1 as scriptsettings.dat says" );
}

byte *GAME_EXPORT SV_CoF_BonusLoadFile( const char *filename, int *pLength )
{
	int length = 0, unlocked;
	byte *data = COM_LoadFileForMe( filename, &length );
	size_t made;
	if( pLength ) *pLength = length;
	if( !cof_bonus_room || cof_bonus_room->value < 0.0f || !CoF_BonusIsSettings( filename ) || !SV_CoF_StoryCoopEnabled( ))
		return data;
	unlocked = cof_bonus_room->value > 0.0f;
	if( cof_bonus_logged != svs.spawncount )
	{
		cof_bonus_logged = svs.spawncount;
		Con_Printf( "cofstory phase=bonus-room state=%s file=%s\n", unlocked ? "open" : "shut", data && length > 0 ? "rewritten" : "none" );
	}
	if( data && length > 0 )
	{
		CoF_BonusRewrite( data, (size_t)length, unlocked );
		return data;
	}
	if( data ) free( data );
	if( pLength ) *pLength = 0;
	if( !unlocked ) return NULL; // no file reads as locked
	// no file in this game folder: one with the gated lines open (freed by COM_FreeFile)
	data = malloc( 2048 );
	if( !data ) return NULL;
	made = CoF_BonusSynthesize( data, 2048, true );
	if( !made ) { free( data ); return NULL; }
	if( pLength ) *pLength = (int)made;
	return data;
}
