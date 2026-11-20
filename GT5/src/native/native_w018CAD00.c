/*
 * native_w018CAD00.c -- the triangle cull of GT5's per-batch geometry job w018CAD00 (workload 4,
 * "GPUSyncArray"), and who does it: the SPU code as on the PS3, or the graphics card.
 *
 * The job (an Edge-style geometry segment: one batch, one draw per job) transforms the batch's
 * vertices, culls its triangles at 0xAB00, packs the kept ones, optionally renumbers the vertices
 * the kept triangles use (vertex map), converts the list to 16-bit triples (0xDF00) and writes its
 * own draw into the command-buffer hole (0x1036C: vertex arrays, SET_INDEX_ARRAY_ADDRESS = ctx+0x11C,
 * BEGIN(TRIANGLES), DRAW_INDEX_ARRAY 3*kept, END, ring-buffer semaphores). It sets no cull, depth or
 * blend state: the draw keeps what the PPU's command stream set before the hole.
 *
 * 0xAB00(r3 list, r4 line, r5 view, r6 batch matrix|0, r7 line|0, r8 scratch, r9 map|0, r10 positions)
 *   list      8 bytes a triangle: i0 i1 i2 pad (u16), kept ones packed in place
 *   r4 line   word0 triangles, word1 vertices, OUT word2 kept, OUT word3 vertices used (map only)
 *   view      byte0 sample pattern, byte1 facing flip (signed > 0), u16 at +8..+15 scissor x w y h,
 *             +0x10 matrix V (rows), +0x50 viewport (scale x, offset x, scale y, offset y)
 *   r6        > 0: batch matrix B (rows); clip = V * B * (x y z 1): the position's w is not used
 *   r7        > 0: byte0 signed > 0 = two-sided (no facing test)
 *   r9        > 0: vertex map out: u16 old*16 per used vertex (4-vertex blocks from the top block
 *             down, ascending in a block); the kept list is renumbered, its pad halfwords zeroed
 * Rejected (measured on the lifted code with black-box runs): all three z < -w (in front of the
 * camera) or all three z > w; bounding box outside the scissor; facing (keeps area < 0 in screen space,
 * the flip byte the other one) unless two-sided; bounding box holding no sample position of the pattern
 * (byte0). Screen x = scale x * X/|W| + offset x - scissor x + ~0.48 (y alike). Behind the camera the
 * rules are not fully known (mode 2's model is approximate there); mode 1 never decides such a batch.
 *
 * GT5_CULL (read once):
 *   0  (default) the PS3's cull: the lifted code, untouched.
 *   1  the graphics card culls. Per batch, from the bounding box of its vertices put through V*B:
 *        - wholly outside the screen (with margin, every corner in front of the camera): the PS3's
 *          result directly, nothing kept (no transform, no draw);
 *        - wholly inside the depth range (every corner 0 < w, |z| <= w): every triangle kept, the
 *          vertex map built for all of them; the draw is tagged (index address, count, first
 *          triangle, facing rule) for the renderer, which applies the SPU's facing rule as the draw's
 *          cull state when the game's own state does not already (rsx_dispatch.c, rsx_cull_tag_*);
 *        - anything else (a corner behind the camera or beyond a depth plane: the port clamps depth
 *          instead of clipping, so such triangles would show), an index >= vertices, a pointer or
 *          count it does not expect, other code at these addresses: the lifted cull, as the PS3.
 *   2  compare: the lifted cull runs as always; a native model counts what it rejects and why, the
 *      kept count it predicts is checked against the job's, and the draw is tagged so the renderer
 *      reports the game's cull state against the SPU's facing rule (it changes nothing).
 * Modes 1 and 2 print "[cull] ..." every 5 s. PS3_AB_PERIOD=10 PS3_AB_WHAT=cull alternates the mode with 0.
 * The draw is tagged at 0x1011C (the writer's draw emitter), when the index address is final; 0xF6C0
 * (the job's other cull routine, for batches with a record list at ctx+0x244) stays lifted, its draws
 * tagged report-only. GT5_CULL_DUMP=<folder> (modes 1, 2) writes real inputs of both routines.
 *
 * The job's output allocator (0x4650, 0x4330, 0x41E8, 0x40A0) runs as native code: GT5_ALLOC, see its
 * section below; so does its quadword copy 0x8F40 (GT5_MEMCPY). The build makes the lift take this image's
 * branches and calls to its own entries directly, as for every WWS job image (gt5_direct_bi.c: GT5_DIRECT_BI).
 *
 * Build: as the other natives (natives.txt `hooked` lines): -DNATIVE_HOOKED in the game; without it
 * (harness) the functions are gt5_w018CAD00_native_XXXXXXXX_sv and NC_LIFTED(A) names the lifted
 * function (the harness defines it to its own copy).
 */
#include "spu_recomp.h"
#include "spu_helpers.h"
#include "spu_context.h"
#include "native_geo.h"
#include <immintrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#endif

#ifdef NATIVE_HOOKED
SPU_SV void gt5_w018CAD00_spu_func_0000AB00_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_0000F6C0_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_0001036C_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_0001011C_orig_sv(spu_context* ctx);
#  define NC_ORIG(A)   gt5_w018CAD00_spu_func_##A##_orig_sv
#  define NC_NATIVE(A) gt5_w018CAD00_spu_func_##A##_sv
#else
#  ifndef NC_LIFTED
#    define NC_LIFTED(A) gt5_w018CAD00_spu_func_##A##_sv
#  endif
SPU_SV void NC_LIFTED(0000AB00)(spu_context* ctx);
SPU_SV void NC_LIFTED(0000F6C0)(spu_context* ctx);
SPU_SV void NC_LIFTED(0001036C)(spu_context* ctx);
SPU_SV void NC_LIFTED(0001011C)(spu_context* ctx);
#  define NC_ORIG(A)   NC_LIFTED(A)
#  define NC_NATIVE(A) gt5_w018CAD00_native_##A##_sv
#endif

/* The renderer's side (ps3recomp libs/video/rsx_dispatch.c). The harness supplies its own. */
void rsx_cull_tag_put(uint32_t index_addr, uint32_t index_count, uint32_t first3, uint32_t rule);

#define NC_LSM (SPU_LS_SIZE - 1u)
#define NC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define NC_UNLIKELY(x) __builtin_expect(!!(x), 0)

static inline uint32_t nc_be32(const uint8_t* ls, uint32_t a) {
    a &= NC_LSM & ~3u;
    uint32_t v; memcpy(&v, ls + a, 4); return __builtin_bswap32(v);
}
static inline uint32_t nc_be16(const uint8_t* ls, uint32_t a) {
    a &= NC_LSM & ~1u;
    return ((uint32_t)ls[a] << 8) | ls[a + 1];
}
static inline void nc_st32(uint8_t* ls, uint32_t a, uint32_t v) {
    a &= NC_LSM & ~3u;
    v = __builtin_bswap32(v); memcpy(ls + a, &v, 4);
}
static inline float nc_f(const uint8_t* ls, uint32_t a) {
    const uint32_t u = nc_be32(ls, a); float f; memcpy(&f, &u, 4); return f;
}

/* ---- the direct branches of this image (and of every WWS job image) are the build's (spu_native_patch.py
 * --direct-bi, gt5_direct_bi.c: GT5_DIRECT_BI, GT5_DIRECT_BI_OFF); the allocator below polls their in-run A/B
 * (PS3_AB_PERIOD=<s> PS3_AB_WHAT=directbi), since it runs often whatever their mode. */
void gt5_dbi_ab_poll(void);
int ps3_ab_on(const char* what);

/* ---- the switch and the counters ---------------------------------------------------------------- */
/* PS3_AB_PERIOD=<s> PS3_AB_WHAT=cull (the runtime's in-run A/B, sys_ppu_thread.c): GT5_CULL's mode in
 * the "on" periods, 0 in the "off" ones, so both are measured under the same load ([ab] lines). */
int ps3_ab_on(const char* what);
static void nc_dump_init(void);
int g_nc_mode = -1;
static int g_nc_ab;
static __attribute__((noinline)) int nc_init(void)
{
    static volatile long said;
    const char* e = getenv("GT5_CULL");
    int m = 0;
    if (e && e[0] >= '0' && e[0] <= '2' && e[1] == 0) m = e[0] - '0';
    g_nc_ab = m != 0 && ps3_ab_on("cull") >= 0;
    if (m != 0) nc_dump_init();
    if (__atomic_exchange_n(&said, 1, __ATOMIC_SEQ_CST) == 0) {
        fprintf(stderr, "[cull] GT5_CULL=%d%s\n", m, g_nc_ab ? " (alternated with 0: PS3_AB_WHAT=cull)" : "");
        fflush(stderr);
    }
    g_nc_mode = m;
    return m;
}
static __attribute__((noinline)) int nc_ab_mode(int m)
{
    static SPU_THREAD_LOCAL unsigned n;
    static SPU_THREAD_LOCAL int on;
    if ((n++ & 63u) == 0) on = ps3_ab_on("cull") == 1;
    return on ? m : 0;
}
static inline int nc_mode(void)
{
    int m = g_nc_mode;
    if (NC_UNLIKELY(m < 0)) m = nc_init();
    return NC_LIKELY(!g_nc_ab) ? m : nc_ab_mode(m);
}

/* Counters (relaxed adds from the worker threads). */
enum {
    NC_CALLS, NC_TRIS, NC_LIFTED_CALLS,                    /* calls of 0xAB00, their triangles, given to the lifted cull */
    NC_WHY_GUARD, NC_WHY_SHAPE, NC_WHY_DEPTH, NC_WHY_INDEX,  /* why: irq/diag/image, pointers/counts, depth range, index >= vertices */
    NC_GPU_CALLS, NC_GPU_TRIS, NC_GPU_MAP,                 /* mode 1: batches kept whole, their triangles, with a vertex map */
    NC_OFF_CALLS, NC_OFF_TRIS,                             /* whole batch off screen: nothing kept */
    NC_TWO_SIDED, NC_FLIPPED,                              /* batches by facing rule (mode 1 kept + mode 2 all) */
    NC_REJ_NEAR, NC_REJ_FAR, NC_REJ_OFF, NC_REJ_FACE, NC_REJ_SMALL, NC_REJ_BEHIND,   /* mode 2: model's reasons */
    NC_MODEL_KEPT,                                         /* mode 2: triangles the model keeps */
    NC_CMP_DRAWS, NC_CMP_SAME, NC_CMP_DIFF, NC_CMP_ACT_KEPT,  /* mode 2: kept count checked at the command writer */
    NC_TAGS, NC_TAG_NOMATCH,                               /* draws tagged; command writers that found no pending batch */
    NC_VERTS_ALL, NC_VERTS_USED_PS3,                       /* map batches: vertices all triangles use / the PS3's kept ones use */
    NC_F6C0_CALLS, NC_F6C0_TRIS, NC_F6C0_KEPT,             /* the other cull routine (left to the lifted code) */
    NC_N
};
unsigned long long g_nc_stat[NC_N];
#define NC_ADD(k, n) __atomic_fetch_add(&g_nc_stat[k], (unsigned long long)(n), __ATOMIC_RELAXED)

static void nc_report(void)
{
#ifdef _WIN32
    static volatile LONG64 next;
    const LONG64 now = (LONG64)GetTickCount64();
    LONG64 t = next;
    if (now < t) return;
    if (InterlockedCompareExchange64(&next, now + 5000, t) != t) return;
    if (!t) return;                             /* the first call only arms the timer */
#else
    return;
#endif
    unsigned long long s[NC_N];
    for (int i = 0; i < NC_N; i++) s[i] = __atomic_load_n(&g_nc_stat[i], __ATOMIC_RELAXED);
    fprintf(stderr, "[cull] mode %d: batches %llu (triangles %llu) | lifted cull %llu (guard %llu, shape %llu, depth range %llu, index %llu) | "
            "kept whole %llu (triangles %llu, with vertex map %llu) | off screen %llu (triangles %llu) | two-sided %llu, flipped %llu | "
            "tagged draws %llu, writers without a batch %llu | 0xF6C0 (lifted, report only): batches %llu, triangles %llu, kept %llu\n",
            g_nc_mode, s[NC_CALLS], s[NC_TRIS], s[NC_LIFTED_CALLS], s[NC_WHY_GUARD], s[NC_WHY_SHAPE], s[NC_WHY_DEPTH], s[NC_WHY_INDEX],
            s[NC_GPU_CALLS], s[NC_GPU_TRIS], s[NC_GPU_MAP], s[NC_OFF_CALLS], s[NC_OFF_TRIS], s[NC_TWO_SIDED], s[NC_FLIPPED],
            s[NC_TAGS], s[NC_TAG_NOMATCH], s[NC_F6C0_CALLS], s[NC_F6C0_TRIS], s[NC_F6C0_KEPT]);
    if (g_nc_mode == 2)
        fprintf(stderr, "[cull] compare: model rejects near %llu, far %llu, off screen %llu, facing %llu, small %llu, behind %llu, keeps %llu | "
                "checked draws %llu: kept count as modelled %llu, not %llu; kept by the PS3 %llu | map batches: vertices used by all %llu, by the kept %llu\n",
                s[NC_REJ_NEAR], s[NC_REJ_FAR], s[NC_REJ_OFF], s[NC_REJ_FACE], s[NC_REJ_SMALL], s[NC_REJ_BEHIND], s[NC_MODEL_KEPT],
                s[NC_CMP_DRAWS], s[NC_CMP_SAME], s[NC_CMP_DIFF], s[NC_CMP_ACT_KEPT], s[NC_VERTS_ALL], s[NC_VERTS_USED_PS3]);
    fflush(stderr);
}

/* ---- the code these functions were written for ------------------------------------------------ */
/* Four words at each place the semantics above come from (entry, vertex loop, both triangle loops,
 * the map code, the command writer). Another image at these addresses keeps its lifted code. */
static const uint32_t k_words[][5] = {
    { 0xAB00, 0x411F5010u, 0x338CBD8Fu, 0x428101B4u, 0x3400020Bu },
    { 0xAD30, 0xE96D601Bu, 0xB372A490u, 0xE9901899u, 0xB332A491u },
    { 0xAFC0, 0xDA77B09Bu, 0x3B834D5Eu, 0x7C00003Du, 0x3F8216BEu },
    { 0xB240, 0x582C2A5Au, 0, 0, 0 },
    { 0xB5D8, 0x0F610B0Bu, 0x12000594u, 0x4080000Du, 0x00200000u },
    { 0x1036C, 0x24FFC0D0u, 0x340741D0u, 0x24FDC081u, 0x127FDC95u },
};
static inline int nc_image_ok(const uint8_t* ls)
{
    for (unsigned i = 0; i < sizeof k_words / sizeof k_words[0]; i++)
        for (int w = 0; w < 4; w++)
            if (k_words[i][1 + w] && nc_be32(ls, k_words[i][0] + 4u * (uint32_t)w) != k_words[i][1 + w]) return 0;
    return 1;
}

/* ---- one call's parameters ----------------------------------------------------------------------- */
typedef struct {
    uint32_t list, line, view, bmat, l7, map, pos;   /* LS addresses (0 = none for bmat / l7 / map) */
    uint32_t ntri, nvert;
    int two_sided, flip;
    float M[16];                                     /* V * B, rows */
    float vp[4];                                     /* scale x, offset x, scale y, offset y */
    float sc[4];                                     /* scissor x, w, y, h */
    uint32_t pattern;                                /* view byte 0 */
} nc_call;

/* 0 = not a call this file handles (left to the lifted cull) */
static int nc_parse(const spu_context* ctx, nc_call* c)
{
    const uint8_t* ls = ctx->ls;
    c->list = ctx->gpr[3]._u32[0]; c->line = ctx->gpr[4]._u32[0] & NC_LSM & ~15u; c->view = ctx->gpr[5]._u32[0] & NC_LSM & ~15u;
    const uint32_t r6 = ctx->gpr[6]._u32[0], r7 = ctx->gpr[7]._u32[0], r9 = ctx->gpr[9]._u32[0];
    c->bmat = (int32_t)r6 > 0 ? (r6 & NC_LSM & ~15u) : 0;
    c->l7 = (int32_t)r7 > 0 ? (r7 & NC_LSM & ~15u) : 0;
    c->map = (int32_t)r9 > 0 ? r9 : 0;
    c->pos = ctx->gpr[10]._u32[0];
    if (((c->list | c->pos | c->map) & 15u) || c->list > NC_LSM || c->pos > NC_LSM || c->map > NC_LSM) return 0;
    c->ntri = nc_be32(ls, c->line); c->nvert = nc_be32(ls, c->line + 4);
    if (c->ntri == 0 || c->ntri > 0x8000u || c->nvert == 0 || c->nvert > 0x4000u) return 0;
    /* the buffers as the SPU walks them (rounded up to its passes) inside local store, no wrap */
    if (c->list + ((c->ntri * 8u + 31u) & ~31u) > SPU_LS_SIZE || c->pos + ((c->nvert + 3u) & ~3u) * 16u > SPU_LS_SIZE) return 0;
    if (c->map && c->map + ((c->nvert + 15u) & ~15u) * 2u > SPU_LS_SIZE) return 0;
    c->pattern = ls[c->view];
    c->flip = (int8_t)ls[c->view + 1] > 0;
    c->two_sided = c->l7 && (int8_t)ls[c->l7] > 0;
    float V[16], B[16];
    for (int i = 0; i < 16; i++) V[i] = nc_f(ls, c->view + 0x10u + 4u * (uint32_t)i);
    if (c->bmat) for (int i = 0; i < 16; i++) B[i] = nc_f(ls, c->bmat + 4u * (uint32_t)i);
    else { memset(B, 0, sizeof B); B[0] = B[5] = B[10] = B[15] = 1.0f; }
    for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) {
        double s = 0; for (int j = 0; j < 4; j++) s += (double)V[r * 4 + j] * (double)B[j * 4 + k];
        c->M[r * 4 + k] = (float)s;
    }
    for (int i = 0; i < 4; i++) c->vp[i] = nc_f(ls, c->view + 0x50u + 4u * (uint32_t)i);
    for (int i = 0; i < 4; i++) c->sc[i] = (float)nc_be16(ls, c->view + 8u + 2u * (uint32_t)i);
    for (int i = 0; i < 16; i++) if (!isfinite(c->M[i])) return 0;
    for (int i = 0; i < 4; i++) if (!isfinite(c->vp[i])) return 0;
    return 1;
}

/* The facing rule as the renderer reads it (rsx_dispatch.c): bit 0 two-sided, bit 1 flip, bit 2 the
 * SPU's viewport x scale is negative, bit 3 its y scale; bits 8-9 the mode (1 the card culls, 2
 * compare: report only). */
static inline uint32_t nc_rule(const nc_call* c, int mode)
{
    return (uint32_t)c->two_sided | ((uint32_t)c->flip << 1) | ((uint32_t)(c->vp[0] < 0.0f) << 2) |
           ((uint32_t)(c->vp[2] < 0.0f) << 3) | ((uint32_t)mode << 8);
}

/* ---- the batch's bounding box through V*B ------------------------------------------------------- */
enum { NC_BOX_MIXED = 0, NC_BOX_INSIDE = 1, NC_BOX_OFFSCREEN = 2 };
static int nc_box(const uint8_t* ls, const nc_call* c)
{
    /* positions: 16 bytes a vertex (x y z, w unused), big-endian, 16-byte aligned (nc_parse) */
    const __m128i bswap = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    const __m128 big = _mm_set1_ps(1e30f), absm = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    __m128 vlo = _mm_set1_ps(INFINITY), vhi = _mm_set1_ps(-INFINITY), bad = _mm_setzero_ps();
    for (uint32_t i = 0; i < c->nvert; i++) {
        const __m128 v = _mm_castsi128_ps(_mm_shuffle_epi8(_mm_load_si128((const __m128i*)(ls + c->pos + 16u * i)), bswap));
        bad = _mm_or_ps(bad, _mm_cmpnle_ps(_mm_and_ps(v, absm), big));   /* NaN, Inf or near it: the lifted cull */
        vlo = _mm_min_ps(vlo, v); vhi = _mm_max_ps(vhi, v);
    }
    if (_mm_movemask_ps(bad) & 7) return NC_BOX_MIXED;
    float lo[4], hi[4];
    _mm_storeu_ps(lo, vlo); _mm_storeu_ps(hi, vhi);
    int depth_ok = 1, left = 1, right = 1, above = 1, below = 1;
    const float W = c->sc[1], H = c->sc[3];
    for (int k = 0; k < 8; k++) {
        const double p[4] = { (k & 1) ? hi[0] : lo[0], (k & 2) ? hi[1] : lo[1], (k & 4) ? hi[2] : lo[2], 1.0 };
        double q[4];
        for (int r = 0; r < 4; r++) q[r] = c->M[r * 4] * p[0] + c->M[r * 4 + 1] * p[1] + c->M[r * 4 + 2] * p[2] + c->M[r * 4 + 3];
        /* |z| <= w with a margin of 1/1000 of w, w clearly positive */
        if (!(q[3] > 1e-4) || !(q[2] <= q[3] * 0.999) || !(q[2] >= -q[3] * 0.999)) { depth_ok = 0; left = right = above = below = 0; break; }
        const double sx = c->vp[0] * (q[0] / q[3]) + c->vp[1] - c->sc[0];
        const double sy = c->vp[2] * (q[1] / q[3]) + c->vp[3] - c->sc[2];
        if (!(sx < -4.0)) left = 0;
        if (!(sx > W + 4.0)) right = 0;
        if (!(sy < -4.0)) above = 0;
        if (!(sy > H + 4.0)) below = 0;
        if (!(fabs(sx) < 1e7 && fabs(sy) < 1e7)) left = right = above = below = 0;   /* far from the fixed-point range: not sure */
    }
    if (left || right || above || below) return NC_BOX_OFFSCREEN;
    return depth_ok ? NC_BOX_INSIDE : NC_BOX_MIXED;
}

/* ---- every triangle kept: the result the lifted cull gives when all its tests pass ----------------- */
/* (checked against the lifted code: harness\keepall.c). Returns 0 when an index is >= the vertex count
 * (nothing written; the lifted cull takes the call). */
static uint8_t* nc_scratch(size_t n)
{
    static SPU_THREAD_LOCAL uint8_t* buf;
    static SPU_THREAD_LOCAL size_t cap;
    if (n > cap) { free(buf); cap = n < 65536 ? 65536 : n; buf = (uint8_t*)malloc(cap); if (!buf) cap = 0; }
    return buf;
}
static int nc_keep_all(uint8_t* ls, const nc_call* c, uint32_t* used_out)
{
    const uint32_t n = c->ntri, nv = c->nvert;
    uint8_t* const L = ls + c->list;                 /* 16-byte aligned, inside local store (nc_parse) */
    /* the largest index: triangles as big-endian u16 x4 */
    uint32_t mx = 0;
    for (uint32_t t = 0; t < n; t++) {
        uint64_t w; memcpy(&w, L + 8u * t, 8);
        const uint32_t i0 = __builtin_bswap16((uint16_t)w), i1 = __builtin_bswap16((uint16_t)(w >> 16)), i2 = __builtin_bswap16((uint16_t)(w >> 32));
        const uint32_t m = i0 > i1 ? i0 : i1; mx = m > mx ? m : mx; mx = i2 > mx ? i2 : mx;
    }
    if (mx >= nv) return 0;
    if (!c->map) {
        nc_st32(ls, c->line + 8, n);
        *used_out = 0;
        return 1;
    }
    /* marks, then numbers: 4-vertex blocks from the top block down, ascending inside a block */
    const uint32_t nv4 = (nv + 3u) & ~3u;
    uint16_t* num = (uint16_t*)nc_scratch(nv4 * sizeof(uint16_t));
    if (!num) return 0;
    memset(num, 0, nv4 * sizeof(uint16_t));
    for (uint32_t t = 0; t < n; t++) {
        uint64_t w; memcpy(&w, L + 8u * t, 8);
        num[__builtin_bswap16((uint16_t)w)] = 1; num[__builtin_bswap16((uint16_t)(w >> 16))] = 1; num[__builtin_bswap16((uint16_t)(w >> 32))] = 1;
    }
    uint32_t used = 0;
    uint8_t* const M = ls + c->map;
    for (int32_t b = (int32_t)(nv4 / 4u) - 1; b >= 0; b--)
        for (uint32_t i = 4u * (uint32_t)b; i < 4u * (uint32_t)b + 4u; i++)
            if (num[i]) { const uint16_t e = __builtin_bswap16((uint16_t)(i * 16u)); memcpy(M + 2u * used, &e, 2); num[i] = (uint16_t)used; used++; }
    for (uint32_t t = 0; t < n; t++) {
        uint64_t w; memcpy(&w, L + 8u * t, 8);
        const uint64_t a = __builtin_bswap16(num[__builtin_bswap16((uint16_t)w)]), b2 = __builtin_bswap16(num[__builtin_bswap16((uint16_t)(w >> 16))]),
                       d = __builtin_bswap16(num[__builtin_bswap16((uint16_t)(w >> 32))]);
        w = a | (b2 << 16) | (d << 32);                /* pad halfword zeroed, as the lifted renumbering does */
        memcpy(L + 8u * t, &w, 8);
    }
    nc_st32(ls, c->line + 8, n);
    nc_st32(ls, c->line + 12, used);
    *used_out = used;
    return 1;
}
/* nothing kept (the batch is off screen): what the lifted cull leaves that its callers read. With
 * nothing kept it does not reach the map code, so word3 stays as the caller left it. */
static void nc_keep_none(uint8_t* ls, const nc_call* c)
{
    nc_st32(ls, c->line + 8, 0);
}

/* ---- mode 2: a model of the tests, for counting ------------------------------------------------ */
typedef struct { float x, y; uint8_t near_, far_, behind; } nc_sv;
#ifdef NC_DIAG
uint8_t g_nc_diag[0x8000];
#endif
static uint32_t nc_model(const uint8_t* ls, const nc_call* c, uint32_t* used_all)
{
    nc_sv* v = (nc_sv*)nc_scratch(c->nvert * sizeof(nc_sv) + c->nvert);
    if (!v) return c->ntri;
    uint8_t* mark = (uint8_t*)(v + c->nvert);
    memset(mark, 0, c->nvert);
    const float bias = 0.0f;                                  /* pixel coordinates: sample k+0.5 = the centre */
    for (uint32_t i = 0; i < c->nvert; i++) {
        const uint32_t a = c->pos + 16u * i;
        const float p0 = nc_f(ls, a), p1 = nc_f(ls, a + 4), p2 = nc_f(ls, a + 8);
        float q[4];
        for (int r = 0; r < 4; r++) q[r] = c->M[r * 4] * p0 + c->M[r * 4 + 1] * p1 + c->M[r * 4 + 2] * p2 + c->M[r * 4 + 3];
        v[i].behind = !(q[3] > 0.0f);
        const float iw = 1.0f / fabsf(q[3]);                     /* the SPU divides by |w| */
        v[i].x = c->vp[0] * (q[0] * iw) + c->vp[1] - c->sc[0] + bias;
        v[i].y = c->vp[2] * (q[1] * iw) + c->vp[3] - c->sc[2] + bias;
        v[i].near_ = q[2] < -q[3]; v[i].far_ = q[2] > q[3];      /* clip space: the same as z/w for w > 0 */
    }
    /* sample offsets inside a pixel, by pattern (0: the centre; 1, 5: modelled as the centre) */
    /* sample positions inside a pixel and the half-width of the window around each that counts as a hit
     * (measured on the lifted code: harness\probe.c map2d / sweep): pattern 0 the centre, +-1/64; 2, 3,
     * 4, 6, 7 the 2x2 grid at .25/.75, +-1/64; 1, 5 a rotated grid (.375,.125) (.875,.375) (.125,.625)
     * (.625,.875), +-5/64. A triangle is kept when its bounding box meets one sample's window. */
    static const float k_s0[1][2] = { { 0.5f, 0.5f } };
    static const float k_s4[4][2] = { { 0.25f, 0.25f }, { 0.75f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.75f } };
    static const float k_sr[4][2] = { { 0.375f, 0.125f }, { 0.875f, 0.375f }, { 0.125f, 0.625f }, { 0.625f, 0.875f } };
    const int rot = c->pattern == 1 || c->pattern == 5, grid = !rot && c->pattern != 0;
    const float (*smp)[2] = rot ? k_sr : grid ? k_s4 : k_s0;
    const int nsmp = (rot || grid) ? 4 : 1;
    const float tol = rot ? 5.0f / 64.0f : 1.0f / 64.0f;
    uint32_t kept = 0, rej[6] = { 0 };
#ifdef NC_DIAG
#  define NC_D(r) (g_nc_diag[t & 0x7FFFu] = (uint8_t)(r))
#else
#  define NC_D(r) ((void)0)
#endif
    for (uint32_t t = 0; t < c->ntri; t++) {
        const uint32_t a = c->list + 8u * t;
        const uint32_t i0 = nc_be16(ls, a), i1 = nc_be16(ls, a + 2), i2 = nc_be16(ls, a + 4);
        if (i0 >= c->nvert || i1 >= c->nvert || i2 >= c->nvert) { NC_D(0); kept++; continue; }
        mark[i0] = mark[i1] = mark[i2] = 1;
        const nc_sv *A = &v[i0], *B = &v[i1], *C = &v[i2];
        /* depth: clip-space flags (z < -w, z > w); a vertex behind the camera (w <= 0) is near-flagged
         * but does not count for the near rejection: the SPU tests the projected positions instead */
        if (A->near_ && B->near_ && C->near_ && !A->behind && !B->behind && !C->behind) { NC_D(1); rej[0]++; continue; }
        if (A->far_ && B->far_ && C->far_) { NC_D(2); rej[1]++; continue; }
        const float x0 = fminf(A->x, fminf(B->x, C->x)), x1 = fmaxf(A->x, fmaxf(B->x, C->x));
        const float y0 = fminf(A->y, fminf(B->y, C->y)), y1 = fmaxf(A->y, fmaxf(B->y, C->y));
        if (x1 < -0.484f || y1 < -0.484f || x0 >= c->sc[1] - 0.484f || y0 >= c->sc[3] - 0.484f) { NC_D(3); rej[2]++; continue; }
        const float area = (B->x - A->x) * (C->y - A->y) - (C->x - A->x) * (B->y - A->y);
        /* wholly behind the camera: the SPU drops it unless the batch is two-sided (measured, not understood) */
        if (!c->two_sided && A->behind && B->behind && C->behind) { NC_D(7); rej[5]++; continue; }
        if (!c->two_sided && (c->flip ? area < 0.0f : area > 0.0f)) { NC_D(4); rej[3]++; continue; }
        if (area == 0.0f) { NC_D(5); rej[4]++; continue; }
        /* a sample window met by the bounding box: some pixel i has x0 <= i + sx + tol and x1 >= i + sx - tol */
        int cov = 0;
        for (int k = 0; k < nsmp && !cov; k++)
            cov = floorf(x1 - smp[k][0] + tol) >= ceilf(x0 - smp[k][0] - tol) &&
                  floorf(y1 - smp[k][1] + tol) >= ceilf(y0 - smp[k][1] - tol);
        if (!cov) { NC_D(6); rej[4]++; continue; }
        NC_D(0); kept++;
    }
    uint32_t ua = 0;
    for (uint32_t i = 0; i < c->nvert; i++) ua += mark[i];
    *used_all = ua;
    NC_ADD(NC_REJ_NEAR, rej[0]); NC_ADD(NC_REJ_FAR, rej[1]); NC_ADD(NC_REJ_OFF, rej[2]);
    NC_ADD(NC_REJ_FACE, rej[3]); NC_ADD(NC_REJ_SMALL, rej[4]); NC_ADD(NC_REJ_BEHIND, rej[5]); NC_ADD(NC_MODEL_KEPT, kept);
    return kept;
}

/* ---- the batch waiting for its draw: from the cull to the command writer of the same job --------- */
typedef struct {
    const spu_context* ctx;
    uint32_t list, ntri, kept, rule, model_kept, used_all, has_map;
    uint32_t base;      /* the job's context in LS, from the command writer's entry (0 = not there yet) */
} nc_pending;
static SPU_THREAD_LOCAL nc_pending t_pend;
/* a 0xF6C0 call whose result (ctx+0x114 - ctx+0x118 = 3 x kept, stored by its caller) is read at the next
 * hooked call of the same job: 0xAB00 or the command writer */
typedef struct { const spu_context* ctx; uint32_t base; } nc_f6_pending;
static SPU_THREAD_LOCAL nc_f6_pending t_f6;

/* ---- GT5_CULL_DUMP=<folder>: real inputs for the test harness ----------------------------------------
 * In modes 1 and 2, every GT5_CULL_DUMP_EVERY-th call (default 1009) of 0xAB00 and of 0xF6C0, 200 a
 * routine at most: <folder>/cull_<routine>_<n>.bin = "CUL1" (little-endian word), the routine's address,
 * 8 bytes 0, the 128 registers (2 KB), the whole local store (256 KB), as the call starts. */
static const char* g_nc_dump_dir;
static unsigned g_nc_dump_every = 1009;
static void nc_dump_init(void)
{
    const char* d = getenv("GT5_CULL_DUMP");
    if (d && *d) g_nc_dump_dir = d;
    const char* e = getenv("GT5_CULL_DUMP_EVERY");
    if (e && atoi(e) > 0) g_nc_dump_every = (unsigned)atoi(e);
}
static void nc_dump(const spu_context* ctx, uint32_t routine)
{
    static unsigned long long calls[2];
    static volatile long made[2];
    const int r = routine == 0xF6C0u;
    if (__atomic_add_fetch(&calls[r], 1ull, __ATOMIC_RELAXED) % g_nc_dump_every) return;
    const long n = __atomic_add_fetch(&made[r], 1, __ATOMIC_RELAXED);
    if (n > 200) return;
    char path[512];
    snprintf(path, sizeof path, "%s/cull_%04X_%03ld.bin", g_nc_dump_dir, routine, n);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const uint32_t head[4] = { 0x314C5543u, routine, 0, 0 };
    fwrite(head, 1, sizeof head, f);
    fwrite(ctx->gpr, 1, sizeof ctx->gpr, f);
    fwrite(ctx->ls, 1, SPU_LS_SIZE, f);
    fclose(f);
}

/* ---- GT5_JOB_DUMP=<folder>: real inputs of the programs the cull dumps do not reach ---------------------
 * Opcodes 9 (handler 0x8880), 12 (0x72A0) and 33 (0x8E80) of the job's program ran in no batch any recorded
 * race dumped (the cull dumps start at the cull: programs 39 42 0 and 37 39 42 0). With GT5_JOB_DUMP set,
 * every GT5_JOB_DUMP_EVERY-th call (default 1) of each of these handlers, 200 a handler at most:
 * <folder>/op<NN>_<handler>_<n>.bin, the cull dumps' layout ("CUL1", the handler's address, 8 bytes 0, the
 * 128 registers, the whole local store) as the handler starts, so a harness can run the job from there to
 * its end. "[job-dump] ..." once at the start. Off: one load and a branch a call of these three handlers. */
static int g_jd_on = -1;
static const char* g_jd_dir;
static unsigned g_jd_every = 1;
static __attribute__((noinline)) int jd_init(void)
{
    const char* d = getenv("GT5_JOB_DUMP");
    const char* e = getenv("GT5_JOB_DUMP_EVERY");
    if (e && atoi(e) > 0) g_jd_every = (unsigned)atoi(e);
    g_jd_dir = (d && *d) ? d : NULL;
    if (g_jd_dir) { fprintf(stderr, "[job-dump] opcodes 9 12 33 of w018CAD00 to %s, 1 call in %u, 200 a handler at most\n", g_jd_dir, g_jd_every); fflush(stderr); }
    g_jd_on = g_jd_dir != NULL;
    return g_jd_on;
}
static __attribute__((noinline)) void jd_dump(const spu_context* ctx, int k, int op, uint32_t handler)
{
    static unsigned long long calls[3];
    static volatile long made[3];
    if (__atomic_add_fetch(&calls[k], 1ull, __ATOMIC_RELAXED) % g_jd_every) return;
    const long n = __atomic_add_fetch(&made[k], 1, __ATOMIC_RELAXED);
    if (n > 200) return;
    char path[512];
    snprintf(path, sizeof path, "%s/op%02d_%04X_%03ld.bin", g_jd_dir, op, handler, n);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const uint32_t head[4] = { 0x314C5543u, handler, 0, 0 };
    fwrite(head, 1, sizeof head, f);
    fwrite(ctx->gpr, 1, sizeof ctx->gpr, f);
    fwrite(ctx->ls, 1, SPU_LS_SIZE, f);
    fclose(f);
}

/* ---- GT5_CULL_PROF=1: where a w018CAD00 job's time goes ------------------------------------------
 * The job is a small program: its interpreter (0x5340) calls one handler per opcode from the table at
 * 0x10A30. Every handler and a few helpers get a hook here that stamps the time stamp counter on the way
 * in and on the way out: the time from one stamp to the next on the same SPU context is booked to what
 * was running, a handler or helper from its entry to its return less the stamped calls it made (its own
 * time), the caller again after the return. Numbers are per job: the time booked over the jobs, and
 * calls a job. A job runs from its entry (0x107D8) to the return of its main function (0x10884); the job
 * manager's own time between jobs is not in it (its calls from inside a job are, in the caller's time). Also counted: the indirect branches the runtime dispatched during the job (the
 * runtime's t_spu_ib_count), and the program each job ran (its opcode sequence, the most frequent ones
 * printed). Only in a build made with the CMake option GT5_CULL_PROF=ON (natives_prof.txt adds the hooks,
 * GT5_CULL_PROF_HOOKS compiles them); a normal build has neither the hooks nor this code.
 * "[cull-prof]" every 5 s (cumulative): jobs, us a job, indirect branches a job, then per stamp: us a job
 * booked to it, calls a job, the runtime's indirect branches (dispatches) a job made while it ran. */
#ifdef _WIN32
#  include <intrin.h>
#  define NP_TSC() __rdtsc()
#else
#  include <x86intrin.h>
#  define NP_TSC() __rdtsc()
#endif
enum {
    NP_OPS = 45,                       /* 0..44: opcode handlers */
    NP_JOB = 45, NP_MAIN, NP_PROG, NP_AB00, NP_F6C0, NP_DF00, NP_RELOC, NP_ATOM, NP_FLUSH, NP_WRITER, NP_EMIT,
    NP_ALLOC, NP_END, NP_N
};
#ifdef GT5_CULL_PROF_HOOKS
extern SPU_THREAD_LOCAL unsigned long long t_spu_ib_count;   /* ps3recomp runtime/spu/spu_channels.c */
static const char* const k_np_name[NP_N] = {
    "op0 end/write", "op1", "op2", "op3", "op4", "op5", "op6", "op7", "op8", "op9", "op10", "op11", "op12", "op13",
    "op14", "op15", "op16", "op17", "op18", "op19", "op20", "op21", "op22", "op23", "op24", "op25", "op26", "op27",
    "op28", "op29", "op30", "op31", "op32", "op33", "op34", "op35", "op36", "op37 cull+map", "op38 cull",
    "op39 indices", "op40", "op41", "op42", "op43", "op44 mirror cull",
    "job entry", "main", "program", "0xAB00", "0xF6C0", "0xDF00", "relocate 0x4330", "ring atomic 0x41E8",
    "dma flush 0x40A0", "writer 0x1036C", "emitter 0x1011C", "out alloc 0x4650", "job end" };
static int g_np_on = -1;
static unsigned long long g_np_ticks[NP_N], g_np_calls[NP_N], g_np_ibs[NP_N], g_np_jobs, g_np_job_ticks, g_np_ib, g_np_unfinished;
typedef struct {
    const spu_context* ctx;
    unsigned long long t_job, t_last, ib0, ib_last;
    int last, nops, depth;
    int stk[16];                       /* what was running when a hooked call came in */
    unsigned char seq[24];
    unsigned long long ticks[NP_N], calls[NP_N], ibs[NP_N];
} np_thread;
static SPU_THREAD_LOCAL np_thread* t_np;
/* programs seen: a small table under a lock (profiling mode only) */
typedef struct { unsigned char seq[24]; int n; unsigned long long jobs, ticks; } np_prog;
static np_prog g_np_prog[48];
static volatile int g_np_lock;

static __attribute__((noinline)) int np_init(void)
{
    const char* e = getenv("GT5_CULL_PROF");
    const int on = e && e[0] == '1';
    if (on) { fprintf(stderr, "[cull-prof] on\n"); fflush(stderr); }
    g_np_on = on;
    return on;
}
static void np_report_ex(int force);
static void np_report(void) { np_report_ex(0); }
static void np_report_ex(int force)
{
#ifdef _WIN32
    static volatile LONG64 next;
    static double tps;
    static LARGE_INTEGER q0; static unsigned long long c0;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    const unsigned long long c = NP_TSC();
    if (!c0) { q0 = q; c0 = c; }
    const LONG64 now = (LONG64)GetTickCount64();
    LONG64 t = next;
    if (!force && (now < t || InterlockedCompareExchange64(&next, now + 5000, t) != t || !t)) return;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    if (q.QuadPart > q0.QuadPart) tps = (double)(c - c0) / ((double)(q.QuadPart - q0.QuadPart) / (double)f.QuadPart);
    if (force && tps <= 0) { /* no interval yet: the rate from a short wait */
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a); const unsigned long long ca = NP_TSC(); Sleep(50);
        QueryPerformanceCounter(&b); const unsigned long long cb = NP_TSC();
        tps = (double)(cb - ca) / ((double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart); }
    if (tps <= 0) return;
    const double jobs = (double)g_np_jobs;
    if (jobs < 1) return;
    char line[4096]; int o = 0;
    o += snprintf(line + o, sizeof line - o, "[cull-prof] jobs %.0f (unfinished %llu), %.2f us a job, %.1f indirect branches a job |", jobs,
                  g_np_unfinished, (double)g_np_job_ticks / tps * 1e6 / jobs, (double)g_np_ib / jobs);
    for (int k = 0; k < NP_N && o < (int)sizeof line - 80; k++)
        if (g_np_calls[k])
            o += snprintf(line + o, sizeof line - o, " %s %.2f us x%.2f ib%.1f;", k_np_name[k], (double)g_np_ticks[k] / tps * 1e6 / jobs,
                          (double)g_np_calls[k] / jobs, (double)g_np_ibs[k] / jobs);
    fprintf(stderr, "%s\n", line);
    /* the most frequent programs */
    while (__atomic_exchange_n(&g_np_lock, 1, __ATOMIC_ACQUIRE)) _mm_pause();
    int order[48], m = 0;
    for (int i = 0; i < 48; i++) if (g_np_prog[i].jobs) order[m++] = i;
    for (int i = 0; i < m; i++) for (int j = i + 1; j < m; j++) if (g_np_prog[order[j]].jobs > g_np_prog[order[i]].jobs) { const int x = order[i]; order[i] = order[j]; order[j] = x; }
    for (int i = 0; i < m && i < 8; i++) {
        const np_prog* p = &g_np_prog[order[i]];
        o = snprintf(line, sizeof line, "[cull-prof] program %d: %.1f %% of jobs, %.2f us a job, opcodes", i + 1, 100.0 * (double)p->jobs / jobs,
                     (double)p->ticks / tps * 1e6 / (double)p->jobs);
        for (int k = 0; k < p->n && k < 24; k++) o += snprintf(line + o, sizeof line - o, " %u", p->seq[k]);
        if (p->n > 24) o += snprintf(line + o, sizeof line - o, " ...(%d)", p->n);
        fprintf(stderr, "%s\n", line);
    }
    __atomic_store_n(&g_np_lock, 0, __ATOMIC_RELEASE);
    fflush(stderr);
#endif
}
static __attribute__((noinline)) void np_stamp(const spu_context* ctx, int ev)
{
    np_thread* s = t_np;
    if (!s) { s = (np_thread*)calloc(1, sizeof *s); if (!s) return; t_np = s; }
    const unsigned long long now = NP_TSC();
    if (ev == NP_JOB || (ev == NP_MAIN && s->ctx != ctx)) {        /* a job starts (main counts when the entry was not seen) */
        if (s->ctx) __atomic_fetch_add(&g_np_unfinished, 1ull, __ATOMIC_RELAXED);
        s->ctx = ctx; s->t_job = s->t_last = now; s->last = NP_JOB; s->ib0 = t_spu_ib_count; s->nops = 0; s->depth = 0;
        memset(s->ticks, 0, sizeof s->ticks); memset(s->calls, 0, sizeof s->calls); memset(s->ibs, 0, sizeof s->ibs);
        s->ib_last = t_spu_ib_count;
        s->calls[ev] = 1;
        return;
    }
    if (s->ctx != ctx) return;                                      /* not inside a job seen from its entry */
    s->ticks[s->last] += now - s->t_last;
    { const unsigned long long ib = t_spu_ib_count; s->ibs[s->last] += ib - s->ib_last; s->ib_last = ib; }
    s->t_last = now; s->last = ev; s->calls[ev]++;
    if (ev < NP_OPS) { if (s->nops < 24) s->seq[s->nops] = (unsigned char)ev; s->nops++; }
    if (ev != NP_END) return;
    /* the job is done: add it up */
    for (int k = 0; k < NP_N; k++) if (s->calls[k]) {
        __atomic_fetch_add(&g_np_ticks[k], s->ticks[k], __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_np_ibs[k], s->ibs[k], __ATOMIC_RELAXED);
        __atomic_fetch_add(&g_np_calls[k], s->calls[k], __ATOMIC_RELAXED);
    }
    __atomic_fetch_add(&g_np_jobs, 1ull, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_np_job_ticks, now - s->t_job, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_np_ib, t_spu_ib_count - s->ib0, __ATOMIC_RELAXED);
    while (__atomic_exchange_n(&g_np_lock, 1, __ATOMIC_ACQUIRE)) _mm_pause();
    int slot = -1;
    for (int i = 0; i < 48; i++) {
        np_prog* p = &g_np_prog[i];
        if (p->jobs && p->n == s->nops && !memcmp(p->seq, s->seq, s->nops < 24 ? s->nops : 24)) { slot = i; break; }
        if (!p->jobs && slot < 0) { slot = i; p->n = s->nops; memcpy(p->seq, s->seq, 24); break; }
    }
    if (slot >= 0) { g_np_prog[slot].jobs++; g_np_prog[slot].ticks += now - s->t_job; }
    __atomic_store_n(&g_np_lock, 0, __ATOMIC_RELEASE);
    s->ctx = NULL;
    np_report();
}
#ifdef NC_DIAG
unsigned long long nc_np_jobs(void) { return g_np_jobs; }
unsigned long long nc_np_calls(int ev) { return g_np_calls[ev]; }
unsigned long long nc_np_ib(void) { return g_np_ib; }
#endif
#define NP_STAMP(ctx, ev) do { const int _o = g_np_on; if (__builtin_expect(_o != 0, 0) && (_o > 0 || np_init())) np_stamp((ctx), (ev)); } while (0)
static inline int np_on(void) { const int o = g_np_on; return __builtin_expect(o != 0, 0) && (o > 0 || np_init()); }
/* a hooked call comes in: what was running gets its time so far, the call is counted */
static __attribute__((noinline)) void np_enter(const spu_context* ctx, int ev)
{
    np_thread* s = t_np;
    if (!s || s->ctx != ctx) { np_stamp(ctx, ev); return; }
    const unsigned long long now = NP_TSC();
    s->ticks[s->last] += now - s->t_last;
    { const unsigned long long ib = t_spu_ib_count; s->ibs[s->last] += ib - s->ib_last; s->ib_last = ib; }
    s->t_last = now;
    if (s->depth < 16) s->stk[s->depth] = s->last;
    s->depth++;
    s->last = ev; s->calls[ev]++;
    if (ev < NP_OPS) { if (s->nops < 24) s->seq[s->nops] = (unsigned char)ev; s->nops++; }
}
/* ...and returns: its time is booked, the caller runs again */
static __attribute__((noinline)) void np_leave(const spu_context* ctx)
{
    np_thread* s = t_np;
    if (!s || s->ctx != ctx || s->depth <= 0) return;
    const unsigned long long now = NP_TSC();
    s->ticks[s->last] += now - s->t_last;
    { const unsigned long long ib = t_spu_ib_count; s->ibs[s->last] += ib - s->ib_last; s->ib_last = ib; }
    s->t_last = now;
    s->depth--;
    if (s->depth < 16) s->last = s->stk[s->depth];
}

#ifdef NC_HARNESS   /* atest job: a job starts at the cull and ends where the drain stops (0x10884) */
void nc_np_begin(const spu_context* ctx) { if (np_on()) np_stamp(ctx, NP_JOB); }
void nc_np_end(const spu_context* ctx) { if (np_on()) np_stamp(ctx, NP_END); }
void nc_np_report_now(void) { np_report_ex(1); }
void nc_np_reset(void)
{
    memset(g_np_ticks, 0, sizeof g_np_ticks); memset(g_np_calls, 0, sizeof g_np_calls); memset(g_np_ibs, 0, sizeof g_np_ibs);
    g_np_jobs = g_np_job_ticks = g_np_ib = g_np_unfinished = 0;
    memset(g_np_prog, 0, sizeof g_np_prog);
}
#endif
/* the hooks: stamp in, the lifted function, stamp out (NP_HOOK_PLAIN: the job's entry, main and end,
 * a stamp on the way in only). Off: a load and a branch, then the lifted function as a tail call. */
#ifdef NATIVE_HOOKED
#  define NP_LIFTED_NAME(A) gt5_w018CAD00_spu_func_##A##_orig_sv
#  define NP_HOOK_NAME(A)   gt5_w018CAD00_spu_func_##A##_sv
#else
#  define NP_LIFTED_NAME(A) NC_LIFTED(A)
#  define NP_HOOK_NAME(A)   gt5_w018CAD00_native_##A##_sv
#endif
#define NP_HOOK(A, EV) \
    SPU_SV void NP_LIFTED_NAME(A)(spu_context* ctx); \
    SPU_SV void NP_HOOK_NAME(A)(spu_context* ctx) { \
        if (__builtin_expect(np_on(), 0)) { np_enter(ctx, EV); NP_LIFTED_NAME(A)(ctx); np_leave(ctx); return; } \
        __attribute__((musttail)) return NP_LIFTED_NAME(A)(ctx); }
#define NP_HOOK_PLAIN(A, EV) \
    SPU_SV void NP_LIFTED_NAME(A)(spu_context* ctx); \
    SPU_SV void NP_HOOK_NAME(A)(spu_context* ctx) { NP_STAMP(ctx, EV); __attribute__((musttail)) return NP_LIFTED_NAME(A)(ctx); }
NP_HOOK(00007608, 0)  NP_HOOK(00005240, 1)  NP_HOOK(00005270, 2)  NP_HOOK(000052E8, 3)  NP_HOOK(000069E0, 4)
NP_HOOK(000069C8, 5)  NP_HOOK(000063A8, 6)  NP_HOOK(00006538, 7)  NP_HOOK(000068F8, 8)
NP_HOOK(00006ED0, 10) NP_HOOK(00006888, 11) NP_HOOK(000067B0, 13) NP_HOOK(00006798, 14)
NP_HOOK(00006780, 15) NP_HOOK(00006768, 18) NP_HOOK(00006750, 19) NP_HOOK(00006428, 20) NP_HOOK(00007298, 21)
NP_HOOK(00008C40, 24) NP_HOOK(00008800, 25) NP_HOOK(00007548, 26) NP_HOOK(00004D58, 27) NP_HOOK(000068C0, 28)
NP_HOOK(00004EB8, 29) NP_HOOK(000075B0, 30) NP_HOOK(000074A0, 31) NP_HOOK(00008428, 34)
NP_HOOK(00007610, 35) NP_HOOK(00007B08, 37) NP_HOOK(00007B00, 38) NP_HOOK(000062F0, 39) NP_HOOK(00005DD8, 40)
NP_HOOK(00005F68, 41) NP_HOOK(00005AE0, 42) NP_HOOK(00006718, 43) NP_HOOK(00007C98, 44)
NP_HOOK_PLAIN(000107D8, NP_JOB) NP_HOOK_PLAIN(00004A30, NP_MAIN) NP_HOOK(00005340, NP_PROG) NP_HOOK(0000DF00, NP_DF00)
NP_HOOK_PLAIN(00010884, NP_END)
#else   /* no profiler in this build */
#define NP_STAMP(ctx, ev) ((void)0)
static inline int np_on(void) { return 0; }
static inline void np_enter(const spu_context* ctx, int ev) { (void)ctx; (void)ev; }
static inline void np_leave(const spu_context* ctx) { (void)ctx; }
#endif

/* ---- the code the allocator, the copy and the triangle loop below were written for --------------------
 * Each runs only where the job's code is byte for byte that code (FNV-1a 64 of the range, the same in all
 * 400 dumps of R1b: no data in these ranges). Checked at a function's first calls in the process (the code
 * is in local store then: it is running); after 16 that differ the lifted code stays for the run:
 * "[cull-code] ... lifted". The direct branches need no such check: they take this lift's own entries. */
enum { NCC_ALLOC, NCC_COPY, NCC_LOOP, NCC_N };
static const struct { uint32_t a, e; uint64_t h; const char* what; } k_ncc[NCC_N] = {
    { 0x40A0u, 0x4848u, 0x3127E29A340EAC36ull, "allocator (0x40A0-0x4848)" },
    { 0x8F40u, 0x9080u, 0x53742DE88AE21ACFull, "copy (0x8F40-0x9080)" },
    { 0xAB00u, 0xB5D8u, 0x224606AA954AFDFCull, "cull (0xAB00-0xB5D8)" },
};
static volatile int g_ncc[NCC_N];            /* 0 not checked yet, 1 the same, -n differed n times (-16: lifted) */
static __attribute__((noinline)) int ncc_check(const spu_context* ctx, int k)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint32_t i = k_ncc[k].a; i < k_ncc[k].e; i++) h = (h ^ ctx->ls[i]) * 0x100000001B3ull;
    if (h == k_ncc[k].h) { g_ncc[k] = 1; return 1; }
    const int f = g_ncc[k] - 1;
    g_ncc[k] = f;
    if (f == -16) { fprintf(stderr, "[cull-code] the job's %s is not the code its native one was written for: lifted\n", k_ncc[k].what); fflush(stderr); }
    return 0;
}
static inline int ncc_ok(const spu_context* ctx, int k)
{
    const int v = g_ncc[k];
    return NC_LIKELY(v == 1) ? 1 : v <= -16 ? 0 : ncc_check(ctx, k);
}

/* ---- the recorder's hooks (GT5_JOB_DUMP above), with these handlers' profiler stamps ---------------- */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w018CAD00_spu_func_00008880_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_000072A0_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_00008E80_orig_sv(spu_context* ctx);
#else
SPU_SV void NC_LIFTED(00008880)(spu_context* ctx);
SPU_SV void NC_LIFTED(000072A0)(spu_context* ctx);
SPU_SV void NC_LIFTED(00008E80)(spu_context* ctx);
#endif
#define JD_HOOK(A, K, OP) \
    SPU_SV void NC_NATIVE(A)(spu_context* ctx) { \
        const int _j = g_jd_on; \
        if (NC_UNLIKELY(_j != 0) && (_j > 0 || jd_init())) jd_dump(ctx, K, OP, 0x##A##u); \
        if (NC_UNLIKELY(np_on())) { np_enter(ctx, OP); NC_ORIG(A)(ctx); np_leave(ctx); return; } \
        __attribute__((musttail)) return NC_ORIG(A)(ctx); }
JD_HOOK(00008880, 0, 9)
JD_HOOK(000072A0, 1, 12)
JD_HOOK(00008E80, 2, 33)

/* ---- the job's output allocator as native code: 0x4650, 0x4330, 0x41E8, 0x40A0 -----------------------
 * ctx = the job's context in local store (0x1F000 in the game), P = W(ctx+0x1D0).
 *  0x4650 alloc(ctx, size, align, relptr, cursor) -> LS address. A bump allocator in one of two staging
 *     buffers of 30720 bytes: ctx+0xC0 buffer index 0/4, ctx+0xC4/0xC8 = tag << 24 | LS address,
 *     ctx+0xD0 next free byte, ctx+0xE0 start of what is not written out yet. relptr != 0: the word at
 *     relptr gets the allocation's offset in that window and goes on a 16-slot relocation list (ctx+0x430
 *     count, ctx+0x440 slots). A full buffer (or list) is written out first (0x4330, when relptr or cursor
 *     is set), then the other buffer is taken once its last PUT is done (a tag wait). A buffer whose
 *     descriptor is 0 comes from the job manager (LS table at 0x118A0: execute commands, buffer tag).
 *  0x4330 relocate(ctx, cursor): main-memory space for the window [start, next): at *cursor (which
 *     advances) when cursor != 0; else W(P+40) != 0: in this SPU's ring (ctx+0x80 start, +0x84 end, +0x88
 *     put, +0x8C the EA of the RSX's label, +0x98 a tag): the label is read by a 4-byte DMA into ctx+0 and
 *     the space taken when the RSX is clear of it, else 2400 decrementer reads and read again; W(P+40) == 0:
 *     the shared buffer (0x41E8). Every listed word += offset - W(P+20), the list emptied, then
 *     0x40A0(ctx, offset, 0).
 *  0x41E8 shared(ctx, size): GETLLAR/PUTLLC loop on the line at W(P+0x10) {put, +16 end, +0x20 bytes
 *     refused}: put += size, or when full: 0 and ctx+0x420 = -1 (then nothing is written out).
 *  0x40A0 flush(ctx, ea, keep): PUT the window to ea in 16 KB pieces with the buffer's tag; keep == 0 first
 *     moves start and next to the next 128-byte boundary; ctx+0x420 != 0: no PUT.
 * In the game (R2): W(P+40) != 0, so the ring; 0x41E8 never runs. Every DMA and tag poll sits between bid
 * and bie, and the lifted code goes back to the dispatcher for each of those: two indirect branches a DMA
 * or poll. Here: the same channel writes and reads through the runtime (spu_wrch / spu_rdch: the same
 * DMAs, PUTs, tags, in the same order), without the dispatcher; the shared line updated under its lock
 * once, the reservation, the notify and the commit hooks as a PUTLLC's. The stack frames the lifted
 * functions write are written alike; r3 (the whole quadword), r0, r1, r80-r127 and int_enable come out
 * as the lifted code leaves them, and their callers read no other register after these calls.
 * The lifted function runs instead, for the whole call, when: the job's code at 0x40A0-0x4848 is not the
 * code this was written for (ncc_ok), an interrupt is pending, an LS diagnostic
 * is on, the context or stack is not where expected, a buffer has to come from the job manager, a tag to
 * wait on has a parked list or is not done, the RSX is not clear of the ring space (the lifted code
 * waits for it), the shared line's page is not known committed. Inside 0x4650 a busy ring runs the
 * lifted 0x4330 and carries on.
 * GT5_ALLOC (read once): 0 the lifted functions (the default since D69); 1 native; 2 native, every result checked
 * (inside the buffer, aligned, after the one before; ring space inside the ring, at the put pointer or
 * at the ring's start after a wrap, the label clear of it; the shared put grown by the size):
 * "[cull-alloc] CHECK ..." for the first 8 that fail; 3 the lifted functions with the ring-space checks of 2 on
 * what the lifted 0x4330 takes ("... (lifted)"): the same checks on the game's own code (the previous allocation
 * of the same ring is kept per ring, not per worker: see na_ring_check). SPU_LOCKSTEP set: 0.
 * GT5_CULL_PROF=1 or GT5_ALLOC_STAT=1: "[cull-alloc] ..." every 5 s (cumulative). PS3_AB_PERIOD=<s>
 * PS3_AB_WHAT=alloc: GT5_ALLOC's mode and 0 alternate ([ab] lines: frames and cores busy in each). */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w018CAD00_spu_func_00004650_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_00004330_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_000041E8_orig_sv(spu_context* ctx);
SPU_SV void gt5_w018CAD00_spu_func_000040A0_orig_sv(spu_context* ctx);
#else
SPU_SV void NC_LIFTED(00004650)(spu_context* ctx);
SPU_SV void NC_LIFTED(00004330)(spu_context* ctx);
SPU_SV void NC_LIFTED(000041E8)(spu_context* ctx);
SPU_SV void NC_LIFTED(000040A0)(spu_context* ctx);
#endif
/* the runtime (spu_coherency.c, spu_channels.c) */
extern uint8_t* vm_base;
extern uint8_t g_vm_page_bitmap[65536 / 8];
void spu_lockline_lock_line(uint32_t ea);
void spu_lockline_unlock_line(uint32_t ea);
void spu_coh_reserve(spu_context* ctx, uint32_t ea);
void spu_coh_hold_drop(spu_context* ctx);
void spu_coh_notify_write(uint32_t ea);
void spu_lockguard_check(spu_context* ctx, uint32_t ea, const uint8_t* p, uint32_t n, const char* what);
void spu_taskset_line_committed(uint32_t line);
extern void (*g_spu_line_commit_hook)(uint32_t line);
extern volatile unsigned g_spu_putllc_count;
uint32_t spu_ll_version(uint32_t ea);                 /* SPU_LL_LAZY: versioned lines (spu_coherency.c) */
extern unsigned long g_spu_lr_raise;
#ifndef SPU_EVENT_LR
#define SPU_EVENT_LR 0x400u                           /* spu_coherency.h: the lost-reservation event */
#endif
/* spu_channels.c's spu_ll_settle, as its GETLLAR and PUTLLC run it: a lazy reservation (the one this
 * GETLLAR replaces) whose line was written since is lost now, with its event */
static inline void na_ll_settle(spu_context* ctx)
{
    if (NC_UNLIKELY(ctx->resv_lazy != 0)) {
        if (ctx->resv_valid && spu_ll_version(ctx->resv_ea) != ctx->resv_ver) {
            ctx->resv_valid = 0;
            SPU_EVENT_SET(ctx, SPU_EVENT_LR);
            __atomic_fetch_add(&g_spu_lr_raise, 1, __ATOMIC_RELAXED);
        }
        if (!ctx->resv_valid) ctx->resv_lazy = 0;
    }
}

enum {
    NA_CALLS, NA_FAST, NA_SWITCH, NA_RELOC, NA_CURSOR, NA_RING, NA_SHARED, NA_FLUSH, NA_PUTS, NA_PUTBYTES, NA_NOPUT,
    NA_GETS, NA_POLLS, NA_SHFULL, NA_SHRETRY, NA_WRAPS, NA_L_JM, NA_L_GUARD, NA_L_TAG, NA_L_BUSY, NA_L_PAGE, NA_NESTED,
    NA_BAD, NA_T_ALLOC, NA_T_RELOC, NA_T_FLUSH, NA_T_SHARED, NA_T_LIFTED, NA_RINGMOVED, NA_N
};
static unsigned long long g_na[NA_N];
static SPU_THREAD_LOCAL unsigned long long t_na[NA_N];
static SPU_THREAD_LOCAL unsigned t_na_n;
static int g_na_mode = -1, g_na_stat, g_na_ab;
#ifdef NA_BREAK            /* harness only: deliberately broken variants (1 no lock, 2 alignment, 3 no notify, 4 relocation) */
int g_na_break;
unsigned long long na_stat_get(int k) { return __atomic_load_n(&g_na[k], __ATOMIC_RELAXED) + t_na[k]; }
#endif

static __attribute__((noinline)) int na_init(void)
{
    static volatile long said;
    const char* e = getenv("GT5_ALLOC");
    int m = 0;   /* off until the ring-space check (GT5_ALLOC=2: 681 "overlaps the last" in one race, D69) is explained */
    if (e && e[0] >= '0' && e[0] <= '3' && e[1] == 0) m = e[0] - '0';
    const char* ls = getenv("SPU_LOCKSTEP");
    if (m && ls && *ls && *ls != '0') m = 0;
    const char* s = getenv("GT5_ALLOC_STAT"); const char* p = getenv("GT5_CULL_PROF");
    g_na_stat = (s && s[0] == '1') || (p && p[0] == '1');
    g_na_ab = m != 0 && ps3_ab_on("alloc") >= 0;
    if ((m != 1 || g_na_stat || g_na_ab) && __atomic_exchange_n(&said, 1, __ATOMIC_SEQ_CST) == 0) {
        fprintf(stderr, "[cull-alloc] GT5_ALLOC=%d%s%s\n", m, m == 3 ? " (lifted, its ring space checked)" : m == 2 ? " (native, every result checked)" : m ? " (native)" : " (lifted)",
                g_na_ab ? ", alternated with 0 (PS3_AB_WHAT=alloc)" : "");
        fflush(stderr);
    }
    g_na_mode = m;
    return m;
}
/* PS3_AB_PERIOD=<s> PS3_AB_WHAT=alloc: GT5_ALLOC's mode in the "on" periods, 0 in the "off" ones ([ab] lines) */
static __attribute__((noinline)) int na_ab_mode(int m)
{
    static SPU_THREAD_LOCAL unsigned n;
    static SPU_THREAD_LOCAL int on;
    if ((n++ & 63u) == 0) on = ps3_ab_on("alloc") == 1;
    return on ? m : 0;
}
static inline int na_mode(void)       /* 0 = run the lifted function (GT5_ALLOC=3: lifted too, 0x4330 watched) */
{
    int m = g_na_mode;
    if (NC_UNLIKELY(m < 0)) m = na_init();
    if (NC_UNLIKELY(m == 3)) return 0;
    return NC_LIKELY(!g_na_ab) ? m : na_ab_mode(m);
}
#define NA_ADD(k, v) do { if (NC_UNLIKELY(g_na_stat)) t_na[k] += (v); } while (0)

static void na_report(void)
{
#ifdef _WIN32
    static volatile LONG64 next;
    static double tps;
    static LARGE_INTEGER q0; static unsigned long long c0;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    const unsigned long long cn = NP_TSC();
    if (!c0) { q0 = q; c0 = cn; }
    const LONG64 now = (LONG64)GetTickCount64();
    LONG64 t = next;
    if (now < t || InterlockedCompareExchange64(&next, now + 5000, t) != t || !t) return;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    if (q.QuadPart > q0.QuadPart) tps = (double)(cn - c0) / ((double)(q.QuadPart - q0.QuadPart) / (double)f.QuadPart);
    if (tps <= 0) return;
    unsigned long long v[NA_N];
    for (int k = 0; k < NA_N; k++) v[k] = __atomic_load_n(&g_na[k], __ATOMIC_RELAXED);
    const double ns = 1e9 / tps;
#define NA_PER(t, n) ((n) ? (double)v[t] * ns / (double)(n) : 0.0)
    fprintf(stderr, "[cull-alloc] 0x4650 %llu (fast %llu, switch %llu) %.0f ns | lifted: job manager %llu, guard %llu, tag %llu, ring busy %llu, page %llu, %.0f ns |"
                    " 0x4330 %llu (cursor %llu, ring %llu, shared %llu, wraps %llu) %.0f ns, label reads %llu, tag polls %llu, busy ring in 0x4650 %llu |"
                    " 0x40A0 %llu %.0f ns, PUTs %llu (%.0f bytes), none %llu | shared full %llu, retries %llu, %.0f ns | ring put moved between allocations %llu | checks failed %llu\n",
            v[NA_CALLS], v[NA_FAST], v[NA_SWITCH], NA_PER(NA_T_ALLOC, v[NA_FAST] + v[NA_SWITCH]),
            v[NA_L_JM], v[NA_L_GUARD], v[NA_L_TAG], v[NA_L_BUSY], v[NA_L_PAGE],
            NA_PER(NA_T_LIFTED, v[NA_L_JM] + v[NA_L_GUARD] + v[NA_L_TAG] + v[NA_L_BUSY] + v[NA_L_PAGE]),
            v[NA_RELOC], v[NA_CURSOR], v[NA_RING], v[NA_SHARED], v[NA_WRAPS], NA_PER(NA_T_RELOC, v[NA_RELOC]), v[NA_GETS], v[NA_POLLS], v[NA_NESTED],
            v[NA_FLUSH], NA_PER(NA_T_FLUSH, v[NA_FLUSH]), v[NA_PUTS], v[NA_PUTS] ? (double)v[NA_PUTBYTES] / (double)v[NA_PUTS] : 0.0, v[NA_NOPUT],
            v[NA_SHFULL], v[NA_SHRETRY], NA_PER(NA_T_SHARED, v[NA_SHARED]), v[NA_RINGMOVED], v[NA_BAD]);
#undef NA_PER
    fflush(stderr);
#endif
}
static __attribute__((noinline)) void na_flush_stats(void)
{
    for (int k = 0; k < NA_N; k++) if (t_na[k]) { __atomic_fetch_add(&g_na[k], t_na[k], __ATOMIC_RELAXED); t_na[k] = 0; }
    t_na_n = 0;
    na_report();
}
static inline void na_done(unsigned long long t0, int slot)
{
    if (NC_LIKELY(!g_na_stat)) return;
    t_na[slot] += NP_TSC() - t0;
    if (++t_na_n >= 64) na_flush_stats();
}
static __attribute__((noinline)) void na_bad(const char* what, uint32_t a, uint32_t b, uint32_t c)
{
    static volatile long n;
    NA_ADD(NA_BAD, 1);
    if (__atomic_add_fetch(&n, 1, __ATOMIC_RELAXED) <= 8) { fprintf(stderr, "[cull-alloc] CHECK %s: %08X %08X %08X\n", what, a, b, c); fflush(stderr); }
}

/* ---- local store as the SPU sees it ---- */
static inline uint32_t na_rd(const uint8_t* ls, uint32_t a)       /* lq + rotqby by a: the word starting at a, inside its quadword */
{
    const uint32_t q = a & NC_LSM & ~15u, o = a & 15u;
    if (NC_LIKELY(o <= 12u)) { uint32_t v; memcpy(&v, ls + q + o, 4); return __builtin_bswap32(v); }
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) v = (v << 8) | ls[q + ((o + i) & 15u)];
    return v;
}
static inline void na_wr(uint8_t* ls, uint32_t a, uint32_t v)     /* cwd + shufb + stq: the word slot of a in its quadword */
{
    v = __builtin_bswap32(v);
    memcpy(ls + (a & NC_LSM & ~15u) + (a & 12u), &v, 4);
}
static inline u128 na_lq(const uint8_t* ls, uint32_t a)
{
    u128 r; const uint8_t* p = ls + (a & NC_LSM & ~15u);
    for (int i = 0; i < 4; i++) { uint32_t w; memcpy(&w, p + 4 * i, 4); r._u32[i] = __builtin_bswap32(w); }
    return r;
}
static inline void na_sq(uint8_t* ls, uint32_t a, u128 v)
{
    uint8_t* p = ls + (a & NC_LSM & ~15u);
    for (int i = 0; i < 4; i++) { const uint32_t w = __builtin_bswap32(v._u32[i]); memcpy(p + 4 * i, &w, 4); }
}
static inline u128 na_rot(u128 q, uint32_t s)                      /* rotqby */
{
    uint8_t b[32];
    for (int i = 0; i < 4; i++) { const uint32_t w = __builtin_bswap32(q._u32[i]); memcpy(b + 4 * i, &w, 4); }
    memcpy(b + 16, b, 16);
    s &= 15u;
    u128 r;
    for (int i = 0; i < 4; i++) { uint32_t w; memcpy(&w, b + s + 4 * i, 4); r._u32[i] = __builtin_bswap32(w); }
    return r;
}
static inline u128 na_ai(u128 v, uint32_t k) { for (int i = 0; i < 4; i++) v._u32[i] += k; return v; }
static inline u128 na_u(uint32_t w0) { u128 r; r._u32[0] = w0; r._u32[1] = r._u32[2] = r._u32[3] = 0; return r; }
static inline uint32_t na_shl1(uint32_t n) { n &= 63u; return n < 32u ? 1u << n : 0u; }   /* shl of 1 by n (SPU: counts 32..63 give 0) */

/* ---- the channels, through the runtime as the lifted code does ---- */
static inline void na_wrch(spu_context* ctx, uint32_t ch, uint32_t v) { spu_wrch(ctx, ch, na_u(v)); }
static inline int na_poll(spu_context* ctx, uint32_t mask)          /* wrch TagMask, wrch TagUpdate 0, rdch TagStat == mask */
{
    na_wrch(ctx, MFC_WrTagMask, mask);
    na_wrch(ctx, MFC_WrTagUpdate, 0);
    NA_ADD(NA_POLLS, 1);
    return spu_rdch(ctx, MFC_RdTagStat)._u32[0] == mask;
}
/* is every tag of mask done (and none with a parked list)? Leaves the channels as they were. */
static int na_tags_done(spu_context* ctx, uint32_t mask)
{
    if (ctx->list_stall_mask & mask) return 0;
    if (!mask) return 1;
    const uint32_t m0 = ctx->mfc_tag_mask, s0 = ctx->mfc_tag_status;
    na_wrch(ctx, MFC_WrTagMask, mask);
    na_wrch(ctx, MFC_WrTagUpdate, 0);
    const int done = spu_rdch(ctx, MFC_RdTagStat)._u32[0] == mask;
    ctx->mfc_tag_mask = m0; ctx->mfc_tag_status = s0;
    return done;
}
static inline void na_dma(spu_context* ctx, uint32_t lsa, uint32_t ea, uint32_t size, uint32_t tag, uint32_t cmd)
{
    na_wrch(ctx, MFC_LSA, lsa); na_wrch(ctx, MFC_EAL, ea); na_wrch(ctx, MFC_Size, size);
    na_wrch(ctx, MFC_TagID, tag); na_wrch(ctx, MFC_Cmd, cmd);
}
static inline int na_committed(uint32_t ea, uint32_t n)
{
    const uint32_t a = ea >> 16, b = (ea + n - 1u) >> 16;
    return ((g_vm_page_bitmap[a >> 3] >> (a & 7)) & 1) && ((g_vm_page_bitmap[b >> 3] >> (b & 7)) & 1);
}

/* the guards every entry shares: 1 = the native code may run */
static inline int na_ok(const spu_context* ctx)
{
    const uint32_t c = ctx->gpr[3]._u32[0], sp = ctx->gpr[1]._u32[0];
    return !NATIVE_GEO_IRQ_PENDING(ctx) && (g_spu_ls_rdiag | g_spu_ls_wdiag) == 0 &&
           (c & 127u) == 0 && c < SPU_LS_SIZE - 0x480u && (sp & 15u) == 0 && sp >= 0x400u && sp < SPU_LS_SIZE - 0x20u &&
           ncc_ok(ctx, NCC_ALLOC);
}

/* ---- 0x40A0 ---- */
static int na_flush(spu_context* ctx, uint32_t c, uint32_t ea, uint32_t keep)        /* returns 1 when it PUT (a bie ran) */
{
    uint8_t* const ls = ctx->ls;
    const unsigned long long t0 = g_na_stat ? NP_TSC() : 0;
    NA_ADD(NA_FLUSH, 1);
    uint32_t start = na_rd(ls, c + 0xE0u);
    const uint32_t cur = na_rd(ls, c + 0xD0u);
    uint32_t n = cur - start;
    if (n == 0) { na_done(t0, NA_T_FLUSH); return 0; }
    if (keep == 0) {
        const uint32_t a = (cur + 127u) & ~127u;
        na_wr(ls, c + 0xD0u, a); na_wr(ls, c + 0xE0u, a);
    }
    if (na_rd(ls, c + 0x420u) != 0) { NA_ADD(NA_NOPUT, 1); na_done(t0, NA_T_FLUSH); return 0; }
    const uint32_t idx = na_rd(ls, c + 0xC0u);
    const uint32_t tag = na_rd(ls, c + (idx & ~3u) + 196u) >> 24;
    while (n > 16384u) {
        na_dma(ctx, start, ea, 16384u, tag, 0x20u);
        n -= 16384u; ea += 16384u; start += 16384u;
        NA_ADD(NA_PUTS, 1); NA_ADD(NA_PUTBYTES, 16384u);
    }
    na_dma(ctx, start, ea, n, tag, 0x20u);
    NA_ADD(NA_PUTS, 1); NA_ADD(NA_PUTBYTES, n);
    na_done(t0, NA_T_FLUSH);
    return 1;
}

/* ---- 0x41E8: the shared buffer, under the line's lock once ---- */
/* s1 = r1 at its entry. Writes its frame, returns r3 (whole quadword). *lift = 1: nothing done, run the lifted one. */
static u128 na_shared(spu_context* ctx, uint32_t c, uint32_t size, u128 s1, int* lift)
{
    uint8_t* const ls = ctx->ls;
    const uint32_t P = na_rd(ls, c + 0x1D0u);
    const uint32_t ea_raw = na_rd(ls, P + 0x10u), line = ea_raw & ~127u;
    if (!vm_base || !na_committed(line, 128u)) { *lift = 1; NA_ADD(NA_L_PAGE, 1); return s1; }
    const unsigned long long t0 = g_na_stat ? NP_TSC() : 0;
    NA_ADD(NA_SHARED, 1);
    const uint32_t sz = (size + 127u) & ~127u;
    na_sq(ls, s1._u32[0] - 0x40u, s1);                          /* stqd $1, -0x40($1) */
    const uint32_t s2 = s1._u32[0] - 64u;
    uint8_t* const mem = vm_base + line;
    uint8_t* const lsl = ls + (c & NC_LSM);
#ifdef NA_BREAK
    const int lock = g_na_break != 1;
#else
    const int lock = 1;
#endif
    na_ll_settle(ctx);                                           /* the reservation this GETLLAR replaces */
    ctx->resv_lazy = 0;                                          /* an ordinary one (the runtime's locked GETLLAR) */
    if (lock) spu_lockline_lock_line(line);
    spu_coh_reserve(ctx, line);                                  /* what the GETLLAR leaves: the line marked, ctx a holder */
    for (;;) {
        memcpy(lsl, mem, 128);                                    /* GETLLAR */
        memcpy(ctx->resv_line, lsl, 128);
        ctx->resv_ea = line; ctx->resv_valid = 1; ctx->atomic_stat = 4; ctx->dbg_getllar++;
        const uint32_t put = na_rd(ls, c), end = na_rd(ls, c + 16u);
        na_wr(ls, s2 + 0x20u, put);
        na_wr(ls, s2 + 0x30u, end);
        if (put + sz > end) {
            na_wr(ls, s2 + 0x20u, 0);
            na_wr(ls, c + 0x20u, na_rd(ls, c + 0x20u) + sz);
        } else {
            na_wr(ls, c, put + sz);
        }
        g_spu_putllc_count++;                                     /* PUTLLC */
#ifdef NA_BREAK
        if (!lock || (ctx->resv_valid && ctx->resv_ea == line && memcmp(mem, ctx->resv_line, 128) == 0)) {   /* 1: a plain read-modify-write */
#else
        if (ctx->resv_valid && ctx->resv_ea == line && memcmp(mem, ctx->resv_line, 128) == 0) {
#endif
            spu_lockguard_check(ctx, line, lsl, 128u, "PUTLLC");
            memcpy(mem, lsl, 128);
            ctx->resv_valid = 0;
            spu_coh_hold_drop(ctx);
#ifdef NA_BREAK
            if (g_na_break != 3)
#endif
            spu_coh_notify_write(line);
            ctx->atomic_stat = 0;
            break;
        }
        ctx->atomic_stat = 1;                                     /* a store landed in between (not under the lock): again */
        NA_ADD(NA_SHRETRY, 1);
    }
    ctx->resv_valid = 0;
    if (lock) spu_lockline_unlock_line(line);
    spu_taskset_line_committed(line);
    if (g_spu_line_commit_hook) g_spu_line_commit_hook(line);
    ctx->mfc_lsa = c; ctx->mfc_eal = ea_raw;                     /* the channel registers the two commands left */
    const u128 r3 = na_lq(ls, s2 + 0x20u);
    if (r3._u32[0] == 0) { na_wr(ls, c + 0x420u, 0xFFFFFFFFu); NA_ADD(NA_SHFULL, 1); }
    ctx->int_enable = 1;                                          /* bie */
    na_done(t0, NA_T_SHARED);
    *lift = 0;
    return r3;
}

/* ---- 0x4330 ---- */
/* GT5_ALLOC=2 (the native's ring space) and 3 (the lifted code's): inside the ring, clear of the RSX's label, and
 * clear of the space the ring's previous allocation took (kept per ring: each ring is one SPU's, its jobs take
 * space one after the other, but not always on the same worker thread, so a worker's own last space can be a lap
 * old). An allocation whose put is not where the ring's previous one ended (the ring's record was changed
 * between two jobs by something else) is counted ("ring put moved"), not failed. */
static struct { uint32_t rbase, p, e; } g_na_rings[32];
static volatile long g_na_rings_lock;
static __attribute__((noinline)) void na_ring_check(uint32_t put, uint32_t nend, uint32_t rbase, uint32_t rend, uint32_t get, const char* who)
{
    char w[96];
    if (put < rbase || nend > rend || nend <= put) { snprintf(w, sizeof w, "ring space outside the ring%s", who); na_bad(w, put, nend, rend); }
    if (!(get <= put || get > nend)) { snprintf(w, sizeof w, "ring space not clear of the RSX%s", who); na_bad(w, get, put, nend); }
    while (__atomic_exchange_n(&g_na_rings_lock, 1, __ATOMIC_ACQUIRE)) _mm_pause();
    unsigned i = 0;
    while (i < 32u && g_na_rings[i].rbase && g_na_rings[i].rbase != rbase) i++;
    uint32_t lp = 0, le = 0;
    if (i < 32u) {
        if (g_na_rings[i].rbase == rbase) { lp = g_na_rings[i].p; le = g_na_rings[i].e; }
        g_na_rings[i].rbase = rbase; g_na_rings[i].p = put; g_na_rings[i].e = nend;
    }
    __atomic_store_n(&g_na_rings_lock, 0, __ATOMIC_RELEASE);
    if (le) {
        if (put != le && put != rbase) NA_ADD(NA_RINGMOVED, 1);
        if (put < le && nend > lp) { snprintf(w, sizeof w, "ring space overlaps the ring's last%s", who); na_bad(w, put, lp, le); }
    }
}
enum { NA_R_DONE, NA_R_LIFT };
/* r0v, r1v, r80v, r81v: those registers at 0x4330's entry. */
static int na_reloc(spu_context* ctx, uint32_t c, uint32_t cursor, u128 r0v, u128 r1v, u128 r80v, u128 r81v, int* bie)
{
    uint8_t* const ls = ctx->ls;
    const unsigned long long t0 = g_na_stat ? NP_TSC() : 0;
    const uint32_t s0 = r1v._u32[0];
    na_sq(ls, s0 - 0x10u, r80v); na_sq(ls, s0 + 0x10u, r0v); na_sq(ls, s0 - 0x20u, r81v); na_sq(ls, s0 - 0x40u, r1v);
    const uint32_t cur = na_rd(ls, c + 0xD0u), start = na_rd(ls, c + 0xE0u);
    const uint32_t n = cur - start;
    if (n == 0) { na_done(t0, NA_T_RELOC); return NA_R_DONE; }
    uint32_t off;
    const uint32_t P = na_rd(ls, c + 0x1D0u);
    if (cursor != 0) {
        off = na_rd(ls, cursor);
        na_wr(ls, cursor, off + n);
        NA_ADD(NA_CURSOR, 1);
    } else if (na_rd(ls, P + 40u) == 0) {
        int lift = 0;
        const u128 r = na_shared(ctx, c, n, na_ai(r1v, (uint32_t)-64), &lift);
        if (lift) return NA_R_LIFT;
        off = r._u32[0];
        *bie = 1;
    } else {
        const uint32_t tagw = na_rd(ls, c + 152u), mw = na_shl1(tagw);
        if ((ctx->list_stall_mask & mw) || !na_poll(ctx, mw)) { NA_ADD(NA_L_TAG, 1); return NA_R_LIFT; }
        const uint32_t rbase = na_rd(ls, c + 0x80u), rend = na_rd(ls, c + 132u), lab = na_rd(ls, c + 140u);
        uint32_t put = na_rd(ls, c + 136u);
        const uint32_t sz = (n + 127u) & ~127u;
        const uint32_t wend = rbase + sz;
        uint32_t nend = put + sz;
        const int wrap = nend > rend;
        na_dma(ctx, c, lab, 4u, tagw, 0x40u);
        NA_ADD(NA_GETS, 1);
        if (!na_poll(ctx, mw)) { NA_ADD(NA_L_TAG, 1); return NA_R_LIFT; }
        const uint32_t get = na_rd(ls, c);
        const int ok = (get <= put || get > nend) && (!wrap || get > wend);
        *bie = 1;
        if (!ok) { NA_ADD(NA_L_BUSY, 1); return NA_R_LIFT; }
        if (wrap) { nend = wend; put = rbase; NA_ADD(NA_WRAPS, 1); }
        if (NC_UNLIKELY(g_na_mode == 2)) na_ring_check(put, nend, rbase, rend, get, "");
        na_wr(ls, c + 136u, nend);
        off = put;
        NA_ADD(NA_RING, 1);
    }
    /* relocation */
    const uint32_t delta = off - na_rd(ls, P + 20u);
    for (uint32_t i = 0; i < 16u; i++) {
        const uint32_t sa = c + 0x440u + 4u * i, p = na_rd(ls, sa);
        if (p == 0) continue;
#ifdef NA_BREAK
        na_wr(ls, p, na_rd(ls, p) + delta + (g_na_break == 4 && i == 3));
#else
        na_wr(ls, p, na_rd(ls, p) + delta);
#endif
        na_wr(ls, sa, 0);
    }
    na_wr(ls, c + 0x430u, 0);
    NA_ADD(NA_RELOC, 1);
    na_done(t0, NA_T_RELOC);
    if (na_flush(ctx, c, off, 0)) *bie = 1;                      /* the tail call */
    return NA_R_DONE;
}

/* ---- 0x4650 ---- */
static int na_alloc(spu_context* ctx)                                /* 1 = done (r3 set), 0 = run the lifted one */
{
    uint8_t* const ls = ctx->ls;
    const u128 R3 = ctx->gpr[3], R4 = ctx->gpr[4], R5 = ctx->gpr[5], R6 = ctx->gpr[6], R7 = ctx->gpr[7];
    const u128 R0 = ctx->gpr[0], R1 = ctx->gpr[1];
    const uint32_t c = R3._u32[0];
    const u128 IDX = na_lq(ls, c + 0xC0u), D = na_lq(ls, c + 0xD0u);  /* c is 128-aligned: no rotation */
    const uint32_t idx = IDX._u32[0], cur = D._u32[0];
    const uint32_t size16 = (R4._u32[0] + 15u) & ~15u;
    u128 res;                                                         /* r11 */
    for (int i = 0; i < 4; i++) res._u32[i] = (R5._u32[i] + D._u32[i] - 1u) & (0u - R5._u32[i]);
#ifdef NA_BREAK
    if (g_na_break == 2) res._u32[0] += 16u;
#endif
    const uint32_t aligned = res._u32[0];
    const uint32_t q = idx & ~3u;
    const u128 DESC = na_rot(na_lq(ls, c + q + 196u), c + q + 196u);
    const uint32_t desc = DESC._u32[0], base = desc & 0x3FFFFu;
    if (base == 0) { NA_ADD(NA_L_JM, 1); return 0; }
    const uint32_t remaining = base - aligned + 30720u;
    const int full = size16 > remaining || na_rd(ls, c + 0x430u) > 15u;
    const uint32_t relptr = R6._u32[0], cursor = R7._u32[0];
    uint32_t idx2 = 0, q2 = 0, mask2 = 0;
    if (full) {
        idx2 = 4u - idx; q2 = idx2 & ~3u;
        const uint32_t desc2 = na_rd(ls, c + q2 + 196u);
        if ((desc2 & 0x3FFFFu) == 0) { NA_ADD(NA_L_JM, 1); return 0; }
        mask2 = na_shl1(desc2 >> 24);
        /* the PUT's tag and the one waited on: done, no parked list; no relocated word may land on the descriptors */
        if (!na_tags_done(ctx, mask2) || (ctx->list_stall_mask & na_shl1(desc >> 24))) { NA_ADD(NA_L_TAG, 1); return 0; }
        if ((relptr | cursor) != 0) {
            if (cursor - (c + 0xC0u) < 16u) { NA_ADD(NA_L_GUARD, 1); return 0; }
            for (uint32_t i = 0; i < 16u; i++) if (na_rd(ls, c + 0x440u + 4u * i) - (c + 0xC0u) < 16u) { NA_ADD(NA_L_GUARD, 1); return 0; }
        }
    }
    const unsigned long long t0 = g_na_stat ? NP_TSC() : 0;
    /* the frame */
    const uint32_t t = R1._u32[0];
    na_sq(ls, t - 0x30u, ctx->gpr[82]); na_sq(ls, t - 0x40u, ctx->gpr[83]); na_sq(ls, t - 0x50u, ctx->gpr[84]);
    na_sq(ls, t - 0x80u, ctx->gpr[87]); na_sq(ls, t - 0x10u, ctx->gpr[80]); na_sq(ls, t - 0x20u, ctx->gpr[81]);
    na_sq(ls, t - 0x60u, ctx->gpr[85]); na_sq(ls, t - 0x70u, ctx->gpr[86]); na_sq(ls, t + 0x10u, R0);
    na_sq(ls, t - 0xC0u, R1);
    na_wr(ls, c + 0xD0u, aligned);
    uint32_t idxc = idx, qc = q, descc = desc;
    int bie = 0;
    if (full) {
        if ((relptr | cursor) != 0) {
            const u128 T1 = na_ai(R1, (uint32_t)-192);
            if (na_reloc(ctx, c, cursor, spu_link(0x4740), T1, R3, R6, &bie) == NA_R_LIFT) {
                /* the lifted 0x4330, called as 0x4650 calls it */
                NA_ADD(NA_NESTED, 1);
                ctx->gpr[3] = R3; ctx->gpr[4] = R7; ctx->gpr[0] = spu_link(0x4740); ctx->gpr[1] = T1;
                const u128 g80 = ctx->gpr[80], g81 = ctx->gpr[81];
                ctx->gpr[80] = R3; ctx->gpr[81] = R6;
                { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++;
                  NC_ORIG(00004330)(ctx); spu_drain_call(ctx, 0x4740); ctx->host_depth--; spu_img_restore(ctx, _si); }
                ctx->gpr[0] = R0; ctx->gpr[1] = R1; ctx->gpr[80] = g80; ctx->gpr[81] = g81;
                bie = 1;
            }
        }
        /* the other buffer (r82 = 4 - the index read at the entry), once its last PUT is done */
        const u128 DESC2 = na_rot(na_lq(ls, c + q2 + 196u), c + q2 + 196u);
        const uint32_t desc2 = DESC2._u32[0];
        mask2 = na_shl1(desc2 >> 24);
        while (!na_poll(ctx, mask2)) _mm_pause();
        bie = 1;
        for (int i = 0; i < 4; i++) res._u32[i] = DESC2._u32[i] & 0x3FFFFu;
        na_wr(ls, c + 0xE0u, res._u32[0]);
        idxc = idx2; qc = q2; descc = desc2;
        NA_ADD(NA_SWITCH, 1);
    } else {
        NA_ADD(NA_FAST, 1);
    }
    /* 0x47C0 */
    const uint32_t r11 = res._u32[0];
    na_wr(ls, c + 0xC0u, idxc);
    na_wr(ls, c + qc + 196u, descc);
    na_wr(ls, c + 0xD0u, r11 + size16);
    if (relptr != 0) {
        const uint32_t nrel = na_rd(ls, c + 0x430u);
        na_wr(ls, c + 0x430u, nrel + 1u);
        na_wr(ls, c + 0x440u + 4u * nrel, relptr);
        na_wr(ls, relptr, r11 - na_rd(ls, c + 0xE0u));
    }
    if (NC_UNLIKELY(g_na_mode == 2)) {
        const uint32_t b = descc & 0x3FFFFu, al = R5._u32[0];
        if (r11 < b || r11 + size16 > b + 30720u) na_bad("allocation outside its buffer", r11, size16, b);
        if (al && !(al & (al - 1u)) && (r11 & (al - 1u))) na_bad("allocation not aligned", r11, al, 0);
        if (!full && r11 < cur) na_bad("allocation before the last one", r11, cur, b);
    }
    if (bie) ctx->int_enable = 1;
    ctx->gpr[3] = res;
    NA_ADD(NA_CALLS, 1);
    na_done(t0, NA_T_ALLOC);
    return 1;
}

/* time in a lifted call made instead, for the report */
#define NA_LIFTED_CALL(A) do { \
        if (NC_UNLIKELY(g_na_stat)) { const unsigned long long _t = NP_TSC(); NC_ORIG(A)(ctx); t_na[NA_T_LIFTED] += NP_TSC() - _t; return; } \
        __attribute__((musttail)) return NC_ORIG(A)(ctx); } while (0)

static SPU_SV void na_4650(spu_context* ctx)
{
    gt5_dbi_ab_poll();
    if (NC_LIKELY(na_mode() != 0)) {
        if (NC_LIKELY(na_ok(ctx))) { if (NC_LIKELY(na_alloc(ctx))) SPU_RET(ctx); }
        else NA_ADD(NA_L_GUARD, 1);
    }
    NA_LIFTED_CALL(00004650);
}
/* GT5_ALLOC=3: the lifted 0x4330 as a call, then the space it took from the ring (ctx+136 before and after; the
 * label it read lands at ctx+0) through the checks above; the return as its own `bi $0` */
static __attribute__((noinline)) void na_watch_4330(spu_context* ctx)
{
    const uint8_t* const ls = ctx->ls;
    const uint32_t c = ctx->gpr[3]._u32[0], cursor = ctx->gpr[4]._u32[0];
    const uint32_t P = na_rd(ls, c + 0x1D0u), ring = cursor == 0 && na_rd(ls, P + 40u) != 0;
    const uint32_t n = na_rd(ls, c + 0xD0u) - na_rd(ls, c + 0xE0u), sz = (n + 127u) & ~127u;
    const uint32_t put0 = na_rd(ls, c + 136u), rbase = na_rd(ls, c + 0x80u), rend = na_rd(ls, c + 132u);
    const uint32_t link = ctx->gpr[0]._u32[0];
    { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; NC_ORIG(00004330)(ctx); spu_drain_call(ctx, link); ctx->host_depth--; spu_img_restore(ctx, _si); }
    const uint32_t put1 = na_rd(ls, c + 136u), get = na_rd(ls, c);
    if (ring && n && put1 != put0) {
        if (put1 == put0 + sz) na_ring_check(put0, put1, rbase, rend, get, " (lifted)");
        else if (put1 == rbase + sz) na_ring_check(rbase, put1, rbase, rend, get, " (lifted)");
    }
}
static SPU_SV void na_4330(spu_context* ctx)
{
    if (NC_UNLIKELY(g_na_mode == 3) && na_ok(ctx)) { na_watch_4330(ctx); SPU_RET(ctx); }
    if (NC_LIKELY(na_mode() != 0)) {
        if (NC_LIKELY(na_ok(ctx))) {
            int bie = 0;
            const uint32_t c = ctx->gpr[3]._u32[0];
            if (NC_LIKELY(na_reloc(ctx, c, ctx->gpr[4]._u32[0], ctx->gpr[0], ctx->gpr[1], ctx->gpr[80], ctx->gpr[81], &bie) == NA_R_DONE)) {
                if (bie) ctx->int_enable = 1;
                SPU_RET(ctx);
            }
        } else NA_ADD(NA_L_GUARD, 1);
    }
    NA_LIFTED_CALL(00004330);
}
static SPU_SV void na_41E8(spu_context* ctx)
{
    if (NC_LIKELY(na_mode() != 0)) {
        if (NC_LIKELY(na_ok(ctx))) {
            int lift = 0;
            const u128 r3 = na_shared(ctx, ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0], ctx->gpr[1], &lift);
            if (NC_LIKELY(!lift)) { ctx->gpr[3] = r3; SPU_RET(ctx); }
        } else NA_ADD(NA_L_GUARD, 1);
    }
    NA_LIFTED_CALL(000041E8);
}
static SPU_SV void na_40A0(spu_context* ctx)
{
    if (NC_LIKELY(na_mode() != 0)) {
        if (NC_LIKELY(na_ok(ctx))) {
            if (na_flush(ctx, ctx->gpr[3]._u32[0], ctx->gpr[4]._u32[0], ctx->gpr[5]._u32[0])) ctx->int_enable = 1;
            SPU_RET(ctx);
        }
        NA_ADD(NA_L_GUARD, 1);
    }
    NA_LIFTED_CALL(000040A0);
}
/* the hooked names, with the profiler's stamps */
SPU_SV void NC_NATIVE(00004650)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_ALLOC); na_4650(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return na_4650(ctx);
}
SPU_SV void NC_NATIVE(00004330)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_RELOC); na_4330(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return na_4330(ctx);
}
SPU_SV void NC_NATIVE(000041E8)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_ATOM); na_41E8(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return na_41E8(ctx);
}
SPU_SV void NC_NATIVE(000040A0)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_FLUSH); na_40A0(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return na_40A0(ctx);
}

/* ---- 0x8F40: the job's quadword copy (dst r3, src r4, bytes r5), as native code -------------------------
 * The SPU code copies 128 bytes a pass (eight lqd, eight stqd) and enters the first pass part way through
 * the stores with a computed `bi` (0x8FB8 + 4 * the quadwords to skip): an address the lift has no function
 * for, so the game interprets from there (an interpreter run and a dispatch each call), and the loop's
 * own `bi` goes through the dispatcher. Here: the same loads and stores in the same order, every register
 * it writes (r2-r13, all four words) left as it leaves them, the return as its `biz $r5, $r0`.
 * Lengths whose remainder never reaches 0 (not a multiple of 16: the SPU code loops forever), LS
 * diagnostics and other code at 0x8F40-0x9080 than this was written for (ncc_ok) run the lifted function. GT5_MEMCPY=0 runs the lifted function always; GT5_MEMCPY=2 runs both and
 * compares (the native on the side, the lifted one's result stays): "[cull-copy] compared N, MISMATCH M" every 16384,
 * the first mismatches named (copies up to 64 KB). */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w018CAD00_spu_func_00008F40_orig_sv(spu_context* ctx);
#else
SPU_SV void NC_LIFTED(00008F40)(spu_context* ctx);
#endif
static int g_nm_on = -1;
static __attribute__((noinline)) int nm_init(void)
{
    const char* e = getenv("GT5_MEMCPY");
    g_nm_on = (e && e[0] == '0') ? 0 : (e && e[0] == '2') ? 2 : 1;
    if (g_nm_on == 2) { fprintf(stderr, "[cull-copy] GT5_MEMCPY=2 (both run, compared)\n"); fflush(stderr); }
    return g_nm_on;
}
static SPU_SV void nm_8F40_native(spu_context* ctx);
static volatile long long g_nm_cmp, g_nm_bad;
#ifdef NC_HARNESS
long long nc_nm_stat(int i) { return i ? g_nm_bad : g_nm_cmp; }
#endif
/* GT5_MEMCPY=2: the native copy on the side, then the lifted one from the same state; registers r2-r13 and the
 * destination quadwords compared; the lifted result stays */
static __attribute__((noinline)) void nm_compare(spu_context* ctx)
{
    const uint32_t len = ctx->gpr[5]._u32[0];
    const uint32_t lo = (ctx->gpr[3]._u32[0] - 128u) & NC_LSM & ~15u, n = (len + 256u + 15u) & ~15u;
    if (len > 65536u || lo + n > SPU_LS_SIZE) { NC_ORIG(00008F40)(ctx); return; }
    uint8_t* const ls = ctx->ls;
    uint8_t* const save = (uint8_t*)malloc(2u * n);
    if (!save) { NC_ORIG(00008F40)(ctx); return; }
    u128 g0[14], g1[14];
    memcpy(g0, ctx->gpr, sizeof g0); memcpy(save, ls + lo, n);
    const uint32_t pc0 = ctx->pc;
    nm_8F40_native(ctx);
    memcpy(g1, ctx->gpr, sizeof g1); memcpy(save + n, ls + lo, n);
    memcpy(ctx->gpr, g0, sizeof g0); memcpy(ls + lo, save, n); ctx->pc = pc0;
    const uint32_t link = ctx->gpr[0]._u32[0];
    { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; NC_ORIG(00008F40)(ctx); spu_drain_call(ctx, link); ctx->host_depth--; spu_img_restore(ctx, _si); }
    const long long k = __atomic_add_fetch(&g_nm_cmp, 1, __ATOMIC_RELAXED);
    if (memcmp(g1 + 2, ctx->gpr + 2, 12 * sizeof(u128)) || memcmp(save + n, ls + lo, n)) {
        const long long b = __atomic_add_fetch(&g_nm_bad, 1, __ATOMIC_RELAXED);
        if (b <= 8) { fprintf(stderr, "[cull-copy] MISMATCH dst %05X src %05X len %u\n", g0[3]._u32[0], g0[4]._u32[0], len); fflush(stderr); }
    }
    if ((k & 0x3FFF) == 0) { fprintf(stderr, "[cull-copy] compared %lld, MISMATCH %lld\n", k, (long long)g_nm_bad); fflush(stderr); }
    free(save);
}
static inline u128 nm_v(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { u128 r; r._u32[0] = a; r._u32[1] = b; r._u32[2] = c; r._u32[3] = d; return r; }
static SPU_SV void nm_8F40(spu_context* ctx)
{
    int on = g_nm_on;
    if (NC_UNLIKELY(on < 0)) on = nm_init();
    if (NC_UNLIKELY(on == 2) && !NATIVE_GEO_IRQ_PENDING(ctx) && (g_spu_ls_rdiag | g_spu_ls_wdiag) == 0 && ncc_ok(ctx, NCC_COPY)) { nm_compare(ctx); SPU_RET(ctx); }
    __attribute__((musttail)) return nm_8F40_native(ctx);
}
static SPU_SV void nm_8F40_native(spu_context* ctx)
{
    const int on = g_nm_on;
    if (NC_UNLIKELY(!on || !ncc_ok(ctx, NCC_COPY))) { __attribute__((musttail)) return NC_ORIG(00008F40)(ctx); }
    u128 r2, r3 = ctx->gpr[3], r4 = ctx->gpr[4], r5 = ctx->gpr[5], r6, r7, r8, r9, r10, r11, r12, r13;
    for (int i = 0; i < 4; i++) r2._u32[i] = 128u - r5._u32[i];                    /* sfi r2, r5, 128 */
    if (r5._u32[0] == 0) { ctx->gpr[2] = r2; SPU_RET(ctx); }                         /* biz r5, r0 */
    if ((g_spu_ls_rdiag | g_spu_ls_wdiag) != 0 || ((r5._u32[0] - 128u + ((128u - r5._u32[0]) & 112u)) & 127u) != 0)
        { __attribute__((musttail)) return NC_ORIG(00008F40)(ctx); }
    uint8_t* const ls = ctx->ls;
    for (int i = 0; i < 4; i++) r2._u32[i] &= 112u;                                  /* andi */
    for (int i = 0; i < 4; i++) { r4._u32[i] -= r2._u32[i]; r3._u32[i] -= r2._u32[i]; }
    r6 = nm_v(0x8FB8u, 0x8FB8u, 0x8FB8u, 0x8FB8u);
    uint32_t a = r4._u32[0];
    r7 = na_lq(ls, a);
    for (int i = 0; i < 4; i++) r8._u32[i] = r2._u32[i] >> 2;                        /* rotmi -2 */
    r9 = na_lq(ls, a + 0x10);
    for (int i = 0; i < 4; i++) r5._u32[i] -= 128u;
    r10 = na_lq(ls, a + 0x20); r11 = na_lq(ls, a + 0x30); r12 = na_lq(ls, a + 0x40);
    for (int i = 0; i < 4; i++) r5._u32[i] += r2._u32[i];
    r2 = na_lq(ls, a + 0x50);
    for (int i = 0; i < 4; i++) r6._u32[i] += r8._u32[i];
    r8 = na_lq(ls, a + 0x60); r13 = na_lq(ls, a + 0x70);
    uint32_t skip = (r6._u32[0] - 0x8FB8u) >> 2;                                      /* bi r6: stores skipped */
    for (;;) {
        const uint32_t d = r3._u32[0];
        const u128* q[8] = { &r7, &r9, &r10, &r11, &r12, &r2, &r8, &r13 };
        for (uint32_t k = skip; k < 8; k++) na_sq(ls, d + 16u * k, *q[k]);
        skip = 0;
        for (int i = 0; i < 4; i++) r4._u32[i] += 128u;                              /* ai r4, r4, 128 */
        if (r5._u32[0] == 0) break;                                                    /* biz r5, r0 */
        for (int i = 0; i < 4; i++) { r3._u32[i] += 128u; r5._u32[i] -= 128u; }      /* ai r3; (loop head) ai r5 */
        a = r4._u32[0];
        r7 = na_lq(ls, a); r9 = na_lq(ls, a + 0x10); r10 = na_lq(ls, a + 0x20); r11 = na_lq(ls, a + 0x30);
        r12 = na_lq(ls, a + 0x40); r2 = na_lq(ls, a + 0x50); r8 = na_lq(ls, a + 0x60); r13 = na_lq(ls, a + 0x70);
    }
    ctx->gpr[2] = r2; ctx->gpr[3] = r3; ctx->gpr[4] = r4; ctx->gpr[5] = r5; ctx->gpr[6] = r6; ctx->gpr[7] = r7;
    ctx->gpr[8] = r8; ctx->gpr[9] = r9; ctx->gpr[10] = r10; ctx->gpr[11] = r11; ctx->gpr[12] = r12; ctx->gpr[13] = r13;
    SPU_RET(ctx);
}
SPU_SV void NC_NATIVE(00008F40)(spu_context* ctx)
{
    __attribute__((musttail)) return nm_8F40(ctx);
}

/* ---- 0xAF88: the cull's triangle loop as native code (exact) ---------------------------------------------
 * 0xAB00's vertex stage leaves, per vertex, screen x, y and near / far masks in its scratch (r8) and the batch's
 * constants in registers; from 0xAF88 a software-pipelined loop (0xAFC0..0xB204, 4 triangles a pass) decides
 * every triangle and packs the kept ones in place at the front of the list (r3), then 0xB4E0 restores the
 * caller's registers, writes the kept count and, with a vertex map, renumbers. Here, for the loop: the
 * decisions in AVX2, 8 triangles a step (exactly the SPU's: single precision round toward zero, denormals
 * flushed, the same compares and selects; checked against the lifted loop on every real batch), and the
 * loop's stores as it makes them: per pass p (two passes of pipeline fill, then one a quad of 4 triangles),
 * per lane, the quadword it carries gets the lane's triangle at the write position's half and keeps its own
 * first half in the other, and is stored at the list + the write position, which moves 8 bytes when the
 * triangle is kept. That is every byte the lifted loop leaves in the list, the ones past the kept triangles
 * too. Then r13 (the write position) and r40 (the read counter) as it leaves them, and on to 0xB4E0 as the
 * lift goes there. 0xB4E0 and the rest of 0xAB00 stay lifted.
 * The lifted loop runs when: GT5_CULLX=0, the job's code at 0xAB00-0xB5D8 is not the code this was written
 * for (ncc_ok), an interrupt is pending, an LS diagnostic is on, MXCSR is not the SPU's (RZ, FTZ, DAZ), the batch's sample pattern is 1 or 5 (not verified), a value is out of the range the
 * x86 operations reproduce (exponent 255, an overflow), an index is past the vertices, the loop's counters
 * are not what its triangle count says. GT5_CULLX=2: both run and compared (the native first on the side,
 * the lifted result stays): "[cull-x] compared N, MISMATCH M", the first mismatches named. */
#ifdef NATIVE_HOOKED
SPU_SV void gt5_w018CAD00_spu_func_0000AF88_orig_sv(spu_context* ctx);
#else
SPU_SV void NC_LIFTED(0000AF88)(spu_context* ctx);
#endif
SPU_SV void gt5_w018CAD00_spu_func_0000B4E0_sv(spu_context* ctx);
void gt5_w018CAD00_spu_func_0000B4E0(spu_context* ctx);

typedef struct {
    float bias, k1, k2, xlim, ylim;
    uint32_t flip;          /* r36: 0, or the sign bit */
    int two_sided;          /* r10 == 16 */
    uint32_t cover_mask;    /* r58: ~0 for the 2x2 sample grid, ~1 for the centre (pattern 0) */
    uint32_t near_off;      /* r50: ~0 = no near-plane test (the near flags are masked out) */
} nx_consts;

/* SPU cflts(x, 172): x * 2 truncated toward zero, saturated to int32 */
static inline __m256i nx_cflts2(__m256 x)
{
    const __m256 t = _mm256_add_ps(x, x);
    __m256i r = _mm256_cvttps_epi32(t);
    const __m256 pos_over = _mm256_cmp_ps(t, _mm256_set1_ps(2147483648.0f), _CMP_GE_OQ);
    return _mm256_blendv_epi8(r, _mm256_set1_epi32(0x7FFFFFFF), _mm256_castps_si256(pos_over));
}
/* the 16-byte host-order lines (x y near far) of 8 vertices -> x[8], y[8], near[8], far[8] */
static inline void nx_role(const uint32_t* soa, const int32_t* idx, __m256* x, __m256* y, __m256i* nn, __m256i* ff)
{
    const __m128i* L = (const __m128i*)soa;
    const __m256i r0 = _mm256_setr_m128i(_mm_load_si128(L + idx[0]), _mm_load_si128(L + idx[4]));
    const __m256i r1 = _mm256_setr_m128i(_mm_load_si128(L + idx[1]), _mm_load_si128(L + idx[5]));
    const __m256i r2 = _mm256_setr_m128i(_mm_load_si128(L + idx[2]), _mm_load_si128(L + idx[6]));
    const __m256i r3 = _mm256_setr_m128i(_mm_load_si128(L + idx[3]), _mm_load_si128(L + idx[7]));
    const __m256i t0 = _mm256_unpacklo_epi32(r0, r1), t1 = _mm256_unpackhi_epi32(r0, r1);
    const __m256i t2 = _mm256_unpacklo_epi32(r2, r3), t3 = _mm256_unpackhi_epi32(r2, r3);
    *x = _mm256_castsi256_ps(_mm256_unpacklo_epi64(t0, t2)); *y = _mm256_castsi256_ps(_mm256_unpackhi_epi64(t0, t2));
    *nn = _mm256_unpacklo_epi64(t1, t3); *ff = _mm256_unpackhi_epi64(t1, t3);
}
/* keep: bit (t & 7) of byte t >> 3 = triangle t kept. Returns the number kept, or -1 when a value is outside
 * the range the x86 operations reproduce exactly (the caller then runs the lifted loop). */
static int nx_decide(const uint8_t* ls, uint32_t list, uint32_t scratch, uint32_t ntri, uint32_t nvert, const nx_consts* k, uint8_t* keep)
{
    const __m256 bias = _mm256_set1_ps(k->bias), k1 = _mm256_set1_ps(k->k1), k2 = _mm256_set1_ps(k->k2);
    const __m256 xlim = _mm256_set1_ps(k->xlim), ylim = _mm256_set1_ps(k->ylim);
    const __m256 absm = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
    const __m256i flip = _mm256_set1_epi32((int)k->flip);
    const __m256i expmask = _mm256_set1_epi32(0x7F800000);
    __m256i bad = _mm256_setzero_si256();
    int kept = 0;
    static SPU_THREAD_LOCAL uint32_t* soa; static SPU_THREAD_LOCAL uint32_t cap;
    const uint32_t nv4 = (nvert + 3u) & ~3u;
    if (nv4 > 4096u || scratch + 16u * nv4 > 0x40000u || (scratch & 15u)) return -1;
    if ((list & 7u) || list + 8u * ((ntri + 7u) & ~7u) > 0x40000u) return -1;
    if (4u * nv4 > cap) { _mm_free(soa); cap = 4u * nv4 + 64u; soa = (uint32_t*)_mm_malloc(4u * cap, 32); if (!soa) { cap = 0; return -1; } }
    const __m256i bsw = _mm256_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12, 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
    for (uint32_t v = 0; v < nv4; v += 2)
        _mm256_store_si256((__m256i*)(soa + 4u * v), _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i*)(ls + scratch + 16u * v)), bsw));
    for (uint32_t t0 = 0; t0 < ntri; t0 += 8) {
        int32_t i0v[8], i1v[8], i2v[8];
        const uint32_t n = ntri - t0 < 8 ? ntri - t0 : 8;
        {
            const __m256i hsw = _mm256_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
            const uint8_t* e = ls + list + 8u * t0;
            const __m256i v0 = _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i*)e), hsw);
            const __m256i v1 = _mm256_shuffle_epi8(_mm256_loadu_si256((const __m256i*)(e + 32)), hsw);
            const __m256i ev = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
            const __m256i p0 = _mm256_permutevar8x32_epi32(v0, ev), p1 = _mm256_permutevar8x32_epi32(v1, ev);
            const __m256i E = _mm256_permute2x128_si256(p0, p1, 0x20), O = _mm256_permute2x128_si256(p0, p1, 0x31);
            const __m256i lo = _mm256_set1_epi32(0xFFFF);
            __m256i I0 = _mm256_and_si256(E, lo), I1 = _mm256_srli_epi32(E, 16), I2 = _mm256_and_si256(O, lo);
            if (n < 8) {
                const __m256i lane = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7), valid = _mm256_cmpgt_epi32(_mm256_set1_epi32((int)n), lane);
                I0 = _mm256_blendv_epi8(_mm256_permutevar8x32_epi32(I0, _mm256_setzero_si256()), I0, valid);
                I1 = _mm256_blendv_epi8(_mm256_permutevar8x32_epi32(I1, _mm256_setzero_si256()), I1, valid);
                I2 = _mm256_blendv_epi8(_mm256_permutevar8x32_epi32(I2, _mm256_setzero_si256()), I2, valid);
            }
            const __m256i lim = _mm256_set1_epi32((int)nv4 - 1);
            const __m256i over = _mm256_or_si256(_mm256_cmpgt_epi32(I0, lim), _mm256_or_si256(_mm256_cmpgt_epi32(I1, lim), _mm256_cmpgt_epi32(I2, lim)));
            if (!_mm256_testz_si256(over, over)) return -1;
            _mm256_storeu_si256((__m256i*)i0v, I0); _mm256_storeu_si256((__m256i*)i1v, I1); _mm256_storeu_si256((__m256i*)i2v, I2);
        }
        __m256 Ax, Ay, Bx, By, Cx, Cy; __m256i An, Bn, Cn, Af, Bf, Cf;
        nx_role(soa, i0v, &Ax, &Ay, &An, &Af);
        nx_role(soa, i1v, &Bx, &By, &Bn, &Bf);
        nx_role(soa, i2v, &Cx, &Cy, &Cn, &Cf);
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(Ax), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(Ay), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(Bx), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(By), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(Cx), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(Cy), expmask), expmask));
        const __m256 dxb = _mm256_sub_ps(Bx, Ax), dxc = _mm256_sub_ps(Cx, Ax), dyb = _mm256_sub_ps(By, Ay), dyc = _mm256_sub_ps(Cy, Ay);
        const __m256 s = _mm256_add_ps(_mm256_add_ps(_mm256_and_ps(dxb, absm), _mm256_and_ps(dxc, absm)),
                                       _mm256_add_ps(_mm256_and_ps(dyb, absm), _mm256_and_ps(dyc, absm)));
        const __m256 eps = _mm256_fmadd_ps(s, k1, k2);
        const __m256 tt = _mm256_fnmadd_ps(dyc, dxb, eps);
        const __m256 uu = _mm256_fmadd_ps(dxc, dyb, tt);
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(s), expmask), expmask));
        bad = _mm256_or_si256(bad, _mm256_cmpeq_epi32(_mm256_and_si256(_mm256_castps_si256(uu), expmask), expmask));
        const __m256i degenerate = _mm256_cmpeq_epi32(_mm256_castps_si256(uu), _mm256_castps_si256(eps));
        const __m256 gx = _mm256_cmp_ps(Ax, Bx, _CMP_GT_OQ), gy = _mm256_cmp_ps(Ay, By, _CMP_GT_OQ);
        const __m256 mxAB = _mm256_blendv_ps(Bx, Ax, gx), mnAB = _mm256_blendv_ps(Ax, Bx, gx);
        const __m256 myAB = _mm256_blendv_ps(By, Ay, gy), nyAB = _mm256_blendv_ps(Ay, By, gy);
        const __m256 xmax = _mm256_blendv_ps(mxAB, Cx, _mm256_cmp_ps(Cx, mxAB, _CMP_GT_OQ));
        const __m256 xmin = _mm256_blendv_ps(Cx, mnAB, _mm256_cmp_ps(Cx, mnAB, _CMP_GT_OQ));
        const __m256 ymax = _mm256_blendv_ps(myAB, Cy, _mm256_cmp_ps(Cy, myAB, _CMP_GT_OQ));
        const __m256 ymin = _mm256_blendv_ps(Cy, nyAB, _mm256_cmp_ps(Cy, nyAB, _CMP_GT_OQ));
        const __m256 xmaxb = _mm256_add_ps(xmax, bias), ymaxb = _mm256_add_ps(ymax, bias);
        const __m256i cmask = _mm256_set1_epi32((int)k->cover_mask);
        const __m256i cover = _mm256_and_si256(_mm256_sub_epi32(nx_cflts2(xmin), _mm256_and_si256(nx_cflts2(xmaxb), cmask)),
                                               _mm256_sub_epi32(nx_cflts2(ymin), _mm256_and_si256(nx_cflts2(ymaxb), cmask)));
        const __m256i top = _mm256_set1_epi32((int)0xFF000000u);
        const __m256i nA = _mm256_cmpeq_epi32(_mm256_and_si256(An, top), top), nB = _mm256_cmpeq_epi32(_mm256_and_si256(Bn, top), top);
        const __m256i nC = _mm256_cmpeq_epi32(_mm256_and_si256(Cn, top), top);
        const __m256i zA = _mm256_cmpeq_epi32(_mm256_and_si256(An, top), _mm256_setzero_si256());
        const __m256i zB = _mm256_cmpeq_epi32(_mm256_and_si256(Bn, top), _mm256_setzero_si256());
        const __m256i zC = _mm256_cmpeq_epi32(_mm256_and_si256(Cn, top), _mm256_setzero_si256());
        const __m256i noff = _mm256_set1_epi32((int)k->near_off);
        const __m256i allnear = _mm256_andnot_si256(noff, _mm256_and_si256(nA, _mm256_and_si256(nB, nC)));
        const __m256i nonenear = _mm256_or_si256(noff, _mm256_and_si256(zA, _mm256_and_si256(zB, zC)));
        const __m256i facing_on = _mm256_or_si256(allnear, nonenear);
        const __m256i allfar = _mm256_and_si256(Af, _mm256_and_si256(Bf, Cf));
        __m256i facing = _mm256_xor_si256(_mm256_castps_si256(uu), flip);
        if (k->two_sided) facing = _mm256_setzero_si256();
        facing = _mm256_and_si256(facing, facing_on);
        __m256i rej = _mm256_or_si256(degenerate, _mm256_andnot_si256(cover, _mm256_set1_epi32(-1)));
        rej = _mm256_or_si256(rej, facing);
        rej = _mm256_or_si256(rej, allnear);
        rej = _mm256_or_si256(rej, allfar);
        rej = _mm256_or_si256(rej, _mm256_castps_si256(xmaxb));
        rej = _mm256_or_si256(rej, _mm256_castps_si256(ymaxb));
        rej = _mm256_or_si256(rej, _mm256_castps_si256(_mm256_cmp_ps(xmin, xlim, _CMP_GT_OQ)));
        rej = _mm256_or_si256(rej, _mm256_castps_si256(_mm256_cmp_ps(ymin, ylim, _CMP_GT_OQ)));
        const uint32_t km = (~(uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(rej))) & ((1u << n) - 1u);
        keep[t0 >> 3] = (uint8_t)km;
        kept += __builtin_popcount(km);
    }
    if (_mm256_movemask_ps(_mm256_castsi256_ps(bad))) return -1;
    return kept;
}

/* the loop's stores (see above). Returns 0 when its counters are not the shape this model knows. */
static int nx_pack(spu_context* ctx, const uint8_t* keep, uint32_t ntri)
{
    uint8_t* const ls = ctx->ls;
    const uint32_t list = ctx->gpr[3]._u32[0];
    const u128 R13 = ctx->gpr[13], R40 = ctx->gpr[40];
    const int32_t r40e = (int32_t)R40._u32[0];
    if (r40e > 0 || (r40e & 31) || (uint32_t)(-r40e) / 32u != (ntri + 3u) / 4u) return 0;
    const uint32_t P = (uint32_t)(-r40e) / 32u + 2u;                      /* passes: (quads) + 2 of fill */
    for (uint32_t j = 0; j < 4; j++)                                       /* the last pass's lane mask is the count's */
        if ((ctx->gpr[43]._u32[j] != 0) != (4u * ((ntri + 3u) / 4u - 1u) + j >= ntri)) return 0;
    uint8_t Q[16], fill[2][4][8];
    for (int i = 0; i < 4; i++) { const uint32_t w = __builtin_bswap32(ctx->gpr[84]._u32[i]); memcpy(Q + 4 * i, &w, 4); }
    const int fr[4] = { 45, 32, 37, 57 };
    for (int f = 0; f < 4; f++) {
        uint8_t b[16];
        for (int i = 0; i < 4; i++) { const uint32_t w = __builtin_bswap32(ctx->gpr[fr[f]]._u32[i]); memcpy(b + 4 * i, &w, 4); }
        memcpy(fill[f >> 1][2 * (f & 1)], b, 8); memcpy(fill[f >> 1][2 * (f & 1) + 1], b + 8, 8);
    }
    uint32_t pos = R13._u32[0], kept = 0;
    uint64_t qlo, qhi, plo = 0, phi = 0;                                   /* the quadword carried; the last one stored */
    memcpy(&qlo, Q, 8); memcpy(&qhi, Q + 8, 8);
    uint32_t cur = 0xFFFFFFFFu;                                            /* where it was stored: only its last store there stays */
    uint8_t quad[3][32];                                                   /* pass p loads quad p, stores quad p - 2's lanes */
    for (uint32_t p = 0; p < P; p++) {
        if (p + 2u < P) {
            const uint32_t a = (list + 32u * p) & NC_LSM;
            if (a + 32u <= SPU_LS_SIZE) memcpy(quad[p % 3u], ls + a, 32);
            else for (uint32_t i = 0; i < 32u; i++) quad[p % 3u][i] = ls[(a + i) & NC_LSM];
        }
        for (uint32_t j = 0; j < 4; j++) {
            uint64_t d;
            uint32_t k = 0;
            if (p < 2) memcpy(&d, fill[p][j], 8);
            else {
                const uint32_t t = 4u * (p - 2u) + j;
                memcpy(&d, quad[(p - 2u) % 3u] + 8u * j, 8);
                k = t < ntri ? (keep[t >> 3] >> (t & 7)) & 1u : 0u;
            }
            if (pos & 8u) qhi = d;
            else { qhi = qlo; qlo = d; }
            const uint32_t addr = (list + pos) & NC_LSM & ~15u;            /* stqx: the quadword at the list + the position */
            if (addr != cur) {
                if (cur != 0xFFFFFFFFu) { memcpy(ls + cur, &plo, 8); memcpy(ls + cur + 8, &phi, 8); }
                cur = addr;
            }
            plo = qlo; phi = qhi;
            if (k) { pos += 8u; kept++; }
        }
    }
    if (cur != 0xFFFFFFFFu) { memcpy(ls + cur, &plo, 8); memcpy(ls + cur + 8, &phi, 8); }
    for (int i = 0; i < 4; i++) { ctx->gpr[13]._u32[i] = R13._u32[i] + 8u * kept; ctx->gpr[40]._u32[i] = R40._u32[i] + 32u * P; }
    return 1;
}

/* the sample pattern of the batch the cull at 0xAB00 started (patterns 1 and 5 are not verified) */
static SPU_THREAD_LOCAL const spu_context* t_nx_ctx;
static SPU_THREAD_LOCAL uint32_t t_nx_pat;
static int g_nx_on = -1;
static volatile long long g_nx_stat[4];   /* native, lifted, compared, mismatches */
#ifdef NC_HARNESS
long long g_nx_tsc[4];                    /* harness: decide ticks, pack ticks, calls, triangles */
#endif
static __attribute__((noinline)) int nx_init(void)
{
    const char* e = getenv("GT5_CULLX");
    g_nx_on = (e && e[0] == '0') ? 0 : (e && e[0] == '2') ? 2 : 1;
    if (g_nx_on != 1) { fprintf(stderr, "[cull-x] GT5_CULLX=%d%s\n", g_nx_on, g_nx_on == 2 ? " (both run, compared)" : " (lifted)"); fflush(stderr); }
    return g_nx_on;
}
/* 1 = done natively (r13 r40 set, list written), 0 = nothing changed: run the lifted loop */
static int nx_try(spu_context* ctx)
{
    static SPU_THREAD_LOCAL uint8_t keep[0x8000 / 8 + 8];
    const uint8_t* ls = ctx->ls;
    if (NATIVE_GEO_IRQ_PENDING(ctx) || (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0 || (_mm_getcsr() & 0xE040u) != 0xE040u) return 0;
    if (t_nx_ctx != ctx || t_nx_pat == 1u || t_nx_pat == 5u || !ncc_ok(ctx, NCC_LOOP)) return 0;
    const uint32_t line = ctx->gpr[4]._u32[0] & NC_LSM & ~15u;
    const uint32_t ntri = nc_be32(ls, line), nvert = nc_be32(ls, line + 4);
    if (!ntri || ntri > 0x8000u) return 0;
    nx_consts k;
    uint32_t u;
    u = ctx->gpr[5]._u32[0]; memcpy(&k.bias, &u, 4); u = ctx->gpr[6]._u32[0]; memcpy(&k.k1, &u, 4);
    u = ctx->gpr[34]._u32[0]; memcpy(&k.k2, &u, 4); u = ctx->gpr[55]._u32[0]; memcpy(&k.xlim, &u, 4);
    u = ctx->gpr[14]._u32[0]; memcpy(&k.ylim, &u, 4);
    k.flip = ctx->gpr[36]._u32[0]; k.two_sided = ctx->gpr[10]._u32[0] == 16u; k.cover_mask = ctx->gpr[58]._u32[0]; k.near_off = ctx->gpr[50]._u32[0];
#ifdef NC_HARNESS
    const unsigned long long nxt0 = __rdtsc();
#endif
    const int kn = nx_decide(ls, ctx->gpr[3]._u32[0], ctx->gpr[8]._u32[0], ntri, nvert, &k, keep);
    if (kn < 0) return 0;
#ifdef NC_HARNESS
    const unsigned long long nxt1 = __rdtsc();
    const int pk = nx_pack(ctx, keep, ntri);
    g_nx_tsc[0] += (long long)(nxt1 - nxt0); g_nx_tsc[1] += (long long)(__rdtsc() - nxt1); g_nx_tsc[2]++; g_nx_tsc[3] += ntri;
    if (pk) return 1;
    if (getenv("NX_DEBUG")) fprintf(stderr, "[nx] list %05X ntri %u nvert %u kept %d keep0 %02X pat %u two %d flip %08X cover %08X noff %08X r13 %08X r40 %08X\n",
                                    ctx->gpr[3]._u32[0], ntri, nvert, kn, keep[0], t_nx_pat, k.two_sided, k.flip, k.cover_mask, k.near_off, ctx->gpr[13]._u32[0], ctx->gpr[40]._u32[0]);
#endif
    return nx_pack(ctx, keep, ntri);
}
/* GT5_CULLX=2: the native loop and the rest of 0xAB00 on the side, then the lifted one from the same state;
 * all local store and r0 r1 r80-r127 compared; the lifted result stays */
static __attribute__((noinline)) void nx_compare(spu_context* ctx)
{
    const uint32_t link = nc_be32(ctx->ls, ctx->gpr[1]._u32[0] + 0x150u);       /* lqd $0, 0x150($1) at 0xB4E4 */
    uint8_t* const save = (uint8_t*)malloc(2u * SPU_LS_SIZE);
    if (!save) { NC_ORIG(0000AF88)(ctx); return; }
    static SPU_THREAD_LOCAL u128 g0[128], g1[128];
    memcpy(g0, ctx->gpr, sizeof g0); memcpy(save, ctx->ls, SPU_LS_SIZE);
    const uint32_t pc0 = ctx->pc, ie0 = ctx->int_enable;
    int native = nx_try(ctx);
    if (native) {
        int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++;
        gt5_w018CAD00_spu_func_0000B4E0_sv(ctx); spu_drain_call(ctx, link); ctx->host_depth--; spu_img_restore(ctx, _si);
        memcpy(g1, ctx->gpr, sizeof g1); memcpy(save + SPU_LS_SIZE, ctx->ls, SPU_LS_SIZE);
        memcpy(ctx->gpr, g0, sizeof g0); memcpy(ctx->ls, save, SPU_LS_SIZE); ctx->pc = pc0; ctx->int_enable = ie0;
    }
    { int32_t _si = (int32_t)ctx->image_id; ctx->host_depth++; NC_ORIG(0000AF88)(ctx); spu_drain_call(ctx, link); ctx->host_depth--; spu_img_restore(ctx, _si); }
    if (native) {
        const long long n = __atomic_add_fetch(&g_nx_stat[2], 1, __ATOMIC_RELAXED);
        int bad = memcmp(save + SPU_LS_SIZE, ctx->ls, SPU_LS_SIZE) != 0 || memcmp(&g1[0], &ctx->gpr[0], 2 * sizeof(u128)) != 0 ||
                  memcmp(&g1[80], &ctx->gpr[80], 48 * sizeof(u128)) != 0;
        if (bad) {
            const long long b = __atomic_add_fetch(&g_nx_stat[3], 1, __ATOMIC_RELAXED);
            if (b <= 8) {
                uint32_t off = 0xFFFFFFFFu;
                for (uint32_t i = 0; i < SPU_LS_SIZE && off == 0xFFFFFFFFu; i++) if (save[SPU_LS_SIZE + i] != ctx->ls[i]) off = i;
                fprintf(stderr, "[cull-x] MISMATCH list %05X: first LS byte %05X\n", g0[3]._u32[0], off); fflush(stderr);
            }
        }
        if ((n & 0x3FF) == 0) { fprintf(stderr, "[cull-x] compared %lld, MISMATCH %lld\n", n, (long long)g_nx_stat[3]); fflush(stderr); }
    }
    free(save);
}
#ifdef NC_HARNESS
long long nc_nx_stat(int i) { return g_nx_stat[i]; }
#endif
#ifdef NC_HARNESS
void (*g_nc_af88_probe)(spu_context*);
#endif
static SPU_SV void nx_AF88(spu_context* ctx)
{
#ifdef NC_HARNESS
    if (g_nc_af88_probe) g_nc_af88_probe(ctx);
#endif
    int on = g_nx_on;
    if (NC_UNLIKELY(on < 0)) on = nx_init();
    if (NC_UNLIKELY(on == 2)) { nx_compare(ctx); SPU_RET(ctx); }
    if (NC_LIKELY(on == 1) && nx_try(ctx)) {
#ifdef NC_HARNESS
        g_nx_stat[0]++;
#endif
        SPU_DIRECT_SV(ctx, 0xB4E0, gt5_w018CAD00_spu_func_0000B4E0);
    }
#ifdef NC_HARNESS
    g_nx_stat[1]++;
#endif
    __attribute__((musttail)) return NC_ORIG(0000AF88)(ctx);
}
SPU_SV void NC_NATIVE(0000AF88)(spu_context* ctx)
{
    __attribute__((musttail)) return nx_AF88(ctx);
}

/* ---- 0xAB00 ------------------------------------------------------------------------------------ */
static __attribute__((noinline)) void nc_f6_done(const spu_context* ctx)
{
    const uint8_t* ls = ctx->ls;
    const uint32_t n = nc_be32(ls, t_f6.base + 0x114) - nc_be32(ls, t_f6.base + 0x118);
    if (n % 3u == 0 && n / 3u <= 0x8000u) NC_ADD(NC_F6C0_KEPT, n / 3u);
    t_f6.ctx = NULL;
}

static SPU_SV void nc_0000AB00(spu_context* ctx)
{
    t_nx_ctx = ctx; t_nx_pat = ctx->ls[ctx->gpr[5]._u32[0] & NC_LSM & ~15u];   /* for 0xAF88: the batch's sample pattern */
    if (NC_UNLIKELY(t_f6.ctx == ctx)) nc_f6_done(ctx);
    const int m = nc_mode();
    if (NC_LIKELY(m == 0)) {
        if (NC_UNLIKELY(g_nc_ab)) t_pend.ctx = NULL;     /* an "off" period of PS3_AB_WHAT=cull: no batch waits */
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    uint8_t* const ls = ctx->ls;
    t_pend.ctx = NULL;
    NC_ADD(NC_CALLS, 1);
    if (NC_UNLIKELY(g_nc_dump_dir != NULL)) nc_dump(ctx, 0xAB00u);
    if (NATIVE_GEO_IRQ_PENDING(ctx) || (g_spu_ls_rdiag | g_spu_ls_wdiag) != 0 || !nc_image_ok(ls)) {
        NC_ADD(NC_WHY_GUARD, 1); NC_ADD(NC_LIFTED_CALLS, 1);
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    nc_call c;
    if (!nc_parse(ctx, &c)) {
        NC_ADD(NC_WHY_SHAPE, 1); NC_ADD(NC_LIFTED_CALLS, 1);
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    NC_ADD(NC_TRIS, c.ntri);
    if (m == 2) {
        uint32_t ua = 0;
        const uint32_t mk = nc_model(ls, &c, &ua);
        NC_ADD(NC_TWO_SIDED, c.two_sided); NC_ADD(NC_FLIPPED, c.flip);
        t_pend.ctx = ctx; t_pend.list = c.list; t_pend.ntri = c.ntri; t_pend.kept = 0xFFFFFFFFu; t_pend.rule = nc_rule(&c, 2);
        t_pend.model_kept = mk; t_pend.used_all = ua; t_pend.has_map = c.map != 0; t_pend.base = 0;
        NC_ADD(NC_LIFTED_CALLS, 1);
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    /* mode 1 */
    const int box = nc_box(ls, &c);
    if (box == NC_BOX_OFFSCREEN) {
        nc_keep_none(ls, &c);
        NC_ADD(NC_OFF_CALLS, 1); NC_ADD(NC_OFF_TRIS, c.ntri);
        SPU_RET(ctx);
    }
    if (box != NC_BOX_INSIDE) {
        NC_ADD(NC_WHY_DEPTH, 1); NC_ADD(NC_LIFTED_CALLS, 1);
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    uint32_t used = 0;
    if (!nc_keep_all(ls, &c, &used)) {
        NC_ADD(NC_WHY_INDEX, 1); NC_ADD(NC_LIFTED_CALLS, 1);
        __attribute__((musttail)) return NC_ORIG(0000AB00)(ctx);
    }
    NC_ADD(NC_GPU_CALLS, 1); NC_ADD(NC_GPU_TRIS, c.ntri); NC_ADD(NC_GPU_MAP, c.map != 0);
    NC_ADD(NC_TWO_SIDED, c.two_sided); NC_ADD(NC_FLIPPED, c.flip);
    t_pend.ctx = ctx; t_pend.list = c.list; t_pend.ntri = c.ntri; t_pend.kept = c.ntri; t_pend.rule = nc_rule(&c, 1);
    t_pend.model_kept = c.ntri; t_pend.used_all = used; t_pend.has_map = c.map != 0; t_pend.base = 0;
    SPU_RET(ctx);
}
#ifdef NC_HARNESS   /* the harness looks at a cull call's state (atest job) */
void (*g_nc_ab00_probe)(spu_context*);
void (*g_nc_f6c0_probe)(spu_context*);
#endif
SPU_SV void NC_NATIVE(0000AB00)(spu_context* ctx)
{
#ifdef NC_HARNESS
    if (g_nc_ab00_probe) g_nc_ab00_probe(ctx);
#endif
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_AB00); nc_0000AB00(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return nc_0000AB00(ctx);
}

/* ---- 0xF6C0: the job's other cull routine (another list layout); counted, left as it is ------------ */
/* Called instead of 0xAB00 (path 0x7B10) when the word at ctx+0x244 points at a list of n > 0 64-byte
 * records (r9 here); r3 list, r4 line {triangles, vertices = ctx+0x104, OUT kept}, r5 view block, r6
 * batch matrix, r7 scratch, r8 positions. Its draw goes through the same command writer. Not replaced
 * yet (the records' meaning comes from real inputs: GT5_CULL_DUMP); in modes 1 and 2 its draws are
 * tagged report-only (rule bit 4) so the renderer shows their cull state too. */
static SPU_SV void nc_0000F6C0(spu_context* ctx)
{
    if (NC_UNLIKELY(nc_mode() != 0)) {
        t_pend.ctx = NULL;
        NC_ADD(NC_F6C0_CALLS, 1);
        t_f6.ctx = ctx; t_f6.base = ctx->gpr[81]._u32[0] & NC_LSM & ~15u;     /* r81 = the job's context in its caller */
        if (NC_UNLIKELY(g_nc_dump_dir != NULL)) nc_dump(ctx, 0xF6C0u);
        const uint8_t* ls = ctx->ls;
        const uint32_t line = ctx->gpr[4]._u32[0] & NC_LSM & ~15u, view = ctx->gpr[5]._u32[0] & NC_LSM & ~15u;
        const uint32_t ntri = nc_be32(ls, line);
        NC_ADD(NC_F6C0_TRIS, ntri);
        if (ntri && ntri <= 0x8000u && !(ctx->gpr[3]._u32[0] & 15u)) {
            const float vx = nc_f(ls, view + 0x50), vy = nc_f(ls, view + 0x58);
            t_pend.ctx = ctx; t_pend.list = ctx->gpr[3]._u32[0]; t_pend.ntri = ntri; t_pend.kept = 0xFFFFFFFFu;
            t_pend.rule = ((uint32_t)((int8_t)ls[view + 1] > 0) << 1) | ((uint32_t)(vx < 0.0f) << 2) | ((uint32_t)(vy < 0.0f) << 3) |
                          (1u << 4) | (2u << 8);
            t_pend.model_kept = 0xFFFFFFFFu; t_pend.used_all = 0; t_pend.has_map = 0; t_pend.base = 0;
        }
    }
    __attribute__((musttail)) return NC_ORIG(0000F6C0)(ctx);
}
SPU_SV void NC_NATIVE(0000F6C0)(spu_context* ctx)
{
#ifdef NC_HARNESS
    if (g_nc_f6c0_probe) g_nc_f6c0_probe(ctx);
#endif
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_F6C0); nc_0000F6C0(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return nc_0000F6C0(ctx);
}

/* ---- 0x1036C, the command writer, and 0x1011C, its draw emitter ---------------------------------
 * At the writer's entry r3 is the job's context; ctx+0x110 = {list LS address, index end, index start,
 * index address}. The index address there is NOT final yet: the output-space allocator (0x4650) put
 * &ctx+0x11C on a relocation list (ctx+0x430 count, ctx+0x440 pointers) and the writer itself adds the
 * ring offset at 0x10408 (0x4330). So the writer's entry only checks the batch and keeps the context;
 * the tag goes out at 0x1011C (called once, from 0x1071C, after SET_INDEX_ARRAY_ADDRESS is written:
 * r4 = 0x1824 DRAW_INDEX_ARRAY, r5 = index count, r6 = 5 triangles), when ctx+0x11C is what the RSX
 * gets. */
static SPU_SV void nc_0001036C(spu_context* ctx)
{
    if (NC_UNLIKELY(t_f6.ctx == ctx)) nc_f6_done(ctx);
    /* no mode test: a batch culled in an "on" period of PS3_AB_WHAT=cull still gets its tag in an "off" one */
    if (NC_LIKELY(t_pend.ctx != ctx)) {
        if (NC_UNLIKELY(g_nc_mode > 0)) nc_report();
        __attribute__((musttail)) return NC_ORIG(0001036C)(ctx);
    }
    {
        const uint8_t* ls = ctx->ls;
        const uint32_t base = ctx->gpr[3]._u32[0] & NC_LSM & ~15u;
        const uint32_t list = nc_be32(ls, base + 0x110), iend = nc_be32(ls, base + 0x114), ibeg = nc_be32(ls, base + 0x118);
        const uint32_t count = iend - ibeg;
        if ((list & NC_LSM) == (t_pend.list & NC_LSM) && count % 3u == 0 && count / 3u <= t_pend.ntri &&
            (t_pend.kept == 0xFFFFFFFFu || count == 3u * t_pend.kept)) {
            if ((t_pend.rule >> 8) == 2 && !(t_pend.rule & 0x10u)) {
                NC_ADD(NC_CMP_DRAWS, 1); NC_ADD(NC_CMP_ACT_KEPT, count / 3u);
                if (count / 3u == t_pend.model_kept) NC_ADD(NC_CMP_SAME, 1); else NC_ADD(NC_CMP_DIFF, 1);
                if (t_pend.has_map) { NC_ADD(NC_VERTS_ALL, t_pend.used_all); NC_ADD(NC_VERTS_USED_PS3, nc_be32(ls, base + 0x344)); }
            }
            t_pend.kept = count / 3u;
            t_pend.base = base;                       /* the tag goes out at 0x1011C */
            if (!count) t_pend.ctx = NULL;            /* nothing kept: no draw, no emitter call */
        } else {
            t_pend.ctx = NULL;
            NC_ADD(NC_TAG_NOMATCH, 1);
        }
    }
    nc_report();
    __attribute__((musttail)) return NC_ORIG(0001036C)(ctx);
}
SPU_SV void NC_NATIVE(0001036C)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_WRITER); nc_0001036C(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return nc_0001036C(ctx);
}

static SPU_SV void nc_0001011C(spu_context* ctx)
{
    if (NC_LIKELY(t_pend.ctx != ctx) || NC_UNLIKELY(!t_pend.base)) { __attribute__((musttail)) return NC_ORIG(0001011C)(ctx); }
    {
        const uint8_t* ls = ctx->ls;
        const uint32_t base = t_pend.base;
        const uint32_t count = ctx->gpr[5]._u32[0];
        t_pend.ctx = NULL;
        if (ctx->gpr[4]._u32[0] == 0x1824u && ctx->gpr[6]._u32[0] == 5u && count && count == 3u * t_pend.kept &&
            (nc_be32(ls, base + 0x110) & NC_LSM) == (t_pend.list & NC_LSM)) {
            const uint32_t list = t_pend.list;
            const uint32_t first3 = (nc_be16(ls, list) << 20) ^ (nc_be16(ls, list + 2) << 10) ^ nc_be16(ls, list + 4);
            rsx_cull_tag_put(nc_be32(ls, base + 0x11C), count, first3, t_pend.rule);
            NC_ADD(NC_TAGS, 1);
        } else {
            NC_ADD(NC_TAG_NOMATCH, 1);
        }
    }
    __attribute__((musttail)) return NC_ORIG(0001011C)(ctx);
}
SPU_SV void NC_NATIVE(0001011C)(spu_context* ctx)
{
    if (NC_UNLIKELY(np_on())) { np_enter(ctx, NP_EMIT); nc_0001011C(ctx); np_leave(ctx); return; }
    __attribute__((musttail)) return nc_0001011C(ctx);
}
