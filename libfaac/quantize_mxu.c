/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "faac_real.h"
#include "quantize.h"
#include "cpu_compute.h"

#ifndef FAAC_PRECISION_SINGLE
#error MXU SIMD quantization only supports single precision float.
#endif

#include "mxu2_shim.h"
#include "mxu3_shim.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const mxu2_v4i32 zero_i = mxu2_li_w(0);
    const mxu2_v4f32 sfac = mxu2_mffpu_w(sfacfix);
    const mxu2_v4f32 magic = mxu2_mffpu_w(MAGIC_NUMBER);

    /* Generate 0x7FFFFFFF without 16-bit truncation issue in mxu2_li_w */
    mxu2_v4i32 all_ones = mxu2_ceq_w(zero_i, zero_i);
    mxu2_v4i32 abs_mask = mxu2_srli_w(all_ones, 1);

    for (; cnt <= n - 4; cnt += 4)
    {
        /* Pointers should be 16-byte aligned due to aligned AllocMemory */
        mxu2_v4i32 x_orig_i = mxu2_load(&xr[cnt]);

        mxu2_v4i32 sign_mask = mxu2_clts_w(x_orig_i, zero_i);
        mxu2_v4f32 x = (mxu2_v4f32)mxu2_andv((mxu2_v16i8)x_orig_i, (mxu2_v16i8)abs_mask);

        x = mxu2_fmul_w(x, sfac);
        /* (x * sfac)^0.75 approx: sqrt( (x*sfac) * sqrt(x*sfac) ) */
        mxu2_v4f32 sqrt_x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
        x = mxu2_fmul_w(x, sqrt_x);
        x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
        x = mxu2_fadd_w(x, magic);

        mxu2_v4i32 xi_vec = mxu2_vtruncsws((mxu2_v4i32)x);

        /* Bitwise Sign Fix: (val ^ mask) - mask */
        xi_vec = mxu2_sub_w(mxu2_xorv(xi_vec, sign_mask), sign_mask);

        mxu2_store(&xi[cnt], xi_vec);
    }

    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

/* Fast SIMD inverse square root for MXU3 using magic constant and one Newton-Raphson iteration */
static inline mxu3_v16i32 mxu3_rsqrt_w(mxu3_v16i32 x)
{
    const mxu3_v16i32 magic_const = mxu3_liw(0x5f3759df);
    const mxu3_v16i32 threehalfs = mxu3_liw(0x3fc00000); /* 1.5f */
    const mxu3_v16i32 half = mxu3_liw(0x3f000000);      /* 0.5f */

    mxu3_v16i32 i = mxu3_srliw(x, 1);
    i = mxu3_subw(magic_const, i);

    /* Newton-Raphson: y = y * (1.5 - (x2 * y * y)) */
    mxu3_v16i32 x2 = mxu3_fmulw(x, half);
    mxu3_v16i32 y2 = mxu3_fmulw(i, i);
    mxu3_v16i32 tmp = mxu3_fmulw(x2, y2);
    tmp = mxu3_fsubw(threehalfs, tmp);
    i = mxu3_fmulw(i, tmp);

    return i;
}

void quantize_mxu3(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;
    const mxu3_v16i32 zero = {0};

    /* Pre-broadcast constants using MXU3_LIW */
    mxu3_v16i32 sfac = mxu3_liw(*(uint32_t*)&sfacfix);
    float magic_f = (float)MAGIC_NUMBER;
    mxu3_v16i32 magic = mxu3_liw(*(uint32_t*)&magic_f);
    mxu3_v16i32 abs_mask = mxu3_liw(0x7FFFFFFF);

    for (; cnt <= n - 16; cnt += 16)
    {
        /* Alignment Note:
         * MXU3 requires 64-byte alignment for MXU3_LOAD/STORE.
         * Buffer xr and xi are now 64-byte aligned via faac_aligned_alloc.
         * However, sfb offsets are not always multiple of 16 (for 64-byte).
         * We check runtime alignment and use direct SIMD if possible.
         */
        if (((uintptr_t)&xr[cnt] & 63) == 0 && ((uintptr_t)&xi[cnt] & 63) == 0) {
            mxu3_v16i32 x_orig = MXU3_LOAD(&xr[cnt]);

            mxu3_v16i32 sign_mask = mxu3_cltsw(x_orig, zero);
            mxu3_v16i32 x = mxu3_andv(x_orig, abs_mask);

            x = mxu3_fmulw(x, sfac);

            /* x^0.75 approx: sqrt(x * sqrt(x)) */
            /* sqrt(x) = x * rsqrt(x) */
            mxu3_v16i32 r1 = mxu3_rsqrt_w(x);
            mxu3_v16i32 s1 = mxu3_fmulw(x, r1); /* s1 = sqrt(x) */
            mxu3_v16i32 x_s1 = mxu3_fmulw(x, s1); /* x * sqrt(x) */
            mxu3_v16i32 r2 = mxu3_rsqrt_w(x_s1);
            x = mxu3_fmulw(x_s1, r2); /* x = sqrt(x * sqrt(x)) */

            x = mxu3_faddw(x, magic);
            mxu3_v16i32 xi_vec = mxu3_ftsiw(x);

            /* Bitwise Sign Fix: (val ^ mask) - mask */
            xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);

            MXU3_STORE(&xi[cnt], xi_vec);
        } else {
            /* Fallback to safe aligned stack buffers if alignment is lost */
            faac_real temp_xr[16] __attribute__((aligned(64)));
            memcpy(temp_xr, &xr[cnt], 64);
            mxu3_v16i32 x_orig = MXU3_LOAD(temp_xr);

            mxu3_v16i32 sign_mask = mxu3_cltsw(x_orig, zero);
            mxu3_v16i32 x = mxu3_andv(x_orig, abs_mask);

            x = mxu3_fmulw(x, sfac);

            mxu3_v16i32 r1 = mxu3_rsqrt_w(x);
            mxu3_v16i32 s1 = mxu3_fmulw(x, r1);
            mxu3_v16i32 x_s1 = mxu3_fmulw(x, s1);
            mxu3_v16i32 r2 = mxu3_rsqrt_w(x_s1);
            x = mxu3_fmulw(x_s1, r2);

            x = mxu3_faddw(x, magic);
            mxu3_v16i32 xi_vec = mxu3_ftsiw(x);

            xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);

            int temp_xi[16] __attribute__((aligned(64)));
            MXU3_STORE(temp_xi, xi_vec);
            memcpy(&xi[cnt], temp_xi, 64);
        }
    }

    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
