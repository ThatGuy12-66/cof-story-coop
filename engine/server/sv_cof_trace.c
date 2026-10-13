/* Opt-in Cry of Fear story co-op: verbose story log. GPL-3.0-or-later.
 * Logs what the game does by itself (commands, player state, a few user
 * messages) so a live test can be checked from the log. Inert unless story
 * co-op is on and cof_story_trace is non-zero. */
#include "common.h"
#include "server.h"
#include "sv_cof_story.h"
#include "sv_cof_trace.h"

#define COF_TRACE_SLOTS 32
#define COF_TRACE_CMD_MAX 32
#define COF_TRACE_ARGS_MAX 96
#define COF_TRACE_MSG_MAX 160
#define COF_TRACE_RATE 20

typedef struct
{
	int userid;
	int known;
	int frozen, hidden, alive;
	char camera[32];
} cof_trace_slot_t;

typedef struct
{
	double window;
	int lines;
	int suppressed;
} cof_trace_rate_t;

static convar_t *cof_story_trace;
static cof_trace_slot_t cof_trace_slots[COF_TRACE_SLOTS];
static cof_trace_rate_t cof_trace_rates[COF_TRACE_SLOTS];
static int cof_trace_spawncount;

static struct
{
	qboolean open;
	const char *name;
	int dest;
	edict_t *ent;
	char text[512];
	size_t length;
	int strings, filled;
} cof_trace_msg;

void SV_CoF_TraceInit( void )
{
	cof_story_trace = Cvar_Get( "cof_story_trace", "1", 0, "story co-op: log players' game commands, state changes and story messages" );
}

static qboolean CoF_TraceOn( void )
{
	return cof_story_trace && cof_story_trace->value != 0.0f && SV_CoF_StoryCoopEnabled();
}

// Copies in with every byte made safe for one log line. Names keep only the
// characters a console command can legitimately be made of.
static void CoF_TraceClean( char *out, size_t size, const char *in, size_t max, qboolean name )
{
	size_t n = 0;

	if( !in ) in = "";
	for( ; *in && n < max && n + 1 < size; in++, n++ )
	{
		unsigned char c = (unsigned char)*in;

		if( name )
			out[n] = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '+' || c == '-' ? (char)c : '?';
		else if( c == '"' ) out[n] = '\'';
		else out[n] = c < 0x20 || c > 0x7E ? '?' : (char)c;
	}
	if( *in && !name && n + 4 <= size )
	{
		memcpy( out + n, "...", 3 );
		n += 3;
	}
	out[n] = 0;
}

void SV_CoF_TraceCommand( sv_client_t *cl )
{
	char name[COF_TRACE_CMD_MAX + 1], joined[512], args[COF_TRACE_ARGS_MAX + 4];
	cof_trace_rate_t *rate;
	size_t length = 0;
	int i, slot;

	if( !CoF_TraceOn() || !cl || cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT )) return;
	// The voice handshake: every client sends both every few seconds, all game long.
	if( !Q_stricmp( Cmd_Argv( 0 ), "VModEnable" ) || !Q_stricmp( Cmd_Argv( 0 ), "vban" )) return;
	slot = (int)( cl - svs.clients );
	if( slot < 0 || slot >= COF_TRACE_SLOTS ) return;

	// a held key or a script must not flood the log; the count is reported later
	rate = &cof_trace_rates[slot];
	if( sv.time >= rate->window + 1.0 || sv.time < rate->window )
	{
		if( rate->suppressed )
			Con_Printf( "cofstory phase=cmd-suppressed userid=%d count=%d\n", cl->userid, rate->suppressed );
		rate->window = sv.time;
		rate->lines = rate->suppressed = 0;
	}
	if( rate->lines >= COF_TRACE_RATE )
	{
		rate->suppressed++;
		return;
	}
	rate->lines++;

	CoF_TraceClean( name, sizeof( name ), Cmd_Argv( 0 ), COF_TRACE_CMD_MAX, true );
	if( !Q_strcmp( name, "say" ) || !Q_strcmp( name, "say_team" ))
	{
		// chat is private to the players
		Con_Printf( "cofstory phase=cmd userid=%d cmd=%s args=\"<chat>\"\n", cl->userid, name );
		return;
	}
	joined[0] = 0;
	for( i = 1; i < Cmd_Argc() && length + 1 < sizeof( joined ); i++ )
	{
		const char *arg = Cmd_Argv( i );

		if( i > 1 ) joined[length++] = ' ';
		while( *arg && length + 1 < sizeof( joined )) joined[length++] = *arg++;
		joined[length] = 0;
	}
	CoF_TraceClean( args, sizeof( args ), joined, COF_TRACE_ARGS_MAX, false );
	Con_Printf( "cofstory phase=cmd userid=%d cmd=%s args=\"%s\"\n", cl->userid, name, args );
}

void SV_CoF_TraceFrame( void )
{
	int i;

	if( !CoF_TraceOn( ))
	{
		memset( cof_trace_slots, 0, sizeof( cof_trace_slots ));
		return;
	}
	// a level change would otherwise print every player as changed
	if( cof_trace_spawncount != svs.spawncount )
	{
		memset( cof_trace_slots, 0, sizeof( cof_trace_slots ));
		cof_trace_spawncount = svs.spawncount;
	}
	for( i = 0; i < svs.maxclients && i < COF_TRACE_SLOTS; i++ )
	{
		sv_client_t *cl = &svs.clients[i];
		cof_trace_slot_t *slot = &cof_trace_slots[i], now;
		char changed[48];

		if( cl->state != cs_spawned || FBitSet( cl->flags, FCL_FAKECLIENT ) || !cl->edict )
		{
			slot->known = 0;
			continue;
		}
		memset( &now, 0, sizeof( now ));
		now.userid = cl->userid;
		now.known = 1;
		now.frozen = FBitSet( cl->edict->v.flags, FL_FROZEN ) != 0;
		now.hidden = FBitSet( cl->edict->v.effects, EF_NODRAW ) != 0;
		now.alive = cl->edict->v.health > 0 && cl->edict->v.deadflag == DEAD_NO;
		Q_strncpy( now.camera, "none", sizeof( now.camera ));
		if( cl->pViewEntity && cl->pViewEntity != cl->edict && SV_IsValidEdict( cl->pViewEntity ))
			CoF_TraceClean( now.camera, sizeof( now.camera ), SV_GetString( cl->pViewEntity->v.classname ), 31, true );

		if( slot->known && slot->userid == now.userid )
		{
			changed[0] = 0;
			if( slot->frozen != now.frozen ) Q_strncat( changed, ",frozen", sizeof( changed ));
			if( Q_strcmp( slot->camera, now.camera )) Q_strncat( changed, ",camera", sizeof( changed ));
			if( slot->hidden != now.hidden ) Q_strncat( changed, ",hidden", sizeof( changed ));
			if( slot->alive != now.alive ) Q_strncat( changed, ",alive", sizeof( changed ));
			if( changed[0] )
				Con_Printf( "cofstory phase=state userid=%d frozen=%d camera=%s hidden=%d alive=%d changed=%s\n",
					now.userid, now.frozen, now.camera, now.hidden, now.alive, changed + 1 );
		}
		*slot = now;
	}
}

static qboolean CoF_TraceWatched( const char *name )
{
	static const char *const watch[] = { "ProFont", "Padlock", "Keypad", "Computer", "NoteDoc", "UserDoc", "AddNote", "Objectives", "ScreenFade" };
	int i;

	if( !name ) return false;
	for( i = 0; i < (int)( sizeof( watch ) / sizeof( watch[0] )); i++ )
		if( !Q_strcmp( name, watch[i] )) return true;
	return false;
}

void SV_CoF_TraceMsgBegin( const char *name, int dest, edict_t *ent )
{
	// a capture left open by an earlier message must never leak into this one
	cof_trace_msg.open = false;
	if( !CoF_TraceOn() || !CoF_TraceWatched( name )) return;
	cof_trace_msg.open = true;
	cof_trace_msg.name = name;
	cof_trace_msg.dest = dest;
	cof_trace_msg.ent = ent;
	cof_trace_msg.text[0] = 0;
	cof_trace_msg.length = 0;
	cof_trace_msg.strings = cof_trace_msg.filled = 0;
}

static void CoF_TraceMsgAppend( const char *field )
{
	size_t n = Q_strlen( field );

	// past the printed width nothing is shown, so stop collecting
	if( !cof_trace_msg.open || cof_trace_msg.length + n + 2 > sizeof( cof_trace_msg.text )) return;
	if( cof_trace_msg.length ) cof_trace_msg.text[cof_trace_msg.length++] = ',';
	memcpy( cof_trace_msg.text + cof_trace_msg.length, field, n + 1 );
	cof_trace_msg.length += n;
}

void SV_CoF_TraceMsgByte( int value )
{
	char field[16];

	if( !cof_trace_msg.open ) return;
	Q_snprintf( field, sizeof( field ), "%d", value );
	CoF_TraceMsgAppend( field );
}

void SV_CoF_TraceMsgShort( int value )
{
	SV_CoF_TraceMsgByte( value );
}

void SV_CoF_TraceMsgString( const char *s )
{
	char clean[COF_TRACE_MSG_MAX + 8], field[COF_TRACE_MSG_MAX + 12];

	if( !cof_trace_msg.open ) return;
	cof_trace_msg.strings++;
	if( s && s[0] ) cof_trace_msg.filled++;
	CoF_TraceClean( clean, sizeof( clean ), s, COF_TRACE_MSG_MAX, false );
	Q_snprintf( field, sizeof( field ), "\"%s\"", clean );
	CoF_TraceMsgAppend( field );
}

void SV_CoF_TraceMsgOther( void )
{
	CoF_TraceMsgAppend( "?" );
}

void SV_CoF_TraceMsgEnd( void )
{
	char to[16] = "all", data[COF_TRACE_MSG_MAX + 4];

	if( !cof_trace_msg.open ) return;
	cof_trace_msg.open = false;
	// the game re-sends all 16 note slots at every spawn; the empty ones are noise
	if( !Q_strcmp( cof_trace_msg.name, "AddNote" ) && cof_trace_msg.strings && !cof_trace_msg.filled ) return;
	if( cof_trace_msg.dest == MSG_ONE || cof_trace_msg.dest == MSG_ONE_UNRELIABLE )
	{
		sv_client_t *cl = SV_ClientFromEdict( cof_trace_msg.ent, false );

		if( cl ) Q_snprintf( to, sizeof( to ), "%d", cl->userid );
		else Q_strncpy( to, "?", sizeof( to ));
	}
	Q_strncpy( data, cof_trace_msg.text, COF_TRACE_MSG_MAX + 1 );
	if( cof_trace_msg.length > COF_TRACE_MSG_MAX ) Q_strncat( data, "...", sizeof( data ));
	Con_Printf( "cofstory phase=msg name=%s to=%s data=%s\n", cof_trace_msg.name, to, data );
}
