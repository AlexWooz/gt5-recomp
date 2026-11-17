/*
 * native_geo.h -- the one switch shared by the native x86 replacements of lifted SPU functions in
 * GT5's worker job images (native_w01606C00.c, native_w0163C080.c, ...).
 *
 * GT5_NATIVE_GEO, read once from the environment:
 *     0   lifted code only
 *     1   native replacements (default)
 *     2   run both, compare every register, pc and local store, log
 *         "[native-geo] MISMATCH <image>:<addr> ..." and keep the lifted result (slow: debugging)
 *
 * This header and native_geo_switch.c include no spu_recomp.h, so one copy serves every image.
 * Compile native_geo_switch.c ONCE per program (the copies in the natives' folders are identical).
 */
#ifndef NATIVE_GEO_H
#define NATIVE_GEO_H

#ifdef __cplusplus
extern "C" {
#endif

/* -1 = GT5_NATIVE_GEO not read yet, else 0 / 1 / 2. A test harness may set it directly. */
extern int g_native_geo;
/* Compare mode: calls compared, calls that differed (all images together). */
extern unsigned long long g_native_geo_cmp_calls, g_native_geo_cmp_mismatches;

/* Reads GT5_NATIVE_GEO, prints "[native-geo] GT5_NATIVE_GEO=n" once, returns the mode. */
int native_geo_init(void);
/* Compare mode found a difference: counts it and logs the first 20,
 * "[native-geo] MISMATCH <image>:<addr> <detail>". */
void native_geo_mismatch(const char* image, unsigned addr, const char* detail);
/* Compare mode compared one call. */
void native_geo_compared(void);

/* Interrupts. The lifted code looks for a pending, enabled event at every label and leaves through
 * the interrupt path when there is one. GT5's worker jobs run with interrupts enabled all the time
 * (the job manager's stall-and-notify handler), so "only with interrupts off" would mean never. A
 * native function runs when no enabled event is pending as it starts. One raised while it runs (by
 * another thread: a lost reservation, a signal) is taken at the next label of the lifted code that
 * follows, some microseconds later than the lifted function would have taken it; an interrupt has
 * no fixed place in the instruction stream, so both are executions the hardware allows. With none
 * pending, the lifted function's checks do nothing, so the results are the same. */
#define NATIVE_GEO_IRQ_PENDING(ctx) ((ctx)->int_enable != 0 && ((ctx)->event_status & (ctx)->event_mask) != 0)

static inline __attribute__((always_inline)) int native_geo_mode(void) {
    const int m = g_native_geo;
    return __builtin_expect(m >= 0, 1) ? m : native_geo_init();
}

#ifdef __cplusplus
}
#endif
#endif
