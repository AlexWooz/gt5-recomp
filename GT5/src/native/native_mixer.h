/* Native fast paths for the mixer job jD58063CD: see native_mixer.c and notes.md. */
#pragma once
#include "spu_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -1 = read GT5_NATIVE_MIXER at the first call (default 1); 0 = every call goes straight to the
 * lifted function; 1 = native fast blocks; 2 = run both and compare (a debug mode: slow). */
extern int g_native_mixer;
extern unsigned long long g_native_cmp_calls, g_native_cmp_mismatches;   /* mode 2 */

#ifndef NATIVE_HOOKED
void gt5_jD58063CD_native_00004720(spu_context* ctx);
void gt5_jD58063CD_native_00004C80(spu_context* ctx);
void gt5_jD58063CD_native_000044CC(spu_context* ctx);
void gt5_jD58063CD_native_00004E58(spu_context* ctx);
SPU_SV void gt5_jD58063CD_native_00004720_sv(spu_context* ctx);
SPU_SV void gt5_jD58063CD_native_00004C80_sv(spu_context* ctx);
SPU_SV void gt5_jD58063CD_native_000044CC_sv(spu_context* ctx);
SPU_SV void gt5_jD58063CD_native_00004E58_sv(spu_context* ctx);
/* 0x1908 replaces the ADPCM block loop (0x1908 + 0x1A2C); see native_mixer.c */
void gt5_jD58063CD_native_00001908(spu_context* ctx);
SPU_SV void gt5_jD58063CD_native_00001908_sv(spu_context* ctx);
#endif

enum { NATIVE_FN_4720, NATIVE_FN_4C80, NATIVE_FN_44CC, NATIVE_FN_4E58, NATIVE_FN_1908, NATIVE_FN_COUNT };
#ifdef NATIVE_STATS
typedef struct native_mixer_stats {
    unsigned long long calls;           /* entries */
    unsigned long long fast_calls;      /* entries where the fast block advanced the loop */
    unsigned long long fast_iters;      /* loop passes (samples; lines for 0x4E58) the fast block did */
    unsigned long long bail_precond;    /* fast block not entered: alignment / count / tables / wrap */
    unsigned long long bail_nonfinite;  /* fast block stopped early on a word with exponent 255 */
    unsigned long long plain, redone;   /* 0x1908 only: blocks kept from the clamp-free pass / decoded again with the clamp */
} native_mixer_stats;
extern native_mixer_stats g_native_stats[NATIVE_FN_COUNT];
extern int g_native_fault;              /* test only: fast blocks flip one bit of their last store */
#endif

#ifdef __cplusplus
}
#endif
