/* Cry of Fear inventory descriptors (cryoffear/inventoryitems/*.txt).
 * Pure helpers, no engine types. GPL-3.0-or-later.
 * A descriptor is a list of blocks:  "key" { data "value" }
 * Story items are the descriptors directly under inventoryitems/; weapons/,
 * ammo/ and outofit/ hold weapons, ammo and costumes. */
#ifndef SV_COF_DESCRIPTOR_H
#define SV_COF_DESCRIPTOR_H
#include <string.h>

static const char *CoF_DescSkipSpace( const char *p )
{
	while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ) p++;
	return p;
}

// A quoted token at p (after spaces) into out, truncated to fit. Returns the
// position after its closing quote, or NULL without a complete quoted token.
static const char *CoF_DescQuoted( const char *p, char *out, size_t size )
{
	size_t n = 0;
	p = CoF_DescSkipSpace( p );
	if( *p != '"' ) return NULL;
	for( p++; *p && *p != '"'; p++ )
		if( n + 1 < size ) out[n++] = *p;
	out[n] = 0;
	return *p == '"' ? p + 1 : NULL;
}

// The value of "key" { data "value" }: 1 with out filled, or 0 with out empty.
static int CoF_DescriptorValue( const char *text, const char *key, char *out, size_t size )
{
	char quoted[80];
	size_t length = strlen( key );
	const char *p;
	if( !size ) return 0;
	out[0] = 0;
	if( length + 3 > sizeof( quoted )) return 0;
	quoted[0] = '"';
	memcpy( quoted + 1, key, length );
	quoted[length + 1] = '"';
	quoted[length + 2] = 0;
	for( p = strstr( text, quoted ); p; p = strstr( p + 1, quoted ))
	{
		const char *q = CoF_DescSkipSpace( p + length + 2 );
		if( *q != '{' ) continue; // the text appeared as a value, not a key
		q = CoF_DescSkipSpace( q + 1 );
		if( strncmp( q, "data", 4 )) continue;
		if( CoF_DescQuoted( q + 4, out, size )) return 1;
		out[0] = 0;
	}
	return 0;
}

// Descriptor paths as the game ships them: letters, digits, '_', '-', '.', '/'
// and no "..". Anything else is refused before it can reach a console command
// or a file path (and so never enters the story item ledger).
static int CoF_DescriptorSafe( const char *d )
{
	const char *c;
	if( !d || !d[0] || strstr( d, ".." )) return 0;
	for( c = d; *c; c++ )
		if( !(( *c >= 'a' && *c <= 'z' ) || ( *c >= 'A' && *c <= 'Z' ) || ( *c >= '0' && *c <= '9' ) ||
			*c == '_' || *c == '-' || *c == '.' || *c == '/' )) return 0;
	return 1;
}

static int CoF_IsStoryItem( const char *descriptor )
{
	static const char prefix[] = "inventoryitems/";
	const char *name;
	size_t length;
	if( !CoF_DescriptorSafe( descriptor ) || strncmp( descriptor, prefix, sizeof( prefix ) - 1 )) return 0;
	name = descriptor + sizeof( prefix ) - 1;
	length = strlen( name );
	if( length <= 4 || strchr( name, '/' ) || strchr( name, '\\' )) return 0;
	return !strcmp( name + length - 4, ".txt" );
}
#endif
