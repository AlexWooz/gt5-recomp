/* Shared pieces of the native fast paths for GT5's sound modules other than the mixer
 * (native_jFE031AF6.c = reverb "socetverb", native_jEB79FDFC.c = EQ "socepdideq";
 * native_snd.c = the switch and the compare mode).
 *
 * Same scheme as the mixer (native_mixer.c): a native function
 *   1. checks the preconditions of its fast block,
 *   2. advances the lifted loop by K whole passes, writing only what K passes leave behind
 *      (the loop-carried registers in ctx->gpr[] and the local-store lines stored),
 *   3. tail-calls the ORIGINAL lifted function (..._orig_sv), which does the remaining passes
 *      (always >= 1), the code after the loop and the hand-over to the next function.
 *
 * Floats.  These jobs are NOT built with SPU_X86_FAST_FLOAT: under MXCSR round-toward-zero + FTZ +
 * DAZ the lifted fa/fs/fm/fma/fms do the host operation t, then
 *      ok = |t| < FLT_MAX (ordered)        result = t with -0 made +0 ("flush")
 * and redo, exactly, every lane that is not ok (spu__x8_*_fix).  The fast blocks do the same host
 * operations and
 *   - never commit a pass in which a lane that reaches the carried state or a stored line was not
 *     ok: they stop BEFORE that pass and the lifted function carries on from exactly there;
 *   - apply the -0 -> +0 rule where a value leaves the block (a stored line, a register handed
 *     over).  In between it is not needed: a zero of the other sign as a factor or addend changes
 *     a result only when that result is itself a zero, and the rule is applied to it at the end.
 * Lanes of the lifted temporaries that never reach the state or a store ("junk lanes") are not
 * computed; the lifted last pass produces the registers they end up in.
 *
 * Build: every file that includes this header is built with -DNATIVE_HOOKED and the flags of the
 * job it belongs to (natives.txt does that); native_snd.c needs only the runtime\spu include.
 *
 * GT5_NATIVE_SND (read once, at the first call of any of these functions):
 *   1  native fast blocks (default)
 *   0  lifted code only
 *   2  run both and compare (native_snd.c); the lifted result is kept.  A test mode: slow.
 */
#pragma once
#include "spu_context.h"
#include <immintrin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NS_INLINE static inline __attribute__((always_inline))
#define NS_QMASK  (SPU_LS_MASK & ~0xFu)

/* ---- native_snd.c: the switch and the compare mode -------------------------------------- */
/* -1 = not read yet (GT5_NATIVE_SND), 0 = lifted only, 1 = native, 2 = run both and compare */
extern int g_native_snd;
int ns_mode_init(void);
NS_INLINE int ns_mode(void) { const int m = g_native_snd; return __builtin_expect(m >= 0, 1) ? m : ns_mode_init(); }

typedef SPU_SV void (*ns_sv_fn)(spu_context*);
typedef struct ns_range { uint32_t a, n; } ns_range;       /* LS bytes [a, a+n), a 16-byte aligned */
#define NS_MAX_RANGES 6
/* mode 2: compared calls, calls that differed, calls left to the lifted code without comparing */
extern unsigned long long g_native_snd_cmp_calls, g_native_snd_cmp_mismatches, g_native_snd_cmp_skipped;
/* Run fast block + lifted remainder, then the lifted function alone from the same state; compare
 * all registers, the lines of r[0..nr), pc and the successor; leave the lifted result.  Returns 0
 * when it did nothing (nr <= 0, ranges too big, a diagnostic armed): the caller then runs the
 * lifted function as usual.  Returns 1 with the lifted function done and its successor pending in
 * g_spu_trampoline_fn: the caller returns to the drain loop. */
int ns_compare(spu_context* ctx, const char* name, void (*fast)(spu_context*), ns_sv_fn orig, const ns_range* r, int nr);

/* add the lines that hold LS bytes [lsa, lsa+bytes) to r[n]; -1 (and it stays -1) when they run
 * past the end of local store */
NS_INLINE int ns_add_range(ns_range* r, int n, uint32_t lsa, uint32_t bytes) {
    const uint32_t a = lsa & NS_QMASK;
    bytes = (bytes + (lsa & 15) + 15) & ~15u;
    if (n < 0 || n >= NS_MAX_RANGES || bytes > SPU_LS_SIZE || a + bytes > SPU_LS_SIZE) return -1;
    r[n].a = a; r[n].n = bytes;
    return n + 1;
}

/* ---- per-function counters and fault injection (test harness only: -DNATIVE_STATS) ------- */
typedef struct native_snd_stats {
    unsigned long long calls;          /* entries */
    unsigned long long fast_calls;     /* entries where the fast block advanced the loop */
    unsigned long long fast_iters;     /* loop passes the fast block did */
    unsigned long long bail_precond;   /* fast block not entered (count, alignment, constants) */
    unsigned long long bail_range;     /* fast block stopped early: a lane was not "ok" */
} native_snd_stats;
enum {
    NS_FE_3720, NS_FE_3568, NS_FE_4680, NS_FE_4840, NS_FE_37D8, NS_FE_38C8, NS_FE_3900, NS_FE_36F0, NS_FE_3538,
    NS_EB_3C6C, NS_EB_3ADC, NS_EB_4158, NS_EB_4008, NS_EB_3D80, NS_EB_43C0,
    NS_FN_COUNT
};
#ifdef NATIVE_STATS
extern native_snd_stats g_native_snd_stats[NS_FN_COUNT];
extern int g_native_snd_fault;         /* 1 = fast blocks flip one bit of their last store,
                                          2 = they flip one bit of a register nothing else writes */
#  define NS_STAT(fn, field, n) (g_native_snd_stats[fn].field += (n))
#  define NS_FAULT(p) do { if (g_native_snd_fault == 1) *(p) ^= 1;                             \
                           else if (g_native_snd_fault == 2) ctx->gpr[127]._u32[3] ^= 1; } while (0)
#else
#  define NS_STAT(fn, field, n) ((void)0)
#  define NS_FAULT(p) ((void)0)
#endif

/* ---- helpers of the fast blocks --------------------------------------------------------- */
/* local store is big-endian; host lane i = SPU word i */
NS_INLINE __m128i ns_bswap(__m128i x) {
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));
}
NS_INLINE uint8_t* ns_line(uint8_t* ls, uint32_t lsa) { return ls + (lsa & NS_QMASK); }
NS_INLINE __m128 ns_ldq(const uint8_t* p) { return _mm_castsi128_ps(ns_bswap(_mm_loadu_si128((const __m128i*)p))); }
NS_INLINE void ns_stq(uint8_t* p, __m128 v) { _mm_storeu_si128((__m128i*)p, ns_bswap(_mm_castps_si128(v))); }
NS_INLINE __m128 ns_reg(u128 a) { return _mm_loadu_ps((const float*)a._u32); }
NS_INLINE u128 ns_u128(__m128 v) { u128 r; _mm_storeu_ps((float*)r._u32, v); return r; }
NS_INLINE u128 ns_splat_add(u128 r, uint32_t d) { for (int i = 0; i < 4; i++) r._u32[i] += d; return r; }

/* the lifted helpers' "-0 becomes +0" (spu__x8_flush) */
NS_INLINE __m128 ns_flush(__m128 r) {
    const __m128i sign = _mm_set1_epi32((int)0x80000000u), x = _mm_castps_si128(r);
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_and_si128(_mm_cmpeq_epi32(x, sign), sign), x));
}
/* Range bookkeeping: NS_ACC folds |t| (as an integer) into a running maximum; ns_bad says whether
 * any folded lane failed the lifted helpers' test |t| < FLT_MAX, i.e. is FLT_MAX, infinity or NaN. */
NS_INLINE __m128i ns_mag(__m128 t) { return _mm_and_si128(_mm_castps_si128(t), _mm_set1_epi32(0x7FFFFFFF)); }
#define NS_ACC(acc, t) ((acc) = _mm_max_epu32((acc), ns_mag(t)))
NS_INLINE int ns_bad(__m128i acc) {
    return _mm_movemask_ps(_mm_castsi128_ps(_mm_cmpgt_epi32(acc, _mm_set1_epi32(0x7F7FFFFE)))) != 0;
}
/* a register holding a shufb control in a known pattern */
NS_INLINE int ns_is(u128 r, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    return r._u32[0] == w0 && r._u32[1] == w1 && r._u32[2] == w2 && r._u32[3] == w3;
}
/* An armed LS diagnostic (SPU_LS_WATCH & co.) must see every access: those runs stay lifted.
 * The gates are -1 until the first lifted access settles them. */
NS_INLINE int ns_diag_armed(void) { return (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0; }

/* Doubles: SPU doubleword i = (_u32[2i] << 32) | _u32[2i+1]; as a host __m128d the two 32-bit
 * halves of each doubleword are swapped. */
NS_INLINE __m128d ns_dreg(u128 a) {
    return _mm_castsi128_pd(_mm_shuffle_epi32(_mm_loadu_si128((const __m128i*)a._u32), _MM_SHUFFLE(2, 3, 0, 1)));
}
NS_INLINE u128 ns_du128(__m128d v) {
    u128 r; _mm_storeu_si128((__m128i*)r._u32, _mm_shuffle_epi32(_mm_castpd_si128(v), _MM_SHUFFLE(2, 3, 0, 1))); return r;
}
/* both doubles finite (exponent below 0x7FF) */
NS_INLINE int ns_dfinite(__m128d v) {
    const __m128i e = _mm_set1_epi64x(0x7FF0000000000000ll);
    return _mm_movemask_pd(_mm_castsi128_pd(_mm_cmpeq_epi64(_mm_and_si128(_mm_castpd_si128(v), e), e))) == 0;
}

/* ---- entry points ------------------------------------------------------------------------
 * The game build renames the lifted body to gt5_<job>_spu_func_<ADDR>_orig_sv (natives.txt,
 * "hooked"); NS_ENTRY defines gt5_<job>_spu_func_<ADDR>_sv, the name every caller, the thunk and
 * the function table use.  Each function needs
 *     static void ns_fast_<ADDR>(spu_context*)                       the fast block
 *     static int  ns_ranges_<ADDR>(const spu_context*, ns_range*)    the lines the LIFTED function
 *                                   can store to from this entry state (mode 2), -1 = do not compare
 * The job files refuse to build without -DNATIVE_HOOKED. */
#define NS_ENTRY(JOB, A, STAT)                                                               \
    SPU_SV void gt5_##JOB##_spu_func_##A##_orig_sv(spu_context* ctx);                          \
    __attribute__((noinline)) static int ns_cmp_##A(spu_context* ctx) {                        \
        ns_range r_[NS_MAX_RANGES];                                                            \
        return ns_compare(ctx, #JOB " " #A, ns_fast_##A, gt5_##JOB##_spu_func_##A##_orig_sv,   \
                          r_, ns_ranges_##A(ctx, r_));                                         \
    }                                                                                          \
    SPU_SV void gt5_##JOB##_spu_func_##A##_sv(spu_context* ctx) {                              \
        NS_STAT(STAT, calls, 1);                                                               \
        const int m_ = ns_mode();                                                              \
        if (__builtin_expect(m_ == 1, 1)) { if (__builtin_expect(!ns_diag_armed(), 1)) ns_fast_##A(ctx); } \
        else if (m_ == 2 && ns_cmp_##A(ctx)) return;                                           \
        __attribute__((musttail)) return gt5_##JOB##_spu_func_##A##_orig_sv(ctx);              \
    }

#ifdef __cplusplus
}
#endif
