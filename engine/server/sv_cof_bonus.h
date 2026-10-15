/* Cry of Fear story co-op: the bonus room switch (cof_bonus_room). GPL-3.0-or-later.
 *
 * The game keeps its unlocks in scriptsettings.dat in the game folder. The retail
 * reader CheckingEndingOrUnlockables (hl.dll 0x100DD000) loads it through the
 * engine's LoadFileForMe on every call, splits it with strtok( buf, "\n" ), takes
 * the n-th non-empty line for a bit and asks strstr for that bit's LOCKED text
 * (false) or UNLOCKED text (true); the two differ in one character. The bonus
 * room's door (c_start, c_bridge: inter_door "unlockablesdoor", Use 0x10048F70)
 * opens on any of the five ending bits; each bonus item removes itself on its
 * first update when its own bit is locked. Rewriting those lines in the buffer the
 * server hands the game decides the room for the session, whatever the file says,
 * and never changes the file. Research: evidence/tester-package-20261015/research/bonus-room-re.md.
 *
 * Pure: no engine headers; the offline harness compiles it as it is. */
#ifndef SV_COF_BONUS_H
#define SV_COF_BONUS_H
#include <stddef.h>
#include <string.h>

typedef struct { int line; const char *locked, *unlocked; } cof_bonus_line_t;

/* The door (the five endings) and the room's items. Not here: the developer
 * commentary (line 73), the outfits and the menus keep the file's own state. */
static const cof_bonus_line_t cof_bonus_lines[] =
{
	{ 15, "v9agjak+p)(,o3qb)aq4", "v9agjak+p)(,03qb)aq4" }, /* 0x1: ending 1 (door) */
	{ 7, "ye&ole1^0(fcanb3v5t4", "ye&0le1^0(fcanb3v5t4" },  /* 0x2: ending 2 (door) */
	{ 45, "1r6fd46ob7u2yj9q83yx", "1r6fd46ob7u2yj9r83yx" }, /* 0x4: ending 3 (door) */
	{ 22, "2quwo6z3y4w" "\xa3" "ld)y!i35", "2quvo6z3y4w" "\xa3" "ld)y!i35" }, /* 0x8: ending 4 (door) */
	{ 3, "z3,4lj)4b!9g7vgi4x5a", "z3,4rj)4b!9g7vgi4x5a" },  /* 0x10: ending 5 (door) */
	{ 39, "r1&,iji4cjtvf,44w919", "r1&,iji4cjtvf.44w919" }, /* 0x40: weapon_famas */
	{ 70, "pzakmx&p^lbk^kw($(fo", "pzakhx&p^lbk^kw($(fo" }, /* 0x80000: weapon_axe */
	{ 71, "g*q$cceqhz&vcdp(au%!", "g*q$cceqhz&vcdp(av%!" }, /* 0x100000: weapon_camera (the room's) */
	{ 72, "aaa^$i)whenzprdh!vus", "aba^$i)whenzprdh!vus" }, /* 0x200000: weapon_booklaser */
	{ 74, "tzi&aoxfbkb^n&ikaxj%", "tzi&a0xfbkb^n&ikaxj%" }, /* 0x800000: the package key */
	{ 75, "wph!uhhgrws)*erdb*rg", "wph!uhmgrws)*erdb*rg" }, /* 0x1000000: item_nightvision */
};
#define COF_BONUS_LINES ( (int)( sizeof( cof_bonus_lines ) / sizeof( cof_bonus_lines[0] )))
#define COF_BONUS_FILE_LINES 83 /* the highest line any bit uses */

static const cof_bonus_line_t *CoF_BonusLine( int line )
{
	int i;
	for( i = 0; i < COF_BONUS_LINES; i++ )
		if( cof_bonus_lines[i].line == line ) return &cof_bonus_lines[i];
	return NULL;
}

static const unsigned char *CoF_BonusFind( const unsigned char *at, size_t size, const char *text )
{
	size_t n = strlen( text ), i;
	for( i = 0; i + n <= size; i++ )
		if( !memcmp( at + i, text, n )) return at + i;
	return NULL;
}

/* In place: the gated lines say `unlocked` (1) or locked (0). Lines are counted as
 * the game counts them. A line holding neither text is left alone (it reads
 * locked). Returns how many lines changed. */
static int CoF_BonusRewrite( unsigned char *text, size_t size, int unlocked )
{
	size_t i = 0;
	int line = 0, changed = 0;
	while( i < size )
	{
		const cof_bonus_line_t *gate;
		size_t start;
		while( i < size && text[i] == '\n' ) i++;
		if( i >= size || !text[i] ) break; /* strtok ends at the terminator */
		start = i; line++;
		while( i < size && text[i] != '\n' && text[i] ) i++;
		if(( gate = CoF_BonusLine( line )) != NULL )
		{
			const char *from = unlocked ? gate->locked : gate->unlocked, *to = unlocked ? gate->unlocked : gate->locked;
			unsigned char *at = (unsigned char *)CoF_BonusFind( text + start, i - start, from );
			if( at ) { memcpy( at, to, strlen( to )); changed++; }
		}
	}
	return changed;
}

/* For a game folder without the file: COF_BONUS_FILE_LINES lines, the gated ones
 * in the asked state and every other one "-" (read as locked). Returns the length
 * written (no terminator counted), or 0 when `size` is too small. */
static size_t CoF_BonusSynthesize( unsigned char *out, size_t size, int unlocked )
{
	size_t used = 0;
	int line;
	for( line = 1; line <= COF_BONUS_FILE_LINES; line++ )
	{
		const cof_bonus_line_t *gate = CoF_BonusLine( line );
		const char *text = gate ? ( unlocked ? gate->unlocked : gate->locked ) : "-";
		size_t n = strlen( text );
		if( used + n + 2 > size ) return 0;
		memcpy( out + used, text, n ); used += n;
		out[used++] = '\n';
	}
	out[used] = 0;
	return used;
}

/* The one file this switch answers for (LoadFileForMe paths are game-folder relative). */
static int CoF_BonusIsSettings( const char *name )
{
	const char *want = "scriptsettings.dat";
	size_t i;
	if( !name ) return 0;
	for( i = 0; want[i]; i++ )
	{
		char c = name[i];
		if( c >= 'A' && c <= 'Z' ) c = (char)( c - 'A' + 'a' );
		if( c != want[i] ) return 0;
	}
	return name[i] == 0;
}
#endif
