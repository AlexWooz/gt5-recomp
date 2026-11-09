/* ld_gather_fast + the scalar finish, against the scalar loop alone: random index buffers of both widths,
 * with and without restart values sprinkled in, all lengths. Refs and cut positions must match. */
#include <smmintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned int u32;
typedef struct { u32 vertex_id, base_index; } rsx_vertex_ref;
#include "gather_fast.h"

static unsigned long long s = 0x9E3779B97F4A7C15ull;
static unsigned rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)s; }

typedef struct { rsx_vertex_ref r[5000]; u32 n; u32 cuts[5000]; u32 nc; } out_t;
static void one(const u8* src, u32 i, int is_u32, int re, u32 rv, u32 base, out_t* o)
{
    const u8* p = src + (size_t)i * (is_u32 ? 4u : 2u);
    const u32 index = is_u32 ? ((u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]) : (u32)(p[0] << 8 | p[1]);
    if (re && index == rv) { o->cuts[o->nc++] = o->n; return; }
    o->r[o->n].vertex_id = index; o->r[o->n].base_index = base; o->n++;
}
int main(void)
{
    static u8 buf[20000]; static out_t a, b;
    int bad = 0;
    for (int t = 0; t < 20000; t++) {
        const int is_u32 = rnd() & 1, re = rnd() % 3 != 0;
        const u32 count = rnd() % 1200, base = rnd() % 5;
        const u32 rv = rnd() % 4 == 0 ? 0xFFFFFFFFu : (is_u32 ? 0xFFFFFFFFu : 0xFFFFu);
        for (u32 i = 0; i < count * 4; i++) buf[i] = (u8)rnd();
        const int density = rnd() % 4;   /* how often a restart appears */
        for (u32 i = 0; i < count; i++)
            if (density && rnd() % (density * 9) == 0) {
                u8* p = buf + (size_t)i * (is_u32 ? 4 : 2);
                if (is_u32) { p[0] = rv >> 24; p[1] = rv >> 16; p[2] = rv >> 8; p[3] = rv; } else { p[0] = 0xFF; p[1] = 0xFF; }
            }
        a.n = a.nc = b.n = b.nc = 0;
        for (u32 i = 0; i < count; i++) one(buf, i, is_u32, re, rv, base, &a);
        const u32 blk = is_u32 ? 4u : 8u; u32 fast_until = 0;
        for (u32 i = 0; i < count; i++) {
            if (i >= fast_until) {
                const u32 done = ld_gather_fast(buf + (size_t)i * (is_u32 ? 4u : 2u), count - i, is_u32, re, rv, base, b.r + b.n);
                b.n += done; i += done; fast_until = i + blk;
                if (i >= count) break;
            }
            one(buf, i, is_u32, re, rv, base, &b);
        }
        if (a.n != b.n || a.nc != b.nc || memcmp(a.r, b.r, a.n * sizeof a.r[0]) || memcmp(a.cuts, b.cuts, a.nc * 4)) {
            if (bad++ < 5) printf("trial %d: u32=%d restart=%d count=%u refs %u/%u cuts %u/%u\n", t, is_u32, re, count, a.n, b.n, a.nc, b.nc);
        }
    }
    printf(bad ? "FAIL %d\n" : "gather: 20000 random index buffers match the scalar loop\n", bad);
    return bad != 0;
}
