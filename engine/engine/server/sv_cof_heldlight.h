/*
sv_cof_heldlight.h - Cry of Fear story co-op: a held lantern, flare or book laser lights the room for everyone
(sv_cof_weaponmodel.c)
GPL-3.0-or-later.

client.dll draws these lights only for its own player, from its HUD weapon and view model (view.cpp:1213-1264 in the
decompilation): the lantern (m_iId 10, either hand of a dual pair) radius 220 +-5, grey 0.4; while flash flag 16 is set,
the flare (13) and the book laser (25) radius 220 +-15 with a red or orange flicker. The phone light and the flashlight
weapon already reach the others (the scoreboard flags 1 and 8). The engine sends the other players a light entity with
client.dll's own renderfx 71 (RendererFilterEntities: a dynamic light of radius renderamt * 8 and colour
rendercolor / 128, the model not drawn). Research: docs/research/2026-10-18-cof-decompilation-survey.md section 3.

Plain C (standard library only): the offline tests share it.
*/
#ifndef SV_COF_HELDLIGHT_H
#define SV_COF_HELDLIGHT_H

#if defined( _MSC_VER )
#define COF_HELDLIGHT_FN static __inline
#else
#define COF_HELDLIGHT_FN static inline
#endif

enum { COF_HELDLIGHT_NONE, COF_HELDLIGHT_LANTERN, COF_HELDLIGHT_FLARE, COF_HELDLIGHT_BOOKLASER };

#define COF_HELDLIGHT_RENDERFX 71
#define COF_HELDLIGHT_LIT 16 // m_iFlashlightFlags: a lit flare or the book laser in hand
#define COF_HELDLIGHT_DUAL 15 // weapon_dualweapon

// What the held item lights (id: m_iId in hand; right, left: a dual pair's weapons; flags: m_iFlashlightFlags).
COF_HELDLIGHT_FN int CoF_HeldLight( int id, int right, int left, int flags )
{
	int dual = id == COF_HELDLIGHT_DUAL;
	if( id == 10 || ( dual && ( right == 10 || left == 10 ))) return COF_HELDLIGHT_LANTERN;
	if( !( flags & COF_HELDLIGHT_LIT )) return COF_HELDLIGHT_NONE;
	if( id == 25 ) return COF_HELDLIGHT_BOOKLASER;
	if( id == 13 || ( dual && ( right == 13 || left == 13 ))) return COF_HELDLIGHT_FLARE;
	return COF_HELDLIGHT_NONE;
}

// The light entity's renderamt and rendercolor; the rolls (0..999) are the flicker the holder's client draws.
COF_HELDLIGHT_FN void CoF_HeldLightLook( int kind, int radius_roll, int red_roll, int green_roll, int *renderamt, int rgb[3] )
{
	if( kind == COF_HELDLIGHT_LANTERN )
	{
		*renderamt = 27 + radius_roll * 2 / 1000; // 216..224
		rgb[0] = rgb[1] = rgb[2] = 51; // 0.4
		return;
	}
	*renderamt = 26 + radius_roll * 4 / 1000; // 208..232
	rgb[0] = 83 + red_roll * 129 / 1000; // 1.15 +-0.5
	rgb[1] = ( kind == COF_HELDLIGHT_BOOKLASER ? 64 : 13 ) + green_roll * 20 / 1000; // 0.6 or 0.2, -0.1..+0.05
	rgb[2] = green_roll * 20 / 1000; // 0.1, -0.1..+0.05
}

// Where the light is: at the holder's hand, in front of the chest.
COF_HELDLIGHT_FN void CoF_HeldLightOrigin( const float origin[3], const float view_ofs[3], const float forward[3], const float right[3], float out[3] )
{
	int i;
	for( i = 0; i < 3; i++ )
		out[i] = origin[i] + view_ofs[i] * 0.75f + forward[i] * 14.0f + right[i] * 6.0f;
}

#endif // SV_COF_HELDLIGHT_H
