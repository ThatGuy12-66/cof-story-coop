/* cof_hud_sprites.h (1.2.9): every name Cry of Fear's client.dll asks hud.txt for gets an entry, so GetSpriteIndex
   never answers -1 and the HUD never reads before the rect array (the Chapter 4 start crash, 2026-10-24). */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct { int left, right, top, bottom; } wrect_t;
typedef struct client_sprite_s
{
	char    szName[64];
	char    szSprite[64];
	int     hspr;
	int     iRes;
	wrect_t rc;
} client_sprite_t;

static int Q_stricmp( const char *a, const char *b )
{
	for( ; tolower( (unsigned char)*a ) == tolower( (unsigned char)*b ); a++, b++ )
		if( !*a ) return 0;
	return tolower( (unsigned char)*a ) - tolower( (unsigned char)*b );
}

static void Q_strncpy( char *dst, const char *src, size_t size )
{
	strncpy( dst, src, size - 1 );
	dst[size - 1] = 0;
}

#include "hud_sprites_under_test.h"

#define CHECK( cond, what ) do { if( !( cond )) { printf( "FAIL: %s\n", what ); return 1; } } while( 0 )

static client_sprite_t list[400];

static void put( int i, const char *name, int res, const char *sprite, int x, int y, int w, int h )
{
	memset( &list[i], 0, sizeof( list[i] ));
	Q_strncpy( list[i].szName, name, sizeof( list[i].szName ));
	Q_strncpy( list[i].szSprite, sprite, sizeof( list[i].szSprite ));
	list[i].iRes = res;
	list[i].rc.left = x; list[i].rc.top = y; list[i].rc.right = x + w; list[i].rc.bottom = y + h;
}

static int find( int count, const char *name, int res )
{
	for( int i = 0; i < count; i++ )
		if( list[i].iRes == res && !Q_stricmp( list[i].szName, name ))
			return i;
	return -1;
}

int main( void )
{
	int count, i;
	size_t n;

	// Cry of Fear's own list: 640 only, its first entry number_0 on cof_ammohud, none of the 15 names
	put( 0, "number_0", 640, "cof_ammohud", 0, 0, 20, 24 );
	put( 1, "number_1", 640, "cof_ammohud", 24, 0, 20, 24 );
	put( 2, "cof_famas_mag", 640, "cof_mags", 0, 64, 32, 32 );
	count = CoF_HudSpriteDefaults( list, 3, 3 + COF_HUD_EXTRA );
	CHECK( count == 3 + (int)COF_HUD_MISSING_COUNT, "each missing name added once" );
	for( n = 0; n < COF_HUD_MISSING_COUNT; n++ )
	{
		i = find( count, cof_hud_missing_names[n], 640 );
		CHECK( i >= 3, "the missing name is found at the list's resolution" );
		CHECK( !strcmp( list[i].szSprite, "cof_ammohud" ), "with the resolution's first sprite" );
		CHECK( !list[i].rc.left && !list[i].rc.right && !list[i].rc.top && !list[i].rc.bottom && !list[i].hspr,
			"and an empty rectangle" );
	}
	CHECK( find( count, "bucket1", 640 ) >= 0, "bucket1 (the Chapter 4 crash) is there" );
	CHECK( list[1].rc.left == 24 && list[1].rc.right == 44 && list[2].rc.bottom == 96, "the file's entries untouched" );

	// a second pass adds nothing
	CHECK( CoF_HudSpriteDefaults( list, count, 400 ) == count, "names already there are not added again" );

	// a list that has some of them keeps its own (any case), and only the rest are added
	put( 0, "number_0", 640, "hud1", 0, 0, 20, 24 );
	put( 1, "Bucket1", 640, "hud2", 1, 2, 3, 4 );
	put( 2, "selection", 640, "hud2", 5, 6, 7, 8 );
	count = CoF_HudSpriteDefaults( list, 3, 400 );
	CHECK( count == 3 + (int)COF_HUD_MISSING_COUNT - 2, "only the names the list lacks" );
	CHECK( find( count, "bucket1", 640 ) == 1 && list[1].rc.right == 4, "the list's own bucket1 kept" );

	// two resolutions: each gets its own entries, with its own first sprite
	put( 0, "number_0", 320, "hud_small", 0, 0, 10, 12 );
	put( 1, "number_0", 640, "hud_big", 0, 0, 20, 24 );
	count = CoF_HudSpriteDefaults( list, 2, 400 );
	CHECK( count == 2 + 2 * (int)COF_HUD_MISSING_COUNT, "every resolution gets the names" );
	CHECK( !strcmp( list[find( count, "flash_full", 320 )].szSprite, "hud_small" ), "320 uses its own sprite" );
	CHECK( !strcmp( list[find( count, "flash_full", 640 )].szSprite, "hud_big" ), "640 uses its own sprite" );

	// never past the room it was given; an empty list gets nothing
	put( 0, "number_0", 640, "hud1", 0, 0, 20, 24 );
	CHECK( CoF_HudSpriteDefaults( list, 1, 5 ) == 5, "stops at the capacity" );
	CHECK( CoF_HudSpriteDefaults( list, 0, 400 ) == 0, "an empty list stays empty" );

	printf( "hud sprite scenarios passed\n" );
	return 0;
}
