/*
sv_cof_blur.h - Cry of Fear story co-op: one screen blur for the whole party (sv_cof_blur.c)
GPL-3.0-or-later.

The game's cof_blur flips m_iBlurActive of the player who fired it (CCofBlur::Use, hl.dll 100A5BB0). The field is
saved with the player, UpdateClientData sends it as "Blur", and client.dll draws it as a full-screen smear
(gl_screenblur, on by default). The maps pair the flips, on in one sequence and off in a later one, so in co-op a flip
that goes to another player leaves both blurred for good: c_nightmare's start turns it on for the host, its exit
"off" for the guest who gets there first, and both carry it into c_start, whose four flips change nothing (a player's
log, 2026-10-20). Here the blur is one state the party shares: a flip by anyone sets everyone, and a player who joins
or arrives takes the party's. A morphine trip (MorphineTrip 100E8D70: m_bIsTripping, the blur on for its 30 s) stays
the tripper's own; when it ends they take the party's again.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_BLUR_H
#define SV_COF_BLUR_H

#include <string.h>

#if defined( _MSC_VER )
#define COF_BLUR_FN static __inline
#else
#define COF_BLUR_FN static inline
#endif

#define COF_BLUR_PLAYERS 32

// Why a player is given the party's blur.
#define COF_BLUR_KEEP     0
#define COF_BLUR_FLIP     1 // another player's flip
#define COF_BLUR_JOIN     2 // joined, arrived, or back on this map
#define COF_BLUR_TRIP_END 3 // their morphine trip ended (StopMorphineTrip clears the blur)

typedef struct
{
	int present; // a spawned player with the game's player data
	int blur;    // m_iBlurActive this frame
	int trip;    // m_bIsTripping this frame
} cof_blur_now_t;

typedef struct
{
	int party; // the party's blur, -1 until a player is seen on this map
	int known[COF_BLUR_PLAYERS], blur[COF_BLUR_PLAYERS], trip[COF_BLUR_PLAYERS]; // last frame's, for a player seen
} cof_blur_state_t;

// Each map: the first player seen brings the party's blur with them (carried in or restored with their save).
COF_BLUR_FN void CoF_BlurReset( cof_blur_state_t *s )
{
	memset( s, 0, sizeof( *s ));
	s->party = -1;
}

// One frame. set[i] is the blur player i is to be given (-1: left as it is) and why[i] the reason. Returns the slot
// whose own flip set the party's blur this frame (the lowest, if two flipped at once), or -1.
COF_BLUR_FN int CoF_BlurShare( cof_blur_state_t *s, const cof_blur_now_t *now, int count, int *set, int *why )
{
	int i, source = -1;
	if( count > COF_BLUR_PLAYERS ) count = COF_BLUR_PLAYERS;
	for( i = 0; i < count; i++ )
	{
		set[i] = -1;
		why[i] = COF_BLUR_KEEP;
		if( !now[i].present ) s->known[i] = 0;
	}
	// A change in a player already seen that is not a trip starting or ending: cof_blur's flip. On a tripper it
	// still flips the party.
	for( i = 0; i < count && source < 0; i++ )
	{
		if( !now[i].present || !s->known[i] || ( now[i].blur != 0 ) == s->blur[i] || ( now[i].trip != 0 ) != s->trip[i] )
			continue;
		s->party = now[i].trip ? !s->party : ( now[i].blur != 0 );
		source = i;
	}
	for( i = 0; i < count; i++ )
	{
		int reason = COF_BLUR_FLIP;
		if( !now[i].present ) continue;
		if( !s->known[i] ) reason = COF_BLUR_JOIN;
		else if( s->trip[i] && !now[i].trip ) reason = COF_BLUR_TRIP_END;
		if( s->party < 0 ) s->party = now[i].trip ? 0 : ( now[i].blur != 0 );
		if( i != source && !now[i].trip && ( now[i].blur != 0 ) != s->party )
		{
			set[i] = s->party;
			why[i] = reason;
		}
		s->known[i] = 1;
		s->blur[i] = set[i] >= 0 ? set[i] : ( now[i].blur != 0 );
		s->trip[i] = now[i].trip != 0;
	}
	return source;
}

#endif // SV_COF_BLUR_H
