/*
cof_entpatch.c - Cry of Fear language packs: entity patches (maps/<map>.entpatch)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

====================================================================

A language pack translates a map's in-world text (game_text messages, door
"locked" lines, trigger hints, ...) with an entity PATCH rather than a whole
replacement entity lump: only the translated key/value pairs, each addressed
by a selector, applied to whatever entity string the map would otherwise
load (the BSP lump, or a gamedir maps/<map>.ent override).

	{
	"@entity" "57"              index hint
	"@classname" "game_text"    selectors (first value of that key, exact)
	"@targetname" "hint3"
	"@crc:message" "1a2b3c4d"   optional: CRC-32 of the value being replaced
	"message" "Polski tekst"    set (first occurrence) or append
	}
	{ "@op" "remove" ...selectors... }   drop the selected entity
	{ "@op" "add" "classname" "..." }    append a new entity

Selection: the candidates are the (not yet removed) entities whose selectors
all match; the one at @entity if any, else the only one, else the one nearest
to @entity (a tie is ambiguous). A block that selects nothing, or a key whose
current value fails its CRC, is skipped and logged - a patch never guesses.

Splicing keeps every byte of the base string that is not a replaced value, so
a pack .ent made by the old generator and base + .entpatch are byte-identical
(checked for all 100 Polish maps in stage1/lang3-20260922).

This file is plain C with no engine dependencies; the Python reference
implementation is scripts/polish/entpatch.py and the two are tested against
each other.
====================================================================
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cof_entpatch.h"

typedef struct
{
	int ks, ke, vs, ve;	// key and value content spans (without quotes)
} ep_pair_t;

typedef struct
{
	int open, close;	// offsets of '{' and '}'
	int first, count;	// pairs
} ep_ent_t;

typedef struct
{
	const char *p;
	int len;
	int set;
} ep_val_t;

typedef struct
{
	int ent;
	int pk_s, pk_e, pv_s, pv_e;	// key and value spans in the PATCH buffer
} ep_append_t;

typedef struct
{
	char *data;
	size_t len, size;
	int failed;
} ep_buf_t;

typedef struct
{
	void *items;
	int count, size;
	size_t item;
} ep_vec_t;

static void *EP_VecPush( ep_vec_t *v )
{
	if( v->count == v->size )
	{
		int newsize = v->size ? v->size * 2 : 64;
		void *n = realloc( v->items, (size_t)newsize * v->item );

		if( !n )
			return NULL;
		v->items = n;
		v->size = newsize;
	}
	memset( (char *)v->items + (size_t)v->count * v->item, 0, v->item );
	return (char *)v->items + (size_t)v->count++ * v->item;
}

static void EP_Put( ep_buf_t *b, const char *p, size_t len )
{
	if( b->failed || !len )
		return;

	if( b->len + len + 1 > b->size )
	{
		size_t newsize = b->size ? b->size : 4096;
		char *n;

		while( newsize < b->len + len + 1 )
			newsize *= 2;

		n = realloc( b->data, newsize );
		if( !n )
		{
			b->failed = 1;
			return;
		}
		b->data = n;
		b->size = newsize;
	}

	memcpy( b->data + b->len, p, len );
	b->len += len;
	b->data[b->len] = 0;
}

static unsigned int EP_Crc32( const char *p, int len )
{
	unsigned int crc = 0xFFFFFFFFu;
	int i, k;

	for( i = 0; i < len; i++ )
	{
		crc ^= (unsigned char)p[i];
		for( k = 0; k < 8; k++ )
			crc = ( crc >> 1 ) ^ ( 0xEDB88320u & ( 0u - ( crc & 1u )));
	}
	return crc ^ 0xFFFFFFFFu;
}

static void EP_Log( cof_entpatch_log_t log, void *ctx, const char *fmt, int a, int b, const char *s, int slen )
{
	char msg[512], str[128];
	int n;

	if( !log )
		return;

	n = slen < (int)sizeof( str ) - 1 ? slen : (int)sizeof( str ) - 1;
	if( n < 0 ) n = 0;
	if( s ) memcpy( str, s, (size_t)n );
	str[n] = 0;

	snprintf( msg, sizeof( msg ), fmt, a, b, str );
	log( ctx, msg );
}

static int EP_IsSpace( char c )
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// the base entity string: '{' ( '"' key '"' ws '"' value '"' )* '}' ...
static int EP_ParseBase( const char *d, int n, ep_vec_t *ents, ep_vec_t *pairs )
{
	int i = 0;

	while( 1 )
	{
		ep_ent_t *e;

		while( i < n && EP_IsSpace( d[i] )) i++;
		if( i >= n )
			return 1;
		if( d[i] != '{' )
			return 0;

		if( !( e = EP_VecPush( ents )))
			return 0;
		e->open = i++;
		e->first = pairs->count;
		e->count = 0;

		while( 1 )
		{
			ep_pair_t *p;
			const char *q;
			int ks, ke, vs, ve;

			while( i < n && EP_IsSpace( d[i] )) i++;
			if( i >= n )
				return 0;
			if( d[i] == '}' )
			{
				// re-fetch: EP_VecPush may have moved the array
				e = (ep_ent_t *)ents->items + ents->count - 1;
				e->close = i++;
				break;
			}
			if( d[i] != '"' )
				return 0;
			ks = i + 1;
			q = memchr( d + ks, '"', (size_t)( n - ks ));
			if( !q )
				return 0;
			ke = (int)( q - d );
			i = ke + 1;

			while( i < n && EP_IsSpace( d[i] )) i++;
			if( i >= n || d[i] != '"' )
				return 0;
			vs = i + 1;
			q = memchr( d + vs, '"', (size_t)( n - vs ));
			if( !q )
				return 0;
			ve = (int)( q - d );
			i = ve + 1;

			if( !( p = EP_VecPush( pairs )))
				return 0;
			p->ks = ks; p->ke = ke; p->vs = vs; p->ve = ve;
			e = (ep_ent_t *)ents->items + ents->count - 1;
			e->count++;
		}
	}
}

// the patch: // comments, '{' ( '"' key '"' '"' value '"' )* '}' blocks
static int EP_ParsePatch( const char *d, int n, ep_vec_t *blocks, ep_vec_t *pairs )
{
	int i = 0, inblock = 0, havekey = 0;
	int ks = 0, ke = 0;
	ep_ent_t *b = NULL;

	while( i < n )
	{
		char c = d[i];

		if( EP_IsSpace( c ))
			i++;
		else if( c == '/' && i + 1 < n && d[i + 1] == '/' )
		{
			while( i < n && d[i] != '\n' ) i++;
		}
		else if( c == '{' )
		{
			if( inblock || !( b = EP_VecPush( blocks )))
				return 0;
			b->first = pairs->count;
			b->count = 0;
			inblock = 1;
			i++;
		}
		else if( c == '}' )
		{
			if( !inblock || havekey )
				return 0;
			inblock = 0;
			i++;
		}
		else if( c == '"' )
		{
			const char *q = memchr( d + i + 1, '"', (size_t)( n - i - 1 ));
			int s = i + 1, e;

			if( !q || !inblock )
				return 0;
			e = (int)( q - d );
			i = e + 1;

			if( !havekey )
			{
				ks = s; ke = e;
				havekey = 1;
			}
			else
			{
				ep_pair_t *p = EP_VecPush( pairs );

				if( !p )
					return 0;
				p->ks = ks; p->ke = ke; p->vs = s; p->ve = e;
				b = (ep_ent_t *)blocks->items + blocks->count - 1;
				b->count++;
				havekey = 0;
			}
		}
		else return 0;
	}

	return !inblock;
}

static int EP_Eq( const char *a, int alen, const char *b, int blen )
{
	return alen == blen && !memcmp( a, b, (size_t)alen );
}

static int EP_EqStr( const char *a, int alen, const char *lit )
{
	return EP_Eq( a, alen, lit, (int)strlen( lit ));
}

static int EP_StartsWith( const char *a, int alen, const char *lit )
{
	int l = (int)strlen( lit );
	return alen >= l && !memcmp( a, lit, (size_t)l );
}

// first pair of entity `e` whose key is k, or -1
static int EP_FindKey( const char *base, const ep_ent_t *e, const ep_pair_t *pairs, const char *k, int klen )
{
	int j;

	for( j = 0; j < e->count; j++ )
	{
		const ep_pair_t *p = &pairs[e->first + j];
		if( EP_Eq( base + p->ks, p->ke - p->ks, k, klen ))
			return e->first + j;
	}
	return -1;
}

static int EP_ParseInt( const char *s, int len, int *out )
{
	char tmp[32];
	char *end;
	long v;

	if( len <= 0 || len >= (int)sizeof( tmp ))
		return 0;
	memcpy( tmp, s, (size_t)len );
	tmp[len] = 0;
	v = strtol( tmp, &end, 10 );
	if( *end )
		return 0;
	*out = (int)v;
	return 1;
}

char *CoF_EntPatch_Apply( const char *base, size_t baselen, const char *patch, size_t patchlen,
	size_t *outlen, cof_entpatch_stats_t *stats, cof_entpatch_log_t log, void *ctx )
{
	ep_vec_t ents = { NULL, 0, 0, sizeof( ep_ent_t ) };
	ep_vec_t bpairs = { NULL, 0, 0, sizeof( ep_pair_t ) };
	ep_vec_t blocks = { NULL, 0, 0, sizeof( ep_ent_t ) };
	ep_vec_t ppairs = { NULL, 0, 0, sizeof( ep_pair_t ) };
	ep_vec_t appends = { NULL, 0, 0, sizeof( ep_append_t ) };
	ep_vec_t adds = { NULL, 0, 0, sizeof( int ) };
	ep_val_t *newval = NULL;
	unsigned char *removed = NULL;
	ep_buf_t out = { NULL, 0, 0, 0 };
	cof_entpatch_stats_t st = { 0 };
	const char *nul;
	char *result = NULL;
	int n, bi, i, cur;

	if( outlen ) *outlen = 0;

	nul = memchr( base, 0, baselen );
	n = nul ? (int)( nul - base ) : (int)baselen;

	if( !EP_ParseBase( base, n, &ents, &bpairs ))
	{
		EP_Log( log, ctx, "entity string could not be parsed%.0d%.0d%s", 0, 0, "", 0 );
		goto done;
	}
	if( !EP_ParsePatch( patch, (int)patchlen, &blocks, &ppairs ))
	{
		EP_Log( log, ctx, "patch file could not be parsed%.0d%.0d%s", 0, 0, "", 0 );
		goto done;
	}

	newval = calloc( (size_t)bpairs.count + 1, sizeof( *newval ));
	removed = calloc( (size_t)ents.count + 1, 1 );
	if( !newval || !removed )
		goto done;

	st.blocks = blocks.count;

	for( bi = 0; bi < blocks.count; bi++ )
	{
		const ep_ent_t *b = (const ep_ent_t *)blocks.items + bi;
		const ep_pair_t *bp = (const ep_pair_t *)ppairs.items + b->first;
		const ep_ent_t *E = (const ep_ent_t *)ents.items;
		const ep_pair_t *P = (const ep_pair_t *)bpairs.items;
		const char *op = "edit";
		int oplen = 4, hint = -1, badhint = 0;
		int pick = -1, ncand = 0, best = -1, bestd = 0, tie = 0;

		for( i = 0; i < b->count; i++ )
		{
			const char *k = patch + bp[i].ks;
			int kl = bp[i].ke - bp[i].ks;

			if( EP_EqStr( k, kl, "@op" ))
			{
				op = patch + bp[i].vs;
				oplen = bp[i].ve - bp[i].vs;
			}
			else if( EP_EqStr( k, kl, "@entity" ))
			{
				if( !EP_ParseInt( patch + bp[i].vs, bp[i].ve - bp[i].vs, &hint ))
					badhint = 1;
			}
		}

		if( EP_EqStr( op, oplen, "add" ))
		{
			int *a = EP_VecPush( &adds );
			if( !a ) goto done;
			*a = bi;
			st.added++;
			continue;
		}

		if( !EP_EqStr( op, oplen, "edit" ) && !EP_EqStr( op, oplen, "remove" ))
		{
			EP_Log( log, ctx, "block %d: unknown @op \"%.0d%s\"", bi, 0, op, oplen );
			st.skipped++;
			continue;
		}

		if( badhint )
		{
			EP_Log( log, ctx, "block %d: bad @entity%.0d%s", bi, 0, "", 0 );
			st.skipped++;
			continue;
		}

		// candidates
		for( i = 0; i < ents.count; i++ )
		{
			int j, ok = 1;

			if( removed[i] )
				continue;

			for( j = 0; j < b->count && ok; j++ )
			{
				const char *k = patch + bp[j].ks;
				int kl = bp[j].ke - bp[j].ks, pi;

				if( kl < 1 || k[0] != '@' || EP_EqStr( k, kl, "@entity" ) || EP_EqStr( k, kl, "@op" ) || EP_StartsWith( k, kl, "@crc:" ))
					continue;

				pi = EP_FindKey( base, &E[i], P, k + 1, kl - 1 );
				if( pi < 0 || !EP_Eq( base + P[pi].vs, P[pi].ve - P[pi].vs, patch + bp[j].vs, bp[j].ve - bp[j].vs ))
					ok = 0;
			}

			if( !ok )
				continue;

			ncand++;
			if( i == hint )
				pick = i;

			if( hint >= 0 )
			{
				int d = i > hint ? i - hint : hint - i;

				if( best < 0 || d < bestd )
				{
					best = i; bestd = d; tie = 0;
				}
				else if( d == bestd )
					tie = 1;
			}
			else best = i;
		}

		if( pick < 0 )
		{
			if( ncand == 0 )
			{
				EP_Log( log, ctx, "block %d (@entity %d): no entity matches%s", bi, hint, "", 0 );
				st.skipped++;
				continue;
			}
			if( ncand == 1 )
				pick = best;
			else if( hint < 0 )
			{
				EP_Log( log, ctx, "block %d: %d entities match and no @entity hint%s", bi, ncand, "", 0 );
				st.skipped++;
				continue;
			}
			else if( tie )
			{
				EP_Log( log, ctx, "block %d (@entity %d): ambiguous, two matches equally near%s", bi, hint, "", 0 );
				st.skipped++;
				continue;
			}
			else pick = best;
		}

		if( EP_EqStr( op, oplen, "remove" ))
		{
			removed[pick] = 1;
			st.removed++;
			continue;
		}

		// edit
		for( i = 0; i < b->count; i++ )
		{
			const char *k = patch + bp[i].ks;
			int kl = bp[i].ke - bp[i].ks, pi, j, crcidx = -1;

			if( kl >= 1 && k[0] == '@' )
				continue;

			pi = EP_FindKey( base, &E[pick], P, k, kl );
			if( pi < 0 )
			{
				ep_append_t *a = EP_VecPush( &appends );

				if( !a ) goto done;
				a->ent = pick;
				a->pk_s = bp[i].ks; a->pk_e = bp[i].ke;
				a->pv_s = bp[i].vs; a->pv_e = bp[i].ve;
				st.edited++;
				continue;
			}

			// the last "@crc:<key>" of the block wins
			for( j = 0; j < b->count; j++ )
			{
				const char *ck = patch + bp[j].ks;
				int ckl = bp[j].ke - bp[j].ks;

				if( EP_StartsWith( ck, ckl, "@crc:" ) && EP_Eq( ck + 5, ckl - 5, k, kl ))
					crcidx = j;
			}

			if( crcidx >= 0 )
			{
				const char *cv = newval[pi].set ? newval[pi].p : base + P[pi].vs;
				int cvl = newval[pi].set ? newval[pi].len : P[pi].ve - P[pi].vs;
				char want[16], have[16];
				int wl = bp[crcidx].ve - bp[crcidx].vs, x;

				snprintf( have, sizeof( have ), "%08x", EP_Crc32( cv, cvl ));
				if( wl != 8 )
					x = 0;
				else
				{
					memcpy( want, patch + bp[crcidx].vs, 8 );
					want[8] = 0;
					for( x = 0; x < 8; x++ )
						if( want[x] >= 'A' && want[x] <= 'F' ) want[x] = (char)( want[x] - 'A' + 'a' );
					x = !strcmp( want, have );
				}

				if( !x )
				{
					EP_Log( log, ctx, "block %d: entity %d key \"%s\" has a different value (crc); not replaced", bi, pick - 0, k, kl );
					st.skipped++;
					continue;
				}
			}

			newval[pi].p = patch + bp[i].vs;
			newval[pi].len = bp[i].ve - bp[i].vs;
			newval[pi].set = 1;
			st.edited++;
		}
	}

	// splice
	cur = 0;
	for( i = 0; i < ents.count; i++ )
	{
		const ep_ent_t *e = (const ep_ent_t *)ents.items + i;
		const ep_pair_t *P = (const ep_pair_t *)bpairs.items;
		const ep_append_t *A = (const ep_append_t *)appends.items;
		int j, any = 0;

		if( removed[i] )
		{
			int end = e->close + 1;

			EP_Put( &out, base + cur, (size_t)( e->open - cur ));
			if( end < n && base[end] == '\n' )
				end++;
			cur = end;
			continue;
		}

		for( j = 0; j < e->count; j++ )
		{
			const ep_pair_t *p = &P[e->first + j];
			const ep_val_t *v = &newval[e->first + j];

			if( !v->set )
				continue;
			EP_Put( &out, base + cur, (size_t)( p->vs - cur ));
			EP_Put( &out, v->p, (size_t)v->len );
			cur = p->ve;
		}

		for( j = 0; j < appends.count; j++ )
		{
			if( A[j].ent != i )
				continue;
			if( !any )
			{
				EP_Put( &out, base + cur, (size_t)( e->close - cur ));
				cur = e->close;
				any = 1;
			}
			EP_Put( &out, "\"", 1 );
			EP_Put( &out, patch + A[j].pk_s, (size_t)( A[j].pk_e - A[j].pk_s ));
			EP_Put( &out, "\" \"", 3 );
			EP_Put( &out, patch + A[j].pv_s, (size_t)( A[j].pv_e - A[j].pv_s ));
			EP_Put( &out, "\"\n", 2 );
		}
	}
	EP_Put( &out, base + cur, (size_t)( n - cur ));

	if( adds.count && out.len && out.data[out.len - 1] != '\n' )
		EP_Put( &out, "\n", 1 );

	for( i = 0; i < adds.count; i++ )
	{
		const ep_ent_t *b = (const ep_ent_t *)blocks.items + ((int *)adds.items)[i];
		const ep_pair_t *bp = (const ep_pair_t *)ppairs.items + b->first;
		int j;

		EP_Put( &out, "{\n", 2 );
		for( j = 0; j < b->count; j++ )
		{
			if( bp[j].ke > bp[j].ks && patch[bp[j].ks] == '@' )
				continue;
			EP_Put( &out, "\"", 1 );
			EP_Put( &out, patch + bp[j].ks, (size_t)( bp[j].ke - bp[j].ks ));
			EP_Put( &out, "\" \"", 3 );
			EP_Put( &out, patch + bp[j].vs, (size_t)( bp[j].ve - bp[j].vs ));
			EP_Put( &out, "\"\n", 2 );
		}
		EP_Put( &out, "}\n", 2 );
	}

	if( out.failed )
		goto done;

	if( !out.data )	// empty result
	{
		out.data = calloc( 1, 1 );
		if( !out.data )
			goto done;
	}

	result = out.data;
	out.data = NULL;
	if( outlen ) *outlen = out.len;

done:
	free( out.data );
	free( newval );
	free( removed );
	free( ents.items );
	free( bpairs.items );
	free( blocks.items );
	free( ppairs.items );
	free( appends.items );
	free( adds.items );
	if( stats ) *stats = st;
	return result;
}
