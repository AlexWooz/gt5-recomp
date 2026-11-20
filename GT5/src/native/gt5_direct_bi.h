/* gt5_direct_bi.h -- direct branches in the lifted SPU job images.
 *
 * scripts/spu_native_patch.py --direct-bi rewrites an image's branches to a register (`bi`, `biz`, `binz`,
 * `bie`/`bid` to the next instruction, loop back edges chosen with selb) and its calls through a register
 * (`bisl`) so that, when the register holds one of the image's own entries, control goes to that entry's
 * lifted function directly instead of through the runtime's dispatcher (spu_indirect_branch). It does so
 * only where the dispatcher would choose that very function and change nothing else on the way
 * (gt5_dbi_ready below, gt5_dbi_confirm in gt5_direct_bi.c); anything else still goes to the dispatcher.
 * GT5_DIRECT_BI=0 (read at start): every image as before; 2: each direct branch checked against the
 * dispatcher's own lookup first ("[direct-bi] <image>: checked N, MISMATCH M"); GT5_DIRECT_BI_OFF=<image,...>
 * leaves the named images with the dispatcher. */
#ifndef GT5_DIRECT_BI_H
#define GT5_DIRECT_BI_H
#include <stdint.h>
#include "spu_context.h"

typedef struct { uint32_t pc; void (*fn)(spu_context*); } gt5_dbi_ent;

typedef struct gt5_dbi_image {
    volatile int mode;            /* 0 the dispatcher, 1 direct, 2 direct and checked (set at registration) */
    const char* name;
    int image_id;                 /* the id the image registers its functions under (src/gen/spu_jobs.c) */
    int kind;                     /* 1: resident code at [lsa, lsa + size) under the job manager (a WWS job's
                                     code); 0: the context's own image (the WWS job manager) */
    uint32_t lsa, size;
    uint32_t lo, hi;              /* the lowest entry, the highest + 4 */
    const gt5_dbi_ent* tab;       /* the entries, by address: the image's own function table, less the
                                     return points (where a waiting host frame takes the branch) and the
                                     runtime's service addresses */
    uint32_t n;
    int cfg;                      /* the mode it was configured with (PS3_AB_WHAT=directbi switches 0 / cfg) */
    volatile int bad;             /* the registry serves another function at one of the entries */
    volatile uint32_t rgen;       /* the registry count the entries were last checked against (0: never) */
    volatile long long checked, mismatch;
    struct gt5_dbi_image* next;
} gt5_dbi_image;

/* The context state a confirmation holds for: within [lo, hi) the image's entries are what the dispatcher
 * resolves, until the context's resident code, image, overlay or the image changes. */
typedef struct {
    const spu_context* ctx;
    const gt5_dbi_image* img;
    uint32_t id, res;             /* ctx->dcache_id, ctx->dcache_residency (the resident code's signature) */
    int iid, ovl;                 /* ctx->image_id, ctx->resident_ovl */
    uint32_t lo, hi;
} gt5_dbi_memo;
extern SPU_THREAD_LOCAL gt5_dbi_memo t_gt5_dbi;

void gt5_dbi_register(gt5_dbi_image* im);
gt5_dbi_image* gt5_dbi_find(const char* name);
int gt5_dbi_confirm(spu_context* ctx, const gt5_dbi_image* im);
int gt5_dbi_check(spu_context* ctx, gt5_dbi_image* im);
void gt5_dbi_ab_poll(void);

/* May the branch to ctx->pc (one of im's entries) go there directly? Only when the drain loop has nothing
 * to do for this step (as for a lifted direct branch: SPU_DIRECT) and the dispatcher's fast path would
 * take it plainly: no interrupt being returned from, no task resident, the image's entries confirmed for
 * the context's present resident code, image and overlay. */
static inline int gt5_dbi_ready(spu_context* ctx, const gt5_dbi_image* im)
{
    const uint32_t pc = ctx->pc;
    if (!(g_spu_direct > 0 && ctx->steps && !g_spu_stack_reset_hint[(pc >> 2) & 1023u] &&
          !(ctx->int_enable && (ctx->event_status & ctx->event_mask)) && !ctx->irq_saved && !ctx->resident_task))
        return 0;
    const gt5_dbi_memo* m = &t_gt5_dbi;
    if (m->ctx == ctx && m->img == im && m->id == ctx->dcache_id && m->res == ctx->dcache_residency &&
        m->iid == ctx->image_id && m->ovl == ctx->resident_ovl && pc - m->lo < m->hi - m->lo)
        return 1;
    return gt5_dbi_confirm(ctx, im);
}
#endif
