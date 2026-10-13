/* Retail InstallGameRules allocates the multiplayer rules without zeroing.
 * Runs the actual patch against x86 code laid out like the retail call site
 * (push 0E4h; call operator new; add esp,4) inside executable memory. */
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
#define true 1
#define false 0
#define XASH_WIN32 1
#define XASH_X86 1
#define XASH_COF_ENTVARS_LEGACY 1
#include "writegate_under_test.c"
#include "rules_under_test.c"

static int new_calls;
static size_t last_size;
static void *__cdecl FakeOperatorNew(size_t size)
{
    byte *p = malloc(size);
    memset(p, 0xa5, size); /* a recycled block: a phantom vote-kick is "active" */
    new_calls++;
    last_size = size;
    return p;
}
typedef void *(__cdecl *install_rules_t)(void);
static int AllZero(const byte *p, size_t n)
{
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

int main(void)
{
    static const byte site[] = { 0x68, 0xe4, 0x00, 0x00, 0x00, 0xe8, 0x46, 0xea, 0x08, 0x00, 0x83, 0xc4, 0x04, 0xc3 };
    byte *base = VirtualAlloc(NULL, 0x140000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    install_rules_t install;
    byte *rules;
    int32_t rel;
    assert(base);
    install = (install_rules_t)(base + 0xAF799);
    memset(base, 0xcc, 0x140000);
    memcpy(base + 0xAF799, site, sizeof(site));
    rel = (int32_t)((byte *)FakeOperatorNew - (base + 0x13E1E9 + 5));
    base[0x13E1E9] = 0xe9; /* the DLL's operator new */
    memcpy(base + 0x13E1EA, &rel, sizeof(rel));

    rules = install(); /* stock: leftover heap bytes become rules state */
    assert(new_calls == 1 && last_size == 0xE4 && rules[8] == 0xa5);
    free(rules);

    assert(CoF_StoryZeroRules(base));
    rules = install();
    assert(new_calls == 2 && last_size == 0xE4); /* still the DLL's own allocator */
    assert(AllZero(rules, 0xE4));
    free(rules);
    assert(!memcmp(base + 0xAF7A3, site + 10, 4)); /* add esp,4; ret untouched */

    CoF_StoryRestoreRules();
    assert(!memcmp(base + 0xAF799, site, sizeof(site)));
    rules = install();
    assert(new_calls == 3 && rules[8] == 0xa5);
    free(rules);

    base[0xAF79F] ^= 1; /* unexpected bytes: refuse without writing */
    assert(!CoF_StoryZeroRules(base));
    assert(base[0xAF79E] == 0xe8 && base[0xAF79F] == (0x46 ^ 1));

    puts("story rules allocation zeroing passed");
    return 0;
}
