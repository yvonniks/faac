/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "faac_real.h"
#include "quantize.h"
#include "cpu_compute.h"
#include "util.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef FAAC_PRECISION_SINGLE
#error MXU SIMD quantization only supports single precision float.
#endif

#ifdef __mips__
#include "mxu2_shim.h"
#include "mxu3_shim.h"

/* Helper macros for integrated assembly loops using shim encodings */
#define MXU2_W_OP(m, vt, vs, vd, mi) _MXU2_WORD(_MXU2_OP(m, vt, vs, vd, mi))
#define MXU2_W_LU(b, i, v) _MXU2_WORD(_MXU2_LU1Q(b, i, v))
#define MXU2_W_SU(b, i, v) _MXU2_WORD(_MXU2_SU1Q(b, i, v))

#define MXU3_OP_V(base, vrp, vrs, vrd) ((base) | ((vrp)<<16) | ((vrs)<<11) | ((vrd)<<6))

#define MXU3_W(op, vrp, vrs, vrd) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+0, (vrs)*4+0, (vrd)*4+0)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+1, (vrs)*4+1, (vrd)*4+1)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+2, (vrs)*4+2, (vrd)*4+2)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+3, (vrs)*4+3, (vrd)*4+3))

#define MXU3_W_IMM(op, vrp, imm, vrd) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+0, (imm), (vrd)*4+0)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+1, (imm), (vrd)*4+1)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+2, (imm), (vrd)*4+2)) \
    _MXU3_WORD(MXU3_OP_V(op, (vrp)*4+3, (imm), (vrd)*4+3))

#define MXU3_W_LU(base, vrd) \
    _MXU3_WORD(_MXU3_LUQ(base, (vrd)*4+0)) \
    _MXU3_WORD(_MXU3_LUQ(base, (vrd)*4+1)) \
    _MXU3_WORD(_MXU3_LUQ(base, (vrd)*4+2)) \
    _MXU3_WORD(_MXU3_LUQ(base, (vrd)*4+3))

#define MXU3_W_SU(base, vrp) \
    _MXU3_WORD(_MXU3_SUQ(base, (vrp)*4+0)) \
    _MXU3_WORD(_MXU3_SUQ(base, (vrp)*4+1)) \
    _MXU3_WORD(_MXU3_SUQ(base, (vrp)*4+2)) \
    _MXU3_WORD(_MXU3_SUQ(base, (vrp)*4+3))

/* MXU3 Opcode bases matching shim constants but with zeroed register fields */
#define MXU3_OP_CLTZW     0x4a000026
#define MXU3_OP_ANDV      0x4a600002
#define MXU3_OP_FADDW     0x4a800003
#define MXU3_OP_FMULW     0x4a600023
#define MXU3_OP_FTRUNCSW  0x70c0002e
#define MXU3_OP_XORV      0x4a600006
#define MXU3_OP_SUBW      0x4a80000a
#define MXU3_OP_SRLIW     0x4aa00032

// MXU3 Constants (64-byte aligned)
static const mxu3_v16u32 mu_v ALIGN_SIMD = {0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df};

static const mxu3_v16u32 am3_v ALIGN_SIMD = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                  0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                  0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                  0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
#endif

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
#ifdef __mips__
    const uintptr_t align_mask = (uintptr_t)FAAC_SIMD_ALIGNMENT - 1;
    if (n >= 4 && !((uintptr_t)xr & align_mask) && !((uintptr_t)xi & align_mask)) {
        const float sf075 = powf((float)sfacfix, 0.75f);
        const float mg_f = (float)MAGIC_NUMBER;

        mxu2_v4f32 sfac_v = {sf075, sf075, sf075, sf075};
        mxu2_v4f32 magic_v = {mg_f, mg_f, mg_f, mg_f};
        mxu2_v4u32 abs_m = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};

        // Load constants into high VPRs
        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            MXU2_W_LU(8, 0, 10) // LU1Q VPR10, sfac_v
            "move $t0, %1\n\t"
            MXU2_W_LU(8, 0, 11) // LU1Q VPR11, magic_v
            "move $t0, %2\n\t"
            MXU2_W_LU(8, 0, 12) // LU1Q VPR12, abs_m
            : : "r"(&sfac_v), "r"(&magic_v), "r"(&abs_m) : "$t0", "memory"
        );

        for (; cnt <= n - 4; cnt += 4) {
            __asm__ __volatile__ (
                "move $t0, %0\n\t"
                MXU2_W_LU(8, 0, 0) // Load x -> VPR0
                MXU2_W_OP(14, 0, 0, 1, 0x0A) // cltzw VPR1, VPR0 (sign mask)
                MXU2_W_OP(6, 12, 0, 0, 0x38) // andv VPR0, VPR0, VPR12 (abs_m)
                MXU2_W_OP(14, 1, 0, 2, 0x00) // fsqrt VPR2, VPR0
                MXU2_W_OP(8, 2, 0, 0, 0x04)  // fmul VPR0, VPR0, VPR2 (x * sqrt(x))
                MXU2_W_OP(14, 1, 0, 0, 0x00) // fsqrt VPR0, VPR0 (x^0.75)
                MXU2_W_OP(8, 10, 0, 0, 0x04) // fmul VPR0, VPR0, VPR10 (sfac_v)
                MXU2_W_OP(8, 11, 0, 0, 0x00) // fadd VPR0, VPR0, VPR11 (magic_v)
                MXU2_W_OP(14, 1, 0, 0, 0x14) // vtruncsws VPR0, VPR0
                MXU2_W_OP(6, 1, 0, 0, 0x3B)  // xorv VPR0, VPR0, VPR1
                MXU2_W_OP(1, 1, 0, 0, 0x2E)  // subw VPR0, VPR0, VPR1
                "move $t0, %1\n\t"
                MXU2_W_SU(8, 0, 0) // Store VPR0 -> xi
                : : "r"(&xr[cnt]), "r"(&xi[cnt]) : "$t0", "memory"
            );
        }
    }
#endif
    for (; cnt < n; cnt++) {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

void quantize_mxu3(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
#ifdef __mips__
    const uintptr_t align_mask = (uintptr_t)FAAC_SIMD_ALIGNMENT - 1;
    if (n >= 16 && !((uintptr_t)xr & align_mask) && !((uintptr_t)xi & align_mask)) {
        const float sf075 = powf((float)sfacfix, 0.75f);
        const float mg_f = (float)MAGIC_NUMBER;

        mxu3_v16f32 sfac_v ALIGN_SIMD, magic_v ALIGN_SIMD;
        for(int i=0; i<16; i++) { sfac_v[i] = sf075; magic_v[i] = mg_f; }

        __asm__ __volatile__ (
            "move $t0, %0\n\t"
            MXU3_W_LU(8, 3) // VPR3 = sfac
            "move $t0, %1\n\t"
            MXU3_W_LU(8, 4) // VPR4 = magic
            "move $t0, %2\n\t"
            MXU3_W_LU(8, 5) // VPR5 = mu
            "move $t0, %3\n\t"
            MXU3_W_LU(8, 6) // VPR6 = am3_v
            : : "r"(&sfac_v), "r"(&magic_v), "r"(&mu_v), "r"(&am3_v) : "$t0", "memory"
        );

        for (; cnt <= n - 16; cnt += 16) {
             __asm__ __volatile__ (
                "move $t0, %0\n\t"
                MXU3_W_LU(8, 0) // Load x -> VPR0
                MXU3_W(MXU3_OP_CLTZW, 0, 0, 1) // VPR1 = cltzw(VPR0) (sign mask)
                MXU3_W(MXU3_OP_ANDV, 0, 6, 0)  // VPR0 = abs(VPR0)

                // Fast approx x^0.75
                MXU3_W_IMM(MXU3_OP_SRLIW, 0, 1, 2)    // VPR2 = VPR0 >> 1
                MXU3_W(MXU3_OP_SUBW, 5, 2, 2)         // VPR2 = VPR5 - VPR2 (rs1 guess)
                MXU3_W(MXU3_OP_FMULW, 0, 2, 2)        // VPR2 = VPR0 * VPR2 (sqrt(x) approx)

                MXU3_W_IMM(MXU3_OP_SRLIW, 2, 1, 11)   // VPR11 = VPR2 >> 1
                MXU3_W(MXU3_OP_SUBW, 5, 11, 11)       // VPR11 = VPR5 - VPR11 (rs2 guess)
                MXU3_W(MXU3_OP_FMULW, 0, 11, 0)       // VPR0 = VPR0 * VPR11 (x^0.75 approx)

                MXU3_W(MXU3_OP_FMULW, 0, 3, 0)        // VPR0 = VPR0 * sfac_v
                MXU3_W(MXU3_OP_FADDW, 0, 4, 0)        // VPR0 = VPR0 + magic_v

                MXU3_W(MXU3_OP_FTRUNCSW, 0, 0, 0)     // VPR0 = truncate(VPR0)

                MXU3_W(MXU3_OP_XORV, 0, 1, 0)         // x = x ^ sign_mask
                MXU3_W(MXU3_OP_SUBW, 0, 1, 0)         // x = x - sign_mask

                "move $t0, %1\n\t"
                MXU3_W_SU(8, 0) // Store VPR0 -> xi
                : : "r"(&xr[cnt]), "r"(&xi[cnt]) : "$t0", "memory"
            );
        }
    }
#endif
    for (; cnt < n; cnt++) {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
