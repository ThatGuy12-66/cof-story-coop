/* sv_cof_inventory12_patch.h - Cry of Fear story co-op: the retail hl.dll sites
 * of the 12-slot inventory. GPL-3.0-or-later.
 *
 * Pure: no engine types. Included by sv_cof_inventory12.c and by the offline
 * harness, which runs it against the real DLL. Cry of Fear 1.6 hl.dll, sha256
 * 0036B91C...0E63; addresses are RVAs (VA - 0x10000000). The disassembly behind
 * every site: evidence/inventory-grid-20261013/server-inventory-re.md.
 *
 * inventorySlot[7] (+0x21FC) grows in place over the arrays after it, which the
 * game only ever zeroes: models[7] (+0x2218), classnames[7] (+0x2234) and the
 * unsaved client copy (+0x2250). Slots 7..12 sit on models[0..5].
 */
#ifndef SV_COF_INVENTORY12_PATCH_H
#define SV_COF_INVENTORY12_PATCH_H
#include <string.h>

#define COF_INV12_SITE_MAX 11
#define COF_INV12_COUNT( table ) ((int)( sizeof( table ) / sizeof(( table )[0] )))

typedef struct
{
	unsigned int rva;
	int len;
	unsigned char stock[COF_INV12_SITE_MAX];
	unsigned char patched[COF_INV12_SITE_MAX];
} cof_inv12_site_t;

/* Compares p with expect, then writes value; len bytes. Nonzero on success. */
typedef int (*cof_inv12_write_t)( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len );

#define COF_INV12_NOPS { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
#define COF_INV12_ESI   { 0xc7, 0x06, 0x00, 0x00, 0x00, 0x00 }             /* mov [esi], 0: models */
#define COF_INV12_ESI1C { 0xc7, 0x46, 0x1c, 0x00, 0x00, 0x00, 0x00 }       /* mov [esi+1Ch], 0: classnames */
#define COF_INV12_ESI38 { 0xc7, 0x46, 0x38, 0x00, 0x00, 0x00, 0x00 }       /* mov [esi+38h], 0: client copy */

/* keep: the zero-writes into the dead arrays. Applied whenever story co-op runs,
 * at six slots too: ClearInventorySlot's bAll loops write models[i], which is
 * slot i + 7, so a map at six slots would otherwise wipe slots 8..13. */
static const cof_inv12_site_t cof_inv12_keep[] =
{
	{ 0xDD9C4, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* ClearInventorySlot bAll, three-slot loop */
	{ 0xDD9CA, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDD9D1, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDD9FE, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* ClearInventorySlot bAll, six-slot loop */
	{ 0xDDA04, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDA0B, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDA3A, 11, { 0xc7, 0x84, 0x87, 0x18, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS }, /* ClearInventorySlot one slot */
	{ 0xDDA45, 11, { 0xc7, 0x84, 0x87, 0x34, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDA50, 11, { 0xc7, 0x84, 0x87, 0x50, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDACE, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* DeleteInventoryItemNamed bAll, three-slot loop */
	{ 0xDDAD4, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDADB, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDB0E, 6, COF_INV12_ESI, COF_INV12_NOPS },   /* DeleteInventoryItemNamed bAll, six-slot loop */
	{ 0xDDB14, 7, COF_INV12_ESI1C, COF_INV12_NOPS },
	{ 0xDDB1B, 7, COF_INV12_ESI38, COF_INV12_NOPS },
	{ 0xDDC0E, 11, { 0xc7, 0x84, 0xb3, 0x18, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS }, /* DeleteInventoryItemNamed one slot */
	{ 0xDDC19, 11, { 0xc7, 0x84, 0xb3, 0x34, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
	{ 0xDDC25, 11, { 0xc7, 0x84, 0xb3, 0x50, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, COF_INV12_NOPS },
};

/* wide: the slot loops and the combine and dual-wield ranges, 6 -> 12. Only on
 * maps at 12. Not here: SavePlayer 1002A866 (its coop record has six fixed weapon
 * entries; story co-op carries inventories through the engine's own save paths).
 * DualWield's range checks count since story co-op opens its multiplayer
 * refusal (sv_cof_story.c, COF_DUAL_GATE). */
static const cof_inv12_site_t cof_inv12_wide[] =
{
	{ 0xDADB1, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* AddInventoryItem: the first free slot */
	{ 0xDDA1B, 3, { 0x83, 0xfb, 0x06 }, { 0x83, 0xfb, 0x0c } }, /* ClearInventorySlot bAll */
	{ 0xDDB2B, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* DeleteInventoryItemNamed bAll */
	{ 0xDDBF3, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* DeleteInventoryItemNamed find */
	{ 0xE22ED, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* HasInventoryItem */
	{ 0xE4341, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* IsInventoryFull */
	{ 0xF75A0, 3, { 0x83, 0xff, 0x06 }, { 0x83, 0xff, 0x0c } }, /* UpdateClientData: the InitHUD resend */
	{ 0xC64D0, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* coop file loader resend */
	{ 0xC6D70, 3, { 0x83, 0xfe, 0x06 }, { 0x83, 0xfe, 0x0c } }, /* map .cfg item loader resend */
	{ 0xE30C2, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* CombineInventoryItems: first slot in 1..N */
	{ 0xE30CE, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* second slot in 1..N */
	{ 0xDEA0C, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* DualWield: first slot in 1..N */
	{ 0xDEA18, 3, { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x0b } }, /* second slot in 1..N */
};

/* 1 when every site holds its patched bytes, 0 when every site holds the stock
 * bytes, -1 for anything else. */
static int CoF_Inv12GroupState( const unsigned char *base, const cof_inv12_site_t *sites, int count )
{
	int i, stock = 0, patched = 0;
	for( i = 0; i < count; i++ )
	{
		if( !memcmp( base + sites[i].rva, sites[i].stock, sites[i].len )) stock++;
		else if( !memcmp( base + sites[i].rva, sites[i].patched, sites[i].len )) patched++;
	}
	return stock == count ? 0 : patched == count ? 1 : -1;
}

/* Puts one group (at most 32 sites) into its patched (on) or stock state, every
 * site or none: it refuses unless every site holds one of its two known byte
 * strings, and undoes its own writes when one fails. 1 on success. */
static int CoF_Inv12GroupSet( unsigned char *base, const cof_inv12_site_t *sites, int count, int on, cof_inv12_write_t write )
{
	unsigned int written = 0;
	int i, j;
	for( i = 0; i < count; i++ )
		if( memcmp( base + sites[i].rva, sites[i].stock, sites[i].len ) && memcmp( base + sites[i].rva, sites[i].patched, sites[i].len ))
			return 0;
	for( i = 0; i < count; i++ )
	{
		const unsigned char *from = on ? sites[i].stock : sites[i].patched;
		const unsigned char *to = on ? sites[i].patched : sites[i].stock;
		if( !memcmp( base + sites[i].rva, to, sites[i].len )) continue;
		if( !write( base + sites[i].rva, from, to, sites[i].len ))
		{
			for( j = 0; j < i; j++ )
				if( written & ( 1u << j ))
					write( base + sites[j].rva, on ? sites[j].patched : sites[j].stock, on ? sites[j].stock : sites[j].patched, sites[j].len );
			return 0;
		}
		written |= 1u << i;
	}
	return 1;
}

#endif /* SV_COF_INVENTORY12_PATCH_H */
