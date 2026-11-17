/*
 * native_w0163C080.c -- native x86 replacements for lifted SPU functions of GT5's job image
 * w0163C080 (EBOOT vaddr 0x0163C080, loaded at local store 0x4000):
 *
 *   0x66A8   one pass of the loop 0x6660..0x6770 (a falloff-weighted vector sum)
 *   0x6418   normalise a vector when its length is in range, then the call of 0x4190
 *   0x4AE0   append a 48-byte entry to a chunked list -- only with -DNATIVE_GEO_4AE0 (and its
 *            natives.txt line): exact, but against the lift with lazy -0 flush it is only
 *            1.2-1.3x on a P-core and 1.0-1.2x on an E-core, and hooking it keeps the compiler
 *            from inlining it into its dozen callers. Off by default.
 *
 * Both are straight-line functions, so each is replaced whole (no "fast block + lifted remainder"):
 * same signature and ABI as the lifted function; reads its live-in registers from ctx->gpr[];
 * leaves every register and local-store byte the lifted function writes exactly as it would; hands
 * control on the same way (the same SPU_DIRECT_SV / SPU_RET). Whatever the native code does not
 * cover is given to the lifted function before anything has been written to local store.
 *
 * Build: the job's flags, as the game gives lifted SPU code,
 *     -O2 /w /GS- -mavx2 -mfma -ffp-contract=off -DSPU_X86_RZ_ALWAYS=1 -DSPU_USE_DISPATCH_MT
 * plus -DNATIVE_HOOKED in the game. Include directories: the job's folder (spu_recomp.h), the
 * runtime's spu folder (spu_helpers.h, spu_context.h) and this folder (native_geo.h, native_common.h).
 *
 * NATIVE_HOOKED: the build renames the lifted definitions listed in natives_lines.txt to ..._orig_sv
 * (scripts/spu_native_patch.py, the `hooked` lines of natives.txt) and this file defines those names. Without it
 * (stand-alone harness) the functions are gt5_w0163C080_native_XXXXXXXX[_sv]. GT5_NATIVE_GEO
 * (native_geo.h) picks lifted / native / run both and compare. -DNATIVE_STATS adds counters and
 * the fault injection of the harness; the game does not define it.
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_context.h"
#include "native_geo.h"
#include "native_common.h"

#ifdef NATIVE_HOOKED
SPU_SV void gt5_w0163C080_spu_func_000066A8_orig_sv(spu_context* ctx);
SPU_SV void gt5_w0163C080_spu_func_00006418_orig_sv(spu_context* ctx);
SPU_SV void gt5_w0163C080_spu_func_00004AE0_orig_sv(spu_context* ctx);
#  define N4_ORIG_SV(A)   gt5_w0163C080_spu_func_##A##_orig_sv
#  define N4_NATIVE_SV(A) gt5_w0163C080_spu_func_##A##_sv
#else
#  define N4_ORIG_SV(A)   gt5_w0163C080_spu_func_##A##_sv
#  define N4_NATIVE_SV(A) gt5_w0163C080_native_##A##_sv
#endif

#ifdef NATIVE_STATS
/* Harness self-test: the native functions flip one bit of a line they stored. */
int g_n4_fault;
unsigned long long g_n4_faults;
#  define N4_FAULT(ls, a) do { if (g_n4_fault) { (ls)[((a) & N4_LSM) + 15u] ^= 1u; g_n4_faults++; } } while (0)
#else
#  define N4_FAULT(ls, a) ((void)0)
#endif

#ifdef NATIVE_STATS
/* The game's test build (GT5_NATIVE_GEO_DUMP): the registers and local store of a call left to the lifted function. */
void native_geo_dump(const char* name, const void* gpr, unsigned gpr_bytes, const void* ls, unsigned ls_bytes);
#  define N4_DUMP(name) native_geo_dump(name, ctx->gpr, (unsigned)sizeof ctx->gpr, ctx->ls, SPU_LS_SIZE)
#else
#  define N4_DUMP(name) ((void)0)
#endif
#ifdef NATIVE_STATS
/* [0] calls  [1] calls given to the lifted function  [2] of those: a register or a diagnostic
 * gate was not as expected  [3] of those: a float out of the host's range */
unsigned long long g_n4_66A8_stat[4];
#endif

/* ---------------------------------------------------------------------------------------------
 * 0x66A8: one pass of the loop 0x6660..0x6770, after the two vector copies.
 *
 *   d    = LS[sp+0xE0]                       (a difference vector, x y z in words 0-2)
 *   n    = 1/sqrt(d.d)  by frsqest + fi      (every lane: lane i uses words i, i+1, i+2)
 *   if !(n[0] > 0.025)  ->  0x676C           (further than 40 away: nothing to add)
 *   s    = min(n, 0.25);  u = d * s[0]       (unit vector; shorter than 4 counts as 4)
 *   w    = min((s - 0.025) * 3, 1) * f       f = the float at r80+0x340 (and the words after it)
 *   c    = max(((u . LS[r81]) + 0.1) * -2, 0)
 *   LS[sp+0xF0] += u * (c * w)[0]            -> 0x6770
 *
 * Expects r123 = 0.25, r124 = 0x00010203 (shufb control: preferred word to every lane),
 * r125 = 0.1, r127 = -2.0 in every lane, as 0x6630 sets them before the loop; anything else goes to
 * the lifted function.
 * ------------------------------------------------------------------------------------------- */
static SPU_SV void n4_66A8_sv(spu_context* ctx) {
    uint8_t* const ls = ctx->ls;
    const __m128i k025 = n4_splat(0x3E800000u), k0025 = n4_splat(0x3CCCCCCCu), k01 = n4_splat(0x3DCCCCCCu);
    const __m128i km2 = n4_splat(0xC0000000u), kctl = n4_splat(0x00010203u);
    const __m128i zero = _mm_setzero_si128();
    N4_STAT(g_n4_66A8_stat, 0);
    {
        const __m128i g = _mm_or_si128(_mm_or_si128(_mm_xor_si128(n4_g(ctx, 123), k025), _mm_xor_si128(n4_g(ctx, 124), kctl)),
                                       _mm_or_si128(_mm_xor_si128(n4_g(ctx, 125), k01), _mm_xor_si128(n4_g(ctx, 127), km2)));
        if (N4_UNLIKELY(!_mm_testz_si128(g, g) || (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0)) {
            N4_STAT(g_n4_66A8_stat, 1); N4_STAT(g_n4_66A8_stat, 2);
            __attribute__((musttail)) return N4_ORIG_SV(000066A8)(ctx);
        }
    }
    const uint32_t sp = n4_g0(ctx, 1);
    const __m128i r9 = n4_lq(ls, sp + 0xE0);
    const __m128i r11a = n4_mul(r9, r9);                    /* squares and their sums are never -0 */
    const __m128i r8 = n4_rot4(r9), r7 = n4_rot8(r9);
    const __m128i r16 = n4_fmadd(r8, r8, r11a);
    const __m128i r79 = n4_fmadd(r7, r7, r16);
    const __m128i r10 = n4_frsqest(r79);
    const __m128i r77 = n4_fi(r79, r10);                    /* sign 0: +0, a positive float, or exponent 255 */
    const __m128i r78 = _mm_cmpgt_epi32(r77, k025);
    const __m128i r76 = _mm_cmpgt_epi32(r77, k0025);
    const __m128i r2a = _mm_min_epi32(r77, k025);           /* selb(r77, 0.25, r78): +0 or [2^-126, 0.25] */
    /* r11a <= r16 <= r79 lane by lane (or NaN all the way), so r79 in range covers all three. From
     * here on the lifted function would take the same branch as this code, and no register written
     * below is one it reads: on a later range failure it starts over and overwrites them all. */
    if (N4_UNLIKELY(n4_chk_bad(_mm_and_si128(r79, _mm_set1_epi32(0x7FFFFFFF))))) goto lifted;
    n4_p(ctx, 5, k0025); n4_p(ctx, 7, r7); n4_p(ctx, 8, r8); n4_p(ctx, 9, r9); n4_p(ctx, 10, r10);
    n4_p(ctx, 16, r16); n4_p(ctx, 76, r76); n4_p(ctx, 77, r77); n4_p(ctx, 78, r78); n4_p(ctx, 79, r79);
    if (_mm_cvtsi128_si32(r76) == 0) {
        n4_p(ctx, 2, r2a); n4_p(ctx, 11, r11a);
        SPU_DIRECT_SV(ctx, 0x676C, gt5_w0163C080_spu_func_0000676C);
    }
    {
        const __m128i k3 = n4_splat(0x40400000u), k1 = n4_splat(0x3F800000u);
        const uint32_t a80 = n4_g0(ctx, 80);
        __m128i chk;
        /* s - 0.025 and (s - 0.025) * 3 with s in {+0} u [2^-126, 0.25]: always in range, a zero is
         * +0, and the product is at most 0.675, so "greater than 1" (r32) is never true */
        const __m128i r35 = n4_add(r2a, n4_splat(0xBCCCCCCCu));
        const __m128i r38 = n4_splat0(r2a);
        const __m128i r34 = n4_lq(ls, n4_g0(ctx, 81));
        const __m128i r37 = n4_lq(ls, a80 + 0x340);
        const __m128i r4 = n4_lq(ls, sp + 0xF0);
        const __m128i r11 = n4_lq(ls, n4_g0(ctx, 83) + 0x380);
        /* The chain runs on unflushed values (names ending in u); what is stored is flushed. */
        const __m128i r12u = n4_mul(r9, r38), r12 = n4_flush(r12u);
        const __m128i r31 = n4_mul(r35, k3);
        const __m128i r28 = n4_rot4(r34), r23 = n4_rot8(r34);
        const __m128i r27 = n4_rotqby(r37, a80);
        const __m128i r29u = n4_mul(r12u, r34), r29 = n4_flush(r29u);
        const __m128i r30 = n4_rot4(r12), r25 = n4_rot8(r12);
        n4_p(ctx, 4, r4); n4_p(ctx, 11, r11); n4_p(ctx, 15, zero); n4_p(ctx, 32, zero); n4_p(ctx, 33, k1); n4_p(ctx, 36, k3);
        n4_p(ctx, 34, r34); n4_p(ctx, 35, r35); n4_p(ctx, 37, r37); n4_p(ctx, 38, r38);
        n4_p(ctx, 12, r12); n4_p(ctx, 31, r31); n4_p(ctx, 26, r31); n4_p(ctx, 28, r28); n4_p(ctx, 23, r23); n4_p(ctx, 27, r27);
        n4_p(ctx, 29, r29); n4_p(ctx, 30, r30); n4_p(ctx, 25, r25);
        chk = n4_chk(_mm_and_si128(r12u, _mm_set1_epi32(0x7FFFFFFF)), r29u);
        const __m128i r24u = n4_fmadd(r28, n4_rot4(r12u), r29u), r24 = n4_flush(r24u);
        const __m128i r19u = n4_mul(r31, r27), r19 = n4_flush(r19u);                 /* r26 = r31 */
        const __m128i r22u = n4_fmadd(r23, n4_rot8(r12u), r24u), r22 = n4_flush(r22u);
        const __m128i r21 = n4_add(r22u, k01);              /* in range when r22 is; a zero is +0 */
        const __m128i r14u = n4_mul(r21, km2), r14 = n4_flush(r14u);
        const __m128i r20 = n4_i(_mm_cmplt_ps(n4_f(r14u), _mm_setzero_ps()));        /* fcgt(0, r14) */
        const __m128i r2 = n4_i(_mm_max_ps(n4_f(r14u), _mm_setzero_ps()));           /* selb(r14, 0, r20); never -0 */
        n4_p(ctx, 24, r24); n4_p(ctx, 19, r19); n4_p(ctx, 22, r22); n4_p(ctx, 21, r21); n4_p(ctx, 14, r14); n4_p(ctx, 20, r20);
        n4_p(ctx, 2, r2);
        chk = n4_chk(n4_chk(n4_chk(n4_chk(chk, r24u), r19u), r22u), r14u);
        const __m128i r13u = n4_mul(r2, r19u), r13 = n4_flush(r13u);
        const __m128i r6 = n4_splat0(r13);
        const __m128i r18 = n4_flush(n4_fmadd(r12u, n4_splat0(r13u), r4));
        n4_p(ctx, 13, r13); n4_p(ctx, 6, r6); n4_p(ctx, 18, r18);
        chk = n4_chk(n4_chk(chk, r13u), r18);
        if (N4_UNLIKELY(n4_chk_bad(chk))) goto lifted;
        n4_sq(ls, sp + 0xF0, r18);
        N4_FAULT(ls, sp + 0xF0);
        SPU_DIRECT_SV(ctx, 0x6770, gt5_w0163C080_spu_func_00006770);
    }
lifted:
    N4_STAT(g_n4_66A8_stat, 1); N4_STAT(g_n4_66A8_stat, 3);
    N4_DUMP("66A8");
    __attribute__((musttail)) return N4_ORIG_SV(000066A8)(ctx);
}

/* The one line 0x66A8 can store to. */
static N4_NOINLINE int n4_66A8_compare(spu_context* ctx) {
    const n4_range r[1] = { { (n4_g0(ctx, 1) + 0xF0u) & N4_LSM, 16 } };
    return n4_compare(ctx, "w0163C080", 0x66A8, n4_66A8_sv, N4_ORIG_SV(000066A8), r, 1);
}
SPU_SV void N4_NATIVE_SV(000066A8)(spu_context* ctx) {
    const int m = native_geo_mode();
    if (N4_LIKELY(m == 1)) { __attribute__((musttail)) return n4_66A8_sv(ctx); }
    if (m == 2 && n4_66A8_compare(ctx)) return;
    __attribute__((musttail)) return N4_ORIG_SV(000066A8)(ctx);
}
#ifndef NATIVE_HOOKED
void gt5_w0163C080_native_000066A8(spu_context* ctx) { SPU_SV_CALL gt5_w0163C080_native_000066A8_sv(ctx); }
#endif

/* ---------------------------------------------------------------------------------------------
 * 0x6418: normalise the vector at LS[sp+0xA0] when its length is in range, then call 0x4190.
 *
 *   v    = LS[sp+0xA0]                            (x y z in words 0-2)
 *   n    = 1/sqrt(v.v)  by frsqest + fi           (every lane: lane i uses words i, i+1, i+2)
 *   if !(1.0 > n[0]) or !(n[0] > r98[0])  ->  0x6CA4     (not longer than 1, or too long)
 *   LS[sp+0xA0] = v * n[0]
 *   r3 = sp + 144, r4 = r83 + 256;  call 0x4190 (copies the line at r4 to the line at r3);  -> 0x6478
 *
 * Expects r118 = 0x00010203 in every word (shufb control: preferred word to every lane), as the
 * code before the loop sets it; anything else goes to the lifted function. r98 is compared with
 * the lifted code's own helper. The call of 0x4190 and what follows it are the lifted statements.
 * ------------------------------------------------------------------------------------------- */
#ifdef NATIVE_STATS
/* [0] calls  [1] calls given to the lifted function  [2] of those: r118 or a diagnostic gate
 * [3] of those: a float out of the host's range  [4] left for 0x6CA4  [5] normalised */
unsigned long long g_n4_6418_stat[6];
#endif

static SPU_SV void n4_6418_sv(spu_context* ctx) {
    uint8_t* const ls = ctx->ls;
    const __m128i k1 = n4_splat(0x3F800000u), kctl = n4_splat(0x00010203u);
    N4_STAT(g_n4_6418_stat, 0);
    {
        const __m128i g = _mm_xor_si128(n4_g(ctx, 118), kctl);
        if (N4_UNLIKELY(!_mm_testz_si128(g, g) || (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0)) {
            N4_STAT(g_n4_6418_stat, 1); N4_STAT(g_n4_6418_stat, 2);
            __attribute__((musttail)) return N4_ORIG_SV(00006418)(ctx);
        }
    }
    const uint32_t sp = n4_g0(ctx, 1);
    const __m128i r5 = n4_lq(ls, sp + 0xA0);
    const __m128i r70 = n4_mul(r5, r5);                     /* squares and their sums are never -0 */
    const __m128i r69 = n4_rot4(r5), r67 = n4_rot8(r5);
    const __m128i r68 = n4_fmadd(r69, r69, r70);
    const __m128i r89 = n4_fmadd(r67, r67, r68);
    /* r70 <= r68 <= r89 lane by lane (or NaN all the way), so r89 in range covers all three */
    if (N4_UNLIKELY(n4_chk_bad(_mm_and_si128(r89, _mm_set1_epi32(0x7FFFFFFF))))) goto lifted;
    const __m128i r66 = n4_frsqest(r89);
    const __m128i r85 = n4_fi(r89, r66);                    /* sign 0: +0, a positive float, or exponent 255 */
    const __m128i r63 = _mm_cmpgt_epi32(k1, r85);           /* fcgt(1.0, r85) */
    const __m128i r62 = n4_from(spu_fcgt(n4_to(r85), ctx->gpr[98]));
    const __m128i ones = _mm_set1_epi32(-1);
    const __m128i r61 = _mm_xor_si128(r63, ones), r60 = _mm_xor_si128(r62, ones);
    const __m128i r59 = _mm_or_si128(r61, r60);
    if (_mm_cvtsi128_si32(r59) != 0) {
        n4_p(ctx, 5, r5); n4_p(ctx, 59, r59); n4_p(ctx, 60, r60); n4_p(ctx, 61, r61); n4_p(ctx, 62, r62); n4_p(ctx, 63, r63);
        n4_p(ctx, 64, k1); n4_p(ctx, 66, r66); n4_p(ctx, 67, r67); n4_p(ctx, 68, r68); n4_p(ctx, 69, r69); n4_p(ctx, 70, r70);
        n4_p(ctx, 85, r85); n4_p(ctx, 89, r89);
        N4_STAT(g_n4_6418_stat, 4);
        SPU_DIRECT_SV(ctx, 0x6CA4, gt5_w0163C080_spu_func_00006CA4);
    }
    {
        const __m128i r9 = n4_splat0(r85);
        const __m128i r10u = n4_mul(r5, r9);
        if (N4_UNLIKELY(n4_chk_bad(_mm_and_si128(r10u, _mm_set1_epi32(0x7FFFFFFF))))) goto lifted;
        const __m128i r10 = n4_flush(r10u);
        n4_sq(ls, sp + 0xA0, r10);
        N4_FAULT(ls, sp + 0xA0);
        ctx->gpr[0] = spu_link(0x6478);
        ctx->gpr[3] = spu_ai(ctx->gpr[1], 144); ctx->gpr[4] = spu_ai(ctx->gpr[83], 256);
        n4_p(ctx, 5, r5); n4_p(ctx, 9, r9); n4_p(ctx, 10, r10);
        n4_p(ctx, 59, r59); n4_p(ctx, 60, r60); n4_p(ctx, 61, r61); n4_p(ctx, 62, r62); n4_p(ctx, 63, r63);
        n4_p(ctx, 64, k1); n4_p(ctx, 66, r66); n4_p(ctx, 67, r67); n4_p(ctx, 68, r68); n4_p(ctx, 69, r69); n4_p(ctx, 70, r70);
        n4_p(ctx, 85, r85); n4_p(ctx, 89, r89);
        N4_STAT(g_n4_6418_stat, 5);
        /* the call of 0x4190 and the hand-over, as the lifted statements have them */
        { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; gt5_w0163C080_spu_func_00004190_sv(ctx); spu_drain_call(ctx, 0x6478); ctx->host_depth--; spu_img_restore(ctx, _si); }
        { SPU_DIRECT_SV(ctx, 0x6478, gt5_w0163C080_spu_func_00006478); }
    }
lifted:
    N4_STAT(g_n4_6418_stat, 1); N4_STAT(g_n4_6418_stat, 3);
    N4_DUMP("6418");
    __attribute__((musttail)) return N4_ORIG_SV(00006418)(ctx);
}

/* The lines 0x6418 (with the 0x4190 it calls) can store to: the vector, and the line at sp+144. */
static N4_NOINLINE int n4_6418_compare(spu_context* ctx) {
    const uint32_t sp = n4_g0(ctx, 1);
    const n4_range r[2] = { { (sp + 0xA0u) & N4_LSM, 16 }, { (sp + 144u) & N4_LSM, 16 } };
    return n4_compare(ctx, "w0163C080", 0x6418, n4_6418_sv, N4_ORIG_SV(00006418), r, 2);
}
SPU_SV void N4_NATIVE_SV(00006418)(spu_context* ctx) {
    const int m = native_geo_mode();
    if (N4_LIKELY(m == 1)) { __attribute__((musttail)) return n4_6418_sv(ctx); }
    if (m == 2 && n4_6418_compare(ctx)) return;
    __attribute__((musttail)) return N4_ORIG_SV(00006418)(ctx);
}
#ifndef NATIVE_HOOKED
void gt5_w0163C080_native_00006418(spu_context* ctx) { SPU_SV_CALL gt5_w0163C080_native_00006418_sv(ctx); }
#endif

#ifdef NATIVE_GEO_4AE0
/* ---------------------------------------------------------------------------------------------
 * 0x4AE0: append one 48-byte entry to a chunked list. r3 = the list header L, r4..r8 = the data.
 *
 *   if !(word[L+8] > word[L])  return            (total count has reached the capacity)
 *   chunk = word[L + 24 + 4 * word[L+0x20]]      (pointer table after the header)
 *   e     = chunk + 48 * word[L+4]               (word[L+4] = entries in the current chunk)
 *   LS[e]      = r4 words 0-2, r5 word 0         (shufb controls at LS 0x7C50 / 60 / 70)
 *   LS[e+0x10] = r6 words 0-1, r7 words 0-1
 *   LS[e+0x20] = r7 word 2, r8 words 0-2
 *   word[L]++, word[L+4]++
 *   if word[L+12] > word[L+4]  return            (room left in the chunk)
 *   else -> 0x4C00                               (the chunk is full: a DMA put follows)
 *
 * n4_push does the loads and stores in the lifted order and keeps what it loaded; n4_push_regs
 * rebuilds from that the 42 registers the lifted function leaves (most are steps on the way).
 * The three controls are compared with the image's bytes on every call; if they differ the
 * shuffles are done the general way with what local store holds.
 * ------------------------------------------------------------------------------------------- */
#ifdef NATIVE_STATS
/* [0] calls  [1] calls given to the lifted function  [2] list full  [3] stored
 * [4] stored, chunk exhausted  [5] shufb controls not the image's */
unsigned long long g_n4_4AE0_stat[6];
#endif

static const __declspec(align(16)) uint8_t n4_k7C50[48] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13,   /* 0x7C50 */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,   /* 0x7C60 */
    0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,   /* 0x7C70 */
};
N4_INLINE int n4_k7C50_ok(const uint8_t* ls) {
    const __m128i d = _mm_or_si128(_mm_or_si128(
        _mm_xor_si128(_mm_loadu_si128((const __m128i*)(ls + 0x7C50)), _mm_load_si128((const __m128i*)n4_k7C50)),
        _mm_xor_si128(_mm_loadu_si128((const __m128i*)(ls + 0x7C60)), _mm_load_si128((const __m128i*)(n4_k7C50 + 16)))),
        _mm_xor_si128(_mm_loadu_si128((const __m128i*)(ls + 0x7C70)), _mm_load_si128((const __m128i*)(n4_k7C50 + 32))));
    return _mm_testz_si128(d, d);
}
/* the three shuffles with the image's controls */
N4_INLINE __m128i n4_sh7C50(__m128i a, __m128i b) { return n4_i(_mm_insert_ps(n4_f(a), n4_f(b), 0x30)); }                     /* a0 a1 a2 b0 */
N4_INLINE __m128i n4_sh7C60(__m128i a, __m128i b) { return _mm_unpacklo_epi64(a, b); }                                        /* a0 a1 b0 b1 */
N4_INLINE __m128i n4_sh7C70(__m128i a, __m128i b) { return n4_i(_mm_insert_ps(n4_f(_mm_slli_si128(b, 4)), n4_f(a), 0x80)); }  /* a2 b0 b1 b2 */

N4_INLINE uint32_t n4_w0(__m128i v) { return (uint32_t)_mm_cvtsi128_si32(v); }
/* insw(b, a, pos): b with word (pos >> 2) & 3 replaced by the preferred word of a */
N4_INLINE __m128i n4_insw(__m128i b, __m128i a, uint32_t pos) {
    const __m128i m = _mm_cmpeq_epi32(_mm_setr_epi32(0, 1, 2, 3), _mm_set1_epi32((int)((pos >> 2) & 3)));
    return _mm_blendv_epi8(b, _mm_shuffle_epi32(a, 0), m);
}
N4_INLINE __m128i n4_clgt(__m128i a, __m128i b) {
    const __m128i s = _mm_set1_epi32((int)0x80000000u);
    return _mm_cmpgt_epi32(_mm_xor_si128(a, s), _mm_xor_si128(b, s));
}

typedef struct N4Push { __m128i r3, r14, r15, r45, r41, r33, r26, r20, r7, r32, r28, r29, r43, r42, r40; } N4Push;

/* 0: list full, nothing stored (t->r3, r14, r15 set); 1: stored; 2: stored and the chunk is full */
N4_INLINE int n4_push(uint8_t* ls, __m128i r3, __m128i r4, __m128i r5, __m128i r6, __m128i r7, __m128i r8, N4Push* t) {
    const uint32_t L = n4_w0(r3);
    t->r3 = r3;
    t->r14 = n4_lq(ls, L);
    t->r15 = n4_lq(ls, L + 8);
    if (!(n4_w0(n4_rotqby(t->r15, L + 8)) > n4_w0(n4_rotqby(t->r14, L)))) return 0;
    t->r45 = n4_lq(ls, L + 0x20);
    t->r41 = n4_lq(ls, L + 4);
    if (N4_LIKELY(n4_k7C50_ok(ls))) {
        t->r43 = _mm_setr_epi32(0x00010203, 0x04050607, 0x08090A0B, 0x10111213);
        t->r42 = _mm_setr_epi32(0x00010203, 0x04050607, 0x10111213, 0x14151617);
        t->r40 = _mm_setr_epi32(0x08090A0B, 0x10111213, 0x14151617, 0x18191A1B);
        t->r32 = n4_sh7C50(r4, r5); t->r28 = n4_sh7C60(r6, r7); t->r29 = n4_sh7C70(r7, r8);
    } else {
        N4_STAT(g_n4_4AE0_stat, 5);
        t->r43 = n4_lq(ls, 0x7C50); t->r42 = n4_lq(ls, 0x7C60); t->r40 = n4_lq(ls, 0x7C70);
        t->r32 = n4_from(spu_shufb(n4_to(r4), n4_to(r5), n4_to(t->r43)));
        t->r28 = n4_from(spu_shufb(n4_to(r6), n4_to(r7), n4_to(t->r42)));
        t->r29 = n4_from(spu_shufb(n4_to(r7), n4_to(r8), n4_to(t->r40)));
    }
    const uint32_t a35 = (n4_w0(n4_rotqby(t->r45, L)) << 2) + L;
    t->r33 = n4_lq(ls, a35 + 24);
    const uint32_t n = n4_w0(n4_rotqby(t->r41, L + 4));
    const uint32_t e = n4_w0(n4_rotqby(t->r33, a35 + 24)) + ((n + n + n) << 4);
    n4_sq(ls, e, t->r32); n4_sq(ls, e + 0x20, t->r29); n4_sq(ls, e + 0x10, t->r28);
    t->r26 = n4_lq(ls, L);
    n4_sq(ls, L, n4_insw(t->r26, _mm_add_epi32(n4_rotqby(t->r26, L), _mm_set1_epi32(1)), L));
    t->r20 = n4_lq(ls, L + 4);
    const __m128i r8n = _mm_add_epi32(n4_rotqby(t->r20, L + 4), _mm_set1_epi32(1));
    n4_sq(ls, L + 4, n4_insw(t->r20, r8n, L + 4));
    t->r7 = n4_lq(ls, L + 12);
    return n4_w0(n4_rotqby(t->r7, L + 12)) > n4_w0(r8n) ? 1 : 2;
}
/* The registers 0x4AE0 leaves, for status 0 (8 of them) or 1 / 2 (42). */
N4_INLINE void n4_push_regs(spu_context* ctx, const N4Push* t, int status) {
    const uint32_t L = n4_w0(t->r3);
    const __m128i r11 = t->r3, one = _mm_set1_epi32(1);
    const __m128i r9 = n4_rotqby(t->r14, L), r2 = n4_rotqby(t->r15, L + 8);
    n4_p(ctx, 2, r2); n4_p(ctx, 3, n4_clgt(r2, r9)); n4_p(ctx, 9, r9); n4_p(ctx, 11, r11); n4_p(ctx, 12, _mm_set1_epi32(8));
    n4_p(ctx, 14, t->r14);
    if (status == 0) { n4_p(ctx, 10, _mm_add_epi32(r11, _mm_set1_epi32(8))); n4_p(ctx, 15, t->r15); return; }
    const __m128i r44 = n4_rotqby(t->r45, L), r38 = n4_rotqby(t->r41, L + 4);
    const __m128i r39 = _mm_slli_epi32(r44, 2), r37 = _mm_add_epi32(r38, r38);
    const __m128i r35 = _mm_add_epi32(r39, r11), r36 = _mm_add_epi32(r37, r38);
    const __m128i r34 = _mm_add_epi32(r35, _mm_set1_epi32(24)), r31 = _mm_slli_epi32(r36, 4);
    const __m128i r30 = n4_rotqby(t->r33, n4_w0(r34));
    const __m128i r25 = n4_rotqby(t->r26, L), r24 = _mm_add_epi32(r25, one);
    const __m128i r8 = _mm_add_epi32(n4_rotqby(t->r20, L + 4), one);
    const __m128i r6 = n4_rotqby(t->r7, L + 12);
    n4_p(ctx, 4, n4_clgt(r6, r8)); n4_p(ctx, 5, _mm_add_epi32(r11, _mm_set1_epi32(12))); n4_p(ctx, 6, r6); n4_p(ctx, 7, t->r7);
    n4_p(ctx, 8, r8); n4_p(ctx, 10, _mm_set1_epi32(4)); n4_p(ctx, 13, _mm_set1_epi32(12));
    ctx->gpr[15] = spu_cwd(n4_to(r11), 0); ctx->gpr[16] = spu_cwd(n4_to(r11), 4);
    n4_p(ctx, 17, _mm_set1_epi32(24)); n4_p(ctx, 18, n4_insw(t->r20, r8, L + 4)); n4_p(ctx, 20, t->r20);
    n4_p(ctx, 21, _mm_add_epi32(r11, _mm_set1_epi32(4))); n4_p(ctx, 22, n4_insw(t->r26, r24, L));
    n4_p(ctx, 24, r24); n4_p(ctx, 25, r25); n4_p(ctx, 26, t->r26); n4_p(ctx, 27, _mm_add_epi32(r30, r31));
    n4_p(ctx, 28, t->r28); n4_p(ctx, 29, t->r29); n4_p(ctx, 30, r30); n4_p(ctx, 31, r31); n4_p(ctx, 32, t->r32);
    n4_p(ctx, 33, t->r33); n4_p(ctx, 34, r34); n4_p(ctx, 35, r35); n4_p(ctx, 36, r36); n4_p(ctx, 37, r37); n4_p(ctx, 38, r38);
    n4_p(ctx, 39, r39); n4_p(ctx, 40, t->r40); n4_p(ctx, 41, t->r41); n4_p(ctx, 42, t->r42); n4_p(ctx, 43, t->r43);
    n4_p(ctx, 44, r44); n4_p(ctx, 45, t->r45);
}

static SPU_SV void n4_4AE0_sv(spu_context* ctx) {
    uint8_t* const ls = ctx->ls;
    N4Push t;
    N4_STAT(g_n4_4AE0_stat, 0);
    if (N4_UNLIKELY((g_spu_ls_rdiag | g_spu_ls_wdiag) != 0)) {
        N4_STAT(g_n4_4AE0_stat, 1);
        __attribute__((musttail)) return N4_ORIG_SV(00004AE0)(ctx);
    }
    const int st = n4_push(ls, n4_g(ctx, 3), n4_g(ctx, 4), n4_g(ctx, 5), n4_g(ctx, 6), n4_g(ctx, 7), n4_g(ctx, 8), &t);
    n4_push_regs(ctx, &t, st);
    N4_STAT(g_n4_4AE0_stat, 2 + st);
#ifdef NATIVE_STATS
    if (st != 0) N4_FAULT(ls, n4_g0(ctx, 27));                      /* r27 = the entry's address */
#endif
    if (N4_LIKELY(st != 2)) SPU_RET(ctx);
    {   /* the chunk is full: what 0x4C00 reads, as the lifted statements have it */
        const uint8_t* const _ls = ls;
        const u128 r8 = ctx->gpr[8], r11 = ctx->gpr[11];
        const u128 r78 = spu_a(r8, r8);
        const u128 r76 = n4_to(n4_lq(_ls, r11._u32[0] + 0x20));
        const u128 r77 = spu_a(r78, r8);
        const u128 r75 = n4_to(n4_lq(_ls, r11._u32[0] + 0x30));
        const u128 r57 = spu_shli(r77, 4);
        const u128 r74 = spu_rotqby(r76, r11);
        const u128 r67 = spu_rotqby(r75, r11);
        const u128 r73 = spu_shli(r74, 2);
        const u128 r70 = spu_a(r73, r11);
        const u128 r72 = spu_ai(r70, 24);
        const u128 r71 = n4_to(n4_lq(_ls, r70._u32[0] + 24));
        const u128 r69 = n4_to(n4_lq(_ls, r70._u32[0] + 0x10));
        const u128 r68 = spu_rotqby(r71, r72);
        const u128 r65 = spu_rotqby(r69, r70);
        ctx->gpr[2] = spu_link(0x4C00);
        ctx->gpr[57] = r57; ctx->gpr[65] = r65; ctx->gpr[67] = r67; ctx->gpr[68] = r68; ctx->gpr[69] = r69; ctx->gpr[70] = r70;
        ctx->gpr[71] = r71; ctx->gpr[72] = r72; ctx->gpr[73] = r73; ctx->gpr[74] = r74; ctx->gpr[75] = r75; ctx->gpr[76] = r76;
        ctx->gpr[77] = r77; ctx->gpr[78] = r78;
        SPU_DIRECT_SV(ctx, 0x4C00, gt5_w0163C080_spu_func_00004C00);
    }
}

/* The lines 0x4AE0 can store to, from the entry state: the three lines of the new entry and the
 * header's counters (word[L] and word[L+4]). The addresses are worked out as n4_push does, from
 * what local store holds before anything is stored. */
static N4_NOINLINE int n4_4AE0_compare(spu_context* ctx) {
    const uint8_t* const ls = ctx->ls;
    const uint32_t L = n4_g0(ctx, 3);
    const uint32_t a35 = (n4_w0(n4_rotqby(n4_lq(ls, L + 0x20), L)) << 2) + L;
    const uint32_t n = n4_w0(n4_rotqby(n4_lq(ls, L + 4), L + 4));
    const uint32_t e = n4_w0(n4_rotqby(n4_lq(ls, a35 + 24), a35 + 24)) + ((n + n + n) << 4);
    const n4_range r[5] = { { e & N4_LSM, 16 }, { (e + 0x10u) & N4_LSM, 16 }, { (e + 0x20u) & N4_LSM, 16 },
                            { L & N4_LSM, 16 }, { (L + 4u) & N4_LSM, 16 } };
    return n4_compare(ctx, "w0163C080", 0x4AE0, n4_4AE0_sv, N4_ORIG_SV(00004AE0), r, 5);
}
SPU_SV void N4_NATIVE_SV(00004AE0)(spu_context* ctx) {
    const int m = native_geo_mode();
    if (N4_LIKELY(m == 1)) { __attribute__((musttail)) return n4_4AE0_sv(ctx); }
    if (m == 2 && n4_4AE0_compare(ctx)) return;
    __attribute__((musttail)) return N4_ORIG_SV(00004AE0)(ctx);
}
#ifndef NATIVE_HOOKED
void gt5_w0163C080_native_00004AE0(spu_context* ctx) { SPU_SV_CALL gt5_w0163C080_native_00004AE0_sv(ctx); }
#endif
#endif /* NATIVE_GEO_4AE0 */
