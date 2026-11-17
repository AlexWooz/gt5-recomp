/*
 * native_geo_switch.c -- GT5_NATIVE_GEO, the switch of the native SPU replacements (native_geo.h).
 * Includes no spu_recomp.h: compile it once, whatever number of images have native files.
 */
#include <stdio.h>
#include <stdlib.h>
#include "native_geo.h"

int g_native_geo = -1;
unsigned long long g_native_geo_cmp_calls, g_native_geo_cmp_mismatches;

__attribute__((noinline)) int native_geo_init(void)
{
    static volatile long said;
    const char* e = getenv("GT5_NATIVE_GEO");
    int m = 1;
    if (e && e[0] >= '0' && e[0] <= '2' && e[1] == 0) m = e[0] - '0';
    /* several SPU threads may get here together: all compute the same value, one prints */
    if (__atomic_exchange_n(&said, 1, __ATOMIC_SEQ_CST) == 0) {
        fprintf(stderr, "[native-geo] GT5_NATIVE_GEO=%d\n", m);
        fflush(stderr);
    }
    g_native_geo = m;
    return m;
}

void native_geo_compared(void)
{
    const unsigned long long n = __atomic_add_fetch(&g_native_geo_cmp_calls, 1, __ATOMIC_RELAXED);
    /* a sign of life in the game's log: how many calls were compared so far, and how many differed */
    if ((n & (n - 1)) == 0 ? n >= 1024 : (n % 1000000) == 0) {
        fprintf(stderr, "[native-geo] compared %llu calls, %llu differed\n", n,
                __atomic_load_n(&g_native_geo_cmp_mismatches, __ATOMIC_RELAXED));
        fflush(stderr);
    }
}

void native_geo_mismatch(const char* image, unsigned addr, const char* detail)
{
    const unsigned long long n = __atomic_add_fetch(&g_native_geo_cmp_mismatches, 1, __ATOMIC_RELAXED);
    if (n <= 20) {
        fprintf(stderr, "[native-geo] MISMATCH %s:%04X %s\n", image, addr, detail ? detail : "");
        fflush(stderr);
    }
}
