/* The switch and the run-both-and-compare mode of the native sound fast paths (native_snd.h):
 * reverb native_jFE031AF6.c, EQ native_jEB79FDFC.c.
 *
 * This file belongs to no job: it includes no spu_recomp.h and needs only the runtime\spu include
 * directory (build it like spu_native.c; nothing in it depends on the float flags of a job).
 */
#include "native_snd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -1 = not read yet (GT5_NATIVE_SND), 0 = lifted only, 1 = native, 2 = run both and compare */
int g_native_snd = -1;
unsigned long long g_native_snd_cmp_calls, g_native_snd_cmp_mismatches, g_native_snd_cmp_skipped;

#ifdef NATIVE_STATS
native_snd_stats g_native_snd_stats[NS_FN_COUNT];
int g_native_snd_fault;
#endif

/* First call of any native sound function, on whichever thread gets there first.  Two threads
 * arriving together both read the same environment and store the same value; the line is printed
 * once. */
__attribute__((noinline)) int ns_mode_init(void)
{
    static int said;
    const char* e = getenv("GT5_NATIVE_SND");
    int m = e && *e ? atoi(e) : 1;
    if (m < 0 || m > 2) m = 1;
    if (!__atomic_exchange_n(&said, 1, __ATOMIC_ACQ_REL)) {
        fprintf(stderr, "[native-snd] GT5_NATIVE_SND=%d (%s)\n", m,
                m == 1 ? "native reverb and EQ loops" : m == 2 ? "run both and compare, keep the lifted result" : "lifted only");
        fflush(stderr);
    }
    g_native_snd = m;
    return m;
}

/* ---------------------------------------------------------------------------------------------
 * Mode 2.  Snapshot the registers and the local-store lines the lifted function can store to; run
 * fast block + lifted remainder; keep that result; put the snapshot back; run the lifted function
 * alone; compare all registers, those lines, pc and the successor; the LIFTED result stays.  Both
 * runs are made to return instead of tail-calling their successor by clearing ctx->steps for their
 * duration (SPU_DIRECT_SV then takes the trampoline: it stores the successor in
 * g_spu_trampoline_fn and returns, and the drain loop this call sits under carries on from there).
 * Every hot loop runs twice: expect stutter.
 * ------------------------------------------------------------------------------------------- */
#define NS_CMP_CAP 0x10000u                 /* bytes of local store one call may have to snapshot */
typedef struct ns_cmp_buf {
    u128 g0[128], g1[128];
    uint8_t ls0[NS_CMP_CAP], ls1[NS_CMP_CAP];
} ns_cmp_buf;
static SPU_THREAD_LOCAL ns_cmp_buf* t_cmp;  /* per thread, allocated at its first compared call, never freed */

static uint32_t ns_copy_ranges(uint8_t* dst, const uint8_t* ls, const ns_range* r, int n)
{
    uint32_t o = 0;
    for (int i = 0; i < n; i++) {
        if (r[i].n > NS_CMP_CAP - o) return 0;
        memcpy(dst + o, ls + r[i].a, r[i].n);
        o += r[i].n;
    }
    return o;
}

__attribute__((noinline)) int ns_compare(spu_context* ctx, const char* name, void (*fast)(spu_context*), ns_sv_fn orig,
                                         const ns_range* r, int nr)
{
    ns_cmp_buf* b = t_cmp;
    uint32_t bytes;
    if (nr > 0 && !b) b = t_cmp = (ns_cmp_buf*)malloc(sizeof *b);
    if (nr <= 0 || !b || ns_diag_armed() || !(bytes = ns_copy_ranges(b->ls0, ctx->ls, r, nr))) {
        __atomic_add_fetch(&g_native_snd_cmp_skipped, 1, __ATOMIC_RELAXED);
        return 0;
    }
    const uint32_t steps = ctx->steps, pc0 = ctx->pc;
    memcpy(b->g0, ctx->gpr, sizeof b->g0);
    ctx->steps = 0;
    g_spu_trampoline_fn = 0;
    fast(ctx); orig(ctx);                                           /* native: fast block + lifted remainder */
    void (* const succ1)(spu_context*) = g_spu_trampoline_fn;
    const uint32_t pc1 = ctx->pc;
    memcpy(b->g1, ctx->gpr, sizeof b->g1);
    ns_copy_ranges(b->ls1, ctx->ls, r, nr);
    memcpy(ctx->gpr, b->g0, sizeof b->g0);                          /* back to the entry state */
    for (uint32_t i = 0, o = 0; i < (uint32_t)nr; o += r[i].n, i++) memcpy(ctx->ls + r[i].a, b->ls0 + o, r[i].n);
    ctx->pc = pc0;
    g_spu_trampoline_fn = 0;
    orig(ctx);                                                      /* lifted alone: the result that stays */
    ctx->steps = steps;
    ns_copy_ranges(b->ls0, ctx->ls, r, nr);
    const unsigned long long calls = __atomic_add_fetch(&g_native_snd_cmp_calls, 1, __ATOMIC_RELAXED);
    if (memcmp(b->g1, ctx->gpr, sizeof b->g1) || memcmp(b->ls0, b->ls1, bytes) || pc1 != ctx->pc ||
        succ1 != g_spu_trampoline_fn) {
        if (__atomic_add_fetch(&g_native_snd_cmp_mismatches, 1, __ATOMIC_RELAXED) <= 20) {
            int reg = -1; uint32_t off = 0xFFFFFFFFu;
            char where[96] = "", *w = where;
            for (int i = 0; i < 128 && reg < 0; i++) if (memcmp(&b->g1[i], &ctx->gpr[i], 16)) reg = i;
            for (uint32_t i = 0, o = 0; i < (uint32_t)nr && off == 0xFFFFFFFFu; o += r[i].n, i++)
                for (uint32_t k = 0; k < r[i].n; k++) if (b->ls0[o + k] != b->ls1[o + k]) { off = r[i].a + k; break; }
            if (reg >= 0) w += sprintf(w, " first reg r%d,", reg);
            if (off != 0xFFFFFFFFu) w += sprintf(w, " first LS byte 0x%05X,", off);
            if (succ1 != g_spu_trampoline_fn) w += sprintf(w, " successor differs,");
            fprintf(stderr, "[native-snd] MISMATCH fn=%s call=%llu%s pc native %05X lifted %05X (lifted result kept)\n",
                    name, calls, where, pc1, ctx->pc);
            fflush(stderr);
        }
    }
    if (calls == 10000 || (calls % 1000000ull) == 0)
        fprintf(stderr, "[native-snd] compared %llu calls, %llu differed, %llu not compared\n", calls,
                g_native_snd_cmp_mismatches, g_native_snd_cmp_skipped);
    return 1;
}
