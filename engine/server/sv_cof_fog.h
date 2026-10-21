/*
sv_cof_fog.h - Cry of Fear story co-op: which fog a player should have (sv_cof_fog.c)
GPL-3.0-or-later.

env_fog (CEnvFog, the only SetFog sender in hl.dll; SendData 1009B650) sends SetFog( byte r, g, b, short fadetime,
short start, short end; all zero = off; fadetime > 0 fades in, < 0 out ) MSG_ONE to every player, and only when it
turns on, fades or turns off; the client clears its fog on every ResetHUD and InitHUD (docs/research/2026-10-18-
cof-decompilation-survey.md section 3, the launcher repo). A section of a merged map is -1 when the map is not merged.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_FOG_H
#define SV_COF_FOG_H

#if defined( _MSC_VER )
#define COF_FOG_FN static __inline
#else
#define COF_FOG_FN static inline
#endif

#define COF_FOG_PAYLOAD 9

typedef struct
{
	int section; // of a merged map (-1: not merged)
	int active, fading; // SF_FOG_ACTIVE (1), SF_FOG_FADING (0x8000)
	float color[3]; // pev->rendercolor
	int start, end; // m_iStartDist, m_iEndDist
} cof_fog_t;

COF_FOG_FN void CoF_FogPayload( byte *out, const float *color, int fade, int start, int end )
{
	out[0] = (byte)(int)color[0]; out[1] = (byte)(int)color[1]; out[2] = (byte)(int)color[2];
	out[3] = (byte)( fade & 0xFF ); out[4] = (byte)(( fade >> 8 ) & 0xFF );
	out[5] = (byte)( start & 0xFF ); out[6] = (byte)(( start >> 8 ) & 0xFF );
	out[7] = (byte)( end & 0xFF ); out[8] = (byte)(( end >> 8 ) & 0xFF );
}

COF_FOG_FN int CoF_FogHere( const cof_fog_t *f, int section )
{
	return section < 0 || f->section == section;
}

// The game's SetFog fits a player in section: it is one of that section's fogs (on, fading in or out), or an off
// while none there is on. Not merged (-1): any of the game's own.
COF_FOG_FN int CoF_FogFits( const cof_fog_t *fogs, int count, int section, const byte *payload )
{
	static const byte off[COF_FOG_PAYLOAD] = { 0 };
	int i;
	if( section < 0 ) return 1;
	if( !memcmp( payload, off, COF_FOG_PAYLOAD ))
	{
		for( i = 0; i < count; i++ )
			if( CoF_FogHere( &fogs[i], section ) && fogs[i].active ) return 0;
		return 1;
	}
	for( i = 0; i < count; i++ )
	{
		byte mine[COF_FOG_PAYLOAD];
		if( !CoF_FogHere( &fogs[i], section )) continue;
		CoF_FogPayload( mine, fogs[i].color, 0, fogs[i].start, fogs[i].end );
		if( !memcmp( payload, mine, 3 ) && !memcmp( payload + 5, mine + 5, 4 )) return 1;
	}
	return 0;
}

// The fog a player in section should have now, at once: 1 with its payload, 0 for none (the payload all zero).
COF_FOG_FN int CoF_FogWanted( const cof_fog_t *fogs, int count, int section, byte *out )
{
	int i;
	memset( out, 0, COF_FOG_PAYLOAD );
	for( i = 0; i < count; i++ )
	{
		if( !CoF_FogHere( &fogs[i], section ) || !fogs[i].active ) continue;
		CoF_FogPayload( out, fogs[i].color, 0, fogs[i].start, fogs[i].end );
		return 1;
	}
	return 0;
}

// Whether a player's frame sends the fog they should have (wanted: there is one; else an off): after a ResetHUD or
// InitHUD only a fog that is on; on a section move, either; on their first frame in a new map, either (1.1.8, E2-6:
// a story level change sends no ResetHUD or InitHUD, and a START_ON fog went out only to the players already in;
// an off also clears a fog the client kept from the map before).
COF_FOG_FN int CoF_FogSend( int replay, int moved, int arrived, int wanted )
{
	return ( replay && wanted ) || moved || arrived;
}

#endif // SV_COF_FOG_H
