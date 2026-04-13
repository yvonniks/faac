#ifndef MXU3_SHIM_H
#define MXU3_SHIM_H

#ifdef __mips__
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

/*
 * mxu3_shim.h - Ingenic MXU3 512-bit VPR intrinsics for any MIPS32 compiler
 *
 * Provides the MXU3 (XBurst2 SIMD512) instruction set as portable inline
 * functions. Works with any MIPS32 GCC -- no -mmxu3 flag or Ingenic toolchain
 * required. Uses .word encodings verified on T41 (XBurst2 V0.0).
 *
 * Usage:
 *   #include "mxu3_shim.h"
 *   mxu3_v16i32 a = MXU3_LOAD(ptr_a);
 *   mxu3_v16i32 b = MXU3_LOAD(ptr_b);
 *   mxu3_v16i32 c = mxu3_addw(a, b);
 *   MXU3_STORE(ptr_c, c);
 *
 * REQUIREMENTS:
 *   - MIPS32 target (mipsel or mipseb)
 *   - Data pointers must be 64-byte aligned
 *   - Kernel with MXU3 COP2 handler (T40/T41 vendor kernels)
 *
 * HARDWARE MODEL (verified on T41):
 *   - 32 x 512-bit VPR registers, each = 4 x 128-bit quarters
 *   - LUQ/SUQ auto-increment the base register by 16
 *   - COP2 VPR fields use sub-register encoding: VPR N = N*4 + quarter
 *     VPR0={0,1,2,3}, VPR1={4,5,6,7}, VPR2={8,9,10,11}, ...
 *   - Each COP2 arithmetic instruction operates on ONE 128-bit quarter
 *   - Full 512-bit operation requires 4 instructions (one per quarter)
 *
 * ENCODING:
 *   COP2: (18<<26)|(minor<<21)|(vrp<<16)|(vrs<<11)|(vrd<<6)|funct
 *   LUQ:  (28<<26)|(base<<21)|(1<<11)|(vrd_qn<<6)|0x13  (auto-incr base+=16)
 *   SUQ:  (28<<26)|(base<<21)|(vrp_qn<<11)|0x57          (auto-incr base+=16)
 *
 * A1 SILICON BUG (from Ingenic GCC mips-mxu3-fix-bug-for-a1.md):
 *   A1 revision XBurst2 silicon has a data coherency bug affecting LUW and
 *   LUD loads (NOT LUQ/LUO). A sync instruction must be placed before each
 *   LUW/LUD to prevent data corruption. This shim uses LUQ exclusively for
 *   its inline functions, so they are safe on all revisions. If you use the
 *   raw MXU3_LUW/MXU3_LUD encoding constants on A1 silicon, prepend a sync.
 *
 * DEVICE DIFFERENCES (T40 vs T41):
 *   T40: MXU3.0 — has NNA, no MXU3.1 shuffle variants (gshufwb_1/2, gshufvb SIGILL)
 *   T41: MXU3.1 — has new shuffles, NNA may not be enabled (nnrwr etc. SIGILL)
 *   T40: MXU3 on CPU0 only without kernel patch (see t40-mxu3-fix.patch)
 */

#else
static inline int mxu3_available(void) { return 0; }
#endif /* __mips__ */
#endif /* MXU3_SHIM_H */
