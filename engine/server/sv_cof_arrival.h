/*
sv_cof_arrival.h - Cry of Fear story co-op: what a story level change leaves behind (sv_cof_arrival.c)
GPL-3.0-or-later.

A story level change keeps each client connected and restores its player in the new map; client.dll stays loaded
but runs its VidInit, and the game DLL treats the restored player as one it already set up (no ResetHUD, no InitHUD:
gInitHUD is only set by a new game or a loaded save, world.cpp:455). Three things came out of that in play
(Xavier's host log of 2026-10-20, two players, 23 level changes):

1. Hands gone after a door. A door to another map ends (inter_door EndSequence clears HIDEHUD_WEAPONS and
   HIDEHUD_HEALTH, CoF_InterDoor.cpp:529-530) in the frame the level change starts, so the HideWeapon that would
   say so never goes out: the game sends it from UpdateClientData a frame later (player.cpp:9413). The client keeps
   the door's 9, draws no viewmodel while HIDEHUD_WEAPONS is set (StudioModelRenderer.cpp:1144), and the restored
   player's m_iClientHideHUD is a fresh 0, the same as its m_iHideHUD: nothing ever sends it. At every story
   sign-on the engine sends the player's HideWeapon. A player carried over in the middle of another door (its
   EndSequence stays in the old map) is let out of it there.
2. The co-op torch off after a level change, and F pressed twice. The torch is pev->frags 32 (client.cpp:2162:
   F switches 0 to 32 and anything else to 0). Every client draws it from its scoreboard copy (g_PlayerExtraInfo,
   gl_studiomodel.cpp:656); the VidInit wipes that copy (ScorePanel::Initialize, vgui_ScorePanel.cpp:246) and no
   ScoreInfo follows a restore. The game also writes its light flags over frags whenever they change
   (player.cpp:6293, the client copy a fresh 0 after a restore): frags 2 is a torch that is off and takes two
   presses (2 -> 0 -> 32). The engine keeps each player's torch as they last switched it, sends everyone's
   ScoreInfo at every story sign-on, puts bit 32 back when the game drops it, and F switches from the kept state.
3. Back through the exit just come through. Players arrive where they stood relative to the landmark: inside the
   new map's exit back (cm_park2 -> cm_park3 -> cm_park2 at sign-on), or a step from it (a dodge took a player
   back 8 s after arriving). A touch of an exit does not count for a player who has stood in an exit ever since
   arriving, nor, in the first COF_ARRIVAL_GRACE seconds, at the exit back by the landmark the party came in by;
   coming through a door just now always counts (the doors are how the story takes those exits).
4. Empty quick-slot pictures. client.dll's VidInit empties the three QUICK SELECT boxes (CRECBInventory::Initialize)
   and the game sends them again (QSlotUpdate) from the first PostThink after a restore, on one global flag
   (m_bRestored, 0x10226476) that the first player's PostThink clears: after every level change or load the
   others' boxes stayed empty, their keys still working. At every story sign-on the engine sends the player's set
   quick slots (a weapon's name in m_sQuickSelK and its inventory slot in m_iQuickSlotSlot[K]).

Plain C: the offline tests share it.
*/
#ifndef SV_COF_ARRIVAL_H
#define SV_COF_ARRIVAL_H

#if defined( _MSC_VER )
#define COF_ARRIVAL_FN static __inline
#else
#define COF_ARRIVAL_FN static inline
#endif

#define COF_ARRIVAL_TORCH 32           // pev->frags bit of the co-op torch
#define COF_ARRIVAL_GRACE 15.0         // seconds after the party arrived: the exit back is taken through its door
#define COF_ARRIVAL_DOOR_WINDOW 3.0    // seconds after a door that a touch of an exit counts
#define COF_ARRIVAL_SCOREINFO_SIZE 9   // byte index, short frags, deaths, class, team

// The frags the game's F should see, so that it switches the torch as the player saw it.
COF_ARRIVAL_FN int CoF_TorchBefore( int on )
{
	return on ? COF_ARRIVAL_TORCH : 0;
}

// After the game's F: -1 when it switched nothing (frags still what it saw), else the torch's new state.
COF_ARRIVAL_FN int CoF_TorchAfter( int before, int after )
{
	if( after == before ) return -1;
	return ( after & COF_ARRIVAL_TORCH ) != 0;
}

// The frags that keep a torch that is on (the game's flags with bit 32), or -1: nothing to put back.
COF_ARRIVAL_FN int CoF_TorchKeep( int on, int frags )
{
	if( !on || ( frags & COF_ARRIVAL_TORCH )) return -1;
	return frags | COF_ARRIVAL_TORCH;
}

// ScoreInfo as the game writes it (WRITE_BYTE, then four WRITE_SHORTs, little endian).
COF_ARRIVAL_FN void CoF_ScoreInfoPayload( unsigned char *out, int index, int frags, int deaths, int pclass, int team )
{
	int values[4], i;
	values[0] = frags; values[1] = deaths; values[2] = pclass; values[3] = team;
	out[0] = (unsigned char)index;
	for( i = 0; i < 4; i++ )
	{
		out[1 + i * 2] = (unsigned char)( values[i] & 0xFF );
		out[2 + i * 2] = (unsigned char)(( values[i] >> 8 ) & 0xFF );
	}
}

// The short at field (0 frags, 1 deaths, 2 class, 3 team) of a ScoreInfo payload.
COF_ARRIVAL_FN int CoF_ScoreInfoField( const unsigned char *payload, int field )
{
	return (short)( payload[1 + field * 2] | ( payload[2 + field * 2] << 8 ));
}

// Whether a player's touch of an exit counts.
// door_age: seconds since they were last seen going through an inter_door in this map, negative: never.
// stayed: in an exit ever since arriving. arrived_age: seconds since the party arrived in this map by a story
// level change, negative: it did not. back: the exit leads back by the landmark the party came in by.
COF_ARRIVAL_FN int CoF_ArrivalTouchCounts( double door_age, int stayed, double arrived_age, int back )
{
	if( door_age >= 0.0 && door_age <= COF_ARRIVAL_DOOR_WINDOW ) return 1;
	if( stayed ) return 0;
	if( back && arrived_age >= 0.0 && arrived_age < COF_ARRIVAL_GRACE ) return 0;
	return 1;
}

// A quick slot whose picture goes out again: an inventory slot (1..12) holding the weapon the slot selects.
COF_ARRIVAL_FN int CoF_QuickSlotShown( int slot, const char *weapon )
{
	static const char prefix[] = "weapon_";
	int i;
	if( slot < 1 || slot > 12 || !weapon ) return 0;
	for( i = 0; prefix[i]; i++ )
		if( weapon[i] != prefix[i] ) return 0;
	return 1;
}

#endif // SV_COF_ARRIVAL_H
