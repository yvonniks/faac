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

// MXU3 Constants (64-byte aligned)
static const mxu3_v16u32 mu_v ALIGN_SIMD = {0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                 0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df};
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

        // Load constants into high VPRs
        __asm__ __volatile__ (
            "move $t0, %0; .word 0x71000154\n\t" // LU1Q VPR10, sfac_v
            "move $t0, %1; .word 0x71000194\n\t" // LU1Q VPR11, magic_v
            : : "r"(&sfac_v), "r"(&magic_v) : "$t0", "memory"
        );

        for (; cnt <= n - 4; cnt += 4) {
            __asm__ __volatile__ (
                "move $t0, %0; .word 0x71000014\n\t" // Load x -> VPR0
                ".word 0x4bc00086\n\t"              // sign_mask = x < 0 ? -1 : 0 -> VPR1 (cltzw)
                ".word 0x4bc0000e\n\t"              // x = abs(x) -> VPR0
                ".word 0x4bc10080\n\t"              // temp = sqrt(x) -> VPR2
                ".word 0x4b020004\n\t"              // x = x * temp -> x * sqrt(x) -> VPR0
                ".word 0x4bc10000\n\t"              // x = sqrt(x) -> x^0.75 -> VPR0
                ".word 0x4b0a0004\n\t"              // x = x * gain^0.75 -> VPR0
                ".word 0x4b0b0000\n\t"              // x = x + magic -> VPR0
                ".word 0x4bc10014\n\t"              // x = truncate(x) -> VPR0
                ".word 0x4ac1003b\n\t"              // x = x ^ sign_mask -> VPR0
                ".word 0x4a21002e\n\t"              // x = x - sign_mask -> VPR0
                "move $t0, %1; .word 0x7100001C\n\t" // Store VPR0 -> xi
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
            ".word 0x71000313, 0x71000353, 0x71000393, 0x710003D3\n\t" // VPR3 = sfac
            "move $t0, %1\n\t"
            ".word 0x71000413, 0x71000453, 0x71000493, 0x710004D3\n\t" // VPR4 = magic
            "move $t0, %2\n\t"
            ".word 0x71000513, 0x71000553, 0x71000593, 0x710005D3\n\t" // VPR5 = mu
            : : "r"(&sfac_v), "r"(&magic_v), "r"(&mu_v) : "$t0", "memory"
        );

        for (; cnt <= n - 16; cnt += 16) {
             __asm__ __volatile__ (
                "move $t0, %0\n\t"
                ".word 0x71000013, 0x71000053, 0x71000093, 0x710000D3\n\t" // Load x -> VPR0
                ".word 0x4a000126, 0x4a010966, 0x4a0211a6, 0x4a0319e6\n\t" // VPR1 = sign_mask
                ".word 0x4a80000e, 0x4a81084e, 0x4a82108e, 0x4a8318ce\n\t" // VPR0 = abs(x)

                ".word 0x4aa00a32, 0x4aa10a72, 0x4aa20ab2, 0x4aa30af2\n\t" // VPR2 = x >> 1
                ".word 0x4a94420a, 0x4a954a4a, 0x4a96528a, 0x4a975aca\n\t" // VPR2 = mu - VPR2 -> rs1 guess
                ".word 0x4a604223, 0x4a614a63, 0x4a6252a3, 0x4a635ae3\n\t" // VPR2 = x * rs1 -> approx sqrt(x)

                ".word 0x4aa80b32, 0x4aa90b72, 0x4aaa0bb2, 0x4aab0bf2\n\t" // VPR11 = VPR2 >> 1
                ".word 0x4a956b0a, 0x4a956b4a, 0x4a977b8a, 0x4a977bca\n\t" // VPR11 = mu - VPR11 -> rs2 guess

                ".word 0x4a616023, 0x4a616863, 0x4a6370a3, 0x4a6378e3\n\t" // VPR0 = x * rs2 -> approx x^0.75
                ".word 0x4a606023, 0x4a616863, 0x4a6270a3, 0x4a6378e3\n\t" // apply gain
                ".word 0x4a808003, 0x4a818843, 0x4a829083, 0x4a8398c3\n\t" // apply magic

                ".word 0x70c0002e, 0x70c1086e, 0x70c210ae, 0x70c318ee\n\t" // truncate

                ".word 0x4a602006, 0x4a612846, 0x4a623086, 0x4a6338c6\n\t" // XORV
                ".word 0x4a80200a, 0x4a81284a, 0x4a82308a, 0x4a8338ca\n\t" // SUBW

                "move $t0, %1\n\t"
                ".word 0x71000057, 0x71000097, 0x710000D7, 0x71000117\n\t" // Store
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
