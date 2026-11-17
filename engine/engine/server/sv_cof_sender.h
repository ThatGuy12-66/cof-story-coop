/* Cry of Fear story co-op: who "the first player" is while one player's client
 * command runs. Pure, no engine types. GPL-3.0-or-later.
 * The retail DLL finds "the player" with UTIL_FindEntityByClassname( NULL,
 * "player" ) (10118460) in 87 places: the lowest client slot, normally the
 * host. Inside a client command that is the wrong player whenever a guest sent
 * it: padlockcancel (1001C968 -> 100B5EA0) restores the host and leaves the
 * guest frozen. While a command runs the order becomes: its sender, then
 * everyone else. */
#ifndef SV_COF_SENDER_H
#define SV_COF_SENDER_H

// The next "player" edict index after start (0: from the top), or 0 when the
// players are exhausted. present[i] says client edict i (1..maxclients) is a
// player the plain search would return; the sender must be present.
static int CoF_SenderNextPlayer( int start, int sender, int maxclients, const unsigned char *present )
{
	int i;
	if( start <= 0 ) return sender;
	for( i = ( start == sender ) ? 1 : start + 1; i <= maxclients; i++ )
		if( i != sender && present[i] ) return i;
	return 0;
}
#endif
