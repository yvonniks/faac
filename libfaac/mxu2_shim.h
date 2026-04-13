#ifndef MXU2_SHIM_H
#define MXU2_SHIM_H

#ifdef __mips__
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>

/*
 * mxu2_shim.h - Ingenic MXU2 128-bit VPR intrinsics for any MIPS32 compiler
 *
 * Provides the complete MXU2 instruction set as portable inline functions.
 * Works with GCC 4.x through GCC 15+ -- no -mmxu2 flag or Ingenic toolchain
 * required. Uses .word encodings verified on T20 (XBurst1 V0.1) and T31
 * (XBurst1 V0.0) with stock Thingino kernels.
 *
 * If compiled with the Ingenic GCC 4.7.2 toolchain (-mmxu2), this header
 * falls back to the native __builtin_mxu2_* intrinsics automatically.
 *
 * Usage:
 *   #include "mxu2_shim.h"
 *   mxu2_v4i32 a = MXU2_LOAD(ptr_a);
 *   mxu2_v4i32 b = MXU2_LOAD(ptr_b);
 *   mxu2_v4i32 c = mxu2_add_w(a, b);
 *   MXU2_STORE(ptr_c, c);
 *
 * REQUIREMENTS:
 *   - MIPS32 target (mipsel or mipseb)
 *   - Data pointers passed to MXU2_LOAD/STORE must be 16-byte aligned
 *   - Kernel with MXU2 COP2 notifier (all Ingenic production kernels with
 *     mxu-v2-ex.obj blob -- T20, T21, T23, T30, T31, T32)
 *
 * ENCODING REFERENCE:
 *
 *   COP2 CO (binary/unary VPR arithmetic):
 *     (18<<26)|(1<<25)|(major<<21)|(vt<<16)|(vs<<11)|(vd<<6)|minor
 *     Shim convention: VPR0=src1(vs=0), VPR1=src2(vt=1), VPR2=dst(vd=2)
 *
 *     Major  Group              Ops (minor ranges)
 *     -----  -----------------  -----------------------------------------
 *      0     Compare/MinMax     ceq/cne/clt/cle (40-63), maxa-minu (0-23)
 *      0     Shifts (reg amt)   sra/srl/srar/srlr (24-39)
 *      1     Add/Sub            add/sub/addss/subss/adduu/subuu... (0-47)
 *      1     Average/SLL        aves/aveu/avers/averu (48-63), sll (40-43)
 *      2     Mul/Div/Mod        mul/divs/divu/mods/modu (0-27)
 *      2     Madd/Msub          madd/msub (12-23, accumulate into vd)
 *      2     Dot/Dadd/Dsub      dotps/dotpu/dadds/daddu/dsubs/dsubu (33-63)
 *      6     128-bit logic      andv(56)/norv(57)/orv(58)/xorv(59)
 *      8     Float arith        fadd-fdiv/fmadd/fmsub (0-11)
 *      8     Float compare      fcor/fceq/fclt/fcle (16-23)
 *      8     Float min/max      fmax/fmaxa/fmin/fmina (24-31)
 *      8     Q-format mul       mulq/mulqr/maddq/msubq (40-55)
 *      8     Vector convert     vcvths/vcvtsd/vcvtqhs/vcvtqwd (12-15)
 *     14     Unary int (vt=0)   ceqz-clez (0-15), loc/lzc (16-23), bcnt (48-51)
 *     14     Unary flt (vt=1)   fsqrt/fclass (0-7), vcvt* (8-57)
 *
 *   SPECIAL2 (opcode=0x1C) immediate ops:
 *     f0x38: sats(v=0)/satu(v=2)/slli(v=4)  rs=sz*8+v, imm in bits 15-11
 *     f0x39: srai(v=0)/srari(v=2)/srli(v=4)/srlri(v=6)
 *     f0x30: andib(0)/norib(8)/orib(16)/xorib(24)  rs=op_sel
 *     f0x35: repi  rs=sz*8, idx in bits 20-16
 *     f0x19: bselv (3-op boolean select)
 *     f0x18: shufv (3-op shuffle)
 *
 *   LU1Q/SU1Q (SPECIAL2 load/store):
 *     (0x1C<<26)|(base<<21)|(offset_idx<<11)|(vpr<<6)|funct
 *     funct: LU1Q=0x14, SU1Q=0x1C; addr = base + offset_idx*16
 */

#else
static inline int mxu2_available(void) { return 0; }
#endif /* __mips__ */
#endif /* MXU2_SHIM_H */
