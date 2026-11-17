/* Native x86 fast paths for the hot loops of GT5's reverb SPU job (socetverb, lifted image
 * jFE031AF6).  native_snd.h has the scheme, the float rules and the GT5_NATIVE_SND switch.
 *
 * Build: the game's flags for this job (the default sound-module flags, GT5 CMakeLists.txt) and
 * the hooked wiring (natives.txt):
 *   -O2 /w /GS- -mavx2 -mfma -ffp-contract=off -DSPU_X86_RZ_ALWAYS=1 -DSPU_USE_DISPATCH_MT -DNATIVE_HOOKED
 * Include directories: this job's folder, ps3recomp's runtime/spu, this file's folder.
 *
 * Each function has its fast block ns_fast_<ADDR> and ns_ranges_<ADDR>: the local-store lines the
 * LIFTED function can store to from the entry state, for GT5_NATIVE_SND=2 (native_snd.c); -1 for a
 * state the fast block does not take anyway (then the lifted function runs alone, uncompared).
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "native_snd.h"
#ifndef NATIVE_HOOKED
#  error "this file defines the lifted functions' own names: build it with -DNATIVE_HOOKED (natives.txt: hooked)"
#endif

/* shufb controls the loops take from the image (LS 0x9060..0x90A0) */
#define CTL_9060(r) ns_is(r, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu, 0x1C1D1E1Fu)   /* a1 a2 a3 b3 */
#define CTL_9070(r) ns_is(r, 0x0C0D0E0Fu, 0x10111213u, 0x14151617u, 0x18191A1Bu)   /* a3 b0 b1 b2 */
#define CTL_9080(r) ns_is(r, 0x08090A0Bu, 0x0C0D0E0Fu, 0x10111213u, 0x14151617u)   /* a2 a3 b0 b1 */
#define CTL_9090(r) ns_is(r, 0x00010203u, 0x10111213u, 0x14151617u, 0x18191A1Bu)   /* a0 b0 b1 b2 */
#define CTL_90A0(r) ns_is(r, 0x00010203u, 0x04050607u, 0x10111213u, 0x14151617u)   /* a0 a1 b0 b1 */

/* ---------------------------------------------------------------------------------------------
 * 0x3720 / 0x3568: comb filter with a one-pole low-pass in the feedback path, a line (4 samples)
 * per pass, in place on the delay line.
 *
 *     a   = buf * g                                   (fm, 4 lanes)          g = feedback gain
 *     y_i = y_(i-1) * d + a_i     i = 0..3            (fma, one after the other; d = damping)
 *     S'  = [y_-1 y0 y1 y2] * d + a                   = [y0 y1 y2 y3] when d's lanes are equal
 *     buf = S' * w + in                               (fma, 4 lanes)         w = wet/output gain
 *     S   = S'                                        (y_-1 of the next pass = lane 3 of S')
 *
 * The lifted loop builds y0..y2 in lane 3 of three four-lane fmas, shifting the state vector with
 * a register-controlled shufb in between: fma + flush + shufb on the chain, four times a line.
 * Here the chain is four scalar fmas (y3 = lane 3 of S', same operation on the same values); the
 * four-lane work hangs off it.  Lanes 0..2 of the three intermediate fmas are junk lanes.
 * (The scalar fmas take the constant as their first operand: the upper lanes of an fmadd_ss result
 * come from that operand, and with the running value there the compiler put a blend on the chain.)
 * ------------------------------------------------------------------------------------------- */
NS_INLINE void ns_comb(spu_context* ctx, int rS, int rBuf, int rIn, int rCnt, int rD, int rCtl, int rW, int rG, int stat)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t n = g[rCnt]._u32[0];
    if (n < 2 || n > 0x4000 || !CTL_9060(g[rCtl])) { NS_STAT(stat, bail_precond, 1); return; }
    const uint32_t pb = g[rBuf]._u32[0], pi = g[rIn]._u32[0];
    const __m128 d = ns_reg(g[rD]), w = ns_reg(g[rW]), gain = ns_reg(g[rG]);
    const __m128 d3 = _mm_shuffle_ps(d, d, _MM_SHUFFLE(3, 3, 3, 3));
    __m128 S = ns_reg(g[rS]);
    __m128 ym = _mm_shuffle_ps(S, S, _MM_SHUFFLE(3, 3, 3, 3));          /* y_-1 in lane 0 */
    uint32_t k = 0;
    for (; k < n - 1; k++) {
        uint8_t* const bp = ns_line(ls, pb + 16u * k);
        const __m128 X = ns_ldq(bp), I = ns_ldq(ns_line(ls, pi + 16u * k));
        const __m128 A = _mm_mul_ps(X, gain);
        const __m128 y0 = _mm_fmadd_ss(d3, ym, A);
        const __m128 y1 = _mm_fmadd_ss(d3, y0, _mm_shuffle_ps(A, A, _MM_SHUFFLE(1, 1, 1, 1)));
        const __m128 y2 = _mm_fmadd_ss(d3, y1, _mm_movehl_ps(A, A));
        const __m128 y3 = _mm_fmadd_ss(d3, y2, _mm_shuffle_ps(A, A, _MM_SHUFFLE(3, 3, 3, 3)));
        const __m128 V = _mm_movelh_ps(_mm_unpacklo_ps(ym, y0), _mm_unpacklo_ps(y1, y2));   /* y_-1 y0 y1 y2 */
        const __m128 Sn = _mm_fmadd_ps(V, d, A);
        const __m128 O = _mm_fmadd_ps(Sn, w, I);
        __m128i acc = ns_mag(A);
        NS_ACC(acc, V); NS_ACC(acc, Sn); NS_ACC(acc, O);
        if (__builtin_expect(ns_bad(acc), 0)) break;
        ns_stq(bp, ns_flush(O));
        S = Sn; ym = y3;
    }
    if (k) {
        g[rS] = ns_u128(ns_flush(S));
        g[rBuf] = ns_splat_add(g[rBuf], 16u * k);
        g[rIn] = ns_splat_add(g[rIn], 16u * k);
        g[rCnt] = ns_splat_add(g[rCnt], 0u - k);
        NS_STAT(stat, fast_calls, 1); NS_STAT(stat, fast_iters, k);
        NS_FAULT(ns_line(ls, pb + 16u * (k - 1)) + 15);
    }
    if (k != n - 1) NS_STAT(stat, bail_range, 1);
}
/* mode 2: a lifted loop that stores one line per pass at LS[dst], dst += 16, count passes */
NS_INLINE int ns_lines_rng(const spu_context* ctx, ns_range* r, int rDst, int rCnt) {
    const uint32_t n = ctx->gpr[rCnt]._u32[0];
    if (n < 2 || n > 0x4000) return -1;
    return ns_add_range(r, 0, ctx->gpr[rDst]._u32[0], 16u * n);
}
/* 0x3720: S=r7 buf=r8 in=r9 count=r10 d=r11 ctl=r12 w=r15 g=r16.  Carried: r7 r8 r9 r10. */
static void ns_fast_00003720(spu_context* ctx) { ns_comb(ctx, 7, 8, 9, 10, 11, 12, 15, 16, NS_FE_3720); }
static int ns_ranges_00003720(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 8, 10); }
/* 0x3568: S=r12 buf=r7 in=r8 count=r9 d=r10 ctl=r11 w=r15 g=r16.  Carried: r7 r8 r9 r12. */
static void ns_fast_00003568(spu_context* ctx) { ns_comb(ctx, 12, 7, 8, 9, 10, 11, 15, 16, NS_FE_3568); }
static int ns_ranges_00003568(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 7, 9); }

/* ---------------------------------------------------------------------------------------------
 * 0x4680 / 0x4840: biquad (direct form I) with an output clamp, a line per pass.
 *
 *     F   = x*b0 + x[n-1]*b1 + x[n-2]*b2                (fm, fma, fma on four lanes; x[n-1], x[n-2]
 *                                                        = the line shifted by one / two samples,
 *                                                        filled from the previous line Q)
 *     y_i = y_(i-2)*a2_i + (y_(i-1)*a1_i + F_i)          i = 0..3 (two fmas a sample; a1, a2 hold
 *                                                        the negated feedback coefficients)
 *     out = min(max(y, lo), hi)                          (fcgt + selb twice); the state keeps y
 *
 * The lifted loop does the recursion as 8 four-lane fmas with 6 register-controlled shufbs
 * between them; lane i of the last pair is y_i, the other lanes of the first three pairs are
 * junk.  Here: the same two fmas per sample as scalars.  Carried: the index, Q (the previous
 * input line, r12) and Y (r13, the last four y; only lanes 2 and 3 are read back).
 * ------------------------------------------------------------------------------------------- */
NS_INLINE void ns_biquad(spu_context* ctx, int rIdx, uint32_t in_base, uint32_t out_base, int rA1, int rCtlA, int rCtlB,
                         int rLo, int rHi, int stat)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t i0 = g[rIdx]._u32[0], span = g[23]._u32[0] - i0;
    if ((span & 15) || span < 32 || span > 0x10000 || !CTL_9070(g[21]) || !CTL_9080(g[22]) || !CTL_9090(g[rCtlA]) ||
        !CTL_90A0(g[rCtlB])) { NS_STAT(stat, bail_precond, 1); return; }
    const uint32_t n = span / 16;
    const __m128 b0 = ns_reg(g[19]), b1 = ns_reg(g[20]), b2 = ns_reg(g[24]), a1 = ns_reg(g[rA1]), a2 = ns_reg(g[7]);
    const __m128 a1_1 = _mm_shuffle_ps(a1, a1, _MM_SHUFFLE(1, 1, 1, 1)), a1_2 = _mm_movehl_ps(a1, a1),
                 a1_3 = _mm_shuffle_ps(a1, a1, _MM_SHUFFLE(3, 3, 3, 3));
    const __m128 a2_1 = _mm_shuffle_ps(a2, a2, _MM_SHUFFLE(1, 1, 1, 1)), a2_2 = _mm_movehl_ps(a2, a2),
                 a2_3 = _mm_shuffle_ps(a2, a2, _MM_SHUFFLE(3, 3, 3, 3));
    const u128 lo = g[rLo], hi = g[rHi];
    __m128 Q = ns_reg(g[12]), Y = ns_reg(g[13]);
    __m128 p2 = _mm_movehl_ps(Y, Y), p3 = _mm_shuffle_ps(Y, Y, _MM_SHUFFLE(3, 3, 3, 3));     /* y_-2, y_-1 */
    uint32_t k = 0;
    for (; k < n - 1; k++) {
        const __m128 X = ns_ldq(ns_line(ls, in_base + i0 + 16u * k));
        const __m128 X1 = _mm_castsi128_ps(_mm_alignr_epi8(_mm_castps_si128(X), _mm_castps_si128(Q), 12));  /* q3 x0 x1 x2 */
        const __m128 X2 = _mm_castsi128_ps(_mm_alignr_epi8(_mm_castps_si128(X), _mm_castps_si128(Q), 8));   /* q2 q3 x0 x1 */
        const __m128 t0 = _mm_mul_ps(X, b0);
        const __m128 t1 = _mm_fmadd_ps(X1, b1, t0);
        const __m128 F = _mm_fmadd_ps(X2, b2, t1);
        const __m128 u0 = _mm_fmadd_ss(a1, p3, F);
        const __m128 y0 = _mm_fmadd_ss(a2, p2, u0);
        const __m128 u1 = _mm_fmadd_ss(a1_1, y0, _mm_shuffle_ps(F, F, _MM_SHUFFLE(1, 1, 1, 1)));
        const __m128 y1 = _mm_fmadd_ss(a2_1, p3, u1);
        const __m128 u2 = _mm_fmadd_ss(a1_2, y1, _mm_movehl_ps(F, F));
        const __m128 y2 = _mm_fmadd_ss(a2_2, y0, u2);
        const __m128 u3 = _mm_fmadd_ss(a1_3, y2, _mm_shuffle_ps(F, F, _MM_SHUFFLE(3, 3, 3, 3)));
        const __m128 y3 = _mm_fmadd_ss(a2_3, y1, u3);
        const __m128 U = _mm_movelh_ps(_mm_unpacklo_ps(u0, u1), _mm_unpacklo_ps(u2, u3));
        const __m128 Yn = _mm_movelh_ps(_mm_unpacklo_ps(y0, y1), _mm_unpacklo_ps(y2, y3));
        __m128i acc = ns_mag(t0);
        NS_ACC(acc, t1); NS_ACC(acc, F); NS_ACC(acc, U); NS_ACC(acc, Yn);
        if (__builtin_expect(ns_bad(acc), 0)) break;
        /* the clamp, with the lifted code's own helpers */
        const u128 yv = ns_u128(ns_flush(Yn));
        const u128 c1 = spu_selb(lo, yv, spu_fcgt(yv, lo));
        const u128 c2 = spu_selb(hi, c1, spu_fcgt(hi, c1));
        ns_stq(ns_line(ls, out_base + i0 + 16u * k), ns_reg(c2));
        Q = X; Y = Yn; p2 = y2; p3 = y3;
    }
    if (k) {
        g[12] = ns_u128(Q);
        g[13] = ns_u128(ns_flush(Y));
        g[rIdx] = ns_splat_add(g[rIdx], 16u * k);
        NS_STAT(stat, fast_calls, 1); NS_STAT(stat, fast_iters, k);
        NS_FAULT(ns_line(ls, out_base + i0 + 16u * (k - 1)) + 15);
    }
    if (k != n - 1) NS_STAT(stat, bail_range, 1);
}
/* 0x4680: in = LS[r11 + r30], out = LS[r11 + r18], r11 up to r23; b0=r19 b1=r20 b2=r24 a1=r9 a2=r7;
 * controls r21 r22 r10 r15; bounds lo=r16 hi=r17.  Carried: r11 r12 r13. */
static void ns_fast_00004680(spu_context* ctx) {
    ns_biquad(ctx, 11, ctx->gpr[30]._u32[0], ctx->gpr[18]._u32[0], 9, 10, 15, 16, 17, NS_FE_4680);
}
/* 0x4840: in place, LS[r14 + r15], r14 up to r23; a1=r10 a2=r7; controls r21 r22 r11 r17; bounds
 * lo=r9 hi=r18.  Carried: r12 r13 r14. */
static void ns_fast_00004840(spu_context* ctx) {
    ns_biquad(ctx, 14, ctx->gpr[15]._u32[0], ctx->gpr[15]._u32[0], 10, 11, 17, 9, 18, NS_FE_4840);
}
/* mode 2: what the lifted biquads store: the output lines, and after the loop four words of the
 * filter's record (the last two inputs and outputs, each inserted into its line: LS[rec + 20],
 * + 24, + 28, + 32), rec = r14 for 0x4680 and r16 for 0x4840. */
NS_INLINE int ns_biquad_rng(const spu_context* ctx, ns_range* r, int rIdx, uint32_t out_base, int rRec) {
    const uint32_t i0 = ctx->gpr[rIdx]._u32[0], span = ctx->gpr[23]._u32[0] - i0;
    if ((span & 15) || span < 32 || span > 0x10000) return -1;
    const int n = ns_add_range(r, 0, out_base + i0, span);
    return ns_add_range(r, n, ctx->gpr[rRec]._u32[0] + 20u, 28u);
}
static int ns_ranges_00004680(const spu_context* ctx, ns_range* r) { return ns_biquad_rng(ctx, r, 11, ctx->gpr[18]._u32[0], 14); }
static int ns_ranges_00004840(const spu_context* ctx, ns_range* r) { return ns_biquad_rng(ctx, r, 14, ctx->gpr[15]._u32[0], 16); }

/* ---------------------------------------------------------------------------------------------
 * Plain line loops: no float is carried from pass to pass.
 * ------------------------------------------------------------------------------------------- */
/* 0x37D8: LS[r8 + r19] += LS[r29 + r8 + r10]  (fa), r8 += 16 up to r9.  Carried: r8.
 * (adds one comb's output buffer into the sum of the combs) */
static void ns_fast_000037D8(spu_context* ctx)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t i0 = g[8]._u32[0], span = g[9]._u32[0] - i0;
    if ((span & 15) || span < 32 || span > 0x10000) { NS_STAT(NS_FE_37D8, bail_precond, 1); return; }
    const uint32_t n = span / 16, d0 = i0 + g[19]._u32[0], s0 = g[29]._u32[0] + i0 + g[10]._u32[0];
    uint32_t k = 0;
    for (; k < n - 1; k++) {
        uint8_t* const dp = ns_line(ls, d0 + 16u * k);
        const __m128 t = _mm_add_ps(ns_ldq(dp), ns_ldq(ns_line(ls, s0 + 16u * k)));
        if (__builtin_expect(ns_bad(ns_mag(t)), 0)) break;
        ns_stq(dp, ns_flush(t));
    }
    if (k) {
        g[8] = ns_splat_add(g[8], 16u * k);
        NS_STAT(NS_FE_37D8, fast_calls, 1); NS_STAT(NS_FE_37D8, fast_iters, k);
        NS_FAULT(ns_line(ls, d0 + 16u * (k - 1)) + 15);
    }
    if (k != n - 1) NS_STAT(NS_FE_37D8, bail_range, 1);
}
static int ns_ranges_000037D8(const spu_context* ctx, ns_range* r) {
    const uint32_t i0 = ctx->gpr[8]._u32[0], span = ctx->gpr[9]._u32[0] - i0;
    if ((span & 15) || span < 32 || span > 0x10000) return -1;
    return ns_add_range(r, 0, i0 + ctx->gpr[19]._u32[0], span);
}

/* 0x38C8: LS[r7] = LS[r6] - LS[r5]  (fs), all three += 16, r8 passes.  Carried: r5 r6 r7 r8.
 * (all-pass: delayed sample minus the feed-forward buffer) */
static void ns_fast_000038C8(spu_context* ctx)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t n = g[8]._u32[0];
    if (n < 2 || n > 0x4000) { NS_STAT(NS_FE_38C8, bail_precond, 1); return; }
    const uint32_t a0 = g[6]._u32[0], b0 = g[5]._u32[0], d0 = g[7]._u32[0];
    uint32_t k = 0;
    for (; k < n - 1; k++) {
        const __m128 t = _mm_sub_ps(ns_ldq(ns_line(ls, a0 + 16u * k)), ns_ldq(ns_line(ls, b0 + 16u * k)));
        if (__builtin_expect(ns_bad(ns_mag(t)), 0)) break;
        ns_stq(ns_line(ls, d0 + 16u * k), ns_flush(t));
    }
    if (k) {
        g[5] = ns_splat_add(g[5], 16u * k); g[6] = ns_splat_add(g[6], 16u * k); g[7] = ns_splat_add(g[7], 16u * k);
        g[8] = ns_splat_add(g[8], 0u - k);
        NS_STAT(NS_FE_38C8, fast_calls, 1); NS_STAT(NS_FE_38C8, fast_iters, k);
        NS_FAULT(ns_line(ls, d0 + 16u * (k - 1)) + 15);
    }
    if (k != n - 1) NS_STAT(NS_FE_38C8, bail_range, 1);
}
static int ns_ranges_000038C8(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 7, 8); }

/* 0x3900: LS[r5] = LS[r5] * r12 + LS[r9]  (fma), both += 16, r10 passes.  Carried: r5 r9 r10.
 * (all-pass: the delay line takes delayed * gain + input) */
static void ns_fast_00003900(spu_context* ctx)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t n = g[10]._u32[0];
    if (n < 2 || n > 0x4000) { NS_STAT(NS_FE_3900, bail_precond, 1); return; }
    const uint32_t c0 = g[9]._u32[0], d0 = g[5]._u32[0];
    const __m128 gain = ns_reg(g[12]);
    uint32_t k = 0;
    for (; k < n - 1; k++) {
        uint8_t* const dp = ns_line(ls, d0 + 16u * k);
        const __m128 c = ns_ldq(ns_line(ls, c0 + 16u * k));
        const __m128 t = _mm_fmadd_ps(ns_ldq(dp), gain, c);
        if (__builtin_expect(ns_bad(ns_mag(t)), 0)) break;
        ns_stq(dp, ns_flush(t));
    }
    if (k) {
        g[5] = ns_splat_add(g[5], 16u * k); g[9] = ns_splat_add(g[9], 16u * k);
        g[10] = ns_splat_add(g[10], 0u - k);
        NS_STAT(NS_FE_3900, fast_calls, 1); NS_STAT(NS_FE_3900, fast_iters, k);
        NS_FAULT(ns_line(ls, d0 + 16u * (k - 1)) + 15);
    }
    if (k != n - 1) NS_STAT(NS_FE_3900, bail_range, 1);
}
static int ns_ranges_00003900(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 5, 10); }

/* 0x36F0 / 0x3538: copy r6 lines from LS[r3] to LS[r4] (a comb's delay-line chunk into the work
 * buffer, before the comb loop rewrites the delay line).  Carried: r3 r4 r6.  No arithmetic: a
 * line is loaded whole and stored whole, one after the other, so any overlap behaves the same. */
NS_INLINE void ns_copy(spu_context* ctx, int stat)
{
    u128* const g = ctx->gpr;
    uint8_t* const ls = ctx->ls;
    const uint32_t n = g[6]._u32[0];
    if (n < 2 || n > 0x4000) { NS_STAT(stat, bail_precond, 1); return; }
    const uint32_t s0 = g[3]._u32[0], d0 = g[4]._u32[0], k = n - 1;
    for (uint32_t i = 0; i < k; i++)
        _mm_storeu_si128((__m128i*)ns_line(ls, d0 + 16u * i), _mm_loadu_si128((const __m128i*)ns_line(ls, s0 + 16u * i)));
    g[3] = ns_splat_add(g[3], 16u * k); g[4] = ns_splat_add(g[4], 16u * k);
    g[6] = ns_splat_add(g[6], 0u - k);
    NS_STAT(stat, fast_calls, 1); NS_STAT(stat, fast_iters, k);
    NS_FAULT(ns_line(ls, d0 + 16u * (k - 1)) + 15);
}
static void ns_fast_000036F0(spu_context* ctx) { ns_copy(ctx, NS_FE_36F0); }
static void ns_fast_00003538(spu_context* ctx) { ns_copy(ctx, NS_FE_3538); }
static int ns_ranges_000036F0(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 4, 6); }
static int ns_ranges_00003538(const spu_context* ctx, ns_range* r) { return ns_lines_rng(ctx, r, 4, 6); }

NS_ENTRY(jFE031AF6, 00003720, NS_FE_3720)
NS_ENTRY(jFE031AF6, 00003568, NS_FE_3568)
NS_ENTRY(jFE031AF6, 00004680, NS_FE_4680)
NS_ENTRY(jFE031AF6, 00004840, NS_FE_4840)
NS_ENTRY(jFE031AF6, 000037D8, NS_FE_37D8)
NS_ENTRY(jFE031AF6, 000038C8, NS_FE_38C8)
NS_ENTRY(jFE031AF6, 00003900, NS_FE_3900)
NS_ENTRY(jFE031AF6, 000036F0, NS_FE_36F0)
NS_ENTRY(jFE031AF6, 00003538, NS_FE_3538)
