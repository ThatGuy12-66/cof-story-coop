/* cof_inventory12_patch.h - Cry of Fear story co-op: client.dll's slot-string
 * accessor redirected to the engine's 12-slot store. GPL-3.0-or-later.
 *
 * Win32 x86 only; included by cof_inventory12.c and by the offline harness,
 * which runs it against the real DLL. client.dll D2A04641...9C00, accessor at
 * RVA 0x6CDD0: char *__thiscall( int slot ), ret 4. The DLL is ASLR-relocatable
 * and only the accessor's first 15 bytes carry no relocation, so those are the
 * ones compared; the jump is computed from the real address. */
#ifndef COF_INVENTORY12_PATCH_H
#define COF_INVENTORY12_PATCH_H
#include <windows.h>
#include <stdint.h>
#include <string.h>

#define COF_INV12_ACCESSOR 0x6CDD0

/* push ebp; mov ebp,esp; mov eax,[ebp+8]; dec eax; cmp eax,5; ja +43h; jmp [eax*4+table] (opcode bytes) */
static const unsigned char cof_inv12_accessor_stock[15] = { 0x55, 0x8b, 0xec, 0x8b, 0x45, 0x08, 0x48, 0x83, 0xf8, 0x05, 0x77, 0x43, 0xff, 0x24, 0x85 };

typedef struct
{
	unsigned char *site;    // the hooked accessor, NULL when not hooked
	unsigned char saved[5]; // its first five bytes
} cof_inv12_hook_t;

/* A jmp rel32 to target, a char *__stdcall( int slot ): the jump keeps the
 * caller's return address and argument, so the target's ret 4 returns straight
 * to client.dll; ecx (the HUD) is not needed. 1 when written. */
static int CoF_Inv12HookAccessor( cof_inv12_hook_t *h, unsigned char *base, const void *target )
{
	unsigned char *p = base + COF_INV12_ACCESSOR, code[5];
	DWORD protect, ignored;
	int32_t rel;
	if( h->site || memcmp( p, cof_inv12_accessor_stock, sizeof( cof_inv12_accessor_stock ))) return 0;
	rel = (int32_t)((const unsigned char *)target - ( p + 5 ));
	code[0] = 0xe9;
	memcpy( code + 1, &rel, sizeof( rel ));
	if( !VirtualProtect( p, sizeof( code ), PAGE_EXECUTE_READWRITE, &protect )) return 0;
	memcpy( h->saved, p, sizeof( code ));
	memcpy( p, code, sizeof( code ));
	FlushInstructionCache( GetCurrentProcess(), p, sizeof( code ));
	VirtualProtect( p, sizeof( code ), protect, &ignored );
	h->site = p;
	return 1;
}

static void CoF_Inv12UnhookAccessor( cof_inv12_hook_t *h )
{
	DWORD protect, ignored;
	if( !h->site ) return;
	if( VirtualProtect( h->site, sizeof( h->saved ), PAGE_EXECUTE_READWRITE, &protect ))
	{
		memcpy( h->site, h->saved, sizeof( h->saved ));
		FlushInstructionCache( GetCurrentProcess(), h->site, sizeof( h->saved ));
		VirtualProtect( h->site, sizeof( h->saved ), protect, &ignored );
	}
	h->site = NULL;
}

#endif /* COF_INVENTORY12_PATCH_H */
