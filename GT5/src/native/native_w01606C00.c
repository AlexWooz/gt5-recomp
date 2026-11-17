/*
 * native_w01606C00.c -- native x86 replacements for lifted SPU functions of GT5's job image
 * w01606C00 (EBOOT vaddr 0x01606C00, loaded at local store 0x4000).
 *
 *   0x8038   the grid pass (the hottest SPU function of the workers): notes below.
 *   0x5BA8   the fill loop of the image's memset (what the profile shows as 0x5BA4): after 0x8038.
 *   0x7DC8   the pack pass (16-byte cells to 8-byte cells, with a running sum): at the end.
 *
 * Build: the job's flags, as the game gives lifted SPU code,
 *     -O2 /w /GS- -mavx2 -mfma -ffp-contract=off -DSPU_X86_RZ_ALWAYS=1 -DSPU_USE_DISPATCH_MT
 * plus -DNATIVE_HOOKED in the game. Include directories: the job's folder (spu_recomp.h), the
 * runtime's spu folder (spu_helpers.h, spu_context.h) and this folder (native_geo.h, blk_*.inc).
 *
 * NATIVE_HOOKED: the build renames the lifted definitions gt5_w01606C00_spu_func_00008038_sv,
 * ..._00005BA8_sv and ..._00007DC8_sv to ..._orig_sv (scripts/spu_native_patch.py, the `hooked`
 * lines of natives.txt) and this file defines those three names, so every
 * caller in the lifted file, and the registered thunk, reach the native code. Without it (the
 * stand-alone harness) the function is gt5_w01606C00_native_00008038[_sv] and the lifted one keeps
 * its name. GT5_NATIVE_GEO (native_geo.h) picks lifted / native / run both and compare.
 * -DNATIVE_STATS adds counters and test switches for the harness; the game does not define it.
 *
 * ---- 0x8038 ----------------------------------------------------------------------------------
 * Same contract as the lifted function: reads its live-in registers from ctx->gpr[], leaves every
 * register and every local-store byte exactly as the lifted function would, sets ctx->pc and the
 * trampoline the same way on return.
 *
 * Unlike the simple loops (advance K passes, let the lifted function finish), this function's
 * registers on return are whatever the LAST run of each of its 16 per-cell basic blocks left, and
 * which cell that is depends on the data. So:
 *   - the start of the function, the per-row header and the function's end run as the lifted
 *     statements themselves (blk_*.inc, cut out of spu_recomp.c by gen_blocks.py), on a private copy
 *     of the registers ("RF");
 *   - each grid cell runs on a fast path that computes only what reaches local store (fast_cell);
 *   - anything the fast path does not cover runs that one cell as the lifted statements (blk_cell);
 *   - every cell records the local-store lines it loaded. At the end the cells that were the last
 *     to run some block are run again from their records, in order, on RF: that gives the registers;
 *   - if something cannot be handled at all, RF is brought up to date the same way and the lifted
 *     code is entered at the label where the native code stands (0x8238 or 0x8298: "deopt").
 * A call the native code does not take at all (interrupts enabled, a local-store diagnostic armed,
 * another float mode, no rows or columns, a tiny grid) goes to the lifted function untouched.
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_context.h"
#include "native_geo.h"
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !SPU_X86 || !defined(__AVX2__) || !defined(__FMA__) || !defined(SPU_X86_RZ_ALWAYS) || defined(SPU_X86_FAST_FLOAT)
#error "native_w01606C00.c must be built like the lifted SPU code: -mavx2 -mfma -DSPU_X86_RZ_ALWAYS=1, no SPU_X86_FAST_FLOAT"
#endif

#ifdef NATIVE_HOOKED
SPU_SV void gt5_w01606C00_spu_func_00008038_orig_sv(spu_context* ctx);
#  define NX8_ORIG_SV   gt5_w01606C00_spu_func_00008038_orig_sv
#  define NX8_NATIVE_SV gt5_w01606C00_spu_func_00008038_sv
#else
#  define NX8_ORIG_SV   gt5_w01606C00_spu_func_00008038_sv
#  define NX8_NATIVE_SV gt5_w01606C00_native_00008038_sv
#endif

/* Grids with fewer cells than this go to the lifted function: the replay at the end costs about as
 * much as a dozen lifted cells. Measured against the lift with lazy -0 flush (25% of the cells
 * occupied, P-core / E-core): 16 cells 0.75x / 0.8x, 24 cells 1.0x / 1.2x, 32 cells 1.15x / 1.3x,
 * 48 cells 1.3x / 1.5x, 64 cells 1.4x / 1.55x, 96 cells 1.6x / 1.8x, 2048 cells 4.0x / 4.3x.
 * A plain global so that a test can lower it; nothing else writes it. */
unsigned g_native8038_min_cells = 48;

#ifdef NATIVE_STATS
/* Test switches (harness): 0 = normal; 1 = every cell as lifted statements on RF; 2 = the same,
 * but on a scratch copy, so the registers on return come from the replay alone. */
int g_native8038_mode = 0;
/* Fault injection (harness self-test): the last local-store line a fast cell stored in the call
 * gets one bit flipped before the function returns. */
int g_native8038_fault = 0;
unsigned long long g_native8038_faults;
/* Counters: [0] calls taken natively, [1] cells on the fast path, [2] cells as lifted statements,
 * [3] deopts, [4] fast cells undone and redone as lifted statements, [5] cells that used a batch
 * result, [6] batches, [7] cells in batches. g_native8038_blocks[n-1]: fast cells that ran basic
 * block n. Not thread safe; nothing depends on them. */
unsigned long long g_native8038_stat[8];
unsigned long long g_native8038_blocks[16];
#  define NX8_STAT(i, n) (g_native8038_stat[i] += (n))
#  define NX8_COUNT_BLOCKS(bbm) do { for (unsigned b_ = 0; b_ < 16; b_++) g_native8038_blocks[b_] += (bbm) >> b_ & 1u; } while (0)
#  define NX8_MODE() g_native8038_mode
#else
#  define NX8_STAT(i, n) ((void)0)
#  define NX8_COUNT_BLOCKS(bbm) ((void)0)
#  define NX8_MODE() 0
#endif

#define LSM (SPU_LS_MASK & ~0xFu)
#define NX8_LIKELY(x)   __builtin_expect(!!(x), 1)
#define NX8_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define NX8_NOINLINE __attribute__((noinline))

typedef struct { u128 gpr[128]; } RF;

#define MAXE 4                       /* entries per stencil cell: a 64-byte cell holds four */
#define NLD  (5 + MAXE * 15)         /* most local-store lines one grid cell can load */
#define NPOOL 18                     /* 16 basic blocks can each pin one record, +1 in use, +1 spare */

typedef struct CellLog {
    uint32_t seq;                    /* running cell number: the order records are replayed in */
    uint32_t row;                    /* grid row of the cell */
    uint32_t k;                      /* grid column: r55 is this in every word */
    uint32_t fast;                   /* made by the fast path: the three constants are not in kc[] */
    uint32_t own;                    /* basic blocks this record is still the last to have run */
    __m128i r53, r51, r54;           /* the other registers a cell reads that change from cell to cell */
    __m128i kc[3];                   /* LS 0x8E60 / 0x8E70 / 0x8E80 as loaded (register order) */
    __m128i ld[NLD];                 /* every other line the cell loaded, in order, as stored in LS */
} CellLog;

#define MODE_LIVE   0                /* load from LS and record, store to LS */
#define MODE_REPLAY 1                /* load from the record, store nothing */

static inline __attribute__((always_inline)) __m128i nx8_bswap(__m128i x) {
    return _mm_shuffle_epi8(x, _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12));
}
static inline __m128i nx8_ld(u128 a) { return _mm_loadu_si128((const __m128i*)&a); }
static inline u128 nx8_st(__m128i v) { u128 r; _mm_storeu_si128((__m128i*)&r, v); return r; }
static inline u128 nx8_lq(const uint8_t* ls, uint32_t a) {
    return nx8_st(nx8_bswap(_mm_loadu_si128((const __m128i*)(ls + (a & LSM)))));
}
static inline void nx8_sq(uint8_t* ls, uint32_t a, u128 v) {
    _mm_storeu_si128((__m128i*)(ls + (a & LSM)), nx8_bswap(nx8_ld(v)));
}
static inline u128 nx8_lq_log(const uint8_t* ls, __m128i* log, uint32_t* li, int mode, uint32_t a) {
    __m128i raw;
    if (mode == MODE_LIVE) { raw = _mm_loadu_si128((const __m128i*)(ls + (a & LSM))); _mm_storeu_si128(log + *li, raw); }
    else raw = _mm_loadu_si128(log + *li);
    (*li)++;
    return nx8_st(nx8_bswap(raw));
}

/* The three constants of the image the fast path builds on (checked against LS before use). */
static const uint8_t k_const_8E60[48] = {
    0x80, 0x80, 0x80, 0x04, 0x80, 0x80, 0x80, 0x05, 0x80, 0x80, 0x80, 0x04, 0x80, 0x80, 0x80, 0x05,   /* 0x8E60 */
    0x02, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02,   /* 0x8E70 */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x13, 0x17, 0x80, 0x80,   /* 0x8E80 */
};
static inline u128 nx8_kconst(int i) { return nx8_st(nx8_bswap(_mm_loadu_si128((const __m128i*)(k_const_8E60 + 16 * i)))); }

/* Sticky MXCSR flags the fast path reads: invalid (bit 0) and overflow (bit 3). They have to be
 * clear when a fast cell starts, so whatever else may raise them (the lifted statements, on lanes
 * the fast path never computes) clears them afterwards. The lifted function never clears a flag:
 * what was cleared is remembered and set again when the function returns. */
#define NX8_FLAGS 0x9u
static SPU_THREAD_LOCAL unsigned t_nx8_flags;
static NX8_NOINLINE void nx8_clear_flags(void) {
    const unsigned csr = _mm_getcsr();
    if (csr & NX8_FLAGS) { t_nx8_flags |= csr & 0x3Fu; _mm_setcsr(csr & ~0x3Fu); }
}
static inline void nx8_restore_flags(void) {
    if (t_nx8_flags) { _mm_setcsr(_mm_getcsr() | t_nx8_flags); t_nx8_flags = 0; }
}

#define NX8_REGS(X) \
    X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) \
    X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) \
    X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56) \
    X(57) X(58) X(59) X(60) X(61) X(62) X(63) X(64) X(65) X(66) X(67) X(68) X(69) X(70) X(71) X(72) X(73) X(74) \
    X(75) X(76) X(77) X(78) X(79) X(80) X(81) X(82) X(83) X(84) X(85) X(86) X(87) X(126)
#define NX8_DECL(n)  u128 r##n;
#define NX8_LOAD(n)  r##n = ctx->gpr[n];
/* The lift carries, beside each register, its "twin" _uN (the value before the -0 flush, for the
 * lazy-flush float helpers). Where the lifted code enters at a label it starts every twin as the
 * register itself; so do the blocks. */
#define NX8_TWIN(n)  u128 _u##n = r##n;
#define NX8_STORE(n) ctx->gpr[n] = r##n;

/* ---- the lifted statements, block by block --------------------------------------------------- */

/* 0x8038 up to the first pass of 0x8238. */
static NX8_NOINLINE void blk_prologue(RF* ctx, uint8_t* ls) {
    NX8_REGS(NX8_DECL)
    uint32_t _ia0, _ia1, _ia2, _ia3, _ia4, _ia5, _ia6, _ia7, _ia8, _ia9, _ia10;
    (void)_ia9; (void)_ia10;
    NX8_REGS(NX8_LOAD)
    NX8_REGS(NX8_TWIN)
    u128 _ud; (void)_ud;
#define LQ(a)    nx8_lq(ls, (a))
#define SQ(a, v) nx8_sq(ls, (a), (v))
#include "blk_prologue.inc"
#undef LQ
#undef SQ
blk_unreachable:
    NX8_REGS(NX8_STORE)
}

/* 0x8238 up to 0x8298: the header of one grid row. hl[2]: the two lines it loads. */
static NX8_NOINLINE void blk_header(RF* ctx, const uint8_t* ls, __m128i* hl, int mode) {
    NX8_REGS(NX8_DECL)
    uint32_t li = 0;
    NX8_REGS(NX8_LOAD)
    NX8_REGS(NX8_TWIN)
    u128 _ud; (void)_ud;
#define LQ(a) nx8_lq_log(ls, hl, &li, mode, (a))
#include "blk_header.inc"
#undef LQ
blk_unreachable:
    NX8_REGS(NX8_STORE)
}

/* 0x8298 up to 0x891C: one grid cell. Returns the basic blocks it ran (bit n-1 = block n), or 0 when
 * the cell cannot be recorded (a stencil cell that claims more than MAXE entries): then local store
 * and *ctx are as they were before the cell. */
static NX8_NOINLINE uint32_t blk_cell(RF* ctx, uint8_t* ls, CellLog* lg, int mode) {
    NX8_REGS(NX8_DECL)
    uint32_t _ia10;
    uint32_t li = 0, bb = 0;
    (void)_ia10;
    NX8_REGS(NX8_LOAD)
    NX8_REGS(NX8_TWIN)
    u128 _ud; (void)_ud;
#define LQ(a) nx8_lq_log(ls, lg->ld, &li, mode, (a))
#define LQC(i, a) (mode == MODE_LIVE ? nx8_st(lg->kc[i] = nx8_ld(nx8_lq(ls, (a)))) \
                                     : (lg->fast ? nx8_kconst(i) : nx8_st(lg->kc[i])))
#define SQ(a, v) do { if (mode == MODE_LIVE) nx8_sq(ls, (a), (v)); } while (0)
#define BBM(n) do { bb |= 1u << ((n) - 1); \
        if ((n) == 6 && mode == MODE_LIVE && (int16_t)r37._u16[0] > MAXE) { \
            /* too many entries to record: take back the generator state this cell stored */ \
            _mm_storeu_si128((__m128i*)(ls + (r34._u32[0] & LSM)), lg->ld[0]); \
            return 0; \
        } } while (0)
#include "blk_cell.inc"
#undef LQ
#undef LQC
#undef SQ
#undef BBM
cell_end:
    NX8_REGS(NX8_STORE)
    return bb;
}

/* 0x891C after the last cell, 0x8934 after the last row, 0x8948: the function's end. */
static NX8_NOINLINE void blk_exit(RF* ctx, const uint8_t* ls) {
    NX8_REGS(NX8_DECL)
    NX8_REGS(NX8_LOAD)
#define LQ(a) nx8_lq(ls, (a))
    /* spu_recomp.c lines 17826-17828 */
    r55 = spu_ai(r55, 1);
    r53 = spu_fa(r53, r62);
    r37 = spu_ceq(r55, r63);
    /* lines 17834-17836 */
    r80 = spu_ai(r80, 1);
    r5 = spu_fa(r53, r81);
    r53 = spu_ceq(r80, r68);
    /* lines 17842-17853 */
    r44 = spu_il(1712);
    r1 = spu_a(r1, r44);
    r80 = LQ(r1._u32[0] + -0x10);
    r81 = LQ(r1._u32[0] + -0x20);
    r82 = LQ(r1._u32[0] + -0x30);
    r83 = LQ(r1._u32[0] + -0x40);
    r84 = LQ(r1._u32[0] + -0x50);
    r85 = LQ(r1._u32[0] + -0x60);
    r86 = LQ(r1._u32[0] + -0x70);
    r87 = LQ(r1._u32[0] + -0x80);
    r126 = LQ(r1._u32[0] + -0x90);
#undef LQ
    NX8_REGS(NX8_STORE)
}

/* ---- state of one call ----------------------------------------------------------------------- */

#define NX8_SCAN 48                  /* columns one look-ahead step covers at most */
#define NX8_LOOK 12                  /* columns the look-ahead stays in front by */
#define NX8_MAXCOLS 1024             /* columns the native code takes (r53row below) */
/* Cells worked out ahead of time: see nx8_precompute. */
#define NPRE 16
typedef struct Pre {
    uint32_t qhead, qtail;           /* slots handed out / filled (a ring of NPRE, filled eight at a time) */
    uint32_t scan;                   /* next column to look at for the next batch */
    uint32_t k[NPRE], valid[NPRE];   /* column of each slot; usable */
    uint32_t mb[NPRE], tb[NPRE], x0[NPRE], y0[NPRE], idx[NPRE], fxw[NPRE];
    uint32_t dir2x[NPRE], dir2y[NPRE], r60cx[NPRE], r60cy[NPRE];
    __m128i craw[NPRE], p1raw[NPRE], p0raw[NPRE];   /* the three lines the slot was worked out from */
} Pre;

typedef struct State {
    RF rf;
    uint8_t* ls;
    __m128i hl[2];                   /* the current row header's two loads */
    u128 hdr_r5, hdr_r80;            /* and the two registers it read that change from row to row */
    uint32_t row;                    /* current row */
    uint32_t seq;
    uint32_t nret, nfree, cur, e_valid;
    uint32_t fault_dest;             /* NATIVE_STATS: the last line a fast cell stored (0xFFFFFFFF: none) */
    uint8_t ret[NPOOL], freel[NPOOL];
    CellLog elog;                    /* the last empty cell (blocks 1 and 4 only) */
    CellLog pool[NPOOL];
    Pre pre;                         /* cells of the current row worked out ahead of time */
    __m128i r53row[NX8_MAXCOLS + 1]; /* r53 at each column of the current row */
} State;

/* Per thread rather than on the stack: 23 KB, and a lifted SPU function is entered with whatever
 * host stack its caller left. The function does not re-enter itself. */
static SPU_THREAD_LOCAL State t_nx8_state;

static void state_init(State* st) {
    st->seq = 0; st->nret = 0; st->e_valid = 0; st->row = 0; st->fault_dest = 0xFFFFFFFFu;
    for (unsigned i = 0; i < NPOOL; i++) st->freel[i] = (uint8_t)i;
    st->nfree = NPOOL - 1; st->cur = NPOOL - 1;
}

/* The cell in pool[cur] has run blocks `bb`: older records lose those blocks; one with none left
 * is free again. The retained list stays in running order. */
static inline void state_retain(State* st, uint32_t bb) {
    unsigned w = 0;
    for (unsigned i = 0; i < st->nret; i++) {
        const unsigned idx = st->ret[i];
        const uint32_t own = st->pool[idx].own & ~bb;
        st->pool[idx].own = own;
        if (own) st->ret[w++] = (uint8_t)idx; else st->freel[st->nfree++] = (uint8_t)idx;
    }
    st->pool[st->cur].own = bb;
    st->ret[w++] = (uint8_t)st->cur;
    st->nret = w;
    st->cur = st->freel[--st->nfree];
}

static void state_apply_header(State* st) {
    st->rf.gpr[5] = st->hdr_r5; st->rf.gpr[80] = st->hdr_r80;
    blk_header(&st->rf, st->ls, st->hl, MODE_REPLAY);
}
static void state_replay(State* st, CellLog* lg) {
    st->rf.gpr[55] = nx8_st(_mm_set1_epi32((int)lg->k)); st->rf.gpr[53] = nx8_st(lg->r53);
    st->rf.gpr[51] = nx8_st(lg->r51); st->rf.gpr[54] = nx8_st(lg->r54);
    (void)blk_cell(&st->rf, st->ls, lg, MODE_REPLAY);
}
/* Bring RF to what the lifted function's registers hold after the last cell run so far: replay the
 * retained cells in order, with the current row's header (which ran after every cell of earlier
 * rows) in its place. */
static NX8_NOINLINE void state_materialise(State* st) {
    int hdr_done = 0;
    unsigned i = 0;
    int e = (int)st->e_valid;
    for (;;) {
        CellLog* lg;
        if (i < st->nret && (!e || st->pool[st->ret[i]].seq < st->elog.seq)) lg = &st->pool[st->ret[i++]];
        else if (e) { lg = &st->elog; e = 0; }
        else break;
        if (!hdr_done && lg->row == st->row) { state_apply_header(st); hdr_done = 1; }
        state_replay(st, lg);
    }
    if (!hdr_done) state_apply_header(st);
    nx8_clear_flags();
}

/* ---- the fast path --------------------------------------------------------------------------- */
/*
 * What a cell does that outlives it: it steps the random generator (a line of local store), and
 * for an occupied cell it stores into up to four cells of the destination strips. Everything else
 * it computes is register scratch. The fast path computes just that, on the lanes that reach it:
 * the mass is one float, velocity and position two.
 *
 * Exactness rests on these, all checked:
 *  - the host float mode is the SPU one (round toward zero, denormals are zero, flush to zero), so
 *    a host add/mul/fma of ordinary operands with an in-range result is the lifted helper's result
 *    once -0 is turned into +0 (done wherever the sign of a zero can reach local store);
 *  - no operand has exponent 255 (the SPU's "large number", the host's Inf/NaN): every float taken
 *    from local store or from the parameter block is checked;
 *  - no result left the range: the sticky overflow / invalid flags of MXCSR are clear at the end of
 *    the cell (invalid also covers a float -> int conversion out of range);
 *  - the three constants at LS 0x8E60-0x8E8F are the image's, and no store of the cell lands there.
 * A cell that fails any of them is undone (its stores are put back) and run as lifted statements.
 */

#define PS(x) _mm_castsi128_ps(x)
#define SI(x) _mm_castps_si128(x)
#define NX8_K(bits) _mm_set1_epi32((int)(bits))

static inline __attribute__((always_inline)) uint32_t nx8_be32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return __builtin_bswap32(v); }
static inline __attribute__((always_inline)) int32_t nx8_be16s(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return (int16_t)__builtin_bswap16(v); }
static inline __attribute__((always_inline)) __m128 nx8_flushf(__m128 r) {
    return PS(_mm_andnot_si128(_mm_cmpeq_epi32(SI(r), NX8_K(0x80000000u)), SI(r)));
}
static inline __attribute__((always_inline)) uint32_t nx8_bits(__m128 x) { return (uint32_t)_mm_cvtsi128_si32(SI(x)); }
static inline __attribute__((always_inline)) __m128 nx8_scalar(uint32_t bits) { return PS(_mm_cvtsi32_si128((int)bits)); }
static inline __attribute__((always_inline)) __m128 nx8_splat(uint32_t bits) { return PS(_mm_set1_epi32((int)bits)); }

/* frsqest / frest / fi on one lane: the rules of spu_frsqest_ref, spu_frest_ref and spu_fi. */
static inline __attribute__((always_inline)) uint32_t nx8_frsqest(uint32_t x) {
    const uint32_t exp = (x >> 23) & 0xFFu;
    const uint32_t rexp = exp ? 190u - ((exp + 1u) >> 1) : 255u;
    return (rexp << 23) | (spu_frsqest_fraction_lut[(x >> 18) & 0x3Fu] & 0x7FFFFFu);
}
static inline __attribute__((always_inline)) uint32_t nx8_frest(uint32_t x) {
    const uint32_t exp = (x >> 23) & 0xFFu;
    const uint32_t rexp = exp == 0u ? 255u : exp >= 253u ? 0u : 253u - exp;
    return (x & 0x80000000u) | (rexp << 23) | (spu_frest_fraction_lut[(x >> 18) & 0x1Fu] & 0x7FFFFFu);
}
static inline __attribute__((always_inline)) uint32_t nx8_fi(uint32_t a, uint32_t est) {
    const uint32_t y = a & 0x7FFFFu, exp = (est >> 23) & 0xFFu;
    const uint32_t b19 = ((est >> 10) & 0x1FFFu) << 19, sy = (est & 0x3FFu) * y;
    const uint32_t big = b19 >= sy, d = b19 - sy;
    const uint32_t frac = (big ? d >> 9 : d >> 8) & 0x7FFFFFu;
    const uint32_t e2 = exp - (big ^ 1u);
    return (exp == 0u || e2 == 0u) ? 0u : ((est & 0x80000000u) | (e2 << 23) | frac);
}
/* cuflt(u, 123), one lane: u / 2^32 truncated (the conversion truncates in this float mode). */
static inline __attribute__((always_inline)) __m128 nx8_cuflt123(uint32_t u) {
    return _mm_mul_ss(_mm_cvtsi64_ss(_mm_setzero_ps(), (long long)u), nx8_scalar(0x2F800000u));
}

/* One step of the generator (0x8298: r20 -> r8, and again at 0x8334 and 0x8650): state S in
 * register order, new state, and the 32 random bits it yields. */
#define NX8_RNG(S, NS, U) do { \
        const __m128i t_ = _mm_shuffle_epi32((S), 0x93);                                    /* (s3, s0, s1, s2) */ \
        const __m128i u_ = _mm_xor_si128(_mm_xor_si128(t_, _mm_srli_epi32(t_, 8)), \
                                         _mm_xor_si128((S), _mm_srli_epi32((S), 19))); \
        const __m128i w_ = _mm_xor_si128(t_, _mm_slli_epi32(t_, 11)); \
        (NS) = _mm_blend_epi16(_mm_blend_epi16(t_, w_, 0xC0), u_, 0x03); \
        (U) = (uint32_t)_mm_cvtsi128_si32(u_); \
    } while (0)

/* spu_fa on four lanes, as 0x891C / 0x8934 use it on r53: exact for any operand. */
static inline __attribute__((always_inline)) __m128i nx8_fa(__m128i a, __m128i b) {
    const __m128 t = _mm_add_ps(PS(a), PS(b));
    if (NX8_LIKELY(spu__x8_all4(spu__x8_inrange(t)))) return spu__x8_flush(SI(t));
    const __m128i r = nx8_ld(spu_fa(nx8_st(a), nx8_st(b)));
    nx8_clear_flags();
    return r;
}

typedef struct Fast {
    uint32_t rngp;                   /* LS line of the generator state */
    uint64_t ev_below;               /* the random event happens when the 32 random bits are below this */
    int32_t key78;                   /* speed^2 above this (as a float key): the second stencil table */
    uint32_t tblA, tblB, tblC;       /* the three tables of the image (0x9000, 0xA000, 0xB000 with the usual base) */
    uint32_t tbase;                  /* sp + 32: the row-pointer tables on the stack */
    uint32_t m44_0, m44_1;           /* coordinate masks */
    int32_t xmax, ymax;              /* bounds */
    int ev_ok;                       /* the event's mass is the same in lanes 0-2 */
    uint32_t aa0, ab0, a60, a20, a30;
    __m128 a40, a50, ac0, r58, r61, r64, r74, dt;   /* lanes 2 and 3 zero */
    __m128i r67, r62;
} Fast;

static int nx8_plain_lanes(u128 x, unsigned lanes) {
    for (unsigned i = 0; i < 4; i++) if ((lanes >> i & 1u) && (x._u32[i] & 0x7F800000u) == 0x7F800000u) return 0;
    return 1;
}
static int nx8_event_at(u128 r77, uint32_t u) {
    return spu__key_fcgt(r77, spu_cuflt(spu_splat_u32(u), 123))._u32[0] != 0;      /* r37 = fcgt(r77, r27), word 0 */
}

/* After the start of the function has run on RF: can this call use the fast path at all? */
static NX8_NOINLINE int fast_setup(Fast* f, const State* st) {
    const u128* g = st->rf.gpr;
    const uint8_t* ls = st->ls;
    const uint32_t sp = g[1]._u32[0];
    const __m128i m01 = _mm_setr_epi32(-1, -1, 0, 0);
    if (sp & 15u) return 0;                                   /* the insert positions and the row tables assume it */
    if (memcmp(ls + 0x8E60, k_const_8E60, 48) != 0) return 0;
    f->rngp = g[34]._u32[0] & LSM;
    if (f->rngp - 0x8E60u < 0x30u) return 0;
    if (!nx8_plain_lanes(g[76], 3) || !nx8_plain_lanes(g[75], 3) || !nx8_plain_lanes(g[73], 3) ||
        !nx8_plain_lanes(g[58], 3) || !nx8_plain_lanes(g[64], 3) || !nx8_plain_lanes(g[74], 3) ||
        !nx8_plain_lanes(g[65], 3) || !nx8_plain_lanes(g[84], 4) || !nx8_plain_lanes(g[83], 4) ||
        !nx8_plain_lanes(g[82], 4)) return 0;
    f->ev_ok = nx8_plain_lanes(g[71], 7) && nx8_plain_lanes(g[72], 7) &&
               g[71]._u32[0] == g[71]._u32[1] && g[71]._u32[0] == g[71]._u32[2] &&
               g[72]._u32[0] == g[72]._u32[1] && g[72]._u32[0] == g[72]._u32[2];
    f->a30 = g[71]._u32[0]; f->a20 = g[72]._u32[0];
    f->aa0 = g[84]._u32[2]; f->ab0 = g[83]._u32[2]; f->a60 = g[82]._u32[2];
    f->a40 = PS(_mm_and_si128(nx8_ld(g[76]), m01)); f->a50 = PS(_mm_and_si128(nx8_ld(g[75]), m01));
    f->ac0 = PS(_mm_and_si128(nx8_ld(g[73]), m01));
    f->r58 = PS(_mm_and_si128(nx8_ld(g[58]), m01)); f->r61 = PS(_mm_and_si128(nx8_ld(g[61]), m01));
    f->r64 = PS(_mm_and_si128(nx8_ld(g[64]), m01)); f->r74 = PS(_mm_and_si128(nx8_ld(g[74]), m01));
    f->dt = PS(_mm_and_si128(nx8_ld(g[65]), m01));
    f->r67 = _mm_and_si128(nx8_ld(g[67]), m01);
    f->r62 = nx8_ld(g[62]);
    f->tblA = g[126]._u32[0] + 0x5000u; f->tblB = g[126]._u32[0] + 0x6000u; f->tblC = g[126]._u32[0] + 0x7000u;
    f->tbase = sp + 32u;
    f->m44_0 = g[44]._u32[0]; f->m44_1 = g[44]._u32[1];
    f->xmax = g[46]._s32[2]; f->ymax = g[46]._s32[3];
    f->key78 = (int32_t)spu__sf_key(g[78]._u32[0]);
    /* fcgt(r77, cuflt(u)) only falls as u rises: find where */
    if (!nx8_event_at(g[77], 0)) f->ev_below = 0;
    else if (nx8_event_at(g[77], 0xFFFFFFFFu)) f->ev_below = 1ull << 32;
    else {
        uint32_t lo = 0, hi = 0xFFFFFFFFu;                    /* event at lo, none at hi */
        while (hi - lo > 1) { const uint32_t mid = lo + (hi - lo) / 2; if (nx8_event_at(g[77], mid)) lo = mid; else hi = mid; }
        f->ev_below = hi;
    }
    return 1;
}

/* ---- r53 along a row ------------------------------------------------------------------------- */
/*
 * The lifted code adds r62 to r53 once per cell (0x891C), each add truncated: a chain as long as
 * the row, 6 cycles a link. When every partial sum is exactly representable the adds do not round
 * and column c holds r53 + c * r62 outright: one fused multiply-add with an exact result, no chain.
 * That is so when both are multiples of a common power of two (2^u) and the sums stay below
 * 2^(u+24): positions and steps that are whole numbers of cells, say. Otherwise: the chain.
 */
static int nx8_lane_exact(uint32_t a, uint32_t b, uint32_t ncols) {
    const uint32_t ea = (a >> 23) & 0xFFu, eb = (b >> 23) & 0xFFu;
    if (ea == 255u || eb == 255u) return 0;                   /* the SPU's large numbers */
    if (ea == 0u && a != 0u) return 0;                        /* -0 or a denormal pattern in r53: leave it to the adds */
    if (eb == 0u) return 1;                                   /* the step is zero: a + 0 = a */
    int u = (int)eb - 150 + __builtin_ctz((b & 0x7FFFFFu) | 0x800000u);          /* lowest set bit of the step */
    int top = (int)eb - 126 + 10;                             /* ncols * |b| < 2^top (ncols <= 1024) */
    if (ea != 0u) {
        const int ua = (int)ea - 150 + __builtin_ctz((a & 0x7FFFFFu) | 0x800000u);
        if (ua < u) u = ua;
        if ((int)ea - 126 > top) top = (int)ea - 126;         /* |a| < 2^(ea-126) */
    }
    (void)ncols;
    /* every partial sum is a multiple of 2^u below 2^(top+1): a float if that is below 2^(u+24),
     * with nothing in the denormal range and nothing near the top */
    return u >= -126 && top + 1 <= u + 23 && top + 1 <= 126;
}
static NX8_NOINLINE void nx8_r53_fill(__m128i* row, __m128i r53, __m128i r62, uint32_t ncols) {
    const u128 a = nx8_st(r53), b = nx8_st(r62);
    int exact = 1;
    for (int i = 0; i < 4; i++) exact &= nx8_lane_exact(a._u32[i], b._u32[i], ncols);
    if (exact) {
        for (uint32_t c = 0; c < ncols; c++)
            _mm_storeu_si128(&row[c], SI(_mm_fmadd_ps(_mm_cvtepi32_ps(_mm_set1_epi32((int)c)), PS(r62), PS(r53))));
    } else {
        __m128i t = r53;
        for (uint32_t c = 0; c < ncols; c++) { _mm_storeu_si128(&row[c], t); t = nx8_fa(t, r62); }
    }
}

/* ---- eight occupied cells at a time ---------------------------------------------------------- */
/*
 * The part of an occupied cell from its 8 bytes to "which stencil cell, which target, what
 * direction" (0x83D8 up to the stencil cell's load) is one long chain: two reciprocal square
 * roots and a reciprocal in series, some 200 cycles, and nothing later can retire while it runs.
 * It depends on nothing a neighbouring cell stores: only on the cell's 8 bytes, the two lines of
 * its column, the row position and the parameters. So it is done ahead of time for the next eight
 * occupied cells of the row at once, one cell per lane (x and y in separate vectors): eight chains
 * for the latency of one. When a cell's turn comes its three lines are compared with what the
 * batch read; if anything differs, or the cell has the random event, it takes the one-cell code.
 */
static inline __attribute__((always_inline)) uint64_t nx8_ld64(const void* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline __attribute__((always_inline)) __m256i nx8_lut8(const uint32_t* lut, __m256i idx) {
    uint32_t i[8];
    _mm256_storeu_si256((__m256i*)i, idx);
    return _mm256_setr_epi32((int)lut[i[0]], (int)lut[i[1]], (int)lut[i[2]], (int)lut[i[3]],
                             (int)lut[i[4]], (int)lut[i[5]], (int)lut[i[6]], (int)lut[i[7]]);
}
#define NX8_K8(bits) _mm256_set1_epi32((int)(bits))
/* spu_frsqest / spu_frest / spu_fi, eight lanes (the same integer rules as the helpers' 128-bit forms). */
static inline __attribute__((always_inline)) __m256i nx8_frsqest8(__m256i x) {
    const __m256i exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), NX8_K8(0xFF));
    const __m256i frac = _mm256_and_si256(nx8_lut8(spu_frsqest_fraction_lut, _mm256_and_si256(_mm256_srli_epi32(x, 18), NX8_K8(0x3F))),
                                          NX8_K8(0x7FFFFF));
    __m256i rexp = _mm256_sub_epi32(NX8_K8(190), _mm256_srli_epi32(_mm256_add_epi32(exp, NX8_K8(1)), 1));
    rexp = _mm256_blendv_epi8(rexp, NX8_K8(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_slli_epi32(rexp, 23), frac);
}
static inline __attribute__((always_inline)) __m256i nx8_frest8(__m256i x) {
    const __m256i exp = _mm256_and_si256(_mm256_srli_epi32(x, 23), NX8_K8(0xFF));
    const __m256i frac = _mm256_and_si256(nx8_lut8(spu_frest_fraction_lut, _mm256_and_si256(_mm256_srli_epi32(x, 18), NX8_K8(0x1F))),
                                          NX8_K8(0x7FFFFF));
    __m256i rexp = _mm256_sub_epi32(NX8_K8(253), exp);
    rexp = _mm256_andnot_si256(_mm256_cmpgt_epi32(exp, NX8_K8(252)), rexp);
    rexp = _mm256_blendv_epi8(rexp, NX8_K8(255), _mm256_cmpeq_epi32(exp, _mm256_setzero_si256()));
    return _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(x, NX8_K8(0x80000000u)), _mm256_slli_epi32(rexp, 23)), frac);
}
static inline __attribute__((always_inline)) __m256i nx8_fi8(__m256i a, __m256i est) {
    const __m256i y = _mm256_and_si256(a, NX8_K8(0x7FFFF));
    const __m256i exp = _mm256_and_si256(_mm256_srli_epi32(est, 23), NX8_K8(0xFF));
    const __m256i b19 = _mm256_slli_epi32(_mm256_and_si256(_mm256_srli_epi32(est, 10), NX8_K8(0x1FFF)), 19);
    const __m256i step = _mm256_and_si256(est, NX8_K8(0x3FF));
    /* step * y is below 2^29: the even and the odd lanes as 64-bit products */
    const __m256i sy = _mm256_blend_epi32(_mm256_mul_epu32(step, y),
                           _mm256_slli_epi64(_mm256_mul_epu32(_mm256_srli_epi64(step, 32), _mm256_srli_epi64(y, 32)), 32), 0xAA);
    const __m256i big = _mm256_cmpeq_epi32(_mm256_max_epu32(b19, sy), b19);
    const __m256i d = _mm256_sub_epi32(b19, sy);
    const __m256i frac = _mm256_and_si256(_mm256_blendv_epi8(_mm256_srli_epi32(d, 8), _mm256_srli_epi32(d, 9), big), NX8_K8(0x7FFFFF));
    const __m256i e2 = _mm256_sub_epi32(exp, _mm256_andnot_si256(big, NX8_K8(1)));
    const __m256i r = _mm256_or_si256(_mm256_or_si256(_mm256_and_si256(est, NX8_K8(0x80000000u)), _mm256_slli_epi32(e2, 23)), frac);
    return _mm256_andnot_si256(_mm256_or_si256(_mm256_cmpeq_epi32(exp, _mm256_setzero_si256()),
                                               _mm256_cmpeq_epi32(e2, _mm256_setzero_si256())), r);
}
static inline __attribute__((always_inline)) __m256 nx8_flush8(__m256 r) {
    return _mm256_castsi256_ps(_mm256_andnot_si256(_mm256_cmpeq_epi32(_mm256_castps_si256(r), NX8_K8(0x80000000u)), _mm256_castps_si256(r)));
}
#define PS8(x) _mm256_castsi256_ps(x)
#define SI8(x) _mm256_castps_si256(x)
#define NX8_X8(v) _mm256_broadcastss_ps(v)                                   /* lane 0 of a parameter, for every cell */
#define NX8_Y8(v) _mm256_broadcastss_ps(_mm_shuffle_ps(v, v, 0x55))          /* lane 1 */
/* Eight 8-byte fields a[0..7], two per 128-bit half in the order (0, 1 | 4, 5) and (2, 3 | 6, 7): after
 * a byte shuffle that puts each field's first dword in the low half of its 128 bits and the second
 * in the high half, unpacklo / unpackhi of the two give the cells in order 0..7. */
#define NX8_Q4(a, i0, i1, i2, i3) _mm256_set_epi64x((long long)(a)[i3], (long long)(a)[i2], (long long)(a)[i1], (long long)(a)[i0])

/* Find the next (up to) eight occupied cells of the row from pre->scan on and do their 0x83D8 work.
 * Names in comments are the lifted code's registers. */
static NX8_NOINLINE void nx8_precompute(Pre* pre, const Fast* f, const uint8_t* ls, uint32_t r66_0, uint32_t r51row_0,
                                        const __m128i* r53row, uint32_t r54_1, uint32_t ncols) {
    uint32_t kk[8], cla[8];
    uint64_t src[8], p1[8], p0[8], rp[8];
    unsigned n = 0;
    uint32_t s = pre->scan;
    const uint32_t end = ncols - s > NX8_SCAN ? s + NX8_SCAN : ncols;
    /* look at a stretch of the row for occupied cells */
    for (; s < end && n < 8; s++) {
        const uint32_t a = ((uint32_t)((int32_t)(int16_t)(s & 0xFFFEu) << 3) + r66_0) & LSM;
        if (nx8_be16s(ls + a + ((s & 1u) << 3)) > 0) { kk[n] = s; cla[n] = a; n++; }
    }
    pre->scan = s;
    if (n == 0) return;
    for (unsigned i = n; i < 8; i++) { kk[i] = kk[n - 1]; cla[i] = cla[n - 1]; }      /* fill up with the last one */
    const unsigned base = pre->qtail & (NPRE - 1);
    for (unsigned i = 0; i < 8; i++) {
        const uint32_t r51_0 = r51row_0 + 32u * kk[i];
        const __m128i c = _mm_loadu_si128((const __m128i*)(ls + cla[i]));
        const __m128i a1 = _mm_loadu_si128((const __m128i*)(ls + ((r51_0 + 0x10u) & LSM)));
        const __m128i a0 = _mm_loadu_si128((const __m128i*)(ls + (r51_0 & LSM)));
        pre->k[base + i] = kk[i];
        _mm_storeu_si128(&pre->craw[base + i], c);
        _mm_storeu_si128(&pre->p1raw[base + i], a1);
        _mm_storeu_si128(&pre->p0raw[base + i], a0);
        src[i] = nx8_ld64(ls + cla[i] + ((kk[i] & 1u) << 3));
        p1[i] = (uint64_t)_mm_cvtsi128_si64(a1); p0[i] = (uint64_t)_mm_cvtsi128_si64(a0);
        rp[i] = nx8_ld64(&r53row[kk[i]]);
    }
    const __m256i zero = _mm256_setzero_si256();
    const __m256i one = NX8_K8(0x3F800000u);
    /* byte shuffles, per 128 bits holding two 8-byte fields a (bytes 0-7) and b (bytes 8-15) */
    const __m256i ctl_be = _mm256_setr_epi8(3, 2, 1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 15, 14, 13, 12,
                                            3, 2, 1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 15, 14, 13, 12);       /* word0 a, word0 b, word1 a, word1 b */
    const __m256i ctl_v = _mm256_setr_epi8(-1, -1, 3, 2, -1, -1, 11, 10, -1, -1, 7, 6, -1, -1, 15, 14,
                                           -1, -1, 3, 2, -1, -1, 11, 10, -1, -1, 7, 6, -1, -1, 15, 14);   /* bytes 2-3 and 6-7, << 16 */
    const __m256i ctl_b = _mm256_setr_epi8(4, -1, -1, -1, 12, -1, -1, -1, 5, -1, -1, -1, 13, -1, -1, -1,
                                           4, -1, -1, -1, 12, -1, -1, -1, 5, -1, -1, -1, 13, -1, -1, -1); /* byte 4, byte 5 */
    const __m256i ctl_h = _mm256_setr_epi8(-1, -1, 1, 0, -1, -1, 9, 8, -1, -1, -1, -1, -1, -1, -1, -1,
                                           -1, -1, 1, 0, -1, -1, 9, 8, -1, -1, -1, -1, -1, -1, -1, -1);   /* bytes 0-1, << 16 */
    const __m256i cA = NX8_Q4(src, 0, 1, 4, 5), cB = NX8_Q4(src, 2, 3, 6, 7);
    const __m256i p1A = _mm256_shuffle_epi8(NX8_Q4(p1, 0, 1, 4, 5), ctl_be), p1B = _mm256_shuffle_epi8(NX8_Q4(p1, 2, 3, 6, 7), ctl_be);
    const __m256i p0A = _mm256_shuffle_epi8(NX8_Q4(p0, 0, 1, 4, 5), ctl_be), p0B = _mm256_shuffle_epi8(NX8_Q4(p0, 2, 3, 6, 7), ctl_be);
    const __m256i rA = _mm256_shuffle_epi32(NX8_Q4(rp, 0, 1, 4, 5), 0xD8), rB = _mm256_shuffle_epi32(NX8_Q4(rp, 2, 3, 6, 7), 0xD8);
    const __m256i vA = _mm256_shuffle_epi8(cA, ctl_v), vB = _mm256_shuffle_epi8(cB, ctl_v);
    const __m256i bA = _mm256_shuffle_epi8(cA, ctl_b), bB = _mm256_shuffle_epi8(cB, ctl_b);
    const __m256i p1x = _mm256_unpacklo_epi64(p1A, p1B), p1y = _mm256_unpackhi_epi64(p1A, p1B);
    const __m256i p0x = _mm256_unpacklo_epi64(p0A, p0B), p0y = _mm256_unpackhi_epi64(p0A, p0B);
    const __m256i r53x = _mm256_unpacklo_epi64(rA, rB), r53y = _mm256_unpackhi_epi64(rA, rB);
    const __m256i abs8 = NX8_K8(0x7FFFFFFFu);
    const __m256i acc = _mm256_max_epu32(_mm256_max_epu32(_mm256_max_epu32(_mm256_and_si256(p1x, abs8), _mm256_and_si256(p1y, abs8)),
                                                          _mm256_max_epu32(_mm256_and_si256(p0x, abs8), _mm256_and_si256(p0y, abs8))),
                                         _mm256_max_epu32(_mm256_and_si256(r53x, abs8), _mm256_and_si256(r53y, abs8)));
    /* velocity, sub-cell position, mass */
    const __m256 k2m12 = PS8(NX8_K8(0x39800000u)), k2m8 = PS8(NX8_K8(0x3B800000u));
    const __m256 vx = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srai_epi32(_mm256_unpacklo_epi64(vA, vB), 16)), k2m12);   /* r13 */
    const __m256 vy = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srai_epi32(_mm256_unpackhi_epi64(vA, vB), 16)), k2m12);
    const __m256 bfx = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_unpacklo_epi64(bA, bB)), k2m8);                          /* r16 */
    const __m256 bfy = _mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_unpackhi_epi64(bA, bB)), k2m8);
    const __m256i mb = SI8(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_srai_epi32(_mm256_unpacklo_epi64(
                           _mm256_shuffle_epi8(cA, ctl_h), _mm256_shuffle_epi8(cB, ctl_h)), 16)), PS8(NX8_K8(0x33800000u))));   /* r22 */
    const __m256i len2 = SI8(_mm256_add_ps(_mm256_mul_ps(vx, vx), _mm256_mul_ps(vy, vy)));                 /* r10, r36 */
    const __m256i tbsel = _mm256_cmpgt_epi32(len2, _mm256_set1_epi32(f->key78));                           /* r38 */
    const __m256 slow = PS8(_mm256_cmpgt_epi32(NX8_K8(0x38D1B717u), len2));
    const __m256 fricx = _mm256_blendv_ps(NX8_X8(f->a50), NX8_X8(f->a40), slow);                           /* r28 */
    const __m256 fricy = _mm256_blendv_ps(NX8_Y8(f->a50), NX8_Y8(f->a40), slow);
    const __m256i isq = nx8_fi8(mb, nx8_frsqest8(mb));                                                     /* r29 */
    const __m256 sq = PS8(nx8_fi8(isq, nx8_frest8(isq)));                                                  /* r28 */
    const __m256 dx = _mm256_sub_ps(PS8(p1x), vx), dy = _mm256_sub_ps(PS8(p1y), vy);                       /* r50 */
    const __m256i d2 = SI8(_mm256_add_ps(_mm256_mul_ps(dx, dx), _mm256_mul_ps(dy, dy)));                   /* r18, r17 */
    const __m256i d2s = _mm256_blendv_epi8(d2, one, _mm256_cmpeq_epi32(d2, zero));                         /* r25 */
    const __m256 gx = _mm256_mul_ps(PS8(d2s), _mm256_mul_ps(NX8_X8(f->ac0), sq));                          /* r27, r24 */
    const __m256 gy = _mm256_mul_ps(PS8(d2s), _mm256_mul_ps(NX8_Y8(f->ac0), sq));
    const __m256 il = PS8(nx8_fi8(d2s, nx8_frsqest8(d2s)));                                                /* r60 */
    const __m256 px = _mm256_add_ps(_mm256_fmadd_ps(_mm256_mul_ps(dx, il), gx, vx), PS8(p0x));             /* r49, r47, r5 */
    const __m256 py = _mm256_add_ps(_mm256_fmadd_ps(_mm256_mul_ps(dy, il), gy, vy), PS8(p0y));
    const __m256i l2 = SI8(_mm256_add_ps(_mm256_mul_ps(px, px), _mm256_mul_ps(py, py)));                   /* r41, r3 */
    const __m256i l2s = _mm256_blendv_epi8(l2, one, _mm256_cmpeq_epi32(l2, zero));                         /* r21 */
    const __m256i il2 = nx8_fi8(l2s, nx8_frsqest8(l2s));                                                   /* r19 */
    const __m256 dirx = nx8_flush8(_mm256_mul_ps(px, PS8(il2))), diry = nx8_flush8(_mm256_mul_ps(py, PS8(il2)));   /* r24 */
    const __m256i len = nx8_fi8(il2, nx8_frest8(il2));                                                     /* r18 */
    const __m256 nolen = PS8(_mm256_cmpeq_epi32(len, zero));
    const __m256 mvx = nx8_flush8(_mm256_mul_ps(dirx, _mm256_max_ps(_mm256_fnmadd_ps(fricx, PS8(isq), PS8(len)), _mm256_setzero_ps())));   /* r26, r25, r60 */
    const __m256 mvy = nx8_flush8(_mm256_mul_ps(diry, _mm256_max_ps(_mm256_fnmadd_ps(fricy, PS8(isq), PS8(len)), _mm256_setzero_ps())));
    const __m256 dir2x = _mm256_blendv_ps(mvx, dirx, nolen), dir2y = _mm256_blendv_ps(mvy, diry, nolen);   /* r24 */
    const __m256 t2x = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(dir2x, NX8_X8(f->dt)), NX8_X8(f->r61)), NX8_X8(f->r58));   /* r48, r42, r41 */
    const __m256 t2y = _mm256_min_ps(_mm256_max_ps(_mm256_mul_ps(dir2y, NX8_Y8(f->dt)), NX8_Y8(f->r61)), NX8_Y8(f->r58));
    const __m256 gqx = _mm256_mul_ps(_mm256_fmadd_ps(bfx, NX8_X8(f->r64), _mm256_add_ps(PS8(r53x), t2x)), NX8_X8(f->r74));   /* r40, r12, r3 */
    const __m256 gqy = _mm256_mul_ps(_mm256_fmadd_ps(bfy, NX8_Y8(f->r64), _mm256_add_ps(PS8(r53y), t2y)), NX8_Y8(f->r74));
    const __m256i gix = _mm256_cvttps_epi32(gqx), giy = _mm256_cvttps_epi32(gqy);                          /* r39 */
    const __m256 k128 = PS8(NX8_K8(0x3C000000u)), k256 = PS8(NX8_K8(0x43800000u));
    const __m256i fx = _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_add_ps(_mm256_sub_ps(gqx, _mm256_cvtepi32_ps(gix)), k128), k256));   /* r36, r21, r8 */
    const __m256i fy = _mm256_cvttps_epi32(_mm256_mul_ps(_mm256_add_ps(_mm256_sub_ps(gqy, _mm256_cvtepi32_ps(giy)), k128), k256));
    const __m256i x0 = _mm256_add_epi32(gix, _mm256_broadcastd_epi32(f->r67));                             /* r42 */
    const __m256i y0 = _mm256_add_epi32(giy, _mm256_broadcastd_epi32(_mm_shuffle_epi32(f->r67, 0x55)));
    const __m256i k4 = NX8_K8(4), k7 = NX8_K8(7), k255 = NX8_K8(255);
    const __m256i relx = _mm256_min_epi32(_mm256_max_epi32(_mm256_add_epi32(_mm256_sub_epi32(x0, _mm256_loadu_si256((const __m256i*)kk)), k4), zero), k7);
    const __m256i rely = _mm256_min_epi32(_mm256_max_epi32(_mm256_add_epi32(_mm256_sub_epi32(y0, _mm256_set1_epi32((int)r54_1)), k4), zero), k7);   /* r20 */
    const __m256i fxw = _mm256_or_si256(_mm256_slli_epi32(_mm256_min_epi32(_mm256_max_epi32(fx, zero), k255), 24),
                                        _mm256_slli_epi32(_mm256_min_epi32(_mm256_max_epi32(fy, zero), k255), 16));   /* r33 */
    const __m256 km79 = PS8(NX8_K8(0xC0FCCCCCu)), kp79 = PS8(NX8_K8(0x40FCCCCCu));
    _mm256_storeu_si256((__m256i*)&pre->mb[base], mb);
    _mm256_storeu_si256((__m256i*)&pre->tb[base], tbsel);
    _mm256_storeu_si256((__m256i*)&pre->x0[base], x0);
    _mm256_storeu_si256((__m256i*)&pre->y0[base], y0);
    _mm256_storeu_si256((__m256i*)&pre->idx[base], _mm256_add_epi32(relx, _mm256_slli_epi32(rely, 3)));
    _mm256_storeu_si256((__m256i*)&pre->fxw[base], fxw);
    _mm256_storeu_ps((float*)&pre->dir2x[base], dir2x);
    _mm256_storeu_ps((float*)&pre->dir2y[base], dir2y);
    _mm256_storeu_ps((float*)&pre->r60cx[base], _mm256_min_ps(_mm256_max_ps(dir2x, km79), kp79));          /* r60 */
    _mm256_storeu_ps((float*)&pre->r60cy[base], _mm256_min_ps(_mm256_max_ps(dir2y, km79), kp79));
    /* An operand with exponent 255, or a result out of range (the flags stay set: the cell being
     * worked on bails too, which is correct and rare): the batch is not used. */
    const uint32_t ok = (_mm256_movemask_ps(PS8(_mm256_cmpgt_epi32(acc, NX8_K8(0x7F7FFFFFu)))) == 0) && (_mm_getcsr() & NX8_FLAGS) == 0;
    for (unsigned i = 0; i < 8; i++) pre->valid[base + i] = ok && i < n;
    pre->qtail += 8; NX8_STAT(6, 1); NX8_STAT(7, n);
}

/* ---- the function ---------------------------------------------------------------------------- */


/* Hand the rest of the function to the lifted code at label `pc` (0x8238: before a row header;
 * 0x8298: before the cell in column k). */
static NX8_NOINLINE void nx8_deopt(spu_context* ctx, State* st, uint32_t pc, uint32_t k, __m128i r53, __m128i r51) {
    if (pc == 0x8298) {
        state_materialise(st);
        st->rf.gpr[55] = nx8_st(_mm_set1_epi32((int)k)); st->rf.gpr[53] = nx8_st(r53); st->rf.gpr[51] = nx8_st(r51);
    }
    memcpy(ctx->gpr, st->rf.gpr, sizeof ctx->gpr);
    nx8_restore_flags();
    NX8_STAT(3, 1);
    ctx->pc = pc;
}

#ifdef NATIVE_STATS
/* Harness self-test: one wrong bit in the last line a fast cell stored. */
static NX8_NOINLINE void nx8_fault(State* st) {
    if (g_native8038_fault && st->fault_dest != 0xFFFFFFFFu) { st->ls[st->fault_dest + 15] ^= 1u; g_native8038_faults++; }
}
#  define NX8_FAULT_NOTE(st, ua, nu, rngp) do { \
        for (unsigned q_ = (nu); q_ > 1; q_--) if ((ua)[q_ - 1] != (rngp)) { (st)->fault_dest = (ua)[q_ - 1]; break; } } while (0)
#  define NX8_FAULT(st) nx8_fault(st)
#else
#  define NX8_FAULT_NOTE(st, ua, nu, rngp) ((void)0)
#  define NX8_FAULT(st) ((void)0)
#endif

static SPU_SV void nx8_native_sv(spu_context* ctx) {
    /* What the native code does not do: interrupts (the lifted code leaves at the next label when
     * one is pending), the local-store diagnostics, a host float mode other than the SPU one, no
     * rows or columns, a row or column count that is not a small positive number, a tiny grid. */
    const unsigned csr = _mm_getcsr();
    const uint32_t nrows = ctx->gpr[68]._u32[0];
    const uint32_t ncols = ctx->gpr[85]._u16[0];
    if (NX8_UNLIKELY(ctx->int_enable != 0 || g_spu_ls_rdiag != 0 || g_spu_ls_wdiag != 0 ||
                     (csr & SPU_MXCSR_RZFZ) != SPU_MXCSR_RZFZ ||
                     nrows - 1u >= 4096u || ctx->gpr[33]._u16[0] == 0 || ncols - 1u >= NX8_MAXCOLS ||
                     (unsigned long long)nrows * ncols < g_native8038_min_cells)) {
        __attribute__((musttail)) return NX8_ORIG_SV(ctx);
    }
    t_nx8_flags = 0;
    nx8_clear_flags();
    NX8_STAT(0, 1);

    State* const st = &t_nx8_state;
    uint8_t* const ls = ctx->ls;
    const int mode = NX8_MODE();
    st->ls = ls;
    state_init(st);
    memcpy(st->rf.gpr, ctx->gpr, sizeof st->rf.gpr);
    blk_prologue(&st->rf, ls);
    nx8_clear_flags();

    Fast f;
    int fast = 0;
    if (mode == 0) {
        fast = fast_setup(&f, st);
        if (!fast) {
            nx8_deopt(ctx, st, 0x8238, 0, _mm_setzero_si128(), _mm_setzero_si128());
            __attribute__((musttail)) return gt5_w01606C00_spu_func_00008238_sv(ctx);
        }
    }

    const __m128i r62 = nx8_ld(st->rf.gpr[62]), r81 = nx8_ld(st->rf.gpr[81]);
    const uint32_t r63_0 = st->rf.gpr[63]._u32[0];       /* columns: the inner loop ends when r55 == r63 */
    const uint32_t r68_0 = st->rf.gpr[68]._u32[0];       /* rows: the outer loop ends when r80 == r68 */
    const __m128i kzero = _mm_setzero_si128();
    const __m128i kabs = NX8_K(0x7FFFFFFFu);
    const __m128i kabs01 = _mm_setr_epi32(0x7FFFFFFF, 0x7FFFFFFF, 0, 0);
    const __m128i kabs012 = _mm_setr_epi32(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0);
    const __m128i kabs2 = _mm_setr_epi32(0, 0, 0x7FFFFFFF, 0);
    const __m128i km01 = _mm_setr_epi32(-1, -1, 0, 0);
    const __m128 km79 = nx8_splat(0xC0FCCCCCu), kp79 = nx8_splat(0x40FCCCCCu);            /* -7.9, 7.9 */
    const __m128 kmc = nx8_scalar(0xBAF9096Bu), kpc = nx8_scalar(0x3AF9096Bu);            /* -/+ 0.0019: the mass clamp */
    unsigned long long nslow = 0, nbail = 0;
    uint32_t seq = 0, e_row = 0xFFFFFFFFu;
    /* The generator state, kept in a register between cells: rng_raw is the line as local store
     * holds it, rng the same in register order. Every store to that line goes through these. */
    __m128i rng = _mm_setzero_si128(), rng_raw = _mm_setzero_si128();
#define NX8_RNG_RELOAD() do { rng_raw = _mm_loadu_si128((const __m128i*)(ls + f.rngp)); rng = nx8_bswap(rng_raw); } while (0)
    if (fast) NX8_RNG_RELOAD();
    __m128i r53, r51;
    uint32_t k = 0, j = 0;
    Pre* const pre = &st->pre;
    for (;;) {
        /* 0x8238: the row header, on RF (r5 and r80 are in place) */
        st->row = j;
        st->hdr_r5 = st->rf.gpr[5]; st->hdr_r80 = st->rf.gpr[80];
        blk_header(&st->rf, ls, st->hl, MODE_LIVE);
        nx8_clear_flags();
        r53 = nx8_ld(st->rf.gpr[53]); r51 = nx8_ld(st->rf.gpr[51]);
        k = st->rf.gpr[55]._u32[0];
        const __m128i r54row = nx8_ld(st->rf.gpr[54]);
        const __m128i r54_1 = _mm_shuffle_epi32(r54row, 0x55);
        const uint32_t r54_1s = st->rf.gpr[54]._u32[1];
        const uint32_t r66_0 = st->rf.gpr[66]._u32[0];
        const uint32_t r51row_0 = st->rf.gpr[51]._u32[0];
        nx8_r53_fill(st->r53row, r53, r62, r63_0);          /* r53 at every column of the row */
        pre->qhead = pre->qtail = 0; pre->scan = fast ? 0 : r63_0;
        if (fast) nx8_precompute(pre, &f, ls, r66_0, r51row_0, st->r53row, r54_1s, r63_0);
        for (;;) {
            /* 0x8298: one cell */
            CellLog* lg;
            uint32_t ua[2 + 2 * MAXE];
            __m128i uv[2 + 2 * MAXE];
            unsigned nu = 0;
            if (NX8_UNLIKELY(!fast)) {
                if (mode == 0) {                              /* the constants changed under us */
                    st->e_valid = e_row != 0xFFFFFFFFu;
                    NX8_STAT(1, seq - nslow); NX8_STAT(2, nslow); NX8_STAT(4, nbail);
                    nx8_deopt(ctx, st, 0x8298, k, r53, r51);
                    __attribute__((musttail)) return gt5_w01606C00_spu_func_00008298_sv(ctx);
                }
                goto slow;
            }
            {
                /* ---- 0x8298: generator step, the cell's 8 bytes ---- */
                const __m128i sraw = rng_raw;                 /* what LS holds: the state before this cell */
                __m128i ns; uint32_t u;
                NX8_RNG(rng, ns, u);
                const uint32_t cla = ((uint32_t)((int32_t)(int16_t)(k & 0xFFFEu) << 3) + r66_0) & LSM;
                const __m128i craw = _mm_loadu_si128((const __m128i*)(ls + cla));
                const uint8_t* const cp = ls + cla + ((k & 1u) << 3);
                const int32_t h0 = nx8_be16s(cp);             /* what is in the cell (x 2^-24) */
                const int ev = (uint64_t)u < f.ev_below;
                if (!ev && h0 <= 0) {
                    /* empty: blocks 1 and 4 */
                    CellLog* const e = &st->elog;
                    rng = ns; rng_raw = nx8_bswap(ns);
                    _mm_storeu_si128((__m128i*)(ls + f.rngp), rng_raw);
                    if (e_row != j) { e_row = j; e->row = j; e->fast = 1; e->r54 = r54row; }   /* once a row */
                    e->seq = seq; e->k = k;
                    e->r53 = r53; e->r51 = r51; e->ld[0] = sraw; e->ld[1] = craw;
                    goto tail;
                }
                if (NX8_UNLIKELY(ev && !f.ev_ok)) goto slow;

                lg = &st->pool[st->cur];
                lg->seq = seq; lg->row = j; lg->k = k; lg->fast = 1;
                lg->r53 = r53; lg->r51 = r51; lg->r54 = r54row; lg->ld[0] = sraw; lg->ld[1] = craw;
                /* the generator state goes back before anything else is loaded (0x8320; with the event,
                 * a second step and its store at 0x8370: the same line, nothing is loaded in between) */
                __m128i nsf = ns;
                uint32_t u2 = 0;
                if (ev) NX8_RNG(ns, nsf, u2);
                rng = nsf; rng_raw = nx8_bswap(nsf);
                _mm_storeu_si128((__m128i*)(ls + f.rngp), rng_raw);
                ua[0] = f.rngp; uv[0] = sraw; nu = 1;
                const uint32_t r51_0 = (uint32_t)_mm_cvtsi128_si32(r51);
                const __m128i p1raw = _mm_loadu_si128((const __m128i*)(ls + ((r51_0 + 0x10u) & LSM)));
                const __m128i p0raw = _mm_loadu_si128((const __m128i*)(ls + (r51_0 & LSM)));
                lg->ld[2] = p1raw; lg->ld[3] = p0raw;
                __m128 dir2, r60c;
                __m128i acc;
                uint32_t x0, y0, idx, fxw, tbl, mb, bbm;
                int used = 0;
                if (!ev) {
                    /* done ahead of time? (the batch is in column order; skip what was not used) */
                    while (pre->qhead != pre->qtail && pre->k[pre->qhead & (NPRE - 1)] < k) pre->qhead++;
                    const unsigned h = pre->qhead & (NPRE - 1);
                    if (pre->qhead != pre->qtail && pre->k[h] == k) {
                        pre->qhead++;
                        if (pre->valid[h] && _mm_movemask_epi8(_mm_and_si128(_mm_and_si128(
                                _mm_cmpeq_epi8(craw, _mm_loadu_si128(&pre->craw[h])), _mm_cmpeq_epi8(p1raw, _mm_loadu_si128(&pre->p1raw[h]))),
                                _mm_cmpeq_epi8(p0raw, _mm_loadu_si128(&pre->p0raw[h])))) == 0xFFFF) {
                            used = 1; NX8_STAT(5, 1);
                            dir2 = PS(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int)pre->dir2x[h]), _mm_cvtsi32_si128((int)pre->dir2y[h])));
                            r60c = PS(_mm_unpacklo_epi32(_mm_cvtsi32_si128((int)pre->r60cx[h]), _mm_cvtsi32_si128((int)pre->r60cy[h])));
                            x0 = pre->x0[h]; y0 = pre->y0[h]; idx = pre->idx[h]; fxw = pre->fxw[h];
                            tbl = pre->tb[h] ? f.tblB : f.tblA;
                            mb = pre->mb[h];
                            bbm = 0x1u | 0x8u | 0x10u;
                            acc = kzero;
                        }
                    }
                }
                if (!used) {
                    /* one cell at a time: the same work as nx8_precompute, on this cell alone */
                    const __m128i c8 = (k & 1u) ? _mm_unpackhi_epi64(craw, craw) : craw;   /* not from LS again: the generator's line may be this one */
                    /* velocity (bytes 2-3, 6-7: s16 x 2^-12), sub-cell position (bytes 4, 5: u8 x 2^-8) */
                    __m128 v = _mm_mul_ps(_mm_cvtepi32_ps(_mm_srai_epi32(_mm_shuffle_epi8(c8,
                                   _mm_setr_epi8(-1, -1, 3, 2, -1, -1, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1)), 16)), nx8_splat(0x39800000u));
                    __m128 bf = _mm_mul_ps(_mm_cvtepi32_ps(_mm_shuffle_epi8(c8,
                                   _mm_setr_epi8(4, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1))), nx8_splat(0x3B800000u));
                    const __m128 h0f = _mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(), h0), nx8_scalar(0x33800000u));   /* r9 */
                    __m128 vv = _mm_mul_ps(v, v);                                                   /* r47 */
                    uint32_t len2b = nx8_bits(_mm_add_ss(vv, _mm_shuffle_ps(vv, vv, 0x55)));        /* r42 */
                    tbl = (int32_t)len2b > f.key78 ? f.tblB : f.tblA;                               /* r15 */
                    bbm = 0x1u | 0x10u;
                    if (ev) {
                        /* ---- 0x8334: the event. A second step, new mass, velocity rescaled ---- */
                        const __m128 r4 = nx8_flushf(_mm_fmadd_ss(nx8_scalar(f.a30), nx8_cuflt123(u2), nx8_scalar(f.a20)));
                        mb = nx8_bits(nx8_flushf(_mm_add_ss(h0f, r4)));                             /* r22 */
                        if (NX8_UNLIKELY((mb & 0x7F800000u) == 0)) goto bail;                       /* 1/0: the SPU's large number */
                        const __m128 r11 = nx8_flushf(_mm_mul_ss(h0f, nx8_scalar(nx8_fi(mb, nx8_frest(mb)))));
                        v = nx8_flushf(_mm_mul_ps(v, _mm_shuffle_ps(r11, r11, 0)));                 /* r13 */
                        bbm |= 0x2u;
                        if (h0 <= 0) { bf = PS(_mm_setr_epi32(0x3F000000, 0x3F000000, 0, 0)); bbm |= 0x4u; }
                        vv = _mm_mul_ps(v, v);
                        len2b = nx8_bits(_mm_add_ss(vv, _mm_shuffle_ps(vv, vv, 0x55)));
                    } else {
                        mb = nx8_bits(h0f);
                        bbm |= 0x8u;
                    }
                    const __m128i P1 = _mm_and_si128(nx8_bswap(p1raw), km01), P0 = _mm_and_si128(nx8_bswap(p0raw), km01);
                    acc = _mm_max_epu32(_mm_and_si128(P1, kabs), _mm_and_si128(P0, kabs));
                    acc = _mm_max_epu32(acc, _mm_and_si128(r53, kabs01));
                    const uint32_t isq = nx8_fi(mb, nx8_frsqest(mb));                               /* r29: 1/sqrt(mass) */
                    const uint32_t sq = nx8_fi(isq, nx8_frest(isq));                                /* r28: sqrt(mass) */
                    const __m128 d = _mm_sub_ps(PS(P1), v);                                         /* r50 */
                    const __m128 dd = _mm_mul_ps(d, d);                                             /* r18 */
                    const uint32_t d2b = nx8_bits(_mm_add_ss(dd, _mm_shuffle_ps(dd, dd, 0x55)));    /* r17 */
                    const __m128 fric = len2b < 0x38D1B717u ? f.a40 : f.a50;                        /* r28 */
                    const __m128 af = _mm_mul_ps(f.ac0, nx8_splat(sq));                             /* r27 */
                    const uint32_t d2s = d2b ? d2b : 0x3F800000u;                                   /* r25 */
                    const __m128 g = _mm_mul_ps(nx8_splat(d2s), af);                                /* r24 */
                    const __m128 n = _mm_mul_ps(d, nx8_splat(nx8_fi(d2s, nx8_frsqest(d2s))));       /* r49 */
                    const __m128 p = _mm_add_ps(_mm_fmadd_ps(n, g, v), PS(P0));                     /* r47, r5 */
                    const __m128 pp = _mm_mul_ps(p, p);                                             /* r41 */
                    const uint32_t l2b = nx8_bits(_mm_add_ss(pp, _mm_shuffle_ps(pp, pp, 0x55)));    /* r3 */
                    const uint32_t l2s = l2b ? l2b : 0x3F800000u;                                   /* r21 */
                    const uint32_t il2 = nx8_fi(l2s, nx8_frsqest(l2s));                             /* r19 */
                    const __m128 dir = nx8_flushf(_mm_mul_ps(p, nx8_splat(il2)));                   /* r24 */
                    const uint32_t len = nx8_fi(il2, nx8_frest(il2));                               /* r18 (= r31: never negative) */
                    const __m128 spd = _mm_max_ps(_mm_fnmadd_ps(fric, nx8_splat(isq), nx8_splat(len)), _mm_setzero_ps());   /* r26, r25 */
                    const __m128 mv = nx8_flushf(_mm_mul_ps(dir, spd));                             /* r60 */
                    dir2 = len ? mv : dir;                                                          /* r24 */
                    const __m128 t2 = _mm_min_ps(_mm_max_ps(_mm_mul_ps(dir2, f.dt), f.r61), f.r58); /* r48, r42, r41 */
                    const __m128 q = _mm_fmadd_ps(bf, f.r64, _mm_add_ps(PS(_mm_and_si128(r53, km01)), t2));   /* r40, r12 */
                    const __m128 gq = _mm_mul_ps(q, f.r74);                                         /* r3 */
                    const __m128i gi = _mm_cvttps_epi32(gq);                                        /* r39 */
                    const __m128 fr2 = _mm_add_ps(_mm_sub_ps(gq, _mm_cvtepi32_ps(gi)), nx8_splat(0x3C000000u));   /* r36, r21 */
                    const __m128i fi8 = _mm_cvttps_epi32(_mm_mul_ps(fr2, nx8_splat(0x43800000u)));  /* r8 */
                    const __m128i xy = _mm_add_epi32(gi, f.r67);                                    /* r42 */
                    const __m128i rel = _mm_min_epi32(_mm_max_epi32(_mm_add_epi32(_mm_sub_epi32(xy,
                                            _mm_unpacklo_epi32(_mm_cvtsi32_si128((int)k), r54_1)), _mm_setr_epi32(4, 4, 0, 0)), kzero),
                                            _mm_set1_epi32(7));                                     /* r37, r7, r11, r20 */
                    const __m128i ff = _mm_min_epi32(_mm_max_epi32(fi8, kzero), _mm_set1_epi32(255));   /* r27, r33 */
                    const uint64_t relq = (uint64_t)_mm_cvtsi128_si64(rel), xyq = (uint64_t)_mm_cvtsi128_si64(xy);
                    const uint64_t ffq = (uint64_t)_mm_cvtsi128_si64(ff);
                    idx = (uint32_t)relq + ((uint32_t)(relq >> 32) << 3);
                    x0 = (uint32_t)xyq; y0 = (uint32_t)(xyq >> 32);
                    fxw = ((uint32_t)ffq << 24) | ((uint32_t)(ffq >> 32) << 16);                    /* word 3 of what is stored */
                    r60c = _mm_min_ps(_mm_max_ps(dir2, km79), kp79);                                /* r60 */
                }
                const uint32_t cellp = tbl + (idx << 6);                                            /* r41 */
                const uint32_t bptr = f.tblC + 48u * idx;                                           /* r23 */
                const __m128i hraw = _mm_loadu_si128((const __m128i*)(ls + (cellp & LSM)));
                lg->ld[4] = hraw;
                const int32_t count = nx8_be16s(ls + (cellp & LSM) + 10);                           /* r37 */
                if (count > 0) {
                    /* ---- 0x8598 / 0x860C: the stencil cell's entries ---- */
                    if (NX8_UNLIKELY(count > MAXE)) goto bail;
                    bbm |= 0x20u | 0x40u | 0x8000u;
                    __m128 m2 = nx8_scalar(mb);                                                 /* r22, word 2 */
                    unsigned li = 5;
                    for (int32_t i = 0; i < count; i++) {
                        const uint32_t ea = (cellp + ((uint32_t)i << 4)) & LSM;
                        lg->ld[li++] = _mm_loadu_si128((const __m128i*)(ls + ea));
                        const uint32_t x = (nx8_be32(ls + ea) + x0) & f.m44_0, y = (nx8_be32(ls + ea + 4) + y0) & f.m44_1;
                        const uint32_t type = nx8_be32(ls + ea + 12);
                        const int oob = ((int32_t)x < 0) | ((int32_t)y < 0) | ((int32_t)x > f.xmax) | ((int32_t)y > f.ymax);
                        if (type == 1) {
                            /* ---- 0x8800: leave a share of the mass in the cell passed over ---- */
                            bbm |= 0x200u | 0x400u;
                            const __m128 t = _mm_min_ss(_mm_max_ss(_mm_mul_ss(m2, nx8_scalar(f.aa0)), _mm_setzero_ps()),
                                                        nx8_scalar(f.ab0));                     /* r13, r14, r3 */
                            const __m128 m2n = nx8_flushf(_mm_sub_ss(m2, t));                   /* r9 */
                            if (oob) { m2 = m2n; continue; }
                            bbm |= 0x800u;
                            const __m128 w = _mm_min_ss(_mm_max_ss(nx8_flushf(_mm_sub_ss(t, nx8_scalar(f.a60))), kmc), kpc);   /* r14, r16, r2 */
                            const uint32_t ra = f.tbase + (y << 2) + 0x300u;
                            lg->ld[li++] = _mm_loadu_si128((const __m128i*)(ls + (ra & LSM)));
                            const uint32_t dest = ((x << 4) + nx8_be32(ls + (ra & (SPU_LS_MASK & ~3u)))) & LSM;
                            const __m128i oraw = _mm_loadu_si128((const __m128i*)(ls + dest));
                            lg->ld[li++] = oraw;
                            if (NX8_UNLIKELY(dest - 0x8E60u < 0x30u)) goto bail;
                            _mm_storeu_si128((__m128i*)(ls + dest), nx8_bswap(_mm_slli_si128(
                                _mm_cvtsi64_si128((long long)(((uint64_t)fxw << 32) | nx8_bits(w))), 8)));
                            ua[nu] = dest; uv[nu++] = oraw;
                            if (NX8_UNLIKELY(dest == f.rngp)) NX8_RNG_RELOAD();
                            const __m128i oreg = nx8_bswap(oraw);
                            acc = _mm_max_epu32(acc, _mm_and_si128(oreg, kabs2));
                            m2 = nx8_flushf(_mm_add_ss(m2n, PS(_mm_shuffle_epi32(oreg, 0xAA))));   /* r22 */
                        } else if (type != 0) {
                            /* ---- 0x88A0: claim an empty cell ---- */
                            bbm |= 0x200u | 0x1000u;
                            if (oob) continue;
                            bbm |= 0x2000u;
                            const uint32_t ra = f.tbase + (y << 2) + 0x300u;
                            lg->ld[li++] = _mm_loadu_si128((const __m128i*)(ls + (ra & LSM)));
                            const uint32_t dest = ((x << 4) + nx8_be32(ls + (ra & (SPU_LS_MASK & ~3u)))) & LSM;
                            const __m128i oraw = _mm_loadu_si128((const __m128i*)(ls + dest));
                            lg->ld[li++] = oraw;
                            if ((int32_t)nx8_be32(ls + dest + 8) >= 0x00800000) continue;       /* r21: mass there already */
                            bbm |= 0x4000u;
                            const __m128 w = _mm_min_ss(_mm_max_ss(nx8_flushf(_mm_sub_ss(m2, nx8_scalar(f.a60))), kmc), kpc);   /* r19, r12, r15 */
                            if (NX8_UNLIKELY(dest - 0x8E60u < 0x30u)) goto bail;
                            _mm_storeu_si128((__m128i*)(ls + dest), nx8_bswap(_mm_unpacklo_epi64(SI(r60c),
                                _mm_cvtsi64_si128((long long)(((uint64_t)fxw << 32) | nx8_bits(w))))));
                            ua[nu] = dest; uv[nu++] = oraw;
                            if (NX8_UNLIKELY(dest == f.rngp)) NX8_RNG_RELOAD();
                        } else {
                            /* ---- 0x8628: merge into one of four neighbours, picked at random by their mass ---- */
                            bbm |= 0x80u;
                            const __m128i b1raw = _mm_loadu_si128((const __m128i*)(ls + ((bptr + 0x10u) & LSM)));
                            const __m128i b0raw = _mm_loadu_si128((const __m128i*)(ls + (bptr & LSM)));
                            const __m128i s2raw = rng_raw;
                            const __m128i b2raw = _mm_loadu_si128((const __m128i*)(ls + ((bptr + 0x20u) & LSM)));
                            lg->ld[li] = b1raw; lg->ld[li + 1] = b0raw; lg->ld[li + 2] = s2raw; lg->ld[li + 3] = b2raw;
                            uint32_t xa[4], ya[4], rp[4];
                            __m128i cr[4];
                            _mm_storeu_si128((__m128i*)ya, _mm_add_epi32(nx8_bswap(b1raw), _mm_set1_epi32((int)y)));   /* r13 */
                            _mm_storeu_si128((__m128i*)xa, _mm_add_epi32(nx8_bswap(b0raw), _mm_set1_epi32((int)x)));   /* r12 */
                            for (int c = 0; c < 4; c++) {
                                const uint32_t ra = f.tbase + (ya[c] << 2) + 0x300u;
                                lg->ld[li + 4 + c] = _mm_loadu_si128((const __m128i*)(ls + (ra & LSM)));
                                rp[c] = nx8_be32(ls + (ra & (SPU_LS_MASK & ~3u)));
                            }
                            for (int c = 0; c < 4; c++) {
                                cr[c] = _mm_loadu_si128((const __m128i*)(ls + (((xa[c] << 4) + rp[c]) & LSM)));
                                lg->ld[li + 8 + c] = cr[c];
                            }
                            li += 12;
                            __m128i ns3; uint32_t u3;
                            NX8_RNG(rng, ns3, u3);
                            rng = ns3; rng_raw = nx8_bswap(ns3);
                            _mm_storeu_si128((__m128i*)(ls + f.rngp), rng_raw);
                            ua[nu] = f.rngp; uv[nu++] = s2raw;
                            /* the four masses (word 2 of each) */
                            const __m128i ms4 = nx8_bswap(_mm_unpacklo_epi64(_mm_unpackhi_epi32(cr[0], cr[1]), _mm_unpackhi_epi32(cr[2], cr[3])));
                            const __m128i b2 = nx8_bswap(b2raw);
                            acc = _mm_max_epu32(acc, _mm_max_epu32(_mm_and_si128(ms4, kabs), _mm_and_si128(b2, kabs)));
                            const __m128 msf = PS(ms4);
                            __m128 s = _mm_add_ss(msf, _mm_shuffle_ps(msf, msf, 0x55));                   /* r16, r2 */
                            s = _mm_add_ss(s, _mm_shuffle_ps(msf, msf, 0xAA));                            /* r18 */
                            s = _mm_add_ss(s, _mm_shuffle_ps(msf, msf, 0xFF));                            /* r4 */
                            const __m128 thr = _mm_mul_ss(_mm_add_ss(s, nx8_scalar(0x360637BDu)), nx8_cuflt123(u3));   /* r11, r14 */
                            const __m128 dv = _mm_sub_ps(_mm_add_ps(PS(b2), msf), _mm_shuffle_ps(thr, thr, 0));         /* r15, r8 */
                            const unsigned mm = (unsigned)_mm_movemask_ps(_mm_cmplt_ps(dv, _mm_setzero_ps()));          /* r9, r7 */
                            const unsigned sel = (unsigned)__builtin_ctz(~mm | 8u);             /* the table at 0x8E70 */
                            const uint32_t xs = xa[sel] & f.m44_0, ys = ya[sel] & f.m44_1;      /* r5 */
                            if (((int32_t)xs < 0) | ((int32_t)ys < 0) | ((int32_t)xs > f.xmax) | ((int32_t)ys > f.ymax)) continue;
                            bbm |= 0x100u;
                            const uint32_t ra = f.tbase + (ys << 2) + 0x300u;
                            lg->ld[li++] = _mm_loadu_si128((const __m128i*)(ls + (ra & LSM)));
                            const uint32_t dest = ((xs << 4) + nx8_be32(ls + (ra & (SPU_LS_MASK & ~3u)))) & LSM;
                            const __m128i oraw = _mm_loadu_si128((const __m128i*)(ls + dest));
                            lg->ld[li++] = oraw;
                            const __m128i oreg = nx8_bswap(oraw);
                            acc = _mm_max_epu32(acc, _mm_and_si128(oreg, kabs012));
                            const __m128 om = PS(_mm_shuffle_epi32(oreg, 0xAA));                /* r5: the mass there, every lane */
                            const __m128 mom = _mm_fmadd_ps(om, PS(_mm_and_si128(oreg, km01)),
                                                            _mm_mul_ps(dir2, _mm_shuffle_ps(m2, m2, 0)));   /* r6, r3 */
                            const __m128 mc = _mm_min_ss(_mm_max_ss(nx8_flushf(_mm_add_ss(m2, om)), kmc), kpc);   /* r19, r15, r4 */
                            const uint32_t mcb = nx8_bits(mc);
                            if (NX8_UNLIKELY((mcb & 0x7F800000u) == 0 || dest - 0x8E60u < 0x30u)) goto bail;
                            const __m128 vc = _mm_min_ps(_mm_max_ps(nx8_flushf(_mm_mul_ps(mom,
                                                  nx8_splat(nx8_fi(mcb, nx8_frest(mcb))))), km79), kp79);   /* r17, r9, r11, r6 */
                            _mm_storeu_si128((__m128i*)(ls + dest), nx8_bswap(_mm_unpacklo_epi64(SI(vc),
                                _mm_cvtsi64_si128((long long)(((uint64_t)fxw << 32) | mcb)))));
                            ua[nu] = dest; uv[nu++] = oraw;
                            if (NX8_UNLIKELY(dest == f.rngp)) NX8_RNG_RELOAD();
                        }
                    }
                }
                if (NX8_UNLIKELY(_mm_movemask_ps(PS(_mm_cmpgt_epi32(acc, NX8_K(0x7F7FFFFFu)))) | (_mm_getcsr() & NX8_FLAGS))) goto bail;
                state_retain(st, bbm);
                NX8_FAULT_NOTE(st, ua, nu, f.rngp);
                NX8_COUNT_BLOCKS(bbm);
                goto tail;
            }
bail:
            /* put back what the cell stored, newest first, and run it as lifted statements */
            while (nu) { nu--; _mm_storeu_si128((__m128i*)(ls + ua[nu]), uv[nu]); }
            nbail++;
slow:
            lg = &st->pool[st->cur];
            lg->seq = seq; lg->row = j; lg->k = k; lg->fast = 0;
            lg->r53 = r53; lg->r51 = r51; lg->r54 = r54row;
            {
                uint32_t bb;
                if (mode == 2) {
                    RF tmp = st->rf;
                    tmp.gpr[55] = nx8_st(_mm_set1_epi32((int)k)); tmp.gpr[53] = nx8_st(r53); tmp.gpr[51] = nx8_st(r51);
                    tmp.gpr[54] = nx8_st(r54row);
                    bb = blk_cell(&tmp, ls, lg, MODE_LIVE);
                } else {
                    st->rf.gpr[55] = nx8_st(_mm_set1_epi32((int)k)); st->rf.gpr[53] = nx8_st(r53); st->rf.gpr[51] = nx8_st(r51);
                    st->rf.gpr[54] = nx8_st(r54row);
                    bb = blk_cell(&st->rf, ls, lg, MODE_LIVE);
                }
                nx8_clear_flags();
                nslow++;
                if (NX8_UNLIKELY(bb == 0)) {                  /* a stencil cell with more than MAXE entries */
                    st->e_valid = e_row != 0xFFFFFFFFu;
                    NX8_STAT(1, seq - nslow); NX8_STAT(2, nslow); NX8_STAT(4, nbail);
                    nx8_deopt(ctx, st, 0x8298, k, r53, r51);
                    __attribute__((musttail)) return gt5_w01606C00_spu_func_00008298_sv(ctx);
                }
                state_retain(st, bb);
                if (fast && memcmp(ls + 0x8E60, k_const_8E60, 48) != 0) fast = 0;
                if (fast) NX8_RNG_RELOAD();
            }
tail:
            /* 0x891C */
            seq++;
            if (k + 1u == r63_0) break;
            /* keep the look-ahead a few columns in front (its long chain runs beside the cells in between) */
            if (k + 1u + NX8_LOOK > pre->scan && pre->scan < r63_0) {
                while (pre->qhead != pre->qtail && pre->k[pre->qhead & (NPRE - 1)] <= k) pre->qhead++;
                if (pre->qtail - pre->qhead <= 8u) nx8_precompute(pre, &f, ls, r66_0, r51row_0, st->r53row, r54_1s, r63_0);
            }
            k++;
            r53 = st->r53row[k];
            r51 = _mm_add_epi32(r51, _mm_set1_epi32(32));
        }
        /* 0x8934 */
        if (j + 1u == r68_0) break;
        j++;
        st->rf.gpr[5] = nx8_st(nx8_fa(nx8_fa(r53, r62), r81));
        st->rf.gpr[80] = nx8_st(_mm_set1_epi32((int)j));
    }
    /* The end: registers as the last runs left them, then 0x891C / 0x8934 / 0x8948 as lifted. */
    st->e_valid = e_row != 0xFFFFFFFFu;
    state_materialise(st);
    st->rf.gpr[55] = nx8_st(_mm_set1_epi32((int)k)); st->rf.gpr[53] = nx8_st(r53); st->rf.gpr[51] = nx8_st(r51);
    blk_exit(&st->rf, ls);
    memcpy(ctx->gpr, st->rf.gpr, sizeof ctx->gpr);
    nx8_restore_flags();
    NX8_STAT(1, seq - nslow); NX8_STAT(2, nslow); NX8_STAT(4, nbail);
    NX8_FAULT(st);
    SPU_RET(ctx);
#undef NX8_RNG_RELOAD
}

/* ---- GT5_NATIVE_GEO: lifted / native / run both and compare ----------------------------------- */
/*
 * Compare (mode 2): keep the entry state (registers, pc, the whole local store: the function stores
 * through pointers it takes from its tables, so anywhere); run the native code to the function's
 * end and keep its result; put the entry state back; run the lifted function to its end; compare
 * everything; the lifted result stays. Both runs end with the function's own return (SPU_RET).
 * After a deopt, or when the lifted code may not chain directly, the function goes on as the lifted
 * pieces of the function (for 0x8038: 0x8120 .. 0x8948), which may hand over through the trampoline: nx8_finish runs
 * those until the function has returned. Slow (three copies of local store per call): debugging.
 */
static SPU_THREAD_LOCAL uint8_t* t_nx8_cmp;      /* compare mode only: two local stores, two register files */

static void nx8_finish(spu_context* ctx, uint32_t pc_lo, uint32_t pc_hi) {
    for (;;) {
        void (* const f)(spu_context*) = g_spu_trampoline_fn;
        if (!f || f == spu_indirect_branch) return;
        if ((ctx->pc & SPU_LS_MASK) - pc_lo >= pc_hi - pc_lo) return;             /* not a piece of this function */
        g_spu_trampoline_fn = 0;
        f(ctx);
    }
}

/* Returns 0 when it did nothing (the caller then runs the lifted function as usual). */
static NX8_NOINLINE int ngeo_compare_whole(spu_context* ctx, unsigned addr, SPU_SV void (*native)(spu_context*),
                                           SPU_SV void (*orig)(spu_context*), uint32_t pc_lo, uint32_t pc_hi) {
    if (ctx->int_enable != 0 || g_spu_ls_rdiag != 0 || g_spu_ls_wdiag != 0) return 0;
    uint8_t* buf = t_nx8_cmp;
    if (!buf) {
        buf = (uint8_t*)malloc(2u * SPU_LS_SIZE + 2u * sizeof ctx->gpr);
        if (!buf) return 0;
        t_nx8_cmp = buf;
    }
    uint8_t* const ls0 = buf, * const ls1 = ls0 + SPU_LS_SIZE, * const g0 = ls1 + SPU_LS_SIZE, * const g1 = g0 + sizeof ctx->gpr;
    const uint32_t pc0 = ctx->pc;
    memcpy(ls0, ctx->ls, SPU_LS_SIZE); memcpy(g0, ctx->gpr, sizeof ctx->gpr);
    g_spu_trampoline_fn = 0;
    native(ctx); nx8_finish(ctx, pc_lo, pc_hi);                      /* native */
    void (* const succ1)(spu_context*) = g_spu_trampoline_fn;
    const uint32_t pc1 = ctx->pc;
    memcpy(g1, ctx->gpr, sizeof ctx->gpr); memcpy(ls1, ctx->ls, SPU_LS_SIZE);
    memcpy(ctx->ls, ls0, SPU_LS_SIZE); memcpy(ctx->gpr, g0, sizeof ctx->gpr); ctx->pc = pc0;   /* back to the entry state */
    g_spu_trampoline_fn = 0;
    orig(ctx); nx8_finish(ctx, pc_lo, pc_hi);                        /* lifted: the result that stays */
    native_geo_compared();
    if (memcmp(g1, ctx->gpr, sizeof ctx->gpr) != 0 || memcmp(ls1, ctx->ls, SPU_LS_SIZE) != 0 || pc1 != ctx->pc ||
        succ1 != g_spu_trampoline_fn) {
        int reg = -1; uint32_t off = 0xFFFFFFFFu;
        char d[160];
        for (int i = 0; i < 128 && reg < 0; i++) if (memcmp(g1 + 16 * i, &ctx->gpr[i], 16) != 0) reg = i;
        for (uint32_t i = 0; i < SPU_LS_SIZE; i++) if (ls1[i] != ctx->ls[i]) { off = i; break; }
        snprintf(d, sizeof d, "first register r%d, first LS byte 0x%05X, pc %05X/%05X (native/lifted)", reg, off, pc1, ctx->pc);
        native_geo_mismatch("w01606C00", addr, d);
    }
    return 1;
}

SPU_SV void NX8_NATIVE_SV(spu_context* ctx) {
    const int m = native_geo_mode();
    if (NX8_LIKELY(m == 1)) { __attribute__((musttail)) return nx8_native_sv(ctx); }
    if (m == 2 && ngeo_compare_whole(ctx, 0x8038, nx8_native_sv, NX8_ORIG_SV, 0x8038u, 0x8980u)) return;
    __attribute__((musttail)) return NX8_ORIG_SV(ctx);
}

#ifndef NATIVE_HOOKED
/* Thunk with the lifted functions' public signature and ABI (the registered / trampoline form). */
void gt5_w01606C00_native_00008038(spu_context* ctx) { SPU_SV_CALL gt5_w01606C00_native_00008038_sv(ctx); }
#endif

/* =============================================================================================
 * 0x5BA8: the fill loop of the image's memset (reached through 0x5BA4, which the profile names:
 * 0x5BA4 is one add, r3 = r6 + r85, and the jump here; the compiler inlines this loop into it).
 *
 *     loop:  LS line at r4 = r5;  r4 += 16 (every word);  r82 = (r3 == r4);  until r4 == r3 (word 0)
 *     then 0x5C50
 *
 * The method of the sound mixer's natives: the fast block does all passes but the last and leaves
 * what they leave -- the lines stored and the loop-carried register r4 -- then the ORIGINAL lifted
 * function does the last pass (which sets r82), writes the registers back and hands over. Entered
 * at a pass boundary the lifted function does nothing but load r3, r4, r5 and r82.
 * No floats: the stored line is r5 as it is.
 * Left to the lifted function alone: a count that is not a whole number of lines or is zero (the
 * lifted loop then runs on through the 32-bit counter, which is its business), fewer than two
 * passes, more than a whole local store, a local-store diagnostic armed.
 * ============================================================================================= */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w01606C00_spu_func_00005BA8_orig_sv(spu_context* ctx);
#  define NFILL_ORIG_SV   gt5_w01606C00_spu_func_00005BA8_orig_sv
#  define NFILL_NATIVE_SV gt5_w01606C00_spu_func_00005BA8_sv
#else
#  define NFILL_ORIG_SV   gt5_w01606C00_spu_func_00005BA8_sv
#  define NFILL_NATIVE_SV gt5_w01606C00_native_00005BA8_sv
#endif

#ifdef NATIVE_STATS
/* [0] calls, [1] calls the fast block advanced, [2] lines it stored */
unsigned long long g_native5BA8_stat[3];
int g_native5BA8_fault;              /* harness self-test: one bit of the fast block's last line flipped */
#  define NFILL_STAT(i, n) (g_native5BA8_stat[i] += (n))
#else
#  define NFILL_STAT(i, n) ((void)0)
#endif

static inline __attribute__((always_inline)) void nfill_lines(uint8_t* p, uint32_t n, __m128i v) {
    const __m256i v2 = _mm256_broadcastsi128_si256(v);
    uint32_t i = 0;
    for (; i + 4 <= n; i += 4) {
        _mm256_storeu_si256((__m256i*)(p + 16u * i), v2);
        _mm256_storeu_si256((__m256i*)(p + 16u * i + 32u), v2);
    }
    for (; i < n; i++) _mm_storeu_si128((__m128i*)(p + 16u * i), v);
}

/* Returns the passes done (0: nothing touched). */
static inline __attribute__((always_inline)) uint32_t nfill_fast(spu_context* ctx) {
    u128* const g = ctx->gpr;
    const uint32_t a0 = g[4]._u32[0], span = g[3]._u32[0] - a0;
    if (NX8_UNLIKELY((span & 15u) != 0 || span < 32u || span > SPU_LS_SIZE)) return 0;
    const uint32_t n = span / 16u - 1u;                              /* at least one pass is the lifted function's */
    const uint32_t a = a0 & LSM;
    const uint32_t first = (SPU_LS_SIZE - a) / 16u < n ? (SPU_LS_SIZE - a) / 16u : n;   /* lines before the store wraps */
    const __m128i v = nx8_bswap(nx8_ld(g[5]));
    nfill_lines(ctx->ls + a, first, v);
    nfill_lines(ctx->ls, n - first, v);
    g[4] = spu_ai(g[4], (int32_t)(16u * n));
    NFILL_STAT(1, 1); NFILL_STAT(2, n);
#ifdef NATIVE_STATS
    if (g_native5BA8_fault) ctx->ls[((a0 + 16u * (n - 1u)) & LSM) + 15u] ^= 1u;
#endif
    return n;
}
static NX8_NOINLINE void nfill_fast_fn(spu_context* ctx) { (void)nfill_fast(ctx); }

/* Compare (mode 2) for a function whose stores are the lines it is handed: keep the registers and
 * those lines; run fast block + lifted remainder and keep the result; put the entry state back;
 * run the lifted function alone; compare registers, lines, pc and the successor; the lifted result
 * stays. Both runs are made to return in place of tail-calling the successor by clearing
 * ctx->steps for their duration (SPU_DIRECT_SV then leaves the successor pending in
 * g_spu_trampoline_fn and returns): the drain loop this call sits under carries on from there. */
typedef struct ngeo_range { uint32_t a, n; } ngeo_range;            /* LS bytes [a, a + n): no wrap */
static void ngeo_copy_out(uint8_t* dst, const uint8_t* ls, const ngeo_range* r, int nr) {
    for (int i = 0; i < nr; dst += r[i].n, i++) memcpy(dst, ls + r[i].a, r[i].n);
}
static NX8_NOINLINE int ngeo_compare_ranges(spu_context* ctx, unsigned addr, void (*fast)(spu_context*),
                                            SPU_SV void (*orig)(spu_context*), const ngeo_range* r, int nr) {
    uint32_t bytes = 0;
    for (int i = 0; i < nr; i++) bytes += r[i].n;
    if (bytes > SPU_LS_SIZE || g_spu_ls_rdiag != 0 || g_spu_ls_wdiag != 0) return 0;
    uint8_t* buf = t_nx8_cmp;
    if (!buf) {
        buf = (uint8_t*)malloc(2u * SPU_LS_SIZE + 2u * sizeof ctx->gpr);
        if (!buf) return 0;
        t_nx8_cmp = buf;
    }
    uint8_t* const ls0 = buf, * const ls1 = ls0 + SPU_LS_SIZE, * const g0 = ls1 + SPU_LS_SIZE, * const g1 = g0 + sizeof ctx->gpr;
    const uint32_t steps = ctx->steps, pc0 = ctx->pc;
    ngeo_copy_out(ls0, ctx->ls, r, nr); memcpy(g0, ctx->gpr, sizeof ctx->gpr);
    ctx->steps = 0;
    g_spu_trampoline_fn = 0;
    fast(ctx); orig(ctx);                                            /* native: fast block + lifted remainder */
    void (* const succ1)(spu_context*) = g_spu_trampoline_fn;
    const uint32_t pc1 = ctx->pc;
    memcpy(g1, ctx->gpr, sizeof ctx->gpr); ngeo_copy_out(ls1, ctx->ls, r, nr);
    memcpy(ctx->gpr, g0, sizeof ctx->gpr);                           /* back to the entry state */
    { const uint8_t* s = ls0; for (int i = 0; i < nr; s += r[i].n, i++) memcpy(ctx->ls + r[i].a, s, r[i].n); }
    ctx->pc = pc0;
    g_spu_trampoline_fn = 0;
    orig(ctx);                                                       /* lifted alone: the result that stays */
    ctx->steps = steps;
    ngeo_copy_out(ls0, ctx->ls, r, nr);
    native_geo_compared();
    if (memcmp(g1, ctx->gpr, sizeof ctx->gpr) != 0 || memcmp(ls0, ls1, bytes) != 0 || pc1 != ctx->pc || succ1 != g_spu_trampoline_fn) {
        int reg = -1; uint32_t off = 0xFFFFFFFFu;
        char d[160];
        for (int i = 0; i < 128 && reg < 0; i++) if (memcmp(g1 + 16 * i, &ctx->gpr[i], 16) != 0) reg = i;
        for (uint32_t i = 0, o = 0; i < (uint32_t)nr && off == 0xFFFFFFFFu; o += r[i].n, i++)
            for (uint32_t b = 0; b < r[i].n; b++) if (ls0[o + b] != ls1[o + b]) { off = r[i].a + b; break; }
        snprintf(d, sizeof d, "first register r%d, first LS byte 0x%05X, pc %05X/%05X (native/lifted)", reg, off, pc1, ctx->pc);
        native_geo_mismatch("w01606C00", addr, d);
    }
    return 1;
}

static NX8_NOINLINE int nfill_compare(spu_context* ctx) {
    const uint32_t a0 = ctx->gpr[4]._u32[0], span = ctx->gpr[3]._u32[0] - a0;
    if ((span & 15u) != 0 || span == 0 || span > SPU_LS_SIZE) return 0;     /* not a fill the fast block takes */
    const uint32_t a = a0 & LSM, first = SPU_LS_SIZE - a < span ? SPU_LS_SIZE - a : span;
    ngeo_range r[2] = { { a, first }, { 0, span - first } };
    return ngeo_compare_ranges(ctx, 0x5BA8, nfill_fast_fn, NFILL_ORIG_SV, r, span - first ? 2 : 1);
}

SPU_SV void NFILL_NATIVE_SV(spu_context* ctx) {
    const int m = native_geo_mode();
    NFILL_STAT(0, 1);
    if (NX8_LIKELY(m == 1)) { if (NX8_LIKELY((g_spu_ls_rdiag | g_spu_ls_wdiag) == 0)) (void)nfill_fast(ctx); }
    else if (m == 2 && nfill_compare(ctx)) return;
    __attribute__((musttail)) return NFILL_ORIG_SV(ctx);
}

#ifndef NATIVE_HOOKED
void gt5_w01606C00_native_00005BA8(spu_context* ctx) { SPU_SV_CALL gt5_w01606C00_native_00005BA8_sv(ctx); }
#endif

/* =============================================================================================
 * 0x7DC8: pack the grid. For every row: a header (0x7E58: a per-row sum and the column count r17
 * below which cells are scaled), then, two cells a pass (0x7F10):
 *
 *     c0, c1 = the 16-byte cells at r13, r13+16            (x, y, mass, two fraction bytes)
 *     scale  = table(r15 > r17 lane by lane) / table(r15 + 1 > r17)     (the constant 0xCDFE)
 *     c      = scale ? c * r22 : c                           (all four words)
 *     LS[r20] = (LS[r20] + c0) + c1                          (a running sum of every cell)
 *     out    = two 8-byte cells: mass * 2^24, x * 2^12, the fraction bytes, y * 2^12 (low 16 bits
 *              of the saturating conversions)  ->  LS[(index << 3) + r25]
 *     r13 += 32, r15 += 2, r16 += 2;  while r21 > r16 + 2
 *
 * The native function IS the lifted function (blk_7DC8.inc: its body, verbatim, every statement,
 * interrupt check and exit) with one addition at the head of the inner loop: NX7_FAST_FORWARD runs
 * all passes of the row but the last and leaves what they leave -- the lines stored and the
 * loop-carried r13, r15, r16 -- and the lifted statements then do the last pass, which assigns
 * every other register of the pass. So the registers on return are the lifted code's own.
 *
 * Floats in the fast passes: the multiply is the host operation when every lane is in range (then
 * flushed), else the lifted helper itself (spu_fm); the sum: see nx7_add3; the conversions are
 * cvttps2dq, redone with spu_cflts when a lane may have saturated. No pass is given up for a float.
 * The fast-forward is not taken (the lifted pass runs instead) when: interrupts are enabled, a
 * local-store diagnostic is armed, the float mode is not the SPU's, the registers that hold constants (r14, r19, r23, r24) are not
 * the image's, fewer than two passes are left, the counts are not small, or the source cells, the
 * output cells and the sum's line overlap or wrap.
 * ============================================================================================= */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w01606C00_spu_func_00007DC8_orig_sv(spu_context* ctx);
#  define NX7_ORIG_SV   gt5_w01606C00_spu_func_00007DC8_orig_sv
#  define NX7_NATIVE_SV gt5_w01606C00_spu_func_00007DC8_sv
#else
#  define NX7_ORIG_SV   gt5_w01606C00_spu_func_00007DC8_sv
#  define NX7_NATIVE_SV gt5_w01606C00_native_00007DC8_sv
#endif

#ifdef NATIVE_STATS
/* [0] calls, [1] fast-forwards, [2] passes fast-forwarded, [3] float operations done by the lifted helper,
 * [4] conversions redone by the helper */
unsigned long long g_native7DC8_stat[5];
int g_native7DC8_fault;              /* harness self-test: one bit of the last fast-forwarded output line flipped */
unsigned long long g_native7DC8_faults;
static SPU_THREAD_LOCAL uint32_t t_nx7_fault_at;
#  define NX7_STAT(i, n) (g_native7DC8_stat[i] += (n))
#else
#  define NX7_STAT(i, n) ((void)0)
#endif

/* Which of the 16 lane patterns of (r15 > r17) scale the cell: the lifted statements themselves,
 * run once (gb, rotqby of the 0xCDFE byte table, ceqbi, xsbh, ceqhi, fsm). Bit m: pattern m (bit i
 * of m = word i) scales; bit 30: computed; bit 31: usable (every pattern gave all ones or zero). */
static uint32_t g_nx7_tab;
static NX8_NOINLINE uint32_t nx7_tab_init(void) {
    const u128 r14 = spu_andhi(spu_fsmbi(0xCDFE), 257);
    uint32_t t = 0xC0000000u;
    for (unsigned m = 0; m < 16; m++) {
        u128 r51;
        for (int i = 0; i < 4; i++) r51._u32[i] = (m >> i & 1u) ? 0xFFFFFFFFu : 0u;
        const u128 r3 = spu_fsm(spu_ceqhi(spu_xsbh(spu_ceqbi(spu_rotqby(r14, spu_gb(r51)), 0)), 0));
        if (spu_keq(r3, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)) t |= 1u << m;
        else if (!spu_keq(r3, 0, 0, 0, 0)) t &= ~0x80000000u;
    }
    g_nx7_tab = t;
    return t;
}

static inline __attribute__((always_inline)) __m128i nx7_fm(__m128i a, __m128i b) {
    const __m128 t = _mm_mul_ps(PS(a), PS(b));
    if (NX8_LIKELY(spu__x8_all4(spu__x8_inrange(t)))) return spu__x8_flush(SI(t));
    NX7_STAT(3, 1);
    return nx8_ld(spu_fm(nx8_st(a), nx8_st(b)));
}
/* The running sum, LS[r20] = (LS[r20] + c0) + c1, is one chain of adds per lane, the length of the
 * grid, and what the pass waits for. Words 0-2 are floats; the fourth word of a cell is two fraction
 * bytes, so as a "float" it is anything, exponent 255 included, and the sum's fourth lane soon sits
 * at the top of the range, where the host's single add is no use. The two are kept apart:
 *  - words 0-2: the host add on unflushed values (a -0 for a +0 changes no later sum), checked for
 *    range after each pass; a pass with a lane out of range is redone with spu_fa itself;
 *  - word 3: the SPU's extended-range add, exactly, in double precision. Every SPU single (exponent
 *    255 too) is a double; under round toward zero the double sum is the exact sum truncated to 53
 *    bits, and clearing its low 29 bits truncates that to the 24 the SPU keeps -- the same as
 *    truncating the exact sum once. Then the SPU's two limits: above Smax -> +-Smax, below 2^-126
 *    -> +0. (spu__sf_sum / spu__sf_pack in spu_helpers.h: the reference this is tested against.) */
static inline __attribute__((always_inline)) __m128d nx7_wide(uint32_t x) {       /* an SPU single as a double */
    const uint64_t e = (x >> 23) & 0xFFu;
    const uint64_t b = e ? ((uint64_t)(x >> 31) << 63) | ((e + 896u) << 52) | ((uint64_t)(x & 0x7FFFFFu) << 29) : 0u;
    return _mm_castsi128_pd(_mm_cvtsi64_si128((long long)b));
}
static inline __attribute__((always_inline)) uint32_t nx7_narrow(__m128d d) {     /* back: d is a result of nx7_add3 */
    const uint64_t b = (uint64_t)_mm_cvtsi128_si64(_mm_castpd_si128(d));
    const uint32_t e = (uint32_t)(b >> 52) & 0x7FFu;
    return e ? (uint32_t)(b >> 63 << 31) | ((e - 896u) << 23) | ((uint32_t)(b >> 29) & 0x7FFFFFu) : 0u;
}
static inline __attribute__((always_inline)) __m128d nx7_add3(__m128d a, __m128d b) {
    const __m128d s = _mm_and_pd(_mm_add_sd(a, b), _mm_castsi128_pd(_mm_cvtsi64_si128((long long)0xFFFFFFFFE0000000ull)));
    const uint64_t m = (uint64_t)_mm_cvtsi128_si64(_mm_castpd_si128(s)) & 0x7FFFFFFFFFFFFFFFull;
    /* zero, or between 2^-126 and Smax: nearly always */
    if (NX8_LIKELY(m - 0x3810000000000000ull <= 0x47FFFFFFE0000000ull - 0x3810000000000000ull)) return s;
    if (m > 0x47FFFFFFE0000000ull)                                                 /* above Smax: saturates, sign kept */
        return _mm_castsi128_pd(_mm_cvtsi64_si128((long long)(((uint64_t)_mm_cvtsi128_si64(_mm_castpd_si128(s)) & 0x8000000000000000ull) | 0x47FFFFFFE0000000ull)));
    return _mm_setzero_pd();                                                       /* zero, or below 2^-126: +0 */
}
/* One packed cell, as the eight local-store bytes (first byte lowest): mass * 2^24, x * 2^12, the two
 * fraction bytes (the high half of word 3), y * 2^12 -- the low 16 bits of each conversion. */
static inline __attribute__((always_inline)) uint64_t nx7_pack(__m128i c) {
    __m128i ti = _mm_cvttps_epi32(_mm_mul_ps(PS(c), _mm_setr_ps(4096.0f, 4096.0f, 16777216.0f, 0.0f)));
    /* 0x80000000 is what the host gives for anything out of range: the SPU saturates either way */
    if (NX8_UNLIKELY(_mm_movemask_ps(PS(_mm_cmpeq_epi32(ti, _mm_set1_epi32((int)0x80000000u)))) & 7)) {
        const u128 a = spu_cflts(nx8_st(c), 161), b = spu_cflts(nx8_st(c), 149);
        ti = _mm_setr_epi32((int)a._u32[0], (int)a._u32[1], (int)b._u32[2], 0);
        NX7_STAT(4, 1);
    }
    const uint64_t m = (uint16_t)_mm_extract_epi16(ti, 4), x = (uint16_t)_mm_extract_epi16(ti, 0), y = (uint16_t)_mm_extract_epi16(ti, 2);
    const uint64_t f = (uint16_t)_mm_extract_epi16(c, 7);
    return __builtin_bswap64((m << 48) | (x << 32) | (f << 16) | y);
}

/* The passes of the row but the last. Returns how many it did (0: nothing touched). */
static inline __attribute__((always_inline)) uint32_t nx7_fast(uint8_t* const ls, u128 r13, u128 r14, u128 r15, u128 r16, u128 r17,
                                                               u128 r18, u128 r19, u128 r20, u128 r21, u128 r22, u128 r23, u128 r24, u128 r25) {
    uint32_t tab = g_nx7_tab;
    if (NX8_UNLIKELY(!(tab & 0x40000000u))) tab = nx7_tab_init();
    /* the constants the pass takes from registers: the 0xCDFE byte table and the three shuffle controls */
    if (NX8_UNLIKELY(!(tab & 0x80000000u) || !spu_keq(r14, 0x01010000u, 0x01010001u, 0x01010101u, 0x01010100u) ||
                     !spu_keq(r19, 0x0A0B1213u, 0x80801617u, 0x80808080u, 0x80808080u) ||
                     !spu_keq(r24, 0x00010203u, 0x1C1D0607u, 0x80808080u, 0x80808080u) ||
                     !spu_keq(r23, 0x80808080u, 0x80808080u, 0x00010203u, 0x1C1D0607u))) return 0;
    const uint32_t n21 = r21._u32[0], c = r16._u32[0];
    if (n21 > 4096u || c >= n21) return 0;
    if (NX8_UNLIKELY((_mm_getcsr() & SPU_MXCSR_RZFZ) != SPU_MXCSR_RZFZ)) return 0;     /* the sums below rest on round toward zero */
    const uint32_t passes = (n21 - c + 1u) / 2u;                    /* from here, this one included: while r21 > r16 + 2 */
    if (passes < 2u) return 0;
    const uint32_t k = passes - 1u;
    const uint32_t idx0 = (r18._u32[0] & 0xFFFFu) + c;              /* the halfword sum r18 + r16: no 16-bit sign change below */
    if (idx0 + 2u * k >= 0x8000u) return 0;
    const uint32_t sa = r13._u32[0] & LSM, oa = ((idx0 << 3) + r25._u32[0]) & LSM, aa = r20._u32[0] & LSM;
    if (sa + 32u * k > SPU_LS_SIZE || oa + 16u * k > SPU_LS_SIZE) return 0;          /* a strip wraps */
    if ((sa < oa + 16u * k && oa < sa + 32u * k) || (aa < sa + 32u * k && sa < aa + 16u) || (aa < oa + 16u * k && oa < aa + 16u)) return 0;
    const __m128i r17v = nx8_ld(r17), r22v = nx8_ld(r22), one = _mm_set1_epi32(1);
    const __m128i m012 = _mm_setr_epi32(-1, -1, -1, 0);
    __m128i cnt = nx8_ld(r15);
    const __m128i acc0 = nx8_bswap(_mm_loadu_si128((const __m128i*)(ls + aa)));
    __m128 acc = PS(_mm_and_si128(acc0, m012));                     /* words 0-2 of the sum, unflushed */
    __m128d acc3 = nx7_wide((uint32_t)_mm_extract_epi32(acc0, 3));  /* word 3 */
    const uint8_t* s = ls + sa;
    uint8_t* o = ls + oa;
    for (uint32_t j = 0; j < k; j++, s += 32, o += 16) {
        __m128i c0 = nx8_bswap(_mm_loadu_si128((const __m128i*)s)), c1 = nx8_bswap(_mm_loadu_si128((const __m128i*)(s + 16)));
        const unsigned m0 = (unsigned)_mm_movemask_ps(PS(_mm_cmpgt_epi32(cnt, r17v)));
        cnt = _mm_add_epi32(cnt, one);
        const unsigned m1 = (unsigned)_mm_movemask_ps(PS(_mm_cmpgt_epi32(cnt, r17v)));
        cnt = _mm_add_epi32(cnt, one);
        if (tab >> m0 & 1u) c0 = nx7_fm(c0, r22v);
        if (tab >> m1 & 1u) c1 = nx7_fm(c1, r22v);
        const __m128 t1 = _mm_add_ps(acc, PS(_mm_and_si128(c0, m012)));
        const __m128 t2 = _mm_add_ps(t1, PS(_mm_and_si128(c1, m012)));
        if (NX8_LIKELY(spu__x8_all4(_mm_and_si128(spu__x8_inrange(t1), spu__x8_inrange(t2))))) acc = t2;
        else {                                                      /* a lane out of the host's range: the lifted helper */
            const u128 a_ = nx8_st(spu__x8_flush(SI(acc)));
            acc = PS(_mm_and_si128(nx8_ld(spu_fa(spu_fa(a_, nx8_st(_mm_and_si128(c0, m012))), nx8_st(_mm_and_si128(c1, m012)))), m012));
            NX7_STAT(3, 2);
        }
        acc3 = nx7_add3(nx7_add3(acc3, nx7_wide((uint32_t)_mm_extract_epi32(c0, 3))), nx7_wide((uint32_t)_mm_extract_epi32(c1, 3)));
        const uint64_t p0 = nx7_pack(c0), p1 = nx7_pack(c1);
        memcpy(o, &p0, 8); memcpy(o + 8, &p1, 8);
    }
    _mm_storeu_si128((__m128i*)(ls + aa), nx8_bswap(_mm_insert_epi32(spu__x8_flush(SI(acc)), (int)nx7_narrow(acc3), 3)));
    NX7_STAT(1, 1); NX7_STAT(2, k);
#ifdef NATIVE_STATS
    t_nx7_fault_at = oa + 16u * (k - 1u);
#endif
    return k;
}

#define NX7_FAST_FORWARD do { \
        if ((_rd | _wd) == 0 && ctx->int_enable == 0) { \
            const uint32_t k_ = nx7_fast(_ls, r13, r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25); \
            if (k_) { r13 = spu_ai(r13, (int32_t)(32u * k_)); r15 = spu_ai(r15, (int32_t)(2u * k_)); \
                      r16 = spu_ai(r16, (int32_t)(2u * k_)); _fa = 1u; } \
        } } while (0)

static SPU_SV void nx7_body_sv(spu_context* ctx) {
#include "blk_7DC8.inc"
}
#undef NX7_FAST_FORWARD

#ifdef NATIVE_STATS
static SPU_SV void nx7_native_sv(spu_context* ctx) {
    t_nx7_fault_at = 0xFFFFFFFFu;
    nx7_body_sv(ctx);
    if (g_native7DC8_fault && t_nx7_fault_at != 0xFFFFFFFFu) { ctx->ls[t_nx7_fault_at + 15u] ^= 1u; g_native7DC8_faults++; }
}
#else
#  define nx7_native_sv nx7_body_sv
#endif

SPU_SV void NX7_NATIVE_SV(spu_context* ctx) {
    const int m = native_geo_mode();
    NX7_STAT(0, 1);
    if (NX8_LIKELY(m == 1)) { __attribute__((musttail)) return nx7_native_sv(ctx); }
    if (m == 2 && ngeo_compare_whole(ctx, 0x7DC8, nx7_native_sv, NX7_ORIG_SV, 0x7DC8u, 0x7FE0u)) return;
    __attribute__((musttail)) return NX7_ORIG_SV(ctx);
}

#ifndef NATIVE_HOOKED
void gt5_w01606C00_native_00007DC8(spu_context* ctx) { SPU_SV_CALL gt5_w01606C00_native_00007DC8_sv(ctx); }
#endif
