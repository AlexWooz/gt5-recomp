/*
 * native_common.h -- shared by the native_<image>.c files: register and local-store access as
 * __m128i, the float pieces, and the rules they rest on.
 *
 * Included after the job's spu_recomp.h, spu_helpers.h, spu_context.h and native_geo.h. Everything is
 * static.
 *
 * FLOAT RULES (lifted code built with SPU_X86_RZ_ALWAYS, MXCSR = round toward zero + FTZ + DAZ)
 *   spu_fa/fs/fm/fma/fnms(x..) = t = the host operation; if every lane of t has |t| < FLT_MAX
 *   (ordered compare: Inf and NaN fail, and so does +-FLT_MAX itself) the result is flush(t) =
 *   t with -0 replaced by +0; otherwise the failing lanes are redone exactly, out of line.
 *   So a native function may use the bare host operation for an operation wherever it then
 *     (a) proves, or checks, that every lane of t is below FLT_MAX in magnitude, and
 *     (b) applies flush, or proves t cannot be -0.
 *   n4_chk() accumulates the largest |t| bit pattern seen (as integers: NaN and Inf patterns
 *   are above 0x7F7FFFFF); n4_chk_bad() is the single test at the end. A function that fails it
 *   has stored nothing to local store yet and redoes the operations exactly (the n4x_ pieces
 *   below): real data has vectors whose last lane is not a float at all, so "give the call to
 *   the lifted function" would be most calls.
 *   flush(t) is t + (+0): under round toward zero (-0) + (+0) = +0 and x + 0 = x for every other
 *   x, NaN payloads included. It must stay a separate add: fused into a*b + 0 it would return -0
 *   for a product that underflows from below. clang does not fuse across intrinsics (contraction
 *   "on" is per source expression); the pragma below says so again, the disassembly was checked,
 *   and the harness feeds underflowing products.
 *   Only values that are STORED (registers, local store) need the flush. Two values that differ
 *   only in the sign of a zero ("~") give ~ results in every add, multiply and fused
 *   multiply-add, have the same magnitude for the range check, compare the same in the host's
 *   float compares, and flush to the same word. So a chain is computed on the unflushed values
 *   and each stored value is flushed on the side, off the chain.
 *   spu_fcgt(a, b): the host compare when neither side holds a NaN pattern, else the integer
 *   "key" compare. Against a positive normal constant C and a value v that is never negative
 *   (or is a flushed in-range float) both are the signed integer compare of the bit patterns.
 */
#ifndef NATIVE_COMMON_H
#define NATIVE_COMMON_H
#include <immintrin.h>
#include <stdio.h>
#include <string.h>

#if !SPU_X86 || !defined(__AVX2__) || !defined(__FMA__) || !defined(SPU_X86_RZ_ALWAYS) || defined(SPU_X86_FAST_FLOAT)
#error "native code must be built like the lifted SPU code: -mavx2 -mfma -DSPU_X86_RZ_ALWAYS=1, no SPU_X86_FAST_FLOAT"
#endif
#pragma clang fp contract(off)

#define N4_LSM (SPU_LS_MASK & ~0xFu)
#define N4_LIKELY(x)   __builtin_expect(!!(x), 1)
#define N4_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define N4_INLINE static inline __attribute__((always_inline))
#define N4_NOINLINE __attribute__((noinline))

#ifdef NATIVE_STATS
#define N4_STAT(arr, i) ((arr)[i]++)
#else
#define N4_STAT(arr, i) ((void)0)
#endif

N4_INLINE __m128i n4_bswap(__m128i x) {
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));
}
/* register n of the context */
N4_INLINE __m128i n4_g(const spu_context* ctx, int n) { return _mm_loadu_si128((const __m128i*)&ctx->gpr[n]); }
N4_INLINE void n4_p(spu_context* ctx, int n, __m128i v) { _mm_storeu_si128((__m128i*)&ctx->gpr[n], v); }
N4_INLINE uint32_t n4_g0(const spu_context* ctx, int n) { return ctx->gpr[n]._u32[0]; }   /* preferred word */
/* quadword load / store, as lqd/lqx/stqd do: the address masked to the 16-byte line */
N4_INLINE __m128i n4_lq(const uint8_t* ls, uint32_t a) { return n4_bswap(_mm_loadu_si128((const __m128i*)(ls + (a & N4_LSM)))); }
N4_INLINE void n4_sq(uint8_t* ls, uint32_t a, __m128i v) { _mm_storeu_si128((__m128i*)(ls + (a & N4_LSM)), n4_bswap(v)); }
N4_INLINE __m128i n4_from(u128 v) { return _mm_loadu_si128((const __m128i*)&v); }
N4_INLINE u128 n4_to(__m128i v) { u128 r; _mm_storeu_si128((__m128i*)&r, v); return r; }
N4_INLINE __m128i n4_splat(uint32_t w) { return _mm_set1_epi32((int)w); }

/* rotqbyi 4 / 8 / 12: word i of the result is word i+1 / i+2 / i+3 of the source */
#define n4_rot4(x)  _mm_shuffle_epi32((x), _MM_SHUFFLE(0, 3, 2, 1))
#define n4_rot8(x)  _mm_shuffle_epi32((x), _MM_SHUFFLE(1, 0, 3, 2))
#define n4_rot12(x) _mm_shuffle_epi32((x), _MM_SHUFFLE(2, 1, 0, 3))
/* shufb(x, x, 0x00010203 in every word): the preferred word in every lane */
#define n4_splat0(x) _mm_shuffle_epi32((x), 0)

N4_INLINE __m128 n4_f(__m128i x) { return _mm_castsi128_ps(x); }
N4_INLINE __m128i n4_i(__m128 x) { return _mm_castps_si128(x); }
N4_INLINE __m128i n4_mul(__m128i a, __m128i b) { return n4_i(_mm_mul_ps(n4_f(a), n4_f(b))); }
N4_INLINE __m128i n4_add(__m128i a, __m128i b) { return n4_i(_mm_add_ps(n4_f(a), n4_f(b))); }
N4_INLINE __m128i n4_sub(__m128i a, __m128i b) { return n4_i(_mm_sub_ps(n4_f(a), n4_f(b))); }
N4_INLINE __m128i n4_fmadd(__m128i a, __m128i b, __m128i c) { return n4_i(_mm_fmadd_ps(n4_f(a), n4_f(b), n4_f(c))); }
#ifdef N4_MUT_NOFLUSH   /* mutation test of the harness: must produce mismatches */
N4_INLINE __m128i n4_flush(__m128i t) { return t; }
#elif defined(N4_FLUSH_INT)
N4_INLINE __m128i n4_flush(__m128i t) { return _mm_andnot_si128(_mm_cmpeq_epi32(t, _mm_set1_epi32((int)0x80000000u)), t); }
#else
N4_INLINE __m128i n4_flush(__m128i t) { return n4_i(_mm_add_ps(n4_f(t), _mm_setzero_ps())); }
#endif
/* the range check: acc = max(acc, |t|) as integers */
N4_INLINE __m128i n4_chk(__m128i acc, __m128i t) { return _mm_max_epu32(acc, _mm_and_si128(t, _mm_set1_epi32(0x7FFFFFFF))); }
N4_INLINE int n4_chk_bad(__m128i acc) {
#ifdef N4_MUT_NOCHK     /* mutation test of the harness: must produce mismatches */
    return 0;
#endif
    return _mm_movemask_epi8(_mm_cmpgt_epi32(acc, _mm_set1_epi32(0x7F7FFFFE))) != 0;
}
/* any lane with exponent 255 (what the host reads as Inf or NaN)? */
N4_INLINE __m128i n4_exp255(__m128i x) {
    const __m128i e = _mm_set1_epi32(0x7F800000);
    return _mm_cmpeq_epi32(_mm_and_si128(x, e), e);
}

/* frsqest, every lane, exact (integer work on the bit fields, as spu_frsqest): the table lookups
 * as four scalar loads in place of the helper's eight permutes and seven blends. The table's
 * entries are below 2^23, so the helper's mask is not needed. *fr gets the fractions alone. */
N4_INLINE __m128i n4_frsqest_fr(__m128i x, __m128i* fr) {
    const __m128i idx = _mm_and_si128(_mm_srli_epi32(x, 18), _mm_set1_epi32(0x3F));
    const __m128i exp = _mm_and_si128(_mm_srli_epi32(x, 23), _mm_set1_epi32(0xFF));
    const uint64_t lo = (uint64_t)_mm_cvtsi128_si64(idx), hi = (uint64_t)_mm_extract_epi64(idx, 1);
    const __m128i f01 = _mm_insert_epi32(_mm_cvtsi32_si128((int)spu_frsqest_fraction_lut[(uint32_t)lo]), (int)spu_frsqest_fraction_lut[lo >> 32], 1);
    const __m128i f23 = _mm_insert_epi32(_mm_cvtsi32_si128((int)spu_frsqest_fraction_lut[(uint32_t)hi]), (int)spu_frsqest_fraction_lut[hi >> 32], 1);
    const __m128i frac = _mm_unpacklo_epi64(f01, f23);
    /* exponent 190 - (exp + 1) / 2, or 255 for exp 0 (190 | 255 = 255) */
    __m128i rexp = _mm_sub_epi32(_mm_set1_epi32(190), _mm_srli_epi32(_mm_add_epi32(exp, _mm_set1_epi32(1)), 1));
    rexp = _mm_or_si128(rexp, _mm_and_si128(_mm_cmpeq_epi32(exp, _mm_setzero_si128()), _mm_set1_epi32(0xFF)));
    *fr = frac;
    return _mm_or_si128(_mm_slli_epi32(rexp, 23), frac);
}
N4_INLINE __m128i n4_frsqest(__m128i x) { __m128i fr; return n4_frsqest_fr(x, &fr); }
/* fi(a, est), every lane, exact: spu_fi's formula. The one multiply, (est & 0x3FF) * (a & 0x7FFFF)
 * (below 2^29), is done as two 32x32->64 multiplies of the even and the odd lanes: half the
 * latency of a 32-bit lane multiply on the path of the preferred word. */
N4_INLINE __m128i n4_fi(__m128i a, __m128i est) {
    const __m128i y = _mm_and_si128(a, _mm_set1_epi32(0x7FFFF)), s = _mm_and_si128(est, _mm_set1_epi32(0x3FF));
    const __m128i sy = _mm_or_si128(_mm_mul_epu32(s, y), _mm_slli_epi64(_mm_mul_epu32(_mm_srli_epi64(s, 32), _mm_srli_epi64(y, 32)), 32));
    const __m128i exp = _mm_and_si128(_mm_srli_epi32(est, 23), _mm_set1_epi32(0xFF));
    const __m128i b19 = _mm_slli_epi32(_mm_and_si128(_mm_srli_epi32(est, 10), _mm_set1_epi32(0x1FFF)), 19);
    const __m128i big = _mm_cmpeq_epi32(_mm_max_epu32(b19, sy), b19);           /* magnitude >= 1.0 */
    const __m128i d = _mm_sub_epi32(b19, sy);
    const __m128i frac = _mm_and_si128(_mm_blendv_epi8(_mm_srli_epi32(d, 8), _mm_srli_epi32(d, 9), big), _mm_set1_epi32(0x7FFFFF));
    const __m128i e2 = _mm_sub_epi32(exp, _mm_andnot_si128(big, _mm_set1_epi32(1)));   /* normalised: exp - 1 */
    const __m128i r = _mm_or_si128(_mm_or_si128(_mm_and_si128(est, _mm_set1_epi32((int)0x80000000u)), _mm_slli_epi32(e2, 23)), frac);
    const __m128i zero = _mm_or_si128(_mm_cmpeq_epi32(exp, _mm_setzero_si128()), _mm_cmpeq_epi32(e2, _mm_setzero_si128()));
    return _mm_andnot_si128(zero, r);
}
/* rotqby(x, sh): SPU byte j of the result is SPU byte (j + sh) & 15 of x; host byte h holds SPU
 * byte h ^ 3. One shuffle with the control from a table (the helper builds the control). */
#define N4_RB(h, s) (char)(((((h) ^ 3) + (s)) & 15) ^ 3)
#define N4_ROTROW(s) { N4_RB(0, s), N4_RB(1, s), N4_RB(2, s), N4_RB(3, s), N4_RB(4, s), N4_RB(5, s), N4_RB(6, s), N4_RB(7, s), \
                       N4_RB(8, s), N4_RB(9, s), N4_RB(10, s), N4_RB(11, s), N4_RB(12, s), N4_RB(13, s), N4_RB(14, s), N4_RB(15, s) }
static const __declspec(align(16)) char n4_rot_ctl[16][16] = {
    N4_ROTROW(0), N4_ROTROW(1), N4_ROTROW(2), N4_ROTROW(3), N4_ROTROW(4), N4_ROTROW(5), N4_ROTROW(6), N4_ROTROW(7),
    N4_ROTROW(8), N4_ROTROW(9), N4_ROTROW(10), N4_ROTROW(11), N4_ROTROW(12), N4_ROTROW(13), N4_ROTROW(14), N4_ROTROW(15),
};
N4_INLINE __m128i n4_rotqby(__m128i x, uint32_t sh) {
    return _mm_shuffle_epi8(x, _mm_load_si128((const __m128i*)n4_rot_ctl[sh & 15]));
}

/* ---- lanes outside the host float range, exactly ----------------------------------------------
 * Two tools, both exact for every bit pattern (they give what spu_fm / spu_fma / spu_fa give):
 *
 * n4x_fm / n4x_fma / n4x_fa: the runtime's own helpers on __m128i. One host operation and, for
 *   lanes that left the host range, the helper's exact out-of-line routine. For operations that
 *   are redone one at a time.
 *
 * n4x_sumsq: the chain  a = v*v;  b = v'*v' + a;  c = v"*v" + b  (v' = v rotated one word, v" two
 *   words: the sum of squares both functions here start with) for all four lanes in double
 *   precision, with one widening for the three operations. Under MXCSR round toward zero:
 *     - an SPU single widens exactly to a double: biased exponent 0 -> +0.0, anything else the
 *       double with the same sign, power of two and fraction, so exponent 255 is one more binade
 *       (n4x_wide; cvtps2pd would read it as Inf/NaN);
 *     - a square of a 24-bit significand is exact in a double, and the squares of v' and v" are
 *       the squares of v, rotated;
 *     - a sum of a square and an SPU single is truncated once at 53 bits by the double add, and
 *       truncating that at 24 bits gives the exact sum truncated at 24 bits (the 24-bit numbers
 *       are 53-bit numbers, and truncation toward zero is monotone);
 *     - n4x_round_pos makes a double >= 0 the SPU result: the 29 low significand bits cleared
 *       (truncation at 24 bits), clamped to Smax, +0 below Smin = 2^-126. That value, still a
 *       double, is the next step's addend; n4x_narrow_pos gives its bits as a single.
 *   Everything is >= 0, so there is no sign and no -0 to handle. No value is a denormal double or
 *   overflows one (squares lie in [2^-252, 2^258)), so FTZ/DAZ never act. */
N4_INLINE __m128i n4x_fm(__m128i a, __m128i b) { return n4_from(spu_fm(n4_to(a), n4_to(b))); }
N4_INLINE __m128i n4x_fa(__m128i a, __m128i b) { return n4_from(spu_fa(n4_to(a), n4_to(b))); }
N4_INLINE __m128i n4x_fma(__m128i a, __m128i b, __m128i c) { return n4_from(spu_fma(n4_to(a), n4_to(b), n4_to(c))); }

N4_INLINE __m256d n4x_wide(__m128i x) {
    /* sign-extended and moved up 29 bits: sign at bit 63 (bits 60-62 are copies of it, masked off),
     * the 8 exponent bits at 52, the fraction at 29; then the exponent rebiased (1023 - 127) */
    const __m256i u = _mm256_and_si256(_mm256_slli_epi64(_mm256_cvtepi32_epi64(x), 29), _mm256_set1_epi64x((long long)0x8FFFFFFFE0000000ull));
    const __m256i nz = _mm256_cmpgt_epi64(_mm256_add_epi64(u, u), _mm256_set1_epi64x(0x001FFFFFFFFFFFFFll));   /* biased exponent != 0 */
    return _mm256_castsi256_pd(_mm256_and_si256(_mm256_add_epi64(u, _mm256_set1_epi64x(896ll << 52)), nz));
}
/* d >= 0 as the SPU would leave it, still a double; *keep = the lanes that are not +0 */
N4_INLINE __m256d n4x_round_pos(__m256d d, __m256i* keep) {
    const __m256d t = _mm256_and_pd(d, _mm256_castsi256_pd(_mm256_set1_epi64x(~0x1FFFFFFFll)));
#ifdef N4X_MUT_NOSMIN   /* mutation test of the harness: must produce mismatches */
    *keep = _mm256_castpd_si256(_mm256_cmp_pd(t, _mm256_setzero_pd(), _CMP_GT_OQ));
#else
    *keep = _mm256_castpd_si256(_mm256_cmp_pd(t, _mm256_castsi256_pd(_mm256_set1_epi64x(0x3810000000000000ll)), _CMP_GE_OQ));   /* 2^-126 */
#endif
    return _mm256_and_pd(_mm256_min_pd(t, _mm256_castsi256_pd(_mm256_set1_epi64x(0x47FFFFFFE0000000ll))),      /* Smax */
                         _mm256_castsi256_pd(*keep));
}
/* the bits of such a value as a single: moved down 29 bits and rebiased; 0 where it is +0 */
N4_INLINE __m128i n4x_narrow_pos(__m256d r, __m256i keep) {
    const __m256i w = _mm256_and_si256(_mm256_sub_epi64(_mm256_srli_epi64(_mm256_castpd_si256(r), 29), _mm256_set1_epi64x(896ll << 23)), keep);
    return _mm256_castsi256_si128(_mm256_permutevar8x32_epi32(w, _mm256_setr_epi32(0, 2, 4, 6, 0, 2, 4, 6)));
}
/* a = fm(v, v), b = fma(rot4(v), rot4(v), a), c = fma(rot8(v), rot8(v), b), every lane exact.
 * Out of line, so that the functions using it hold no 256-bit state themselves (inlined, every
 * exit of theirs got a vzeroupper, also on the path with all floats in range); the argument and
 * the three results travel in registers (__vectorcall: a struct of three vectors comes back in
 * xmm0-xmm2). */
typedef struct n4x_sumsq_t { __m128i a, b, c; } n4x_sumsq_t;
static N4_NOINLINE n4x_sumsq_t __vectorcall n4x_sumsq(__m128i v) {
    const __m256d w = n4x_wide(v);
    const __m256d p = _mm256_mul_pd(w, w);                                   /* exact */
    __m256i ka, kb, kc;
    const __m256d da = n4x_round_pos(p, &ka);
#ifdef N4X_MUT_NOROUND  /* mutation test of the harness: must produce mismatches */
    const __m256d db = n4x_round_pos(_mm256_add_pd(_mm256_permute4x64_pd(p, _MM_SHUFFLE(0, 3, 2, 1)), p), &kb);
#else
    const __m256d db = n4x_round_pos(_mm256_add_pd(_mm256_permute4x64_pd(p, _MM_SHUFFLE(0, 3, 2, 1)), da), &kb);
#endif
    const __m256d dc = n4x_round_pos(_mm256_add_pd(_mm256_permute4x64_pd(p, _MM_SHUFFLE(1, 0, 3, 2)), db), &kc);
    n4x_sumsq_t r;
    r.a = n4x_narrow_pos(da, ka); r.b = n4x_narrow_pos(db, kb); r.c = n4x_narrow_pos(dc, kc);
    return r;
}

/* ---- GT5_NATIVE_GEO=2: run both and compare ---------------------------------------------------
 * For a function whose possible stores are a few known lines: keep the registers and those lines;
 * run the native function and keep its result; put the entry state back; run the lifted function;
 * compare all registers, those lines, pc and the successor; log a difference
 * ("[native-geo] MISMATCH <image>:<addr> ..."); the LIFTED result stays. Both runs are made to
 * return in place of tail-calling their successor by clearing ctx->steps for their duration
 * (SPU_DIRECT_SV then leaves the successor pending in g_spu_trampoline_fn and returns; SPU_RET
 * returns anyway): the drain loop this call sits under carries on from there.
 * Returns 0 when it did nothing (the caller then runs the lifted function as usual). */
typedef struct n4_range { uint32_t a, n; } n4_range;                /* LS bytes [a, a + n), inside local store */
#define N4_CMP_CAP 256u
static N4_NOINLINE int n4_compare(spu_context* ctx, const char* image, unsigned addr, SPU_SV void (*native)(spu_context*),
                                  SPU_SV void (*orig)(spu_context*), const n4_range* r, int nr) {
    uint8_t ls0[N4_CMP_CAP], ls1[N4_CMP_CAP];
    u128 g0[128], g1[128];
    uint32_t bytes = 0;
    for (int i = 0; i < nr; i++) { if (r[i].a > SPU_LS_SIZE || r[i].n > SPU_LS_SIZE - r[i].a) return 0; bytes += r[i].n; }
    if (bytes > N4_CMP_CAP || (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0) return 0;
    const uint32_t steps = ctx->steps, pc0 = ctx->pc;
    for (uint32_t i = 0, o = 0; i < (uint32_t)nr; o += r[i].n, i++) memcpy(ls0 + o, ctx->ls + r[i].a, r[i].n);
    memcpy(g0, ctx->gpr, sizeof g0);
    ctx->steps = 0;
    g_spu_trampoline_fn = 0;
    native(ctx);
    void (* const succ1)(spu_context*) = g_spu_trampoline_fn;
    const uint32_t pc1 = ctx->pc;
    memcpy(g1, ctx->gpr, sizeof g1);
    for (uint32_t i = 0, o = 0; i < (uint32_t)nr; o += r[i].n, i++) memcpy(ls1 + o, ctx->ls + r[i].a, r[i].n);
    memcpy(ctx->gpr, g0, sizeof g0);                                 /* back to the entry state */
    for (uint32_t i = 0, o = 0; i < (uint32_t)nr; o += r[i].n, i++) memcpy(ctx->ls + r[i].a, ls0 + o, r[i].n);
    ctx->pc = pc0;
    g_spu_trampoline_fn = 0;
    orig(ctx);                                                       /* the result that stays */
    ctx->steps = steps;
    for (uint32_t i = 0, o = 0; i < (uint32_t)nr; o += r[i].n, i++) memcpy(ls0 + o, ctx->ls + r[i].a, r[i].n);
    native_geo_compared();
    if (memcmp(g1, ctx->gpr, sizeof g1) != 0 || memcmp(ls0, ls1, bytes) != 0 || pc1 != ctx->pc || succ1 != g_spu_trampoline_fn) {
        int reg = -1; uint32_t off = 0xFFFFFFFFu;
        char d[160];
        for (int i = 0; i < 128 && reg < 0; i++) if (memcmp(&g1[i], &ctx->gpr[i], 16) != 0) reg = i;
        for (uint32_t i = 0, o = 0; i < (uint32_t)nr && off == 0xFFFFFFFFu; o += r[i].n, i++)
            for (uint32_t b = 0; b < r[i].n; b++) if (ls0[o + b] != ls1[o + b]) { off = r[i].a + b; break; }
        snprintf(d, sizeof d, "first register r%d, first LS byte 0x%05X, pc %05X/%05X (native/lifted)", reg, off, pc1, ctx->pc);
        native_geo_mismatch(image, addr, d);
    }
    return 1;
}
#endif
