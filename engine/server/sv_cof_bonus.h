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
 * commentary (line 73) and the menus keep the file's own state; the outfits have their own switch. */
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

/* The wardrobe's twelve outfits (cof_costumes): CBasePlayer::UpdateClientData builds the Clothes mask from
 * these on the server, so the host's file decided every player's wardrobe (player.cpp:9567-9651, the tokens
 * player.cpp:15559-15724 in the decompilation; docs/research/2026-10-18-cof-decompilation-survey.md section 3). */
static const cof_bonus_line_t cof_costume_lines[] =
{
	{ 51, "olt0\243s^\243rn(aki!9eqkh", "olt0\243s^\243rn(aki!9eqkj" }, /* 0x80: David Leatherhoff suit */
	{ 52, "h*wchks\243n&%!\243bf%jtew", "h*wchks\243n&%!\243bf%jtei" }, /* 0x100: ModDB hoodie */
	{ 53, "%xn%i)jhxmfnbgjsr^if", "\243xn%i)jhxmfnbgjsr^if" },       /* 0x200: Hello Kitty suit */
	{ 54, "fy^xix&&vkz)vcdn!wtv", "fy^xix&&vkz)vcrn!wtv" },         /* 0x400: Afraid of Monsters suit */
	{ 55, "jwsyv**yb&(qtqlg)moj", "jwtyv**yb&(qtqlg)moj" },         /* 0x800: Camouflage hoodie */
	{ 56, "henp(cm$at!n%\243f(c$jp", "henp(cm$at!m%\243f(c$jp" },     /* 0x1000: Half-Life Creations hoodie */
	{ 57, "yef!xkyzw*^vd*tj)zkc", "yef!&kyzw*^vd*tj)zkc" },         /* 0x2000: Black Metal suit */
	{ 58, "z(*mbwy(talqlrqf$lmw", "z(*mbwy(tolqlrqf$lmw" },         /* 0x4000: Team Psykskallar hoodie */
	{ 59, "zgpwg\243vyqlzensuauwat", "zwpwg\243vyqlzensuauwat" },     /* 0x8000: Awesome suit */
	{ 60, "dzpy)*iiy$oj!akyfbn%", "dzpy)*iiy$0j!akyfbn%" },         /* 0x10000: Sick Simon suit */
	{ 61, "pe%%*\243\243rxa(%(bog)l^j", "pe%%*\243\243rxr(%(bog)l^j" },   /* 0x20000: AoM Twitcher suit */
	{ 62, "efq%)x)mk\243*odxsdocb)", "efq%)u)mk\243*odxsdocb)" },     /* 0x40000: Custom hoodie */
};
#define COF_COSTUME_LINES ( (int)( sizeof( cof_costume_lines ) / sizeof( cof_costume_lines[0] )))

static const cof_bonus_line_t *CoF_BonusLineIn( const cof_bonus_line_t *table, int count, int line )
{
	int i;
	for( i = 0; i < count; i++ )
		if( table[i].line == line ) return &table[i];
	return NULL;
}

static const cof_bonus_line_t *CoF_BonusLine( int line )
{
	return CoF_BonusLineIn( cof_bonus_lines, COF_BONUS_LINES, line );
}

static const unsigned char *CoF_BonusFind( const unsigned char *at, size_t size, const char *text )
{
	size_t n = strlen( text ), i;
	for( i = 0; i + n <= size; i++ )
		if( !memcmp( at + i, text, n )) return at + i;
	return NULL;
}

/* In place: the table's lines say `unlocked` (1) or locked (0). Lines are counted as
 * the game counts them. A line holding neither text is left alone (it reads
 * locked). Returns how many lines changed. */
static int CoF_BonusRewriteTable( unsigned char *text, size_t size, const cof_bonus_line_t *table, int count, int unlocked )
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
		if(( gate = CoF_BonusLineIn( table, count, line )) != NULL )
		{
			const char *from = unlocked ? gate->locked : gate->unlocked, *to = unlocked ? gate->unlocked : gate->locked;
			unsigned char *at = (unsigned char *)CoF_BonusFind( text + start, i - start, from );
			if( at ) { memcpy( at, to, strlen( to )); changed++; }
		}
	}
	return changed;
}

static int CoF_BonusRewrite( unsigned char *text, size_t size, int unlocked )
{
	return CoF_BonusRewriteTable( text, size, cof_bonus_lines, COF_BONUS_LINES, unlocked );
}

static int CoF_CostumeRewrite( unsigned char *text, size_t size, int unlocked )
{
	return CoF_BonusRewriteTable( text, size, cof_costume_lines, COF_COSTUME_LINES, unlocked );
}

/* For a game folder without the file: COF_BONUS_FILE_LINES lines, each switch's lines in its asked state (1 open,
 * 0 or -1 shut) and every other one "-" (read as locked). Returns the length written (no terminator counted), or 0
 * when `size` is too small. */
static size_t CoF_BonusSynthesize( unsigned char *out, size_t size, int bonus, int costumes )
{
	size_t used = 0;
	int line;
	for( line = 1; line <= COF_BONUS_FILE_LINES; line++ )
	{
		const cof_bonus_line_t *gate = CoF_BonusLine( line ), *outfit = CoF_BonusLineIn( cof_costume_lines, COF_COSTUME_LINES, line );
		const char *text = gate ? ( bonus > 0 ? gate->unlocked : gate->locked ) :
			outfit ? ( costumes > 0 ? outfit->unlocked : outfit->locked ) : "-";
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

/* The game's own reading of one bit (CheckingEndingOrUnlockables, player.cpp:15310): the gate's line, counted as
 * strtok counts it, holds its locked text: no; else its unlocked text: yes; else (or no such line): no. */
static int CoF_BonusUnlocked( const unsigned char *text, size_t size, const cof_bonus_line_t *gate )
{
	size_t i = 0;
	int line = 0;
	if( !text || !gate ) return 0;
	while( i < size )
	{
		size_t start;
		while( i < size && text[i] == '\n' ) i++;
		if( i >= size || !text[i] ) break;
		start = i; line++;
		while( i < size && text[i] != '\n' && text[i] ) i++;
		if( line != gate->line ) continue;
		if( CoF_BonusFind( text + start, i - start, gate->locked )) return 0;
		return CoF_BonusFind( text + start, i - start, gate->unlocked ) != NULL;
	}
	return 0;
}

/* The bonus room's door opens on any of the five endings (CoF_InterDoor.cpp:248): the first five lines of the table. */
static int CoF_BonusDoorOpen( const unsigned char *text, size_t size )
{
	int i;
	for( i = 0; i < 5; i++ )
		if( CoF_BonusUnlocked( text, size, &cof_bonus_lines[i] )) return 1;
	return 0;
}

/* After 1.1.9 (the wardrobe key, cof_wardrobe): the outfits the wardrobe offers, as CCoFClothesMenu::Use builds them
 * (effects.cpp:11800-11870): outfit k unlocked -> bit k; *any (the message's canseeclothes): one of them is. */
static unsigned int CoF_WardrobeFlags( const unsigned char *text, size_t size, int *any )
{
	unsigned int flags = 0;
	int k;
	for( k = 0; k < COF_COSTUME_LINES; k++ )
		if( CoF_BonusUnlocked( text, size, &cof_costume_lines[k] )) flags |= 1u << k;
	if( any ) *any = flags != 0;
	return flags;
}

/* The Clothes message that opens the menu (REG_USER_MSG( "Clothes", 7 ), player.cpp:1343), as the wardrobe sends it
 * to its user: LONG the outfits, BYTE canseeclothes, BYTE m_iUnlockedClothes (the outfit worn), BYTE 1 (show). */
#define COF_WARDROBE_MESSAGE 7
static void CoF_WardrobeMessage( unsigned char *out, unsigned int flags, int any, int outfit )
{
	out[0] = (unsigned char)( flags & 0xFF );
	out[1] = (unsigned char)(( flags >> 8 ) & 0xFF );
	out[2] = (unsigned char)(( flags >> 16 ) & 0xFF );
	out[3] = (unsigned char)(( flags >> 24 ) & 0xFF );
	out[4] = (unsigned char)( any ? 1 : 0 );
	out[5] = (unsigned char)( outfit & 0xFF );
	out[6] = 1;
}

/* What cof_wardrobe does for a player: COF_WARDROBE_OPEN, or why not. The bonus room shut for this game; dead; held
 * by the game (a camera or cutscene holds the view, a model cutscene, a ladder climb or a scene freezes them, which the
 * game shows its own camera for); on a ladder of the plain kind; rowing the boat; or pressed again too soon. */
enum
{
	COF_WARDROBE_OPEN, COF_WARDROBE_SHUT, COF_WARDROBE_DEAD, COF_WARDROBE_SCENE, COF_WARDROBE_LADDER, COF_WARDROBE_BOAT,
	COF_WARDROBE_SOON
};
static int CoF_WardrobeAnswer( int room_open, int alive, int camera, int frozen, int ladder, int boat, int soon )
{
	if( soon ) return COF_WARDROBE_SOON;
	if( !room_open ) return COF_WARDROBE_SHUT;
	if( !alive ) return COF_WARDROBE_DEAD;
	if( camera || frozen ) return COF_WARDROBE_SCENE;
	if( ladder ) return COF_WARDROBE_LADDER;
	if( boat ) return COF_WARDROBE_BOAT;
	return COF_WARDROBE_OPEN;
}

/* The game's closeclothes (client.cpp:1552) is Use OFF 1 on the map's FIRST cof_clothesmenu: its posed Simon hidden
 * and its camera's Use OFF 666, which, while the camera runs, frees every living player and gives each their own view
 * (triggers.cpp:5556-5610; off, ShouldToggle refuses and nothing happens; a map without a wardrobe: nothing at all).
 * The Close of a menu the key opened goes no further, so it never ends someone else's wardrobe session; unless its
 * sender is the one a running wardrobe camera is for (they used the real wardrobe since: theirs to close). */
static int CoF_WardrobeKeepsClose( int opened_by_key, int wardrobe_camera_user )
{
	return opened_by_key && !wardrobe_camera_user;
}
#endif
