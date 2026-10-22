/* Cry of Fear story co-op: the black rooms behind the doors between maps (sv_cof_party.c, sv_save.c).
 * Pure rules, no engine types. GPL-3.0-or-later.
 *
 * Every door to another map ends in a small sealed black room: the inter_door puts the player there, in the room's
 * trigger_changelevel; the map on the other side has the same room around the same landmark, with the level change
 * back on one side of the landmark and, on the other, a trigger_multiple whose cof_teleport takes the player out to
 * the door (2 to 16 units from the landmark in every story map; another door's room is 67 units or more away). The
 * retail arrival keeps the player's offset from the landmark, which puts them in that walk-in trigger at once. */
#ifndef SV_COF_DOORROOM_H
#define SV_COF_DOORROOM_H

// the walk-in trigger of the room a landmark stands in: its box within this many units of the landmark
#define COF_DOORROOM_WALKIN_NEAR 24.0f
// how far from the landmark an arrival is still in its room (the rooms are about 140 by 72 units)
#define COF_DOORROOM_ROOM 160.0f

// Squared distance from a point to a box (0 inside or on it).
static float CoF_DoorRoomBoxDistance2( const float *p, const float *absmin, const float *absmax )
{
	float d = 0.0f, e;
	int k;
	for( k = 0; k < 3; k++ )
	{
		if( p[k] < absmin[k] ) e = absmin[k] - p[k];
		else if( p[k] > absmax[k] ) e = p[k] - absmax[k];
		else continue;
		d += e * e;
	}
	return d;
}

// A walk-in trigger of the room this landmark stands in.
static int CoF_DoorRoomWalkInNear( const float *mark, const float *absmin, const float *absmax )
{
	return CoF_DoorRoomBoxDistance2( mark, absmin, absmax ) <= COF_DOORROOM_WALKIN_NEAR * COF_DOORROOM_WALKIN_NEAR;
}

// A player at origin stands well inside the trigger: the origin itself within the trigger's box (the engine pads
// absmin/absmax by a unit; a brush trigger touches a player whose origin lies inside it grown by the hull) and the
// hull overlapping it in height. One only grazing its edge is not counted, so they are set down in its middle.
static int CoF_DoorRoomInside( const float *origin, const float *mins, const float *maxs, const float *absmin, const float *absmax )
{
	int k;
	for( k = 0; k < 2; k++ )
		if( origin[k] < absmin[k] + 1.0f || origin[k] > absmax[k] - 1.0f ) return 0;
	return origin[2] + mins[2] < absmax[2] - 1.0f && origin[2] + maxs[2] > absmin[2] + 1.0f;
}

// Where in the trigger to set an arrival down, before the drop to the floor (out: x, y; z the landmark's height):
// 0, the trigger's middle; 1, the landmark brought into the trigger by half a hull and a unit, nearer the room's
// middle when the middle is blocked. False when there is no such spot.
static int CoF_DoorRoomSpot( const float *mark, const float *absmin, const float *absmax, const float *mins, const float *maxs, int which, float *out )
{
	int k;
	if( which < 0 || which > 1 ) return 0;
	for( k = 0; k < 2; k++ )
	{
		float lo = absmin[k] + 1.0f, hi = absmax[k] - 1.0f, inset = ( maxs[k] - mins[k] ) * 0.5f + 1.0f;
		if( hi < lo ) return 0;
		if( which == 0 || hi - lo <= 2.0f * inset ) out[k] = ( lo + hi ) * 0.5f;
		else if( mark[k] < lo + inset ) out[k] = lo + inset;
		else if( mark[k] > hi - inset ) out[k] = hi - inset;
		else out[k] = mark[k];
	}
	out[2] = mark[2];
	return 1;
}

// A cof_teleport stays in its own map across a story level change when its destination is in that map. The game
// lets one cross (CBaseEntity's FCAP_ACROSS_TRANSITION) when it lies in the landmark's view, as every black room's
// does; the few whose destination is elsewhere keep crossing, as in single player.
static int CoF_DoorRoomTeleportStays( int is_teleport, int has_target, int target_here )
{
	return is_teleport && has_target && target_here;
}

#endif // SV_COF_DOORROOM_H
