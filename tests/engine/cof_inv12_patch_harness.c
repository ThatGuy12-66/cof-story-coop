/* The 12-slot inventory sites against the real retail hl.dll, mapped (never run)
 * by the Windows loader: every expected byte is there, a group goes on and off
 * as a whole, and unknown bytes or a failed write leave nothing behind. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "patch_under_test.h"

static int writes, fail_at = -1;

static int Write( unsigned char *p, const unsigned char *expect, const unsigned char *value, int len )
{
	DWORD protect, ignored;
	if( writes++ == fail_at ) return 0;
	if( memcmp( p, expect, len ) || !VirtualProtect( p, len, PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( p, value, len );
	VirtualProtect( p, len, protect, &ignored );
	return 1;
}

static void Poke( unsigned char *p, unsigned char value )
{
	DWORD protect, ignored;
	assert( VirtualProtect( p, 1, PAGE_EXECUTE_READWRITE, &protect ));
	*p = value;
	VirtualProtect( p, 1, protect, &ignored );
}

#define KEEP cof_inv12_keep, COF_INV12_COUNT( cof_inv12_keep )
#define WIDE cof_inv12_wide, COF_INV12_COUNT( cof_inv12_wide )

int main( int argc, char **argv )
{
	unsigned char *base;
	assert( argc == 2 );
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base );
	assert( COF_INV12_COUNT( cof_inv12_keep ) == 18 && COF_INV12_COUNT( cof_inv12_wide ) == 11 );
	assert( CoF_Inv12GroupState( base, KEEP ) == 0 && CoF_Inv12GroupState( base, WIDE ) == 0 );

	assert( CoF_Inv12GroupSet( base, KEEP, 1, Write ));
	assert( CoF_Inv12GroupSet( base, WIDE, 1, Write ));
	assert( CoF_Inv12GroupState( base, KEEP ) == 1 && CoF_Inv12GroupState( base, WIDE ) == 1 );
	assert( base[0xDADB1 + 2] == 0x0c ); /* AddInventoryItem: cmp esi,12 */
	assert( base[0xE30C2 + 2] == 0x0b ); /* CombineInventoryItems: slot - 1 <= 11 */
	assert( base[0xDDA3A] == 0x90 && base[0xDDA3A + 10] == 0x90 ); /* models[slot] = 0 is gone */

	writes = 0; /* already there: nothing is written */
	assert( CoF_Inv12GroupSet( base, KEEP, 1, Write ) && writes == 0 );

	/* back to six slots: the keep group stays */
	assert( CoF_Inv12GroupSet( base, WIDE, 0, Write ));
	assert( CoF_Inv12GroupState( base, WIDE ) == 0 && CoF_Inv12GroupState( base, KEEP ) == 1 );

	/* a write that fails half way: the group is rolled back */
	writes = 0; fail_at = 4;
	assert( !CoF_Inv12GroupSet( base, WIDE, 1, Write ));
	assert( CoF_Inv12GroupState( base, WIDE ) == 0 );
	fail_at = -1;

	/* one unknown byte anywhere: refuse before writing anything */
	Poke( base + cof_inv12_wide[5].rva + 2, 0x07 );
	writes = 0;
	assert( !CoF_Inv12GroupSet( base, WIDE, 1, Write ) && writes == 0 );
	assert( CoF_Inv12GroupState( base, WIDE ) == -1 );
	Poke( base + cof_inv12_wide[5].rva + 2, 0x06 );

	assert( CoF_Inv12GroupSet( base, KEEP, 0, Write ) && CoF_Inv12GroupState( base, KEEP ) == 0 );
	FreeLibrary( (HMODULE)base );
	puts( "inventory12 hl.dll sites passed" );
	return 0;
}
