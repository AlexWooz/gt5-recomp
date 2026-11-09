/* rsx_vertex_remap_build against a plain reference: random draws, compact and scattered index runs,
 * one base or several, repeats, sizes from 2 to 100k. The unique order, the unique count and every
 * occurrence's slot must match first-seen order exactly. */
#include "rsx_vertex_compact.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long s = 88172645463325252ull;
static unsigned rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)s; }

int main(void)
{
    rsx_vertex_remap remap; memset(&remap, 0, sizeof remap);
    int bad = 0;
    for (int trial = 0; trial < 3000; trial++) {
        const unsigned count = 2 + (trial % 7 == 0 ? rnd() % 100000 : rnd() % 3000);
        const int mode = rnd() % 4;               /* 0 compact, 1 scattered, 2 two bases, 3 tiny range */
        const unsigned lo = rnd() % 900000;
        const unsigned range = mode == 3 ? 1 + rnd() % 8 : mode == 1 ? 1 + rnd() % 0x7FFFFFFF : 1 + rnd() % (count * 2 + 10);
        rsx_vertex_ref* refs = malloc(count * sizeof *refs);
        rsx_vertex_ref* orig = malloc(count * sizeof *refs);
        for (unsigned i = 0; i < count; i++) {
            refs[i].vertex_id = lo + rnd() % range;
            refs[i].base_index = mode == 2 ? (rnd() & 1) * 5 : 7;
        }
        memcpy(orig, refs, count * sizeof *refs);
        unsigned uniq = 0;
        if (!rsx_vertex_remap_build(&remap, refs, count, &uniq)) { printf("build failed trial %d\n", trial); return 1; }
        /* Reference: first-seen order by linear search over the uniques so far (fine at these sizes). */
        rsx_vertex_ref* ref_u = malloc(count * sizeof *ref_u);
        unsigned* ref_occ = malloc(count * sizeof *ref_occ);
        unsigned ru = 0;
        /* a simple open hash for the reference, independent of the code under test */
        unsigned cap = 1; while (cap < count * 4) cap <<= 1;
        long long* hk = malloc(cap * sizeof *hk); unsigned* hv = malloc(cap * sizeof *hv);
        for (unsigned i = 0; i < cap; i++) hk[i] = -1;
        for (unsigned i = 0; i < count; i++) {
            const long long key = ((long long)orig[i].base_index << 32) | orig[i].vertex_id;
            unsigned h = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 40) & (cap - 1);
            while (hk[h] != -1 && hk[h] != key) h = (h + 1) & (cap - 1);
            if (hk[h] == -1) { hk[h] = key; hv[h] = ru; ref_u[ru++] = orig[i]; }
            ref_occ[i] = hv[h];
        }
        if (ru != uniq) { if (bad++ < 5) printf("trial %d mode %d count %u: unique %u vs %u\n", trial, mode, count, uniq, ru); }
        else {
            for (unsigned i = 0; i < ru; i++)
                if (refs[i].vertex_id != ref_u[i].vertex_id || refs[i].base_index != ref_u[i].base_index) {
                    if (bad++ < 5) printf("trial %d mode %d: unique %u differs\n", trial, mode, i);
                    break;
                }
            for (unsigned i = 0; i < count; i++)
                if (rsx_vertex_remap_index(&remap, i) != ref_occ[i]) {
                    if (bad++ < 5) printf("trial %d mode %d: occurrence %u -> %u vs %u\n", trial, mode, i, rsx_vertex_remap_index(&remap, i), ref_occ[i]);
                    break;
                }
        }
        free(refs); free(orig); free(ref_u); free(ref_occ); free(hk); free(hv);
    }
    printf(bad ? "FAIL: %d bad trials\n" : "remap: 3000 random draws match first-seen order\n", bad);
    return bad != 0;
}
