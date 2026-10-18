/*
sv_cof_voice.h - Cry of Fear story co-op: Simon's voice instead of the police officer's (sv_cof_voice.c)
GPL-3.0-or-later.

In multiplayer the game plays the police officer's samples for a player's pain, death, melee swing, morphine,
low health, running out of breath and sprint breathing (CBasePlayer::Pain 100E9EB0, DeathSound 100DDC60 and the
others; player.cpp:1374-1440, 13178-13335 in the decompilation), and precaches only that set (ClientPrecache).
Each police set here maps to Simon's single-player set, a random one of which is played instead, as single player
picks one. The multiplayer voice commands (player/police/adlib) have no Simon counterpart and stay.
Research: docs/research/2026-10-18-cof-decompilation-survey.md section 4 (the launcher repo).

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_VOICE_H
#define SV_COF_VOICE_H

#if defined( _MSC_VER )
#define COF_VOICE_FN static __inline
#else
#define COF_VOICE_FN static inline
#endif

typedef struct
{
	const char *police; // the game's multiplayer sample, up to its number
	const char *simon;  // the single-player sample, %d its number from 1
	int count;
} cof_voice_set_t;

static const cof_voice_set_t cof_voice_sets[] =
{
	{ "player/police/Pain/Pain", "player/vocal/Pain/Pain%d.wav", 16 },
	{ "player/police/LowHealth/lhealth", "player/vocal/LowHealth/lhealth%d.wav", 5 },
	{ "player/police/Swing/Swing", "player/vocal/Swing/Swing%d.wav", 10 },
	{ "player/police/Death/Death", "player/vocal/Death/Death%d.wav", 7 },
	{ "player/police/MorphineInjection/Morphine", "player/vocal/MorphineInjection/Morphine%d.wav", 8 },
	{ "player/police/player_out_of_breath", "player/player_out_of_breath.wav", 1 },
	{ "player/police/sprint_breathe", "player/sprint/sprint_breathe%d.wav", 5 },
};

COF_VOICE_FN int CoF_VoiceSets( void )
{
	return (int)( sizeof( cof_voice_sets ) / sizeof( cof_voice_sets[0] ));
}

// The set of a police sample: its prefix (any case: the game's names and the files differ), a number, ".wav"; or -1.
COF_VOICE_FN int CoF_VoiceSet( const char *sample )
{
	int set;
	for( set = 0; sample && set < CoF_VoiceSets(); set++ )
	{
		const char *p = cof_voice_sets[set].police, *s = sample;
		while( *p && tolower( (unsigned char)*p ) == tolower( (unsigned char)*s )) p++, s++;
		if( *p || !isdigit( (unsigned char)*s )) continue;
		while( isdigit( (unsigned char)*s )) s++;
		if( tolower( (unsigned char)s[0] ) == '.' && tolower( (unsigned char)s[1] ) == 'w' && tolower( (unsigned char)s[2] ) == 'a' &&
			tolower( (unsigned char)s[3] ) == 'v' && !s[4] )
			return set;
	}
	return -1;
}

COF_VOICE_FN int CoF_VoiceCount( int set )
{
	return set >= 0 && set < CoF_VoiceSets() ? cof_voice_sets[set].count : 0;
}

// Simon's sample n (0 .. count - 1) of a set.
COF_VOICE_FN void CoF_VoiceName( int set, int n, char *out, size_t size )
{
	if( !size ) return;
	out[0] = 0;
	if( n < 0 || n >= CoF_VoiceCount( set )) return;
	snprintf( out, size, cof_voice_sets[set].simon, n + 1 );
}

#endif // SV_COF_VOICE_H
