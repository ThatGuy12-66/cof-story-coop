/* client.dll's slot accessor (1006CDD0) hooked to the engine's store, against
 * the real retail client.dll, mapped (never initialised) by the Windows loader
 * at a base other than its preferred one, as ASLR may load it. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "state_under_test.h"
#include "patch_under_test.h"

static cof_inv12_slots_t store;
static char * __stdcall Accessor( int slot ) { return CoF_Inv12Slot( &store, slot ); }
/* __thiscall: self in ecx, the slot on the stack, ret 4 - the same as __fastcall( self, edx, slot ) */
typedef char *( __fastcall *accessor_t )( void *self, void *unused, int slot );

static void Poke( unsigned char *p, unsigned char value )
{
	DWORD protect, ignored;
	assert( VirtualProtect( p, 1, PAGE_EXECUTE_READWRITE, &protect ));
	*p = value;
	VirtualProtect( p, 1, protect, &ignored );
}

int main( int argc, char **argv )
{
	static char hud[0x3200]; /* stands in for gHUD: the stock accessor returns hud + 0x2C64 + ( n - 1 ) * 0x80 */
	cof_inv12_hook_t hook;
	unsigned char *base;
	accessor_t call;
	void *taken;
	assert( argc == 2 );
	memset( &hook, 0, sizeof( hook ));
	taken = VirtualAlloc( (void *)0x10000000, 0x1000, MEM_RESERVE, PAGE_NOACCESS ); /* push the DLL off its preferred base */
	base = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
	assert( base );
	assert( !taken || base != (unsigned char *)0x10000000 );
	call = (accessor_t)( base + COF_INV12_ACCESSOR );

	/* stock */
	assert( call( hud, NULL, 1 ) == hud + 0x2C64 && call( hud, NULL, 6 ) == hud + 0x2C64 + 5 * 0x80 );

	/* hooked: 1..12 are the store, anything else the scratch buffer */
	assert( CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor ));
	assert( !CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor )); /* once */
	assert( call( hud, NULL, 1 ) == store.slot[1] && call( hud, NULL, 7 ) == store.slot[7] && call( hud, NULL, 12 ) == store.slot[12] );
	assert( call( hud, NULL, 13 ) == store.scratch && call( hud, NULL, 0 ) == store.scratch );
	strcpy( call( hud, NULL, 13 ), "x" ); /* the stock fallback was read-only: this crashed client.dll */

	/* unhooked: the stock bytes and behaviour are back */
	CoF_Inv12UnhookAccessor( &hook );
	assert( !hook.site && !memcmp( base + COF_INV12_ACCESSOR, cof_inv12_accessor_stock, sizeof( cof_inv12_accessor_stock )));
	assert( call( hud, NULL, 2 ) == hud + 0x2C64 + 0x80 );

	/* unexpected bytes: refuse, write nothing */
	Poke( base + COF_INV12_ACCESSOR + 9, 0x0b );
	assert( !CoF_Inv12HookAccessor( &hook, base, (const void *)Accessor ) && base[COF_INV12_ACCESSOR] == 0x55 );
	Poke( base + COF_INV12_ACCESSOR + 9, 0x05 );
	FreeLibrary( (HMODULE)base );
	puts( "inventory12 client.dll accessor passed" );
	return 0;
}
