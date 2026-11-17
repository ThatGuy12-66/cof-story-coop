// SPDX-License-Identifier: BSD-3-Clause
// Cry of Fear panel identification (cof_panel_pause): the one place the
// support library asks the operating system whether memory may be read.
// Kept apart so <windows.h> never meets the VGUI headers.

#include <stddef.h>

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace vgui
{

bool CofMemReadable( const void *ptr, size_t size )
{
#if defined( _WIN32 )
	const char	*p = (const char *)ptr;
	const char	*end = p + size;

	if( !ptr || end < p )
		return false;

	while( p < end )
	{
		MEMORY_BASIC_INFORMATION	mbi;
		const DWORD	readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
			| PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

		if( !VirtualQuery( p, &mbi, sizeof( mbi )))
			return false;

		if( mbi.State != MEM_COMMIT || ( mbi.Protect & ( PAGE_GUARD | PAGE_NOACCESS )) || !( mbi.Protect & readable ))
			return false;

		p = (const char *)mbi.BaseAddress + mbi.RegionSize;
	}

	return true;
#else
	(void)ptr;
	(void)size;
	return false;
#endif
}

} // namespace vgui
