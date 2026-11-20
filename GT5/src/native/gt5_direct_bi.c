/* gt5_direct_bi.c -- the run-time half of the direct branches (gt5_direct_bi.h, scripts/spu_native_patch.py
 * --direct-bi): one switch and one check for every image, the confirmation that an image's entries are what
 * the dispatcher resolves for a context, and the [direct-bi] lines.
 *
 * Why a direct branch gives what the dispatcher gives (spu_indirect_branch / spu_ib_full and the resolver
 * spu_lifted_lookup_full in ps3recomp/runtime/spu/spu_channels.c):
 *  - The dispatcher resolves ctx->pc by the context's resident code slots first (the first slot whose span
 *    holds pc: its image's function there, nothing if the slot is not verified), then the resident task,
 *    the resident overlay and the context's image. gt5_dbi_confirm reproduces that for the image and the
 *    branch's pc and stores the range around pc where the answer is the same (a slot image: inside its slot
 *    and clear of every earlier slot; the job manager: clear of every slot, no task, no overlay, the
 *    context's image). The answer is then the registry's function for (pc, image), and every entry of the
 *    image's table is checked once against the registry (spu_lookup) -- again whenever its count changed --
 *    so it is the very function the switch calls.
 *  - What the dispatcher does on the way besides the lookup, and how each is left to it: the return from
 *    an interrupt (irq_saved: never direct), the policy-module and taskset service addresses, the job
 *    return (0x3FF00), LS 0 and 0xA00 (never in a switch), resident_task := 0 below 0x3000 (direct only
 *    when it is 0 already; GT5 registers no taskset tasks, so the task branch of the full path never runs),
 *    the stack-reset entries (their hint byte: never direct), the counters (t_spu_ib_count: not counted).
 *    The drain loop's per-step work (an interrupt pending with interrupts on, the first step of a run) is
 *    left to it as for a lifted direct branch (SPU_DIRECT): never direct then.
 *  - A branch to an address where a host frame waits (the return address of a brsl / bisl of the image:
 *    spu_link / spu_drain_call in its lift) must reach the drain loop that waits for it: never in a switch.
 *  - Code changed in local store under a verified slot: the dispatcher's caches do not see it either
 *    (verified once per load), so neither does this.
 * GT5_DIRECT_BI=2 checks the confirmation itself: every direct branch asks the dispatcher's resolver
 * (spu_lifted_lookup) as well; a difference is counted, named, and that branch goes to the dispatcher. */
#include "gt5_direct_bi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*gt5_dbi_fn)(spu_context*);
gt5_dbi_fn spu_lookup(uint32_t addr, int image_id);                    /* spu_channels.c */
gt5_dbi_fn spu_lifted_lookup(const spu_context* ctx, uint32_t lsa);     /* spu_channels.c */
uint32_t spu_registry_size(void);                                       /* spu_channels.c */
int ps3_ab_on(const char* what);                                        /* sys_ppu_thread.c */
#define GT5_DBI_VERIFIED 2   /* spu_channels.c: SPU_OVERLAY_PENDING = 1, SPU_OVERLAY_VERIFIED, SPU_OVERLAY_REJECTED */

SPU_THREAD_LOCAL gt5_dbi_memo t_gt5_dbi;
int g_gt5_direct_bi = -1;            /* GT5_DIRECT_BI: 0, 1 (default), 2 */
static char s_off[512];              /* GT5_DIRECT_BI_OFF */
static gt5_dbi_image* s_images;
static volatile int s_lock;
static int s_ab = -1;                /* PS3_AB_WHAT=directbi: -1 not read, 0 no, 1 alternating */

static void dbi_init(void)
{
    const char* e = getenv("GT5_DIRECT_BI");
    g_gt5_direct_bi = (e && e[0] == '0') ? 0 : (e && e[0] == '2') ? 2 : 1;
    const char* o = getenv("GT5_DIRECT_BI_OFF");
    if (o) { snprintf(s_off + 1, sizeof s_off - 2, "%s", o); s_off[0] = ','; strcat(s_off, ","); }
    if (g_gt5_direct_bi != 1 || o) {
        fprintf(stderr, "[direct-bi] GT5_DIRECT_BI=%d%s%s%s\n", g_gt5_direct_bi,
                g_gt5_direct_bi == 2 ? " (every direct branch checked)" : g_gt5_direct_bi == 0 ? " (the dispatcher)" : "",
                o ? ", off for " : "", o ? o : "");
        fflush(stderr);
    }
}
static int dbi_listed_off(const char* name)
{
    char k[64];
    if (!s_off[0]) return 0;
    snprintf(k, sizeof k, ",%s,", name);
    return strstr(s_off, k) != NULL;
}
/* From the image's constructor (its patched lift), before any SPU code runs. */
void gt5_dbi_register(gt5_dbi_image* im)
{
    while (__atomic_exchange_n(&s_lock, 1, __ATOMIC_ACQUIRE)) {}
    if (g_gt5_direct_bi < 0) dbi_init();
    im->cfg = dbi_listed_off(im->name) ? 0 : g_gt5_direct_bi;
    im->mode = im->cfg;
    im->next = s_images; s_images = im;
    __atomic_store_n(&s_lock, 0, __ATOMIC_RELEASE);
}

/* Every entry of the image's table is the registry's function for (entry, image)? Once, and again when
 * the registry has grown. A difference leaves the whole image with the dispatcher. */
static __attribute__((noinline)) int dbi_table_ok(gt5_dbi_image* im, uint32_t rg)
{
    for (uint32_t i = 0; i < im->n; i++)
        if (spu_lookup(im->tab[i].pc, im->image_id) != im->tab[i].fn) {
            if (!__atomic_exchange_n(&im->bad, 1, __ATOMIC_RELAXED)) {
                fprintf(stderr, "[direct-bi] %s: the registry serves another function at 0x%05X: left with the dispatcher\n",
                        im->name, im->tab[i].pc);
                fflush(stderr);
            }
            return 0;
        }
    im->rgen = rg;
    return 1;
}

/* The branch to ctx->pc: is the dispatcher's answer the image's function there? If so, remember for this
 * thread the range around pc where it is, for this state of the context. */
int gt5_dbi_confirm(spu_context* ctx, const gt5_dbi_image* im0)
{
    gt5_dbi_image* im = (gt5_dbi_image*)im0;
    const uint32_t pc = ctx->pc;
    if (im->bad || !ctx->dcache_id) return 0;                  /* dcache_id 0: the dispatcher signs it first */
    { const uint32_t rg = spu_registry_size();
      if (im->rgen != rg + 1u && !dbi_table_ok(im, rg + 1u)) return 0; }
    uint32_t lo = im->lo, hi = im->hi;
    unsigned first = 4;
    for (unsigned s = 0; s < 4; s++)
        if (ctx->resident_code[s].image_id && pc - ctx->resident_code[s].lsa < ctx->resident_code[s].size) { first = s; break; }
    if (im->kind == 1) {
        if (first == 4) return 0;
        const int vs = ctx->resident_code[first].verify_state;
        if (ctx->resident_code[first].image_id != im->image_id || (vs && vs != GT5_DBI_VERIFIED)) return 0;
        const uint32_t a = ctx->resident_code[first].lsa, e = a + ctx->resident_code[first].size;
        if (a > lo) lo = a;
        if (e < hi) hi = e;
    } else {
        if (first != 4 || ctx->image_id != im->image_id || ctx->resident_ovl || ctx->resident_task) return 0;
    }
    /* clear of the slots the resolver looks at before (or, for the context's image, at all) */
    for (unsigned s = 0; s < first; s++) {
        if (!ctx->resident_code[s].image_id) continue;
        const uint32_t a = ctx->resident_code[s].lsa, e = a + ctx->resident_code[s].size;
        if (e <= pc) { if (e > lo) lo = e; }
        else if (a > pc) { if (a < hi) hi = a; }
    }
    if (pc - lo >= hi - lo) return 0;
    gt5_dbi_memo* m = &t_gt5_dbi;
    m->ctx = ctx; m->img = im; m->id = ctx->dcache_id; m->res = ctx->dcache_residency;
    m->iid = ctx->image_id; m->ovl = ctx->resident_ovl; m->lo = lo; m->hi = hi;
    return 1;
}

/* GT5_DIRECT_BI=2: the dispatcher's own answer for this branch. 1 = the same function. */
int gt5_dbi_check(spu_context* ctx, gt5_dbi_image* im)
{
    const uint32_t pc = ctx->pc;
    uint32_t lo = 0, hi = im->n;
    while (lo < hi) { const uint32_t mid = (lo + hi) / 2; if (im->tab[mid].pc < pc) lo = mid + 1; else hi = mid; }
    if (lo >= im->n || im->tab[lo].pc != pc) return 1;          /* not an entry: the dispatcher takes it anyway */
    const gt5_dbi_fn want = im->tab[lo].fn, got = spu_lifted_lookup(ctx, pc);
    const long long n = __atomic_add_fetch(&im->checked, 1, __ATOMIC_RELAXED);
    long long bad = im->mismatch;
    if (got != want) {
        bad = __atomic_add_fetch(&im->mismatch, 1, __ATOMIC_RELAXED);
        if (bad <= 8) {
            fprintf(stderr, "[direct-bi] %s: MISMATCH at %05X (image %d, slot images %d %d %d %d, overlay %d, task %d): the dispatcher %p, the switch %p\n",
                    im->name, pc, ctx->image_id, ctx->resident_code[0].image_id, ctx->resident_code[1].image_id,
                    ctx->resident_code[2].image_id, ctx->resident_code[3].image_id, ctx->resident_ovl, ctx->resident_task,
                    (void*)got, (void*)want);
            fflush(stderr);
        }
    }
    if ((n & ((1ll << 22) - 1)) == 0 || (got != want && bad <= 8)) {
        fprintf(stderr, "[direct-bi] %s: checked %lld, MISMATCH %lld\n", im->name, n, bad);
        fflush(stderr);
    }
    return got == want;
}

/* An image's descriptor, for natives that take its direct branches themselves (NULL: none built). */
gt5_dbi_image* gt5_dbi_find(const char* name)
{
    for (gt5_dbi_image* im = s_images; im; im = im->next) if (!strcmp(im->name, name)) return im;
    return NULL;
}
#ifdef GT5_DBI_HARNESS   /* the stand-alone tests: set every image's mode, find an image */
void gt5_dbi_harness_mode(int m) { for (gt5_dbi_image* im = s_images; im; im = im->next) im->mode = m; }
gt5_dbi_image* gt5_dbi_harness_find(const char* name) { return gt5_dbi_find(name); }
#endif

/* PS3_AB_PERIOD=<s> PS3_AB_WHAT=directbi: every image's mode and 0 alternate (polled by a caller that runs
 * often whatever the mode: the w018CAD00 job's output allocator). */
void gt5_dbi_ab_poll(void)
{
    if (s_ab < 0) s_ab = ps3_ab_on("directbi") >= 0;
    if (!s_ab) return;
    const int on = ps3_ab_on("directbi") == 1;
    for (gt5_dbi_image* im = s_images; im; im = im->next) im->mode = on ? im->cfg : 0;
}
