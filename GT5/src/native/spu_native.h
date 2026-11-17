/* Native replacements of lifted SPU functions (see scripts/spu_native_patch.py and natives.txt).
 *
 * A lifted SPU function does what the PS3's SPU did, step for step. A native one does the same
 * job the way a PC would, and must leave the context and the local store exactly as the lifted one
 * does. GT5_SPU_NATIVE picks which runs:
 *   1  native (default)
 *   0  lifted, as before
 *   2  both: the lifted one on a copy, the native one for real, and every register and local-store
 *      byte compared; a difference is logged ("[spu-native] MISMATCH ...") and the lifted result
 *      is kept, so the game goes on correctly. Slow (a 256 KB copy per call): for testing.
 */
#ifndef GT5_SPU_NATIVE_H
#define GT5_SPU_NATIVE_H
#include "spu_context.h"
#ifdef __cplusplus
extern "C" {
#endif
extern int g_gt5_spu_native;
void gt5_spu_native_compare(spu_context* ctx, void (*native)(spu_context*),
                            void (SPU_SV *lifted)(spu_context*), const char* name);
#ifdef __cplusplus
}
#endif
#endif
