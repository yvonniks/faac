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

// Fast rsqrt approximation for MXU3 (no native rsqrt in shim)
static inline mxu3_v16i32 mxu3_rsqrt_optimized(mxu3_v16i32 x, mxu3_v16i32 mu, mxu3_v16i32 ou, mxu3_v16i32 thu, mxu3_v16i32 hu) {
    mxu3_v16i32 i = mxu3_subw(mu, mxu3_srlw(x, ou));
    mxu3_v16i32 xhalf = mxu3_fmulw(x, hu);
    mxu3_v16i32 y2 = mxu3_fmulw(i, i);
    mxu3_v16i32 factor = mxu3_fsubw(thu, mxu3_fmulw(xhalf, y2));
    return mxu3_fmulw(i, factor);
}
#endif

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
#ifdef __mips__
    const uintptr_t align_mask = (uintptr_t)FAAC_SIMD_ALIGNMENT - 1;
    if (n >= 4 && !((uintptr_t)xr & align_mask) && !((uintptr_t)xi & align_mask)) {
        const float sf075 = powf((float)sfacfix, 0.75f);
        const mxu2_v4f32 sfac_v = {sf075, sf075, sf075, sf075};
        const float mg_f = (float)MAGIC_NUMBER;
        const mxu2_v4f32 magic_v = {mg_f, mg_f, mg_f, mg_f};
        const mxu2_v4i32 abs_m = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
        const mxu2_v4i32 zero_v = {0, 0, 0, 0};
        for (; cnt <= n - 4; cnt += 4) {
            mxu2_v4i32 x_orig_i = mxu2_load(&xr[cnt]);
            mxu2_v4i32 sign_mask = mxu2_clts_w(x_orig_i, zero_v);
            mxu2_v4f32 x = (mxu2_v4f32)mxu2_andv((mxu2_v16i8)x_orig_i, (mxu2_v16i8)abs_m);
            // x^0.75 = sqrt(x * sqrt(x))
            mxu2_v4f32 s1 = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
            x = mxu2_fmul_w(x, s1);
            x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
            x = mxu2_fmul_w(x, sfac_v);
            x = mxu2_fadd_w(x, magic_v);
            mxu2_v4i32 xi_vec = mxu2_vtruncsws((mxu2_v4i32)x);
            xi_vec = (mxu2_v4i32)mxu2_sub_w((mxu2_v4i32)mxu2_xorv((mxu2_v16i8)xi_vec, (mxu2_v16i8)sign_mask), sign_mask);
            mxu2_store(&xi[cnt], xi_vec);
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
        const mxu3_v16f32 sfac_v = {sf075, sf075, sf075, sf075, sf075, sf075, sf075, sf075,
                                    sf075, sf075, sf075, sf075, sf075, sf075, sf075, sf075};
        const mxu3_v16f32 magic_v = {(float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER,
                                     (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER,
                                     (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER,
                                     (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER, (float)MAGIC_NUMBER};
        const mxu3_v16u32 abs_m_v = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                     0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                     0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
                                     0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
        const mxu3_v16u32 mu_v = {0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                  0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                  0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df,
                                  0x5f3759df, 0x5f3759df, 0x5f3759df, 0x5f3759df};
        const mxu3_v16u32 ou_v = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        const mxu3_v16f32 thu_v = {1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f,
                                   1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f, 1.5f};
        const mxu3_v16f32 hu_v = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
                                  0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        const mxu3_v16i32 zero_v = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

        const mxu3_v16i32 abs_m = (mxu3_v16i32)abs_m_v;
        const mxu3_v16i32 mu = (mxu3_v16i32)mu_v;
        const mxu3_v16i32 ou = (mxu3_v16i32)ou_v;
        const mxu3_v16i32 thu = (mxu3_v16i32)thu_v;
        const mxu3_v16i32 hu = (mxu3_v16i32)hu_v;

        for (; cnt <= n - 16; cnt += 16) {
            mxu3_v16i32 x = MXU3_LOAD(&xr[cnt]);
            mxu3_v16i32 sign_mask = mxu3_cltsw(x, zero_v);
            x = mxu3_andv(x, abs_m);
            // x^0.75 = x * x^-0.5 * (x^-0.5)^-0.5 = x * rs1 * rs2
            mxu3_v16i32 rs1 = mxu3_rsqrt_optimized(x, mu, ou, thu, hu);
            mxu3_v16i32 rs2 = mxu3_rsqrt_optimized(rs1, mu, ou, thu, hu);
            x = mxu3_fmulw(x, rs1);
            x = mxu3_fmulw(x, rs2);
            x = mxu3_fmulw(x, (mxu3_v16i32)sfac_v);
            x = mxu3_faddw(x, (mxu3_v16i32)magic_v);
            mxu3_v16i32 xi_vec = mxu3_ftruncsw(x);
            xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);
            MXU3_STORE(&xi[cnt], xi_vec);
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
