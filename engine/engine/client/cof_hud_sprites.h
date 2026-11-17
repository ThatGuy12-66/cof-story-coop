/*
cof_hud_sprites.h - Cry of Fear story co-op 1.2.9: an empty sprites/hud.txt entry for each name client.dll asks for
and Cry of Fear's hud.txt does not have. GPL-3.0-or-later.

Cry of Fear's client.dll asks GetSpriteIndex for 15 names its own hud.txt lacks (the Half-Life weapon buckets,
flashlight, suit, damage and title sprites). Each answer is -1, and the HUD reads m_rgrcRects[-1] and
m_rghSprites[-1], just before those arrays: the heap block's header, harmless until the heap puts the rects at the
start of a page after one that is not mapped. A Chapter 4 (c_park3) start, 2026-10-24: CHudAmmo::VidInit (client.dll
1001BF0E) read 12 bytes before the rects for "bucket1" and the game closed with C0000005, every time; a full dump
showed m_HUD_bucket0 -1 and the rects 8 bytes into a page. Each missing name now gets an entry at every resolution
the list has, with that resolution's first sprite and an empty rectangle: the HUD reads 0 x 0 where it read the heap
header, and draws nothing where it drew whatever the header held.

SPR_GetList (cl_game.c) makes room for them and calls CoF_HudSpriteDefaults on "sprites/hud.txt" only.
*/
#ifndef COF_HUD_SPRITES_H
#define COF_HUD_SPRITES_H

static const char *const cof_hud_missing_names[] =
{
	"bucket1", "selection", "machinegun", "cof_m76_mag", "cof_m76_mag_icon", "cross", "dmg_bio", "d_skull",
	"flash_beam", "flash_empty", "flash_full", "suit_empty", "suit_full", "title_half", "title_life",
};

#define COF_HUD_MISSING_COUNT ( sizeof( cof_hud_missing_names ) / sizeof( cof_hud_missing_names[0] ))
#define COF_HUD_RES_MAX 4   // distinct resolutions given defaults (Cry of Fear's list has one, 640)
#define COF_HUD_EXTRA ((int)COF_HUD_MISSING_COUNT * COF_HUD_RES_MAX)

static int CoF_HudSpriteListHas( const client_sprite_t *list, int count, const char *name, int res )
{
	int i;

	for( i = 0; i < count; i++ )
	{
		if( list[i].iRes == res && !Q_stricmp( list[i].szName, name ))
			return 1;
	}
	return 0;
}

/*
Adds the missing names to list (count entries, room for capacity) and returns the new count; the entries the file
gave are left as they are.
*/
static int CoF_HudSpriteDefaults( client_sprite_t *list, int count, int capacity )
{
	int res_first[COF_HUD_RES_MAX];
	int res_count = 0, parsed = count, i, r;
	size_t n;

	for( i = 0; i < parsed && res_count < COF_HUD_RES_MAX; i++ )
	{
		for( r = 0; r < res_count; r++ )
		{
			if( list[res_first[r]].iRes == list[i].iRes )
				break;
		}
		if( r == res_count )
			res_first[res_count++] = i;
	}

	for( r = 0; r < res_count; r++ )
	{
		const client_sprite_t *model = &list[res_first[r]];

		for( n = 0; n < COF_HUD_MISSING_COUNT && count < capacity; n++ )
		{
			client_sprite_t *entry;

			if( CoF_HudSpriteListHas( list, count, cof_hud_missing_names[n], model->iRes ))
				continue;

			entry = &list[count++];
			memset( entry, 0, sizeof( *entry ));
			Q_strncpy( entry->szName, cof_hud_missing_names[n], sizeof( entry->szName ));
			Q_strncpy( entry->szSprite, model->szSprite, sizeof( entry->szSprite ));
			entry->iRes = model->iRes;
		}
	}

	return count;
}

#endif // COF_HUD_SPRITES_H
