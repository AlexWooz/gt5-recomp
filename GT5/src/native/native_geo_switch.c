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

#ifdef NATIVE_STATS
/* A test build (cmake -DGT5_NATIVE_STATS=ON): what the native functions did, every 5 seconds. The
 * counters are plain increments from several threads, so the numbers are approximate. */
#include <windows.h>
unsigned long long g_native_geo_why[16];
extern unsigned long long g_native8038_stat[8], g_native5BA8_stat[3], g_native7DC8_stat[5];
extern unsigned long long g_n4_66A8_stat[4], g_n4_6418_stat[6], g_n4_66A8_exact[2], g_n4_6418_exact[2];
static DWORD WINAPI native_geo_stat_thread(LPVOID p)
{
    (void)p;
    for (;;) {
        Sleep(5000);
        const unsigned long long* w = g_native_geo_why; const unsigned long long* a = g_native8038_stat;
        if (!w[0] && !g_n4_66A8_stat[0]) continue;
        fprintf(stderr, "[native-geo-stat] 8038: calls %llu (cells %llu) | left to lifted: event pending %llu (calls with interrupts enabled %llu), diag %llu, "
                "mxcsr %llu, shape %llu, small %llu | native calls %llu, fast cells %llu, lifted-statement cells %llu, deopts %llu, redone %llu\n",
                w[0], w[6], w[1], w[7], w[2], w[3], w[4], w[5], a[0], a[1], a[2], a[3], a[4]);
        fprintf(stderr, "[native-geo-stat] 5BA8: calls %llu, fast %llu, lines %llu | 7DC8: calls %llu, fast-forwards %llu, passes %llu, helper ops %llu, "
                "conversions redone %llu | 66A8: calls %llu, to lifted %llu (guard %llu, range %llu) | 6418: calls %llu, to lifted %llu "
                "(guard %llu, range %llu), left for 6CA4 %llu, normalised %llu | done exactly in native code: 66A8 %llu + %llu, 6418 %llu + %llu\n",
                g_native5BA8_stat[0], g_native5BA8_stat[1], g_native5BA8_stat[2],
                g_native7DC8_stat[0], g_native7DC8_stat[1], g_native7DC8_stat[2], g_native7DC8_stat[3], g_native7DC8_stat[4],
                g_n4_66A8_stat[0], g_n4_66A8_stat[1], g_n4_66A8_stat[2], g_n4_66A8_stat[3],
                g_n4_6418_stat[0], g_n4_6418_stat[1], g_n4_6418_stat[2], g_n4_6418_stat[3], g_n4_6418_stat[4], g_n4_6418_stat[5],
                g_n4_66A8_exact[0], g_n4_66A8_exact[1], g_n4_6418_exact[0], g_n4_6418_exact[1]);
        fflush(stderr);
    }
    return 0;
}
/* GT5_NATIVE_GEO_DUMP=<folder>: real inputs of calls a native function left to the lifted one, for the
 * test harness: <folder>\<name>_<n>.bin = the 128 registers, then the whole local store. Every 2048th
 * such call of a name, 48 files a name at most. */
void native_geo_dump(const char* name, const void* gpr, unsigned gpr_bytes, const void* ls, unsigned ls_bytes)
{
    static const char* dir; static int init;
    static const char* names[8]; static volatile long seen[8], wrote[8];
    if (!init) { dir = getenv("GT5_NATIVE_GEO_DUMP"); init = 1; }
    if (!dir || !dir[0]) return;
    int k = 0;
    while (k < 8 && names[k] && names[k] != name) k++;
    if (k == 8) return;
    names[k] = name;
    const long n = __atomic_add_fetch(&seen[k], 1, __ATOMIC_RELAXED);
    if ((n & 2047) != 1) return;
    const long w = __atomic_add_fetch(&wrote[k], 1, __ATOMIC_RELAXED);
    if (w > 48) return;
    char p[600];
    snprintf(p, sizeof p, "%s\\%s_%02ld.bin", dir, name, w);
    FILE* f = fopen(p, "wb");
    if (!f) return;
    fwrite(gpr, 1, gpr_bytes, f); fwrite(ls, 1, ls_bytes, f); fclose(f);
}
__attribute__((constructor)) static void native_geo_stat_start(void)
{
    CloseHandle(CreateThread(NULL, 0, native_geo_stat_thread, NULL, 0, NULL));
}
#endif

void native_geo_mismatch(const char* image, unsigned addr, const char* detail)
{
    const unsigned long long n = __atomic_add_fetch(&g_native_geo_cmp_mismatches, 1, __ATOMIC_RELAXED);
    if (n <= 20) {
        fprintf(stderr, "[native-geo] MISMATCH %s:%04X %s\n", image, addr, detail ? detail : "");
        fflush(stderr);
    }
}
