/*
sv_cof_weather.h - Cry of Fear story co-op: scripted weather for every player (sv_cof_weather.c)
GPL-3.0-or-later.

The rain lives in each player object (CBasePlayer Rain_*, 0x27DC..0x280C). A named rain_modify, when fired, writes
only player 1's (CRainModify::Use 100A99A0, INDEXENT( 1 )): directly, or as the ideal values of a fade that the
player's own PostThink walks once a second; PostThink sends RainData to that player whenever Rain_needsUpdate is set
(player.cpp:9656-9760 in the decompilation). Every other player is given player 1's rain when what it is heading for
differs: the ideal values while fading, the current ones otherwise; each player then fades on its own.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_WEATHER_H
#define SV_COF_WEATHER_H

#if defined( _MSC_VER )
#define COF_WEATHER_FN static __inline
#else
#define COF_WEATHER_FN static inline
#endif

#define COF_RAIN_BLOCK        0x27DC // int Rain_dripsPerSecond, the first of cof_rain_t
#define COF_RAIN_NEEDS_UPDATE 0x280C // int Rain_needsUpdate: PostThink sends RainData
#define COF_RAIN_PLAYER_NEED  0x2810

typedef struct
{
	int drips;
	float wind_x, wind_y, rand_x, rand_y;
	int ideal_drips;
	float ideal_wind_x, ideal_wind_y, ideal_rand_x, ideal_rand_y;
	float end_fade;  // a fade under way until then (0: none)
	float next_fade; // its next once-a-second step
} cof_rain_t;

// The rain a player is heading for: the ideal values while fading, the current ones otherwise.
COF_WEATHER_FN void CoF_RainTarget( const cof_rain_t *r, float out[5] )
{
	int fading = r->end_fade > 0.0f;
	out[0] = (float)( fading ? r->ideal_drips : r->drips );
	out[1] = fading ? r->ideal_wind_x : r->wind_x;
	out[2] = fading ? r->ideal_wind_y : r->wind_y;
	out[3] = fading ? r->ideal_rand_x : r->rand_x;
	out[4] = fading ? r->ideal_rand_y : r->rand_y;
}

// Give player (a CBasePlayer's private data) the rain of player 1 (host) when they head for different rain;
// 1 when it was copied and RainData asked for.
COF_WEATHER_FN int CoF_RainMirror( const unsigned char *host, unsigned char *player )
{
	cof_rain_t a, b;
	float ta[5], tb[5];
	int update = 1;
	memcpy( &a, host + COF_RAIN_BLOCK, sizeof( a ));
	memcpy( &b, player + COF_RAIN_BLOCK, sizeof( b ));
	CoF_RainTarget( &a, ta );
	CoF_RainTarget( &b, tb );
	if( !memcmp( ta, tb, sizeof( ta ))) return 0;
	memcpy( player + COF_RAIN_BLOCK, &a, sizeof( a ));
	memcpy( player + COF_RAIN_NEEDS_UPDATE, &update, sizeof( update ));
	return 1;
}

#endif // SV_COF_WEATHER_H
