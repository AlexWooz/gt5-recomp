/* The switch and the compare mode for native SPU functions (spu_native.h). */
#include "spu_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int native_mode(void)
{
    const char* e = getenv("GT5_SPU_NATIVE");
    const int m = e && *e ? atoi(e) : 1;
    fprintf(stderr, "[spu-native] %s (GT5_SPU_NATIVE=%d: 1 native, 0 lifted, 2 compare)\n",
            m == 1 ? "native functions on" : m == 2 ? "comparing native against lifted" : "lifted functions only", m);
    return m;
}
/* Read before any SPU code runs: a constructor in this translation unit. */
int g_gt5_spu_native = 1;
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor)) static void native_init(void) { g_gt5_spu_native = native_mode(); }
#endif

static volatile long s_calls, s_bad;

void gt5_spu_native_compare(spu_context* ctx, void (*native)(spu_context*),
                            void (SPU_SV *lifted)(spu_context*), const char* name)
{
    /* The reference runs on a copy, the native function on the real context; the reference's
     * result is what the game keeps. The local store is part of the context unless ls points
     * elsewhere, and then it is copied too. */
    static SPU_THREAD_LOCAL spu_context* ref;
    static SPU_THREAD_LOCAL uint8_t* ref_ls;
    if (!ref) {
        ref = (spu_context*)_aligned_malloc(sizeof *ref, 64);
        ref_ls = (uint8_t*)_aligned_malloc(SPU_LS_SIZE, 64);
        if (!ref || !ref_ls) { lifted(ctx); return; }
    }
    const int inside = ctx->ls == ctx->ls_store;
    memcpy(ref, ctx, sizeof *ctx);
    if (inside) ref->ls = ref->ls_store;
    else { memcpy(ref_ls, ctx->ls, SPU_LS_SIZE); ref->ls = ref_ls; }
    lifted(ref);
    native(ctx);
    const long n = _InterlockedIncrement(&s_calls);
    int bad_reg = -1; long bad_ls = -1;
    for (int i = 0; i < 128 && bad_reg < 0; i++)
        if (memcmp(&ctx->gpr[i], &ref->gpr[i], 16)) bad_reg = i;
    if (memcmp(ctx->ls, ref->ls, SPU_LS_SIZE))
        for (long i = 0; i < SPU_LS_SIZE; i++) if (ctx->ls[i] != ref->ls[i]) { bad_ls = i; break; }
    if (bad_reg >= 0 || bad_ls >= 0 || ctx->pc != ref->pc) {
        const long b = _InterlockedIncrement(&s_bad);
        if (b <= 20) {
            fprintf(stderr, "[spu-native] MISMATCH %s call %ld:", name, n);
            if (bad_reg >= 0)
                fprintf(stderr, " r%d native %08X %08X %08X %08X lifted %08X %08X %08X %08X;", bad_reg,
                        ctx->gpr[bad_reg]._u32[0], ctx->gpr[bad_reg]._u32[1], ctx->gpr[bad_reg]._u32[2], ctx->gpr[bad_reg]._u32[3],
                        ref->gpr[bad_reg]._u32[0], ref->gpr[bad_reg]._u32[1], ref->gpr[bad_reg]._u32[2], ref->gpr[bad_reg]._u32[3]);
            if (bad_ls >= 0) fprintf(stderr, " local store 0x%05lX native %02X lifted %02X;", bad_ls, ctx->ls[bad_ls], ref->ls[bad_ls]);
            if (ctx->pc != ref->pc) fprintf(stderr, " pc native %05X lifted %05X;", ctx->pc, ref->pc);
            fputc('\n', stderr);
        }
        /* keep the reference's result */
        memcpy(ctx->gpr, ref->gpr, sizeof ctx->gpr);
        memcpy(ctx->ls, ref->ls, SPU_LS_SIZE);
        ctx->pc = ref->pc;
    }
    if ((n & 0xFFFF) == 0)
        fprintf(stderr, "[spu-native] compared %ld calls, %ld differed\n", n, (long)s_bad);
}
