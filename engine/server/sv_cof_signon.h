/*
sv_cof_signon.h - Cry of Fear story co-op: how much of the sign-on a joining client gets per round trip
GPL-3.0-or-later.

Every story level change signs each guest on again: resources, consistency, baselines, user messages, the game's
own messages, a few hundred KB before LZSS. The netchan sends that as reliable fragments, one at a time, the next
only after the client has acknowledged the last (stop and wait), so the sign-on takes one round trip per fragment.
While a client is signing on, the engine's fragment is cl_dlmax less the netchan header: 1366 bytes with the
co-op's cl_dlmax 1400, about a hundred round trips for every level change (seconds between two players on
different coasts).

A client with NET_EXT_SPLITSIZE takes any packet the server sends it in cl_dlmax pieces (NET_SendLong, a
SPLITPACKET header on each, put back together by NET_GetLong up to 64 KB), so the size of a fragment is not the
size of a datagram on the wire: the pieces stay at cl_dlmax and never need IP fragmentation (RFC 8900). A fragment
for a client signing on is COF_SIGNON_WINDOW pieces: ten, the initial window TCP has used on the same links since
RFC 6928 (about 14 KB at a time). The sign-on then takes about a tenth of the round trips. In game (cs_spawned)
nothing changes: the engine's half of cl_dlmax, or cl_frmax.

The client side needs room for a burst of pieces until its next frame reads them: the engine asks for a 256 KB
receive queue (net_ws.c; Windows 7 gave a UDP socket 8 KB, Windows 11 gives 64 KB).

1.1.9 (N1): the window is the host's cvar cof_signon_window (net_ws.c, kept in config.cfg), 10 by default. A link
that drops bursts or splits them again (a VPN with a small MTU) gets the engine's own one piece per round trip with
cof_signon_window 1, no rebuild needed. Only the host's fragment sizes change: every 1.1.x guest takes any of them.

Plain C: the offline tests share it (they see the default window).
*/
#ifndef SV_COF_SIGNON_H
#define SV_COF_SIGNON_H

#if defined( _MSC_VER )
#define COF_SIGNON_FN static __inline
#else
#define COF_SIGNON_FN static inline
#endif

#define COF_SIGNON_WINDOW_DEFAULT 10 // split pieces per sign-on fragment: TCP's initial window (RFC 6928)
#define COF_SIGNON_WINDOW_MAX 64    // more than a fragment holds at any split size (it is capped at max_fragment)
#define COF_SIGNON_SPLIT_HEADER 10  // SPLITPACKET: int net_id, int sequence_number, short packet_id
#define COF_SIGNON_MIN_SPLIT 1000   // a smaller split size is a client asking for small packets: keep the engine's
#define COF_SIGNON_SLACK 64         // netchan header (HEADER_BYTES 34), cookie, a reliable bit or two

// The window a cvar value asks for: whole pieces from 1 (the engine's own fragments) to COF_SIGNON_WINDOW_MAX.
COF_SIGNON_FN int CoF_SignonWindow( float value )
{
	if( !( value >= 1.0f )) // below one, or not a number
		return 1;
	if( value >= (float)COF_SIGNON_WINDOW_MAX )
		return COF_SIGNON_WINDOW_MAX;
	return (int)value;
}

#if defined( SERVER_H )
// in the engine (sv_client.c includes server.h first): the host's cvar, registered in NET_Init
extern convar_t cof_signon_window;
#define COF_SIGNON_WINDOW CoF_SignonWindow( cof_signon_window.value )
#else
#define COF_SIGNON_WINDOW COF_SIGNON_WINDOW_DEFAULT
#endif

// The size of a reliable fragment for a client, or 0: the engine's own.
// split: the client's split size (cl_dlmax), 0 without NET_EXT_SPLITSIZE. spawned: in game.
// max_fragment: the largest fragment the netchan takes (FRAGMENT_MAX_SIZE less its header).
COF_SIGNON_FN int CoF_SignonFragmentSize( int split, int spawned, int window, int max_fragment )
{
	int size;

	if( spawned || window <= 1 || split < COF_SIGNON_MIN_SPLIT )
		return 0;

	size = window * ( split - COF_SIGNON_SPLIT_HEADER ) - COF_SIGNON_SLACK;
	if( size > max_fragment )
		size = max_fragment;
	return size;
}

// How many cl_dlmax pieces a fragment of size goes out in (with the netchan header and slack).
COF_SIGNON_FN int CoF_SignonPieces( int split, int size )
{
	int body = split - COF_SIGNON_SPLIT_HEADER;
	int length = size + COF_SIGNON_SLACK;

	if( body <= 0 )
		return 0;
	if( length <= split )
		return 1;
	return ( length + body - 1 ) / body;
}

#endif // SV_COF_SIGNON_H
